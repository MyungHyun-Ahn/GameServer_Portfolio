#pragma once

namespace GameData::Map
{
	class FMapDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SMapData* Find(std::uint32_t mapDataId) const noexcept;
		std::vector<const SMapData*> GetAll() const;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SMapData> m_maps;
	};
}
