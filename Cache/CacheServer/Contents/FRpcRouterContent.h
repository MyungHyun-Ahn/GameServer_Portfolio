#pragma once

namespace CacheServer::Contents
{
	class FRpcRouterContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FRpcRouterContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			RpcLib::Protocol::FRpcServerInstanceId serverInstanceId,
			std::uint64_t maxPacketQueueDepth,
			RpcLib::Session::FRpcSessionRegistry& sessionRegistry,
			RpcLib::Transport::FServerRpcTransport& transport,
			std::vector<ContentsRuntime::Core::FContentInstanceId> playerCacheInstanceIds);

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

	private:
		void HandleHello(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleRequest(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleResponse(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleNotification(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void SendRequestError(std::uint64_t sessionId,
			const RpcLib::Protocol::FRpcRequest& request,
			RpcLib::Protocol::ERpcResponseCode responseCode);
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
				m_logger->Log(level, "CacheServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		RpcLib::Protocol::FRpcServerInstanceId m_serverInstanceId = 0;
		std::uint64_t m_maxPacketQueueDepth = 0;
		RpcLib::Session::FRpcSessionRegistry& m_sessionRegistry;
		RpcLib::Transport::FServerRpcTransport& m_transport;
		std::vector<ContentsRuntime::Core::FContentInstanceId> m_playerCacheInstanceIds;
		std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> m_handshakeDeadlines;
	};
}
