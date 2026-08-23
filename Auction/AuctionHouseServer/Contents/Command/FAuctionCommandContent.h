#pragma once

namespace GameData::Item
{
	class FItemDataTable;
}

namespace GameData::Auction
{
	class FAuctionPolicyTable;
}

namespace AuctionHouseServer::Contents
{
	class FAuctionUserRegistry;

	class FAuctionCommandContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FAuctionCommandContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::uint32_t shardIndex,
			std::uint32_t shardCount,
			std::uint64_t maxPacketQueueDepth,
			std::uint32_t testDelayShardIndex,
			std::uint32_t testDelayMilliseconds,
			bool faultInjectionAfterAuctionCommit,
			std::shared_ptr<FAuctionUserRegistry> userRegistry,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			Database::SAuctionDatabaseConfig databaseConfig);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		std::uint64_t GetMaxPacketQueueDepth() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;

	private:
		void HandlePing(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMyBidList(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleInventoryList(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingRegister(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingSearch(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingDetail(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleSaleHistorySearch(std::uint64_t sessionId,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleSaleHistoryDetail(std::uint64_t sessionId,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBid(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBuyout(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailList(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailDetail(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailClaim(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingCancel(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBidRefund(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleDebugCheat(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void Log(Foundation::ELogLevel level, const std::string& message) const;

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			Foundation::ELogLevel level,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "AuctionHouseServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::uint32_t m_shardIndex = 0;
		std::uint32_t m_shardCount = 0;
		std::uint64_t m_maxPacketQueueDepth = 0;
		std::uint32_t m_testDelayShardIndex = 0;
		std::uint32_t m_testDelayMilliseconds = 0;
		bool m_faultInjectionAfterAuctionCommit = false;
		std::shared_ptr<FAuctionUserRegistry> m_userRegistry;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> m_auctionPolicyTable;
		Database::SAuctionDatabaseConfig m_databaseConfig;
	};
}
