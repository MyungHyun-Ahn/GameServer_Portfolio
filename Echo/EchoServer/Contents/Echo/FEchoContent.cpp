#include "EchoServerPch.h"

#include "EchoServer/Contents/Echo/FEchoContent.h"

#include "EchoServer/Contents/Room/FRoomRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "EchoServer/Contents/ContentTypes.h"
#include "EchoServer/Contents/Room/RoomFlowTypes.h"
#include "Generated/Packets/Cpp/Chat/ChatPackets.h"
#include "Generated/Packets/Cpp/Echo/EchoPackets.h"

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
	}

	FEchoContent::FEchoContent(
		std::shared_ptr<Foundation::ILogger> logger,
		ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FRoomRegistry> roomRegistry,
		std::uint32_t roomId,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_roomRegistry(std::move(roomRegistry))
		, m_roomId(roomId)
		, m_runtimeOptions(runtimeOptions)
	{
	}

	ContentsRuntime::Core::FContentId FEchoContent::GetContentId() const noexcept
	{
		return kRoomContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FEchoContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FEchoContent::OnEnter(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_sessionGenerations[sessionId] = routeGeneration;
		if (m_runtimeOptions.enableFirstEchoAfterRoomChangeRaceInjection)
		{
			m_injectFirstEchoAfterRoomChange[sessionId] = true;
		}
		Log(Foundation::ELogLevel::Info,
			"room content enter. sessionId={} roomId={} contentInstanceId={} routeGeneration={}",
			sessionId,
			m_roomId,
			m_contentInstanceId,
			routeGeneration);
	}

	void FEchoContent::OnLeave(
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
			m_injectFirstEchoAfterRoomChange.erase(sessionId);
		}
	}

	void FEchoContent::OnPacket(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		std::uint16_t opcode,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,

				"room onpacket ingress. sessionId={} roomId={} contentInstanceId={} opcode={} routeGeneration={} payloadBytes={}",
				sessionId,
				m_roomId,
				m_contentInstanceId,
				opcode,
				routeGeneration,
				payload.size());
		}

		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
		if (!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId)
		{
			if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
			{
				Log(Foundation::ELogLevel::Warn,
					"stale packet ignored. sessionId={} roomId={} contentInstanceId={} routeGeneration={} opcode={}",
					sessionId,
					m_roomId,
					m_contentInstanceId,
					routeGeneration,
					opcode);
			}
			return;
		}

		const auto generationIt = m_sessionGenerations.find(sessionId);
		if (generationIt == m_sessionGenerations.end() || generationIt->second != routeGeneration)
		{
			if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
			{
				Log(Foundation::ELogLevel::Warn,
					"packet route generation mismatch ignored. sessionId={} roomId={} contentInstanceId={} "
					"packetRouteGeneration={} currentRouteGeneration={} opcode={}",
					sessionId,
					m_roomId,
					m_contentInstanceId,
					routeGeneration,
					(generationIt != m_sessionGenerations.end() ? std::to_string(generationIt->second) : std::string("none")),
					opcode);
			}
			return;
		}

		switch (opcode)
		{
			case Generated::Echo::FEchoRq::kOpcode:
				HandleEchoRq(sessionId, payload, bridge);
				return;
			case Generated::Chat::FRoomListRq::kOpcode:
				HandleRoomListRq(sessionId, bridge);
				return;
			case Generated::Chat::FRoomChangeRq::kOpcode:
				HandleRoomChangeRq(sessionId, payload, bridge, routeGeneration);
				return;
			default:
				return;
		}
	}

	void FEchoContent::OnFrame(
		const int delayFrame,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		++m_frameCount;
		if (!m_runtimeOptions.enableDelegateTestSleep || m_runtimeOptions.delegateTestTargetRoomId == 0 ||
			m_runtimeOptions.delegateTestTargetRoomId != m_roomId || m_runtimeOptions.delegateTestSleepMs <= 0)
		{
			return;
		}

		const std::uint64_t framePeriod = static_cast<std::uint64_t>(std::max(1, m_runtimeOptions.delegateTestSleepEveryNFrames));
		if ((m_frameCount % framePeriod) != 0)
		{
			return;
		}

		if (ShouldTraceSession(m_runtimeOptions, 0))
		{
			Log(Foundation::ELogLevel::Info,
				"delegate test sleep injected. roomId={} contentInstanceId={} sleepMs={} delayFrame={} frameCount={}",
				m_roomId,
				m_contentInstanceId,
				m_runtimeOptions.delegateTestSleepMs,
				delayFrame,
				m_frameCount);
		}

		::Sleep(static_cast<DWORD>(m_runtimeOptions.delegateTestSleepMs));
	}

	void FEchoContent::HandleEchoRq(
		std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Echo::FEchoRq packet;
		NetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Echo::FEchoRq::kOpcode, payload, borrowedViewScope, packet))
		{
			Log(Foundation::ELogLevel::Warn, "echo deserialize failed.");
			return;
		}

		if (m_runtimeOptions.logPackets)
		{
			Log(Foundation::ELogLevel::Info,
				"received. sessionId={} roomId={} contentInstanceId={} opcode={} message={}",
				sessionId,
				m_roomId,
				m_contentInstanceId,
				packet.GetOpcode(),
				packet.GetMessageValue());
		}

		if (ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"echo request accepted. sessionId={} roomId={} contentInstanceId={} message={}",
				sessionId,
				m_roomId,
				m_contentInstanceId,
				packet.GetMessageValue());
		}

		if (m_runtimeOptions.enableFirstEchoAfterRoomChangeRaceInjection)
		{
			const auto injectIt = m_injectFirstEchoAfterRoomChange.find(sessionId);
			if (injectIt != m_injectFirstEchoAfterRoomChange.end() && injectIt->second)
			{
				injectIt->second = false;
				RunRaceInjection(m_runtimeOptions.firstEchoAfterRoomChangeRaceInjectionMode);
				if (ShouldTraceSession(m_runtimeOptions, sessionId))
				{
					Log(Foundation::ELogLevel::Info,
						"first echo race injection triggered. sessionId={} roomId={} contentInstanceId={}",
						sessionId,
						m_roomId,
						m_contentInstanceId);
				}
			}
		}

		if (m_runtimeOptions.sendThreadCount == 1 && m_runtimeOptions.responsesPerThread == 1)
		{
			Generated::Echo::FEchoRp responsePacket;
			responsePacket.SetMessageValue(packet.GetMessageValue());
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
			{
				Log(Foundation::ELogLevel::Error,
					"echo response send failed. sessionId={} roomId={} contentInstanceId={} message={}",
					sessionId,
					m_roomId,
					m_contentInstanceId,
					packet.GetMessageValue());
			}
			else if (ShouldTraceSession(m_runtimeOptions, sessionId))
			{
				Log(Foundation::ELogLevel::Info,
					"echo response sent. sessionId={} roomId={} contentInstanceId={} message={}",
					sessionId,
					m_roomId,
					m_contentInstanceId,
					packet.GetMessageValue());
			}
			return;
		}

		std::vector<std::thread> sendThreads;
		sendThreads.reserve(static_cast<std::size_t>(m_runtimeOptions.sendThreadCount));
		for (int threadIndex = 0; threadIndex < m_runtimeOptions.sendThreadCount; ++threadIndex)
		{
			sendThreads.emplace_back(
				[&, threadIndex, sessionId, message = std::string(packet.GetMessageValue())]()
				{
					for (int responseIndex = 0; responseIndex < m_runtimeOptions.responsesPerThread; ++responseIndex)
					{
						std::ostringstream responseBuilder;
						responseBuilder << message << "|t=" << threadIndex << "|r=" << responseIndex;

						Generated::Echo::FEchoRp responsePacket;
						responsePacket.SetMessageValue(responseBuilder.str());
						if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
						{
							Log(Foundation::ELogLevel::Error,
								"echo response send failed. sessionId={} roomId={} contentInstanceId={} message={}",
								sessionId,
								m_roomId,
								m_contentInstanceId,
								responseBuilder.str());
						}
						else if (ShouldTraceSession(m_runtimeOptions, sessionId))
						{
							Log(Foundation::ELogLevel::Info,
								"echo response sent. sessionId={} roomId={} contentInstanceId={} message={}",
								sessionId,
								m_roomId,
								m_contentInstanceId,
								responseBuilder.str());
						}
					}
				});
		}

		for (std::thread& sendThread : sendThreads)
		{
			sendThread.join();
		}
	}

	void FEchoContent::HandleRoomListRq(
		std::uint64_t sessionId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto roomSnapshots = m_roomRegistry != nullptr ? m_roomRegistry->GetRoomsSnapshot() : std::vector<SRoomInfoSnapshot>{};
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

	void FEchoContent::HandleRoomChangeRq(
		std::uint64_t sessionId,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge,
		const std::uint64_t routeGeneration)
	{
		Generated::Chat::FRoomChangeRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Chat::FRoomChangeRq::kOpcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "room change deserialize failed.");
			return;
		}

		const auto registryRoomBefore = m_roomRegistry != nullptr ? m_roomRegistry->GetSessionRoomId(sessionId) : std::nullopt;
		ContentsRuntime::Core::FContentInstanceId targetContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		const std::uint64_t targetRouteGeneration = routeGeneration + 1;
		if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"room change request begin. sessionId={} currentRoomId={} requestedTargetRoomId={} contentInstanceId={} "
				"routeGeneration={} targetRouteGeneration={} registryRoomBefore={}",
				sessionId,
				m_roomId,
				requestPacket.targetRoomId,
				m_contentInstanceId,
				routeGeneration,
				targetRouteGeneration,
				(registryRoomBefore.has_value() ? std::to_string(*registryRoomBefore) : std::string("none")));
		}

		ERoomFlowResultCode resultCode =
			m_roomRegistry != nullptr ? m_roomRegistry->TryChangeRoom(
											sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration, targetContentInstanceId)
									  : ERoomFlowResultCode::InternalError;
		if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
		{
			Log(Foundation::ELogLevel::Info,
				"room change registry result. sessionId={} currentRoomId={} requestedTargetRoomId={} "
				"targetContentInstanceId={} resultCode={}",
				sessionId,
				m_roomId,
				requestPacket.targetRoomId,
				targetContentInstanceId,
				ToString(resultCode));
		}

		if (resultCode == ERoomFlowResultCode::Success && !bridge.HasContentInstance(targetContentInstanceId))
		{
			if (m_roomRegistry != nullptr)
			{
				m_roomRegistry->RevertChangeRoom(sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration);
			}
			resultCode = ERoomFlowResultCode::MissingContentInstance;
		}

		bool moveAccepted = false;
		if (resultCode == ERoomFlowResultCode::Success)
		{
			moveAccepted = bridge.MoveSessionToInstanceWithCompletion(sessionId,
				targetContentInstanceId,
				[bridgePtr = &bridge,
					logger = m_logger,
					runtimeOptions = m_runtimeOptions,
					previousRoomId = m_roomId,
					targetRoomId = requestPacket.targetRoomId,
					sessionId]()
				{
					if (logger != nullptr && (runtimeOptions.logPackets || ShouldTraceSession(runtimeOptions, sessionId)))
					{
						logger->Log(Foundation::ELogLevel::Info,
							"EchoServer",
							"room change completion callback begin. sessionId={} previousRoomId={} targetRoomId={}",
							sessionId,
							previousRoomId,
							targetRoomId);
					}
					RunTransitionResponseRaceInjection(runtimeOptions);
					Generated::Chat::FRoomChangeRp responsePacket;
					responsePacket.previousRoomId = previousRoomId;
					responsePacket.currentRoomId = targetRoomId;
					responsePacket.success = true;
					responsePacket.resultCode = static_cast<std::uint16_t>(ERoomFlowResultCode::Success);
					if (!ContentsRuntime::Bridge::SendContentPacket(*bridgePtr, sessionId, responsePacket) && logger != nullptr)
					{
						logger->Log(Foundation::ELogLevel::Error,
							"EchoServer",
							"room change response send failed after transition. sessionId={} previousRoomId={} currentRoomId={} "
							"resultCode={}",
							sessionId,
							previousRoomId,
							targetRoomId,
							ToString(ERoomFlowResultCode::Success));
					}
					else if (runtimeOptions.enablePostRoomChangeResponseRaceInjection)
					{
						RunRaceInjection(runtimeOptions.postRoomChangeResponseRaceInjectionMode);
					}
				});

			if (m_runtimeOptions.logPackets || ShouldTraceSession(m_runtimeOptions, sessionId))
			{
				Log(Foundation::ELogLevel::Info,
					"room change move request result. sessionId={} currentRoomId={} requestedTargetRoomId={} "
					"targetContentInstanceId={} targetRouteGeneration={} accepted={}",
					sessionId,
					m_roomId,
					requestPacket.targetRoomId,
					targetContentInstanceId,
					targetRouteGeneration,
					(moveAccepted ? 1 : 0));
			}
		}

		if (resultCode == ERoomFlowResultCode::Success && !moveAccepted)
		{
			if (m_roomRegistry != nullptr)
			{
				m_roomRegistry->RevertChangeRoom(sessionId, m_roomId, requestPacket.targetRoomId, targetRouteGeneration);
			}
			resultCode = ERoomFlowResultCode::RuntimeRouteFailure;
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
		else if (m_runtimeOptions.logPackets)
		{
			const auto registryRoomAfter = m_roomRegistry != nullptr ? m_roomRegistry->GetSessionRoomId(sessionId) : std::nullopt;
			Log(Foundation::ELogLevel::Info,
				"room change succeeded. sessionId={} previousRoomId={} targetRoomId={} targetContentInstanceId={} "
				"registryRoomBefore={} registryRoomAfter={}",
				sessionId,
				m_roomId,
				requestPacket.targetRoomId,
				targetContentInstanceId,
				(registryRoomBefore.has_value() ? std::to_string(*registryRoomBefore) : std::string("none")),
				(registryRoomAfter.has_value() ? std::to_string(*registryRoomAfter) : std::string("none")));
		}

		if (resultCode == ERoomFlowResultCode::Success)
		{
			return;
		}

		Generated::Chat::FRoomChangeRp responsePacket;
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

	void FEchoContent::LogRoomChangeFailure(
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

	void FEchoContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "EchoServer", message);
		}
	}
}
