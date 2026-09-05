#include "GameDataPch.h"

#include "GameData/Monster/FMonsterDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Monster
{
	using GameData::Common::EMonsterAggroType;
	using GameData::Common::EMonsterType;

	namespace
	{
		constexpr std::array<std::string_view, 13> kKnownKeys = {"MonsterDataId",
			"Name",
			"MonsterType",
			"AggroType",
			"MaxHp",
			"Attack",
			"Defense",
			"MoveSpeed",
			"CollisionRadius",
			"AggroRadius",
			"LeashRadius",
			"AttackRange",
			"AttackCooldownMilliseconds"};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EMonsterType>, 2> kMonsterTypeValues = {
			{{"Normal", EMonsterType::Normal}, {"Boss", EMonsterType::Boss}}};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EMonsterAggroType>, 2> kMonsterAggroTypeValues = {
			{{"Aggressive", EMonsterAggroType::Aggressive}, {"Passive", EMonsterAggroType::Passive}}};
	}

	bool FMonsterDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SMonsterData> loadedMonsters;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SMonsterData monster;
			if (!reader.ReadRequiredUInt32(sectionName, "MonsterDataId", monster.monsterDataId, outError) ||
				!reader.ReadRequiredString(sectionName, "Name", monster.name, outError) ||
				!reader.ReadRequiredEnum(sectionName, "MonsterType", kMonsterTypeValues, monster.monsterType, outError) ||
				!reader.ReadRequiredEnum(sectionName, "AggroType", kMonsterAggroTypeValues, monster.aggroType, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MaxHp", monster.maxHp, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Attack", monster.attack, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Defense", monster.defense, outError) ||
				!reader.ReadRequiredFloat(sectionName, "MoveSpeed", monster.moveSpeed, outError) ||
				!reader.ReadRequiredFloat(sectionName, "CollisionRadius", monster.collisionRadius, outError) ||
				!reader.ReadRequiredFloat(sectionName, "AggroRadius", monster.aggroRadius, outError) ||
				!reader.ReadRequiredFloat(sectionName, "LeashRadius", monster.leashRadius, outError) ||
				!reader.ReadRequiredFloat(sectionName, "AttackRange", monster.attackRange, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "AttackCooldownMilliseconds", monster.attackCooldownMilliseconds, outError))
			{
				return false;
			}

			if (monster.monsterDataId == 0 || monster.name.empty() || monster.name.size() > 100 || monster.maxHp == 0 ||
				monster.attack == 0 || !std::isfinite(monster.moveSpeed) || monster.moveSpeed <= 0.0f ||
				!std::isfinite(monster.collisionRadius) || monster.collisionRadius <= 0.0f || !std::isfinite(monster.aggroRadius) ||
				monster.aggroRadius <= 0.0f || !std::isfinite(monster.leashRadius) || monster.leashRadius <= 0.0f ||
				!std::isfinite(monster.attackRange) || monster.attackRange <= 0.0f || monster.attackRange > monster.aggroRadius ||
				monster.aggroRadius > monster.leashRadius || monster.attackCooldownMilliseconds == 0)
			{
				outError = "invalid monster data in section: " + sectionName;
				return false;
			}

			const std::uint32_t monsterDataId = monster.GetKey();
			if (!loadedMonsters.emplace(monsterDataId, std::move(monster)).second)
			{
				outError = "duplicate MonsterDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedMonsters.empty())
		{
			outError = "monster data table is empty.";
			return false;
		}

		m_monsters = std::move(loadedMonsters);
		outError.clear();
		return true;
	}

	const SMonsterData* FMonsterDataTable::Find(
		const std::uint32_t monsterDataId) const noexcept
	{
		const auto it = m_monsters.find(monsterDataId);
		return it == m_monsters.end() ? nullptr : &it->second;
	}

	std::vector<const SMonsterData*> FMonsterDataTable::GetAll() const
	{
		std::vector<const SMonsterData*> monsters;
		monsters.reserve(m_monsters.size());
		for (const auto& [monsterDataId, monster] : m_monsters)
		{
			(void)monsterDataId;
			monsters.push_back(&monster);
		}

		std::ranges::sort(monsters, {}, &SMonsterData::monsterDataId);
		return monsters;
	}

	std::size_t FMonsterDataTable::Size() const noexcept
	{
		return m_monsters.size();
	}
}
