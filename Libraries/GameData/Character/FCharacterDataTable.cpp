#include "GameDataPch.h"

#include "GameData/Character/FCharacterDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Character
{
	namespace
	{
		constexpr std::array<std::string_view, 10> kKnownKeys = {"CharacterDataId",
			"Name",
			"InitialLevel",
			"InitialStr",
			"InitialDex",
			"InitialInt",
			"InitialLuk",
			"InitialUnspentStatPoints",
			"MoveSpeed",
			"CollisionRadius"};
	}

	bool FCharacterDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SCharacterData> loadedCharacters;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SCharacterData character;
			if (!reader.ReadRequiredUInt32(sectionName, "CharacterDataId", character.characterDataId, outError) ||
				!reader.ReadRequiredString(sectionName, "Name", character.name, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialLevel", character.initialLevel, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialStr", character.initialStr, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialDex", character.initialDex, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialInt", character.initialInt, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialLuk", character.initialLuk, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "InitialUnspentStatPoints", character.initialUnspentStatPoints, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MoveSpeed", character.moveSpeed, outError) ||
				!reader.ReadRequiredFloat(sectionName, "CollisionRadius", character.collisionRadius, outError))
			{
				return false;
			}

			if (character.characterDataId == 0 || character.name.empty() || character.name.size() > 100 || character.initialLevel == 0 ||
				!std::isfinite(character.moveSpeed) || character.moveSpeed <= 0.0f || !std::isfinite(character.collisionRadius) ||
				character.collisionRadius <= 0.0f)
			{
				outError = "invalid character data in section: " + sectionName;
				return false;
			}

			const std::uint32_t characterDataId = character.GetKey();
			if (!loadedCharacters.emplace(characterDataId, std::move(character)).second)
			{
				outError = "duplicate CharacterDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedCharacters.empty())
		{
			outError = "character data table is empty.";
			return false;
		}

		m_characters = std::move(loadedCharacters);
		outError.clear();
		return true;
	}

	const SCharacterData* FCharacterDataTable::Find(
		const std::uint32_t characterDataId) const noexcept
	{
		const auto it = m_characters.find(characterDataId);
		return it == m_characters.end() ? nullptr : &it->second;
	}

	std::vector<const SCharacterData*> FCharacterDataTable::GetAll() const
	{
		std::vector<const SCharacterData*> characters;
		characters.reserve(m_characters.size());
		for (const auto& [characterDataId, character] : m_characters)
		{
			(void)characterDataId;
			characters.push_back(&character);
		}

		std::ranges::sort(characters, {}, &SCharacterData::characterDataId);
		return characters;
	}

	std::size_t FCharacterDataTable::Size() const noexcept
	{
		return m_characters.size();
	}
}
