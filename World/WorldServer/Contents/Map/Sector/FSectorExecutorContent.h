#pragma once

namespace WorldServer::Contents
{
	class FSectorExecutorContent final : public ContentsRuntime::Core::IContent
	{
	public:
		explicit FSectorExecutorContent(ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::uint64_t maxPacketQueueDepth) noexcept;

		[[nodiscard]] ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		[[nodiscard]] ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		[[nodiscard]] std::uint32_t GetTargetFps() const noexcept override;
		[[nodiscard]] std::uint64_t GetMaxPacketQueueDepth() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnFrame(int delayFrame, ContentsRuntime::Bridge::IContentBridge& bridge) override;

	private:
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::uint64_t m_maxPacketQueueDepth = 0;
	};
}
