#pragma once

namespace GameData::Character
{
	class FCharacterDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SCharacterData* Find(std::uint32_t characterDataId) const noexcept;
		std::vector<const SCharacterData*> GetAll() const;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SCharacterData> m_characters;
	};
}
