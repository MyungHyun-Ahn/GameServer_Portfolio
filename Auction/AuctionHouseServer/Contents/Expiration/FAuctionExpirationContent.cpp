#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Expiration/FAuctionExpirationContent.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"
#include "AuctionHouseServer/Service/FExpirationService.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Auction/AuctionPackets.h"

#include <format>
namespace AuctionHouseServer::Contents
{
	FAuctionExpirationContent::FAuctionExpirationContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FAuctionUserRegistry> userRegistry,
		Database::SAuctionDatabaseConfig databaseConfig,
		const std::uint32_t pollMilliseconds)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_userRegistry(std::move(userRegistry))
		, m_databaseConfig(std::move(databaseConfig))
		, m_pollInterval(std::max<std::uint32_t>(1, pollMilliseconds))
		, m_targetFps(std::clamp<std::uint32_t>(1000 / std::max<std::uint32_t>(1, pollMilliseconds), 1, 1000))
	{
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
		if (!m_databaseConfig.enabled)
			return;
		const auto now = std::chrono::steady_clock::now();
		if (now < m_nextPollTime)
			return;
		m_nextPollTime = now + m_pollInterval;

		Service::FExpirationService service(m_databaseConfig);
		std::vector<std::uint64_t> listingIds;
		std::string error;
		if (!service.GetCandidates(100, listingIds, error))
		{
			Log(Foundation::ELogLevel::Error, "expiration candidate query failed: " + error);
			return;
		}

		std::size_t settledCount = 0;
		for (const auto listingId : listingIds)
		{
			Database::SExpirationResult result;
			const auto resultCode = service.Execute(listingId, result, error);
			if (resultCode == Domain::EAuctionResultCode::ExpireNotAvailable)
				continue;
			if (resultCode != Domain::EAuctionResultCode::Success)
			{
				Log(Foundation::ELogLevel::Error,
					"expiration settlement failed. listingId={} resultCode={} error={}",
					listingId,
					static_cast<std::uint16_t>(resultCode),
					error);
				continue;
			}
			++settledCount;
			if (result.winnerUserId != 0)
			{
				const auto winnerSessionId = m_userRegistry->GetSessionId(result.winnerUserId);
				if (winnerSessionId.has_value() && bridge.IsSessionAlive(*winnerSessionId))
				{
					Generated::Auction::FAuctionWonNoti notification;
					notification.listingId = result.listingId;
					notification.bidId = result.highestBidId;
					notification.finalPrice = result.finalPrice;
					notification.itemMailId = result.itemMailId;
					ContentsRuntime::Bridge::SendContentPacket(bridge, *winnerSessionId, notification);
				}
			}
			Log(Foundation::ELogLevel::Info,
				"expiration settled. listingId={} winnerUserId={} finalPrice={} listingVersion={}",
				listingId,
				result.winnerUserId,
				result.finalPrice,
				result.listingVersion);
		}
		if (!listingIds.empty())
		{
			Log(Foundation::ELogLevel::Info, "expiration batch completed. candidates={} settled={}", listingIds.size(), settledCount);
		}
	}

	void FAuctionExpirationContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger)
			m_logger->Log(level, "AuctionHouseServer", message);
	}
}
