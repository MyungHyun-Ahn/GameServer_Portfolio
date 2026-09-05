#pragma once

namespace Foundation::Diagnostics
{
	class FTimingMetricsRuntime;
}

namespace ContentsRuntime::Core
{
	using FContentId = std::uint16_t;
	using FContentInstanceId = std::uint64_t;
	inline constexpr FContentId kInvalidContentId = 0;
	inline constexpr FContentInstanceId kInvalidContentInstanceId = 0;
	inline constexpr std::uint32_t kContentInstanceIdContentBitCount = 16;
	inline constexpr std::uint32_t kContentInstanceIdReserveBitCount = 4;
	inline constexpr std::uint32_t kContentInstanceIdSequenceBitCount =
		64 - kContentInstanceIdContentBitCount - kContentInstanceIdReserveBitCount;
	inline constexpr std::uint64_t kContentInstanceIdContentMask = (std::uint64_t{1} << kContentInstanceIdContentBitCount) - 1;
	inline constexpr std::uint64_t kContentInstanceIdReserveMask = (std::uint64_t{1} << kContentInstanceIdReserveBitCount) - 1;
	inline constexpr std::uint64_t kContentInstanceIdSequenceMask = (std::uint64_t{1} << kContentInstanceIdSequenceBitCount) - 1;
	inline constexpr std::uint32_t kContentInstanceIdReserveShift = kContentInstanceIdSequenceBitCount;
	inline constexpr std::uint32_t kContentInstanceIdContentShift = kContentInstanceIdSequenceBitCount + kContentInstanceIdReserveBitCount;
	inline constexpr std::uint8_t kDefaultContentInstanceReserveBits = 0;

	inline constexpr bool IsValidContentInstanceId(
		const FContentInstanceId contentInstanceId) noexcept
	{
		return contentInstanceId != kInvalidContentInstanceId;
	}

	inline constexpr FContentInstanceId MakeContentInstanceId(
		const FContentId contentId,
		const std::uint8_t reserveBits,
		const std::uint64_t sequence) noexcept
	{
		if (contentId == kInvalidContentId || (static_cast<std::uint64_t>(contentId) & ~kContentInstanceIdContentMask) != 0 ||
			(static_cast<std::uint64_t>(reserveBits) & ~kContentInstanceIdReserveMask) != 0 ||
			(sequence & ~kContentInstanceIdSequenceMask) != 0)
		{
			return kInvalidContentInstanceId;
		}

		return (static_cast<FContentInstanceId>(contentId) << kContentInstanceIdContentShift) |
			   (static_cast<FContentInstanceId>(reserveBits) << kContentInstanceIdReserveShift) | static_cast<FContentInstanceId>(sequence);
	}

	inline constexpr FContentId ExtractContentId(
		const FContentInstanceId contentInstanceId) noexcept
	{
		return static_cast<FContentId>((contentInstanceId >> kContentInstanceIdContentShift) & kContentInstanceIdContentMask);
	}

	inline constexpr std::uint8_t ExtractContentInstanceReserveBits(
		const FContentInstanceId contentInstanceId) noexcept
	{
		return static_cast<std::uint8_t>((contentInstanceId >> kContentInstanceIdReserveShift) & kContentInstanceIdReserveMask);
	}

	inline constexpr std::uint64_t ExtractContentInstanceSequence(
		const FContentInstanceId contentInstanceId) noexcept
	{
		return contentInstanceId & kContentInstanceIdSequenceMask;
	}

	enum class ERaceInjectionMode : std::uint8_t
	{
		None = 0,
		SwitchToThread = 1,
		Sleep0 = 2,
		Yield = 3
	};

	enum class EPacketEnqueueResult : std::uint8_t
	{
		Accepted = 0,
		QueueFull = 1,
		InvalidTarget = 2,
		StaleSession = 3
	};

	struct SPacketTimingMetricConfig
	{
		FContentId contentId = kInvalidContentId;
		std::uint16_t opcode = 0;
		std::uint16_t queueWaitMetricIndex = 0;
		std::uint16_t handlerMetricIndex = 0;
	};

	struct SContentRuntimeConfig
	{
		std::uint32_t workerThreadCount = 1;
		bool enableRaceInjection = false;
		bool failFastOnRuntimeError = false;
		bool enableTraceLogging = false;
		bool enableOwnershipTransferPolicy = false;
		std::uint32_t raceInjectionPeriod = 0;
		ERaceInjectionMode raceInjectionMode = ERaceInjectionMode::SwitchToThread;
		std::vector<FContentId> ownershipTransferAllowedContentIds;
		std::uint64_t ownershipTransferSourcePendingWorkThreshold = 32;
		std::uint64_t ownershipTransferContentQueueDepthThreshold = 16;
		std::uint64_t ownershipTransferPendingWorkGapThreshold = 8;
		std::int32_t ownershipTransferDelayFrameThreshold = 3;
		std::chrono::milliseconds ownershipTransferSustainDuration{2000};
		std::chrono::milliseconds ownershipTransferRequestCooldown{2000};
		std::chrono::milliseconds ownershipTransferCommitCooldown{5000};
		std::chrono::milliseconds ownershipTransferWorkerStabilizationDuration{5000};
		std::chrono::milliseconds workStealAttemptCooldown{1000};
		std::shared_ptr<std::atomic<std::uint64_t>> tracedSessionId;
		std::function<void(const std::string&)> traceLogger;
		Foundation::Diagnostics::FTimingMetricsRuntime* timingMetricsRuntime = nullptr;
		std::vector<SPacketTimingMetricConfig> packetTimingMetrics;
	};

	struct FOwnedPacketEnvelope
	{
		std::uint64_t sessionId = 0;
		std::uint64_t routeGeneration = 0;
		FContentInstanceId contentInstanceId = kInvalidContentInstanceId;
		std::uint16_t opcode = 0;
		std::vector<char> payload;
	};

	struct SContentLifecycleEvent
	{
		std::uint64_t sessionId = 0;
		std::uint64_t routeGeneration = 0;
		FContentInstanceId contentInstanceId = kInvalidContentInstanceId;
		std::shared_ptr<std::atomic<bool>> completionFlag;
		std::function<void()> completionCallback;
	};

	struct SContentThreadStats
	{
		FContentId contentId = kInvalidContentId;
		FContentInstanceId contentInstanceId = kInvalidContentInstanceId;
		bool running = false;
		std::uint64_t enqueueEnterCallCount = 0;
		std::uint64_t enqueueLeaveCallCount = 0;
		std::uint64_t enqueuePacketCallCount = 0;
		std::uint64_t enqueueCompletionCallCount = 0;
		std::uint64_t enterCount = 0;
		std::uint64_t leaveCount = 0;
		std::uint64_t packetCount = 0;
		std::uint64_t completionCount = 0;
		std::uint64_t frameCount = 0;
		std::uint64_t enterQueueDepth = 0;
		std::uint64_t leaveQueueDepth = 0;
		std::uint64_t packetQueueDepth = 0;
		std::uint64_t completionQueueDepth = 0;
		std::uint64_t maxEnterQueueDepth = 0;
		std::uint64_t maxLeaveQueueDepth = 0;
		std::uint64_t maxPacketQueueDepth = 0;
		std::uint64_t maxCompletionQueueDepth = 0;
		std::uint64_t enqueueEnterLockWaitNs = 0;
		std::uint64_t enqueueLeaveLockWaitNs = 0;
		std::uint64_t enqueuePacketLockWaitNs = 0;
		std::uint64_t enqueueCompletionLockWaitNs = 0;
		std::uint64_t maxEnqueueEnterLockWaitNs = 0;
		std::uint64_t maxEnqueueLeaveLockWaitNs = 0;
		std::uint64_t maxEnqueuePacketLockWaitNs = 0;
		std::uint64_t maxEnqueueCompletionLockWaitNs = 0;
		std::uint64_t rejectedPacketCount = 0;
		int lastDelayFrame = 0;
		int maxDelayFrame = 0;
	};

	struct SContentRuntimeContentStats
	{
		FContentId contentId = kInvalidContentId;
		FContentInstanceId contentInstanceId = kInvalidContentInstanceId;
		std::uint64_t activeSessionCount = 0;
		SContentThreadStats threadStats;
	};

	struct SContentRuntimeStats
	{
		std::uint64_t registeredContentCount = 0;
		std::uint64_t activeSessionCount = 0;
		std::uint64_t enterSessionCallCount = 0;
		std::uint64_t leaveSessionCallCount = 0;
		std::uint64_t enqueuePacketCallCount = 0;
		std::uint64_t enqueueCompletionCallCount = 0;
		std::uint64_t moveSessionCount = 0;
		std::uint64_t enqueueFailureCount = 0;
		std::uint64_t enterSessionLockWaitNs = 0;
		std::uint64_t leaveSessionLockWaitNs = 0;
		std::uint64_t enqueuePacketLockWaitNs = 0;
		std::uint64_t enqueueCompletionLockWaitNs = 0;
		std::uint64_t moveSessionLockWaitNs = 0;
		std::uint64_t maxEnterSessionLockWaitNs = 0;
		std::uint64_t maxLeaveSessionLockWaitNs = 0;
		std::uint64_t maxEnqueuePacketLockWaitNs = 0;
		std::uint64_t maxEnqueueCompletionLockWaitNs = 0;
		std::uint64_t maxMoveSessionLockWaitNs = 0;
		std::vector<SContentRuntimeContentStats> contents;
	};
}
