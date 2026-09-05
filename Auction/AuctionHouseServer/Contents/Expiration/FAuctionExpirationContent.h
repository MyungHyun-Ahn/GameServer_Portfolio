#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionSessionRegistry;

	class FAuctionExpirationContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FAuctionExpirationContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::shared_ptr<FAuctionSessionRegistry> sessionRegistry,
			Database::SAuctionDatabaseConfig databaseConfig,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId,
			std::chrono::milliseconds cacheRpcTimeout,
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
		void ProcessCacheRpcResponse(std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcResponse& response);
		void FailCacheRpcSession(std::uint64_t rpcSessionId);

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
		std::shared_ptr<FAuctionSessionRegistry> m_sessionRegistry;
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::chrono::milliseconds m_pollInterval{5000};
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		RpcLib::Protocol::FRpcServerInstanceId m_cacheServerInstanceId = 0;
		std::chrono::milliseconds m_cacheRpcTimeout{3000};
		RpcLib::Dispatch::FRpcMethodDispatcher m_rpcDispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
		std::uint32_t m_targetFps = 1;
		std::chrono::steady_clock::time_point m_nextPollTime{};
	};
}
