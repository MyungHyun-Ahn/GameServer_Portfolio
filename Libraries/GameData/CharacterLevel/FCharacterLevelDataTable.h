#pragma once

namespace GameData::Character
{
	class FCharacterDataTable;
}

namespace GameData::CharacterLevel
{
	class FCharacterLevelDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SCharacterLevelData* Find(std::uint32_t characterLevelDataId) const noexcept;
		const SCharacterLevelData* Find(std::uint32_t characterDataId, std::uint32_t level) const noexcept;
		std::vector<const SCharacterLevelData*> FindByCharacter(std::uint32_t characterDataId) const;
		bool ValidateCharacters(const GameData::Character::FCharacterDataTable& characters, std::string& outError) const;
		std::size_t Size() const noexcept;

	private:
		static std::uint64_t MakeCharacterLevelKey(std::uint32_t characterDataId, std::uint32_t level) noexcept;

		std::unordered_map<std::uint32_t, SCharacterLevelData> m_characterLevels;
		std::unordered_map<std::uint64_t, std::uint32_t> m_characterLevelIdsByKey;
	};
}
