#pragma once

namespace ChattingServer::Contents
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

		int roomCount = 50;
		int roomCapacity = 8;
		std::uint32_t maxChatPayloadBytes = 8000;
		std::uint32_t traceUserId = 0;
		bool logPackets = false;
		bool bootstrapTrace = false;
		bool allowLegacyLogin = true;
		bool enablePagePool = true;
		std::uint32_t pageSize = 4096;
		bool enableTransitionResponseRaceInjection = false;
		ETransitionRaceInjectionMode transitionRaceInjectionMode = ETransitionRaceInjectionMode::None;
		bool enablePostRoomChangeResponseRaceInjection = false;
		ETransitionRaceInjectionMode postRoomChangeResponseRaceInjectionMode = ETransitionRaceInjectionMode::None;
		std::shared_ptr<std::atomic<std::uint64_t>> tracedSessionId;
	};
}
