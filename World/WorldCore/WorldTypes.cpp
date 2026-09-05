#include "WorldCorePch.h"

#include "WorldCore/WorldTypes.h"

namespace WorldCore
{
	bool IsFinite(
		const SVector2& value) noexcept
	{
		return std::isfinite(value.x) && std::isfinite(value.y);
	}

	float GetDistanceSquared(
		const SVector2& lhs,
		const SVector2& rhs) noexcept
	{
		const float xDistance = lhs.x - rhs.x;
		const float yDistance = lhs.y - rhs.y;
		return xDistance * xDistance + yDistance * yDistance;
	}

	SVector2 NormalizeOrZero(
		const SVector2& value) noexcept
	{
		if (!IsFinite(value))
		{
			return {};
		}

		const float lengthSquared = value.x * value.x + value.y * value.y;
		if (lengthSquared <= std::numeric_limits<float>::epsilon())
		{
			return {};
		}

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return {value.x * inverseLength, value.y * inverseLength};
	}

	bool IsValidPlayerRuntimeSnapshot(
		const SPlayerRuntimeSnapshot& snapshot,
		std::string& outError)
	{
		outError.clear();
		if (snapshot.characterId == 0 || snapshot.characterDataId == 0 || snapshot.level == 0)
		{
			outError = "Player Snapshot identity and Level must be non-zero.";
			return false;
		}
		if (snapshot.progressVersion == 0 || snapshot.statVersion == 0 || snapshot.statRevision == 0)
		{
			outError = "Player Snapshot versions must be non-zero.";
			return false;
		}
		if (snapshot.maxHp == 0 || snapshot.maxMp == 0 || snapshot.moveSpeedMilli == 0 || !std::isfinite(snapshot.collisionRadius) ||
			snapshot.collisionRadius <= 0.0f)
		{
			outError = "Player Snapshot MaxHP, MaxMP, MoveSpeed, and CollisionRadius must be positive.";
			return false;
		}
		return true;
	}

	bool IsValidMonsterRuntimeSnapshot(
		const SMonsterRuntimeSnapshot& snapshot,
		std::string& outError)
	{
		outError.clear();
		if (snapshot.monsterDataId == kInvalidMonsterDataId)
		{
			outError = "Monster Snapshot identity must be non-zero.";
			return false;
		}
		if (snapshot.monsterType != EMonsterType::Normal && snapshot.monsterType != EMonsterType::Boss)
		{
			outError = "Monster Snapshot has an unknown MonsterType.";
			return false;
		}
		if (snapshot.aggroType != EMonsterAggroType::Aggressive && snapshot.aggroType != EMonsterAggroType::Passive)
		{
			outError = "Monster Snapshot has an unknown MonsterAggroType.";
			return false;
		}
		if (snapshot.maxHp == 0 || snapshot.attack == 0 || snapshot.attackCooldownMilliseconds == 0 || snapshot.attackCooldownTicks == 0)
		{
			outError = "Monster Snapshot MaxHP, Attack, and AttackCooldown values must be non-zero.";
			return false;
		}
		if (!std::isfinite(snapshot.moveSpeed) || snapshot.moveSpeed <= 0.0f || !std::isfinite(snapshot.collisionRadius) ||
			snapshot.collisionRadius <= 0.0f || !std::isfinite(snapshot.aggroRadius) || snapshot.aggroRadius <= 0.0f ||
			!std::isfinite(snapshot.leashRadius) || snapshot.leashRadius < snapshot.aggroRadius || !std::isfinite(snapshot.attackRange) ||
			snapshot.attackRange <= 0.0f || snapshot.attackRange > snapshot.aggroRadius)
		{
			outError = "Monster Snapshot movement, collision, Aggro, Leash, or combat ranges are invalid.";
			return false;
		}
		return true;
	}

	bool IsValidMonsterSpawnerRuntimeDefinition(
		const SMonsterSpawnerRuntimeDefinition& definition,
		const SMapDefinition& mapDefinition,
		std::string& outError)
	{
		outError.clear();
		if (definition.spawnerDataId == kInvalidSpawnerDataId || definition.mapDataId != mapDefinition.mapDataId)
		{
			outError = "Monster Spawner identity or MapDataId is invalid.";
			return false;
		}
		if (!IsValidMonsterRuntimeSnapshot(definition.monsterSnapshot, outError))
		{
			return false;
		}
		if (definition.monsterSnapshot.aggroRadius > static_cast<float>(mapDefinition.sectorSize))
		{
			outError = "Monster AggroRadius must not exceed SectorSize.";
			return false;
		}
		if (!IsFinite(definition.areaMinimum) || !IsFinite(definition.areaMaximum) || definition.areaMinimum.x < 0.0f ||
			definition.areaMinimum.y < 0.0f || definition.areaMaximum.x > static_cast<float>(mapDefinition.worldWidth) ||
			definition.areaMaximum.y > static_cast<float>(mapDefinition.worldHeight) ||
			definition.areaMinimum.x >= definition.areaMaximum.x || definition.areaMinimum.y >= definition.areaMaximum.y)
		{
			outError = "Monster Spawn Area must be a non-empty half-open rectangle inside the Map.";
			return false;
		}
		const float diameter = definition.monsterSnapshot.collisionRadius * 2.0f;
		if (definition.areaMaximum.x - definition.areaMinimum.x <= diameter ||
			definition.areaMaximum.y - definition.areaMinimum.y <= diameter)
		{
			outError = "Monster Spawn Area must be larger than the Monster collision diameter.";
			return false;
		}
		if (definition.initialSpawnCount > definition.maxAliveCount || definition.maxAliveCount == 0 || definition.respawnDelayTicks == 0)
		{
			outError = "Monster Spawner counts or RespawnDelayTicks are invalid.";
			return false;
		}
		return true;
	}

	bool IsValidMapDefinition(
		const SMapDefinition& definition,
		std::string& outError)
	{
		outError.clear();
		if (definition.mapDataId == kInvalidMapDataId)
		{
			outError = "MapDataId must be non-zero.";
			return false;
		}
		if (definition.worldWidth == 0 || definition.worldHeight == 0 || definition.sectorSize == 0)
		{
			outError = "World dimensions and SectorSize must be non-zero.";
			return false;
		}
		if (definition.worldWidth % definition.sectorSize != 0 || definition.worldHeight % definition.sectorSize != 0)
		{
			outError = "World dimensions must be exact multiples of SectorSize.";
			return false;
		}
		if (!IsFinite(definition.spawnPosition) || definition.spawnPosition.x < 0.0f || definition.spawnPosition.y < 0.0f ||
			definition.spawnPosition.x >= static_cast<float>(definition.worldWidth) ||
			definition.spawnPosition.y >= static_cast<float>(definition.worldHeight))
		{
			outError = "Spawn position must be inside the world boundary.";
			return false;
		}
		if (!IsFinite(definition.playerSpawnAreaMinimum) || !IsFinite(definition.playerSpawnAreaMaximum) ||
			definition.playerSpawnAreaMinimum.x < 0.0f || definition.playerSpawnAreaMinimum.y < 0.0f ||
			definition.playerSpawnAreaMaximum.x > static_cast<float>(definition.worldWidth) ||
			definition.playerSpawnAreaMaximum.y > static_cast<float>(definition.worldHeight) ||
			definition.playerSpawnAreaMinimum.x >= definition.playerSpawnAreaMaximum.x ||
			definition.playerSpawnAreaMinimum.y >= definition.playerSpawnAreaMaximum.y || definition.playerRespawnDelayTicks == 0)
		{
			outError = "Player Spawn Area or RespawnDelayTicks is invalid.";
			return false;
		}
		if (!std::isfinite(definition.maxAcceptedPositionError) || definition.maxAcceptedPositionError < 0.0f)
		{
			outError = "MaxAcceptedPositionError must be a finite non-negative value.";
			return false;
		}
		if (definition.tickRateHz == 0 || definition.tickRateHz > 1000)
		{
			outError = "TickRateHz must be between 1 and 1000.";
			return false;
		}
		if (definition.combatPolicy.minimumDamage == 0 || !std::isfinite(definition.combatPolicy.playerBasicAttackRange) ||
			definition.combatPolicy.playerBasicAttackRange <= 0.0f || definition.combatPolicy.playerBasicAttackCooldownMilliseconds == 0)
		{
			outError = "Combat policy values must be finite and positive.";
			return false;
		}
		if (definition.sectorExecutionMode != ESectorExecutionMode::Serial &&
			definition.sectorExecutionMode != ESectorExecutionMode::TaskGraph)
		{
			outError = "Unknown SectorExecutionMode.";
			return false;
		}
		return true;
	}
}
