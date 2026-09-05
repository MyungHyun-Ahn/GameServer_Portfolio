#include "WorldServerPch.h"

#include "WorldServer/Domain/FPlayerStatCalculator.h"

#include "GameData/Common/TGameDataRow.h"
#include "Generated/GameData/Cpp/Common/GameDataEnums.g.h"
#include "Generated/GameData/Cpp/Character/CharacterData.g.h"
#include "Generated/GameData/Cpp/CharacterLevel/CharacterLevelData.g.h"
#include "Generated/GameData/Cpp/Item/ItemData.g.h"
#include "Generated/GameData/Cpp/StatConversion/StatConversionData.g.h"
#include "GameData/Character/FCharacterDataTable.h"
#include "GameData/CharacterLevel/FCharacterLevelDataTable.h"
#include "GameData/Item/FItemDataTable.h"
#include "GameData/StatConversion/FStatConversionTable.h"

#include <cmath>

namespace WorldServer::Domain
{
	namespace
	{
		bool TryAdd(
			std::uint32_t& target,
			const std::uint32_t value) noexcept
		{
			if (target > std::numeric_limits<std::uint32_t>::max() - value)
			{
				return false;
			}

			target += value;
			return true;
		}

		std::uint32_t ClampToUInt32(
			const std::uint64_t value) noexcept
		{
			return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::uint32_t>::max()));
		}

		std::uint32_t GetPrimaryStatValue(
			const WorldCore::SPlayerRuntimeSnapshot& snapshot,
			const GameData::Common::EPrimaryStatType type) noexcept
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
	}

	FPlayerStatCalculator::FPlayerStatCalculator(
		std::shared_ptr<const GameData::Character::FCharacterDataTable> characterDataTable,
		std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> characterLevelDataTable,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
		std::shared_ptr<const GameData::StatConversion::FStatConversionTable> statConversionTable)
		: m_characterDataTable(std::move(characterDataTable))
		, m_characterLevelDataTable(std::move(characterLevelDataTable))
		, m_itemDataTable(std::move(itemDataTable))
		, m_statConversionTable(std::move(statConversionTable))
	{
		if (m_characterDataTable == nullptr || m_characterLevelDataTable == nullptr || m_itemDataTable == nullptr ||
			m_statConversionTable == nullptr)
		{
			throw std::invalid_argument("World player stat calculator GameData is incomplete.");
		}
	}

	bool FPlayerStatCalculator::Calculate(
		const Cache::Protocol::FPlayerWorldSnapshot& source,
		WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
		std::string& outError) const
	{
		outSnapshot = {};
		outError.clear();

		const auto& progress = source.progress;
		if (progress.characterId == 0 || progress.characterDataId == 0 || progress.level == 0 || progress.progressVersion == 0 ||
			progress.statVersion == 0 || source.equipmentVersion == 0 || source.statRevision == 0)
		{
			outError = "Cache player state contains an invalid identity or revision.";
			return false;
		}

		const GameData::Character::SCharacterData* const character = m_characterDataTable->Find(progress.characterDataId);
		const GameData::CharacterLevel::SCharacterLevelData* const level =
			m_characterLevelDataTable->Find(progress.characterDataId, progress.level);
		if (character == nullptr || level == nullptr)
		{
			outError = "Cache player state references missing Character GameData.";
			return false;
		}

		WorldCore::SPlayerRuntimeSnapshot calculated;
		calculated.characterId = progress.characterId;
		calculated.characterDataId = progress.characterDataId;
		calculated.level = progress.level;
		calculated.exp = progress.exp;
		calculated.requiredExpToNextLevel = level->requiredExpToNextLevel;
		calculated.str = progress.strStat;
		calculated.dex = progress.dexStat;
		calculated.intelligence = progress.intStat;
		calculated.luk = progress.lukStat;
		calculated.unspentStatPoints = progress.unspentStatPoints;
		calculated.progressVersion = progress.progressVersion;
		calculated.statVersion = progress.statVersion;
		calculated.finalStr = progress.strStat;
		calculated.finalDex = progress.dexStat;
		calculated.finalIntelligence = progress.intStat;
		calculated.finalLuk = progress.lukStat;
		calculated.equipmentVersion = source.equipmentVersion;
		calculated.statRevision = source.statRevision;
		calculated.collisionRadius = character->collisionRadius;

		std::uint64_t equippedAttack = 0;
		std::unordered_set<GameData::Common::EEquipmentSlot> occupiedSlots;
		for (const Cache::Protocol::FEquippedItemSnapshot& item : source.equippedItems)
		{
			if (item.itemInstanceId == 0 || item.itemDataId == 0 || item.itemVersion == 0)
			{
				outError = "Cache equipped item state contains an invalid identity or version.";
				return false;
			}

			const GameData::Item::SItemTemplate* const itemTemplate = m_itemDataTable->Find(item.itemDataId);
			if (itemTemplate == nullptr || itemTemplate->category != GameData::Common::EItemCategory::Equipment ||
				itemTemplate->equipmentSlot == GameData::Common::EEquipmentSlot::None)
			{
				outError = "Cache equipped item state references invalid equipment GameData.";
				return false;
			}
			if (!occupiedSlots.emplace(itemTemplate->equipmentSlot).second)
			{
				outError = "Cache equipped item state contains duplicate EquipmentSlot entries.";
				return false;
			}

			if (!TryAdd(calculated.finalStr, itemTemplate->str) || !TryAdd(calculated.finalDex, itemTemplate->dex) ||
				!TryAdd(calculated.finalIntelligence, itemTemplate->intelligence) || !TryAdd(calculated.finalLuk, itemTemplate->luk) ||
				!TryAdd(calculated.finalStr, item.strStat) || !TryAdd(calculated.finalDex, item.dexStat) ||
				!TryAdd(calculated.finalIntelligence, item.intStat) || !TryAdd(calculated.finalLuk, item.lukStat))
			{
				outError = "Equipped item primary stat accumulation overflowed.";
				return false;
			}

			if (equippedAttack > std::numeric_limits<std::uint64_t>::max() - itemTemplate->attack)
			{
				outError = "Equipped item attack accumulation overflowed.";
				return false;
			}
			equippedAttack += itemTemplate->attack;
		}

		std::array<std::uint64_t, 6> conversionNumerators{};
		for (const GameData::StatConversion::SStatConversionData* conversion :
			m_statConversionTable->FindByCharacter(progress.characterDataId))
		{
			const std::size_t targetIndex = static_cast<std::size_t>(conversion->targetStat);
			if (targetIndex >= conversionNumerators.size())
			{
				outError = "StatConversion contains an invalid target stat.";
				return false;
			}

			const std::uint64_t contribution =
				static_cast<std::uint64_t>(GetPrimaryStatValue(calculated, conversion->sourceStat)) * conversion->valuePerPointPermille;
			if (conversionNumerators[targetIndex] > std::numeric_limits<std::uint64_t>::max() - contribution)
			{
				outError = "StatConversion accumulation overflowed.";
				return false;
			}
			conversionNumerators[targetIndex] += contribution;
		}

		const auto converted = [&conversionNumerators](const GameData::Common::EDerivedStatType type)
		{
			return conversionNumerators[static_cast<std::size_t>(type)] / 1000ULL;
		};
		const double scaledMoveSpeed = static_cast<double>(character->moveSpeed) * 1000.0;
		if (!std::isfinite(scaledMoveSpeed) || scaledMoveSpeed > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
		{
			outError = "Character MoveSpeed cannot be represented in milli units.";
			return false;
		}

		const std::uint64_t baseMoveSpeedMilli = static_cast<std::uint64_t>(std::llround(scaledMoveSpeed));
		calculated.attack = ClampToUInt32(
			static_cast<std::uint64_t>(level->attack) + equippedAttack + converted(GameData::Common::EDerivedStatType::Attack));
		calculated.defense =
			ClampToUInt32(static_cast<std::uint64_t>(level->defense) + converted(GameData::Common::EDerivedStatType::Defense));
		calculated.maxHp = ClampToUInt32(static_cast<std::uint64_t>(level->maxHp) + converted(GameData::Common::EDerivedStatType::MaxHp));
		calculated.maxMp = ClampToUInt32(static_cast<std::uint64_t>(level->maxMp) + converted(GameData::Common::EDerivedStatType::MaxMp));
		const std::uint64_t moveSpeedContribution =
			conversionNumerators[static_cast<std::size_t>(GameData::Common::EDerivedStatType::MoveSpeed)];
		calculated.moveSpeedMilli = moveSpeedContribution > std::numeric_limits<std::uint32_t>::max() - baseMoveSpeedMilli
										? std::numeric_limits<std::uint32_t>::max()
										: static_cast<std::uint32_t>(baseMoveSpeedMilli + moveSpeedContribution);

		if (!WorldCore::IsValidPlayerRuntimeSnapshot(calculated, outError))
		{
			return false;
		}

		outSnapshot = calculated;
		return true;
	}

	bool FPlayerStatCalculator::BuildDevelopmentSnapshot(
		const WorldCore::FUserId userId,
		WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
		std::string& outError) const
	{
		outSnapshot = {};
		outError.clear();
		if (userId == WorldCore::kInvalidUserId)
		{
			outError = "Development Player requires a valid UserId.";
			return false;
		}

		const std::vector<const GameData::Character::SCharacterData*> characters = m_characterDataTable->GetAll();
		const auto characterIterator = std::ranges::min_element(characters,
			{},
			[](const GameData::Character::SCharacterData* const character)
			{
				return character != nullptr ? character->characterDataId : std::numeric_limits<std::uint32_t>::max();
			});
		if (characterIterator == characters.end() || *characterIterator == nullptr)
		{
			outError = "Character GameData does not contain a development default.";
			return false;
		}

		const GameData::Character::SCharacterData& character = **characterIterator;
		Cache::Protocol::FPlayerWorldSnapshot source;
		source.progress.characterId = userId;
		source.progress.characterDataId = character.characterDataId;
		source.progress.level = character.initialLevel;
		source.progress.strStat = character.initialStr;
		source.progress.dexStat = character.initialDex;
		source.progress.intStat = character.initialInt;
		source.progress.lukStat = character.initialLuk;
		source.progress.unspentStatPoints = character.initialUnspentStatPoints;
		source.progress.progressVersion = 1;
		source.progress.statVersion = 1;
		source.equipmentVersion = 1;
		source.statRevision = 1;
		return Calculate(source, outSnapshot, outError);
	}
}
