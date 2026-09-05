#include "EchoServerPch.h"

#include "EchoServer/Contents/Lobby/FLobbyContent.h"

#include "EchoServer/Contents/Room/FRoomRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "EchoServer/Contents/ContentTypes.h"
#include "EchoServer/Contents/Room/RoomFlowTypes.h"
#include "Generated/Packets/Cpp/Chat/ChatPackets.h"

#include <format>
namespace EchoServer::Contents
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

		Generated::Chat::FRoomListRp BuildRoomListResponse(
			const std::vector<SRoomInfoSnapshot>& roomSnapshots)
		{
			Generated::Chat::FRoomListRp responsePacket;
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
	}

	FLobbyContent::FLobbyContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FRoomRegistry> roomRegistry,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_roomRegistry(std::move(roomRegistry))
		, m_runtimeOptions(runtimeOptions)
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
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		std::uint16_t opcode,
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
			case Generated::Chat::FRoomListRq::kOpcode:
				HandleRoomListRq(sessionId, bridge);
				return;
			case Generated::Chat::FRoomEnterRq::kOpcode:
				HandleRoomEnterRq(sessionId, payload, bridge, routeGeneration);
				return;
			default:
				return;
		}
	}

	void FLobbyContent::HandleRoomListRq(
		std::uint64_t sessionId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto roomSnapshots = m_roomRegistry->GetRoomsSnapshot();
		Generated::Chat::FRoomListRp responsePacket = BuildRoomListResponse(roomSnapshots);
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

	void FLobbyContent::HandleRoomEnterRq(
		std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge,
		const std::uint64_t routeGeneration)
	{
		Generated::Chat::FRoomEnterRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Chat::FRoomEnterRq::kOpcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "room enter deserialize failed.");
			return;
		}

		ContentsRuntime::Core::FContentInstanceId targetContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		const std::uint64_t targetRouteGeneration = routeGeneration + 1;
		ERoomFlowResultCode resultCode =
			m_roomRegistry->TryEnterRoom(sessionId, requestPacket.roomId, targetRouteGeneration, targetContentInstanceId);

		if (resultCode == ERoomFlowResultCode::Success && !bridge.HasContentInstance(targetContentInstanceId))
		{
			m_roomRegistry->RevertEnterRoom(sessionId, requestPacket.roomId, targetRouteGeneration);
			resultCode = ERoomFlowResultCode::MissingContentInstance;
		}

		if (resultCode == ERoomFlowResultCode::Success &&
			!bridge.MoveSessionToInstanceWithCompletion(sessionId,
				targetContentInstanceId,
				[bridgePtr = &bridge, logger = m_logger, runtimeOptions = m_runtimeOptions, sessionId, roomId = requestPacket.roomId]()
				{
					RunTransitionResponseRaceInjection(runtimeOptions);
					Generated::Chat::FRoomEnterRp responsePacket;
					responsePacket.roomId = roomId;
					responsePacket.success = true;
					responsePacket.resultCode = static_cast<std::uint16_t>(ERoomFlowResultCode::Success);
					if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePtr, sessionId, responsePacket) && logger != nullptr)
					{
						logger->Log(Foundation::ELogLevel::Error,
							"EchoServer",
							"room enter response send failed after transition. sessionId={} roomId={} resultCode={}",
							sessionId,
							roomId,
							ToString(ERoomFlowResultCode::Success));
					}
				}))
		{
			m_roomRegistry->RevertEnterRoom(sessionId, requestPacket.roomId, targetRouteGeneration);
			resultCode = ERoomFlowResultCode::RuntimeRouteFailure;
		}

		if (resultCode != ERoomFlowResultCode::Success)
		{
			LogRoomEnterFailure(sessionId, requestPacket.roomId, resultCode);
		}

		if (resultCode == ERoomFlowResultCode::Success)
		{
			return;
		}

		Generated::Chat::FRoomEnterRp responsePacket;
		responsePacket.roomId = requestPacket.roomId;
		responsePacket.success = false;
		responsePacket.resultCode = static_cast<std::uint16_t>(resultCode);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error,
				"room enter response send failed. sessionId={} roomId={} resultCode={}",
				sessionId,
				requestPacket.roomId,
				ToString(resultCode));
		}
	}

	void FLobbyContent::LogRoomEnterFailure(
		const std::uint64_t sessionId,
		const std::uint32_t roomId,
		const ERoomFlowResultCode resultCode) const
	{
		Log(IsNormalRoomFlowFailure(resultCode) ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Error,
			"room enter failed. sessionId={} roomId={} resultCode={}",
			sessionId,
			roomId,
			ToString(resultCode));
	}

	void FLobbyContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "EchoServer", message);
		}
	}
}
