#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionUserRegistry;

	class FAuctionExpirationContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FAuctionExpirationContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::shared_ptr<FAuctionUserRegistry> userRegistry,
			Database::SAuctionDatabaseConfig databaseConfig,
			std::uint32_t pollMilliseconds = 5000);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		std::uint32_t GetTargetFps() const noexcept override
		{
			return m_targetFps;
		}
		void OnEnter(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}
		void OnLeave(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}
		void OnPacket(
			std::uint64_t,
			std::uint64_t,
			std::uint16_t,
			std::span<const char>,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}
		void OnFrame(int delayFrame, ContentsRuntime::Bridge::IContentBridge& bridge) override;

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

		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId;
		std::shared_ptr<FAuctionUserRegistry> m_userRegistry;
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::chrono::milliseconds m_pollInterval{5000};
		std::uint32_t m_targetFps = 1;
		std::chrono::steady_clock::time_point m_nextPollTime{};
	};
}
