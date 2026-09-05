#include "GameDataPch.h"

#include "GameData/CombatFormulaPolicy/FCombatFormulaPolicyTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::CombatFormulaPolicy
{
	namespace
	{
		constexpr std::string_view kSectionName = "CombatFormulaPolicy1";
		constexpr std::array<std::string_view, 1> kKnownSections = {kSectionName};
		constexpr std::array<std::string_view, 5> kKnownKeys = {"CombatFormulaPolicyId",
			"MinimumDamage",
			"PlayerBasicAttackRange",
			"PlayerBasicAttackCooldownMilliseconds",
			"PlayerRespawnDelayMilliseconds"};
	}

	bool FCombatFormulaPolicyTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);
		SCombatFormulaPolicyData loaded;
		if (!reader.ValidateKnownSections(kKnownSections, outError) || !reader.ValidateKnownKeys(kSectionName, kKnownKeys, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "CombatFormulaPolicyId", loaded.combatFormulaPolicyId, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MinimumDamage", loaded.minimumDamage, outError) ||
			!reader.ReadRequiredFloat(kSectionName, "PlayerBasicAttackRange", loaded.playerBasicAttackRange, outError) ||
			!reader.ReadRequiredUInt32(
				kSectionName, "PlayerBasicAttackCooldownMilliseconds", loaded.playerBasicAttackCooldownMilliseconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "PlayerRespawnDelayMilliseconds", loaded.playerRespawnDelayMilliseconds, outError))
		{
			return false;
		}

		if (loaded.combatFormulaPolicyId != 1 || loaded.minimumDamage == 0 || !std::isfinite(loaded.playerBasicAttackRange) ||
			loaded.playerBasicAttackRange <= 0.0f || loaded.playerBasicAttackCooldownMilliseconds == 0 ||
			loaded.playerRespawnDelayMilliseconds == 0)
		{
			outError = "invalid combat formula policy range.";
			return false;
		}

		m_policy = loaded;
		outError.clear();
		return true;
	}

	const SCombatFormulaPolicyData& FCombatFormulaPolicyTable::Get() const noexcept
	{
		return m_policy;
	}
}
