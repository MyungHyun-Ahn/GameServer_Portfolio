#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/FAuctionContentRouter.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"
#include "AuctionHouseServer/Domain/AuctionResultCode.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Auction/AuctionPackets.h"

#include <format>
namespace AuctionHouseServer::Contents
{
	namespace
	{
		std::uint64_t GetUnixTimeMilliseconds() noexcept
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	FAuctionContentRouter::FAuctionContentRouter(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FAuctionUserRegistry> userRegistry,
		std::vector<ContentsRuntime::Core::FContentInstanceId> commandShardInstanceIds)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_userRegistry(std::move(userRegistry))
		, m_commandShardInstanceIds(std::move(commandShardInstanceIds))
	{
	}

	ContentsRuntime::Core::FContentId FAuctionContentRouter::GetContentId() const noexcept
	{
		return kRouterContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuctionContentRouter::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FAuctionContentRouter::OnEnter(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FAuctionContentRouter::OnLeave(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FAuctionContentRouter::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (m_commandShardInstanceIds.empty())
		{
			Log(Foundation::ELogLevel::Error, "router has no command shards.");
			return;
		}

		std::uint64_t routingKey = 0;
		std::uint64_t requestId = 0;
		std::uint64_t listingId = 0;
		std::uint64_t bidId = 0;
		std::uint64_t clientTimeUnixMs = 0;
		if (opcode == Generated::Auction::FPingRq::kOpcode)
		{
			Generated::Auction::FPingRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize Ping request.");
				return;
			}
			routingKey = request.routingKey;
			requestId = request.requestId;
			clientTimeUnixMs = request.clientTimeUnixMs;
		}
		else if (opcode == Generated::Auction::FAuctionAuthRq::kOpcode)
		{
			Generated::Auction::FAuctionAuthRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
				Generated::Auction::FAuctionAuthRp response;
				response.resultCode =
					static_cast<std::uint16_t>(authenticatedUserId.has_value() ? Domain::EAuctionResultCode::AlreadyAuthenticated
																			   : Domain::EAuctionResultCode::AuthRequired);
				response.requestId = request.requestId;
				response.userId = authenticatedUserId.value_or(0);
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			return;
		}
		else if (opcode == Generated::Auction::FMyBidListRq::kOpcode)
		{
			Generated::Auction::FMyBidListRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize MyBidList request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FInventoryListRq::kOpcode)
		{
			Generated::Auction::FInventoryListRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize InventoryList request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FListingRegisterRq::kOpcode)
		{
			Generated::Auction::FListingRegisterRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize ListingRegister request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FListingSearchRq::kOpcode)
		{
			Generated::Auction::FListingSearchRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize ListingSearch request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FListingDetailRq::kOpcode)
		{
			Generated::Auction::FListingDetailRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize ListingDetail request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FBidRq::kOpcode)
		{
			Generated::Auction::FBidRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize Bid request.");
				return;
			}
			requestId = request.requestId;
			listingId = request.listingId;
		}
		else if (opcode == Generated::Auction::FBuyoutRq::kOpcode)
		{
			Generated::Auction::FBuyoutRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize Buyout request.");
				return;
			}
			requestId = request.requestId;
			listingId = request.listingId;
		}
		else if (opcode == Generated::Auction::FMailListRq::kOpcode)
		{
			Generated::Auction::FMailListRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				return;
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FMailDetailRq::kOpcode)
		{
			Generated::Auction::FMailDetailRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				return;
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FMailClaimRq::kOpcode)
		{
			Generated::Auction::FMailClaimRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				return;
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FListingCancelRq::kOpcode)
		{
			Generated::Auction::FListingCancelRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				return;
			requestId = request.requestId;
			listingId = request.listingId;
		}
		else if (opcode == Generated::Auction::FBidRefundRq::kOpcode)
		{
			Generated::Auction::FBidRefundRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize BidRefund request.");
				return;
			}
			requestId = request.requestId;
			bidId = request.bidId;
		}
		else if (opcode == Generated::Auction::FDebugCheatRq::kOpcode)
		{
			Generated::Auction::FDebugCheatRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize DebugCheat request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FSaleHistorySearchRq::kOpcode)
		{
			Generated::Auction::FSaleHistorySearchRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize SaleHistorySearch request.");
				return;
			}
			requestId = request.requestId;
		}
		else if (opcode == Generated::Auction::FSaleHistoryDetailRq::kOpcode)
		{
			Generated::Auction::FSaleHistoryDetailRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "router failed to deserialize SaleHistoryDetail request.");
				return;
			}
			requestId = request.requestId;
			listingId = request.listingId;
		}
		else
		{
			Log(Foundation::ELogLevel::Warn, "router received an unsupported packet.");
			return;
		}

		if (opcode != Generated::Auction::FPingRq::kOpcode)
		{
			const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
			if (!authenticatedUserId.has_value())
			{
				Log(Foundation::ELogLevel::Warn, "router rejected a session without authenticated user state.");
				return;
			}
			routingKey = *authenticatedUserId;
		}

		const std::size_t shardIndex = GetCommandShardIndex(routingKey, m_commandShardInstanceIds.size());
		const auto targetInstanceId = m_commandShardInstanceIds[shardIndex];
		const auto enqueueResult = bridge.EnqueuePacketToInstance(sessionId, routeGeneration, targetInstanceId, opcode, payload);
		if (enqueueResult == ContentsRuntime::Core::EPacketEnqueueResult::Accepted)
		{
			return;
		}

		if (enqueueResult == ContentsRuntime::Core::EPacketEnqueueResult::QueueFull)
		{
			bool sent = false;
			if (opcode == Generated::Auction::FPingRq::kOpcode)
			{
				Generated::Auction::FPingRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.routingKey = routingKey;
				response.clientTimeUnixMs = clientTimeUnixMs;
				response.serverTimeUnixMs = GetUnixTimeMilliseconds();
				response.shardIndex = static_cast<std::uint32_t>(shardIndex);
				response.shardCount = static_cast<std::uint32_t>(m_commandShardInstanceIds.size());
				response.contentInstanceId = targetInstanceId;
				response.contentThreadId = 0;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FMyBidListRq::kOpcode)
			{
				Generated::Auction::FMyBidListRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FInventoryListRq::kOpcode)
			{
				Generated::Auction::FInventoryListRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FListingRegisterRq::kOpcode)
			{
				Generated::Auction::FListingRegisterRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FListingSearchRq::kOpcode)
			{
				Generated::Auction::FListingSearchRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FListingDetailRq::kOpcode)
			{
				Generated::Auction::FListingDetailRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FBidRq::kOpcode)
			{
				Generated::Auction::FBidRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.listingId = listingId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FBuyoutRq::kOpcode)
			{
				Generated::Auction::FBuyoutRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.listingId = listingId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FMailListRq::kOpcode)
			{
				Generated::Auction::FMailListRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FMailDetailRq::kOpcode)
			{
				Generated::Auction::FMailDetailRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FMailClaimRq::kOpcode)
			{
				Generated::Auction::FMailClaimRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FListingCancelRq::kOpcode)
			{
				Generated::Auction::FListingCancelRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.listingId = listingId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FBidRefundRq::kOpcode)
			{
				Generated::Auction::FBidRefundRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.bidId = bidId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FSaleHistorySearchRq::kOpcode)
			{
				Generated::Auction::FSaleHistorySearchRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FSaleHistoryDetailRq::kOpcode)
			{
				Generated::Auction::FSaleHistoryDetailRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.listingId = listingId;
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else if (opcode == Generated::Auction::FDebugCheatRq::kOpcode)
			{
				Generated::Auction::FDebugCheatRp response;
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ServerBusy);
				response.requestId = requestId;
				response.message = "command queue is full";
				sent = ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			if (!sent)
			{
				Log(Foundation::ELogLevel::Error, "SERVER_BUSY response send failed.");
			}

			Log(Foundation::ELogLevel::Warn,
				"command shard queue full. sessionId={} routingKey={} shardIndex={}",
				sessionId,
				routingKey,
				shardIndex);
			return;
		}

		{
			Log(Foundation::ELogLevel::Error,
				"command shard enqueue failed. sessionId={} routingKey={} shardIndex={} result={}",
				sessionId,
				routingKey,
				shardIndex,
				static_cast<int>(enqueueResult));
		}
	}

	void FAuctionContentRouter::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "AuctionHouseServer", message);
		}
	}
}
