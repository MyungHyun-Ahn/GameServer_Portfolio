#pragma once

namespace GameData::Item
{
	class FItemDataTable;
}

namespace GameData::Auction
{
	class FAuctionPolicyTable;
}

namespace GameData::InventoryPolicy
{
	class FInventoryPolicyTable;
}

namespace GameData::MailPolicy
{
	class FMailPolicyTable;
}

namespace AuctionHouseServer::Contents
{
	class FAuctionSessionRegistry;

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
			bool faultInjectionBidRefundBeforeComplete,
			std::shared_ptr<FAuctionSessionRegistry> sessionRegistry,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
			std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
			Database::SAuctionDatabaseConfig databaseConfig,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId,
			std::chrono::milliseconds cacheRpcTimeout);

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
		void OnFrame(int delayFrame, ContentsRuntime::Bridge::IContentBridge& bridge) override;

		void ProcessCacheRpcResponse(std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcResponse& response);
		void FailCacheRpcSession(std::uint64_t rpcSessionId);

	private:
		void HandlePing(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMyBidList(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleInventoryList(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingRegister(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingSearch(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingDetail(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleSaleHistorySearch(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleSaleHistoryDetail(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBid(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBuyout(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailList(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailDetail(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleMailClaim(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleListingCancel(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBidRefund(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleDebugCheat(ContentsRuntime::Session::FContentRequestContext& requestContext,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
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
		bool m_faultInjectionBidRefundBeforeComplete = false;
		std::shared_ptr<FAuctionSessionRegistry> m_sessionRegistry;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> m_auctionPolicyTable;
		std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> m_inventoryPolicyTable;
		std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> m_mailPolicyTable;
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		RpcLib::Protocol::FRpcServerInstanceId m_cacheServerInstanceId = 0;
		std::chrono::milliseconds m_cacheRpcTimeout{3000};
		RpcLib::Dispatch::FRpcMethodDispatcher m_rpcDispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
	};
}
