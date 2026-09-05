#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/FMapInputBuffer.h"
#include "WorldCore/Map/FMapInstance.h"
#include "WorldCore/Map/FMapTickExecution.h"
#include "WorldCore/Map/FMapVisibilitySystem.h"
#include "WorldCore/Map/Spawn/FPlayerRespawnSystem.h"
#include "WorldCore/Map/Spawn/FMonsterSpawnSystem.h"
#include "WorldCore/Map/Sector/FSectorDeferredCommandBuffer.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorTaskProcessor.h"
#include "WorldCore/Map/Sector/FSectorTickPlan.h"

namespace WorldCore
{
	namespace
	{
		void HashCombine(
			std::uint64_t& hash,
			const std::uint64_t value) noexcept
		{
			hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
		}
	}

	struct FMapInstance::SImpl final
	{
		FMapInstanceId mapInstanceId = kInvalidMapInstanceId;
		FMapIncarnation mapIncarnation = kInvalidMapIncarnation;
		SMapDefinition definition{};
		FEntityRegistry entityRegistry;
		FSectorGrid sectorGrid;
		FMapInputBuffer inputBuffer;
		FSectorTaskProcessor taskProcessor;
		FMapTickExecution tickExecution;
		std::unique_ptr<ISectorExecutor> sectorExecutor;
		FSectorDeferredCommandBuffer deferredCommands;
		FMapVisibilitySystem visibilitySystem;
		FMonsterSpawnSystem monsterSpawnSystem;
		FPlayerRespawnSystem playerRespawnSystem;
		std::uint64_t lastCommittedTickIndex = 0;
		FMapTickGeneration nextTickGeneration = kInvalidMapTickGeneration;

		SImpl(
			const FMapInstanceId inMapInstanceId,
			const FMapIncarnation inMapIncarnation,
			const SMapDefinition& inDefinition,
			std::unique_ptr<ISectorExecutor> inSectorExecutor)
			: mapInstanceId(inMapInstanceId)
			, mapIncarnation(inMapIncarnation)
			, definition(inDefinition)
			, sectorGrid(inDefinition)
			, sectorExecutor(std::move(inSectorExecutor))
		{
		}
	};

	FMapInstance::FMapInstance(
		const FMapInstanceId mapInstanceId,
		const FMapIncarnation mapIncarnation,
		const SMapDefinition& definition,
		std::unique_ptr<ISectorExecutor> sectorExecutor)
		: m_impl(std::make_unique<SImpl>(mapInstanceId, mapIncarnation, definition, std::move(sectorExecutor)))
	{
	}

	FMapInstance::~FMapInstance() = default;

	FMapInstanceId FMapInstance::GetMapInstanceId() const noexcept
	{
		return m_impl->mapInstanceId;
	}

	const SMapDefinition& FMapInstance::GetDefinition() const noexcept
	{
		return m_impl->definition;
	}

	std::size_t FMapInstance::GetPlayerCount() const noexcept
	{
		return m_impl->entityRegistry.GetPlayerCount();
	}

	std::size_t FMapInstance::GetMonsterCount() const noexcept
	{
		return m_impl->entityRegistry.GetMonsterCount();
	}

	const FPlayerEntity* FMapInstance::FindPlayer(
		const FEntityId entityId) const noexcept
	{
		return m_impl->entityRegistry.FindPlayer(entityId);
	}

	const FMonsterEntity* FMapInstance::FindMonster(
		const FEntityId entityId) const noexcept
	{
		return m_impl->entityRegistry.FindMonster(entityId);
	}

	const FSectorGrid& FMapInstance::GetSectorGrid() const noexcept
	{
		return m_impl->sectorGrid;
	}

	std::size_t FMapInstance::GetPendingMoveCount() const noexcept
	{
		return m_impl->inputBuffer.GetPendingMoveCount();
	}

	std::size_t FMapInstance::GetPendingAttackCount() const noexcept
	{
		return m_impl->inputBuffer.GetPendingAttackCount();
	}

	std::uint64_t FMapInstance::GetTickIndex() const noexcept
	{
		return m_impl->lastCommittedTickIndex;
	}

	EMapTickExecutionState FMapInstance::GetTickExecutionState() const noexcept
	{
		return m_impl->tickExecution.GetState();
	}

	SMapTickTicket FMapInstance::GetActiveTickTicket() const noexcept
	{
		return m_impl->tickExecution.GetTicket();
	}

	std::uint64_t FMapInstance::GetStateHash() const
	{
		std::uint64_t hash = 0xCBF29CE484222325ull;
		HashCombine(hash, m_impl->mapInstanceId);
		HashCombine(hash, m_impl->lastCommittedTickIndex);
		for (const FEntityId entityId : m_impl->entityRegistry.GetPlayerEntityIds())
		{
			const FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(entityId);
			if (player == nullptr)
			{
				continue;
			}
			HashCombine(hash, entityId);
			HashCombine(hash, player->GetUserId());
			HashCombine(hash, player->GetSectorId());
			HashCombine(hash, player->GetLastMoveSequence());
			HashCombine(hash, static_cast<std::uint8_t>(player->GetMoveState()));
			HashCombine(hash, static_cast<std::uint8_t>(player->GetLifeState()));
			HashCombine(hash, player->GetLifeRevision());
			HashCombine(hash, player->GetRespawnDueTick());
			HashCombine(hash, player->GetLastKillerEntityId());
			HashCombine(hash, player->GetNextBasicAttackTick());
			HashCombine(hash, std::bit_cast<std::uint32_t>(player->GetPosition().x));
			HashCombine(hash, std::bit_cast<std::uint32_t>(player->GetPosition().y));
			HashCombine(hash, std::bit_cast<std::uint32_t>(player->GetDirection().x));
			HashCombine(hash, std::bit_cast<std::uint32_t>(player->GetDirection().y));
			if (player->HasRuntimeSnapshot())
			{
				const SPlayerRuntimeSnapshot& snapshot = player->GetRuntimeSnapshot();
				HashCombine(hash, snapshot.characterId);
				HashCombine(hash, snapshot.characterDataId);
				HashCombine(hash, snapshot.level);
				HashCombine(hash, snapshot.exp);
				HashCombine(hash, snapshot.requiredExpToNextLevel);
				HashCombine(hash, snapshot.str);
				HashCombine(hash, snapshot.dex);
				HashCombine(hash, snapshot.intelligence);
				HashCombine(hash, snapshot.luk);
				HashCombine(hash, snapshot.unspentStatPoints);
				HashCombine(hash, snapshot.progressVersion);
				HashCombine(hash, snapshot.statVersion);
				HashCombine(hash, snapshot.finalStr);
				HashCombine(hash, snapshot.finalDex);
				HashCombine(hash, snapshot.finalIntelligence);
				HashCombine(hash, snapshot.finalLuk);
				HashCombine(hash, snapshot.maxHp);
				HashCombine(hash, snapshot.maxMp);
				HashCombine(hash, snapshot.attack);
				HashCombine(hash, snapshot.defense);
				HashCombine(hash, snapshot.moveSpeedMilli);
				HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.collisionRadius));
				HashCombine(hash, snapshot.equipmentVersion);
				HashCombine(hash, snapshot.statRevision);
				HashCombine(hash, player->GetCurrentHp());
				HashCombine(hash, player->GetCurrentMp());
			}
		}
		for (const FEntityId entityId : m_impl->entityRegistry.GetMonsterEntityIds())
		{
			const FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(entityId);
			if (monster == nullptr)
			{
				continue;
			}
			const SMonsterRuntimeSnapshot& snapshot = monster->GetRuntimeSnapshot();
			HashCombine(hash, entityId);
			HashCombine(hash, monster->GetSpawnerDataId());
			HashCombine(hash, monster->GetSpawnGeneration());
			HashCombine(hash, monster->GetSectorId());
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetSpawnPosition().x));
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetSpawnPosition().y));
			HashCombine(hash, monster->GetTargetEntityId());
			HashCombine(hash, static_cast<std::uint8_t>(monster->GetAiState()));
			HashCombine(hash, static_cast<std::uint8_t>(monster->GetMoveState()));
			HashCombine(hash, monster->GetNextAttackTick());
			HashCombine(hash, snapshot.monsterDataId);
			HashCombine(hash, static_cast<std::uint8_t>(snapshot.monsterType));
			HashCombine(hash, static_cast<std::uint8_t>(snapshot.aggroType));
			HashCombine(hash, snapshot.maxHp);
			HashCombine(hash, snapshot.attack);
			HashCombine(hash, snapshot.defense);
			HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.moveSpeed));
			HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.collisionRadius));
			HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.aggroRadius));
			HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.leashRadius));
			HashCombine(hash, std::bit_cast<std::uint32_t>(snapshot.attackRange));
			HashCombine(hash, snapshot.attackCooldownMilliseconds);
			HashCombine(hash, snapshot.attackCooldownTicks);
			HashCombine(hash, monster->GetCurrentHp());
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetPosition().x));
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetPosition().y));
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetDirection().x));
			HashCombine(hash, std::bit_cast<std::uint32_t>(monster->GetDirection().y));
		}
		if (m_impl->monsterSpawnSystem.IsConfigured())
		{
			HashCombine(hash, m_impl->monsterSpawnSystem.GetStateHash());
		}
		return hash;
	}

	bool FMapInstance::ConfigureMonsterSpawning(
		const std::uint64_t fixedSeed,
		const std::span<const SMonsterSpawnerRuntimeDefinition> definitions,
		std::string& outError)
	{
		outError.clear();
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle || m_impl->lastCommittedTickIndex != 0 ||
			m_impl->nextTickGeneration != kInvalidMapTickGeneration || m_impl->entityRegistry.GetMonsterCount() != 0)
		{
			outError = "Monster Spawn System must be configured once before the first Map Tick.";
			return false;
		}
		return m_impl->monsterSpawnSystem.Configure(m_impl->mapInstanceId, m_impl->definition, fixedSeed, definitions, outError);
	}

	bool FMapInstance::AddPlayer(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		outVisibilityEvents.clear();
		outError.clear();
		if ((entityId & kMapLocalEntityIdBit) != 0)
		{
			outError = "Player EntityId overlaps the Map-local Monster ID range.";
			return false;
		}
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			outError = "Player registration is deferred while a Map Tick is active.";
			return false;
		}
		FSectorId sectorId = kInvalidSectorId;
		if (!m_impl->sectorGrid.TryResolveSector(position, sectorId))
		{
			outError = "Player position is outside the Map boundary.";
			return false;
		}
		if (!m_impl->entityRegistry.AddPlayer(entityId, userId, position, direction, sectorId))
		{
			outError = "Player Entity registration failed.";
			return false;
		}
		if (!m_impl->sectorGrid.AddEntity(sectorId, entityId))
		{
			(void)m_impl->entityRegistry.RemovePlayer(entityId);
			outError = "Player Sector registration failed.";
			return false;
		}

		outVisibilityEvents =
			m_impl->visibilitySystem.Refresh(m_impl->entityRegistry, m_impl->sectorGrid, m_impl->definition.visibilitySectorRadius, {});
		return true;
	}

	bool FMapInstance::AddPlayerAtRandomSpawn(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& direction,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		SVector2 position{};
		FSectorId sectorId = kInvalidSectorId;
		if (!m_impl->playerRespawnSystem.TrySelectSpawnPosition(m_impl->mapInstanceId,
				entityId,
				1,
				0.0f,
				m_impl->definition,
				m_impl->entityRegistry,
				m_impl->sectorGrid,
				position,
				sectorId,
				outError))
		{
			outVisibilityEvents.clear();
			return false;
		}
		return AddPlayer(entityId, userId, position, direction, outVisibilityEvents, outError);
	}

	bool FMapInstance::AddPlayerAtRandomSpawn(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& direction,
		const SPlayerRuntimeSnapshot& runtimeSnapshot,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		outVisibilityEvents.clear();
		if (!IsValidPlayerRuntimeSnapshot(runtimeSnapshot, outError))
		{
			return false;
		}
		SVector2 position{};
		FSectorId sectorId = kInvalidSectorId;
		if (!m_impl->playerRespawnSystem.TrySelectSpawnPosition(m_impl->mapInstanceId,
				entityId,
				1,
				runtimeSnapshot.collisionRadius,
				m_impl->definition,
				m_impl->entityRegistry,
				m_impl->sectorGrid,
				position,
				sectorId,
				outError))
		{
			return false;
		}
		return AddPlayer(entityId, userId, position, direction, runtimeSnapshot, outVisibilityEvents, outError);
	}

	bool FMapInstance::AddPlayer(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		const SPlayerRuntimeSnapshot& runtimeSnapshot,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		outVisibilityEvents.clear();
		outError.clear();
		if ((entityId & kMapLocalEntityIdBit) != 0)
		{
			outError = "Player EntityId overlaps the Map-local Monster ID range.";
			return false;
		}
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			outError = "Player registration is deferred while a Map Tick is active.";
			return false;
		}
		if (!IsValidPlayerRuntimeSnapshot(runtimeSnapshot, outError))
		{
			return false;
		}
		FSectorId sectorId = kInvalidSectorId;
		if (!m_impl->sectorGrid.TryResolveSector(position, sectorId))
		{
			outError = "Player position is outside the Map boundary.";
			return false;
		}
		if (!m_impl->entityRegistry.AddPlayer(entityId, userId, position, direction, sectorId, runtimeSnapshot))
		{
			outError = "Player Entity registration failed.";
			return false;
		}
		if (!m_impl->sectorGrid.AddEntity(sectorId, entityId))
		{
			(void)m_impl->entityRegistry.RemovePlayer(entityId);
			outError = "Player Sector registration failed.";
			return false;
		}

		outVisibilityEvents =
			m_impl->visibilitySystem.Refresh(m_impl->entityRegistry, m_impl->sectorGrid, m_impl->definition.visibilitySectorRadius, {});
		return true;
	}

	bool FMapInstance::RemovePlayer(
		const FEntityId entityId,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		outVisibilityEvents.clear();
		outError.clear();
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			outError = "Player removal is deferred while a Map Tick is active.";
			return false;
		}
		if (m_impl->entityRegistry.FindPlayer(entityId) == nullptr)
		{
			outError = "Player Entity does not exist.";
			return false;
		}
		if (!m_impl->sectorGrid.RemoveEntity(entityId) || !m_impl->entityRegistry.RemovePlayer(entityId))
		{
			outError = "Player removal violated the Entity/Sector ownership invariant.";
			return false;
		}
		m_impl->inputBuffer.RemoveEntity(entityId);
		outVisibilityEvents =
			m_impl->visibilitySystem.Refresh(m_impl->entityRegistry, m_impl->sectorGrid, m_impl->definition.visibilitySectorRadius, {});
		return true;
	}

	bool FMapInstance::RemoveMonster(
		const FEntityId entityId,
		std::vector<SVisibilityEvent>& outVisibilityEvents,
		std::string& outError)
	{
		outVisibilityEvents.clear();
		outError.clear();
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			outError = "Monster removal is deferred while a Map Tick is active.";
			return false;
		}

		const FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(entityId);
		if (monster == nullptr)
		{
			outError = "Monster Entity does not exist.";
			return false;
		}
		const FSpawnerDataId spawnerDataId = monster->GetSpawnerDataId();
		const FSpawnGeneration spawnGeneration = monster->GetSpawnGeneration();
		const FSectorId sectorId = monster->GetSectorId();
		const SVector2 position = monster->GetPosition();
		const SVector2 spawnPosition = monster->GetSpawnPosition();
		const SVector2 direction = monster->GetDirection();
		const SMonsterRuntimeSnapshot snapshot = monster->GetRuntimeSnapshot();
		const std::uint32_t currentHp = monster->GetCurrentHp();
		const FMonsterEntity::SCommittedAiState committedAiState = monster->CaptureCommittedAiState();
		if (!m_impl->monsterSpawnSystem.CanScheduleRespawn(
				entityId, spawnerDataId, spawnGeneration, m_impl->lastCommittedTickIndex, outError))
		{
			return false;
		}

		if (!m_impl->sectorGrid.RemoveEntity(entityId))
		{
			outError = "Monster Sector removal violated the Entity/Sector ownership invariant.";
			return false;
		}
		if (!m_impl->entityRegistry.RemoveMonster(entityId))
		{
			(void)m_impl->sectorGrid.AddEntity(sectorId, entityId);
			outError = "Monster Registry removal violated the Entity/Sector ownership invariant.";
			return false;
		}
		if (!m_impl->monsterSpawnSystem.ScheduleRespawn(entityId, spawnerDataId, spawnGeneration, m_impl->lastCommittedTickIndex, outError))
		{
			const bool restoredEntity = m_impl->entityRegistry.AddMonster(
				entityId, spawnerDataId, spawnGeneration, spawnPosition, position, direction, sectorId, snapshot, currentHp);
			FMonsterEntity* const restoredMonster = restoredEntity ? m_impl->entityRegistry.FindMonster(entityId) : nullptr;
			const bool restoredAi = restoredMonster != nullptr && restoredMonster->RestoreCommittedAiState(committedAiState);
			const bool restoredSector = restoredAi && m_impl->sectorGrid.AddEntity(sectorId, entityId);
			if (!restoredSector)
			{
				outError += " Monster rollback failed.";
			}
			return false;
		}

		outVisibilityEvents =
			m_impl->visibilitySystem.Refresh(m_impl->entityRegistry, m_impl->sectorGrid, m_impl->definition.visibilitySectorRadius, {});
		return true;
	}

	bool FMapInstance::ApplyPlayerRuntimeSnapshot(
		const FEntityId entityId,
		const SPlayerRuntimeSnapshot& runtimeSnapshot,
		std::string& outError)
	{
		outError.clear();
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			outError = "Player Runtime Snapshot can only be applied while the Map Tick is idle.";
			return false;
		}

		FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(entityId);
		if (player == nullptr)
		{
			outError = "Player Runtime Snapshot target entity was not found.";
			return false;
		}
		if (!player->ApplyRuntimeSnapshot(runtimeSnapshot))
		{
			outError = "Player Runtime Snapshot was invalid or stale.";
			return false;
		}

		return true;
	}

	bool FMapInstance::QueueMove(
		const SMoveCommand& command)
	{
		const FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(command.entityId);
		return player != nullptr && player->IsAlive() && (!player->HasRuntimeSnapshot() || player->GetCurrentHp() > 0) &&
			   m_impl->inputBuffer.EnqueueMove(command);
	}

	bool FMapInstance::QueuePlayerAttack(
		const SPlayerAttackCommand& command)
	{
		return m_impl->entityRegistry.FindPlayer(command.attackerEntityId) != nullptr && m_impl->inputBuffer.EnqueuePlayerAttack(command);
	}

	EMapTickCompletionResult FMapInstance::CompleteTickExecution(
		SMapTickExecutionCompletion completion)
	{
		return m_impl->tickExecution.Complete(std::move(completion));
	}

	SMapTickResult FMapInstance::StartTickExecution()
	{
		SMapTickResult result{};
		result.mapInstanceId = m_impl->mapInstanceId;
		if (m_impl->tickExecution.GetState() != EMapTickExecutionState::Idle)
		{
			result.result = EMapTickResult::Pending;
			const SMapTickTicket& activeTicket = m_impl->tickExecution.GetTicket();
			result.tickIndex = activeTicket.tickIndex;
			result.tickGeneration = activeTicket.generation;
			return result;
		}
		if (m_impl->lastCommittedTickIndex == std::numeric_limits<std::uint64_t>::max() ||
			m_impl->nextTickGeneration == std::numeric_limits<FMapTickGeneration>::max())
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Map Tick identity space was exhausted.";
			return result;
		}

		const SMapTickTicket ticket{
			m_impl->mapInstanceId, m_impl->mapIncarnation, m_impl->lastCommittedTickIndex + 1, ++m_impl->nextTickGeneration};
		result.tickIndex = ticket.tickIndex;
		result.tickGeneration = ticket.generation;
		result.executionStarted = true;

		std::vector<SMoveCommand> commands = m_impl->inputBuffer.BeginTick();
		std::vector<SPlayerAttackCommand> attackCommands = m_impl->inputBuffer.BeginAttackTick();
		std::map<FSectorId, std::vector<SMoveCommand>> commandsBySector;
		std::map<FSectorId, std::vector<SPlayerAttackCommand>> attacksBySector;
		std::vector<SMoveRequestIdentity> consumedMoveRequests;
		consumedMoveRequests.reserve(commands.size());
		for (SMoveCommand& command : commands)
		{
			const FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(command.entityId);
			if (player == nullptr)
			{
				continue;
			}
			consumedMoveRequests.push_back({command.entityId, command.sequence});
			commandsBySector[player->GetSectorId()].push_back(std::move(command));
		}
		std::vector<SPlayerAttackRequestIdentity> consumedAttackRequests;
		consumedAttackRequests.reserve(attackCommands.size());
		for (SPlayerAttackCommand& command : attackCommands)
		{
			const FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(command.attackerEntityId);
			if (player == nullptr)
			{
				continue;
			}
			consumedAttackRequests.push_back({command.attackerEntityId, command.attackSequence});
			attacksBySector[player->GetSectorId()].push_back(std::move(command));
		}

		std::vector<SSectorTask> tasks;
		tasks.reserve(m_impl->sectorGrid.GetSectorCount());
		for (FSectorId sectorId = 0; sectorId < m_impl->sectorGrid.GetSectorCount(); ++sectorId)
		{
			SSectorTask task{};
			task.sectorId = sectorId;
			task.stableOrder = sectorId;
			task.tickIndex = ticket.tickIndex;
			const auto foundCommands = commandsBySector.find(sectorId);
			if (foundCommands != commandsBySector.end())
			{
				task.moveCommands = std::move(foundCommands->second);
			}
			const auto foundAttacks = attacksBySector.find(sectorId);
			if (foundAttacks != attacksBySector.end())
			{
				task.playerAttackCommands = std::move(foundAttacks->second);
			}
			tasks.push_back(std::move(task));
		}

		if (!m_impl->tickExecution.Begin(
				ticket, m_impl->sectorGrid, std::move(tasks), std::move(consumedMoveRequests), std::move(consumedAttackRequests)))
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Map Tick could not start because another execution is active.";
			return result;
		}
		result.consumedMoveRequests.assign(
			m_impl->tickExecution.GetConsumedMoveRequests().begin(), m_impl->tickExecution.GetConsumedMoveRequests().end());
		result.consumedAttackRequests.assign(
			m_impl->tickExecution.GetConsumedAttackRequests().begin(), m_impl->tickExecution.GetConsumedAttackRequests().end());
		if (m_impl->tickExecution.GetState() == EMapTickExecutionState::Failed)
		{
			SMapTickResult failedResult = FinalizeTickExecution();
			failedResult.executionStarted = true;
			return failedResult;
		}

		if (m_impl->sectorExecutor == nullptr)
		{
			SMapTickExecutionCompletion completion{};
			completion.ticket = ticket;
			completion.status = EMapTickCompletionStatus::Failed;
			completion.failureReason = "Map Tick has no Sector executor.";
			(void)m_impl->tickExecution.Complete(std::move(completion));
			SMapTickResult failedResult = FinalizeTickExecution();
			failedResult.executionStarted = true;
			return failedResult;
		}

		SSectorExecutionStartResult startResult = m_impl->sectorExecutor->Execute(ticket,
			m_impl->tickExecution.GetTickPlan(),
			m_impl->taskProcessor,
			m_impl->entityRegistry,
			m_impl->sectorGrid,
			m_impl->definition);
		if (startResult.executionResult == ESectorExecutionResult::Pending && startResult.taskOutputs.empty() &&
			startResult.failureReason.empty())
		{
			result.result = EMapTickResult::Pending;
			return result;
		}

		SMapTickExecutionCompletion completion{};
		completion.ticket = ticket;
		if (startResult.executionResult == ESectorExecutionResult::CompletedInline)
		{
			completion.status = EMapTickCompletionStatus::Succeeded;
			completion.taskOutputs = std::move(startResult.taskOutputs);
		}
		else
		{
			completion.status = EMapTickCompletionStatus::Failed;
			completion.failureReason = startResult.executionResult == ESectorExecutionResult::Pending
										   ? "A Pending Sector execution returned inline outputs or an inline error."
										   : (startResult.failureReason.empty() ? "Sector execution failed without a reason."
																				: std::move(startResult.failureReason));
		}
		(void)m_impl->tickExecution.Complete(std::move(completion));
		SMapTickResult completedResult = FinalizeTickExecution();
		completedResult.executionStarted = true;
		return completedResult;
	}

	SMapTickResult FMapInstance::FinalizeTickExecution()
	{
		SMapTickResult result{};
		const EMapTickExecutionState state = m_impl->tickExecution.GetState();
		const SMapTickTicket ticket = m_impl->tickExecution.GetTicket();
		result.mapInstanceId = m_impl->mapInstanceId;
		result.tickIndex = ticket.tickIndex;
		result.tickGeneration = ticket.generation;
		result.consumedMoveRequests.assign(
			m_impl->tickExecution.GetConsumedMoveRequests().begin(), m_impl->tickExecution.GetConsumedMoveRequests().end());
		result.consumedAttackRequests.assign(
			m_impl->tickExecution.GetConsumedAttackRequests().begin(), m_impl->tickExecution.GetConsumedAttackRequests().end());
		if (state == EMapTickExecutionState::Executing)
		{
			result.result = EMapTickResult::Pending;
			return result;
		}
		if (state == EMapTickExecutionState::Failed)
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = m_impl->tickExecution.GetFailureReason();
			m_impl->tickExecution.Reset();
			return result;
		}
		if (state != EMapTickExecutionState::ReadyToCommit)
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Map Tick finalize was requested without a completed execution.";
			return result;
		}

		const std::span<const SSectorTaskOutput> taskOutputs = m_impl->tickExecution.GetTaskOutputs();
		std::unordered_map<FEntityId, FMoveSequence> expectedMoveRequests;
		for (const SMoveRequestIdentity& moveRequest : m_impl->tickExecution.GetConsumedMoveRequests())
		{
			expectedMoveRequests.emplace(moveRequest.entityId, moveRequest.sequence);
		}
		std::map<std::pair<FEntityId, FAttackSequence>, FEntityId> expectedPlayerAttackRequests;
		for (const SSectorTaskWave& wave : m_impl->tickExecution.GetTickPlan().GetWaves())
		{
			for (const SSectorTask& task : wave.tasks)
			{
				for (const SPlayerAttackCommand& command : task.playerAttackCommands)
				{
					expectedPlayerAttackRequests.emplace(
						std::pair{command.attackerEntityId, command.attackSequence}, command.targetEntityId);
				}
			}
		}
		std::set<std::pair<FEntityId, FAttackSequence>> resolvedPlayerAttackRequests;
		std::unordered_set<FEntityId> movedEntitySet;
		std::unordered_set<FEntityId> monsterAiEntitySet;
		std::unordered_set<FEntityId> expectedAttackers;
		std::unordered_set<FEntityId> actualAttackers;
		std::unordered_map<FEntityId, const SMonsterAiResult*> monsterAiByEntityId;
		std::vector<SMonsterAttackIntent> attackIntents;
		std::vector<SPlayerAttackIntent> playerAttackIntents;
		for (const SSectorTaskOutput& output : taskOutputs)
		{
			for (const SMoveResult& moveResult : output.moveResults)
			{
				const FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(moveResult.entityId);
				FSectorId resolvedSectorId = kInvalidSectorId;
				const auto expectedMove = expectedMoveRequests.find(moveResult.entityId);
				if (expectedMove == expectedMoveRequests.end() || expectedMove->second != moveResult.sequence || player == nullptr ||
					player->GetSectorId() != moveResult.previousSectorId || moveResult.sequence <= player->GetLastMoveSequence() ||
					!m_impl->sectorGrid.TryResolveSector(moveResult.acceptedPosition, resolvedSectorId) ||
					resolvedSectorId != moveResult.currentSectorId || !movedEntitySet.insert(moveResult.entityId).second)
				{
					result.result = EMapTickResult::Failed;
					result.failureReason = "Sector output failed pre-commit validation.";
					m_impl->tickExecution.Reset();
					return result;
				}
				result.moveResults.push_back(moveResult);
			}
			for (const SMonsterAiResult& monsterAiResult : output.monsterAiResults)
			{
				const FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(monsterAiResult.entityId);
				const FPlayerEntity* const target = m_impl->entityRegistry.FindPlayer(monsterAiResult.targetEntityId);
				FSectorId resolvedSectorId = kInvalidSectorId;
				const bool isIdle = monsterAiResult.aiState == EMonsterAiState::Idle;
				const bool isChasing = monsterAiResult.aiState == EMonsterAiState::Chase;
				const bool isAttackReady = monsterAiResult.aiState == EMonsterAiState::AttackReady;
				const bool isReturning = monsterAiResult.aiState == EMonsterAiState::Return;
				const bool attackPositionValid =
					!isAttackReady || (monster != nullptr && target != nullptr &&
										  GetDistanceSquared(monster->GetPosition(), target->GetPosition()) <=
											  monster->GetRuntimeSnapshot().attackRange * monster->GetRuntimeSnapshot().attackRange &&
										  GetDistanceSquared(monster->GetSpawnPosition(), target->GetPosition()) <=
											  monster->GetRuntimeSnapshot().leashRadius * monster->GetRuntimeSnapshot().leashRadius);
				if (monster == nullptr || output.sectorId != monsterAiResult.previousSectorId ||
					monster->GetSectorId() != monsterAiResult.previousSectorId ||
					!m_impl->sectorGrid.ContainsEntity(monsterAiResult.previousSectorId, monsterAiResult.entityId) ||
					(!isIdle && !isChasing && !isAttackReady && !isReturning) ||
					(isIdle && (monsterAiResult.targetEntityId != kInvalidEntityId || monsterAiResult.moveState != EMoveState::Stop)) ||
					((isChasing || isAttackReady) && target == nullptr) ||
					(isReturning &&
						(monsterAiResult.targetEntityId != kInvalidEntityId || monsterAiResult.moveState != EMoveState::Start)) ||
					(isChasing && monsterAiResult.moveState != EMoveState::Start) ||
					(isAttackReady && (monsterAiResult.moveState != EMoveState::Stop || !attackPositionValid)) ||
					!m_impl->sectorGrid.TryResolveSector(monsterAiResult.acceptedPosition, resolvedSectorId) ||
					resolvedSectorId != monsterAiResult.currentSectorId || !monsterAiEntitySet.insert(monsterAiResult.entityId).second)
				{
					result.result = EMapTickResult::Failed;
					result.failureReason = "Monster AI output failed pre-commit validation.";
					m_impl->tickExecution.Reset();
					return result;
				}
				result.monsterAiResults.push_back(monsterAiResult);
				monsterAiByEntityId.emplace(monsterAiResult.entityId, &monsterAiResult);
				if (isAttackReady && target != nullptr && target->IsAlive() && target->HasRuntimeSnapshot() && target->GetCurrentHp() > 0 &&
					ticket.tickIndex >= monster->GetNextAttackTick())
				{
					expectedAttackers.insert(monsterAiResult.entityId);
				}
			}
			for (const SMonsterAttackIntent& attackIntent : output.monsterAttackIntents)
			{
				const FMonsterEntity* const attacker = m_impl->entityRegistry.FindMonster(attackIntent.attackerEntityId);
				const FPlayerEntity* const target = m_impl->entityRegistry.FindPlayer(attackIntent.targetEntityId);
				const auto aiFound = monsterAiByEntityId.find(attackIntent.attackerEntityId);
				if (attacker == nullptr || target == nullptr || !target->IsAlive() || !target->HasRuntimeSnapshot() ||
					target->GetCurrentHp() == 0 || attackIntent.attackerSpawnGeneration != attacker->GetSpawnGeneration() ||
					attackIntent.attackerSectorId != output.sectorId || attackIntent.attackerSectorId != attacker->GetSectorId() ||
					attackIntent.tickIndex != ticket.tickIndex || attackIntent.expectedNextAttackTick != attacker->GetNextAttackTick() ||
					ticket.tickIndex < attacker->GetNextAttackTick() ||
					ticket.tickIndex > std::numeric_limits<std::uint64_t>::max() - attacker->GetRuntimeSnapshot().attackCooldownTicks ||
					aiFound == monsterAiByEntityId.end() || aiFound->second->aiState != EMonsterAiState::AttackReady ||
					aiFound->second->targetEntityId != attackIntent.targetEntityId ||
					GetDistanceSquared(attacker->GetPosition(), target->GetPosition()) >
						attacker->GetRuntimeSnapshot().attackRange * attacker->GetRuntimeSnapshot().attackRange ||
					GetDistanceSquared(attacker->GetSpawnPosition(), target->GetPosition()) >
						attacker->GetRuntimeSnapshot().leashRadius * attacker->GetRuntimeSnapshot().leashRadius ||
					!actualAttackers.insert(attackIntent.attackerEntityId).second)
				{
					result.result = EMapTickResult::Failed;
					result.failureReason = "Monster attack Intent failed pre-commit validation.";
					m_impl->tickExecution.Reset();
					return result;
				}
				attackIntents.push_back(attackIntent);
			}
			for (const SRejectedPlayerAttack& rejectedAttack : output.rejectedPlayerAttacks)
			{
				const std::pair key{rejectedAttack.request.attackerEntityId, rejectedAttack.request.attackSequence};
				const auto expected = expectedPlayerAttackRequests.find(key);
				if (expected == expectedPlayerAttackRequests.end() || expected->second != rejectedAttack.targetEntityId ||
					!resolvedPlayerAttackRequests.insert(key).second)
				{
					result.result = EMapTickResult::Failed;
					result.failureReason = "Player attack rejection failed pre-commit validation.";
					m_impl->tickExecution.Reset();
					return result;
				}
				result.rejectedAttackRequests.push_back(rejectedAttack);
			}
			for (const SPlayerAttackIntent& attackIntent : output.playerAttackIntents)
			{
				const std::pair key{attackIntent.attackerEntityId, attackIntent.attackSequence};
				const auto expected = expectedPlayerAttackRequests.find(key);
				const FPlayerEntity* const attacker = m_impl->entityRegistry.FindPlayer(attackIntent.attackerEntityId);
				const FMonsterEntity* const target = m_impl->entityRegistry.FindMonster(attackIntent.targetEntityId);
				const float attackRange = m_impl->definition.combatPolicy.playerBasicAttackRange;
				if (expected == expectedPlayerAttackRequests.end() || expected->second != attackIntent.targetEntityId ||
					attacker == nullptr || target == nullptr || !attacker->HasRuntimeSnapshot() || !attacker->IsAlive() ||
					attacker->GetCurrentHp() == 0 || target->GetCurrentHp() == 0 || attackIntent.attackerSectorId != output.sectorId ||
					attackIntent.attackerSectorId != attacker->GetSectorId() || attackIntent.tickIndex != ticket.tickIndex ||
					attackIntent.expectedNextAttackTick != attacker->GetNextBasicAttackTick() ||
					ticket.tickIndex < attacker->GetNextBasicAttackTick() ||
					GetDistanceSquared(attacker->GetPosition(), target->GetPosition()) > attackRange * attackRange ||
					!resolvedPlayerAttackRequests.insert(key).second)
				{
					result.result = EMapTickResult::Failed;
					result.failureReason = "Player attack Intent failed pre-commit validation.";
					m_impl->tickExecution.Reset();
					return result;
				}
				playerAttackIntents.push_back(attackIntent);
			}
		}
		if (result.moveResults.size() != expectedMoveRequests.size())
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Sector output omitted a consumed Move request.";
			m_impl->tickExecution.Reset();
			return result;
		}
		if (result.monsterAiResults.size() != m_impl->entityRegistry.GetMonsterCount())
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Sector output omitted a Monster AI result.";
			m_impl->tickExecution.Reset();
			return result;
		}
		if (actualAttackers != expectedAttackers)
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Sector output omitted or added a Monster attack Intent.";
			m_impl->tickExecution.Reset();
			return result;
		}
		if (resolvedPlayerAttackRequests.size() != expectedPlayerAttackRequests.size())
		{
			result.result = EMapTickResult::Failed;
			result.failureReason = "Sector output omitted a consumed Player attack request.";
			m_impl->tickExecution.Reset();
			return result;
		}
		std::sort(result.monsterAiResults.begin(),
			result.monsterAiResults.end(),
			[](const SMonsterAiResult& lhs, const SMonsterAiResult& rhs)
			{
				return lhs.entityId < rhs.entityId;
			});
		std::sort(attackIntents.begin(),
			attackIntents.end(),
			[](const SMonsterAttackIntent& lhs, const SMonsterAttackIntent& rhs)
			{
				return lhs.attackerEntityId < rhs.attackerEntityId;
			});
		std::sort(playerAttackIntents.begin(),
			playerAttackIntents.end(),
			[](const SPlayerAttackIntent& lhs, const SPlayerAttackIntent& rhs)
			{
				return lhs.targetEntityId < rhs.targetEntityId ||
					   (lhs.targetEntityId == rhs.targetEntityId &&
						   (lhs.attackerEntityId < rhs.attackerEntityId ||
							   (lhs.attackerEntityId == rhs.attackerEntityId && lhs.attackSequence < rhs.attackSequence)));
			});
		std::sort(result.rejectedAttackRequests.begin(),
			result.rejectedAttackRequests.end(),
			[](const SRejectedPlayerAttack& lhs, const SRejectedPlayerAttack& rhs)
			{
				return lhs.request.attackerEntityId < rhs.request.attackerEntityId ||
					   (lhs.request.attackerEntityId == rhs.request.attackerEntityId &&
						   lhs.request.attackSequence < rhs.request.attackSequence);
			});

		m_impl->deferredCommands.Build(taskOutputs);
		struct SPlayerMoveRollbackEntry final
		{
			FPlayerEntity* player = nullptr;
			FPlayerEntity::SCommittedMoveState state{};
		};
		std::vector<SPlayerMoveRollbackEntry> playerMoveRollbackEntries;
		playerMoveRollbackEntries.reserve(result.moveResults.size());
		for (const SMoveResult& moveResult : result.moveResults)
		{
			FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(moveResult.entityId);
			if (player == nullptr)
			{
				result.result = EMapTickResult::Failed;
				result.failureReason = "Validated movement lost its Player before Commit.";
				m_impl->tickExecution.Reset();
				return result;
			}
			playerMoveRollbackEntries.push_back({player, player->CaptureCommittedMoveState()});
		}
		struct SMonsterAiRollbackEntry final
		{
			FEntityId entityId = kInvalidEntityId;
			FMonsterEntity::SCommittedAiState state{};
		};
		std::vector<SMonsterAiRollbackEntry> monsterAiRollbackEntries;
		monsterAiRollbackEntries.reserve(result.monsterAiResults.size());
		for (const SMonsterAiResult& monsterAiResult : result.monsterAiResults)
		{
			FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(monsterAiResult.entityId);
			if (monster == nullptr)
			{
				result.result = EMapTickResult::Failed;
				result.failureReason = "Validated AI output lost its Monster before Commit.";
				m_impl->tickExecution.Reset();
				return result;
			}
			monsterAiRollbackEntries.push_back({monsterAiResult.entityId, monster->CaptureCommittedAiState()});
		}
		struct SMonsterCombatRollbackEntry final
		{
			FEntityId entityId = kInvalidEntityId;
			FMonsterEntity::SCommittedCombatState state{};
		};
		std::vector<SMonsterCombatRollbackEntry> monsterCombatRollbackEntries;
		for (const FEntityId monsterEntityId : m_impl->entityRegistry.GetMonsterEntityIds())
		{
			const FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(monsterEntityId);
			if (monster == nullptr)
			{
				result.result = EMapTickResult::Failed;
				result.failureReason = "Monster registry changed before Combat Commit.";
				m_impl->tickExecution.Reset();
				return result;
			}
			monsterCombatRollbackEntries.push_back({monsterEntityId, monster->CaptureCommittedCombatState()});
		}
		struct SPlayerLifecycleRollbackEntry final
		{
			FPlayerEntity* player = nullptr;
			FPlayerEntity::SCommittedLifecycleState state{};
		};
		std::vector<SPlayerLifecycleRollbackEntry> playerLifecycleRollbackEntries;
		const std::vector<FEntityId> playerEntityIds = m_impl->entityRegistry.GetPlayerEntityIds();
		playerLifecycleRollbackEntries.reserve(playerEntityIds.size());
		for (const FEntityId playerEntityId : playerEntityIds)
		{
			FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(playerEntityId);
			if (player == nullptr)
			{
				result.result = EMapTickResult::Failed;
				result.failureReason = "Player registry changed before Lifecycle Commit.";
				m_impl->tickExecution.Reset();
				return result;
			}
			playerLifecycleRollbackEntries.push_back({player, player->CaptureCommittedLifecycleState()});
		}
		auto rollbackMovementCommit = [&]() noexcept
		{
			bool rollbackSucceeded = true;
			for (auto iterator = result.actorRespawnResults.rbegin(); iterator != result.actorRespawnResults.rend(); ++iterator)
			{
				if (iterator->previousSectorId != iterator->currentSectorId &&
					!m_impl->sectorGrid.TransferEntity(iterator->entityId, iterator->currentSectorId, iterator->previousSectorId))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = result.monsterAiResults.rbegin(); iterator != result.monsterAiResults.rend(); ++iterator)
			{
				if (iterator->previousSectorId != iterator->currentSectorId &&
					!m_impl->sectorGrid.TransferEntity(iterator->entityId, iterator->currentSectorId, iterator->previousSectorId))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = result.moveResults.rbegin(); iterator != result.moveResults.rend(); ++iterator)
			{
				if (iterator->previousSectorId != iterator->currentSectorId &&
					!m_impl->sectorGrid.TransferEntity(iterator->entityId, iterator->currentSectorId, iterator->previousSectorId))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = monsterAiRollbackEntries.rbegin(); iterator != monsterAiRollbackEntries.rend(); ++iterator)
			{
				FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(iterator->entityId);
				if (monster == nullptr || !monster->RestoreCommittedAiState(iterator->state))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = monsterCombatRollbackEntries.rbegin(); iterator != monsterCombatRollbackEntries.rend(); ++iterator)
			{
				FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(iterator->entityId);
				if (monster == nullptr || !monster->RestoreCommittedCombatState(iterator->state))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = playerMoveRollbackEntries.rbegin(); iterator != playerMoveRollbackEntries.rend(); ++iterator)
			{
				if (iterator->player == nullptr || !iterator->player->RestoreCommittedMoveState(iterator->state))
				{
					rollbackSucceeded = false;
				}
			}
			for (auto iterator = playerLifecycleRollbackEntries.rbegin(); iterator != playerLifecycleRollbackEntries.rend(); ++iterator)
			{
				if (iterator->player == nullptr || !iterator->player->RestoreCommittedLifecycleState(iterator->state))
				{
					rollbackSucceeded = false;
				}
			}
			return rollbackSucceeded;
		};
		if (!m_impl->deferredCommands.Validate(m_impl->sectorGrid, result.failureReason) ||
			!m_impl->deferredCommands.Commit(m_impl->sectorGrid, result.failureReason))
		{
			result.result = EMapTickResult::Failed;
			m_impl->tickExecution.Reset();
			return result;
		}

		std::vector<FEntityId> movedEntityIds;
		movedEntityIds.reserve(result.moveResults.size() + result.monsterAiResults.size());
		for (const SMoveResult& moveResult : result.moveResults)
		{
			FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(moveResult.entityId);
			if (player == nullptr || !player->ApplyMove(moveResult))
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Validated movement could not be committed to the Entity."
														 : "Validated movement failed and its rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
			movedEntityIds.push_back(moveResult.entityId);
		}
		for (std::size_t index = 0; index < result.monsterAiResults.size(); ++index)
		{
			const SMonsterAiResult& monsterAiResult = result.monsterAiResults[index];
			FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(monsterAiResult.entityId);
			const FMonsterEntity::SCommittedAiState& previousState = monsterAiRollbackEntries[index].state;
			if (monster == nullptr || !monster->ApplyAiResult(monsterAiResult))
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.monsterAiResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Validated AI output could not be committed to the Monster."
														 : "Validated AI output failed and its rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
			if (previousState.position != monsterAiResult.acceptedPosition || previousState.direction != monsterAiResult.direction ||
				previousState.moveState != monsterAiResult.moveState)
			{
				movedEntityIds.push_back(monsterAiResult.entityId);
			}
		}
		const std::uint64_t playerAttackCooldownTicks = std::max<std::uint64_t>(1,
			(static_cast<std::uint64_t>(m_impl->definition.combatPolicy.playerBasicAttackCooldownMilliseconds) *
					m_impl->definition.tickRateHz +
				999) /
				1'000);
		for (const SPlayerAttackIntent& attackIntent : playerAttackIntents)
		{
			FPlayerEntity* const attacker = m_impl->entityRegistry.FindPlayer(attackIntent.attackerEntityId);
			FMonsterEntity* const target = m_impl->entityRegistry.FindMonster(attackIntent.targetEntityId);
			if (attacker == nullptr || target == nullptr)
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorDeathResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded
										   ? "Validated Player attack participants disappeared before Commit."
										   : "Player attack participant Commit failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
			if (target->GetCurrentHp() == 0)
			{
				result.rejectedAttackRequests.push_back({{attackIntent.attackerEntityId, attackIntent.attackSequence},
					attackIntent.targetEntityId,
					EPlayerAttackRejectReason::TargetDead});
				continue;
			}

			const std::uint32_t attackerPower = attacker->GetRuntimeSnapshot().attack;
			const std::uint32_t targetDefense = target->GetRuntimeSnapshot().defense;
			const std::uint32_t rawDamage = attackerPower > targetDefense ? attackerPower - targetDefense : 0;
			const std::uint32_t requestedDamage = std::max(m_impl->definition.combatPolicy.minimumDamage, rawDamage);
			std::uint32_t appliedDamage = 0;
			if (!attacker->CommitBasicAttackCooldown(ticket.tickIndex, playerAttackCooldownTicks) ||
				!target->ApplyDamage(requestedDamage, appliedDamage) || appliedDamage == 0)
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorDeathResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Validated Player attack could not be committed."
														 : "Player attack Commit failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}

			result.playerAttackResults.push_back({attackIntent.attackerEntityId,
				attackIntent.attackSequence,
				attackIntent.targetEntityId,
				appliedDamage,
				target->GetCurrentHp(),
				target->GetRuntimeSnapshot().maxHp});
			result.actorAttackResults.push_back({attackIntent.attackerEntityId,
				attackIntent.targetEntityId,
				appliedDamage,
				target->GetCurrentHp(),
				target->GetRuntimeSnapshot().maxHp});
			if (target->GetCurrentHp() == 0)
			{
				result.actorDeathResults.push_back(
					{target->GetEntityId(), attackIntent.attackerEntityId, target->GetSpawnGeneration(), ticket.tickIndex});
			}
			else if (target->GetRuntimeSnapshot().aggroType == EMonsterAggroType::Passive &&
					 !target->AcquireAttacker(attacker->GetEntityId(), attacker->GetPosition()))
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorDeathResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded
										   ? "Passive Monster could not acquire its attacker."
										   : "Passive Monster target acquisition failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
		}
		if (result.playerAttackResults.size() + result.rejectedAttackRequests.size() != result.consumedAttackRequests.size())
		{
			const bool rollbackSucceeded = rollbackMovementCommit();
			result.playerAttackResults.clear();
			result.actorAttackResults.clear();
			result.actorDeathResults.clear();
			result.result = EMapTickResult::Failed;
			result.failureReason = rollbackSucceeded
									   ? "Consumed Player attack did not resolve exactly once."
									   : "Player attack resolution invariant failed and rollback could not restore Map state.";
			m_impl->tickExecution.Reset();
			return result;
		}
		for (const SMonsterAttackIntent& attackIntent : attackIntents)
		{
			FMonsterEntity* const attacker = m_impl->entityRegistry.FindMonster(attackIntent.attackerEntityId);
			FPlayerEntity* const target = m_impl->entityRegistry.FindPlayer(attackIntent.targetEntityId);
			if (attacker == nullptr || target == nullptr)
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.monsterAiResults.clear();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Validated attack participants disappeared before Commit."
														 : "Attack participant Commit failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
			if (attacker->GetCurrentHp() == 0 || !target->IsAlive() || target->GetCurrentHp() == 0)
			{
				continue;
			}

			const std::uint32_t attackerPower = attacker->GetRuntimeSnapshot().attack;
			const std::uint32_t targetDefense = target->GetRuntimeSnapshot().defense;
			const std::uint32_t rawDamage = attackerPower > targetDefense ? attackerPower - targetDefense : 0;
			const std::uint32_t requestedDamage = std::max(m_impl->definition.combatPolicy.minimumDamage, rawDamage);
			std::uint32_t appliedDamage = 0;
			if (!target->ApplyDamage(requestedDamage, appliedDamage) || appliedDamage == 0 ||
				!attacker->CommitAttackCooldown(ticket.tickIndex))
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.monsterAiResults.clear();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Validated Monster attack could not be committed."
														 : "Monster attack Commit failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}

			result.actorAttackResults.push_back({attackIntent.attackerEntityId,
				attackIntent.targetEntityId,
				appliedDamage,
				target->GetCurrentHp(),
				target->GetRuntimeSnapshot().maxHp});
			if (target->GetCurrentHp() == 0)
			{
				if (!target->CommitDeath(attackIntent.attackerEntityId, ticket.tickIndex, m_impl->definition.playerRespawnDelayTicks))
				{
					const bool rollbackSucceeded = rollbackMovementCommit();
					result.moveResults.clear();
					result.monsterAiResults.clear();
					result.playerAttackResults.clear();
					result.actorAttackResults.clear();
					result.actorDeathResults.clear();
					result.result = EMapTickResult::Failed;
					result.failureReason = rollbackSucceeded ? "Player Death could not be committed."
															 : "Player Death Commit failed and rollback could not restore Map state.";
					m_impl->tickExecution.Reset();
					return result;
				}
				result.actorDeathResults.push_back(
					{target->GetEntityId(), attackIntent.attackerEntityId, target->GetLifeRevision(), ticket.tickIndex});
			}
		}
		for (const FEntityId playerEntityId : playerEntityIds)
		{
			FPlayerEntity* const player = m_impl->entityRegistry.FindPlayer(playerEntityId);
			if (player == nullptr || player->IsAlive() || player->GetRespawnDueTick() > ticket.tickIndex)
			{
				continue;
			}
			if (player->GetLifeRevision() == std::numeric_limits<std::uint64_t>::max())
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.monsterAiResults.clear();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorDeathResults.clear();
				result.actorRespawnResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Player LifeRevision space was exhausted."
														 : "Player LifeRevision exhaustion rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
			const std::uint64_t nextLifeRevision = player->GetLifeRevision() + 1;

			SVector2 respawnPosition{};
			FSectorId respawnSectorId = kInvalidSectorId;
			std::string spawnError;
			if (!player->HasRuntimeSnapshot() || !m_impl->playerRespawnSystem.TrySelectSpawnPosition(m_impl->mapInstanceId,
													 player->GetEntityId(),
													 nextLifeRevision,
													 player->GetRuntimeSnapshot().collisionRadius,
													 m_impl->definition,
													 m_impl->entityRegistry,
													 m_impl->sectorGrid,
													 respawnPosition,
													 respawnSectorId,
													 spawnError))
			{
				// A full spawn area is a normal, retryable condition.  The Player stays dead in the registry.
				continue;
			}

			const FSectorId previousSectorId = player->GetSectorId();
			const bool requiresSectorTransfer = previousSectorId != respawnSectorId;
			const bool transferredSector =
				!requiresSectorTransfer || m_impl->sectorGrid.TransferEntity(player->GetEntityId(), previousSectorId, respawnSectorId);
			if (!transferredSector || !player->CommitRespawn(respawnPosition, player->GetDirection(), respawnSectorId))
			{
				if (requiresSectorTransfer && transferredSector)
				{
					(void)m_impl->sectorGrid.TransferEntity(player->GetEntityId(), respawnSectorId, previousSectorId);
				}
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.moveResults.clear();
				result.monsterAiResults.clear();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorDeathResults.clear();
				result.actorRespawnResults.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = rollbackSucceeded ? "Player Respawn could not be committed."
														 : "Player Respawn Commit failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}

			result.actorRespawnResults.push_back({player->GetEntityId(),
				respawnPosition,
				player->GetDirection(),
				player->GetCurrentHp(),
				player->GetRuntimeSnapshot().maxHp,
				player->GetLifeRevision(),
				ticket.tickIndex,
				previousSectorId,
				respawnSectorId});
			movedEntityIds.push_back(player->GetEntityId());
		}

		// Attack and Death observers must be captured while a lethal Monster is still present in the previous visibility set.
		result.actorAttackEvents = m_impl->visibilitySystem.BuildActorAttackEvents(m_impl->entityRegistry, result.actorAttackResults);
		result.actorDeathEvents = m_impl->visibilitySystem.BuildActorDeathEvents(m_impl->entityRegistry, result.actorDeathResults);

		struct SRemovedMonsterRollbackEntry final
		{
			FEntityId entityId = kInvalidEntityId;
			FSpawnerDataId spawnerDataId = kInvalidSpawnerDataId;
			FSpawnGeneration spawnGeneration = kInvalidSpawnGeneration;
			SVector2 spawnPosition{};
			SMonsterRuntimeSnapshot snapshot{};
			FMonsterEntity::SCommittedAiState aiState{};
			FMonsterEntity::SCommittedCombatState combatState{};
		};
		std::vector<SRemovedMonsterRollbackEntry> removedMonsters;
		for (const SActorDeathResult& death : result.actorDeathResults)
		{
			const FMonsterEntity* const monster = m_impl->entityRegistry.FindMonster(death.entityId);
			if (monster == nullptr)
			{
				continue;
			}
			const auto aiState = std::ranges::find_if(monsterAiRollbackEntries,
				[&](const SMonsterAiRollbackEntry& entry)
				{
					return entry.entityId == death.entityId;
				});
			const auto combatState = std::ranges::find_if(monsterCombatRollbackEntries,
				[&](const SMonsterCombatRollbackEntry& entry)
				{
					return entry.entityId == death.entityId;
				});
			if (aiState == monsterAiRollbackEntries.end() || combatState == monsterCombatRollbackEntries.end() ||
				!m_impl->monsterSpawnSystem.CanScheduleRespawn(
					death.entityId, monster->GetSpawnerDataId(), monster->GetSpawnGeneration(), ticket.tickIndex, result.failureReason))
			{
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorAttackEvents.clear();
				result.actorDeathResults.clear();
				result.actorDeathEvents.clear();
				result.result = EMapTickResult::Failed;
				if (!rollbackSucceeded)
				{
					result.failureReason += " Combat rollback failed.";
				}
				m_impl->tickExecution.Reset();
				return result;
			}
			removedMonsters.push_back({death.entityId,
				monster->GetSpawnerDataId(),
				monster->GetSpawnGeneration(),
				monster->GetSpawnPosition(),
				monster->GetRuntimeSnapshot(),
				aiState->state,
				combatState->state});
		}

		auto restoreRemovedMonsters = [&]() noexcept
		{
			bool restored = true;
			for (auto iterator = removedMonsters.rbegin(); iterator != removedMonsters.rend(); ++iterator)
			{
				const bool restoredSpawner = m_impl->monsterSpawnSystem.RollbackScheduledRespawn(
					iterator->entityId, iterator->spawnerDataId, iterator->spawnGeneration, ticket.tickIndex);
				const bool restoredRegistry = restoredSpawner && m_impl->entityRegistry.AddMonster(iterator->entityId,
																	 iterator->spawnerDataId,
																	 iterator->spawnGeneration,
																	 iterator->spawnPosition,
																	 iterator->aiState.position,
																	 iterator->aiState.direction,
																	 iterator->aiState.sectorId,
																	 iterator->snapshot,
																	 iterator->combatState.currentHp);
				FMonsterEntity* const restoredMonster = restoredRegistry ? m_impl->entityRegistry.FindMonster(iterator->entityId) : nullptr;
				const bool restoredAi = restoredMonster != nullptr && restoredMonster->RestoreCommittedAiState(iterator->aiState);
				const bool restoredSector = restoredAi && m_impl->sectorGrid.AddEntity(iterator->aiState.sectorId, iterator->entityId);
				restored = restored && restoredSector;
			}
			return restored;
		};

		for (std::size_t removedIndex = 0; removedIndex < removedMonsters.size(); ++removedIndex)
		{
			const SRemovedMonsterRollbackEntry& removed = removedMonsters[removedIndex];
			if (!m_impl->sectorGrid.RemoveEntity(removed.entityId) || !m_impl->entityRegistry.RemoveMonster(removed.entityId) ||
				!m_impl->monsterSpawnSystem.ScheduleRespawn(
					removed.entityId, removed.spawnerDataId, removed.spawnGeneration, ticket.tickIndex, result.failureReason))
			{
				// All operations were prevalidated.  A failure here is an invariant break; restore the current entry if needed.
				if (m_impl->entityRegistry.FindMonster(removed.entityId) != nullptr)
				{
					(void)m_impl->sectorGrid.AddEntity(removed.aiState.sectorId, removed.entityId);
				}
				else
				{
					const bool restoredRegistry = m_impl->entityRegistry.AddMonster(removed.entityId,
						removed.spawnerDataId,
						removed.spawnGeneration,
						removed.spawnPosition,
						removed.aiState.position,
						removed.aiState.direction,
						removed.aiState.sectorId,
						removed.snapshot,
						removed.combatState.currentHp);
					FMonsterEntity* const restoredMonster =
						restoredRegistry ? m_impl->entityRegistry.FindMonster(removed.entityId) : nullptr;
					if (restoredMonster != nullptr)
					{
						(void)restoredMonster->RestoreCommittedAiState(removed.aiState);
						(void)m_impl->sectorGrid.AddEntity(removed.aiState.sectorId, removed.entityId);
					}
				}
				removedMonsters.resize(removedIndex);
				const bool restoredPrevious = restoreRemovedMonsters();
				const bool rollbackSucceeded = rollbackMovementCommit();
				result.playerAttackResults.clear();
				result.actorAttackResults.clear();
				result.actorAttackEvents.clear();
				result.actorDeathResults.clear();
				result.actorDeathEvents.clear();
				result.result = EMapTickResult::Failed;
				result.failureReason = restoredPrevious && rollbackSucceeded
										   ? "Monster Death removal failed after pre-commit validation."
										   : "Monster Death removal failed and rollback could not restore Map state.";
				m_impl->tickExecution.Reset();
				return result;
			}
		}
		if (!m_impl->monsterSpawnSystem.CommitInitialAndDueSpawns(
				ticket.tickIndex, m_impl->entityRegistry, m_impl->sectorGrid, result.spawnResults, result.failureReason))
		{
			const std::string spawnFailureReason = result.failureReason;
			const bool restoredDeaths = restoreRemovedMonsters();
			const bool rollbackSucceeded = rollbackMovementCommit();
			result.moveResults.clear();
			result.monsterAiResults.clear();
			result.actorAttackResults.clear();
			result.actorAttackEvents.clear();
			result.actorDeathResults.clear();
			result.actorDeathEvents.clear();
			result.actorRespawnResults.clear();
			result.actorRespawnEvents.clear();
			result.playerAttackResults.clear();
			result.spawnResults.clear();
			result.result = EMapTickResult::Failed;
			result.failureReason =
				restoredDeaths && rollbackSucceeded ? spawnFailureReason : spawnFailureReason + " Combat rollback failed.";
			m_impl->tickExecution.Reset();
			return result;
		}
		for (const SActorDeathResult& death : result.actorDeathResults)
		{
			const std::optional<SMoveRequestIdentity> rejectedMove = m_impl->inputBuffer.DiscardPendingMove(death.entityId);
			if (rejectedMove.has_value())
			{
				result.rejectedMoveRequests.push_back(*rejectedMove);
			}
		}

		result.visibilityEvents = m_impl->visibilitySystem.Refresh(
			m_impl->entityRegistry, m_impl->sectorGrid, m_impl->definition.visibilitySectorRadius, movedEntityIds);
		result.actorRespawnEvents = m_impl->visibilitySystem.BuildActorRespawnEvents(m_impl->entityRegistry, result.actorRespawnResults);
		std::sort(result.rejectedAttackRequests.begin(),
			result.rejectedAttackRequests.end(),
			[](const SRejectedPlayerAttack& lhs, const SRejectedPlayerAttack& rhs)
			{
				return lhs.request.attackerEntityId < rhs.request.attackerEntityId ||
					   (lhs.request.attackerEntityId == rhs.request.attackerEntityId &&
						   lhs.request.attackSequence < rhs.request.attackSequence);
			});
		m_impl->lastCommittedTickIndex = ticket.tickIndex;
		result.result = EMapTickResult::Completed;
		m_impl->tickExecution.Reset();
		return result;
	}

	void FMapInstance::InjectNextMonsterSpawnCommitFailureForTesting() noexcept
	{
		m_impl->monsterSpawnSystem.InjectNextCommitFailureForTesting();
	}

	SMapTickResult FMapInstance::Tick()
	{
		switch (m_impl->tickExecution.GetState())
		{
			case EMapTickExecutionState::Idle:
				return StartTickExecution();
			case EMapTickExecutionState::Executing:
				return FinalizeTickExecution();
			case EMapTickExecutionState::ReadyToCommit:
			case EMapTickExecutionState::Failed:
				return FinalizeTickExecution();
			default:
				SMapTickResult result{};
				result.mapInstanceId = m_impl->mapInstanceId;
				result.result = EMapTickResult::Failed;
				result.failureReason = "Map Tick execution entered an unknown state.";
				return result;
		}
	}
}
