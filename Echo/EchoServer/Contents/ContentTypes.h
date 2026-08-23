#pragma once

namespace EchoServer::Contents
{
	inline constexpr ContentsRuntime::Core::FContentId kAuthContentId = 1;
	inline constexpr ContentsRuntime::Core::FContentId kLobbyContentId = 2;
	inline constexpr ContentsRuntime::Core::FContentId kRoomContentId = 3;
	inline constexpr std::uint32_t kRoomIdBase = 77;

	inline constexpr std::uint32_t MakeRoomId(
		const std::uint32_t index) noexcept
	{
		return kRoomIdBase + index;
	}

	struct SRuntimeOptions
	{
		enum class ETransitionRaceInjectionMode : std::uint8_t
		{
			None = 0,
			SwitchToThread = 1,
			Sleep0 = 2,
			Yield = 3
		};

		int sendThreadCount = 1;
		int responsesPerThread = 1;
		int roomCount = 3;
		int roomCapacity = 2;
		int roomChangeProbabilityPercent = 25;
		std::uint32_t traceUserId = 0;
		bool logPackets = false;
		bool bootstrapTrace = false;
		bool enablePagePool = true;
		std::uint32_t pageSize = 4096;
		bool enableTransitionResponseRaceInjection = false;
		ETransitionRaceInjectionMode transitionRaceInjectionMode = ETransitionRaceInjectionMode::Sleep0;
		bool enablePostRoomChangeResponseRaceInjection = false;
		ETransitionRaceInjectionMode postRoomChangeResponseRaceInjectionMode = ETransitionRaceInjectionMode::Sleep0;
		bool enableFirstEchoAfterRoomChangeRaceInjection = false;
		ETransitionRaceInjectionMode firstEchoAfterRoomChangeRaceInjectionMode = ETransitionRaceInjectionMode::Sleep0;
		bool enableDelegateTestSleep = false;
		std::uint32_t delegateTestTargetRoomId = 0;
		std::int32_t delegateTestSleepMs = 0;
		std::int32_t delegateTestSleepEveryNFrames = 1;
		std::shared_ptr<std::atomic<std::uint64_t>> tracedSessionId;
	};
}
