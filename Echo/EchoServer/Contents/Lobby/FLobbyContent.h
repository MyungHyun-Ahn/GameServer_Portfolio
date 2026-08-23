#pragma once

namespace EchoServer::Contents
{
	class FRoomRegistry;

	class FLobbyContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FLobbyContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::shared_ptr<FRoomRegistry> roomRegistry,
			SRuntimeOptions runtimeOptions);

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
		void HandleRoomListRq(std::uint64_t sessionId, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleRoomEnterRq(std::uint64_t sessionId,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge,
			std::uint64_t routeGeneration);
		void LogRoomEnterFailure(std::uint64_t sessionId, std::uint32_t roomId, ERoomFlowResultCode resultCode) const;
		void Log(Foundation::ELogLevel logLevel, const std::string& message) const;

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			Foundation::ELogLevel logLevel,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(logLevel, "EchoServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::shared_ptr<FRoomRegistry> m_roomRegistry;
		SRuntimeOptions m_runtimeOptions;
		std::unordered_map<std::uint64_t, std::uint64_t> m_sessionGenerations;
	};
}
