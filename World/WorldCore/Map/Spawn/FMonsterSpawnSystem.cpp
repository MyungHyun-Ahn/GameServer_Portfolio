#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Spawn/FDeterministicSpawnSampler.h"
#include "WorldCore/Map/Spawn/FMonsterSpawnSystem.h"

namespace WorldCore
{
	namespace
	{
		constexpr std::uint32_t kMaximumPositionCandidateCount = 8;

		void HashCombine(
			std::uint64_t& hash,
			const std::uint64_t value) noexcept
		{
			hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
		}

		void HashFloat(
			std::uint64_t& hash,
			const float value) noexcept
		{
			HashCombine(hash, std::bit_cast<std::uint32_t>(value));
		}

	}

	struct FMonsterSpawnSystem::SImpl final
	{
		struct SPendingSpawn final
		{
			std::uint64_t dueTick = 0;
			std::uint64_t requestSequence = 0;

			bool operator<(
				const SPendingSpawn& other) const noexcept
			{
				return dueTick < other.dueTick || (dueTick == other.dueTick && requestSequence < other.requestSequence);
			}
		};

		struct SSpawnerState final
		{
			SMonsterSpawnerRuntimeDefinition definition{};
			std::map<FEntityId, FSpawnGeneration> aliveEntities;
			std::set<SPendingSpawn> pendingSpawns;
			std::uint64_t nextRequestSequence = 1;
			std::uint64_t nextAttemptSequence = 1;
			FSpawnGeneration nextSpawnGeneration = 1;
		};

		bool configured = false;
		bool failNextCommitForTesting = false;
		FMapInstanceId mapInstanceId = kInvalidMapInstanceId;
		std::uint64_t fixedSeed = 0;
		std::uint64_t nextMonsterEntitySequence = 1;
		std::map<FSpawnerDataId, SSpawnerState> spawners;

		[[nodiscard]] SVector2 BuildPosition(
			const SSpawnerState& state,
			const std::uint64_t attemptSequence,
			const std::uint32_t candidateIndex) const noexcept
		{
			const float radius = state.definition.monsterSnapshot.collisionRadius;
			const float minimumX = state.definition.areaMinimum.x + radius;
			const float minimumY = state.definition.areaMinimum.y + radius;
			const float maximumX = state.definition.areaMaximum.x - radius;
			const float maximumY = state.definition.areaMaximum.y - radius;
			return FDeterministicSpawnSampler::BuildPosition(fixedSeed,
				mapInstanceId,
				state.definition.spawnerDataId,
				attemptSequence,
				candidateIndex,
				minimumX,
				minimumY,
				maximumX,
				maximumY);
		}

		[[nodiscard]] static bool IsPositionAvailable(
			const SMonsterRuntimeSnapshot& snapshot,
			const SVector2& position,
			const FEntityRegistry& entityRegistry) noexcept
		{
			for (const FEntityId entityId : entityRegistry.GetActorEntityIds())
			{
				const FActorEntity* const actor = entityRegistry.FindActor(entityId);
				if (actor == nullptr)
				{
					continue;
				}
				float otherRadius = 0.0f;
				if (actor->GetActorKind() == EActorKind::Monster)
				{
					const FMonsterEntity* const monster = entityRegistry.FindMonster(entityId);
					otherRadius = monster == nullptr ? 0.0f : monster->GetRuntimeSnapshot().collisionRadius;
				}
				else
				{
					const FPlayerEntity* const player = entityRegistry.FindPlayer(entityId);
					if (player == nullptr || !player->IsAlive())
					{
						continue;
					}
					otherRadius = player->HasRuntimeSnapshot() ? player->GetRuntimeSnapshot().collisionRadius : 0.0f;
				}
				const float minimumDistance = snapshot.collisionRadius + otherRadius;
				if (GetDistanceSquared(position, actor->GetPosition()) < minimumDistance * minimumDistance)
				{
					return false;
				}
			}
			return true;
		}
	};

	FMonsterSpawnSystem::FMonsterSpawnSystem()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FMonsterSpawnSystem::~FMonsterSpawnSystem() = default;

	bool FMonsterSpawnSystem::Configure(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& mapDefinition,
		const std::uint64_t fixedSeed,
		const std::span<const SMonsterSpawnerRuntimeDefinition> definitions,
		std::string& outError)
	{
		outError.clear();
		if (m_impl->configured)
		{
			outError = "Monster Spawn System is already configured.";
			return false;
		}
		if (mapInstanceId == kInvalidMapInstanceId)
		{
			outError = "Monster Spawn System requires a valid MapInstanceId.";
			return false;
		}

		std::map<FSpawnerDataId, SImpl::SSpawnerState> preparedSpawners;
		for (const SMonsterSpawnerRuntimeDefinition& definition : definitions)
		{
			if (!IsValidMonsterSpawnerRuntimeDefinition(definition, mapDefinition, outError))
			{
				return false;
			}
			SImpl::SSpawnerState state{};
			state.definition = definition;
			for (std::uint32_t index = 0; index < definition.initialSpawnCount; ++index)
			{
				state.pendingSpawns.insert({0, state.nextRequestSequence++});
			}
			if (!preparedSpawners.emplace(definition.spawnerDataId, std::move(state)).second)
			{
				outError = "Monster Spawn System received duplicate SpawnerDataId values.";
				return false;
			}
		}

		m_impl->configured = true;
		m_impl->mapInstanceId = mapInstanceId;
		m_impl->fixedSeed = fixedSeed;
		m_impl->spawners = std::move(preparedSpawners);
		return true;
	}

	bool FMonsterSpawnSystem::IsConfigured() const noexcept
	{
		return m_impl->configured;
	}

	bool FMonsterSpawnSystem::CommitInitialAndDueSpawns(
		const std::uint64_t committedTick,
		FEntityRegistry& entityRegistry,
		FSectorGrid& sectorGrid,
		std::vector<SMonsterSpawnResult>& outResults,
		std::string& outError)
	{
		outResults.clear();
		outError.clear();
		if (m_impl->failNextCommitForTesting)
		{
			m_impl->failNextCommitForTesting = false;
			outError = "Injected Monster Spawn Commit failure.";
			return false;
		}
		if (!m_impl->configured)
		{
			return true;
		}

		const SImpl previousState = *m_impl;
		std::vector<FEntityId> insertedEntityIds;
		auto rollback = [&]() noexcept
		{
			for (auto iterator = insertedEntityIds.rbegin(); iterator != insertedEntityIds.rend(); ++iterator)
			{
				(void)sectorGrid.RemoveEntity(*iterator);
				(void)entityRegistry.RemoveMonster(*iterator);
			}
			*m_impl = previousState;
			outResults.clear();
		};

		for (auto& [spawnerDataId, state] : m_impl->spawners)
		{
			while (!state.pendingSpawns.empty() && state.pendingSpawns.begin()->dueTick <= committedTick &&
				   state.aliveEntities.size() < state.definition.maxAliveCount)
			{
				SVector2 position{};
				FSectorId sectorId = kInvalidSectorId;
				bool foundPosition = false;
				for (std::uint32_t candidateIndex = 0; candidateIndex < kMaximumPositionCandidateCount; ++candidateIndex)
				{
					if (state.nextAttemptSequence == std::numeric_limits<std::uint64_t>::max())
					{
						outError = "Monster Spawn attempt sequence was exhausted.";
						rollback();
						return false;
					}
					const std::uint64_t attemptSequence = state.nextAttemptSequence++;
					position = m_impl->BuildPosition(state, attemptSequence, candidateIndex);
					if (sectorGrid.TryResolveSector(position, sectorId) &&
						SImpl::IsPositionAvailable(state.definition.monsterSnapshot, position, entityRegistry))
					{
						foundPosition = true;
						break;
					}
				}
				if (!foundPosition)
				{
					break;
				}

				if (m_impl->nextMonsterEntitySequence >= kMapLocalEntityIdBit ||
					state.nextSpawnGeneration == std::numeric_limits<FSpawnGeneration>::max())
				{
					outError = "Monster EntityId or SpawnGeneration space was exhausted.";
					rollback();
					return false;
				}
				const FEntityId entityId = kMapLocalEntityIdBit | m_impl->nextMonsterEntitySequence;
				const FSpawnGeneration spawnGeneration = state.nextSpawnGeneration;
				if (!entityRegistry.AddMonster(entityId,
						spawnerDataId,
						spawnGeneration,
						position,
						{},
						sectorId,
						state.definition.monsterSnapshot,
						state.definition.monsterSnapshot.maxHp))
				{
					outError = "Monster Entity registration failed during Spawn Commit.";
					rollback();
					return false;
				}
				if (!sectorGrid.AddEntity(sectorId, entityId))
				{
					(void)entityRegistry.RemoveMonster(entityId);
					outError = "Monster Sector registration failed during Spawn Commit.";
					rollback();
					return false;
				}

				insertedEntityIds.push_back(entityId);
				state.aliveEntities.emplace(entityId, spawnGeneration);
				state.pendingSpawns.erase(state.pendingSpawns.begin());
				++m_impl->nextMonsterEntitySequence;
				++state.nextSpawnGeneration;
				outResults.push_back(
					{entityId, state.definition.monsterSnapshot.monsterDataId, spawnerDataId, spawnGeneration, position, sectorId});
			}
		}
		return true;
	}

	void FMonsterSpawnSystem::InjectNextCommitFailureForTesting() noexcept
	{
		m_impl->failNextCommitForTesting = true;
	}

	bool FMonsterSpawnSystem::CanScheduleRespawn(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const std::uint64_t deathTick,
		std::string& outError) const
	{
		outError.clear();
		const auto foundSpawner = m_impl->spawners.find(spawnerDataId);
		if (!m_impl->configured || foundSpawner == m_impl->spawners.end())
		{
			outError = "Monster is not tracked as alive by its Spawner.";
			return false;
		}
		const auto foundAlive = foundSpawner->second.aliveEntities.find(entityId);
		if (foundAlive == foundSpawner->second.aliveEntities.end() || foundAlive->second != spawnGeneration ||
			spawnGeneration == kInvalidSpawnGeneration)
		{
			outError = "Monster is not tracked as alive by its Spawner.";
			return false;
		}
		if (deathTick > std::numeric_limits<std::uint64_t>::max() - foundSpawner->second.definition.respawnDelayTicks ||
			foundSpawner->second.nextRequestSequence == std::numeric_limits<std::uint64_t>::max())
		{
			outError = "Monster Respawn schedule identity or due Tick was exhausted.";
			return false;
		}
		return true;
	}

	bool FMonsterSpawnSystem::ScheduleRespawn(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const std::uint64_t deathTick,
		std::string& outError)
	{
		if (!CanScheduleRespawn(entityId, spawnerDataId, spawnGeneration, deathTick, outError))
		{
			return false;
		}
		SImpl::SSpawnerState& state = m_impl->spawners.at(spawnerDataId);
		state.aliveEntities.erase(entityId);
		state.pendingSpawns.insert({deathTick + state.definition.respawnDelayTicks, state.nextRequestSequence++});
		return true;
	}

	bool FMonsterSpawnSystem::RollbackScheduledRespawn(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const std::uint64_t deathTick) noexcept
	{
		const auto foundSpawner = m_impl->spawners.find(spawnerDataId);
		if (!m_impl->configured || foundSpawner == m_impl->spawners.end() || spawnGeneration == kInvalidSpawnGeneration)
		{
			return false;
		}
		SImpl::SSpawnerState& state = foundSpawner->second;
		if (state.aliveEntities.contains(entityId) || state.nextRequestSequence <= 1 ||
			deathTick > std::numeric_limits<std::uint64_t>::max() - state.definition.respawnDelayTicks)
		{
			return false;
		}

		const SImpl::SPendingSpawn pending{deathTick + state.definition.respawnDelayTicks, state.nextRequestSequence - 1};
		const auto foundPending = state.pendingSpawns.find(pending);
		if (foundPending == state.pendingSpawns.end())
		{
			return false;
		}
		state.pendingSpawns.erase(foundPending);
		--state.nextRequestSequence;
		return state.aliveEntities.emplace(entityId, spawnGeneration).second;
	}

	std::uint64_t FMonsterSpawnSystem::GetStateHash() const noexcept
	{
		if (!m_impl->configured)
		{
			return 0;
		}
		std::uint64_t hash = 0x84222325CBF29CE4ull;
		HashCombine(hash, m_impl->mapInstanceId);
		HashCombine(hash, m_impl->fixedSeed);
		HashCombine(hash, m_impl->nextMonsterEntitySequence);
		for (const auto& [spawnerDataId, state] : m_impl->spawners)
		{
			HashCombine(hash, spawnerDataId);
			HashCombine(hash, state.definition.mapDataId);
			HashCombine(hash, state.definition.monsterSnapshot.monsterDataId);
			HashCombine(hash, static_cast<std::uint8_t>(state.definition.monsterSnapshot.monsterType));
			HashCombine(hash, static_cast<std::uint8_t>(state.definition.monsterSnapshot.aggroType));
			HashCombine(hash, state.definition.monsterSnapshot.maxHp);
			HashCombine(hash, state.definition.monsterSnapshot.attack);
			HashCombine(hash, state.definition.monsterSnapshot.defense);
			HashFloat(hash, state.definition.monsterSnapshot.moveSpeed);
			HashFloat(hash, state.definition.monsterSnapshot.collisionRadius);
			HashFloat(hash, state.definition.monsterSnapshot.aggroRadius);
			HashFloat(hash, state.definition.monsterSnapshot.leashRadius);
			HashFloat(hash, state.definition.monsterSnapshot.attackRange);
			HashCombine(hash, state.definition.monsterSnapshot.attackCooldownMilliseconds);
			HashCombine(hash, state.definition.monsterSnapshot.attackCooldownTicks);
			HashFloat(hash, state.definition.areaMinimum.x);
			HashFloat(hash, state.definition.areaMinimum.y);
			HashFloat(hash, state.definition.areaMaximum.x);
			HashFloat(hash, state.definition.areaMaximum.y);
			HashCombine(hash, state.definition.initialSpawnCount);
			HashCombine(hash, state.definition.maxAliveCount);
			HashCombine(hash, state.definition.respawnDelayTicks);
			HashCombine(hash, state.nextRequestSequence);
			HashCombine(hash, state.nextAttemptSequence);
			HashCombine(hash, state.nextSpawnGeneration);
			for (const auto& [entityId, spawnGeneration] : state.aliveEntities)
			{
				HashCombine(hash, entityId);
				HashCombine(hash, spawnGeneration);
			}
			for (const SImpl::SPendingSpawn& pending : state.pendingSpawns)
			{
				HashCombine(hash, pending.dueTick);
				HashCombine(hash, pending.requestSequence);
			}
		}
		return hash;
	}
}
