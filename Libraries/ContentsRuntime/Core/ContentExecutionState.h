#pragma once

namespace ContentsRuntime::Core
{
	class IContent;

	enum class EQueuedWorkKind : std::uint8_t
	{
		Enter,
		Leave,
		Packet,
		Completion
	};

	struct SQueuedWorkItem
	{
		EQueuedWorkKind kind = EQueuedWorkKind::Packet;
		std::chrono::steady_clock::time_point enqueuedAt{};
		SContentLifecycleEvent lifecycleEvent{};
		FOwnedPacketEnvelope packet{};
		std::function<void()> completion;
	};

	struct SContentMailbox
	{
		std::mutex lock;
		std::deque<SQueuedWorkItem> items;
		bool readyQueued = false;
	};

	struct SContentExecutionState
	{
		inline static constexpr std::uint32_t kInvalidWorkerIndex = std::numeric_limits<std::uint32_t>::max();

		IContent* content = nullptr;
		FContentId contentId = kInvalidContentId;
		FContentInstanceId contentInstanceId = kInvalidContentInstanceId;
		std::atomic<std::uint32_t> ownerWorkerIndex = kInvalidWorkerIndex;
		std::atomic<std::uint32_t> requestedTransferTargetWorkerIndex = kInvalidWorkerIndex;
		std::atomic<std::int64_t> overloadSinceMs = 0;
		std::atomic<std::int64_t> lastTransferRequestMs = 0;
		std::atomic<std::int64_t> lastTransferCommitMs = 0;
		std::chrono::milliseconds frameDuration{33};
		std::chrono::steady_clock::time_point nextFrameTime{};
		std::uint64_t maxPacketQueueDepthLimit = 0;
		std::mutex consumerLock;
		SContentMailbox mailbox;
		std::atomic<std::uint64_t> inFlightCallbackCount = 0;
		std::atomic<std::uint64_t> enqueueEnterCallCount = 0;
		std::atomic<std::uint64_t> enqueueLeaveCallCount = 0;
		std::atomic<std::uint64_t> enqueuePacketCallCount = 0;
		std::atomic<std::uint64_t> enqueueCompletionCallCount = 0;
		std::atomic<std::uint64_t> enterCount = 0;
		std::atomic<std::uint64_t> leaveCount = 0;
		std::atomic<std::uint64_t> packetCount = 0;
		std::atomic<std::uint64_t> completionCount = 0;
		std::atomic<std::uint64_t> frameCount = 0;
		std::atomic<std::uint64_t> enterQueueDepth = 0;
		std::atomic<std::uint64_t> leaveQueueDepth = 0;
		std::atomic<std::uint64_t> packetQueueDepth = 0;
		std::atomic<std::uint64_t> completionQueueDepth = 0;
		std::atomic<std::uint64_t> maxEnterQueueDepth = 0;
		std::atomic<std::uint64_t> maxLeaveQueueDepth = 0;
		std::atomic<std::uint64_t> maxPacketQueueDepth = 0;
		std::atomic<std::uint64_t> maxCompletionQueueDepth = 0;
		std::atomic<std::uint64_t> enqueueEnterLockWaitNs = 0;
		std::atomic<std::uint64_t> enqueueLeaveLockWaitNs = 0;
		std::atomic<std::uint64_t> enqueuePacketLockWaitNs = 0;
		std::atomic<std::uint64_t> enqueueCompletionLockWaitNs = 0;
		std::atomic<std::uint64_t> maxEnqueueEnterLockWaitNs = 0;
		std::atomic<std::uint64_t> maxEnqueueLeaveLockWaitNs = 0;
		std::atomic<std::uint64_t> maxEnqueuePacketLockWaitNs = 0;
		std::atomic<std::uint64_t> maxEnqueueCompletionLockWaitNs = 0;
		std::atomic<std::uint64_t> rejectedPacketCount = 0;
		std::atomic<int> lastDelayFrame = 0;
		std::atomic<int> maxDelayFrame = 0;
	};
}
