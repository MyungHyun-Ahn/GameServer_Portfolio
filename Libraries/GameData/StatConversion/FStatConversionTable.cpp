#include "GameDataPch.h"

#include "GameData/StatConversion/FStatConversionTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::StatConversion
{
	using GameData::Common::EDerivedStatType;
	using GameData::Common::EPrimaryStatType;

	namespace
	{
		constexpr std::array<std::string_view, 5> kKnownKeys = {"StatConversionDataId",
			"CharacterDataId",
			"SourceStat",
			"TargetStat",
			"ValuePerPointPermille"};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EPrimaryStatType>, 4> kPrimaryStatValues = {
			{{"Str", EPrimaryStatType::Str},
				{"Dex", EPrimaryStatType::Dex},
				{"Int", EPrimaryStatType::Int},
				{"Luk", EPrimaryStatType::Luk}}};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EDerivedStatType>, 5> kDerivedStatValues = {
			{{"Attack", EDerivedStatType::Attack},
				{"Defense", EDerivedStatType::Defense},
				{"MaxHp", EDerivedStatType::MaxHp},
				{"MaxMp", EDerivedStatType::MaxMp},
				{"MoveSpeed", EDerivedStatType::MoveSpeed}}};
	}

	bool FStatConversionTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SStatConversionData> loadedConversions;
		std::unordered_map<std::uint64_t, std::uint32_t> loadedConversionIdsByKey;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SStatConversionData conversion;
			if (!reader.ReadRequiredUInt32(sectionName, "StatConversionDataId", conversion.statConversionDataId, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "CharacterDataId", conversion.characterDataId, outError) ||
				!reader.ReadRequiredEnum(sectionName, "SourceStat", kPrimaryStatValues, conversion.sourceStat, outError) ||
				!reader.ReadRequiredEnum(sectionName, "TargetStat", kDerivedStatValues, conversion.targetStat, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "ValuePerPointPermille", conversion.valuePerPointPermille, outError))
			{
				return false;
			}

			if (conversion.statConversionDataId == 0 || conversion.characterDataId == 0 || conversion.valuePerPointPermille == 0 ||
				conversion.valuePerPointPermille > 1000000)
			{
				outError = "invalid stat conversion data in section: " + sectionName;
				return false;
			}

			const std::uint32_t statConversionDataId = conversion.GetKey();
			const std::uint64_t conversionKey = MakeConversionKey(conversion.characterDataId, conversion.sourceStat, conversion.targetStat);
			if (!loadedConversionIdsByKey.emplace(conversionKey, statConversionDataId).second)
			{
				outError = "duplicate character/source/target stat conversion in section: " + sectionName;
				return false;
			}
			if (!loadedConversions.emplace(statConversionDataId, std::move(conversion)).second)
			{
				outError = "duplicate StatConversionDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedConversions.empty())
		{
			outError = "stat conversion data table is empty.";
			return false;
		}

		m_conversions = std::move(loadedConversions);
		m_conversionIdsByKey = std::move(loadedConversionIdsByKey);
		outError.clear();
		return true;
	}

	const SStatConversionData* FStatConversionTable::Find(
		const std::uint32_t statConversionDataId) const noexcept
	{
		const auto it = m_conversions.find(statConversionDataId);
		return it == m_conversions.end() ? nullptr : &it->second;
	}

	const SStatConversionData* FStatConversionTable::Find(
		const std::uint32_t characterDataId,
		const EPrimaryStatType sourceStat,
		const EDerivedStatType targetStat) const noexcept
	{
		const auto it = m_conversionIdsByKey.find(MakeConversionKey(characterDataId, sourceStat, targetStat));
		return it == m_conversionIdsByKey.end() ? nullptr : Find(it->second);
	}

	std::vector<const SStatConversionData*> FStatConversionTable::FindByCharacter(
		const std::uint32_t characterDataId) const
	{
		std::vector<const SStatConversionData*> conversions;
		for (const auto& [statConversionDataId, conversion] : m_conversions)
		{
			(void)statConversionDataId;
			if (conversion.characterDataId == characterDataId)
			{
				conversions.push_back(&conversion);
			}
		}

		std::ranges::sort(conversions, {}, &SStatConversionData::statConversionDataId);
		return conversions;
	}

	std::size_t FStatConversionTable::Size() const noexcept
	{
		return m_conversions.size();
	}

	std::uint64_t FStatConversionTable::MakeConversionKey(
		const std::uint32_t characterDataId,
		const EPrimaryStatType sourceStat,
		const EDerivedStatType targetStat) noexcept
	{
		return (static_cast<std::uint64_t>(characterDataId) << 16) |
			   (static_cast<std::uint64_t>(static_cast<std::uint8_t>(sourceStat)) << 8) |
			   static_cast<std::uint64_t>(static_cast<std::uint8_t>(targetStat));
	}
}
