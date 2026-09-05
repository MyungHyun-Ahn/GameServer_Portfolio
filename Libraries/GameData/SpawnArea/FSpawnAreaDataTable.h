#pragma once

namespace GameData::Map
{
	class FMapDataTable;
}

namespace GameData::SpawnArea
{
	class FSpawnAreaDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SSpawnAreaData* Find(std::uint32_t spawnAreaDataId) const noexcept;
		std::vector<const SSpawnAreaData*> FindByMap(std::uint32_t mapDataId) const;
		bool ValidateMaps(const GameData::Map::FMapDataTable& maps, std::string& outError) const;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SSpawnAreaData> m_spawnAreas;
	};
}
