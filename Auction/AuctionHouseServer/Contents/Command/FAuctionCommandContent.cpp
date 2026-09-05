#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Command/FAuctionCommandContent.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/Response/FAuctionErrorResponseSender.h"
#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Domain/AuctionResultCode.h"
#include "AuctionHouseServer/Domain/AuctionState.h"
#include "AuctionHouseServer/Service/FBidRefundService.h"
#include "AuctionHouseServer/Service/FBidService.h"
#include "AuctionHouseServer/Service/FBuyoutService.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "AuctionHouseServer/Service/FListingCancelService.h"
#include "AuctionHouseServer/Service/FListingRegistrationService.h"
#include "AuctionHouseServer/Contents/Session/FAuctionSession.h"
#include "AuctionHouseServer/Contents/Session/FAuctionSessionRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"
#include "GameData/Item/FItemDataTable.h"
#include "GameData/InventoryPolicy/FInventoryPolicyTable.h"
#include "GameData/MailPolicy/FMailPolicyTable.h"

#include <Windows.h>

#include <format>
namespace AuctionHouseServer::Contents
{
	namespace
	{
		constexpr std::size_t kMaximumPortableFramedPacketBytes = 8u * 1024u;

		template <typename TPacket>
		bool IsWithinPortablePacketLimit(
			const TPacket& packet) noexcept
		{
			constexpr std::size_t headerBytes =
				sizeof(NetworkLib::Packet::Framing::SPacketHeader) + sizeof(NetworkLib::Packet::Framing::SContentHeader);
			return packet.GetEstimatedBodySize() <= kMaximumPortableFramedPacketBytes - headerBytes;
		}

		std::string_view GetResultCodeName(
			const Domain::EAuctionResultCode resultCode) noexcept
		{
			switch (resultCode)
			{
				case Domain::EAuctionResultCode::InvalidRequest:
					return "INVALID_REQUEST";
				case Domain::EAuctionResultCode::DatabaseUnavailable:
					return "DATABASE_UNAVAILABLE";
				case Domain::EAuctionResultCode::PartialCommit:
					return "PARTIAL_COMMIT";
				case Domain::EAuctionResultCode::InternalError:
					return "INTERNAL_ERROR";
				case Domain::EAuctionResultCode::InventoryItemNotFound:
					return "INVENTORY_ITEM_NOT_FOUND";
				case Domain::EAuctionResultCode::ItemNotTradable:
					return "ITEM_NOT_TRADABLE";
				case Domain::EAuctionResultCode::ItemVersionMismatch:
					return "ITEM_VERSION_MISMATCH";
				case Domain::EAuctionResultCode::ItemEquipped:
					return "ITEM_EQUIPPED";
				case Domain::EAuctionResultCode::ListingLimitExceeded:
					return "LISTING_LIMIT_EXCEEDED";
				default:
					return "AUCTION_ERROR";
			}
		}

		Domain::EAuctionResultCode ToAuctionResultCode(
			const Cache::Protocol::ECacheCommandResult result) noexcept
		{
			using Cache::Protocol::ECacheCommandResult;
			switch (result)
			{
				case ECacheCommandResult::Success:
					return Domain::EAuctionResultCode::Success;
				case ECacheCommandResult::InvalidArgument:
					return Domain::EAuctionResultCode::InvalidRequest;
				case ECacheCommandResult::NotFound:
					return Domain::EAuctionResultCode::InventoryItemNotFound;
				case ECacheCommandResult::ItemVersionMismatch:
					return Domain::EAuctionResultCode::ItemVersionMismatch;
				case ECacheCommandResult::ItemEquipped:
					return Domain::EAuctionResultCode::ItemEquipped;
				case ECacheCommandResult::InventoryFull:
					return Domain::EAuctionResultCode::InventoryFull;
				case ECacheCommandResult::ItemInstanceConflict:
					return Domain::EAuctionResultCode::ItemInstanceConflict;
				case ECacheCommandResult::CurrencyLimitExceeded:
					return Domain::EAuctionResultCode::CurrencyLimitExceeded;
				case ECacheCommandResult::MailAttachmentNotClaimable:
					return Domain::EAuctionResultCode::MailAttachmentNotClaimable;
				case ECacheCommandResult::InsufficientCurrency:
					return Domain::EAuctionResultCode::InsufficientCurrency;
				case ECacheCommandResult::OutcomeUnknown:
					return Domain::EAuctionResultCode::PartialCommit;
				default:
					return Domain::EAuctionResultCode::DatabaseUnavailable;
			}
		}

		Domain::EAuctionResultCode ToAuctionResultCode(
			const Cache::Protocol::ECacheQueryResult result) noexcept
		{
			switch (result)
			{
				case Cache::Protocol::ECacheQueryResult::Success:
					return Domain::EAuctionResultCode::Success;
				case Cache::Protocol::ECacheQueryResult::InvalidArgument:
					return Domain::EAuctionResultCode::InvalidRequest;
				case Cache::Protocol::ECacheQueryResult::NotFound:
					return Domain::EAuctionResultCode::InventoryItemNotFound;
				default:
					return Domain::EAuctionResultCode::DatabaseUnavailable;
			}
		}

		Database::SInventoryItem ToAuctionInventoryItem(
			const Cache::Protocol::FInventoryItemSnapshot& source)
		{
			Database::SInventoryItem item;
			item.itemInstanceId = source.itemInstanceId;
			item.itemDataId = source.itemDataId;
			item.quantity = source.quantity;
			item.equipped = source.equipped;
			item.tradable = source.tradable;
			item.itemDataJson = source.itemDataJson;
			item.str = source.strStat;
			item.dex = source.dexStat;
			item.intelligence = source.intStat;
			item.luk = source.lukStat;
			item.version = source.version;
			return item;
		}

		std::uint64_t GetUnixTimeMilliseconds() noexcept
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		}

		ContentsRuntime::Session::ERequestProcessingPolicy GetRequestProcessingPolicy(
			const std::uint16_t opcode) noexcept
		{
			if (opcode == Generated::Auction::FPingRq::kOpcode)
			{
				return ContentsRuntime::Session::ERequestProcessingPolicy::AllowedWhileBusy;
			}

			return ContentsRuntime::Session::ERequestProcessingPolicy::Exclusive;
		}

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

	FAuctionCommandContent::FAuctionCommandContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const std::uint32_t shardIndex,
		const std::uint32_t shardCount,
		const std::uint64_t maxPacketQueueDepth,
		const std::uint32_t testDelayShardIndex,
		const std::uint32_t testDelayMilliseconds,
		const bool faultInjectionAfterAuctionCommit,
		const bool faultInjectionBidRefundBeforeComplete,
		std::shared_ptr<FAuctionSessionRegistry> sessionRegistry,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
		std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
		std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
		Database::SAuctionDatabaseConfig databaseConfig,
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
		const RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId,
		const std::chrono::milliseconds cacheRpcTimeout)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_shardIndex(shardIndex)
		, m_shardCount(shardCount)
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
		, m_testDelayShardIndex(testDelayShardIndex)
		, m_testDelayMilliseconds(testDelayMilliseconds)
		, m_faultInjectionAfterAuctionCommit(faultInjectionAfterAuctionCommit)
		, m_faultInjectionBidRefundBeforeComplete(faultInjectionBidRefundBeforeComplete)
		, m_sessionRegistry(std::move(sessionRegistry))
		, m_itemDataTable(std::move(itemDataTable))
		, m_auctionPolicyTable(std::move(auctionPolicyTable))
		, m_inventoryPolicyTable(std::move(inventoryPolicyTable))
		, m_mailPolicyTable(std::move(mailPolicyTable))
		, m_databaseConfig(std::move(databaseConfig))
		, m_cacheRpcClient(std::move(cacheRpcClient))
		, m_cacheServerInstanceId(cacheServerInstanceId)
		, m_cacheRpcTimeout(cacheRpcTimeout)
		, m_rpcCommon(GetRequiredCacheRpcClient(m_cacheRpcClient).GetSessionRegistry(),
			  m_rpcDispatcher,
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetRequestIdGenerator(),
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetTransport(),
			  contentInstanceId)
	{
		if (m_auctionPolicyTable == nullptr || m_inventoryPolicyTable == nullptr || m_mailPolicyTable == nullptr)
		{
			throw std::invalid_argument("auction command policy table is null.");
		}
		if (m_cacheServerInstanceId == 0 || m_cacheRpcTimeout.count() <= 0)
		{
			throw std::invalid_argument("invalid cache RPC target configuration.");
		}
	}

	ContentsRuntime::Core::FContentId FAuctionCommandContent::GetContentId() const noexcept
	{
		return kCommandContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuctionCommandContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint64_t FAuctionCommandContent::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FAuctionCommandContent::OnEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		Log(Foundation::ELogLevel::Info,
			"session entered command content. sessionId={} routeGeneration={} contentInstanceId={}",
			sessionId,
			routeGeneration,
			m_contentInstanceId);
	}

	void FAuctionCommandContent::OnLeave(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		Log(Foundation::ELogLevel::Info,
			"session left command content. sessionId={} routeGeneration={} contentInstanceId={}",
			sessionId,
			routeGeneration,
			m_contentInstanceId);
	}

	void FAuctionCommandContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		NetworkLib::Packet::Serialization::FPacketReader requestIdReader(payload.data(), payload.size());
		std::uint64_t requestId = 0;
		if (!requestIdReader.Read(requestId))
		{
			Log(Foundation::ELogLevel::Warn, "request id deserialize failed. sessionId={} opcode={}", sessionId, opcode);
			return;
		}

		const std::shared_ptr<FAuctionSession> session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr || !session->IsConnected())
		{
			Log(Foundation::ELogLevel::Warn, "command packet rejected for missing session. sessionId={} opcode={}", sessionId, opcode);
			return;
		}

		ContentsRuntime::Session::FRequestProcessingToken requestToken;
		const auto beginResult = session->TryBeginRequest(requestId, opcode, GetRequestProcessingPolicy(opcode), requestToken);
		if (beginResult == ContentsRuntime::Session::EBeginRequestResult::AllowedWithoutTracking)
		{
			HandlePing(sessionId, payload, bridge);
			return;
		}

		if (beginResult == ContentsRuntime::Session::EBeginRequestResult::AlreadyProcessing)
		{
			if (!FAuctionErrorResponseSender::Send(
					bridge, sessionId, opcode, payload, Domain::EAuctionResultCode::RequestInProgress, "request already processing"))
			{
				Log(Foundation::ELogLevel::Error, "request-in-progress response send failed. sessionId={} opcode={}", sessionId, opcode);
			}

			Log(Foundation::ELogLevel::Warn,
				"request rejected while another request is processing. sessionId={} opcode={} activeOpcode={} activeRequestId={}",
				sessionId,
				opcode,
				session->GetActiveRequestOpcode(),
				session->GetActiveRequestId());
			return;
		}
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::Started)
		{
			return;
		}

		ContentsRuntime::Session::FContentRequestContext requestContext(*session, requestToken);
		if (opcode == Generated::Auction::FMyBidListRq::kOpcode)
		{
			HandleMyBidList(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FInventoryListRq::kOpcode)
		{
			HandleInventoryList(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingRegisterRq::kOpcode)
		{
			HandleListingRegister(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingSearchRq::kOpcode)
		{
			HandleListingSearch(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingDetailRq::kOpcode)
		{
			HandleListingDetail(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBidRq::kOpcode)
		{
			HandleBid(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBuyoutRq::kOpcode)
		{
			HandleBuyout(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailListRq::kOpcode)
		{
			HandleMailList(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailDetailRq::kOpcode)
		{
			HandleMailDetail(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailClaimRq::kOpcode)
		{
			HandleMailClaim(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingCancelRq::kOpcode)
		{
			HandleListingCancel(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBidRefundRq::kOpcode)
		{
			HandleBidRefund(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FSaleHistorySearchRq::kOpcode)
		{
			HandleSaleHistorySearch(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FSaleHistoryDetailRq::kOpcode)
		{
			HandleSaleHistoryDetail(requestContext, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FDebugCheatRq::kOpcode)
		{
			HandleDebugCheat(requestContext, payload, bridge);
			return;
		}

		Log(Foundation::ELogLevel::Warn, "unhandled command packet. sessionId={} opcode={}", sessionId, opcode);
	}

	void FAuctionCommandContent::OnFrame(
		const int,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
	}

	void FAuctionCommandContent::ProcessCacheRpcResponse(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcResponse& response)
	{
		const auto result = m_rpcCommon.ProcessResponse(rpcSessionId, response);
		if (result != RpcLib::Protocol::ERpcCompletionResult::Completed)
		{
			Log(Foundation::ELogLevel::Warn,
				"cache RPC response was not completed. rpcSessionId={} requestId={} result={}",
				rpcSessionId,
				response.requestId,
				static_cast<std::uint8_t>(result));
		}
	}

	void FAuctionCommandContent::FailCacheRpcSession(
		const std::uint64_t rpcSessionId)
	{
		const std::size_t failedCount = m_rpcCommon.FailSession(rpcSessionId, RpcLib::Protocol::ERpcCallError::Disconnected);
		if (failedCount != 0)
		{
			Log(Foundation::ELogLevel::Warn,
				"pending cache RPC calls failed after disconnect. rpcSessionId={} count={}",
				rpcSessionId,
				failedCount);
		}
	}

	void FAuctionCommandContent::HandleListingSearch(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FListingSearchRq request;
		const std::uint32_t maximumFetchSize = m_auctionPolicyTable->Get().searchPageSize + 1;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingSearchRq::kOpcode, payload, request) ||
			request.itemCategory > 3 || request.itemDataIds.size() > 100 || request.sellerOnly > 1 ||
			!Domain::IsValidListingSearchSortType(request.sortType) || request.limit == 0 || request.limit > maximumFetchSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingSearch request.");
			return;
		}

		Generated::Auction::FListingSearchRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Database::SListingSearchQuery query;
		query.itemCategory = request.itemCategory;
		query.itemDataIds = request.itemDataIds;
		query.minStr = request.minStr;
		query.minDex = request.minDex;
		query.minInt = request.minInt;
		query.minLuk = request.minLuk;
		query.sellerUserId = request.sellerOnly != 0 ? *authenticatedUserId : 0;
		query.sortType = request.sortType;
		query.cursorSortValue = request.cursorSortValue;
		query.cursorListingId = request.cursorListingId;
		query.limit = request.limit;

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SListingSummary> listings;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).SearchListings(query, listings, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "ListingSearch failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& listing : listings)
			{
				response.listingIds.push_back(listing.listingId);
				response.sellerUserIds.push_back(listing.sellerUserId);
				response.sellerLoginIds.push_back(listing.sellerLoginId);
				response.itemDataIds.push_back(listing.itemDataId);
				response.itemCategories.push_back(listing.itemCategory);
				response.quantities.push_back(listing.quantity);
				response.names.push_back(listing.name);
				response.strStats.push_back(listing.str);
				response.dexStats.push_back(listing.dex);
				response.intStats.push_back(listing.intelligence);
				response.lukStats.push_back(listing.luk);
				response.currencyIds.push_back(listing.currencyId);
				response.startPrices.push_back(listing.startPrice);
				response.currentBidPrices.push_back(listing.currentBidPrice);
				response.buyoutPrices.push_back(listing.buyoutPrice);
				response.expiresAtUnixMs.push_back(listing.expiresAtUnixMs);
				response.versions.push_back(listing.version);
			}
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response))
		{
			Log(Foundation::ELogLevel::Error, "ListingSearch response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"ListingSearch completed. userId={} itemCategory={} itemDataIdCount={} sortType={} count={} source={} workerNumber={} "
				"replicaNumber={}",
				*authenticatedUserId,
				static_cast<unsigned int>(request.itemCategory),
				request.itemDataIds.size(),
				static_cast<unsigned int>(request.sortType),
				listings.size(),
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleDebugCheat(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FDebugCheatRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FDebugCheatRq::kOpcode, payload, request) ||
			(request.cheatType != 1 && request.cheatType != 2))
		{
			Log(Foundation::ELogLevel::Warn, "invalid DebugCheat request.");
			return;
		}

		Generated::Auction::FDebugCheatRp response;
		response.requestId = request.requestId;
		response.cheatType = request.cheatType;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			response.message = "authentication required";
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto* itemData = request.cheatType == 2 && m_itemDataTable != nullptr ? m_itemDataTable->Find(request.itemDataId) : nullptr;
		if ((request.cheatType == 1 && request.amount == 0) || (request.cheatType == 2 && itemData == nullptr))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
			response.message = request.cheatType == 1 ? "gold amount must be greater than zero" : "unknown ItemDataId";
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t userId = *authenticatedUserId;
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = userId;

		if (request.cheatType == 1)
		{
			const auto callResult = m_rpcCommon.Call<Cache::Protocol::FCreditCurrencyRpc>(
				target,
				m_cacheRpcTimeout,
				[this, session, requestToken, bridgePointer, response, userId, amount = request.amount](
					const Cache::Protocol::ECacheCommandResult result, const Cache::Protocol::FCurrencyBalance& balance) mutable
				{
					response.resultCode = static_cast<std::uint16_t>(ToAuctionResultCode(result));
					response.currencyBalance = balance.amount;
					response.message = result == Cache::Protocol::ECacheCommandResult::Success ? std::format("gold credited: {}", amount)
																							   : "cache currency mutation failed";
					ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
					Log(result == Cache::Protocol::ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
						"DebugCheat completed. userId={} cheatType=1 result={} source=cache-rpc",
						userId,
						static_cast<std::uint8_t>(result));
				},
				[this, session, requestToken, bridgePointer, response, userId](const RpcLib::Protocol::FRpcCallFailure& failure) mutable
				{
					response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
					response.message = "cache RPC failed";
					ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
					Log(Foundation::ELogLevel::Error,
						"DebugCheat cache RPC failed. userId={} cheatType=1 error={} remoteCode={}",
						userId,
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode));
				},
				userId,
				static_cast<std::uint16_t>(m_auctionPolicyTable->Get().defaultCurrencyDataId),
				request.amount);
			if (!callResult.accepted)
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				response.message = "cache RPC unavailable";
				ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			}
			return;
		}

		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FGrantInventoryItemRpc>(
			target,
			m_cacheRpcTimeout,
			[this, session, requestToken, bridgePointer, response, userId, itemDataId = request.itemDataId](
				const Cache::Protocol::ECacheCommandResult result, const Cache::Protocol::FInventoryItemSnapshot& item) mutable
			{
				response.resultCode = static_cast<std::uint16_t>(ToAuctionResultCode(result));
				response.itemInstanceId = item.itemInstanceId;
				response.message = result == Cache::Protocol::ECacheCommandResult::Success
									   ? std::format("item created: ItemDataId={} instanceId={}", itemDataId, item.itemInstanceId)
									   : "cache inventory mutation failed";
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
				Log(result == Cache::Protocol::ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
					"DebugCheat completed. userId={} cheatType=2 result={} source=cache-rpc",
					userId,
					static_cast<std::uint8_t>(result));
			},
			[this, session, requestToken, bridgePointer, response, userId](const RpcLib::Protocol::FRpcCallFailure& failure) mutable
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				response.message = "cache RPC failed";
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
				Log(Foundation::ELogLevel::Error,
					"DebugCheat cache RPC failed. userId={} cheatType=2 error={} remoteCode={}",
					userId,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			userId,
			request.itemDataId,
			std::uint32_t{1},
			itemData->maxStack,
			request.strStat,
			request.dexStat,
			request.intStat,
			request.lukStat,
			itemData->tradable);
		if (!callResult.accepted)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			response.message = "cache RPC unavailable";
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
		}
	}

	void FAuctionCommandContent::HandleListingDetail(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FListingDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingDetailRq::kOpcode, payload, request) ||
			request.listingId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingDetail request.");
			return;
		}

		Generated::Auction::FListingDetailRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Database::SListingDetail listing;
		bool found = false;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).GetListingDetail(request.listingId, listing, found, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "ListingDetail failed: " + error);
		}
		else if (!found)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ListingNotFound);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			response.sellerUserId = listing.sellerUserId;
			response.sellerLoginId = listing.sellerLoginId;
			response.itemInstanceId = listing.itemInstanceId;
			response.itemDataId = listing.itemDataId;
			response.itemCategory = listing.itemCategory;
			response.quantity = listing.quantity;
			response.itemData = listing.itemDataJson;
			response.name = listing.name;
			response.strStat = listing.str;
			response.dexStat = listing.dex;
			response.intStat = listing.intelligence;
			response.lukStat = listing.luk;
			response.currencyId = listing.currencyId;
			response.startPrice = listing.startPrice;
			response.currentBidPrice = listing.currentBidPrice;
			response.buyoutPrice = listing.buyoutPrice;
			response.highestBidderUserId = listing.highestBidderUserId;
			response.expiresAtUnixMs = listing.expiresAtUnixMs;
			response.version = listing.version;
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response))
		{
			Log(Foundation::ELogLevel::Error, "ListingDetail response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"ListingDetail completed. userId={} listingId={} source={} workerNumber={} replicaNumber={}",
				*authenticatedUserId,
				request.listingId,
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleSaleHistorySearch(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FSaleHistorySearchRq request;
		const std::uint32_t maximumFetchSize = m_auctionPolicyTable->Get().searchPageSize + 1;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FSaleHistorySearchRq::kOpcode, payload, request) ||
			request.itemCategory > 3 || request.itemDataIds.size() > 100 || !Domain::IsValidSaleHistorySortType(request.sortType) ||
			request.limit == 0 || request.limit > maximumFetchSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid SaleHistorySearch request.");
			return;
		}

		Generated::Auction::FSaleHistorySearchRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Database::SSaleHistoryQuery query;
		query.itemCategory = request.itemCategory;
		query.itemDataIds = request.itemDataIds;
		query.minStr = request.minStr;
		query.minDex = request.minDex;
		query.minInt = request.minInt;
		query.minLuk = request.minLuk;
		query.sortType = request.sortType;
		query.cursorSortValue = request.cursorSortValue;
		query.cursorListingId = request.cursorListingId;
		query.limit = request.limit;

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SSaleHistorySummary> history;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).SearchSaleHistory(query, history, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "SaleHistorySearch failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& sale : history)
			{
				response.listingIds.push_back(sale.listingId);
				response.itemDataIds.push_back(sale.itemDataId);
				response.itemCategories.push_back(sale.itemCategory);
				response.quantities.push_back(sale.quantity);
				response.names.push_back(sale.name);
				response.strStats.push_back(sale.str);
				response.dexStats.push_back(sale.dex);
				response.intStats.push_back(sale.intelligence);
				response.lukStats.push_back(sale.luk);
				response.currencyIds.push_back(sale.currencyId);
				response.finalPrices.push_back(sale.finalPrice);
				response.saleTypes.push_back(sale.saleType);
				response.soldAtUnixMs.push_back(sale.soldAtUnixMs);
			}
		}

		ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
		if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"SaleHistorySearch completed. userId={} itemDataIdCount={} sortType={} count={} source={} workerNumber={} replicaNumber={}",
				*authenticatedUserId,
				request.itemDataIds.size(),
				static_cast<unsigned int>(request.sortType),
				history.size(),
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleSaleHistoryDetail(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FSaleHistoryDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FSaleHistoryDetailRq::kOpcode, payload, request) ||
			request.listingId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid SaleHistoryDetail request.");
			return;
		}

		Generated::Auction::FSaleHistoryDetailRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Database::SSaleHistoryDetail sale;
		bool found = false;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).GetSaleHistoryDetail(request.listingId, sale, found, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "SaleHistoryDetail failed: " + error);
		}
		else if (!found)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ListingNotFound);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			response.sellerLoginId = sale.sellerLoginId;
			response.itemDataId = sale.itemDataId;
			response.itemCategory = sale.itemCategory;
			response.quantity = sale.quantity;
			response.itemData = sale.itemDataJson;
			response.name = sale.name;
			response.strStat = sale.str;
			response.dexStat = sale.dex;
			response.intStat = sale.intelligence;
			response.lukStat = sale.luk;
			response.currencyId = sale.currencyId;
			response.startPrice = sale.startPrice;
			response.finalPrice = sale.finalPrice;
			response.saleType = sale.saleType;
			response.soldAtUnixMs = sale.soldAtUnixMs;
		}
		ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
	}

	void FAuctionCommandContent::HandleListingRegister(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FListingRegisterRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingRegisterRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingRegister request.");
			return;
		}

		Generated::Auction::FListingRegisterRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		const auto sellerLoginId = m_sessionRegistry->GetLoginId(sessionId);
		if (!authenticatedUserId.has_value() || !sellerLoginId.has_value() || sellerLoginId->empty())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		if (request.itemInstanceId == 0 || request.expectedItemVersion == 0)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t userId = *authenticatedUserId;
		const std::string loginId = *sellerLoginId;
		const auto complete = [this,
								  session,
								  requestToken,
								  bridgePointer,
								  clientRequestId = request.requestId,
								  userId,
								  itemInstanceId = request.itemInstanceId](const Domain::EAuctionResultCode resultCode,
								  const std::uint64_t listingId,
								  const Service::SListingRegistrationFailureContext& failure,
								  const std::string& error)
		{
			Generated::Auction::FListingRegisterRp rpcResponse;
			rpcResponse.requestId = clientRequestId;
			rpcResponse.listingId = listingId;
			rpcResponse.resultCode = static_cast<std::uint16_t>(resultCode);
			if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
			{
				Log(Foundation::ELogLevel::Error,
					"ListingRegister response send failed. userId={} listingId={} result={}",
					userId,
					listingId,
					static_cast<std::uint16_t>(resultCode));
			}

			if (resultCode == Domain::EAuctionResultCode::Success)
			{
				Log(Foundation::ELogLevel::Info,
					"ListingRegister completed. userId={} itemInstanceId={} listingId={} shardIndex={} gameData=cache-rpc",
					userId,
					itemInstanceId,
					listingId,
					m_shardIndex);
				return;
			}

			const auto level =
				resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn;
			Log(level,
				"Auction operation failed. operation=ListingRegister requestId={} userId={} itemInstanceId={} listingId={} "
				"result={}({}) failedStep={} auctionDbCommit={} cacheRpc={} activationCommit={} remainingState={} faultInjected={} "
				"error={}",
				clientRequestId,
				userId,
				itemInstanceId,
				listingId,
				GetResultCodeName(resultCode),
				static_cast<std::uint16_t>(resultCode),
				failure.failedStep,
				failure.auctionDbCommit,
				failure.cacheRpc,
				failure.activationCommit,
				failure.remainingListingState,
				failure.faultInjected,
				error);
		};

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = userId;
		const auto snapshotCall = m_rpcCommon.Call<Cache::Protocol::FGetInventoryItemRpc>(
			target,
			m_cacheRpcTimeout,
			[this, request, userId, loginId, target, complete](
				const Cache::Protocol::ECacheQueryResult queryResult, const Cache::Protocol::FInventoryItemSnapshot& snapshot)
			{
				if (queryResult != Cache::Protocol::ECacheQueryResult::Success)
				{
					Service::SListingRegistrationFailureContext failure;
					failure.failedStep = "CacheRpc.GetInventoryItem";
					complete(ToAuctionResultCode(queryResult), 0, failure, "inventory item snapshot RPC failed.");
					return;
				}

				Service::FListingRegistrationService service(m_databaseConfig, m_itemDataTable, m_auctionPolicyTable);
				Service::SListingRegistrationFailureContext failure;
				Database::SListingPrepareResult prepareResult;
				std::string error;
				const Database::SInventoryItem inventoryItem = ToAuctionInventoryItem(snapshot);
				const Domain::EAuctionResultCode prepareCode = service.Prepare(userId,
					loginId,
					inventoryItem,
					request.expectedItemVersion,
					request.currencyId,
					request.startPrice,
					request.buyoutPrice,
					request.durationSeconds,
					prepareResult,
					failure,
					error);
				if (prepareCode != Domain::EAuctionResultCode::Success)
				{
					complete(prepareCode, prepareResult.listingId, failure, error);
					return;
				}

				if (m_faultInjectionAfterAuctionCommit)
				{
					failure.failedStep = "BeforeCacheRpc.ConsumeInventoryItem";
					failure.cacheRpc = "NOT_ATTEMPTED";
					failure.remainingListingState = "REGISTER_PENDING";
					failure.faultInjected = true;
					complete(Domain::EAuctionResultCode::PartialCommit,
						prepareResult.listingId,
						failure,
						"fault injection requested after AuctionDB commit.");
					return;
				}

				const auto consumeCall = m_rpcCommon.Call<Cache::Protocol::FConsumeInventoryItemForListingRpc>(
					target,
					m_cacheRpcTimeout,
					[this, userId, prepareResult, failure, complete](
						const Cache::Protocol::ECacheCommandResult commandResult, const Cache::Protocol::FInventoryItemSnapshot&) mutable
					{
						Service::FListingRegistrationService continuationService(m_databaseConfig, m_itemDataTable, m_auctionPolicyTable);
						std::string continuationError;
						if (commandResult != Cache::Protocol::ECacheCommandResult::Success)
						{
							failure.failedStep = "CacheRpc.ConsumeInventoryItem";
							failure.cacheRpc = commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown ? "UNKNOWN" : "FAILED";
							if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
							{
								complete(Domain::EAuctionResultCode::PartialCommit,
									prepareResult.listingId,
									failure,
									"Cache RPC commit outcome is unknown; pending listing was preserved.");
								return;
							}

							if (!continuationService.DeletePending(
									userId, prepareResult.listingId, prepareResult.version, continuationError))
							{
								failure.remainingListingState = "REGISTER_PENDING_OR_REMOVED";
								complete(Domain::EAuctionResultCode::PartialCommit, prepareResult.listingId, failure, continuationError);
								return;
							}
							failure.remainingListingState = "NONE";
							complete(
								ToAuctionResultCode(commandResult), prepareResult.listingId, failure, "inventory consume was rejected.");
							return;
						}

						failure.cacheRpc = "SUCCEEDED";
						const Domain::EAuctionResultCode activateCode =
							continuationService.Activate(prepareResult.listingId, prepareResult.version, failure, continuationError);
						complete(activateCode, prepareResult.listingId, failure, continuationError);
					},
					[this, userId, complete, prepareResult, failure](const RpcLib::Protocol::FRpcCallFailure& rpcFailure) mutable
					{
						failure.failedStep = "CacheRpc.ConsumeInventoryItem";
						if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(rpcFailure))
						{
							Service::FListingRegistrationService continuationService(
								m_databaseConfig, m_itemDataTable, m_auctionPolicyTable);
							std::string revertError;
							failure.cacheRpc = "NOT_ATTEMPTED";
							if (continuationService.DeletePending(userId, prepareResult.listingId, prepareResult.version, revertError))
							{
								failure.remainingListingState = "NONE";
								complete(Domain::EAuctionResultCode::DatabaseUnavailable,
									prepareResult.listingId,
									failure,
									"Cache RPC was rejected before dispatch; pending listing was removed.");
								return;
							}
							failure.remainingListingState = "REGISTER_PENDING_OR_REMOVED";
							complete(Domain::EAuctionResultCode::PartialCommit, prepareResult.listingId, failure, revertError);
							return;
						}
						failure.cacheRpc = "UNKNOWN";
						failure.remainingListingState = "REGISTER_PENDING";
						complete(Domain::EAuctionResultCode::PartialCommit,
							prepareResult.listingId,
							failure,
							std::format("Cache RPC failed after listing prepare. error={} remoteCode={}",
								static_cast<std::uint8_t>(rpcFailure.error),
								static_cast<std::uint16_t>(rpcFailure.remoteResponseCode)));
					},
					userId,
					request.itemInstanceId,
					request.expectedItemVersion);
				if (!consumeCall.accepted)
				{
					failure.failedStep = "CacheRpc.ConsumeInventoryItem.Start";
					failure.cacheRpc = "NOT_ATTEMPTED";
					if (service.DeletePending(userId, prepareResult.listingId, prepareResult.version, error))
					{
						failure.remainingListingState = "NONE";
						complete(Domain::EAuctionResultCode::DatabaseUnavailable,
							prepareResult.listingId,
							failure,
							"Cache RPC was not started; pending listing was removed.");
					}
					else
					{
						failure.remainingListingState = "REGISTER_PENDING_OR_REMOVED";
						complete(Domain::EAuctionResultCode::PartialCommit, prepareResult.listingId, failure, error);
					}
				}
				else
				{
					Log(Foundation::ELogLevel::Info,
						"Cache RPC started. operation=ConsumeInventoryItem listingId={} rpcRequestId={}",
						prepareResult.listingId,
						consumeCall.requestId);
				}
			},
			[complete](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				Service::SListingRegistrationFailureContext operationFailure;
				operationFailure.failedStep = "CacheRpc.GetInventoryItem";
				complete(Domain::EAuctionResultCode::DatabaseUnavailable,
					0,
					operationFailure,
					std::format("inventory snapshot RPC failed. error={} remoteCode={}",
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode)));
			},
			userId,
			request.itemInstanceId);
		if (!snapshotCall.accepted)
		{
			Service::SListingRegistrationFailureContext failure;
			failure.failedStep = "CacheRpc.GetInventoryItem.Start";
			complete(Domain::EAuctionResultCode::DatabaseUnavailable, 0, failure, "inventory snapshot RPC was not started.");
		}
	}

	void FAuctionCommandContent::HandleInventoryList(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FInventoryListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FInventoryListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > m_inventoryPolicyTable->Get().inventoryListPageSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid InventoryList request.");
			return;
		}

		Generated::Auction::FInventoryListRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t userId = *authenticatedUserId;
		const std::uint64_t clientRequestId = request.requestId;
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = userId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FGetInventoryRpc>(
			target,
			m_cacheRpcTimeout,
			[this, session, requestToken, bridgePointer, clientRequestId, userId](
				const Cache::Protocol::ECacheQueryResult result, std::vector<Cache::Protocol::FInventoryItem> items)
			{
				Generated::Auction::FInventoryListRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
					for (const auto& item : items)
					{
						rpcResponse.itemInstanceIds.push_back(item.itemInstanceId);
						rpcResponse.itemDataIds.push_back(item.itemDataId);
						rpcResponse.quantities.push_back(item.quantity);
						rpcResponse.equippedStates.push_back(item.equipped ? 1 : 0);
						rpcResponse.tradableStates.push_back(item.tradable ? 1 : 0);
						rpcResponse.itemData.push_back(item.itemDataJson);
						rpcResponse.versions.push_back(item.version);
					}
				}
				else if (result == Cache::Protocol::ECacheQueryResult::InvalidArgument)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
				}
				else
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				}
				if (!IsWithinPortablePacketLimit(rpcResponse))
				{
					rpcResponse = {};
					rpcResponse.requestId = clientRequestId;
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
					Log(Foundation::ELogLevel::Error,
						"InventoryList client response exceeded the portable packet limit. userId={}",
						userId);
				}

				if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
				{
					Log(Foundation::ELogLevel::Error, "InventoryList RPC response send failed. userId={}", userId);
				}
				else if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					Log(Foundation::ELogLevel::Info,
						"InventoryList completed. userId={} shardIndex={} itemCount={} source=cache-rpc",
						userId,
						m_shardIndex,
						items.size());
				}
			},
			[this, session, requestToken, bridgePointer, clientRequestId, userId](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				Generated::Auction::FInventoryListRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse);
				Log(Foundation::ELogLevel::Error,
					"InventoryList cache RPC failed. userId={} error={} remoteCode={}",
					userId,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			userId,
			request.cursorItemInstanceId,
			request.limit);
		if (!callResult.accepted)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			Log(Foundation::ELogLevel::Error,
				"InventoryList cache RPC start failed. userId={} error={}",
				userId,
				static_cast<std::uint8_t>(callResult.error));
		}
	}

	void FAuctionCommandContent::HandleMyBidList(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FMyBidListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMyBidListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > m_auctionPolicyTable->Get().searchPageSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MyBidList request.");
			return;
		}

		Generated::Auction::FMyBidListRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}
		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SMyBid> bids;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection)
						.GetMyBids(*authenticatedUserId, request.cursorBidId, request.limit, bids, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "MyBidList failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& bid : bids)
			{
				response.bidIds.push_back(bid.bidId);
				response.listingIds.push_back(bid.listingId);
				response.currencyIds.push_back(bid.currencyId);
				response.bidAmounts.push_back(bid.bidAmount);
				response.bidStates.push_back(bid.bidState);
				response.bidVersions.push_back(bid.bidVersion);
				response.currentBidPrices.push_back(bid.currentBidPrice);
				response.listingStates.push_back(bid.listingState);
			}
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response))
		{
			Log(Foundation::ELogLevel::Error, "MyBidList response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"MyBidList completed. userId={} shardIndex={} bidCount={} source={}",
				*authenticatedUserId,
				m_shardIndex,
				bids.size(),
				(usedPrimary ? "primary" : "replica"));
		}
	}

	void FAuctionCommandContent::HandleBid(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FBidRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBidRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid Bid request.");
			return;
		}

		Generated::Auction::FBidRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Service::FBidService service(m_databaseConfig, m_auctionPolicyTable->Get().minimumBidIncrement);
		Database::SBidPrepareResult prepared;
		std::string error;
		const Domain::EAuctionResultCode prepareCode =
			service.Prepare(*authenticatedUserId, request.listingId, request.bidAmount, request.expectedListingVersion, prepared, error);
		if (prepareCode != Domain::EAuctionResultCode::Success)
		{
			response.resultCode = static_cast<std::uint16_t>(prepareCode);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			Log(prepareCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
				"Bid prepare failed. userId={} listingId={} result={} error={}",
				*authenticatedUserId,
				request.listingId,
				static_cast<std::uint16_t>(prepareCode),
				error);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t bidderUserId = *authenticatedUserId;
		const auto complete = [this, session, requestToken, bridgePointer, request, bidderUserId, prepared](
								  const Domain::EAuctionResultCode resultCode,
								  const Cache::Protocol::FCurrencyBalance& balance,
								  const std::uint64_t bidId,
								  const std::uint64_t listingVersion,
								  const std::string& operationError)
		{
			Generated::Auction::FBidRp rpcResponse;
			rpcResponse.requestId = request.requestId;
			rpcResponse.listingId = request.listingId;
			rpcResponse.resultCode = static_cast<std::uint16_t>(resultCode);
			rpcResponse.bidId = bidId;
			rpcResponse.bidAmount = request.bidAmount;
			rpcResponse.additionalDebit = prepared.additionalDebit;
			rpcResponse.currencyBalance = balance.amount;
			rpcResponse.listingVersion = listingVersion;
			if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
			{
				Log(Foundation::ELogLevel::Error, "Bid response send failed. userId={} listingId={}", bidderUserId, request.listingId);
			}

			if (resultCode != Domain::EAuctionResultCode::Success)
			{
				Log(resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
					"Bid failed. userId={} listingId={} result={} remainingState={} error={}",
					bidderUserId,
					request.listingId,
					static_cast<std::uint16_t>(resultCode),
					resultCode == Domain::EAuctionResultCode::PartialCommit ? "BID_PENDING" : "ACTIVE",
					operationError);
				return;
			}

			if (prepared.previousHighestBidderUserId != 0 && prepared.previousHighestBidderUserId != bidderUserId)
			{
				const auto previousSessionId = m_sessionRegistry->GetSessionId(prepared.previousHighestBidderUserId);
				if (previousSessionId.has_value() && bridgePointer->IsSessionAlive(*previousSessionId))
				{
					Generated::Auction::FAuctionOutbidNoti notification;
					notification.listingId = request.listingId;
					notification.bidId = prepared.previousHighestBidId;
					notification.heldAmount = prepared.previousHighestAmount;
					notification.newHighestAmount = request.bidAmount;
					if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *previousSessionId, notification))
					{
						Log(Foundation::ELogLevel::Warn, "online outbid notification send failed.");
					}
				}
			}

			Log(Foundation::ELogLevel::Info,
				"Bid completed. userId={} listingId={} bidId={} amount={} previousHighestUserId={} gameData=cache-rpc",
				bidderUserId,
				request.listingId,
				bidId,
				request.bidAmount,
				prepared.previousHighestBidderUserId);
		};

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = bidderUserId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FDebitCurrencyRpc>(
			target,
			m_cacheRpcTimeout,
			[this, bidderUserId, request, prepared, complete](
				const Cache::Protocol::ECacheCommandResult commandResult, const Cache::Protocol::FCurrencyBalance& balance)
			{
				Service::FBidService continuationService(m_databaseConfig, m_auctionPolicyTable->Get().minimumBidIncrement);
				std::string continuationError;
				if (commandResult == Cache::Protocol::ECacheCommandResult::Success)
				{
					std::uint64_t bidId = 0;
					std::uint64_t listingVersion = 0;
					const Domain::EAuctionResultCode completionCode = continuationService.Complete(bidderUserId,
						request.listingId,
						request.bidAmount,
						prepared.preparedListingVersion,
						bidId,
						listingVersion,
						continuationError);
					complete(completionCode, balance, bidId, listingVersion, continuationError);
					return;
				}
				if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						balance,
						0,
						0,
						"Cache debit commit outcome is unknown; BID_PENDING was preserved.");
					return;
				}
				if (!continuationService.Revert(request.listingId, prepared.preparedListingVersion, continuationError))
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						balance,
						0,
						0,
						"Cache debit was rejected but BID_PENDING revert failed: " + continuationError);
					return;
				}
				complete(ToAuctionResultCode(commandResult), balance, 0, 0, "Cache debit was rejected; BID_PENDING was reverted.");
			},
			[this, request, prepared, complete](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure))
				{
					Service::FBidService continuationService(m_databaseConfig, m_auctionPolicyTable->Get().minimumBidIncrement);
					std::string revertError;
					const bool reverted = continuationService.Revert(request.listingId, prepared.preparedListingVersion, revertError);
					complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
						{},
						0,
						0,
						reverted ? "Cache debit RPC was rejected before dispatch; BID_PENDING was reverted."
								 : "Cache debit RPC was rejected before dispatch and BID_PENDING revert failed: " + revertError);
					return;
				}
				complete(Domain::EAuctionResultCode::PartialCommit,
					{},
					0,
					0,
					std::format("Cache debit RPC outcome is unknown; BID_PENDING was preserved. error={} remoteCode={}",
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode)));
			},
			bidderUserId,
			prepared.currencyId,
			prepared.additionalDebit);
		if (!callResult.accepted)
		{
			std::string revertError;
			const bool reverted = service.Revert(request.listingId, prepared.preparedListingVersion, revertError);
			complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
				{},
				0,
				0,
				reverted ? "Cache debit RPC was not started; BID_PENDING was reverted."
						 : "Cache debit RPC was not started and BID_PENDING revert failed: " + revertError);
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"Cache RPC started. operation=DebitCurrency listingId={} rpcRequestId={}",
				request.listingId,
				callResult.requestId);
		}
	}

	void FAuctionCommandContent::HandleBuyout(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FBuyoutRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBuyoutRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid Buyout request.");
			return;
		}

		Generated::Auction::FBuyoutRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Service::FBuyoutService service(m_databaseConfig);
		Database::SBuyoutPrepareResult prepared;
		std::string error;
		const Domain::EAuctionResultCode prepareCode =
			service.Prepare(*authenticatedUserId, request.listingId, request.expectedListingVersion, prepared, error);
		if (prepareCode != Domain::EAuctionResultCode::Success)
		{
			response.resultCode = static_cast<std::uint16_t>(prepareCode);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			Log(prepareCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
				"Buyout prepare failed. userId={} listingId={} result={} error={}",
				*authenticatedUserId,
				request.listingId,
				static_cast<std::uint16_t>(prepareCode),
				error);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t buyerUserId = *authenticatedUserId;
		const auto complete = [this, session, requestToken, bridgePointer, request, buyerUserId, prepared](
								  const Domain::EAuctionResultCode resultCode,
								  const Cache::Protocol::FBuyoutSettlementResult& settlement,
								  const std::uint64_t listingVersion,
								  const std::string& operationError)
		{
			Generated::Auction::FBuyoutRp rpcResponse;
			rpcResponse.requestId = request.requestId;
			rpcResponse.listingId = request.listingId;
			rpcResponse.resultCode = static_cast<std::uint16_t>(resultCode);
			rpcResponse.buyoutPrice = prepared.buyoutPrice;
			rpcResponse.additionalDebit = prepared.additionalDebit;
			rpcResponse.currencyBalance = settlement.currencyBalance.amount;
			rpcResponse.itemMailId = settlement.itemMailId;
			rpcResponse.sellerMailId = settlement.sellerMailId;
			rpcResponse.listingVersion = listingVersion;
			if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
			{
				Log(Foundation::ELogLevel::Error, "Buyout response send failed. userId={} listingId={}", buyerUserId, request.listingId);
			}

			if (resultCode != Domain::EAuctionResultCode::Success)
			{
				Log(resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
					"Buyout failed. userId={} listingId={} result={} remainingState={} error={}",
					buyerUserId,
					request.listingId,
					static_cast<std::uint16_t>(resultCode),
					resultCode == Domain::EAuctionResultCode::PartialCommit ? "BUYOUT_PENDING" : "ACTIVE",
					operationError);
				return;
			}

			if (prepared.previousHighestBidderUserId != 0 && prepared.previousHighestBidderUserId != buyerUserId)
			{
				const auto previousSessionId = m_sessionRegistry->GetSessionId(prepared.previousHighestBidderUserId);
				if (previousSessionId.has_value() && bridgePointer->IsSessionAlive(*previousSessionId))
				{
					Generated::Auction::FAuctionOutbidNoti notification;
					notification.listingId = request.listingId;
					notification.bidId = prepared.previousHighestBidId;
					notification.heldAmount = prepared.previousHighestAmount;
					notification.newHighestAmount = prepared.buyoutPrice;
					ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *previousSessionId, notification);
				}
			}

			Log(Foundation::ELogLevel::Info,
				"Buyout completed. userId={} listingId={} price={} itemMailId={} sellerMailId={} gameData=cache-rpc",
				buyerUserId,
				request.listingId,
				prepared.buyoutPrice,
				settlement.itemMailId,
				settlement.sellerMailId);
		};

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = buyerUserId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FSettleBuyoutRpc>(
			target,
			m_cacheRpcTimeout,
			[this, buyerUserId, request, prepared, complete](
				const Cache::Protocol::ECacheCommandResult commandResult, const Cache::Protocol::FBuyoutSettlementResult& settlement)
			{
				Service::FBuyoutService continuationService(m_databaseConfig);
				std::string continuationError;
				if (commandResult == Cache::Protocol::ECacheCommandResult::Success)
				{
					std::uint64_t listingVersion = 0;
					const Domain::EAuctionResultCode completionCode = continuationService.Complete(
						buyerUserId, request.listingId, prepared.preparedListingVersion, listingVersion, continuationError);
					complete(completionCode, settlement, listingVersion, continuationError);
					return;
				}
				if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						settlement,
						0,
						"Cache buyout settlement outcome is unknown; BUYOUT_PENDING was preserved.");
					return;
				}
				if (!continuationService.Revert(request.listingId, prepared.preparedListingVersion, continuationError))
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						settlement,
						0,
						"Cache buyout settlement was rejected but BUYOUT_PENDING revert failed: " + continuationError);
					return;
				}
				complete(ToAuctionResultCode(commandResult),
					settlement,
					0,
					"Cache buyout settlement was rejected; BUYOUT_PENDING was reverted.");
			},
			[this, request, prepared, complete](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure))
				{
					Service::FBuyoutService continuationService(m_databaseConfig);
					std::string revertError;
					const bool reverted = continuationService.Revert(request.listingId, prepared.preparedListingVersion, revertError);
					complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
						{},
						0,
						reverted ? "Cache buyout RPC was rejected before dispatch; BUYOUT_PENDING was reverted."
								 : "Cache buyout RPC was rejected before dispatch and BUYOUT_PENDING revert failed: " + revertError);
					return;
				}
				complete(Domain::EAuctionResultCode::PartialCommit,
					{},
					0,
					std::format("Cache buyout RPC outcome is unknown; BUYOUT_PENDING was preserved. error={} remoteCode={}",
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode)));
			},
			buyerUserId,
			prepared.sellerUserId,
			prepared.currencyId,
			prepared.additionalDebit,
			prepared.buyoutPrice,
			prepared.itemInstanceId,
			prepared.itemDataId,
			prepared.quantity,
			prepared.itemDataJson);
		if (!callResult.accepted)
		{
			std::string revertError;
			const bool reverted = service.Revert(request.listingId, prepared.preparedListingVersion, revertError);
			complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
				{},
				0,
				reverted ? "Cache buyout RPC was not started; BUYOUT_PENDING was reverted."
						 : "Cache buyout RPC was not started and BUYOUT_PENDING revert failed: " + revertError);
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"Cache RPC started. operation=SettleBuyout listingId={} rpcRequestId={}",
				request.listingId,
				callResult.requestId);
		}
	}

	void FAuctionCommandContent::HandleMailList(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FMailListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > m_mailPolicyTable->Get().mailListPageSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailList request.");
			return;
		}
		Generated::Auction::FMailListRp response;
		response.requestId = request.requestId;
		const auto userId = m_sessionRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t authenticatedUserId = *userId;
		const std::uint64_t clientRequestId = request.requestId;
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = authenticatedUserId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FGetMailListRpc>(
			target,
			m_cacheRpcTimeout,
			[this, session, requestToken, bridgePointer, clientRequestId, authenticatedUserId](
				const Cache::Protocol::ECacheQueryResult result, std::vector<Cache::Protocol::FMailSummary> mails)
			{
				Generated::Auction::FMailListRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
					for (const auto& mail : mails)
					{
						rpcResponse.mailIds.push_back(mail.mailId);
						rpcResponse.mailTypes.push_back(mail.mailType);
						rpcResponse.subjects.push_back(mail.subject);
						rpcResponse.states.push_back(mail.state);
						rpcResponse.expiresAtUnixMs.push_back(mail.expiresAtUnixMs);
						rpcResponse.createdAtUnixMs.push_back(mail.createdAtUnixMs);
					}
				}
				else if (result == Cache::Protocol::ECacheQueryResult::InvalidArgument)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
				}
				else
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				}
				if (!IsWithinPortablePacketLimit(rpcResponse))
				{
					rpcResponse = {};
					rpcResponse.requestId = clientRequestId;
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
					Log(Foundation::ELogLevel::Error,
						"MailList client response exceeded the portable packet limit. userId={}",
						authenticatedUserId);
				}

				if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
				{
					Log(Foundation::ELogLevel::Error, "MailList RPC response send failed. userId={}", authenticatedUserId);
				}
				else if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					Log(Foundation::ELogLevel::Info,
						"MailList completed. userId={} shardIndex={} mailCount={} source=cache-rpc",
						authenticatedUserId,
						m_shardIndex,
						mails.size());
				}
			},
			[this, session, requestToken, bridgePointer, clientRequestId, authenticatedUserId](
				const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				Generated::Auction::FMailListRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse);
				Log(Foundation::ELogLevel::Error,
					"MailList cache RPC failed. userId={} error={} remoteCode={}",
					authenticatedUserId,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			authenticatedUserId,
			request.cursorMailId,
			request.limit);
		if (!callResult.accepted)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			Log(Foundation::ELogLevel::Error,
				"MailList cache RPC start failed. userId={} error={}",
				authenticatedUserId,
				static_cast<std::uint8_t>(callResult.error));
		}
	}

	void FAuctionCommandContent::HandleMailDetail(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FMailDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailDetailRq::kOpcode, payload, request) ||
			request.mailId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailDetail request.");
			return;
		}
		Generated::Auction::FMailDetailRp response;
		response.requestId = request.requestId;
		response.mailId = request.mailId;
		const auto userId = m_sessionRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t authenticatedUserId = *userId;
		const std::uint64_t clientRequestId = request.requestId;
		const std::uint64_t mailId = request.mailId;
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = authenticatedUserId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FGetMailDetailRpc>(
			target,
			m_cacheRpcTimeout,
			[this, session, requestToken, bridgePointer, clientRequestId, authenticatedUserId, mailId](
				const Cache::Protocol::ECacheQueryResult result, Cache::Protocol::FMailDetail mail)
			{
				Generated::Auction::FMailDetailRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				rpcResponse.mailId = mailId;
				if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
					rpcResponse.mailId = mail.mailId;
					rpcResponse.mailType = mail.mailType;
					rpcResponse.subject = std::move(mail.subject);
					rpcResponse.body = std::move(mail.body);
					rpcResponse.state = mail.state;
					rpcResponse.expiresAtUnixMs = mail.expiresAtUnixMs;
					for (auto& attachment : mail.attachments)
					{
						rpcResponse.attachmentIds.push_back(attachment.attachmentId);
						rpcResponse.attachmentTypes.push_back(attachment.attachmentType);
						rpcResponse.itemInstanceIds.push_back(attachment.itemInstanceId);
						rpcResponse.itemDataIds.push_back(attachment.itemDataId);
						rpcResponse.quantities.push_back(attachment.quantity);
						rpcResponse.itemData.push_back(std::move(attachment.itemDataJson));
						rpcResponse.currencyIds.push_back(attachment.currencyId);
						rpcResponse.currencyAmounts.push_back(attachment.currencyAmount);
						rpcResponse.attachmentStates.push_back(attachment.state);
					}
				}
				else if (result == Cache::Protocol::ECacheQueryResult::NotFound)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::MailNotFound);
				}
				else if (result == Cache::Protocol::ECacheQueryResult::InvalidArgument)
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
				}
				else
				{
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				}
				if (!IsWithinPortablePacketLimit(rpcResponse))
				{
					rpcResponse = {};
					rpcResponse.requestId = clientRequestId;
					rpcResponse.mailId = mailId;
					rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
					Log(Foundation::ELogLevel::Error,
						"MailDetail client response exceeded the portable packet limit. userId={} mailId={}",
						authenticatedUserId,
						mailId);
				}

				if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse))
				{
					Log(Foundation::ELogLevel::Error,
						"MailDetail RPC response send failed. userId={} mailId={}",
						authenticatedUserId,
						mailId);
				}
				else if (result == Cache::Protocol::ECacheQueryResult::Success)
				{
					Log(Foundation::ELogLevel::Info,
						"MailDetail completed. userId={} mailId={} shardIndex={} source=cache-rpc",
						authenticatedUserId,
						mailId,
						m_shardIndex);
				}
			},
			[this, session, requestToken, bridgePointer, clientRequestId, authenticatedUserId, mailId](
				const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				Generated::Auction::FMailDetailRp rpcResponse;
				rpcResponse.requestId = clientRequestId;
				rpcResponse.mailId = mailId;
				rpcResponse.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse);
				Log(Foundation::ELogLevel::Error,
					"MailDetail cache RPC failed. userId={} mailId={} error={} remoteCode={}",
					authenticatedUserId,
					mailId,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			authenticatedUserId,
			mailId);
		if (!callResult.accepted)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			Log(Foundation::ELogLevel::Error,
				"MailDetail cache RPC start failed. userId={} mailId={} error={}",
				authenticatedUserId,
				mailId,
				static_cast<std::uint8_t>(callResult.error));
		}
	}

	void FAuctionCommandContent::HandleMailClaim(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FMailClaimRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailClaimRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailClaim request.");
			return;
		}
		Generated::Auction::FMailClaimRp response;
		response.requestId = request.requestId;
		response.mailId = request.mailId;
		response.attachmentId = request.attachmentId;
		const auto userId = m_sessionRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}
		if (request.mailId == 0 || request.attachmentId == 0)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = *userId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FClaimMailAttachmentRpc>(
			target,
			m_cacheRpcTimeout,
			[this, session, requestToken, bridgePointer, response, authenticatedUserId = *userId](
				const Cache::Protocol::ECacheCommandResult result, Cache::Protocol::FMailClaimResult claim) mutable
			{
				response.resultCode = static_cast<std::uint16_t>(ToAuctionResultCode(result));
				response.attachmentType = claim.attachmentType;
				response.itemInstanceId = claim.itemInstanceId;
				response.itemDataId = claim.itemDataId;
				response.quantity = claim.quantity;
				response.currencyId = claim.currencyId;
				response.currencyAmount = claim.currencyAmount;
				response.currencyBalance = claim.currencyBalance;
				response.mailState = claim.mailState;
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
				Log(result == Cache::Protocol::ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
					"MailClaim completed. userId={} mailId={} attachmentId={} result={} source=cache-rpc",
					authenticatedUserId,
					response.mailId,
					response.attachmentId,
					static_cast<std::uint8_t>(result));
			},
			[this, session, requestToken, bridgePointer, response, authenticatedUserId = *userId](
				const RpcLib::Protocol::FRpcCallFailure& failure) mutable
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response);
				Log(Foundation::ELogLevel::Error,
					"MailClaim cache RPC failed. userId={} mailId={} attachmentId={} error={} remoteCode={}",
					authenticatedUserId,
					response.mailId,
					response.attachmentId,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			*userId,
			request.mailId,
			request.attachmentId);
		if (!callResult.accepted)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
		}
	}

	void FAuctionCommandContent::HandleListingCancel(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FListingCancelRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingCancelRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingCancel request.");
			return;
		}
		Generated::Auction::FListingCancelRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto userId = m_sessionRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}
		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Service::FListingCancelService service(m_databaseConfig);
		Database::SListingCancelPrepareResult prepared;
		std::string error;
		const Domain::EAuctionResultCode prepareCode =
			service.Prepare(*userId, request.listingId, request.expectedListingVersion, prepared, error);
		if (prepareCode != Domain::EAuctionResultCode::Success)
		{
			response.resultCode = static_cast<std::uint16_t>(prepareCode);
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			Log(prepareCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
				"ListingCancel prepare failed. userId={} listingId={} result={} error={}",
				*userId,
				request.listingId,
				static_cast<std::uint16_t>(prepareCode),
				error);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t sellerUserId = *userId;
		const auto complete = [this, session, requestToken, bridgePointer, request, sellerUserId](
								  const Domain::EAuctionResultCode resultCode,
								  const std::uint64_t returnMailId,
								  const std::uint64_t listingVersion,
								  const std::string& operationError)
		{
			Generated::Auction::FListingCancelRp rpcResponse;
			rpcResponse.requestId = request.requestId;
			rpcResponse.listingId = request.listingId;
			rpcResponse.resultCode = static_cast<std::uint16_t>(resultCode);
			rpcResponse.returnMailId = returnMailId;
			rpcResponse.listingVersion = listingVersion;
			ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, rpcResponse);
			Log(resultCode == Domain::EAuctionResultCode::Success
					? Foundation::ELogLevel::Info
					: (resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error
																			   : Foundation::ELogLevel::Warn),
				"ListingCancel completed. userId={} listingId={} returnMailId={} result={} remainingState={} error={}",
				sellerUserId,
				request.listingId,
				returnMailId,
				static_cast<std::uint16_t>(resultCode),
				resultCode == Domain::EAuctionResultCode::PartialCommit
					? "CANCEL_PENDING"
					: (resultCode == Domain::EAuctionResultCode::Success ? "CANCELLED" : "ACTIVE"),
				operationError);
		};

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = sellerUserId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FCreateListingReturnMailRpc>(
			target,
			m_cacheRpcTimeout,
			[this, sellerUserId, request, prepared, complete](
				const Cache::Protocol::ECacheCommandResult commandResult, const std::uint64_t mailId)
			{
				Service::FListingCancelService continuationService(m_databaseConfig);
				std::string continuationError;
				if (commandResult == Cache::Protocol::ECacheCommandResult::Success)
				{
					std::uint64_t listingVersion = 0;
					const Domain::EAuctionResultCode completionCode = continuationService.Complete(
						sellerUserId, request.listingId, prepared.preparedListingVersion, listingVersion, continuationError);
					complete(completionCode, mailId, listingVersion, continuationError);
					return;
				}
				if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						0,
						0,
						"Cache return-mail outcome is unknown; CANCEL_PENDING was preserved.");
					return;
				}
				if (!continuationService.Revert(sellerUserId, request.listingId, prepared.preparedListingVersion, continuationError))
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						0,
						0,
						"Cache return-mail creation was rejected but CANCEL_PENDING revert failed: " + continuationError);
					return;
				}
				complete(ToAuctionResultCode(commandResult), 0, 0, "Cache return-mail creation was rejected; CANCEL_PENDING was reverted.");
			},
			[this, sellerUserId, request, prepared, complete](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure))
				{
					Service::FListingCancelService continuationService(m_databaseConfig);
					std::string revertError;
					const bool reverted =
						continuationService.Revert(sellerUserId, request.listingId, prepared.preparedListingVersion, revertError);
					complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
						0,
						0,
						reverted ? "Cache return-mail RPC was rejected before dispatch; CANCEL_PENDING was reverted."
								 : "Cache return-mail RPC was rejected before dispatch and CANCEL_PENDING revert failed: " + revertError);
					return;
				}
				complete(Domain::EAuctionResultCode::PartialCommit,
					0,
					0,
					std::format("Cache return-mail RPC outcome is unknown; CANCEL_PENDING was preserved. error={} remoteCode={}",
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode)));
			},
			sellerUserId,
			prepared.itemInstanceId,
			prepared.itemDataId,
			prepared.quantity,
			prepared.itemDataJson);
		if (!callResult.accepted)
		{
			std::string revertError;
			const bool reverted = service.Revert(sellerUserId, request.listingId, prepared.preparedListingVersion, revertError);
			complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
				0,
				0,
				reverted ? "Cache return-mail RPC was not started; CANCEL_PENDING was reverted."
						 : "Cache return-mail RPC was not started and CANCEL_PENDING revert failed: " + revertError);
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"Cache RPC started. operation=CreateListingReturnMail listingId={} rpcRequestId={}",
				request.listingId,
				callResult.requestId);
		}
	}

	void FAuctionCommandContent::HandleBidRefund(
		ContentsRuntime::Session::FContentRequestContext& requestContext,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::uint64_t sessionId = requestContext.GetSession().GetSessionId();
		Generated::Auction::FBidRefundRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBidRefundRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid BidRefund request.");
			return;
		}

		const auto authenticatedUserId = m_sessionRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			Generated::Auction::FBidRefundRp response;
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			response.requestId = request.requestId;
			response.bidId = request.bidId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}
		const auto session = m_sessionRegistry->Find(sessionId);
		if (session == nullptr)
		{
			Generated::Auction::FBidRefundRp response;
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			response.requestId = request.requestId;
			response.bidId = request.bidId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			return;
		}

		Service::FBidRefundService service(m_databaseConfig);
		Database::SBidRefundPrepareResult prepared;
		std::string error;
		const Domain::EAuctionResultCode prepareCode =
			service.Prepare(*authenticatedUserId, request.listingId, request.bidId, request.expectedBidVersion, prepared, error);
		if (prepareCode != Domain::EAuctionResultCode::Success)
		{
			Generated::Auction::FBidRefundRp response;
			response.resultCode = static_cast<std::uint16_t>(prepareCode);
			response.requestId = request.requestId;
			response.bidId = request.bidId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, requestContext, response);
			Log(prepareCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn,
				"BidRefund prepare failed. userId={} listingId={} bidId={} result={} error={}",
				*authenticatedUserId,
				request.listingId,
				request.bidId,
				static_cast<std::uint16_t>(prepareCode),
				error);
			return;
		}

		const auto requestToken = requestContext.Defer();
		auto* const bridgePointer = &bridge;
		const std::uint64_t userId = *authenticatedUserId;
		const auto complete = [this, session, requestToken, bridgePointer, request, userId, prepared](
								  const Domain::EAuctionResultCode resultCode,
								  const Cache::Protocol::FCurrencyBalance& balance,
								  const std::string& operationError,
								  const std::string_view stage,
								  const bool faultInjected)
		{
			Generated::Auction::FBidRefundRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = request.requestId;
			response.bidId = request.bidId;
			response.refundedAmount = resultCode == Domain::EAuctionResultCode::Success ? prepared.bidAmount : 0;
			response.currencyBalance = balance.amount;
			response.bidState =
				resultCode == Domain::EAuctionResultCode::Success ? static_cast<std::uint8_t>(Domain::EAuctionBidState::Refunded) : 0;
			response.bidVersion = resultCode == Domain::EAuctionResultCode::Success ? prepared.preparedVersion + 1 : 0;
			if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, requestToken, response))
			{
				Log(Foundation::ELogLevel::Error, "BidRefund response send failed. userId={} bidId={}", userId, request.bidId);
			}
			Log(resultCode == Domain::EAuctionResultCode::Success
					? Foundation::ELogLevel::Info
					: (resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error
																			   : Foundation::ELogLevel::Warn),
				"BidRefund completed. userId={} listingId={} shardIndex={} bidId={} result={} remainingState={} "
				"stage={} faultInjected={} error={}",
				userId,
				request.listingId,
				m_shardIndex,
				request.bidId,
				static_cast<std::uint16_t>(resultCode),
				resultCode == Domain::EAuctionResultCode::PartialCommit
					? "REFUND_PENDING"
					: (resultCode == Domain::EAuctionResultCode::Success ? "REFUNDED" : "OUTBID_CLAIMABLE"),
				stage,
				faultInjected,
				operationError);
		};

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cacheServerInstanceId;
		target.routingKey = userId;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FCreditCurrencyRpc>(
			target,
			m_cacheRpcTimeout,
			[this, prepared, complete](
				const Cache::Protocol::ECacheCommandResult commandResult, const Cache::Protocol::FCurrencyBalance& balance)
			{
				Service::FBidRefundService continuationService(m_databaseConfig);
				std::string continuationError;
				if (commandResult == Cache::Protocol::ECacheCommandResult::Success)
				{
					if (m_faultInjectionBidRefundBeforeComplete)
					{
						complete(Domain::EAuctionResultCode::PartialCommit,
							balance,
							"fault injection requested after Cache credit success and before AuctionDB bid-refund completion.",
							"BeforeAuctionDB.CompleteBidRefund",
							true);
						return;
					}
					const Domain::EAuctionResultCode completionCode = continuationService.Complete(prepared, continuationError);
					complete(completionCode,
						balance,
						continuationError,
						completionCode == Domain::EAuctionResultCode::Success ? "Completed" : "AuctionDB.CompleteBidRefund",
						false);
					return;
				}
				if (commandResult == Cache::Protocol::ECacheCommandResult::OutcomeUnknown)
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						balance,
						"Cache refund credit outcome is unknown; REFUND_PENDING was preserved.",
						"CacheRpc.CreditCurrency",
						false);
					return;
				}
				if (!continuationService.Revert(prepared, continuationError))
				{
					complete(Domain::EAuctionResultCode::PartialCommit,
						balance,
						"Cache refund credit was rejected but REFUND_PENDING revert failed: " + continuationError,
						"AuctionDB.RevertBidRefund",
						false);
					return;
				}
				complete(ToAuctionResultCode(commandResult),
					balance,
					"Cache refund credit was rejected; REFUND_PENDING was reverted.",
					"CacheRpc.CreditCurrency",
					false);
			},
			[this, prepared, complete](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				if (RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure))
				{
					Service::FBidRefundService continuationService(m_databaseConfig);
					std::string revertError;
					const bool reverted = continuationService.Revert(prepared, revertError);
					complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
						{},
						reverted ? "Cache refund RPC was rejected before dispatch; REFUND_PENDING was reverted."
								 : "Cache refund RPC was rejected before dispatch and REFUND_PENDING revert failed: " + revertError,
						reverted ? "CacheRpc.CreditCurrency" : "AuctionDB.RevertBidRefund",
						false);
					return;
				}
				complete(Domain::EAuctionResultCode::PartialCommit,
					{},
					std::format("Cache refund RPC outcome is unknown; REFUND_PENDING was preserved. error={} remoteCode={}",
						static_cast<std::uint8_t>(failure.error),
						static_cast<std::uint16_t>(failure.remoteResponseCode)),
					"CacheRpc.CreditCurrency",
					false);
			},
			userId,
			prepared.currencyId,
			prepared.bidAmount);
		if (!callResult.accepted)
		{
			std::string revertError;
			const bool reverted = service.Revert(prepared, revertError);
			complete(reverted ? Domain::EAuctionResultCode::DatabaseUnavailable : Domain::EAuctionResultCode::PartialCommit,
				{},
				reverted ? "Cache refund RPC was not started; REFUND_PENDING was reverted."
						 : "Cache refund RPC was not started and REFUND_PENDING revert failed: " + revertError,
				reverted ? "BeforeCacheRpc.Dispatch" : "AuctionDB.RevertBidRefund",
				false);
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"Cache RPC started. operation=CreditCurrency listingId={} rpcRequestId={}",
				request.listingId,
				callResult.requestId);
		}
	}

	void FAuctionCommandContent::HandlePing(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FPingRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FPingRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "ping request deserialize failed.");
			return;
		}

		if (m_shardIndex == m_testDelayShardIndex && m_testDelayMilliseconds > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(m_testDelayMilliseconds));
		}

		Generated::Auction::FPingRp response;
		response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
		response.requestId = request.requestId;
		response.routingKey = request.routingKey;
		response.clientTimeUnixMs = request.clientTimeUnixMs;
		response.serverTimeUnixMs = GetUnixTimeMilliseconds();
		response.shardIndex = m_shardIndex;
		response.shardCount = m_shardCount;
		response.contentInstanceId = m_contentInstanceId;
		response.contentThreadId = static_cast<std::uint32_t>(GetCurrentThreadId());

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "ping response send failed.");
			return;
		}

		Log(Foundation::ELogLevel::Info,
			"ping handled on command content. sessionId={} requestId={} routingKey={} shardIndex={} contentThreadId={}",
			sessionId,
			request.requestId,
			request.routingKey,
			m_shardIndex,
			response.contentThreadId);
	}

	void FAuctionCommandContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "AuctionHouseServer", message);
		}
	}
}
