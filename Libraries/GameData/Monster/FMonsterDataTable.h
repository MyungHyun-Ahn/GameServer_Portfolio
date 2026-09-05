#pragma once

namespace GameData::Monster
{
	class FMonsterDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SMonsterData* Find(std::uint32_t monsterDataId) const noexcept;
		std::vector<const SMonsterData*> GetAll() const;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SMonsterData> m_monsters;
	};
}
