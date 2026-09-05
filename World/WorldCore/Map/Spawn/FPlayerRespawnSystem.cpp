#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Spawn/FDeterministicSpawnSampler.h"
#include "WorldCore/Map/Spawn/FPlayerRespawnSystem.h"

namespace WorldCore
{
	namespace
	{
		constexpr std::uint64_t kPlayerSpawnSeed = 0xD1B54A32D192ED03ull;
		constexpr std::uint32_t kMaximumPositionCandidateCount = 64;

		bool IsPositionAvailable(
			const FEntityId entityId,
			const float collisionRadius,
			const SVector2& position,
			const FEntityRegistry& entityRegistry) noexcept
		{
			for (const FEntityId otherEntityId : entityRegistry.GetActorEntityIds())
			{
				if (otherEntityId == entityId)
				{
					continue;
				}

				const FActorEntity* const actor = entityRegistry.FindActor(otherEntityId);
				if (actor == nullptr)
				{
					continue;
				}

				float otherRadius = 0.0f;
				if (actor->GetActorKind() == EActorKind::Monster)
				{
					const FMonsterEntity* const monster = entityRegistry.FindMonster(otherEntityId);
					otherRadius = monster == nullptr ? 0.0f : monster->GetRuntimeSnapshot().collisionRadius;
				}
				else
				{
					const FPlayerEntity* const player = entityRegistry.FindPlayer(otherEntityId);
					if (player == nullptr || !player->IsAlive())
					{
						continue;
					}
					otherRadius = player->HasRuntimeSnapshot() ? player->GetRuntimeSnapshot().collisionRadius : 0.0f;
				}

				const float minimumDistance = collisionRadius + otherRadius;
				if (GetDistanceSquared(position, actor->GetPosition()) < minimumDistance * minimumDistance)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool FPlayerRespawnSystem::TrySelectSpawnPosition(
		const FMapInstanceId mapInstanceId,
		const FEntityId entityId,
		const std::uint64_t lifeRevision,
		const float collisionRadius,
		const SMapDefinition& mapDefinition,
		const FEntityRegistry& entityRegistry,
		const FSectorGrid& sectorGrid,
		SVector2& outPosition,
		FSectorId& outSectorId,
		std::string& outError) const
	{
		outPosition = {};
		outSectorId = kInvalidSectorId;
		outError.clear();
		if (mapInstanceId == kInvalidMapInstanceId || entityId == kInvalidEntityId || lifeRevision == 0 ||
			!std::isfinite(collisionRadius) || collisionRadius < 0.0f)
		{
			outError = "Player Spawn selection received an invalid identity, LifeRevision, or CollisionRadius.";
			return false;
		}

		const float minimumX = mapDefinition.playerSpawnAreaMinimum.x + collisionRadius;
		const float minimumY = mapDefinition.playerSpawnAreaMinimum.y + collisionRadius;
		const float maximumX = mapDefinition.playerSpawnAreaMaximum.x - collisionRadius;
		const float maximumY = mapDefinition.playerSpawnAreaMaximum.y - collisionRadius;
		if (!std::isfinite(minimumX) || !std::isfinite(minimumY) || !std::isfinite(maximumX) || !std::isfinite(maximumY) ||
			minimumX >= maximumX || minimumY >= maximumY)
		{
			outError = "Player Spawn Area is not larger than the Player collision diameter.";
			return false;
		}

		for (std::uint32_t candidateIndex = 0; candidateIndex < kMaximumPositionCandidateCount; ++candidateIndex)
		{
			const SVector2 position = FDeterministicSpawnSampler::BuildPosition(
				kPlayerSpawnSeed, mapInstanceId, entityId, lifeRevision, candidateIndex, minimumX, minimumY, maximumX, maximumY);
			FSectorId sectorId = kInvalidSectorId;
			if (sectorGrid.TryResolveSector(position, sectorId) && IsPositionAvailable(entityId, collisionRadius, position, entityRegistry))
			{
				outPosition = position;
				outSectorId = sectorId;
				return true;
			}
		}

		outError = "Player Spawn Area does not currently contain an available position.";
		return false;
	}
}
