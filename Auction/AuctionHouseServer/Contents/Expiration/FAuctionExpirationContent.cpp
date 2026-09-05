#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Expiration/FAuctionExpirationContent.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/Session/FAuctionSessionRegistry.h"
#include "AuctionHouseServer/Service/FExpirationService.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"

#include <format>
namespace AuctionHouseServer::Contents
{
	namespace
	{
		constexpr std::size_t kMaxPendingExpirationSettlements = 100;

		RpcLib::Client::FOutboundRpcClient& GetRequiredCacheRpcClient(
			const std::shared_ptr<RpcLib::Client::FOutboundRpcClient>& cacheRpcClient)
		{
			if (cacheRpcClient == nullptr)
			{
				throw std::invalid_argument("cache RPC client is null.");
			}
			return *cacheRpcClient;
		}
	}

	FAuctionExpirationContent::FAuctionExpirationContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FAuctionSessionRegistry> sessionRegistry,
		Database::SAuctionDatabaseConfig databaseConfig,
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
		const RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId,
		const std::chrono::milliseconds cacheRpcTimeout,
		const std::uint32_t pollMilliseconds)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_sessionRegistry(std::move(sessionRegistry))
		, m_databaseConfig(std::move(databaseConfig))
		, m_pollInterval(std::max<std::uint32_t>(1, pollMilliseconds))
		, m_cacheRpcClient(std::move(cacheRpcClient))
		, m_cacheServerInstanceId(cacheServerInstanceId)
		, m_cacheRpcTimeout(cacheRpcTimeout)
		, m_rpcCommon(GetRequiredCacheRpcClient(m_cacheRpcClient).GetSessionRegistry(),
			  m_rpcDispatcher,
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetRequestIdGenerator(),
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetTransport(),
			  contentInstanceId,
			  kMaxPendingExpirationSettlements)
		, m_targetFps(std::clamp<std::uint32_t>(1000 / std::max<std::uint32_t>(1, pollMilliseconds), 1, 1000))
	{
		if (m_cacheServerInstanceId == 0 || m_cacheRpcTimeout.count() <= 0)
		{
			throw std::invalid_argument("invalid cache RPC target configuration.");
		}
	}

	ContentsRuntime::Core::FContentId FAuctionExpirationContent::GetContentId() const noexcept
	{
		return kExpirationContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuctionExpirationContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FAuctionExpirationContent::OnFrame(
		const int,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		m_rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
		if (!m_databaseConfig.enabled)
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now < m_nextPollTime)
		{
			return;
		}
		m_nextPollTime = now + m_pollInterval;
		if (!m_cacheRpcClient->IsReady())
		{
			Log(Foundation::ELogLevel::Warn, "expiration polling skipped because Cache RPC is not ready.");
			return;
		}

		const std::size_t pendingCount = m_rpcCommon.GetPendingCallCount();
		if (pendingCount >= kMaxPendingExpirationSettlements)
		{
			Log(Foundation::ELogLevel::Warn, "expiration polling skipped because pending RPC limit was reached. pending={}", pendingCount);
			return;
		}
		const auto available = static_cast<std::uint32_t>(kMaxPendingExpirationSettlements - pendingCount);

		Service::FExpirationService service(m_databaseConfig);
		std::vector<std::uint64_t> listingIds;
		std::string error;
		if (!service.GetCandidates(available, listingIds, error))
		{
			Log(Foundation::ELogLevel::Error, "expiration candidate query failed: " + error);
			return;
		}

		for (const auto listingId : listingIds)
		{
			Database::SExpirationPrepareResult prepared;
			error.clear();
			const Domain::EAuctionResultCode prepareCode = service.Prepare(listingId, prepared, error);
			if (prepareCode == Domain::EAuctionResultCode::ExpireNotAvailable)
			{
				continue;
			}
			if (prepareCode != Domain::EAuctionResultCode::Success)
			{
				Log(prepareCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
					"expiration prepare failed. listingId={} resultCode={} error={}",
					listingId,
					static_cast<std::uint16_t>(prepareCode),
					error);
				continue;
			}

			RpcLib::Protocol::FRpcTarget target;
			target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
			target.serverInstanceId = m_cacheServerInstanceId;
			target.routingKey = prepared.winnerUserId == 0 ? prepared.sellerUserId : prepared.winnerUserId;
			auto* const bridgePointer = &bridge;
			const auto callResult = m_rpcCommon.Call<Cache::Protocol::FSettleExpirationRpc>(
				target,
				m_cacheRpcTimeout,
				[this, listingId, prepared, bridgePointer](const Cache::Protocol::ECacheCommandResult commandResult,
					const Cache::Protocol::FExpirationSettlementResult& settlement)
				{
					Service::FExpirationService continuationService(m_databaseConfig);
					std::string continuationError;
					if (commandResult == Cache::Protocol::ECacheCommandResult::Success)
					{
						std::uint64_t listingVersion = 0;
						const Domain::EAuctionResultCode completionCode = continuationService.Complete(listingId,
							prepared.winnerUserId,
							prepared.finalPrice,
							prepared.preparedListingVersion,
							listingVersion,
							continuationError);
						if (completionCode != Domain::EAuctionResultCode::Success)
						{
							Log(Foundation::ELogLevel::Error,
								"expiration completion failed. listingId={} resultCode={} remainingState=SETTLING error={}",
								listingId,
								static_cast<std::uint16_t>(completionCode),
								continuationError);
							return;
						}

						if (prepared.winnerUserId != 0)
						{
							const auto winnerSessionId = m_sessionRegistry->GetSessionId(prepared.winnerUserId);
							if (winnerSessionId.has_value() && bridgePointer->IsSessionAlive(*winnerSessionId))
							{
								Generated::Auction::FAuctionWonNoti notification;
								notification.listingId = listingId;
								notification.bidId = prepared.highestBidId;
								notification.finalPrice = prepared.finalPrice;
								notification.itemMailId = settlement.itemMailId;
								ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *winnerSessionId, notification);
							}
						}
						Log(Foundation::ELogLevel::Info,
							"expiration settled. listingId={} winnerUserId={} finalPrice={} listingVersion={} itemMailId={} "
							"sellerMailId={} "
							"gameData=cache-rpc",
							listingId,
							prepared.winnerUserId,
							prepared.finalPrice,
							listingVersion,
							settlement.itemMailId,
							settlement.sellerMailId);
						return;
					}

					if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
					{
						Log(Foundation::ELogLevel::Error,
							"expiration Cache settlement outcome is unknown. listingId={} remainingState=SETTLING",
							listingId);
						return;
					}

					if (!continuationService.Revert(listingId, prepared.preparedListingVersion, continuationError))
					{
						Log(Foundation::ELogLevel::Error,
							"expiration Cache settlement failed and revert failed. listingId={} cacheResult={} remainingState=SETTLING "
							"error={}",
							listingId,
							static_cast<std::uint8_t>(commandResult),
							continuationError);
						return;
					}
					Log(Foundation::ELogLevel::Warn,
						"expiration Cache settlement rejected; listing reverted. listingId={} cacheResult={} remainingState=ACTIVE",
						listingId,
						static_cast<std::uint8_t>(commandResult));
				},
				[this, listingId, prepared](const RpcLib::Protocol::FRpcCallFailure& failure)
				{
					if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure))
					{
						Service::FExpirationService continuationService(m_databaseConfig);
						std::string revertError;
						if (continuationService.Revert(listingId, prepared.preparedListingVersion, revertError))
						{
							Log(Foundation::ELogLevel::Warn,
								"expiration Cache RPC was rejected before dispatch; listing reverted. listingId={} remainingState=ACTIVE",
								listingId);
							return;
						}
						Log(Foundation::ELogLevel::Error,
							"expiration Cache RPC was rejected before dispatch and revert failed. listingId={} remainingState=SETTLING "
							"error={}",
							listingId,
							revertError);
						return;
					}
					Log(Foundation::ELogLevel::Error,
						"expiration Cache RPC outcome is unknown. listingId={} remainingState=SETTLING error={} remoteCode={}",
						listingId,
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode));
				},
				target.routingKey,
				prepared.sellerUserId,
				prepared.winnerUserId,
				prepared.currencyId,
				prepared.finalPrice,
				prepared.itemInstanceId,
				prepared.itemDataId,
				prepared.quantity,
				prepared.itemDataJson);
			if (!callResult.accepted)
			{
				std::string revertError;
				if (service.Revert(listingId, prepared.preparedListingVersion, revertError))
				{
					Log(Foundation::ELogLevel::Warn,
						"expiration Cache RPC was not started; listing reverted. listingId={} remainingState=ACTIVE",
						listingId);
				}
				else
				{
					Log(Foundation::ELogLevel::Error,
						"expiration Cache RPC was not started and revert failed. listingId={} remainingState=SETTLING error={}",
						listingId,
						revertError);
				}
			}
			else
			{
				Log(Foundation::ELogLevel::Info,
					"Cache RPC started. operation=SettleExpiration listingId={} rpcRequestId={}",
					listingId,
					callResult.requestId);
			}
		}
	}

	void FAuctionExpirationContent::ProcessCacheRpcResponse(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcResponse& response)
	{
		const auto result = m_rpcCommon.ProcessResponse(rpcSessionId, response);
		if (result != RpcLib::Protocol::ERpcCompletionResult::Completed)
		{
			Log(Foundation::ELogLevel::Warn,
				"expiration Cache RPC response was not completed. rpcSessionId={} requestId={} result={}",
				rpcSessionId,
				response.requestId,
				static_cast<std::uint8_t>(result));
		}
	}

	void FAuctionExpirationContent::FailCacheRpcSession(
		const std::uint64_t rpcSessionId)
	{
		const std::size_t failedCount = m_rpcCommon.FailSession(rpcSessionId, RpcLib::Protocol::ERpcCallError::Disconnected);
		if (failedCount != 0)
		{
			Log(Foundation::ELogLevel::Warn,
				"pending expiration Cache RPC calls failed after disconnect. rpcSessionId={} count={}",
				rpcSessionId,
				failedCount);
		}
	}

	void FAuctionExpirationContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger)
		{
			m_logger->Log(level, "AuctionHouseServer", message);
		}
	}
}
