#include "GameDataPch.h"

#include "GameData/CharacterLevel/FCharacterLevelDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"
#include "GameData/Character/FCharacterDataTable.h"

namespace GameData::CharacterLevel
{
	namespace
	{
		constexpr std::array<std::string_view, 9> kKnownKeys = {"CharacterLevelDataId",
			"CharacterDataId",
			"Level",
			"RequiredExpToNextLevel",
			"MaxHp",
			"MaxMp",
			"Attack",
			"Defense",
			"StatPointReward"};
	}

	bool FCharacterLevelDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SCharacterLevelData> loadedCharacterLevels;
		std::unordered_map<std::uint64_t, std::uint32_t> loadedCharacterLevelIdsByKey;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SCharacterLevelData characterLevel;
			if (!reader.ReadRequiredUInt32(sectionName, "CharacterLevelDataId", characterLevel.characterLevelDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "CharacterDataId", characterLevel.characterDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Level", characterLevel.level, outError) ||
				!reader.ReadRequiredUInt64(sectionName, "RequiredExpToNextLevel", characterLevel.requiredExpToNextLevel, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MaxHp", characterLevel.maxHp, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MaxMp", characterLevel.maxMp, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Attack", characterLevel.attack, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Defense", characterLevel.defense, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "StatPointReward", characterLevel.statPointReward, outError))
			{
				return false;
			}

			if (characterLevel.characterLevelDataId == 0 || characterLevel.characterDataId == 0 || characterLevel.level == 0 ||
				characterLevel.maxHp == 0 || characterLevel.attack == 0)
			{
				outError = "invalid character level data in section: " + sectionName;
				return false;
			}

			const std::uint32_t characterLevelDataId = characterLevel.GetKey();
			const std::uint64_t characterLevelKey = MakeCharacterLevelKey(characterLevel.characterDataId, characterLevel.level);
			if (!loadedCharacterLevelIdsByKey.emplace(characterLevelKey, characterLevelDataId).second)
			{
				outError = "duplicate CharacterDataId/Level in section: " + sectionName;
				return false;
			}
			if (!loadedCharacterLevels.emplace(characterLevelDataId, std::move(characterLevel)).second)
			{
				outError = "duplicate CharacterLevelDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedCharacterLevels.empty())
		{
			outError = "character level data table is empty.";
			return false;
		}

		std::unordered_map<std::uint32_t, std::vector<const SCharacterLevelData*>> levelsByCharacter;
		for (const auto& [characterLevelDataId, characterLevel] : loadedCharacterLevels)
		{
			(void)characterLevelDataId;
			levelsByCharacter[characterLevel.characterDataId].push_back(&characterLevel);
		}
		for (auto& [characterDataId, levels] : levelsByCharacter)
		{
			std::ranges::sort(levels, {}, &SCharacterLevelData::level);
			for (std::size_t index = 0; index < levels.size(); ++index)
			{
				const SCharacterLevelData& characterLevel = *levels[index];
				const std::uint32_t expectedLevel = static_cast<std::uint32_t>(index) + 1;
				const bool isLastLevel = index + 1 == levels.size();
				if (characterLevel.level != expectedLevel)
				{
					outError = "CharacterDataId " + std::to_string(characterDataId) + " levels must be contiguous from 1.";
					return false;
				}
				if ((!isLastLevel && characterLevel.requiredExpToNextLevel == 0) ||
					(isLastLevel && characterLevel.requiredExpToNextLevel != 0))
				{
					outError = "CharacterDataId " + std::to_string(characterDataId) +
							   " RequiredExpToNextLevel must be positive before the final level and zero at the final level.";
					return false;
				}
			}
		}

		m_characterLevels = std::move(loadedCharacterLevels);
		m_characterLevelIdsByKey = std::move(loadedCharacterLevelIdsByKey);
		outError.clear();
		return true;
	}

	const SCharacterLevelData* FCharacterLevelDataTable::Find(
		const std::uint32_t characterLevelDataId) const noexcept
	{
		const auto it = m_characterLevels.find(characterLevelDataId);
		return it == m_characterLevels.end() ? nullptr : &it->second;
	}

	const SCharacterLevelData* FCharacterLevelDataTable::Find(
		const std::uint32_t characterDataId,
		const std::uint32_t level) const noexcept
	{
		const auto it = m_characterLevelIdsByKey.find(MakeCharacterLevelKey(characterDataId, level));
		return it == m_characterLevelIdsByKey.end() ? nullptr : Find(it->second);
	}

	std::vector<const SCharacterLevelData*> FCharacterLevelDataTable::FindByCharacter(
		const std::uint32_t characterDataId) const
	{
		std::vector<const SCharacterLevelData*> characterLevels;
		for (const auto& [characterLevelDataId, characterLevel] : m_characterLevels)
		{
			(void)characterLevelDataId;
			if (characterLevel.characterDataId == characterDataId)
			{
				characterLevels.push_back(&characterLevel);
			}
		}

		std::ranges::sort(characterLevels, {}, &SCharacterLevelData::level);
		return characterLevels;
	}

	bool FCharacterLevelDataTable::ValidateCharacters(
		const GameData::Character::FCharacterDataTable& characters,
		std::string& outError) const
	{
		for (const auto& [characterLevelDataId, characterLevel] : m_characterLevels)
		{
			(void)characterLevelDataId;
			if (characters.Find(characterLevel.characterDataId) == nullptr)
			{
				outError = "CharacterLevel references unknown CharacterDataId: " + std::to_string(characterLevel.characterDataId);
				return false;
			}
		}

		for (const GameData::Character::SCharacterData* character : characters.GetAll())
		{
			const SCharacterLevelData* initialLevel = Find(character->characterDataId, character->initialLevel);
			if (initialLevel == nullptr)
			{
				outError = "CharacterDataId " + std::to_string(character->characterDataId) + " InitialLevel " +
						   std::to_string(character->initialLevel) + " has no matching CharacterLevel row.";
				return false;
			}
			if (initialLevel->statPointReward != 0)
			{
				outError = "CharacterDataId " + std::to_string(character->characterDataId) + " InitialLevel " +
						   std::to_string(character->initialLevel) + " must have StatPointReward 0.";
				return false;
			}

			std::uint64_t availableStatPoints = character->initialUnspentStatPoints;
			for (const SCharacterLevelData* characterLevel : FindByCharacter(character->characterDataId))
			{
				if (characterLevel->level <= character->initialLevel)
				{
					continue;
				}
				if (availableStatPoints >
					std::numeric_limits<std::uint32_t>::max() - static_cast<std::uint64_t>(characterLevel->statPointReward))
				{
					outError = "CharacterDataId " + std::to_string(character->characterDataId) +
							   " cumulative available stat points exceed uint32.";
					return false;
				}
				availableStatPoints += characterLevel->statPointReward;
			}

			const std::array<std::uint32_t, 4> initialStats = {
				character->initialStr, character->initialDex, character->initialInt, character->initialLuk};
			for (const std::uint32_t initialStat : initialStats)
			{
				if (initialStat > std::numeric_limits<std::uint32_t>::max() - availableStatPoints)
				{
					outError = "CharacterDataId " + std::to_string(character->characterDataId) +
							   " initial stat plus all obtainable points exceed uint32.";
					return false;
				}
			}
		}

		outError.clear();
		return true;
	}

	std::size_t FCharacterLevelDataTable::Size() const noexcept
	{
		return m_characterLevels.size();
	}

	std::uint64_t FCharacterLevelDataTable::MakeCharacterLevelKey(
		const std::uint32_t characterDataId,
		const std::uint32_t level) noexcept
	{
		return (static_cast<std::uint64_t>(characterDataId) << 32) | static_cast<std::uint64_t>(level);
	}
}
