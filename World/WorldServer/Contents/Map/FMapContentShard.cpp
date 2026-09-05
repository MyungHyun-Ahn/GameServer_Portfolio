#include "WorldServerPch.h"

#include "WorldServer/Contents/Map/FMapContentShard.h"

#include "WorldServer/Contents/Session/FWorldSession.h"
#include "WorldServer/Contents/Session/FWorldSessionRegistry.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutionService.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutor.h"
#include "WorldServer/Domain/FPlayerStatCalculator.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/FMapInstance.h"
#include "WorldCore/Map/FMapInstanceManager.h"

namespace WorldServer::Contents
{
	namespace
	{
		RpcLib::Client::FOutboundRpcClient& GetRequiredCacheRpcClient(
			const std::shared_ptr<RpcLib::Client::FOutboundRpcClient>& cacheRpcClient)
		{
			if (cacheRpcClient == nullptr)
			{
				throw std::invalid_argument("World map shard cache RPC client is null.");
			}
			return *cacheRpcClient;
		}

		template <typename TResponse>
		void FillEquipmentResponse(
			TResponse& response,
			const EWorldResultCode resultCode,
			const std::uint64_t requestId,
			const std::uint64_t itemInstanceId,
			const std::uint64_t itemVersion,
			const bool equipped,
			const WorldCore::SPlayerRuntimeSnapshot* const snapshot,
			const WorldCore::FPlayerEntity* const player)
		{
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = requestId;
			response.itemInstanceId = itemInstanceId;
			response.itemVersion = itemVersion;
			response.equipped = equipped;
			if (snapshot == nullptr)
			{
				return;
			}

			response.finalStr = snapshot->finalStr;
			response.finalDex = snapshot->finalDex;
			response.finalInt = snapshot->finalIntelligence;
			response.finalLuk = snapshot->finalLuk;
			response.currentHp = player != nullptr ? player->GetCurrentHp() : snapshot->maxHp;
			response.maxHp = snapshot->maxHp;
			response.currentMp = player != nullptr ? player->GetCurrentMp() : snapshot->maxMp;
			response.maxMp = snapshot->maxMp;
			response.attack = snapshot->attack;
			response.defense = snapshot->defense;
			response.moveSpeedMilli = snapshot->moveSpeedMilli;
			response.equipmentVersion = snapshot->equipmentVersion;
			response.statRevision = snapshot->statRevision;
		}

		EWorldResultCode MapPlayerAttackRejectReason(
			const WorldCore::EPlayerAttackRejectReason reason) noexcept
		{
			switch (reason)
			{
				case WorldCore::EPlayerAttackRejectReason::AttackerDead:
					return EWorldResultCode::AttackAttackerDead;
				case WorldCore::EPlayerAttackRejectReason::InvalidTarget:
					return EWorldResultCode::AttackTargetInvalid;
				case WorldCore::EPlayerAttackRejectReason::TargetDead:
					return EWorldResultCode::AttackTargetDead;
				case WorldCore::EPlayerAttackRejectReason::OutOfRange:
					return EWorldResultCode::AttackOutOfRange;
				case WorldCore::EPlayerAttackRejectReason::Cooldown:
					return EWorldResultCode::AttackCooldown;
				case WorldCore::EPlayerAttackRejectReason::InvalidAttacker:
				default:
					return EWorldResultCode::AttackRejected;
			}
		}
	}

	FMapContentShard::FMapContentShard(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const std::uint32_t shardIndex,
		const std::uint32_t shardCount,
		const std::uint32_t targetFps,
		const std::uint64_t maxPacketQueueDepth,
		std::shared_ptr<FWorldSessionRegistry> sessionRegistry,
		std::shared_ptr<FTaskGraphSectorExecutionService> taskGraphExecutionService,
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
		const SCachePresenceConfig cachePresenceConfig,
		std::shared_ptr<const Domain::FPlayerStatCalculator> playerStatCalculator)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_shardIndex(shardIndex)
		, m_shardCount(shardCount)
		, m_targetFps(std::max<std::uint32_t>(1u, targetFps))
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
		, m_sessionRegistry(std::move(sessionRegistry))
		, m_taskGraphExecutionService(std::move(taskGraphExecutionService))
		, m_cacheRpcClient(std::move(cacheRpcClient))
		, m_cachePresenceConfig(cachePresenceConfig)
		, m_playerStatCalculator(std::move(playerStatCalculator))
		, m_rpcCommon(GetRequiredCacheRpcClient(m_cacheRpcClient).GetSessionRegistry(),
			  m_rpcDispatcher,
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetRequestIdGenerator(),
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetTransport(),
			  contentInstanceId)
		, m_mapInstanceManager(std::make_unique<WorldCore::FMapInstanceManager>())
	{
		if (m_playerStatCalculator == nullptr)
		{
			throw std::invalid_argument("World map shard player stat calculator is null.");
		}
	}

	FMapContentShard::~FMapContentShard() = default;

	bool FMapContentShard::Initialize(
		const std::vector<SBootMapDefinition>& mapDefinitions,
		std::string& outError)
	{
		if (m_shardCount == 0 || m_shardIndex >= m_shardCount || m_mapInstanceManager == nullptr)
		{
			outError = "invalid map shard configuration.";
			return false;
		}

		for (const SBootMapDefinition& bootMap : mapDefinitions)
		{
			const std::size_t expectedShardIndex = GetMapContentShardIndex(bootMap.mapInstanceId, m_shardCount);
			if (expectedShardIndex != m_shardIndex)
			{
				outError = std::format(
					"MapInstanceId {} belongs to shard {}, not shard {}.", bootMap.mapInstanceId, expectedShardIndex, m_shardIndex);
				return false;
			}

			WorldCore::EMapCreateResult createResult = WorldCore::EMapCreateResult::InvalidDefinition;
			std::string createError;
			WorldCore::FMapInstance* mapInstance = nullptr;
			if (bootMap.definition.sectorExecutionMode == WorldCore::ESectorExecutionMode::Serial)
			{
				mapInstance = m_mapInstanceManager->CreateMap(bootMap.mapInstanceId, bootMap.definition, createResult, createError);
			}
			else if (bootMap.definition.sectorExecutionMode == WorldCore::ESectorExecutionMode::TaskGraph)
			{
				if (m_taskGraphExecutionService == nullptr)
				{
					outError = std::format("MapDataId {} requests TaskGraph, but its execution service is unavailable.", bootMap.mapDataId);
					return false;
				}
				auto executor = std::make_unique<FTaskGraphSectorExecutor>(m_taskGraphExecutionService,
					m_contentInstanceId,
					[this](WorldCore::SMapTickExecutionCompletion completion)
					{
						HandleTaskGraphCompletion(std::move(completion));
					});
				mapInstance = m_mapInstanceManager->CreateMapWithExecutor(
					bootMap.mapInstanceId, bootMap.definition, std::move(executor), createResult, createError);
			}
			else
			{
				createError = "Unknown Sector execution mode.";
			}

			if (mapInstance == nullptr)
			{
				outError = std::format("map instance creation failed. mapDataId={} mapInstanceId={} result={} error={}",
					bootMap.mapDataId,
					bootMap.mapInstanceId,
					static_cast<std::uint32_t>(createResult),
					createError);
				return false;
			}

			std::string monsterSpawnError;
			if (!mapInstance->ConfigureMonsterSpawning(kDefaultMonsterSpawnSeed, bootMap.monsterSpawners, monsterSpawnError))
			{
				outError = std::format("monster spawn configuration failed. mapDataId={} mapInstanceId={} error={}",
					bootMap.mapDataId,
					bootMap.mapInstanceId,
					monsterSpawnError);
				return false;
			}

			if (!m_mapInstanceIdsByMapDataId.emplace(bootMap.mapDataId, bootMap.mapInstanceId).second)
			{
				outError = std::format("MapDataId {} is duplicated inside shard {}.", bootMap.mapDataId, m_shardIndex);
				return false;
			}
		}

		outError.clear();
		return true;
	}

	ContentsRuntime::Core::FContentId FMapContentShard::GetContentId() const noexcept
	{
		return kMapContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FMapContentShard::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint32_t FMapContentShard::GetTargetFps() const noexcept
	{
		return m_targetFps;
	}

	std::uint64_t FMapContentShard::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FMapContentShard::OnEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr || !session->IsConnected() || !bridge.IsSessionAlive(sessionId))
		{
			return;
		}

		m_sessionGenerations[sessionId] = routeGeneration;
		Log(Foundation::ELogLevel::Info,
			"session entered map shard. sessionId={} routeGeneration={} shardIndex={}",
			sessionId,
			routeGeneration,
			m_shardIndex);
	}

	void FMapContentShard::OnLeave(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto generationIt = m_sessionGenerations.find(sessionId);
		const bool isCurrentGeneration = generationIt != m_sessionGenerations.end() && generationIt->second == routeGeneration;
		if (isCurrentGeneration)
		{
			CancelDeferredMapEnter(sessionId);
			CancelPendingEquipmentMutation(sessionId);
			if (RemoveLocalPlayer(sessionId, bridge))
			{
				m_sessionGenerations.erase(generationIt);
				m_deferredLeaveGenerations.erase(sessionId);
			}
			else
			{
				m_deferredLeaveGenerations[sessionId] = routeGeneration;
			}
		}

		Log(Foundation::ELogLevel::Info,
			"session left map shard. sessionId={} routeGeneration={} shardIndex={} stale={}",
			sessionId,
			routeGeneration,
			m_shardIndex,
			(isCurrentGeneration ? 0 : 1));

		if (!bridge.IsSessionAlive(sessionId) && m_sessionRegistry != nullptr)
		{
			m_sessionRegistry->Remove(sessionId);
		}
	}

	void FMapContentShard::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
		const auto generationIt = m_sessionGenerations.find(sessionId);
		if (!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId ||
			generationIt == m_sessionGenerations.end() || generationIt->second != routeGeneration)
		{
			return;
		}

		switch (opcode)
		{
			case Generated::Map::FMapEnterRq::kOpcode:
				HandleMapEnter(sessionId, routeGeneration, payload, bridge);
				return;
			case Generated::Map::FMoveRq::kOpcode:
				HandleMove(sessionId, payload, bridge);
				return;
			case Generated::Map::FBasicAttackRq::kOpcode:
				HandleBasicAttack(sessionId, payload, bridge);
				return;
			case Generated::World::FEquipItemRq::kOpcode:
			case Generated::World::FUnequipItemRq::kOpcode:
				HandleEquipmentMutation(sessionId, routeGeneration, opcode, payload, bridge);
				return;
			default:
				Log(Foundation::ELogLevel::Warn,
					"map shard rejected an unsupported packet. sessionId={} opcode={} shardIndex={}",
					sessionId,
					opcode,
					m_shardIndex);
				return;
		}
	}

	void FMapContentShard::OnFrame(
		const int delayFrame,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (m_taskGraphExecutionService != nullptr)
		{
			m_taskGraphExecutionService->DrainOwnerCallbacks(m_contentInstanceId);
			if (m_taskGraphExecutionService->IsStopping())
			{
				return;
			}
		}
		m_rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
		RemoveDisconnectedSessions(bridge);
		ProcessDeferredExternalEvents(bridge);
		ProcessDeferredEquipmentMutations(bridge);
		if (m_mapInstanceManager == nullptr)
		{
			return;
		}
		if (delayFrame > 1)
		{
			Log(Foundation::ELogLevel::Warn,
				"map frame delayed; unbounded catch-up is skipped. shardIndex={} delayFrame={} mapCount={}",
				m_shardIndex,
				delayFrame,
				m_mapInstanceManager->GetMapCount());
		}

		std::vector<WorldCore::FMapInstanceId> restartMapInstanceIds;
		std::vector<WorldCore::SMapTickResult> tickResults = m_mapInstanceManager->TickAll();
		for (const WorldCore::SMapTickResult& tickResult : tickResults)
		{
			TrackStartedTickMoves(tickResult);
			TrackStartedTickAttacks(tickResult);

			if (tickResult.result == WorldCore::EMapTickResult::Pending)
			{
				continue;
			}
			if (tickResult.result == WorldCore::EMapTickResult::Failed)
			{
				if (m_taskGraphExecutionService != nullptr && m_taskGraphExecutionService->IsStopping())
				{
					return;
				}
				Log(Foundation::ELogLevel::Error,
					"map tick failed. shardIndex={} mapInstanceId={} tickIndex={} tickGeneration={} delayFrame={} error={}",
					m_shardIndex,
					tickResult.mapInstanceId,
					tickResult.tickIndex,
					tickResult.tickGeneration,
					delayFrame,
					tickResult.failureReason);
				FailTickMoves(tickResult.mapInstanceId, tickResult.consumedMoveRequests, EWorldResultCode::InternalError, bridge);
				FailTickAttacks(tickResult.mapInstanceId,
					tickResult.consumedAttackRequests,
					EWorldResultCode::InternalError,
					tickResult.tickIndex,
					bridge);
				continue;
			}
			if (!tickResult.executionStarted)
			{
				restartMapInstanceIds.push_back(tickResult.mapInstanceId);
			}

			WorldCore::FMapInstance* const mapInstance = m_mapInstanceManager->FindMap(tickResult.mapInstanceId);
			if (mapInstance == nullptr)
			{
				Log(Foundation::ELogLevel::Error,
					"completed map tick references a missing map. shardIndex={} mapInstanceId={} tickIndex={}",
					m_shardIndex,
					tickResult.mapInstanceId,
					tickResult.tickIndex);
				FailTickMoves(tickResult.mapInstanceId, tickResult.consumedMoveRequests, EWorldResultCode::InternalError, bridge);
				FailTickAttacks(tickResult.mapInstanceId,
					tickResult.consumedAttackRequests,
					EWorldResultCode::InternalError,
					tickResult.tickIndex,
					bridge);
				continue;
			}

			for (const WorldCore::SMoveResult& moveResult : tickResult.moveResults)
			{
				const auto sessionIt = m_sessionIdsByEntityId.find(moveResult.entityId);
				if (sessionIt == m_sessionIdsByEntityId.end())
				{
					continue;
				}

				const auto localSessionIt = m_localSessions.find(sessionIt->second);
				if (localSessionIt == m_localSessions.end() || localSessionIt->second.inFlightMoveSequence != moveResult.sequence)
				{
					Log(Foundation::ELogLevel::Error,
						"Move result does not match pending input. sessionId={} entityId={} sequence={}",
						sessionIt->second,
						moveResult.entityId,
						moveResult.sequence);
					continue;
				}
				localSessionIt->second.inFlightMoveSequence = 0;

				Generated::Map::FMoveRp response;
				response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::Success);
				response.sequence = moveResult.sequence;
				response.moveState = static_cast<std::uint8_t>(moveResult.moveState);
				response.acceptedPositionX = moveResult.acceptedPosition.x;
				response.acceptedPositionY = moveResult.acceptedPosition.y;
				response.directionX = moveResult.direction.x;
				response.directionY = moveResult.direction.y;
				response.isCorrected = moveResult.isCorrected;
				if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionIt->second, response))
				{
					Log(Foundation::ELogLevel::Warn,
						"Move response send failed. sessionId={} entityId={} sequence={}",
						sessionIt->second,
						moveResult.entityId,
						moveResult.sequence);
				}
			}
			FailQueuedMoves(tickResult.mapInstanceId, tickResult.rejectedMoveRequests, EWorldResultCode::MoveRejected, bridge);

			for (const WorldCore::SPlayerAttackResult& attackResult : tickResult.playerAttackResults)
			{
				const auto sessionIt = m_sessionIdsByEntityId.find(attackResult.attackerEntityId);
				if (sessionIt == m_sessionIdsByEntityId.end())
				{
					continue;
				}

				const auto localSessionIt = m_localSessions.find(sessionIt->second);
				if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != tickResult.mapInstanceId ||
					localSessionIt->second.inFlightAttackSequences.erase(attackResult.attackSequence) == 0)
				{
					Log(Foundation::ELogLevel::Error,
						"BasicAttack result does not match an in-flight request. sessionId={} attackerEntityId={} sequence={}",
						sessionIt->second,
						attackResult.attackerEntityId,
						attackResult.attackSequence);
					continue;
				}

				Generated::Map::FBasicAttackRp response;
				response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::Success);
				response.attackSequence = attackResult.attackSequence;
				response.serverTick = tickResult.tickIndex;
				if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionIt->second, response))
				{
					Log(Foundation::ELogLevel::Warn,
						"BasicAttack response send failed. sessionId={} attackerEntityId={} sequence={}",
						sessionIt->second,
						attackResult.attackerEntityId,
						attackResult.attackSequence);
				}
			}
			CompleteRejectedAttacks(tickResult.mapInstanceId, tickResult.rejectedAttackRequests, tickResult.tickIndex, bridge);

			// A completed tick must resolve every consumed request. Keep the wire contract
			// total even if a future WorldCore change accidentally omits a result.
			for (const WorldCore::SPlayerAttackRequestIdentity& attackRequest : tickResult.consumedAttackRequests)
			{
				const auto sessionIt = m_sessionIdsByEntityId.find(attackRequest.attackerEntityId);
				if (sessionIt == m_sessionIdsByEntityId.end())
				{
					continue;
				}
				const auto localSessionIt = m_localSessions.find(sessionIt->second);
				if (localSessionIt == m_localSessions.end() ||
					!localSessionIt->second.inFlightAttackSequences.contains(attackRequest.attackSequence))
				{
					continue;
				}

				Log(Foundation::ELogLevel::Error,
					"Completed map tick omitted a BasicAttack outcome. sessionId={} attackerEntityId={} sequence={}",
					sessionIt->second,
					attackRequest.attackerEntityId,
					attackRequest.attackSequence);
				const std::array unresolvedRequest{attackRequest};
				FailTickAttacks(tickResult.mapInstanceId, unresolvedRequest, EWorldResultCode::InternalError, tickResult.tickIndex, bridge);
			}

			DispatchActorAttackEvents(tickResult.actorAttackEvents, tickResult.tickIndex, bridge);
			DispatchActorDeathEvents(tickResult.actorDeathEvents, bridge);
			DispatchActorRespawnEvents(tickResult.actorRespawnEvents, bridge);
			DispatchVisibilityEvents(*mapInstance, tickResult.visibilityEvents, tickResult.tickIndex, bridge);
		}

		ProcessDeferredExternalEvents(bridge);
		ProcessDeferredEquipmentMutations(bridge);
		for (const WorldCore::FMapInstanceId mapInstanceId : restartMapInstanceIds)
		{
			if (m_taskGraphExecutionService != nullptr && m_taskGraphExecutionService->IsStopping())
			{
				return;
			}
			WorldCore::FMapInstance* const mapInstance = m_mapInstanceManager->FindMap(mapInstanceId);
			if (mapInstance == nullptr || mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
			{
				continue;
			}

			const WorldCore::SMapTickResult startResult = mapInstance->Tick();
			TrackStartedTickMoves(startResult);
			TrackStartedTickAttacks(startResult);
			if (startResult.result == WorldCore::EMapTickResult::Failed)
			{
				if (m_taskGraphExecutionService != nullptr && m_taskGraphExecutionService->IsStopping())
				{
					return;
				}
				Log(Foundation::ELogLevel::Error,
					"restarted async map tick failed. shardIndex={} mapInstanceId={} tickIndex={} generation={} error={}",
					m_shardIndex,
					startResult.mapInstanceId,
					startResult.tickIndex,
					startResult.tickGeneration,
					startResult.failureReason);
				FailTickMoves(startResult.mapInstanceId, startResult.consumedMoveRequests, EWorldResultCode::InternalError, bridge);
				FailTickAttacks(startResult.mapInstanceId,
					startResult.consumedAttackRequests,
					EWorldResultCode::InternalError,
					startResult.tickIndex,
					bridge);
			}
			else if (startResult.result != WorldCore::EMapTickResult::Pending || !startResult.executionStarted)
			{
				Log(Foundation::ELogLevel::Error,
					"async map restart violated the Pending execution contract. shardIndex={} mapInstanceId={} result={} started={}",
					m_shardIndex,
					startResult.mapInstanceId,
					static_cast<std::uint32_t>(startResult.result),
					startResult.executionStarted);
			}
		}
	}

	void FMapContentShard::HandleMapEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Map::FMapEnterRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Map::FMapEnterRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "MapEnter deserialize failed in map shard. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}
		const auto mapIdIt = m_mapInstanceIdsByMapDataId.find(request.mapDataId);
		if (mapIdIt == m_mapInstanceIdsByMapDataId.end() || m_mapInstanceManager == nullptr)
		{
			const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
			if (session != nullptr)
			{
				const std::optional<SPendingMapEnter> pending = session->ConsumePendingMapEnter(request.requestId, request.mapDataId);
				if (pending.has_value())
				{
					Generated::Map::FMapEnterRp response;
					response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::MapNotFound);
					response.requestId = request.requestId;
					ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response);
				}
			}
			return;
		}

		WorldCore::FMapInstance* const mapInstance = m_mapInstanceManager->FindMap(mapIdIt->second);
		if (mapInstance == nullptr)
		{
			Log(Foundation::ELogLevel::Error,
				"MapEnter route references a missing map. sessionId={} mapDataId={} mapInstanceId={}",
				sessionId,
				request.mapDataId,
				mapIdIt->second);
			const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
			if (session != nullptr)
			{
				const std::optional<SPendingMapEnter> pending = session->ConsumePendingMapEnter(request.requestId, request.mapDataId);
				if (pending.has_value())
				{
					Generated::Map::FMapEnterRp response;
					response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::MapNotFound);
					response.requestId = request.requestId;
					ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response);
				}
			}
			if (bridge.IsSessionAlive(sessionId))
			{
				bridge.DisconnectSession(sessionId);
			}
			return;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		const std::optional<SPendingMapEnter> pending =
			session != nullptr ? session->FindPendingMapEnter(request.requestId, request.mapDataId) : std::nullopt;
		if (!pending.has_value())
		{
			Log(Foundation::ELogLevel::Warn,
				"MapEnter packet no longer owns pending state. sessionId={} requestId={} mapDataId={}",
				sessionId,
				request.requestId,
				request.mapDataId);
			return;
		}

		const SDeferredMapEnter deferredEnter{
			sessionId, routeGeneration, request.requestId, request.mapDataId, mapIdIt->second, pending->requestToken};
		if (mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
		{
			const auto [deferredIt, inserted] = m_deferredMapEnters.emplace(sessionId, deferredEnter);
			if (!inserted && (deferredIt->second.routeGeneration != routeGeneration || deferredIt->second.requestId != request.requestId ||
								 deferredIt->second.mapDataId != request.mapDataId ||
								 deferredIt->second.requestToken.operationId != pending->requestToken.operationId))
			{
				Log(Foundation::ELogLevel::Error,
					"MapEnter deferred state conflict. sessionId={} requestId={} mapDataId={}",
					sessionId,
					request.requestId,
					request.mapDataId);
				bridge.DisconnectSession(sessionId);
			}
			return;
		}

		CompleteMapEnter(deferredEnter, bridge);
	}

	void FMapContentShard::CompleteMapEnter(
		const SDeferredMapEnter& deferredEnter,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::shared_ptr<FWorldSession> session =
			m_sessionRegistry != nullptr ? m_sessionRegistry->Find(deferredEnter.sessionId) : nullptr;
		const auto generationIt = m_sessionGenerations.find(deferredEnter.sessionId);
		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(deferredEnter.sessionId);
		if (session == nullptr || !session->IsConnected() || !bridge.IsSessionAlive(deferredEnter.sessionId) ||
			generationIt == m_sessionGenerations.end() || generationIt->second != deferredEnter.routeGeneration ||
			!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId)
		{
			if (session != nullptr)
			{
				const std::optional<SPendingMapEnter> stalePending =
					session->ConsumePendingMapEnter(deferredEnter.requestId, deferredEnter.mapDataId, deferredEnter.requestToken);
				if (stalePending.has_value())
				{
					session->CancelRequest(stalePending->requestToken);
				}
			}
			return;
		}

		const std::optional<SPendingMapEnter> pending =
			session->ConsumePendingMapEnter(deferredEnter.requestId, deferredEnter.mapDataId, deferredEnter.requestToken);
		if (!pending.has_value())
		{
			Log(Foundation::ELogLevel::Warn,
				"MapEnter deferred request no longer owns pending state. sessionId={} requestId={} mapDataId={}",
				deferredEnter.sessionId,
				deferredEnter.requestId,
				deferredEnter.mapDataId);
			return;
		}

		if (m_localSessions.contains(deferredEnter.sessionId))
		{
			Generated::Map::FMapEnterRp response;
			response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::AlreadyInMap);
			response.requestId = deferredEnter.requestId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response);
			return;
		}

		auto failMapEnter = [&](const EWorldResultCode resultCode)
		{
			Generated::Map::FMapEnterRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = deferredEnter.requestId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response);
			if (bridge.IsSessionAlive(deferredEnter.sessionId))
			{
				bridge.DisconnectSession(deferredEnter.sessionId);
			}
		};

		WorldCore::FMapInstance* const mapInstance =
			m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(deferredEnter.mapInstanceId) : nullptr;
		if (mapInstance == nullptr)
		{
			failMapEnter(EWorldResultCode::MapNotFound);
			return;
		}
		if (mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
		{
			Log(Foundation::ELogLevel::Error,
				"MapEnter was applied before the Map became idle. sessionId={} mapInstanceId={}",
				deferredEnter.sessionId,
				deferredEnter.mapInstanceId);
			session->CancelRequest(pending->requestToken);
			return;
		}

		if (!session->IsPlayerReady())
		{
			failMapEnter(EWorldResultCode::PlayerNotReady);
			return;
		}

		const WorldCore::FEntityId entityId = deferredEnter.sessionId;
		const WorldCore::FUserId userId = session->GetUserId();
		const std::optional<WorldCore::SPlayerRuntimeSnapshot> runtimeSnapshot = session->GetPlayerRuntimeSnapshot();
		if (!runtimeSnapshot.has_value())
		{
			failMapEnter(EWorldResultCode::PlayerNotReady);
			return;
		}
		const WorldCore::SVector2 direction{0.0f, 1.0f};
		std::vector<WorldCore::SVisibilityEvent> visibilityEvents;
		std::string addError;
		const bool playerAdded =
			mapInstance->AddPlayerAtRandomSpawn(entityId, userId, direction, *runtimeSnapshot, visibilityEvents, addError);
		if (!playerAdded)
		{
			Log(Foundation::ELogLevel::Error,
				"MapEnter player registration failed. sessionId={} mapInstanceId={} error={}",
				deferredEnter.sessionId,
				deferredEnter.mapInstanceId,
				addError);
			failMapEnter(EWorldResultCode::EntityRegistrationFailed);
			return;
		}
		const WorldCore::FPlayerEntity* const enteredPlayer = mapInstance->FindPlayer(entityId);
		if (enteredPlayer == nullptr)
		{
			failMapEnter(EWorldResultCode::InternalError);
			return;
		}
		const WorldCore::SVector2 position = enteredPlayer->GetPosition();

		const SLocalSessionState localState{deferredEnter.routeGeneration, deferredEnter.mapInstanceId, entityId, userId, 0, 0};
		const bool sessionInserted = m_localSessions.emplace(deferredEnter.sessionId, localState).second;
		const bool entityInserted = sessionInserted && m_sessionIdsByEntityId.emplace(entityId, deferredEnter.sessionId).second;
		if (!sessionInserted || !entityInserted)
		{
			std::vector<WorldCore::SVisibilityEvent> ignoredEvents;
			std::string ignoredError;
			(void)mapInstance->RemovePlayer(entityId, ignoredEvents, ignoredError);
			m_localSessions.erase(deferredEnter.sessionId);
			m_sessionIdsByEntityId.erase(entityId);
			failMapEnter(EWorldResultCode::InternalError);
			return;
		}

		Generated::Map::FMapEnterRp response;
		response.resultCode = static_cast<std::uint16_t>(EWorldResultCode::Success);
		response.requestId = deferredEnter.requestId;
		response.mapInstanceId = deferredEnter.mapInstanceId;
		response.entityId = entityId;
		response.positionX = position.x;
		response.positionY = position.y;
		response.directionX = direction.x;
		response.directionY = direction.y;
		response.serverTick = mapInstance->GetTickIndex();
		if (runtimeSnapshot.has_value())
		{
			response.characterId = runtimeSnapshot->characterId;
			response.characterDataId = runtimeSnapshot->characterDataId;
			response.level = runtimeSnapshot->level;
			response.exp = runtimeSnapshot->exp;
			response.requiredExpToNextLevel = runtimeSnapshot->requiredExpToNextLevel;
			response.strStat = runtimeSnapshot->str;
			response.dexStat = runtimeSnapshot->dex;
			response.intStat = runtimeSnapshot->intelligence;
			response.lukStat = runtimeSnapshot->luk;
			response.unspentStatPoints = runtimeSnapshot->unspentStatPoints;
			response.progressVersion = runtimeSnapshot->progressVersion;
			response.statVersion = runtimeSnapshot->statVersion;
			response.finalStr = runtimeSnapshot->finalStr;
			response.finalDex = runtimeSnapshot->finalDex;
			response.finalInt = runtimeSnapshot->finalIntelligence;
			response.finalLuk = runtimeSnapshot->finalLuk;
			response.currentHp = enteredPlayer->GetCurrentHp();
			response.maxHp = runtimeSnapshot->maxHp;
			response.currentMp = enteredPlayer->GetCurrentMp();
			response.maxMp = runtimeSnapshot->maxMp;
			response.attack = runtimeSnapshot->attack;
			response.defense = runtimeSnapshot->defense;
			response.moveSpeedMilli = runtimeSnapshot->moveSpeedMilli;
			response.equipmentVersion = runtimeSnapshot->equipmentVersion;
			response.statRevision = runtimeSnapshot->statRevision;
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response))
		{
			std::vector<WorldCore::SVisibilityEvent> ignoredEvents;
			std::string ignoredError;
			(void)mapInstance->RemovePlayer(entityId, ignoredEvents, ignoredError);
			m_sessionIdsByEntityId.erase(entityId);
			m_localSessions.erase(deferredEnter.sessionId);
			return;
		}

		DispatchVisibilityEvents(*mapInstance, visibilityEvents, mapInstance->GetTickIndex(), bridge);
	}

	void FMapContentShard::CancelDeferredMapEnter(
		const std::uint64_t sessionId)
	{
		const auto deferredIt = m_deferredMapEnters.find(sessionId);
		if (deferredIt == m_deferredMapEnters.end())
		{
			return;
		}

		const SDeferredMapEnter deferredEnter = deferredIt->second;
		m_deferredMapEnters.erase(deferredIt);
		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr)
		{
			return;
		}

		const std::optional<SPendingMapEnter> pending =
			session->ConsumePendingMapEnter(deferredEnter.requestId, deferredEnter.mapDataId, deferredEnter.requestToken);
		if (pending.has_value())
		{
			session->CancelRequest(pending->requestToken);
		}
	}

	void FMapContentShard::ProcessDeferredExternalEvents(
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		std::vector<std::uint64_t> leaveSessionIds;
		leaveSessionIds.reserve(m_deferredLeaveGenerations.size());
		for (const auto& [sessionId, routeGeneration] : m_deferredLeaveGenerations)
		{
			(void)routeGeneration;
			leaveSessionIds.push_back(sessionId);
		}

		for (const std::uint64_t sessionId : leaveSessionIds)
		{
			const auto leaveIt = m_deferredLeaveGenerations.find(sessionId);
			if (leaveIt == m_deferredLeaveGenerations.end())
			{
				continue;
			}
			const auto localSessionIt = m_localSessions.find(sessionId);
			if (localSessionIt != m_localSessions.end() && localSessionIt->second.routeGeneration != leaveIt->second)
			{
				m_deferredLeaveGenerations.erase(leaveIt);
				continue;
			}
			if (!RemoveLocalPlayer(sessionId, bridge))
			{
				continue;
			}

			const std::uint64_t leaveGeneration = leaveIt->second;
			m_deferredLeaveGenerations.erase(leaveIt);
			const auto generationIt = m_sessionGenerations.find(sessionId);
			if (generationIt != m_sessionGenerations.end() && generationIt->second == leaveGeneration)
			{
				m_sessionGenerations.erase(generationIt);
			}
			if (!bridge.IsSessionAlive(sessionId) && m_sessionRegistry != nullptr)
			{
				m_sessionRegistry->Remove(sessionId);
			}
		}

		std::vector<std::uint64_t> enterSessionIds;
		enterSessionIds.reserve(m_deferredMapEnters.size());
		for (const auto& [sessionId, deferredEnter] : m_deferredMapEnters)
		{
			(void)deferredEnter;
			enterSessionIds.push_back(sessionId);
		}

		for (const std::uint64_t sessionId : enterSessionIds)
		{
			const auto deferredIt = m_deferredMapEnters.find(sessionId);
			if (deferredIt == m_deferredMapEnters.end())
			{
				continue;
			}
			WorldCore::FMapInstance* const mapInstance =
				m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(deferredIt->second.mapInstanceId) : nullptr;
			if (mapInstance != nullptr && mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
			{
				continue;
			}

			const SDeferredMapEnter deferredEnter = deferredIt->second;
			m_deferredMapEnters.erase(deferredIt);
			CompleteMapEnter(deferredEnter, bridge);
		}
	}

	void FMapContentShard::HandleTaskGraphCompletion(
		WorldCore::SMapTickExecutionCompletion completion)
	{
		if (m_mapInstanceManager == nullptr)
		{
			return;
		}

		const WorldCore::SMapTickTicket ticket = completion.ticket;
		const WorldCore::EMapTickCompletionResult result = m_mapInstanceManager->CompleteTickExecution(std::move(completion));
		if (result != WorldCore::EMapTickCompletionResult::Accepted)
		{
			Log(Foundation::ELogLevel::Warn,
				"TaskGraph completion rejected. mapInstanceId={} mapIncarnation={} tickIndex={} generation={} result={}",
				ticket.mapInstanceId,
				ticket.mapIncarnation,
				ticket.tickIndex,
				ticket.generation,
				static_cast<std::uint32_t>(result));
		}
	}

	void FMapContentShard::TrackStartedTickMoves(
		const WorldCore::SMapTickResult& tickResult)
	{
		if (!tickResult.executionStarted)
		{
			return;
		}

		for (const WorldCore::SMoveRequestIdentity& moveRequest : tickResult.consumedMoveRequests)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(moveRequest.entityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}

			const auto localSessionIt = m_localSessions.find(sessionIt->second);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != tickResult.mapInstanceId)
			{
				continue;
			}
			if (localSessionIt->second.queuedMoveSequence != moveRequest.sequence)
			{
				Log(Foundation::ELogLevel::Error,
					"Map Tick consumed an unexpected queued Move. sessionId={} entityId={} sequence={} queuedSequence={}",
					sessionIt->second,
					moveRequest.entityId,
					moveRequest.sequence,
					localSessionIt->second.queuedMoveSequence);
				continue;
			}

			localSessionIt->second.queuedMoveSequence = 0;
			localSessionIt->second.inFlightMoveSequence = moveRequest.sequence;
		}
	}

	void FMapContentShard::TrackStartedTickAttacks(
		const WorldCore::SMapTickResult& tickResult)
	{
		if (!tickResult.executionStarted)
		{
			return;
		}

		for (const WorldCore::SPlayerAttackRequestIdentity& attackRequest : tickResult.consumedAttackRequests)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(attackRequest.attackerEntityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}

			const auto localSessionIt = m_localSessions.find(sessionIt->second);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != tickResult.mapInstanceId)
			{
				continue;
			}

			SLocalSessionState& localState = localSessionIt->second;
			if (localState.queuedAttackSequences.erase(attackRequest.attackSequence) == 0)
			{
				Log(Foundation::ELogLevel::Error,
					"Map Tick consumed an unexpected queued BasicAttack. sessionId={} attackerEntityId={} sequence={}",
					sessionIt->second,
					attackRequest.attackerEntityId,
					attackRequest.attackSequence);
			}

			if (!localState.inFlightAttackSequences.insert(attackRequest.attackSequence).second)
			{
				Log(Foundation::ELogLevel::Error,
					"Map Tick consumed a duplicate in-flight BasicAttack. sessionId={} attackerEntityId={} sequence={}",
					sessionIt->second,
					attackRequest.attackerEntityId,
					attackRequest.attackSequence);
			}
		}
	}

	void FMapContentShard::HandleEquipmentMutation(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const bool isEquip = opcode == Generated::World::FEquipItemRq::kOpcode;
		std::uint64_t requestId = 0;
		std::uint64_t itemInstanceId = 0;
		std::uint64_t expectedItemVersion = 0;
		if (isEquip)
		{
			Generated::World::FEquipItemRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "EquipItem deserialize failed. sessionId={}", sessionId);
				bridge.DisconnectSession(sessionId);
				return;
			}
			requestId = request.requestId;
			itemInstanceId = request.itemInstanceId;
			expectedItemVersion = request.expectedItemVersion;
		}
		else
		{
			Generated::World::FUnequipItemRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				Log(Foundation::ELogLevel::Warn, "UnequipItem deserialize failed. sessionId={}", sessionId);
				bridge.DisconnectSession(sessionId);
				return;
			}
			requestId = request.requestId;
			itemInstanceId = request.itemInstanceId;
			expectedItemVersion = request.expectedItemVersion;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr)
		{
			return;
		}

		auto sendUntrackedFailure = [&](const EWorldResultCode resultCode)
		{
			if (isEquip)
			{
				Generated::World::FEquipItemRp response;
				FillEquipmentResponse(response, resultCode, requestId, itemInstanceId, expectedItemVersion, false, nullptr, nullptr);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else
			{
				Generated::World::FUnequipItemRp response;
				FillEquipmentResponse(response, resultCode, requestId, itemInstanceId, expectedItemVersion, true, nullptr, nullptr);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
		};

		ContentsRuntime::Session::FRequestProcessingToken requestToken{};
		const ContentsRuntime::Session::EBeginRequestResult beginResult =
			session->TryBeginRequest(requestId, opcode, ContentsRuntime::Session::ERequestProcessingPolicy::Exclusive, requestToken);
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::Started)
		{
			sendUntrackedFailure(beginResult == ContentsRuntime::Session::EBeginRequestResult::AlreadyProcessing
									 ? EWorldResultCode::RequestAlreadyProcessing
									 : EWorldResultCode::SessionUnavailable);
			return;
		}

		auto sendTrackedFailure = [&](const EWorldResultCode resultCode)
		{
			if (isEquip)
			{
				Generated::World::FEquipItemRp response;
				FillEquipmentResponse(response, resultCode, requestId, itemInstanceId, expectedItemVersion, false, nullptr, nullptr);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			}
			else
			{
				Generated::World::FUnequipItemRp response;
				FillEquipmentResponse(response, resultCode, requestId, itemInstanceId, expectedItemVersion, true, nullptr, nullptr);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			}
		};

		if (requestId == 0 || itemInstanceId == 0 || expectedItemVersion == 0)
		{
			sendTrackedFailure(EWorldResultCode::InvalidRequest);
			return;
		}
		const auto localIt = m_localSessions.find(sessionId);
		const std::optional<WorldCore::SPlayerRuntimeSnapshot> currentSnapshot = session->GetPlayerRuntimeSnapshot();
		const WorldCore::FUserId userId = session->GetUserId();
		const std::uint64_t ownerGeneration = session->GetCacheOwnerGeneration();
		if (!session->IsConnected() || !session->IsAuthenticated() || !session->IsPlayerReady() || !bridge.IsSessionAlive(sessionId))
		{
			sendTrackedFailure(EWorldResultCode::PlayerNotReady);
			return;
		}
		if (!m_cachePresenceConfig.enabled || userId == WorldCore::kInvalidUserId || ownerGeneration == 0 || !currentSnapshot.has_value())
		{
			sendTrackedFailure(EWorldResultCode::CacheUnavailable);
			return;
		}
		if (localIt == m_localSessions.end() || localIt->second.routeGeneration != routeGeneration || localIt->second.userId != userId ||
			localIt->second.mapInstanceId == WorldCore::kInvalidMapInstanceId || localIt->second.entityId == WorldCore::kInvalidEntityId)
		{
			sendTrackedFailure(EWorldResultCode::SessionUnavailable);
			return;
		}

		SPendingEquipmentMutation pending;
		pending.session = session;
		pending.mutation = isEquip ? EEquipmentMutation::Equip : EEquipmentMutation::Unequip;
		pending.sessionId = sessionId;
		pending.routeGeneration = routeGeneration;
		pending.requestId = requestId;
		pending.itemInstanceId = itemInstanceId;
		pending.expectedItemVersion = expectedItemVersion;
		pending.resultItemVersion = expectedItemVersion;
		pending.expectedStatRevision = currentSnapshot->statRevision;
		pending.expectedEquipmentVersion = currentSnapshot->equipmentVersion;
		pending.mapInstanceId = localIt->second.mapInstanceId;
		pending.entityId = localIt->second.entityId;
		pending.requestToken = requestToken;
		const auto [pendingIt, inserted] = m_pendingEquipmentMutations.emplace(sessionId, std::move(pending));
		if (!inserted)
		{
			sendTrackedFailure(EWorldResultCode::InternalError);
			return;
		}

		const std::uint64_t operationId = requestToken.operationId;
		auto onSuccess = [this, sessionId, operationId](const Cache::Protocol::EPlayerEquipmentResult result,
							 const Cache::Protocol::FPlayerWorldSnapshot& snapshot,
							 const std::uint64_t itemVersion,
							 const bool equipped,
							 const bool stateInvalidated)
		{
			const auto found = m_pendingEquipmentMutations.find(sessionId);
			if (found == m_pendingEquipmentMutations.end() || found->second.rpcCompleted ||
				found->second.requestToken.operationId != operationId)
			{
				return;
			}

			SPendingEquipmentMutation& current = found->second;
			current.resultCode = MapEquipmentResult(result);
			current.resultItemVersion = itemVersion;
			current.resultEquipped = equipped;
			if (stateInvalidated || result == Cache::Protocol::EPlayerEquipmentResult::OutcomeUnknown ||
				result == Cache::Protocol::EPlayerEquipmentResult::CharacterDataError ||
				result == Cache::Protocol::EPlayerEquipmentResult::UnauthorizedCaller ||
				result == Cache::Protocol::EPlayerEquipmentResult::ConcurrentModification)
			{
				current.disconnectAfterResponse = true;
			}
			if (current.resultCode == EWorldResultCode::Success)
			{
				const bool expectedEquipped = current.mutation == EEquipmentMutation::Equip;
				std::string calculateError;
				if (itemVersion == 0 || equipped != expectedEquipped || m_playerStatCalculator == nullptr ||
					!m_playerStatCalculator->Calculate(snapshot, current.runtimeSnapshot, calculateError) ||
					current.runtimeSnapshot.statRevision <= current.expectedStatRevision ||
					current.runtimeSnapshot.equipmentVersion == current.expectedEquipmentVersion)
				{
					Log(Foundation::ELogLevel::Error,
						"Cache equipment response could not be applied. sessionId={} requestId={} itemVersion={} equipped={} error={}",
						current.sessionId,
						current.requestId,
						itemVersion,
						equipped,
						calculateError);
					current.resultCode = EWorldResultCode::InternalError;
					current.disconnectAfterResponse = true;
				}
			}
			current.rpcCompleted = true;
		};
		auto onFailure = [this, sessionId, operationId](const RpcLib::Protocol::FRpcCallFailure& failure)
		{
			const auto found = m_pendingEquipmentMutations.find(sessionId);
			if (found == m_pendingEquipmentMutations.end() || found->second.rpcCompleted ||
				found->second.requestToken.operationId != operationId)
			{
				return;
			}
			found->second.resultCode = EWorldResultCode::CacheUnavailable;
			found->second.disconnectAfterResponse = !RpcLib::Protocol::IsRequestDefinitelyNotDispatched(failure);
			found->second.rpcCompleted = true;
		};

		const RpcLib::Protocol::FRpcTarget target = BuildCacheTarget(userId);
		RpcLib::Protocol::FRpcCallStartResult callResult;
		if (isEquip)
		{
			callResult = m_rpcCommon.Call<Cache::Protocol::FEquipPlayerItemV2Rpc>(target,
				m_cachePresenceConfig.rpcTimeout,
				onSuccess,
				onFailure,
				static_cast<std::uint64_t>(userId),
				sessionId,
				ownerGeneration,
				itemInstanceId,
				expectedItemVersion,
				currentSnapshot->statRevision,
				currentSnapshot->equipmentVersion);
		}
		else
		{
			callResult = m_rpcCommon.Call<Cache::Protocol::FUnequipPlayerItemV2Rpc>(target,
				m_cachePresenceConfig.rpcTimeout,
				onSuccess,
				onFailure,
				static_cast<std::uint64_t>(userId),
				sessionId,
				ownerGeneration,
				itemInstanceId,
				expectedItemVersion,
				currentSnapshot->statRevision,
				currentSnapshot->equipmentVersion);
		}
		if (!callResult.accepted)
		{
			pendingIt->second.resultCode = EWorldResultCode::CacheUnavailable;
			pendingIt->second.rpcCompleted = true;
		}
	}

	void FMapContentShard::ProcessDeferredEquipmentMutations(
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		std::vector<std::uint64_t> completedSessionIds;
		completedSessionIds.reserve(m_pendingEquipmentMutations.size());
		for (const auto& [sessionId, pending] : m_pendingEquipmentMutations)
		{
			if (pending.rpcCompleted)
			{
				completedSessionIds.push_back(sessionId);
			}
		}

		for (const std::uint64_t sessionId : completedSessionIds)
		{
			const auto pendingIt = m_pendingEquipmentMutations.find(sessionId);
			if (pendingIt == m_pendingEquipmentMutations.end())
			{
				continue;
			}
			SPendingEquipmentMutation& pending = pendingIt->second;
			WorldCore::FMapInstance* const mapInstance =
				m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(pending.mapInstanceId) : nullptr;
			if (pending.resultCode == EWorldResultCode::Success && mapInstance != nullptr &&
				mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
			{
				continue;
			}

			const std::shared_ptr<FWorldSession> session = pending.session;
			const auto localIt = m_localSessions.find(sessionId);
			const auto generationIt = m_sessionGenerations.find(sessionId);
			const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
			if (session == nullptr || !session->IsConnected() || !session->IsCurrentRequest(pending.requestToken) ||
				!bridge.IsSessionAlive(sessionId) || localIt == m_localSessions.end() ||
				localIt->second.routeGeneration != pending.routeGeneration || localIt->second.mapInstanceId != pending.mapInstanceId ||
				localIt->second.entityId != pending.entityId || generationIt == m_sessionGenerations.end() ||
				generationIt->second != pending.routeGeneration || !currentContentInstanceId.has_value() ||
				*currentContentInstanceId != m_contentInstanceId)
			{
				if (session != nullptr)
				{
					(void)session->CancelRequest(pending.requestToken);
				}
				m_pendingEquipmentMutations.erase(pendingIt);
				continue;
			}

			if (pending.resultCode == EWorldResultCode::Success)
			{
				const std::optional<WorldCore::SPlayerRuntimeSnapshot> sessionSnapshot = session->GetPlayerRuntimeSnapshot();
				const WorldCore::FPlayerEntity* const currentPlayer =
					mapInstance != nullptr ? mapInstance->FindPlayer(pending.entityId) : nullptr;
				if (!sessionSnapshot.has_value() || currentPlayer == nullptr || !currentPlayer->HasRuntimeSnapshot() ||
					sessionSnapshot->statRevision != pending.expectedStatRevision ||
					sessionSnapshot->equipmentVersion != pending.expectedEquipmentVersion ||
					currentPlayer->GetRuntimeSnapshot().statRevision != pending.expectedStatRevision ||
					currentPlayer->GetRuntimeSnapshot().equipmentVersion != pending.expectedEquipmentVersion)
				{
					pending.resultCode = EWorldResultCode::ConcurrentModification;
					pending.disconnectAfterResponse = true;
				}
				else
				{
					std::string applyError;
					if (!session->TryApplyPlayerRuntimeSnapshot(pending.expectedStatRevision,
							pending.expectedEquipmentVersion,
							pending.runtimeSnapshot,
							[&]()
							{
								return mapInstance->ApplyPlayerRuntimeSnapshot(pending.entityId, pending.runtimeSnapshot, applyError);
							}))
					{
						Log(Foundation::ELogLevel::Error,
							"World equipment snapshot apply failed. sessionId={} requestId={} mapInstanceId={} error={}",
							sessionId,
							pending.requestId,
							pending.mapInstanceId,
							applyError);
						pending.resultCode = EWorldResultCode::InternalError;
						pending.disconnectAfterResponse = true;
					}
				}
			}

			const WorldCore::FPlayerEntity* const appliedPlayer = pending.resultCode == EWorldResultCode::Success && mapInstance != nullptr
																	  ? mapInstance->FindPlayer(pending.entityId)
																	  : nullptr;
			if (pending.mutation == EEquipmentMutation::Equip)
			{
				Generated::World::FEquipItemRp response;
				FillEquipmentResponse(response,
					pending.resultCode,
					pending.requestId,
					pending.itemInstanceId,
					pending.resultItemVersion,
					pending.resultEquipped,
					pending.resultCode == EWorldResultCode::Success ? &pending.runtimeSnapshot : nullptr,
					appliedPlayer);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending.requestToken, response);
			}
			else
			{
				Generated::World::FUnequipItemRp response;
				FillEquipmentResponse(response,
					pending.resultCode,
					pending.requestId,
					pending.itemInstanceId,
					pending.resultItemVersion,
					pending.resultEquipped,
					pending.resultCode == EWorldResultCode::Success ? &pending.runtimeSnapshot : nullptr,
					appliedPlayer);
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending.requestToken, response);
			}
			const bool disconnect = pending.disconnectAfterResponse;
			m_pendingEquipmentMutations.erase(pendingIt);
			if (disconnect)
			{
				session->ClearPlayerReady();
				(void)bridge.DisconnectSession(sessionId);
			}
		}
	}

	void FMapContentShard::CancelPendingEquipmentMutation(
		const std::uint64_t sessionId)
	{
		const auto found = m_pendingEquipmentMutations.find(sessionId);
		if (found == m_pendingEquipmentMutations.end())
		{
			return;
		}
		if (found->second.session != nullptr)
		{
			(void)found->second.session->CancelRequest(found->second.requestToken);
		}
		m_pendingEquipmentMutations.erase(found);
	}

	RpcLib::Protocol::FRpcTarget FMapContentShard::BuildCacheTarget(
		const WorldCore::FUserId userId) const noexcept
	{
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cachePresenceConfig.cacheServerInstanceId;
		target.routingKey = userId;
		return target;
	}

	EWorldResultCode FMapContentShard::MapEquipmentResult(
		const Cache::Protocol::EPlayerEquipmentResult result) noexcept
	{
		switch (result)
		{
			case Cache::Protocol::EPlayerEquipmentResult::Success:
				return EWorldResultCode::Success;
			case Cache::Protocol::EPlayerEquipmentResult::InvalidArgument:
				return EWorldResultCode::InvalidRequest;
			case Cache::Protocol::EPlayerEquipmentResult::ConcurrentModification:
				return EWorldResultCode::ConcurrentModification;
			case Cache::Protocol::EPlayerEquipmentResult::UnauthorizedCaller:
				return EWorldResultCode::PlayerRevoked;
			case Cache::Protocol::EPlayerEquipmentResult::ItemNotFound:
				return EWorldResultCode::ItemNotFound;
			case Cache::Protocol::EPlayerEquipmentResult::ItemVersionMismatch:
				return EWorldResultCode::ItemVersionMismatch;
			case Cache::Protocol::EPlayerEquipmentResult::NotEquipment:
				return EWorldResultCode::NotEquipment;
			case Cache::Protocol::EPlayerEquipmentResult::EquipmentStateConflict:
				return EWorldResultCode::EquipmentStateConflict;
			case Cache::Protocol::EPlayerEquipmentResult::DatabaseError:
			case Cache::Protocol::EPlayerEquipmentResult::CharacterDataError:
			case Cache::Protocol::EPlayerEquipmentResult::OutcomeUnknown:
			default:
				return EWorldResultCode::CacheUnavailable;
		}
	}

	void FMapContentShard::HandleMove(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Map::FMoveRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Map::FMoveRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "Move deserialize failed. sessionId={}", sessionId);
			return;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr)
		{
			return;
		}

		ContentsRuntime::Session::FRequestProcessingToken ignoredToken{};
		const auto beginResult = session->TryBeginRequest(request.sequence,
			Generated::Map::FMoveRq::kOpcode,
			ContentsRuntime::Session::ERequestProcessingPolicy::AllowedWhileBusy,
			ignoredToken);
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::AllowedWithoutTracking)
		{
			return;
		}

		const auto localSessionIt = m_localSessions.find(sessionId);
		WorldCore::FMapInstance* mapInstance = nullptr;
		const WorldCore::FPlayerEntity* player = nullptr;
		if (localSessionIt != m_localSessions.end() && m_mapInstanceManager != nullptr)
		{
			mapInstance = m_mapInstanceManager->FindMap(localSessionIt->second.mapInstanceId);
			if (mapInstance != nullptr)
			{
				player = mapInstance->FindPlayer(localSessionIt->second.entityId);
			}
		}

		auto sendMoveFailure =
			[&](const EWorldResultCode resultCode, const WorldCore::FMoveSequence sequence, const std::uint8_t responseMoveState)
		{
			Generated::Map::FMoveRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.sequence = sequence;
			response.moveState = responseMoveState;
			response.isCorrected = true;
			if (player != nullptr)
			{
				response.acceptedPositionX = player->GetPosition().x;
				response.acceptedPositionY = player->GetPosition().y;
				response.directionX = player->GetDirection().x;
				response.directionY = player->GetDirection().y;
			}
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		};

		if (!session->IsConnected() || !session->IsPlayerReady() || !bridge.IsSessionAlive(sessionId))
		{
			sendMoveFailure(EWorldResultCode::PlayerRevoked, request.sequence, request.moveState);
			return;
		}

		if (localSessionIt == m_localSessions.end() || mapInstance == nullptr || player == nullptr)
		{
			sendMoveFailure(EWorldResultCode::SessionUnavailable, request.sequence, request.moveState);
			return;
		}

		const WorldCore::EMoveState moveState = static_cast<WorldCore::EMoveState>(request.moveState);
		if (request.sequence == 0 || (moveState != WorldCore::EMoveState::Start && moveState != WorldCore::EMoveState::Sync &&
										 moveState != WorldCore::EMoveState::Stop))
		{
			sendMoveFailure(EWorldResultCode::InvalidRequest, request.sequence, request.moveState);
			return;
		}

		WorldCore::SMoveCommand command{};
		command.entityId = localSessionIt->second.entityId;
		command.sequence = request.sequence;
		command.moveState = moveState;
		command.clientPosition = {request.clientPositionX, request.clientPositionY};
		command.direction = {request.directionX, request.directionY};
		if (!mapInstance->QueueMove(command))
		{
			sendMoveFailure(EWorldResultCode::MoveRejected, request.sequence, request.moveState);
			return;
		}

		if (localSessionIt->second.queuedMoveSequence != 0)
		{
			sendMoveFailure(EWorldResultCode::MoveSuperseded,
				localSessionIt->second.queuedMoveSequence,
				static_cast<std::uint8_t>(player->GetMoveState()));
		}

		localSessionIt->second.queuedMoveSequence = request.sequence;
	}

	void FMapContentShard::HandleBasicAttack(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Map::FBasicAttackRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Map::FBasicAttackRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "BasicAttack deserialize failed. sessionId={}", sessionId);
			return;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr)
		{
			return;
		}

		ContentsRuntime::Session::FRequestProcessingToken ignoredToken{};
		const auto beginResult = session->TryBeginRequest(request.attackSequence,
			Generated::Map::FBasicAttackRq::kOpcode,
			ContentsRuntime::Session::ERequestProcessingPolicy::AllowedWhileBusy,
			ignoredToken);
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::AllowedWithoutTracking)
		{
			return;
		}

		const auto localSessionIt = m_localSessions.find(sessionId);
		WorldCore::FMapInstance* mapInstance = nullptr;
		if (localSessionIt != m_localSessions.end() && m_mapInstanceManager != nullptr)
		{
			mapInstance = m_mapInstanceManager->FindMap(localSessionIt->second.mapInstanceId);
		}
		auto sendResponse = [&](const EWorldResultCode resultCode)
		{
			Generated::Map::FBasicAttackRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.attackSequence = request.attackSequence;
			response.serverTick = mapInstance != nullptr ? mapInstance->GetTickIndex() : 0;
			(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		};

		if (!session->IsConnected() || !session->IsPlayerReady() || !bridge.IsSessionAlive(sessionId))
		{
			sendResponse(EWorldResultCode::PlayerRevoked);
			return;
		}
		if (localSessionIt == m_localSessions.end() || mapInstance == nullptr ||
			mapInstance->FindPlayer(localSessionIt->second.entityId) == nullptr)
		{
			sendResponse(EWorldResultCode::SessionUnavailable);
			return;
		}
		if (request.attackSequence == 0 || request.targetEntityId == WorldCore::kInvalidEntityId ||
			localSessionIt->second.queuedAttackSequences.contains(request.attackSequence) ||
			localSessionIt->second.inFlightAttackSequences.contains(request.attackSequence))
		{
			sendResponse(EWorldResultCode::InvalidRequest);
			return;
		}

		const WorldCore::SPlayerAttackCommand command{localSessionIt->second.entityId, request.attackSequence, request.targetEntityId};
		if (!mapInstance->QueuePlayerAttack(command))
		{
			sendResponse(EWorldResultCode::AttackRejected);
			return;
		}
		localSessionIt->second.queuedAttackSequences.insert(request.attackSequence);
	}

	void FMapContentShard::DispatchVisibilityEvents(
		const WorldCore::FMapInstance& mapInstance,
		const std::span<const WorldCore::SVisibilityEvent> visibilityEvents,
		const std::uint64_t serverTick,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SVisibilityEvent& event : visibilityEvents)
		{
			const auto observerIt = m_sessionIdsByEntityId.find(event.observerEntityId);
			if (observerIt == m_sessionIdsByEntityId.end() || !bridge.IsSessionAlive(observerIt->second))
			{
				continue;
			}

			bool sent = false;
			switch (event.kind)
			{
				case WorldCore::EVisibilityEventKind::Spawn:
				{
					const WorldCore::FPlayerEntity* const player = mapInstance.FindPlayer(event.subjectEntityId);
					const WorldCore::FMonsterEntity* const monster = mapInstance.FindMonster(event.subjectEntityId);
					if (player == nullptr && monster == nullptr)
					{
						continue;
					}

					Generated::Map::FActorSpawnNoti notification;
					notification.entityId = event.subjectEntityId;
					if (player != nullptr)
					{
						notification.actorKind = static_cast<std::uint8_t>(WorldCore::EActorKind::Player);
						notification.actorDataId = player->HasRuntimeSnapshot() ? player->GetRuntimeSnapshot().characterDataId : 0;
						notification.lifeRevision = player->GetLifeRevision();
						notification.moveState = static_cast<std::uint8_t>(player->GetMoveState());
						if (player->HasRuntimeSnapshot())
						{
							notification.currentHp = player->GetCurrentHp();
							notification.maxHp = player->GetRuntimeSnapshot().maxHp;
						}
					}
					else
					{
						notification.actorKind = static_cast<std::uint8_t>(WorldCore::EActorKind::Monster);
						notification.actorDataId = monster->GetMonsterDataId();
						notification.lifeRevision = monster->GetSpawnGeneration();
						notification.moveState = static_cast<std::uint8_t>(monster->GetMoveState());
						notification.currentHp = monster->GetCurrentHp();
						notification.maxHp = monster->GetRuntimeSnapshot().maxHp;
					}
					notification.positionX = event.position.x;
					notification.positionY = event.position.y;
					notification.directionX = event.direction.x;
					notification.directionY = event.direction.y;
					notification.moveSequence = event.moveSequence;
					notification.serverTick = serverTick;
					sent = ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification);
					break;
				}
				case WorldCore::EVisibilityEventKind::Despawn:
				{
					Generated::Map::FActorDespawnNoti notification;
					notification.entityId = event.subjectEntityId;
					sent = ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification);
					break;
				}
				case WorldCore::EVisibilityEventKind::Move:
				{
					const WorldCore::FPlayerEntity* const player = mapInstance.FindPlayer(event.subjectEntityId);
					const WorldCore::FMonsterEntity* const monster = mapInstance.FindMonster(event.subjectEntityId);
					if (player == nullptr && monster == nullptr)
					{
						continue;
					}

					Generated::Map::FMoveNoti notification;
					notification.entityId = event.subjectEntityId;
					notification.sequence = event.moveSequence;
					notification.moveState =
						static_cast<std::uint8_t>(player != nullptr ? player->GetMoveState() : monster->GetMoveState());
					notification.positionX = event.position.x;
					notification.positionY = event.position.y;
					notification.directionX = event.direction.x;
					notification.directionY = event.direction.y;
					notification.serverTick = serverTick;
					sent = ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification);
					break;
				}
			}

			if (!sent)
			{
				Log(Foundation::ELogLevel::Warn,
					"visibility notification send failed. observerSessionId={} observerEntityId={} subjectEntityId={} kind={}",
					observerIt->second,
					event.observerEntityId,
					event.subjectEntityId,
					static_cast<std::uint32_t>(event.kind));
			}
		}
	}

	void FMapContentShard::DispatchActorAttackEvents(
		const std::span<const WorldCore::SActorAttackEvent> attackEvents,
		const std::uint64_t serverTick,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SActorAttackEvent& event : attackEvents)
		{
			const auto observerIt = m_sessionIdsByEntityId.find(event.observerEntityId);
			if (observerIt == m_sessionIdsByEntityId.end() || !bridge.IsSessionAlive(observerIt->second))
			{
				continue;
			}

			Generated::Map::FActorAttackNoti notification;
			notification.attackerEntityId = event.attack.attackerEntityId;
			notification.targetEntityId = event.attack.targetEntityId;
			notification.damage = event.attack.damage;
			notification.targetCurrentHp = event.attack.targetCurrentHp;
			notification.targetMaxHp = event.attack.targetMaxHp;
			notification.serverTick = serverTick;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification))
			{
				Log(Foundation::ELogLevel::Warn,
					"attack notification send failed. observerSessionId={} observerEntityId={} attackerEntityId={} targetEntityId={}",
					observerIt->second,
					event.observerEntityId,
					event.attack.attackerEntityId,
					event.attack.targetEntityId);
			}
		}
	}

	void FMapContentShard::DispatchActorDeathEvents(
		const std::span<const WorldCore::SActorDeathEvent> deathEvents,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SActorDeathEvent& event : deathEvents)
		{
			const auto observerIt = m_sessionIdsByEntityId.find(event.observerEntityId);
			if (observerIt == m_sessionIdsByEntityId.end() || !bridge.IsSessionAlive(observerIt->second))
			{
				continue;
			}

			Generated::Map::FActorDeathNoti notification;
			notification.entityId = event.death.entityId;
			notification.killerEntityId = event.death.killerEntityId;
			notification.lifeRevision = event.death.lifeRevision;
			notification.serverTick = event.death.serverTick;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification))
			{
				Log(Foundation::ELogLevel::Warn,
					"death notification send failed. observerSessionId={} observerEntityId={} entityId={} lifeRevision={}",
					observerIt->second,
					event.observerEntityId,
					event.death.entityId,
					event.death.lifeRevision);
			}
		}
	}

	void FMapContentShard::DispatchActorRespawnEvents(
		const std::span<const WorldCore::SActorRespawnEvent> respawnEvents,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SActorRespawnEvent& event : respawnEvents)
		{
			const auto observerIt = m_sessionIdsByEntityId.find(event.observerEntityId);
			if (observerIt == m_sessionIdsByEntityId.end() || !bridge.IsSessionAlive(observerIt->second))
			{
				continue;
			}

			Generated::Map::FActorRespawnNoti notification;
			notification.entityId = event.respawn.entityId;
			notification.positionX = event.respawn.position.x;
			notification.positionY = event.respawn.position.y;
			notification.directionX = event.respawn.direction.x;
			notification.directionY = event.respawn.direction.y;
			notification.currentHp = event.respawn.currentHp;
			notification.maxHp = event.respawn.maxHp;
			notification.lifeRevision = event.respawn.lifeRevision;
			notification.serverTick = event.respawn.serverTick;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, observerIt->second, notification))
			{
				Log(Foundation::ELogLevel::Warn,
					"respawn notification send failed. observerSessionId={} observerEntityId={} entityId={} lifeRevision={}",
					observerIt->second,
					event.observerEntityId,
					event.respawn.entityId,
					event.respawn.lifeRevision);
			}
		}
	}

	void FMapContentShard::FailTickMoves(
		const WorldCore::FMapInstanceId mapInstanceId,
		const std::span<const WorldCore::SMoveRequestIdentity> moveRequests,
		const EWorldResultCode resultCode,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		WorldCore::FMapInstance* const mapInstance =
			m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(mapInstanceId) : nullptr;
		for (const WorldCore::SMoveRequestIdentity& moveRequest : moveRequests)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(moveRequest.entityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}
			const std::uint64_t sessionId = sessionIt->second;
			const auto localSessionIt = m_localSessions.find(sessionId);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != mapInstanceId ||
				localSessionIt->second.inFlightMoveSequence != moveRequest.sequence)
			{
				continue;
			}
			SLocalSessionState& localState = localSessionIt->second;

			Generated::Map::FMoveRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.sequence = moveRequest.sequence;
			response.moveState = static_cast<std::uint8_t>(WorldCore::EMoveState::Stop);
			response.isCorrected = true;
			const WorldCore::FPlayerEntity* const player = mapInstance != nullptr ? mapInstance->FindPlayer(localState.entityId) : nullptr;
			if (player != nullptr)
			{
				response.moveState = static_cast<std::uint8_t>(player->GetMoveState());
				response.acceptedPositionX = player->GetPosition().x;
				response.acceptedPositionY = player->GetPosition().y;
				response.directionX = player->GetDirection().x;
				response.directionY = player->GetDirection().y;
			}

			localState.inFlightMoveSequence = 0;
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		}
	}

	void FMapContentShard::FailQueuedMoves(
		const WorldCore::FMapInstanceId mapInstanceId,
		const std::span<const WorldCore::SMoveRequestIdentity> moveRequests,
		const EWorldResultCode resultCode,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		WorldCore::FMapInstance* const mapInstance =
			m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(mapInstanceId) : nullptr;
		for (const WorldCore::SMoveRequestIdentity& moveRequest : moveRequests)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(moveRequest.entityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}

			const std::uint64_t sessionId = sessionIt->second;
			const auto localSessionIt = m_localSessions.find(sessionId);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != mapInstanceId ||
				localSessionIt->second.queuedMoveSequence != moveRequest.sequence)
			{
				continue;
			}
			SLocalSessionState& localState = localSessionIt->second;

			Generated::Map::FMoveRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.sequence = moveRequest.sequence;
			response.moveState = static_cast<std::uint8_t>(WorldCore::EMoveState::Stop);
			response.isCorrected = true;
			const WorldCore::FPlayerEntity* const player = mapInstance != nullptr ? mapInstance->FindPlayer(localState.entityId) : nullptr;
			if (player != nullptr)
			{
				response.moveState = static_cast<std::uint8_t>(player->GetMoveState());
				response.acceptedPositionX = player->GetPosition().x;
				response.acceptedPositionY = player->GetPosition().y;
				response.directionX = player->GetDirection().x;
				response.directionY = player->GetDirection().y;
			}

			localState.queuedMoveSequence = 0;
			(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		}
	}

	void FMapContentShard::FailTickAttacks(
		const WorldCore::FMapInstanceId mapInstanceId,
		const std::span<const WorldCore::SPlayerAttackRequestIdentity> attackRequests,
		const EWorldResultCode resultCode,
		const std::uint64_t serverTick,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SPlayerAttackRequestIdentity& attackRequest : attackRequests)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(attackRequest.attackerEntityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}

			const auto localSessionIt = m_localSessions.find(sessionIt->second);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != mapInstanceId ||
				localSessionIt->second.inFlightAttackSequences.erase(attackRequest.attackSequence) == 0)
			{
				continue;
			}

			Generated::Map::FBasicAttackRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.attackSequence = attackRequest.attackSequence;
			response.serverTick = serverTick;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionIt->second, response))
			{
				Log(Foundation::ELogLevel::Warn,
					"BasicAttack failure response send failed. sessionId={} attackerEntityId={} sequence={} resultCode={}",
					sessionIt->second,
					attackRequest.attackerEntityId,
					attackRequest.attackSequence,
					static_cast<std::uint16_t>(resultCode));
			}
		}
	}

	void FMapContentShard::CompleteRejectedAttacks(
		const WorldCore::FMapInstanceId mapInstanceId,
		const std::span<const WorldCore::SRejectedPlayerAttack> rejectedAttacks,
		const std::uint64_t serverTick,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		for (const WorldCore::SRejectedPlayerAttack& rejectedAttack : rejectedAttacks)
		{
			const auto sessionIt = m_sessionIdsByEntityId.find(rejectedAttack.request.attackerEntityId);
			if (sessionIt == m_sessionIdsByEntityId.end())
			{
				continue;
			}

			const auto localSessionIt = m_localSessions.find(sessionIt->second);
			if (localSessionIt == m_localSessions.end() || localSessionIt->second.mapInstanceId != mapInstanceId ||
				localSessionIt->second.inFlightAttackSequences.erase(rejectedAttack.request.attackSequence) == 0)
			{
				Log(Foundation::ELogLevel::Error,
					"Rejected BasicAttack does not match an in-flight request. sessionId={} attackerEntityId={} sequence={} reason={}",
					sessionIt->second,
					rejectedAttack.request.attackerEntityId,
					rejectedAttack.request.attackSequence,
					static_cast<std::uint32_t>(rejectedAttack.reason));
				continue;
			}

			Generated::Map::FBasicAttackRp response;
			response.resultCode = static_cast<std::uint16_t>(MapPlayerAttackRejectReason(rejectedAttack.reason));
			response.attackSequence = rejectedAttack.request.attackSequence;
			response.serverTick = serverTick;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionIt->second, response))
			{
				Log(Foundation::ELogLevel::Warn,
					"BasicAttack rejection response send failed. sessionId={} attackerEntityId={} sequence={} reason={}",
					sessionIt->second,
					rejectedAttack.request.attackerEntityId,
					rejectedAttack.request.attackSequence,
					static_cast<std::uint32_t>(rejectedAttack.reason));
			}
		}
	}

	void FMapContentShard::RemoveDisconnectedSessions(
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		std::vector<std::uint64_t> disconnectedSessionIds;
		disconnectedSessionIds.reserve(m_sessionGenerations.size());
		for (const auto& [sessionId, routeGeneration] : m_sessionGenerations)
		{
			(void)routeGeneration;
			if (!bridge.IsSessionAlive(sessionId))
			{
				disconnectedSessionIds.push_back(sessionId);
			}
		}

		for (const std::uint64_t sessionId : disconnectedSessionIds)
		{
			CancelDeferredMapEnter(sessionId);
			CancelPendingEquipmentMutation(sessionId);
			if (RemoveLocalPlayer(sessionId, bridge))
			{
				m_deferredLeaveGenerations.erase(sessionId);
				m_sessionGenerations.erase(sessionId);
				if (m_sessionRegistry != nullptr)
				{
					m_sessionRegistry->Remove(sessionId);
				}
			}
			else
			{
				const auto generationIt = m_sessionGenerations.find(sessionId);
				if (generationIt != m_sessionGenerations.end())
				{
					m_deferredLeaveGenerations[sessionId] = generationIt->second;
				}
			}
		}
	}

	bool FMapContentShard::RemoveLocalPlayer(
		const std::uint64_t sessionId,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto localSessionIt = m_localSessions.find(sessionId);
		if (localSessionIt == m_localSessions.end())
		{
			return true;
		}

		const SLocalSessionState localState = localSessionIt->second;

		WorldCore::FMapInstance* const mapInstance =
			m_mapInstanceManager != nullptr ? m_mapInstanceManager->FindMap(localState.mapInstanceId) : nullptr;
		if (mapInstance == nullptr)
		{
			Log(Foundation::ELogLevel::Error,
				"local session references a missing map during leave. sessionId={} mapInstanceId={}",
				sessionId,
				localState.mapInstanceId);
			m_localSessions.erase(localSessionIt);
			m_sessionIdsByEntityId.erase(localState.entityId);
			return true;
		}
		if (mapInstance->GetTickExecutionState() != WorldCore::EMapTickExecutionState::Idle)
		{
			return false;
		}

		std::vector<WorldCore::SVisibilityEvent> visibilityEvents;
		std::string removeError;
		if (!mapInstance->RemovePlayer(localState.entityId, visibilityEvents, removeError))
		{
			Log(Foundation::ELogLevel::Error,
				"map player removal failed. sessionId={} entityId={} mapInstanceId={} error={}",
				sessionId,
				localState.entityId,
				localState.mapInstanceId,
				removeError);
			return false;
		}

		m_localSessions.erase(localSessionIt);
		m_sessionIdsByEntityId.erase(localState.entityId);
		DispatchVisibilityEvents(*mapInstance, visibilityEvents, mapInstance->GetTickIndex(), bridge);
		return true;
	}

	std::uint32_t FMapContentShard::GetShardIndex() const noexcept
	{
		return m_shardIndex;
	}

	void FMapContentShard::ProcessCacheRpcResponse(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcResponse& response)
	{
		const RpcLib::Protocol::ERpcCompletionResult result = m_rpcCommon.ProcessResponse(rpcSessionId, response);
		if (result != RpcLib::Protocol::ERpcCompletionResult::Completed)
		{
			Log(Foundation::ELogLevel::Warn,
				"World map Cache RPC response was not completed. shardIndex={} requestId={} result={}",
				m_shardIndex,
				response.requestId,
				static_cast<std::uint32_t>(result));
		}
	}

	void FMapContentShard::FailCacheRpcSession(
		const std::uint64_t rpcSessionId)
	{
		(void)m_rpcCommon.FailSession(rpcSessionId, RpcLib::Protocol::ERpcCallError::Disconnected);
	}

	std::size_t FMapContentShard::GetMapCount() const noexcept
	{
		return m_mapInstanceManager == nullptr ? 0 : m_mapInstanceManager->GetMapCount();
	}

	void FMapContentShard::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "WorldServer", message);
		}
	}
}
