#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"

namespace WorldCore
{
	namespace
	{
		std::vector<FEntityId> GetStableSortedEntityIds(
			const std::unordered_map<FEntityId, std::unique_ptr<FActorEntity>>& actors,
			const std::optional<EActorKind> actorKind)
		{
			std::vector<FEntityId> entityIds;
			entityIds.reserve(actors.size());
			for (const auto& [entityId, actor] : actors)
			{
				if (!actorKind.has_value() || actor->GetActorKind() == actorKind.value())
				{
					entityIds.push_back(entityId);
				}
			}
			std::sort(entityIds.begin(), entityIds.end());
			return entityIds;
		}
	}

	struct FEntityRegistry::SImpl final
	{
		std::unordered_map<FEntityId, std::unique_ptr<FActorEntity>> actors;
		std::size_t playerCount = 0;
		std::size_t monsterCount = 0;
	};

	FEntityRegistry::FEntityRegistry()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FEntityRegistry::~FEntityRegistry() = default;

	bool FEntityRegistry::AddPlayer(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId)
	{
		if (entityId == kInvalidEntityId || userId == kInvalidUserId || sectorId == kInvalidSectorId || !IsFinite(position))
		{
			return false;
		}

		const bool inserted =
			m_impl->actors.emplace(entityId, std::make_unique<FPlayerEntity>(entityId, userId, position, direction, sectorId)).second;
		if (inserted)
		{
			++m_impl->playerCount;
		}
		return inserted;
	}

	bool FEntityRegistry::AddPlayer(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId,
		const SPlayerRuntimeSnapshot& runtimeSnapshot)
	{
		std::string snapshotError;
		if (entityId == kInvalidEntityId || userId == kInvalidUserId || sectorId == kInvalidSectorId || !IsFinite(position) ||
			!IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}

		const bool inserted =
			m_impl->actors
				.emplace(entityId, std::make_unique<FPlayerEntity>(entityId, userId, position, direction, sectorId, runtimeSnapshot))
				.second;
		if (inserted)
		{
			++m_impl->playerCount;
		}
		return inserted;
	}

	bool FEntityRegistry::AddMonster(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId,
		const SMonsterRuntimeSnapshot& runtimeSnapshot,
		const std::uint32_t currentHp)
	{
		return AddMonster(entityId, spawnerDataId, spawnGeneration, position, position, direction, sectorId, runtimeSnapshot, currentHp);
	}

	bool FEntityRegistry::AddMonster(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const SVector2& spawnPosition,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId,
		const SMonsterRuntimeSnapshot& runtimeSnapshot,
		const std::uint32_t currentHp)
	{
		std::string snapshotError;
		if (entityId == kInvalidEntityId || spawnerDataId == kInvalidSpawnerDataId || spawnGeneration == kInvalidSpawnGeneration ||
			sectorId == kInvalidSectorId || !IsFinite(spawnPosition) || !IsFinite(position) || !IsFinite(direction) || currentHp == 0 ||
			!IsValidMonsterRuntimeSnapshot(runtimeSnapshot, snapshotError) || currentHp > runtimeSnapshot.maxHp)
		{
			return false;
		}

		const bool inserted =
			m_impl->actors
				.emplace(entityId,
					std::make_unique<FMonsterEntity>(
						entityId, spawnerDataId, spawnGeneration, spawnPosition, position, direction, sectorId, runtimeSnapshot, currentHp))
				.second;
		if (inserted)
		{
			++m_impl->monsterCount;
		}
		return inserted;
	}

	bool FEntityRegistry::RemoveActor(
		const FEntityId entityId)
	{
		const auto found = m_impl->actors.find(entityId);
		if (found == m_impl->actors.end())
		{
			return false;
		}

		if (found->second->GetActorKind() == EActorKind::Player)
		{
			--m_impl->playerCount;
		}
		else
		{
			--m_impl->monsterCount;
		}
		m_impl->actors.erase(found);
		return true;
	}

	bool FEntityRegistry::RemovePlayer(
		const FEntityId entityId)
	{
		const FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Player && RemoveActor(entityId);
	}

	bool FEntityRegistry::RemoveMonster(
		const FEntityId entityId)
	{
		const FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Monster && RemoveActor(entityId);
	}

	FActorEntity* FEntityRegistry::FindActor(
		const FEntityId entityId) noexcept
	{
		const auto found = m_impl->actors.find(entityId);
		return found == m_impl->actors.end() ? nullptr : found->second.get();
	}

	const FActorEntity* FEntityRegistry::FindActor(
		const FEntityId entityId) const noexcept
	{
		const auto found = m_impl->actors.find(entityId);
		return found == m_impl->actors.end() ? nullptr : found->second.get();
	}

	FPlayerEntity* FEntityRegistry::FindPlayer(
		const FEntityId entityId) noexcept
	{
		FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Player ? static_cast<FPlayerEntity*>(actor) : nullptr;
	}

	const FPlayerEntity* FEntityRegistry::FindPlayer(
		const FEntityId entityId) const noexcept
	{
		const FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Player ? static_cast<const FPlayerEntity*>(actor) : nullptr;
	}

	FMonsterEntity* FEntityRegistry::FindMonster(
		const FEntityId entityId) noexcept
	{
		FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Monster ? static_cast<FMonsterEntity*>(actor) : nullptr;
	}

	const FMonsterEntity* FEntityRegistry::FindMonster(
		const FEntityId entityId) const noexcept
	{
		const FActorEntity* const actor = FindActor(entityId);
		return actor != nullptr && actor->GetActorKind() == EActorKind::Monster ? static_cast<const FMonsterEntity*>(actor) : nullptr;
	}

	bool FEntityRegistry::Contains(
		const FEntityId entityId) const noexcept
	{
		return m_impl->actors.contains(entityId);
	}

	std::size_t FEntityRegistry::GetActorCount() const noexcept
	{
		return m_impl->actors.size();
	}

	std::size_t FEntityRegistry::GetPlayerCount() const noexcept
	{
		return m_impl->playerCount;
	}

	std::size_t FEntityRegistry::GetMonsterCount() const noexcept
	{
		return m_impl->monsterCount;
	}

	std::vector<FEntityId> FEntityRegistry::GetActorEntityIds() const
	{
		return GetStableSortedEntityIds(m_impl->actors, std::nullopt);
	}

	std::vector<FEntityId> FEntityRegistry::GetPlayerEntityIds() const
	{
		return GetStableSortedEntityIds(m_impl->actors, EActorKind::Player);
	}

	std::vector<FEntityId> FEntityRegistry::GetMonsterEntityIds() const
	{
		return GetStableSortedEntityIds(m_impl->actors, EActorKind::Monster);
	}
}
