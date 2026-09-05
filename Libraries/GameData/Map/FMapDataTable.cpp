#include "GameDataPch.h"

#include "GameData/Map/FMapDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Map
{
	using GameData::Common::EMapType;
	using GameData::Common::ESectorExecutionMode;

	namespace
	{
		constexpr std::array<std::string_view, 10> kKnownKeys = {"MapDataId",
			"Name",
			"MapType",
			"WorldWidth",
			"WorldHeight",
			"SectorSize",
			"AoiSectorRadius",
			"SpawnX",
			"SpawnY",
			"SectorExecutionMode"};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EMapType>, 2> kMapTypeValues = {
			{{"Town", EMapType::Town}, {"Dungeon", EMapType::Dungeon}}};
		constexpr std::array<Foundation::Config::SConfigEnumValue<ESectorExecutionMode>, 2> kSectorExecutionModeValues = {
			{{"Serial", ESectorExecutionMode::Serial}, {"TaskGraph", ESectorExecutionMode::TaskGraph}}};
	}

	bool FMapDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SMapData> loadedMaps;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SMapData map;
			if (!reader.ReadRequiredUInt32(sectionName, "MapDataId", map.mapDataId, outError) ||
				!reader.ReadRequiredString(sectionName, "Name", map.name, outError) ||
				!reader.ReadRequiredEnum(sectionName, "MapType", kMapTypeValues, map.mapType, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "WorldWidth", map.worldWidth, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "WorldHeight", map.worldHeight, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "SectorSize", map.sectorSize, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "AoiSectorRadius", map.aoiSectorRadius, outError) ||
				!reader.ReadRequiredFloat(sectionName, "SpawnX", map.spawnX, outError) ||
				!reader.ReadRequiredFloat(sectionName, "SpawnY", map.spawnY, outError) ||
				!reader.ReadRequiredEnum(sectionName, "SectorExecutionMode", kSectorExecutionModeValues, map.sectorExecutionMode, outError))
			{
				return false;
			}

			if (map.mapDataId == 0 || map.name.empty() || map.worldWidth == 0 || map.worldHeight == 0 || map.sectorSize == 0 ||
				map.worldWidth % map.sectorSize != 0 || map.worldHeight % map.sectorSize != 0 || map.aoiSectorRadius == 0 ||
				map.aoiSectorRadius > 4 || map.spawnX < 0.0f || map.spawnX >= static_cast<float>(map.worldWidth) || map.spawnY < 0.0f ||
				map.spawnY >= static_cast<float>(map.worldHeight))
			{
				outError = "invalid map data in section: " + sectionName;
				return false;
			}

			const std::uint32_t mapDataId = map.GetKey();
			if (!loadedMaps.emplace(mapDataId, std::move(map)).second)
			{
				outError = "duplicate MapDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedMaps.empty())
		{
			outError = "map data table is empty.";
			return false;
		}

		m_maps = std::move(loadedMaps);
		outError.clear();
		return true;
	}

	const SMapData* FMapDataTable::Find(
		const std::uint32_t mapDataId) const noexcept
	{
		const auto it = m_maps.find(mapDataId);
		return it == m_maps.end() ? nullptr : &it->second;
	}

	std::vector<const SMapData*> FMapDataTable::GetAll() const
	{
		std::vector<const SMapData*> maps;
		maps.reserve(m_maps.size());
		for (const auto& [mapDataId, map] : m_maps)
		{
			(void)mapDataId;
			maps.push_back(&map);
		}

		std::ranges::sort(maps, {}, &SMapData::mapDataId);
		return maps;
	}

	std::size_t FMapDataTable::Size() const noexcept
	{
		return m_maps.size();
	}
}
