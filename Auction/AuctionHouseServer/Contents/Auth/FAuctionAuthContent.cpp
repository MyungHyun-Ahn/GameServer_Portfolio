#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Auth/FAuctionAuthContent.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"
#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Domain/AuctionResultCode.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Auction/AuctionPackets.h"
#include "GameData/Auction/FAuctionPolicyTable.h"

#include <format>
namespace AuctionHouseServer::Contents
{
	FAuctionAuthContent::FAuctionAuthContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FAuctionUserRegistry> userRegistry,
		std::shared_ptr<Connector::ILoginTicketStore> ticketStore,
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
		Database::SAuctionDatabaseConfig databaseConfig)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_userRegistry(std::move(userRegistry))
		, m_ticketStore(std::move(ticketStore))
		, m_auctionPolicyTable(std::move(auctionPolicyTable))
		, m_databaseConfig(std::move(databaseConfig))
	{
	}

	ContentsRuntime::Core::FContentId FAuctionAuthContent::GetContentId() const noexcept
	{
		return kAuthContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuctionAuthContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FAuctionAuthContent::OnEnter(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FAuctionAuthContent::OnLeave(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FAuctionAuthContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (opcode != Generated::Auction::FAuctionAuthRq::kOpcode)
		{
			SendAuthRequired(sessionId, opcode, payload, bridge);
			return;
		}

		Generated::Auction::FAuctionAuthRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "AuctionAuth deserialize failed.");
			return;
		}

		Generated::Auction::FAuctionAuthRp response;
		response.requestId = request.requestId;
		Connector::SConsumedLoginTicket consumedTicket{};
		std::string error;
		const bool consumed = m_ticketStore != nullptr && m_ticketStore->TryConsumeLoginTicket(request.ticket, consumedTicket, error);
		if (!consumed || !consumedTicket.valid || consumedTicket.userId == 0)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthenticationFailed);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			Log(Foundation::ELogLevel::Warn, "AuctionAuth rejected: " + error);
			return;
		}

		const std::uint64_t userId = consumedTicket.userId;
		const auto previousSessionId = m_userRegistry->Bind(sessionId, userId, consumedTicket.loginId);
		if (!bridge.MoveSession(sessionId, kRouterContentId))
		{
			m_userRegistry->Remove(sessionId);
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			Log(Foundation::ELogLevel::Error, "AuctionAuth move to router failed.");
			return;
		}

		if (previousSessionId.has_value() && *previousSessionId != sessionId && bridge.IsSessionAlive(*previousSessionId))
		{
			bridge.DisconnectSession(*previousSessionId);
		}

		response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
		response.userId = userId;
		const auto& policy = m_auctionPolicyTable->Get();
		response.maxActiveListings = policy.maxActiveListings;
		response.searchPageSize = policy.searchPageSize;
		response.minimumListingDurationSeconds = policy.minimumListingDurationSeconds;
		response.maximumListingDurationSeconds = policy.maximumListingDurationSeconds;
		response.defaultListingDurationSeconds = policy.defaultListingDurationSeconds;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			m_userRegistry->Remove(sessionId);
			bridge.DisconnectSession(sessionId);
			Log(Foundation::ELogLevel::Error, "AuctionAuth response send failed.");
			return;
		}

		Log(Foundation::ELogLevel::Info,
			"AuctionAuth succeeded. sessionId={} userId={} loginVersion={}",
			sessionId,
			userId,
			consumedTicket.loginVersion);
		SendOutbidCatchUp(sessionId, userId, bridge);
	}

	void FAuctionAuthContent::SendOutbidCatchUp(
		const std::uint64_t sessionId,
		const std::uint64_t userId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (!m_databaseConfig.enabled)
		{
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		auto* connection = context.GetAuctionPrimary(error);
		std::vector<Database::SOutbidClaimable> bids;
		if (connection == nullptr || !Database::FAuctionRepository(*connection).GetOutbidClaimable(userId, 100, bids, error))
		{
			Log(Foundation::ELogLevel::Warn, "outbid catch-up query failed: " + error);
			return;
		}

		std::size_t sentCount = 0;
		for (const auto& bid : bids)
		{
			Generated::Auction::FAuctionOutbidNoti notification;
			notification.listingId = bid.listingId;
			notification.bidId = bid.bidId;
			notification.heldAmount = bid.heldAmount;
			notification.newHighestAmount = bid.newHighestAmount;
			if (ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, notification))
			{
				++sentCount;
			}
		}

		Log(Foundation::ELogLevel::Info, "outbid catch-up completed. userId={} count={}", userId, sentCount);
	}

	void FAuctionAuthContent::SendAuthRequired(
		const std::uint64_t sessionId,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
		if (opcode == Generated::Auction::FMyBidListRq::kOpcode)
		{
			Generated::Auction::FMyBidListRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FMyBidListRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FPingRq::kOpcode)
		{
			Generated::Auction::FPingRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FPingRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.routingKey = request.routingKey;
				response.clientTimeUnixMs = request.clientTimeUnixMs;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FSaleHistorySearchRq::kOpcode)
		{
			Generated::Auction::FSaleHistorySearchRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FSaleHistorySearchRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FSaleHistoryDetailRq::kOpcode)
		{
			Generated::Auction::FSaleHistoryDetailRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FSaleHistoryDetailRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.listingId = request.listingId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FBidRefundRq::kOpcode)
		{
			Generated::Auction::FBidRefundRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FBidRefundRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.bidId = request.bidId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FBidRq::kOpcode)
		{
			Generated::Auction::FBidRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FBidRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.listingId = request.listingId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FBuyoutRq::kOpcode)
		{
			Generated::Auction::FBuyoutRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FBuyoutRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.listingId = request.listingId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FMailListRq::kOpcode)
		{
			Generated::Auction::FMailListRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FMailListRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FMailDetailRq::kOpcode)
		{
			Generated::Auction::FMailDetailRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FMailDetailRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.mailId = request.mailId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FMailClaimRq::kOpcode)
		{
			Generated::Auction::FMailClaimRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FMailClaimRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.mailId = request.mailId;
				response.attachmentId = request.attachmentId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		else if (opcode == Generated::Auction::FListingCancelRq::kOpcode)
		{
			Generated::Auction::FListingCancelRq request;
			if (ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Generated::Auction::FListingCancelRp response;
				response.resultCode = resultCode;
				response.requestId = request.requestId;
				response.listingId = request.listingId;
				ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		}
		Log(Foundation::ELogLevel::Warn, "unauthenticated auction request rejected.");
	}

	void FAuctionAuthContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "AuctionHouseServer", message);
		}
	}
}
