#pragma once

namespace GameData::Auction
{
	class FAuctionPolicyTable;
}

namespace AuctionHouseServer::Contents
{
	class FAuctionUserRegistry;

	class FAuctionAuthContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FAuctionAuthContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::shared_ptr<FAuctionUserRegistry> userRegistry,
			std::shared_ptr<Connector::ILoginTicketStore> ticketStore,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			Database::SAuctionDatabaseConfig databaseConfig);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;

	private:
		void SendAuthRequired(std::uint64_t sessionId,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void SendOutbidCatchUp(std::uint64_t sessionId, std::uint64_t userId, ContentsRuntime::Bridge::IContentBridge& bridge);
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
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId;
		std::shared_ptr<FAuctionUserRegistry> m_userRegistry;
		std::shared_ptr<Connector::ILoginTicketStore> m_ticketStore;
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> m_auctionPolicyTable;
		Database::SAuctionDatabaseConfig m_databaseConfig;
	};
}
