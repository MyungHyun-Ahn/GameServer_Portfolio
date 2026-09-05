#include "PlayerStatCalculatorSmokeTestPch.h"

namespace
{
	using WorldServer::Domain::FPlayerStatCalculator;

	void Require(
		const bool condition,
		const std::string_view message)
	{
		if (!condition)
		{
			throw std::runtime_error(std::string(message));
		}
	}

	std::uint32_t GetPrimary(
		const WorldCore::SPlayerRuntimeSnapshot& snapshot,
		const GameData::Common::EPrimaryStatType type)
	{
		switch (type)
		{
			case GameData::Common::EPrimaryStatType::Str:
				return snapshot.finalStr;
			case GameData::Common::EPrimaryStatType::Dex:
				return snapshot.finalDex;
			case GameData::Common::EPrimaryStatType::Int:
				return snapshot.finalIntelligence;
			case GameData::Common::EPrimaryStatType::Luk:
				return snapshot.finalLuk;
			default:
				return 0;
		}
	}

	std::uint64_t GetConversionNumerator(
		const GameData::StatConversion::FStatConversionTable& conversions,
		const std::uint32_t characterDataId,
		const WorldCore::SPlayerRuntimeSnapshot& snapshot,
		const GameData::Common::EDerivedStatType target)
	{
		std::uint64_t numerator = 0;
		for (const auto* conversion : conversions.FindByCharacter(characterDataId))
		{
			if (conversion->targetStat == target)
			{
				numerator += static_cast<std::uint64_t>(GetPrimary(snapshot, conversion->sourceStat)) * conversion->valuePerPointPermille;
			}
		}
		return numerator;
	}
}

int main(
	const int argc,
	char* argv[])
{
	try
	{
		const std::filesystem::path dataDirectory =
			argc > 1 ? std::filesystem::absolute(argv[1]) : std::filesystem::absolute(argv[0]).parent_path() / "GameData";
		std::string error;
		auto characters = std::make_shared<GameData::Character::FCharacterDataTable>();
		auto levels = std::make_shared<GameData::CharacterLevel::FCharacterLevelDataTable>();
		auto items = std::make_shared<GameData::Item::FItemDataTable>();
		auto conversions = std::make_shared<GameData::StatConversion::FStatConversionTable>();
		Require(characters->Load(dataDirectory / "Character.yaml", error), error);
		Require(levels->Load(dataDirectory / "CharacterLevel.yaml", error), error);
		Require(levels->ValidateCharacters(*characters, error), error);
		Require(items->Load(dataDirectory / "Item.yaml", error), error);
		Require(conversions->Load(dataDirectory / "StatConversion.yaml", error), error);

		const auto* character = characters->Find(1);
		const auto* level = character == nullptr ? nullptr : levels->Find(character->characterDataId, character->initialLevel);
		const auto* equipment = items->Find(1001);
		Require(character != nullptr && level != nullptr && equipment != nullptr, "smoke fixture GameData is missing.");

		Cache::Protocol::FPlayerWorldSnapshot source;
		source.progress.characterId = 1;
		source.progress.characterDataId = character->characterDataId;
		source.progress.level = character->initialLevel;
		source.progress.exp = 0;
		source.progress.strStat = character->initialStr;
		source.progress.dexStat = character->initialDex;
		source.progress.intStat = character->initialInt;
		source.progress.lukStat = character->initialLuk;
		source.progress.unspentStatPoints = character->initialUnspentStatPoints;
		source.progress.progressVersion = 1;
		source.progress.statVersion = 1;
		source.equipmentVersion = 1;
		source.statRevision = 1;

		const FPlayerStatCalculator calculator(characters, levels, items, conversions);
		WorldCore::SPlayerRuntimeSnapshot baseline;
		Require(calculator.Calculate(source, baseline, error), error);
		Require(baseline.finalStr == source.progress.strStat && baseline.attack >= level->attack,
			"empty equipment state changed the combat projection.");

		Cache::Protocol::FEquippedItemSnapshot equippedItem;
		equippedItem.itemInstanceId = 5001;
		equippedItem.itemDataId = equipment->itemDataId;
		equippedItem.itemVersion = 7;
		equippedItem.strStat = 8;
		equippedItem.dexStat = 3;
		equippedItem.intStat = 1;
		equippedItem.lukStat = 2;
		source.equippedItems.push_back(equippedItem);
		source.equipmentVersion = 2;
		source.statRevision = 2;

		WorldCore::SPlayerRuntimeSnapshot equipped;
		Require(calculator.Calculate(source, equipped, error), error);
		Require(equipped.finalStr == source.progress.strStat + equipment->str + equippedItem.strStat &&
					equipped.finalDex == source.progress.dexStat + equipment->dex + equippedItem.dexStat &&
					equipped.finalIntelligence == source.progress.intStat + equipment->intelligence + equippedItem.intStat &&
					equipped.finalLuk == source.progress.lukStat + equipment->luk + equippedItem.lukStat,
			"equipped Item template and instance primary stats were not added exactly once.");
		Require(equipped.attack ==
					level->attack + equipment->attack +
						GetConversionNumerator(
							*conversions, source.progress.characterDataId, equipped, GameData::Common::EDerivedStatType::Attack) /
							1000,
			"equipped attack projection is incorrect.");
		Require(equipped.maxHp ==
					level->maxHp + GetConversionNumerator(
									   *conversions, source.progress.characterDataId, equipped, GameData::Common::EDerivedStatType::MaxHp) /
									   1000,
			"MaxHP projection is incorrect.");
		Require(equipped.maxMp ==
					level->maxMp + GetConversionNumerator(
									   *conversions, source.progress.characterDataId, equipped, GameData::Common::EDerivedStatType::MaxMp) /
									   1000,
			"MaxMP projection is incorrect.");
		Require(
			equipped.defense ==
				level->defense + GetConversionNumerator(
									 *conversions, source.progress.characterDataId, equipped, GameData::Common::EDerivedStatType::Defense) /
									 1000,
			"defense projection is incorrect.");
		Require(equipped.moveSpeedMilli ==
					static_cast<std::uint32_t>(std::llround(character->moveSpeed * 1000.0)) +
						GetConversionNumerator(
							*conversions, source.progress.characterDataId, equipped, GameData::Common::EDerivedStatType::MoveSpeed),
			"move speed projection is incorrect.");
		Require(equipped.equipmentVersion == source.equipmentVersion && equipped.statRevision == source.statRevision,
			"source revisions were not preserved.");

		Cache::Protocol::FEquippedItemSnapshot duplicate = equippedItem;
		duplicate.itemInstanceId = 5002;
		duplicate.itemVersion = 8;
		source.equippedItems.push_back(duplicate);
		WorldCore::SPlayerRuntimeSnapshot rejected;
		Require(!calculator.Calculate(source, rejected, error), "duplicate EquipmentSlot was accepted.");
		source.equippedItems.pop_back();
		source.progress.strStat = std::numeric_limits<std::uint32_t>::max();
		Require(!calculator.Calculate(source, rejected, error), "overflowing equipment primary stat was accepted.");
		source.progress.strStat = character->initialStr;
		source.equippedItems.front().itemDataId = 999999;
		Require(!calculator.Calculate(source, rejected, error), "missing equipment GameData was accepted.");
		source.equippedItems.front().itemDataId = equipment->itemDataId;
		source.equippedItems.front().itemVersion = 0;
		Require(!calculator.Calculate(source, rejected, error), "invalid equipment item version was accepted.");

		std::cout << "[PASS] PlayerStatCalculator raw Cache snapshot projection, conversion, slot, and overflow validation. path="
				  << dataDirectory.string() << '\n';
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[FAIL] PlayerStatCalculator smoke: " << exception.what() << '\n';
		return 1;
	}
}
