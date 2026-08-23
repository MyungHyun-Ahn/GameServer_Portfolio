#pragma once

namespace ContentsRuntime::Bridge
{
	class IContentBridge;
}

namespace ContentsRuntime::Core
{
	class IContent
	{
	public:
		virtual ~IContent() = default;

		virtual FContentId GetContentId() const noexcept = 0;
		virtual FContentInstanceId GetContentInstanceId() const noexcept
		{
			return kInvalidContentInstanceId;
		}
		virtual std::uint32_t GetTargetFps() const noexcept
		{
			return 30;
		}
		virtual std::uint64_t GetMaxPacketQueueDepth() const noexcept
		{
			return 0;
		}

		virtual void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, Bridge::IContentBridge& bridge) = 0;
		virtual void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, Bridge::IContentBridge& bridge) = 0;
		virtual void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			Bridge::IContentBridge& bridge) = 0;
		virtual void OnFrame(
			int delayFrame,
			Bridge::IContentBridge& bridge)
		{
			(void)delayFrame;
			(void)bridge;
		}
	};
}
