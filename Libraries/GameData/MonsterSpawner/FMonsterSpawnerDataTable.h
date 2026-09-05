#pragma once

namespace GameData::Map
{
	class FMapDataTable;
}

namespace GameData::Monster
{
	class FMonsterDataTable;
}

namespace GameData::SpawnArea
{
	class FSpawnAreaDataTable;
}

namespace GameData::MonsterSpawner
{
	class FMonsterSpawnerDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SMonsterSpawnerData* Find(std::uint32_t spawnerDataId) const noexcept;
		std::vector<const SMonsterSpawnerData*> FindByMap(std::uint32_t mapDataId) const;
		bool ValidateReferences(const GameData::Map::FMapDataTable& maps,
			const GameData::Monster::FMonsterDataTable& monsters,
			const GameData::SpawnArea::FSpawnAreaDataTable& spawnAreas,
			std::string& outError) const;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SMonsterSpawnerData> m_spawners;
	};
}
