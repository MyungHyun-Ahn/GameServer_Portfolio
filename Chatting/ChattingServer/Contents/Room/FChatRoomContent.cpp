#include "ChattingServerPch.h"

#include "ChattingServer/Contents/Room/FChatRoomContent.h"

#include "ChattingServer/Contents/Room/FRoomRegistry.h"
#include "ChattingServer/Contents/Session/FUserRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "ChattingServer/Contents/Room/RoomFlowTypes.h"
#include "Generated/Packets/Chatting/ChattingPackets.h"

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
						"deferred room activation failed. sessionId={} roomId={} routeGeneration={}",
						sessionId,
						roomId,
						routeGeneration);
				}
			}
		};
	}

	FChatRoomContent::FChatRoomContent(
		std::shared_ptr<Foundation::ILogger> logger,
		ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FRoomRegistry> roomRegistry,
		std::shared_ptr<FUserRegistry> userRegistry,
		const std::uint32_t roomId,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_roomRegistry(std::move(roomRegistry))
		, m_userRegistry(std::move(userRegistry))
		, m_roomId(roomId)
		, m_runtimeOptions(std::move(runtimeOptions))
	{
	}

	ContentsRuntime::Core::FContentId FChatRoomContent::GetContentId() const noexcept
	{
		return kRoomContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FChatRoomContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FChatRoomContent::OnEnter(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_sessionGenerations[sessionId] = routeGeneration;
		Log(Foundation::ELogLevel::Info,
			"room content enter. sessionId={} roomId={} contentInstanceId={} routeGeneration={}",
			sessionId,
			m_roomId,
			m_contentInstanceId,
			routeGeneration);
	}

	void FChatRoomContent::OnLeave(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		const auto sessionGenerationIt = m_sessionGenerations.find(sessionId);
		const bool isCurrentOccupancy = sessionGenerationIt != m_sessionGenerations.end() && sessionGenerationIt->second == routeGeneration;

		Log(Foundation::ELogLevel::Info,
			"room content leave. sessionId={} roomId={} contentInstanceId={} routeGeneration={} stale={}",
			sessionId,
			m_roomId,
			m_contentInstanceId,
			routeGeneration,
			(isCurrentOccupancy ? 0 : 1));
		if (m_roomRegistry != nullptr && isCurrentOccupancy)
		{
			m_roomRegistry->LeaveRoom(sessionId, m_roomId, routeGeneration);
		}
		if (isCurrentOccupancy)
		{
			m_sessionGenerations.erase(sessionId);
		}
	}

	void FChatRoomContent::OnPacket(
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
			case Generated::Chatting::FChattingRq::kOpcode:
				HandleChattingRq(sessionId, payload, bridge);
				return;
			default:
				return;
		}
	}

	void FChatRoomContent::HandleRoomListRq(
		const std::uint64_t sessionId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto roomSnapshots = m_roomRegistry != nullptr ? m_roomRegistry->GetRoomsSnapshot() : std::vector<SRoomInfoSnapshot>{};
		Generated::Chatting::FRoomListRp responsePacket = BuildRoomListResponse(roomSnapshots);

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error,
				"room list response send failed. sessionId={} roomId={} contentInstanceId={}",
				sessionId,
				m_roomId,
				m_contentInstanceId);
		}
		else if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"room list response sent. sessionId={} roomId={} contentInstanceId={} roomCount={}",
				sessionId,
				m_roomId,
				m_contentInstanceId,
				roomSnapshots.size());
		}
	}

	void FChatRoomContent::HandleRoomChangeRq(
		const std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge,
		const std::uint64_t routeGeneration)
	{
		Generated::Chatting::FRoomChangeRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Chatting::FRoomChangeRq::kOpcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "room change deserialize failed.");
			return;
		}

		const auto registryRoomBefore = m_roomRegistry != nullptr ? m_roomRegistry->GetSessionRoomId(sessionId) : std::nullopt;
		ContentsRuntime::Core::FContentInstanceId targetContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		const std::uint64_t targetRouteGeneration = routeGeneration + 1;
		std::shared_ptr<SDeferredRoomActivation> activationState;
		ERoomFlowResultCode resultCode =
			m_roomRegistry != nullptr ? m_roomRegistry->TryChangeRoom(
											sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration, targetContentInstanceId)
									  : ERoomFlowResultCode::InternalError;

		if (resultCode == ERoomFlowResultCode::Success && !bridge.HasContentInstance(targetContentInstanceId))
		{
			if (m_roomRegistry != nullptr)
			{
				m_roomRegistry->RevertChangeRoom(sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration);
			}
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
					m_roomRegistry->RevertChangeRoom(sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration);
				}
				resultCode = ERoomFlowResultCode::RuntimeRouteFailure;
				activationState.reset();
			}
		}

		if (resultCode != ERoomFlowResultCode::Success)
		{
			if (m_logger != nullptr && resultCode == ERoomFlowResultCode::RetryRequired)
			{
				const auto registryRoomAfter = m_roomRegistry != nullptr ? m_roomRegistry->GetSessionRoomId(sessionId) : std::nullopt;
				Log(Foundation::ELogLevel::Warn,
					"room change retry required. sessionId={} contentRoomId={} requestedTargetRoomId={} registryRoomBefore={} "
					"registryRoomAfter={} contentInstanceId={}",
					sessionId,
					m_roomId,
					requestPacket.targetRoomId,
					(registryRoomBefore.has_value() ? std::to_string(*registryRoomBefore) : std::string("none")),
					(registryRoomAfter.has_value() ? std::to_string(*registryRoomAfter) : std::string("none")),
					m_contentInstanceId);
			}
			LogRoomChangeFailure(sessionId, requestPacket.targetRoomId, resultCode);
		}

		if (resultCode == ERoomFlowResultCode::Success)
		{
			RunTransitionResponseRaceInjection(m_runtimeOptions);

			Generated::Chatting::FRoomChangeRp responsePacket;
			responsePacket.previousRoomId = m_roomId;
			responsePacket.currentRoomId = requestPacket.targetRoomId;
			responsePacket.success = true;
			responsePacket.resultCode = static_cast<std::uint16_t>(ERoomFlowResultCode::Success);
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
			{
				Log(Foundation::ELogLevel::Error,

					"room change response send failed before activation. sessionId={} previousRoomId={} currentRoomId={} resultCode={}",
					sessionId,
					m_roomId,
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
		responsePacket.previousRoomId = m_roomId;
		responsePacket.currentRoomId = m_roomId;
		responsePacket.success = false;
		responsePacket.resultCode = static_cast<std::uint16_t>(resultCode);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error,
				"room change response send failed. sessionId={} roomId={} contentInstanceId={} targetRoomId={} resultCode={}",
				sessionId,
				m_roomId,
				m_contentInstanceId,
				requestPacket.targetRoomId,
				ToString(resultCode));
		}
	}

	void FChatRoomContent::HandleChattingRq(
		const std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Chatting::FChattingRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Chatting::FChattingRq::kOpcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "chatting deserialize failed.");
			return;
		}

		const std::optional<std::uint32_t> senderUserId = m_userRegistry != nullptr ? m_userRegistry->GetUserId(sessionId) : std::nullopt;
		const bool success = requestPacket.roomId == m_roomId && requestPacket.payload.size() <= m_runtimeOptions.maxChatPayloadBytes &&
							 senderUserId.has_value();

		Generated::Chatting::FChattingRp responsePacket;
		responsePacket.success = success;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error,
				"chatting response send failed. sessionId={} roomId={} clientMessageId={}",
				sessionId,
				m_roomId,
				requestPacket.clientMessageId);
			return;
		}

		if (!success)
		{
			Log(Foundation::ELogLevel::Warn,
				"chatting rejected. sessionId={} roomId={} requestRoomId={} payloadBytes={} maxPayloadBytes={} hasUserId={}",
				sessionId,
				m_roomId,
				requestPacket.roomId,
				requestPacket.payload.size(),
				m_runtimeOptions.maxChatPayloadBytes,
				(senderUserId.has_value() ? 1 : 0));
			return;
		}

		Generated::Chatting::FBroadcast broadcastPacket;
		broadcastPacket.roomId = m_roomId;
		broadcastPacket.senderUserId = *senderUserId;
		broadcastPacket.messageId = m_nextMessageId++;
		broadcastPacket.sentTick = requestPacket.sentTick;
		broadcastPacket.payload = requestPacket.payload;

		const std::vector<std::uint64_t> sessionIds =
			m_roomRegistry != nullptr ? m_roomRegistry->GetSessionIdsInRoom(m_roomId) : std::vector<std::uint64_t>{};
		std::size_t recipientCount = 0;
		for (const std::uint64_t targetSessionId : sessionIds)
		{
			if (targetSessionId == sessionId)
			{
				continue;
			}

			++recipientCount;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, targetSessionId, broadcastPacket))
			{
				Log(Foundation::ELogLevel::Error,
					"broadcast send failed. senderSessionId={} targetSessionId={} roomId={} messageId={}",
					sessionId,
					targetSessionId,
					m_roomId,
					broadcastPacket.messageId);
			}
		}

		if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"chatting broadcast sent. sessionId={} roomId={} messageId={} recipients={} payloadBytes={}",
				sessionId,
				m_roomId,
				broadcastPacket.messageId,
				recipientCount,
				broadcastPacket.payload.size());
		}
	}

	void FChatRoomContent::LogRoomChangeFailure(
		const std::uint64_t sessionId,
		const std::uint32_t targetRoomId,
		const ERoomFlowResultCode resultCode) const
	{
		Log(IsNormalRoomFlowFailure(resultCode) ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Error,
			"room change failed. sessionId={} currentRoomId={} targetRoomId={} contentInstanceId={} resultCode={}",
			sessionId,
			m_roomId,
			targetRoomId,
			m_contentInstanceId,
			ToString(resultCode));
	}

	void FChatRoomContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "ChattingServer", message);
		}
	}
}
