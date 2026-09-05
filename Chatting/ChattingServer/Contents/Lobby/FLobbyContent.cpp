#include "ChattingServerPch.h"

#include "ChattingServer/Contents/Lobby/FLobbyContent.h"

#include "ChattingServer/Contents/Room/FRoomRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "ChattingServer/Contents/Room/RoomFlowTypes.h"
#include "Generated/Packets/Cpp/Chatting/ChattingPackets.h"

#include <format>
namespace ChattingServer::Contents
{
	namespace
	{
		bool ShouldTraceSession(
			const SRuntimeOptions& runtimeOptions,
			const std::uint64_t sessionId) noexcept
		{
			if (!runtimeOptions.bootstrapTrace)
			{
				return false;
			}

			if (runtimeOptions.tracedSessionId == nullptr)
			{
				return true;
			}

			const std::uint64_t tracedSessionId = runtimeOptions.tracedSessionId->load(std::memory_order_relaxed);
			return tracedSessionId != 0 && tracedSessionId == sessionId;
		}

		void RunTransitionResponseRaceInjection(
			const SRuntimeOptions& runtimeOptions) noexcept
		{
			if (!runtimeOptions.enableTransitionResponseRaceInjection)
			{
				return;
			}

			switch (runtimeOptions.transitionRaceInjectionMode)
			{
				case SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread:
					::SwitchToThread();
					break;
				case SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0:
					::Sleep(0);
					break;
				case SRuntimeOptions::ETransitionRaceInjectionMode::Yield:
					std::this_thread::yield();
					break;
				default:
					break;
			}
		}

		void RunRaceInjection(
			const SRuntimeOptions::ETransitionRaceInjectionMode mode) noexcept
		{
			switch (mode)
			{
				case SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread:
					::SwitchToThread();
					break;
				case SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0:
					::Sleep(0);
					break;
				case SRuntimeOptions::ETransitionRaceInjectionMode::Yield:
					std::this_thread::yield();
					break;
				default:
					break;
			}
		}

		Generated::Chatting::FRoomListRp BuildRoomListResponse(
			const std::vector<SRoomInfoSnapshot>& roomSnapshots)
		{
			Generated::Chatting::FRoomListRp responsePacket;
			responsePacket.roomIds.reserve(roomSnapshots.size());
			responsePacket.roomNames.reserve(roomSnapshots.size());
			responsePacket.participantCounts.reserve(roomSnapshots.size());
			responsePacket.capacities.reserve(roomSnapshots.size());
			responsePacket.joinableFlags.reserve(roomSnapshots.size());
			for (const SRoomInfoSnapshot& roomSnapshot : roomSnapshots)
			{
				responsePacket.roomIds.push_back(roomSnapshot.roomId);
				responsePacket.roomNames.push_back(roomSnapshot.roomName);
				responsePacket.participantCounts.push_back(roomSnapshot.participantCount);
				responsePacket.capacities.push_back(roomSnapshot.capacity);
				responsePacket.joinableFlags.push_back(roomSnapshot.joinable ? 1u : 0u);
			}

			return responsePacket;
		}

		struct SDeferredRoomActivation
		{
			std::shared_ptr<FRoomRegistry> roomRegistry;
			std::shared_ptr<Foundation::ILogger> logger;
			std::uint64_t sessionId = 0;
			std::uint32_t roomId = 0;
			std::uint64_t routeGeneration = 0;
			std::atomic<bool> ackQueued = false;
			std::atomic<bool> moveCompleted = false;
			std::atomic<bool> activationCompleted = false;

			void MarkAckQueued() noexcept
			{
				ackQueued.store(true, std::memory_order_release);
				TryActivate();
			}

			void MarkMoveCompleted() noexcept
			{
				moveCompleted.store(true, std::memory_order_release);
				TryActivate();
			}

			void TryActivate() noexcept
			{
				if (!ackQueued.load(std::memory_order_acquire) || !moveCompleted.load(std::memory_order_acquire))
				{
					return;
				}

				if (activationCompleted.exchange(true, std::memory_order_acq_rel))
				{
					return;
				}

				if (roomRegistry == nullptr || roomRegistry->ActivateSessionRoom(sessionId, roomId, routeGeneration))
				{
					return;
				}

				if (logger != nullptr)
				{
					logger->Log(Foundation::ELogLevel::Warn,
						"ChattingServer",
						"deferred lobby room activation failed. sessionId={} roomId={} routeGeneration={}",
						sessionId,
						roomId,
						routeGeneration);
				}
			}
		};
	}

	FLobbyContent::FLobbyContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FRoomRegistry> roomRegistry,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_roomRegistry(std::move(roomRegistry))
		, m_runtimeOptions(std::move(runtimeOptions))
	{
	}

	ContentsRuntime::Core::FContentId FLobbyContent::GetContentId() const noexcept
	{
		return kLobbyContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FLobbyContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FLobbyContent::OnEnter(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_sessionGenerations[sessionId] = routeGeneration;
		Log(Foundation::ELogLevel::Info, "lobby content enter. sessionId={} routeGeneration={}", sessionId, routeGeneration);
	}

	void FLobbyContent::OnLeave(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		const auto generationIt = m_sessionGenerations.find(sessionId);
		const bool isCurrentGeneration = generationIt != m_sessionGenerations.end() && generationIt->second == routeGeneration;
		Log(Foundation::ELogLevel::Info,
			"lobby content leave. sessionId={} routeGeneration={} stale={}",
			sessionId,
			routeGeneration,
			(isCurrentGeneration ? 0 : 1));
		if (isCurrentGeneration)
		{
			m_sessionGenerations.erase(sessionId);
		}
	}

	void FLobbyContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
		if (!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId)
		{
			return;
		}

		const auto generationIt = m_sessionGenerations.find(sessionId);
		if (generationIt == m_sessionGenerations.end() || generationIt->second != routeGeneration)
		{
			return;
		}

		switch (opcode)
		{
			case Generated::Chatting::FRoomListRq::kOpcode:
				HandleRoomListRq(sessionId, bridge);
				return;
			case Generated::Chatting::FRoomChangeRq::kOpcode:
				HandleRoomChangeRq(sessionId, payload, bridge, routeGeneration);
				return;
			default:
				return;
		}
	}

	void FLobbyContent::HandleRoomListRq(
		const std::uint64_t sessionId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto roomSnapshots = m_roomRegistry != nullptr ? m_roomRegistry->GetRoomsSnapshot() : std::vector<SRoomInfoSnapshot>{};
		Generated::Chatting::FRoomListRp responsePacket = BuildRoomListResponse(roomSnapshots);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error, "room list response send failed in lobby. sessionId={}", sessionId);
		}
		else if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"room list response sent in lobby. sessionId={} roomCount={}",
				sessionId,
				roomSnapshots.size());
		}
	}

	void FLobbyContent::HandleRoomChangeRq(
		const std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge,
		const std::uint64_t routeGeneration)
	{
		Generated::Chatting::FRoomChangeRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Chatting::FRoomChangeRq::kOpcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "lobby room change deserialize failed.");
			return;
		}

		ContentsRuntime::Core::FContentInstanceId targetContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		const std::uint64_t targetRouteGeneration = routeGeneration + 1;
		std::shared_ptr<SDeferredRoomActivation> activationState;
		ERoomFlowResultCode resultCode =
			m_roomRegistry != nullptr
				? m_roomRegistry->TryEnterRoom(sessionId, requestPacket.targetRoomId, targetRouteGeneration, targetContentInstanceId)
				: ERoomFlowResultCode::InternalError;

		if (resultCode == ERoomFlowResultCode::Success && !bridge.HasContentInstance(targetContentInstanceId))
		{
			m_roomRegistry->RevertEnterRoom(sessionId, requestPacket.targetRoomId, targetRouteGeneration);
			resultCode = ERoomFlowResultCode::MissingContentInstance;
		}

		if (resultCode == ERoomFlowResultCode::Success)
		{
			activationState = std::make_shared<SDeferredRoomActivation>();
			activationState->roomRegistry = m_roomRegistry;
			activationState->logger = m_logger;
			activationState->sessionId = sessionId;
			activationState->roomId = requestPacket.targetRoomId;
			activationState->routeGeneration = targetRouteGeneration;

			if (!bridge.MoveSessionToInstanceWithCompletion(sessionId,
					targetContentInstanceId,
					[activationState]()
					{
						activationState->MarkMoveCompleted();
					}))
			{
				if (m_roomRegistry != nullptr)
				{
					m_roomRegistry->RevertEnterRoom(sessionId, requestPacket.targetRoomId, targetRouteGeneration);
				}
				resultCode = ERoomFlowResultCode::RuntimeRouteFailure;
				activationState.reset();
			}
		}

		if (resultCode != ERoomFlowResultCode::Success)
		{
			LogRoomChangeFailure(sessionId, requestPacket.targetRoomId, resultCode);
		}

		if (resultCode == ERoomFlowResultCode::Success)
		{
			RunTransitionResponseRaceInjection(m_runtimeOptions);

			Generated::Chatting::FRoomChangeRp responsePacket;
			responsePacket.previousRoomId = 0;
			responsePacket.currentRoomId = requestPacket.targetRoomId;
			responsePacket.success = true;
			responsePacket.resultCode = static_cast<std::uint16_t>(ERoomFlowResultCode::Success);
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
			{
				Log(Foundation::ELogLevel::Error,
					"lobby room change response send failed before activation. sessionId={} targetRoomId={} resultCode={}",
					sessionId,
					requestPacket.targetRoomId,
					ToString(ERoomFlowResultCode::Success));
				return;
			}

			activationState->MarkAckQueued();
			if (m_runtimeOptions.enablePostRoomChangeResponseRaceInjection)
			{
				RunRaceInjection(m_runtimeOptions.postRoomChangeResponseRaceInjectionMode);
			}
			return;
		}

		Generated::Chatting::FRoomChangeRp responsePacket;
		responsePacket.previousRoomId = 0;
		responsePacket.currentRoomId = 0;
		responsePacket.success = false;
		responsePacket.resultCode = static_cast<std::uint16_t>(resultCode);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error,
				"lobby room change response send failed. sessionId={} targetRoomId={} resultCode={}",
				sessionId,
				requestPacket.targetRoomId,
				ToString(resultCode));
		}
	}

	void FLobbyContent::LogRoomChangeFailure(
		const std::uint64_t sessionId,
		const std::uint32_t targetRoomId,
		const ERoomFlowResultCode resultCode) const
	{
		Log(IsNormalRoomFlowFailure(resultCode) ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Error,
			"lobby room change failed. sessionId={} previousRoomId=0 targetRoomId={} resultCode={}",
			sessionId,
			targetRoomId,
			ToString(resultCode));
	}

	void FLobbyContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "ChattingServer", message);
		}
	}
}
