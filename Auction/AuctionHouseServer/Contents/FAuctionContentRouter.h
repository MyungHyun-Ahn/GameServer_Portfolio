#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionUserRegistry;

	class FAuctionContentRouter final : public ContentsRuntime::Core::IContent
	{
	public:
		FAuctionContentRouter(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::shared_ptr<FAuctionUserRegistry> userRegistry,
			std::vector<ContentsRuntime::Core::FContentInstanceId> commandShardInstanceIds);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		void OnEnter(std::uint64_t, std::uint64_t, ContentsRuntime::Bridge::IContentBridge&) override;
		void OnLeave(std::uint64_t, std::uint64_t, ContentsRuntime::Bridge::IContentBridge&) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;

	private:
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
		std::shared_ptr<FAuctionUserRegistry> m_userRegistry;
		std::vector<ContentsRuntime::Core::FContentInstanceId> m_commandShardInstanceIds;
	};
}
