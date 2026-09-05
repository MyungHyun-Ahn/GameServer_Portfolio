#include "GameDataPch.h"

#include "GameData/SpawnArea/FSpawnAreaDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"
#include "GameData/Map/FMapDataTable.h"

namespace GameData::SpawnArea
{
	namespace
	{
		constexpr std::array<std::string_view, 6> kKnownKeys = {"SpawnAreaDataId", "MapDataId", "MinX", "MinY", "MaxX", "MaxY"};
	}

	bool FSpawnAreaDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SSpawnAreaData> loadedSpawnAreas;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SSpawnAreaData spawnArea;
			if (!reader.ReadRequiredUInt32(sectionName, "SpawnAreaDataId", spawnArea.spawnAreaDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MapDataId", spawnArea.mapDataId, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MinX", spawnArea.minX, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MinY", spawnArea.minY, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MaxX", spawnArea.maxX, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MaxY", spawnArea.maxY, outError))
			{
				return false;
			}

			if (spawnArea.spawnAreaDataId == 0 || spawnArea.mapDataId == 0 || !std::isfinite(spawnArea.minX) ||
				!std::isfinite(spawnArea.minY) || !std::isfinite(spawnArea.maxX) || !std::isfinite(spawnArea.maxY) ||
				spawnArea.minX < 0.0f || spawnArea.minY < 0.0f || spawnArea.minX >= spawnArea.maxX || spawnArea.minY >= spawnArea.maxY)
			{
				outError = "invalid spawn area data in section: " + sectionName;
				return false;
			}

			const std::uint32_t spawnAreaDataId = spawnArea.GetKey();
			if (!loadedSpawnAreas.emplace(spawnAreaDataId, std::move(spawnArea)).second)
			{
				outError = "duplicate SpawnAreaDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedSpawnAreas.empty())
		{
			outError = "spawn area data table is empty.";
			return false;
		}

		m_spawnAreas = std::move(loadedSpawnAreas);
		outError.clear();
		return true;
	}

	const SSpawnAreaData* FSpawnAreaDataTable::Find(
		const std::uint32_t spawnAreaDataId) const noexcept
	{
		const auto it = m_spawnAreas.find(spawnAreaDataId);
		return it == m_spawnAreas.end() ? nullptr : &it->second;
	}

	std::vector<const SSpawnAreaData*> FSpawnAreaDataTable::FindByMap(
		const std::uint32_t mapDataId) const
	{
		std::vector<const SSpawnAreaData*> spawnAreas;
		for (const auto& [spawnAreaDataId, spawnArea] : m_spawnAreas)
		{
			(void)spawnAreaDataId;
			if (spawnArea.mapDataId == mapDataId)
			{
				spawnAreas.push_back(&spawnArea);
			}
		}

		std::ranges::sort(spawnAreas, {}, &SSpawnAreaData::spawnAreaDataId);
		return spawnAreas;
	}

	bool FSpawnAreaDataTable::ValidateMaps(
		const GameData::Map::FMapDataTable& maps,
		std::string& outError) const
	{
		for (const auto& [spawnAreaDataId, spawnArea] : m_spawnAreas)
		{
			(void)spawnAreaDataId;
			const GameData::Map::SMapData* map = maps.Find(spawnArea.mapDataId);
			if (map == nullptr)
			{
				outError = "SpawnArea references unknown MapDataId: " + std::to_string(spawnArea.mapDataId);
				return false;
			}
			if (spawnArea.maxX > static_cast<float>(map->worldWidth) || spawnArea.maxY > static_cast<float>(map->worldHeight))
			{
				outError = "SpawnAreaDataId " + std::to_string(spawnArea.spawnAreaDataId) + " exceeds its Map bounds.";
				return false;
			}
		}

		outError.clear();
		return true;
	}

	std::size_t FSpawnAreaDataTable::Size() const noexcept
	{
		return m_spawnAreas.size();
	}
}
