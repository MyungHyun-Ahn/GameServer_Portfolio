#include "GameDataPch.h"

#include "GameData/MonsterSpawner/FMonsterSpawnerDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"
#include "GameData/Map/FMapDataTable.h"
#include "GameData/Monster/FMonsterDataTable.h"
#include "GameData/SpawnArea/FSpawnAreaDataTable.h"

namespace GameData::MonsterSpawner
{
	namespace
	{
		constexpr std::array<std::string_view, 7> kKnownKeys = {"SpawnerDataId",
			"MapDataId",
			"MonsterDataId",
			"SpawnAreaDataId",
			"InitialSpawnCount",
			"MaxAliveCount",
			"RespawnIntervalMilliseconds"};
	}

	bool FMonsterSpawnerDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SMonsterSpawnerData> loadedSpawners;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SMonsterSpawnerData spawner;
			if (!reader.ReadRequiredUInt32(sectionName, "SpawnerDataId", spawner.spawnerDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MapDataId", spawner.mapDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MonsterDataId", spawner.monsterDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "SpawnAreaDataId", spawner.spawnAreaDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialSpawnCount", spawner.initialSpawnCount, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MaxAliveCount", spawner.maxAliveCount, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "RespawnIntervalMilliseconds", spawner.respawnIntervalMilliseconds, outError))
			{
				return false;
			}

			if (spawner.spawnerDataId == 0 || spawner.mapDataId == 0 || spawner.monsterDataId == 0 || spawner.spawnAreaDataId == 0 ||
				spawner.initialSpawnCount == 0 || spawner.maxAliveCount == 0 || spawner.initialSpawnCount > spawner.maxAliveCount ||
				spawner.respawnIntervalMilliseconds == 0)
			{
				outError = "invalid monster spawner data in section: " + sectionName;
				return false;
			}

			const std::uint32_t spawnerDataId = spawner.GetKey();
			if (!loadedSpawners.emplace(spawnerDataId, std::move(spawner)).second)
			{
				outError = "duplicate SpawnerDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedSpawners.empty())
		{
			outError = "monster spawner data table is empty.";
			return false;
		}

		m_spawners = std::move(loadedSpawners);
		outError.clear();
		return true;
	}

	const SMonsterSpawnerData* FMonsterSpawnerDataTable::Find(
		const std::uint32_t spawnerDataId) const noexcept
	{
		const auto it = m_spawners.find(spawnerDataId);
		return it == m_spawners.end() ? nullptr : &it->second;
	}

	std::vector<const SMonsterSpawnerData*> FMonsterSpawnerDataTable::FindByMap(
		const std::uint32_t mapDataId) const
	{
		std::vector<const SMonsterSpawnerData*> spawners;
		for (const auto& [spawnerDataId, spawner] : m_spawners)
		{
			(void)spawnerDataId;
			if (spawner.mapDataId == mapDataId)
			{
				spawners.push_back(&spawner);
			}
		}

		std::ranges::sort(spawners, {}, &SMonsterSpawnerData::spawnerDataId);
		return spawners;
	}

	bool FMonsterSpawnerDataTable::ValidateReferences(
		const GameData::Map::FMapDataTable& maps,
		const GameData::Monster::FMonsterDataTable& monsters,
		const GameData::SpawnArea::FSpawnAreaDataTable& spawnAreas,
		std::string& outError) const
	{
		for (const auto& [spawnerDataId, spawner] : m_spawners)
		{
			(void)spawnerDataId;
			const GameData::Map::SMapData* const map = maps.Find(spawner.mapDataId);
			if (map == nullptr)
			{
				outError = "MonsterSpawner references unknown MapDataId: " + std::to_string(spawner.mapDataId);
				return false;
			}
			const GameData::Monster::SMonsterData* const monster = monsters.Find(spawner.monsterDataId);
			if (monster == nullptr)
			{
				outError = "MonsterSpawner references unknown MonsterDataId: " + std::to_string(spawner.monsterDataId);
				return false;
			}
			if (monster->aggroRadius > static_cast<float>(map->sectorSize))
			{
				outError = "MonsterSpawner references a Monster whose AggroRadius exceeds the Map SectorSize: " +
						   std::to_string(spawner.spawnerDataId);
				return false;
			}

			const GameData::SpawnArea::SSpawnAreaData* spawnArea = spawnAreas.Find(spawner.spawnAreaDataId);
			if (spawnArea == nullptr)
			{
				outError = "MonsterSpawner references unknown SpawnAreaDataId: " + std::to_string(spawner.spawnAreaDataId);
				return false;
			}
			if (spawnArea->mapDataId != spawner.mapDataId)
			{
				outError = "MonsterSpawner and SpawnArea MapDataId differ for SpawnerDataId: " + std::to_string(spawner.spawnerDataId);
				return false;
			}
			const float minimumDiameter = monster->collisionRadius * 2.0f;
			if (spawnArea->maxX - spawnArea->minX <= minimumDiameter || spawnArea->maxY - spawnArea->minY <= minimumDiameter)
			{
				outError =
					"MonsterSpawner SpawnArea is too small for the Monster CollisionRadius: " + std::to_string(spawner.spawnerDataId);
				return false;
			}
		}

		outError.clear();
		return true;
	}

	std::size_t FMonsterSpawnerDataTable::Size() const noexcept
	{
		return m_spawners.size();
	}
}
