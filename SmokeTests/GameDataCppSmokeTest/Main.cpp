#include "GameDataCppSmokeTestPch.h"

#include "GameData/Common/TGameDataRow.h"
#include "Generated/GameData/Cpp/Common/GameDataEnums.g.h"
#include "Generated/GameData/Cpp/AuctionPolicy/AuctionPolicyData.g.h"
#include "Generated/GameData/Cpp/Character/CharacterData.g.h"
#include "Generated/GameData/Cpp/CharacterLevel/CharacterLevelData.g.h"
#include "Generated/GameData/Cpp/CombatFormulaPolicy/CombatFormulaPolicyData.g.h"
#include "Generated/GameData/Cpp/Currency/CurrencyData.g.h"
#include "Generated/GameData/Cpp/InventoryPolicy/InventoryPolicyData.g.h"
#include "Generated/GameData/Cpp/Item/ItemData.g.h"
#include "Generated/GameData/Cpp/MailPolicy/MailPolicyData.g.h"
#include "Generated/GameData/Cpp/MailTemplate/MailTemplateData.g.h"
#include "Generated/GameData/Cpp/Map/MapData.g.h"
#include "Generated/GameData/Cpp/Monster/MonsterData.g.h"
#include "Generated/GameData/Cpp/MonsterSpawner/MonsterSpawnerData.g.h"
#include "Generated/GameData/Cpp/SpawnArea/SpawnAreaData.g.h"
#include "Generated/GameData/Cpp/StatConversion/StatConversionData.g.h"
#include "GameData/Auction/AuctionPolicyTypes.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "GameData/Character/FCharacterDataTable.h"
#include "GameData/CharacterLevel/FCharacterLevelDataTable.h"
#include "GameData/CombatFormulaPolicy/FCombatFormulaPolicyTable.h"
#include "GameData/Currency/FCurrencyDataTable.h"
#include "GameData/InventoryPolicy/FInventoryPolicyTable.h"
#include "GameData/Item/FItemDataTable.h"
#include "GameData/MailPolicy/FMailPolicyTable.h"
#include "GameData/MailTemplate/FMailTemplateTable.h"
#include "GameData/Map/FMapDataTable.h"
#include "GameData/Monster/FMonsterDataTable.h"
#include "GameData/MonsterSpawner/FMonsterSpawnerDataTable.h"
#include "GameData/SpawnArea/FSpawnAreaDataTable.h"
#include "GameData/StatConversion/FStatConversionTable.h"

namespace
{
	bool Require(
		const bool condition,
		const std::string_view message)
	{
		if (condition)
		{
			return true;
		}

		std::cerr << "[FAIL] " << message << '\n';
		return false;
	}
}

int main(
	const int argumentCount,
	char* arguments[])
{
	if (!Require(argumentCount > 0 && arguments[0] != nullptr, "Executable path is unavailable."))
	{
		return 1;
	}

	const std::filesystem::path itemDataPath = argumentCount > 1
												   ? std::filesystem::path(arguments[1])
												   : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "Item.yaml";
	const std::filesystem::path auctionPolicyPath =
		argumentCount > 2 ? std::filesystem::path(arguments[2])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "AuctionPolicy.yaml";
	const std::filesystem::path mapDataPath = argumentCount > 3
												  ? std::filesystem::path(arguments[3])
												  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "Map.yaml";
	const std::filesystem::path combatFormulaPolicyPath =
		argumentCount > 4 ? std::filesystem::path(arguments[4])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "CombatFormulaPolicy.yaml";
	const std::filesystem::path statConversionPath =
		argumentCount > 5 ? std::filesystem::path(arguments[5])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "StatConversion.yaml";
	const std::filesystem::path inventoryPolicyPath =
		argumentCount > 6 ? std::filesystem::path(arguments[6])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "InventoryPolicy.yaml";
	const std::filesystem::path currencyPath = argumentCount > 7
												   ? std::filesystem::path(arguments[7])
												   : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "Currency.yaml";
	const std::filesystem::path mailPolicyPath =
		argumentCount > 8 ? std::filesystem::path(arguments[8])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "MailPolicy.yaml";
	const std::filesystem::path mailTemplatePath =
		argumentCount > 9 ? std::filesystem::path(arguments[9])
						  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "MailTemplate.yaml";
	const std::filesystem::path characterPath = argumentCount > 10
													? std::filesystem::path(arguments[10])
													: std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "Character.yaml";
	const std::filesystem::path characterLevelPath =
		argumentCount > 11 ? std::filesystem::path(arguments[11])
						   : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "CharacterLevel.yaml";
	const std::filesystem::path monsterPath = argumentCount > 12
												  ? std::filesystem::path(arguments[12])
												  : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "Monster.yaml";
	const std::filesystem::path spawnAreaPath = argumentCount > 13
													? std::filesystem::path(arguments[13])
													: std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "SpawnArea.yaml";
	const std::filesystem::path monsterSpawnerPath =
		argumentCount > 14 ? std::filesystem::path(arguments[14])
						   : std::filesystem::absolute(arguments[0]).parent_path() / "GameData" / "MonsterSpawner.yaml";

	GameData::Item::FItemDataTable table;
	std::string error;
	if (!table.Load(itemDataPath, error))
	{
		std::cerr << "[FAIL] Could not load generated Item.yaml: " << error << " path=" << itemDataPath.string() << '\n';
		return 1;
	}

	const GameData::Item::SItemTemplate* warriorSword = table.Find(1001);
	const GameData::Item::SItemTemplate* healthPotion = table.Find(2001);
	const GameData::Item::SItemTemplate* magicCloth = table.Find(3002);

	const bool valid =
		Require(static_cast<std::uint8_t>(GameData::Common::EItemCategory::Equipment) == 1 &&
					static_cast<std::uint8_t>(GameData::Common::EItemCategory::Consumable) == 2 &&
					static_cast<std::uint8_t>(GameData::Common::EItemCategory::Material) == 3,
			"The shared ItemCategory numeric contract differs from Enums.xlsx.") &&
		Require(table.Size() == 8, "Generated server Item table row count differs from the fixture.") &&
		Require(warriorSword != nullptr, "Warrior Sword was not found by ItemDataId.") &&
		Require(warriorSword != nullptr && warriorSword->GetKey() == 1001, "The base game-data key contract returned an unexpected key.") &&
		Require(warriorSword != nullptr && warriorSword->name == "Warrior Sword" &&
					warriorSword->category == GameData::Common::EItemCategory::Equipment &&
					warriorSword->equipmentSlot == GameData::Common::EEquipmentSlot::Weapon && warriorSword->maxStack == 1 &&
					warriorSword->tradable && warriorSword->attack == 12 && warriorSword->str == 12,
			"Warrior Sword server fields differ from generated Item.yaml.") &&
		Require(healthPotion != nullptr && healthPotion->name == "Health Potion" &&
					healthPotion->category == GameData::Common::EItemCategory::Consumable &&
					healthPotion->equipmentSlot == GameData::Common::EEquipmentSlot::None && healthPotion->attack == 0 &&
					healthPotion->maxStack == 99,
			"Health Potion server fields differ from generated Item.yaml.") &&
		Require(magicCloth != nullptr && magicCloth->name == "Magic Cloth" &&
					magicCloth->category == GameData::Common::EItemCategory::Material && magicCloth->maxStack == 999,
			"Magic Cloth server fields differ from generated Item.yaml.") &&
		Require(table.Find(999999) == nullptr, "An unknown ItemDataId unexpectedly resolved.");

	if (!valid)
	{
		return 1;
	}

	GameData::Auction::FAuctionPolicyTable policyTable;
	if (!policyTable.Load(auctionPolicyPath, error))
	{
		std::cerr << "[FAIL] Could not load generated AuctionPolicy.yaml: " << error << " path=" << auctionPolicyPath.string() << '\n';
		return 1;
	}

	const auto& policy = policyTable.Get();
	const bool validPolicy = Require(policy.GetKey() == 1, "The AuctionPolicy base key contract returned an unexpected key.") &&
							 Require(policy.maxActiveListings == 5, "MaxActiveListings differs from generated AuctionPolicy.yaml.") &&
							 Require(policy.searchPageSize == 20, "SearchPageSize differs from generated AuctionPolicy.yaml.") &&
							 Require(policy.minimumListingDurationSeconds == 3600 && policy.maximumListingDurationSeconds == 604800 &&
										 policy.defaultListingDurationSeconds == 86400,
								 "Listing duration policy differs from generated AuctionPolicy.yaml.") &&
							 Require(policy.defaultCurrencyDataId == 1 && policy.minimumBidIncrement == 1 &&
										 policy.minimumListingPrice == 1 && policy.maximumListingPrice == 999999999999ULL,
								 "Auction economy policy differs from generated AuctionPolicy.yaml.");
	if (!validPolicy)
	{
		return 1;
	}

	GameData::CombatFormulaPolicy::FCombatFormulaPolicyTable combatFormulaPolicyTable;
	if (!combatFormulaPolicyTable.Load(combatFormulaPolicyPath, error))
	{
		std::cerr << "[FAIL] Could not load generated CombatFormulaPolicy.yaml: " << error << " path=" << combatFormulaPolicyPath.string()
				  << '\n';
		return 1;
	}
	const auto& combatFormulaPolicy = combatFormulaPolicyTable.Get();
	if (!Require(combatFormulaPolicy.GetKey() == 1 && combatFormulaPolicy.minimumDamage == 1 &&
					 combatFormulaPolicy.playerBasicAttackRange == 64.0f &&
					 combatFormulaPolicy.playerBasicAttackCooldownMilliseconds == 1000 &&
					 combatFormulaPolicy.playerRespawnDelayMilliseconds == 3000,
			"Combat formula policy differs from generated CombatFormulaPolicy.yaml."))
	{
		return 1;
	}

	GameData::StatConversion::FStatConversionTable statConversionTable;
	if (!statConversionTable.Load(statConversionPath, error))
	{
		std::cerr << "[FAIL] Could not load generated StatConversion.yaml: " << error << " path=" << statConversionPath.string() << '\n';
		return 1;
	}
	const auto* strengthToAttack =
		statConversionTable.Find(1, GameData::Common::EPrimaryStatType::Str, GameData::Common::EDerivedStatType::Attack);
	const auto* dexterityToMoveSpeed =
		statConversionTable.Find(1, GameData::Common::EPrimaryStatType::Dex, GameData::Common::EDerivedStatType::MoveSpeed);
	if (!Require(statConversionTable.Size() == 8 && statConversionTable.FindByCharacter(1).size() == 8,
			"Stat conversion row count differs from generated StatConversion.yaml.") ||
		!Require(strengthToAttack != nullptr && strengthToAttack->GetKey() == 1001 && strengthToAttack->valuePerPointPermille == 1000,
			"STR to Attack conversion differs from generated StatConversion.yaml.") ||
		!Require(
			dexterityToMoveSpeed != nullptr && dexterityToMoveSpeed->GetKey() == 1004 && dexterityToMoveSpeed->valuePerPointPermille == 100,
			"DEX to MoveSpeed conversion differs from generated StatConversion.yaml."))
	{
		return 1;
	}

	GameData::InventoryPolicy::FInventoryPolicyTable inventoryPolicyTable;
	if (!inventoryPolicyTable.Load(inventoryPolicyPath, error))
	{
		std::cerr << "[FAIL] Could not load generated InventoryPolicy.yaml: " << error << " path=" << inventoryPolicyPath.string() << '\n';
		return 1;
	}
	const auto& inventoryPolicy = inventoryPolicyTable.Get();
	if (!Require(inventoryPolicy.GetKey() == 1 && inventoryPolicy.maxInventorySlots == 100 && inventoryPolicy.inventoryListPageSize == 20,
			"Inventory policy differs from generated InventoryPolicy.yaml."))
	{
		return 1;
	}

	GameData::Currency::FCurrencyDataTable currencyTable;
	if (!currencyTable.Load(currencyPath, error))
	{
		std::cerr << "[FAIL] Could not load generated Currency.yaml: " << error << " path=" << currencyPath.string() << '\n';
		return 1;
	}
	const auto* gold = currencyTable.Find(1);
	if (!Require(currencyTable.Size() == 1 && gold != nullptr && gold->GetKey() == 1 && gold->name == "Gold" &&
					 gold->maxAmount == 999999999999ULL,
			"Currency data differs from generated Currency.yaml."))
	{
		return 1;
	}

	GameData::MailPolicy::FMailPolicyTable mailPolicyTable;
	if (!mailPolicyTable.Load(mailPolicyPath, error))
	{
		std::cerr << "[FAIL] Could not load generated MailPolicy.yaml: " << error << " path=" << mailPolicyPath.string() << '\n';
		return 1;
	}
	const auto& mailPolicy = mailPolicyTable.Get();
	if (!Require(mailPolicy.GetKey() == 1 && mailPolicy.mailListPageSize == 20 && mailPolicy.expirationSeconds == 2592000,
			"Mail policy differs from generated MailPolicy.yaml."))
	{
		return 1;
	}

	GameData::MailTemplate::FMailTemplateTable mailTemplateTable;
	if (!mailTemplateTable.Load(mailTemplatePath, error))
	{
		std::cerr << "[FAIL] Could not load generated MailTemplate.yaml: " << error << " path=" << mailTemplatePath.string() << '\n';
		return 1;
	}
	const auto* purchaseMail = mailTemplateTable.FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionPurchase);
	const auto* expirationMail = mailTemplateTable.Find(1004);
	if (!Require(mailTemplateTable.Size() == 4 && purchaseMail != nullptr && purchaseMail->GetKey() == 1001 &&
					 purchaseMail->subject == "Auction purchase",
			"Auction purchase mail template differs from generated MailTemplate.yaml.") ||
		!Require(expirationMail != nullptr && expirationMail->purpose == GameData::Common::EMailTemplatePurpose::AuctionExpirationReturn &&
					 expirationMail->mailType == 4,
			"Auction expiration mail template differs from generated MailTemplate.yaml."))
	{
		return 1;
	}

	GameData::Character::FCharacterDataTable characterTable;
	if (!characterTable.Load(characterPath, error))
	{
		std::cerr << "[FAIL] Could not load generated Character.yaml: " << error << " path=" << characterPath.string() << '\n';
		return 1;
	}
	const auto* character = characterTable.Find(1);
	if (!Require(characterTable.Size() == 1 && character != nullptr && character->GetKey() == 1 && character->name == "Green Ninja" &&
					 character->initialLevel == 1 && character->initialStr == 4 && character->initialDex == 4 &&
					 character->initialInt == 4 && character->initialLuk == 4 && character->initialUnspentStatPoints == 0 &&
					 character->moveSpeed == 96.0f && character->collisionRadius == 6.0f,
			"Character data differs from generated Character.yaml."))
	{
		return 1;
	}

	GameData::CharacterLevel::FCharacterLevelDataTable characterLevelTable;
	if (!characterLevelTable.Load(characterLevelPath, error))
	{
		std::cerr << "[FAIL] Could not load generated CharacterLevel.yaml: " << error << " path=" << characterLevelPath.string() << '\n';
		return 1;
	}
	if (!characterLevelTable.ValidateCharacters(characterTable, error))
	{
		std::cerr << "[FAIL] Character and CharacterLevel relation validation failed: " << error << '\n';
		return 1;
	}
	const auto* firstCharacterLevel = characterLevelTable.Find(1, 1);
	const auto* finalCharacterLevel = characterLevelTable.Find(1, 10);
	if (!Require(characterLevelTable.Size() == 10 && characterLevelTable.FindByCharacter(1).size() == 10,
			"CharacterLevel row count differs from generated CharacterLevel.yaml.") ||
		!Require(firstCharacterLevel != nullptr && firstCharacterLevel->GetKey() == 1001 &&
					 firstCharacterLevel->requiredExpToNextLevel == 100 && firstCharacterLevel->maxHp == 100 &&
					 firstCharacterLevel->attack == 10 && firstCharacterLevel->statPointReward == 0,
			"Initial CharacterLevel fields differ from generated CharacterLevel.yaml.") ||
		!Require(finalCharacterLevel != nullptr && finalCharacterLevel->GetKey() == 1010 &&
					 finalCharacterLevel->requiredExpToNextLevel == 0 && finalCharacterLevel->maxHp == 300 &&
					 finalCharacterLevel->statPointReward == 5,
			"Final CharacterLevel fields differ from generated CharacterLevel.yaml.") ||
		!Require(characterLevelTable.Find(1, 11) == nullptr, "An undefined Character level unexpectedly resolved."))
	{
		return 1;
	}

	GameData::Map::FMapDataTable mapTable;
	if (!mapTable.Load(mapDataPath, error))
	{
		std::cerr << "[FAIL] Could not load generated Map.yaml: " << error << " path=" << mapDataPath.string() << '\n';
		return 1;
	}
	const GameData::Map::SMapData* serialMap = mapTable.Find(1);
	const GameData::Map::SMapData* taskGraphMap = mapTable.Find(2);
	if (!Require(static_cast<std::uint8_t>(GameData::Common::EMapType::Town) == 1 &&
					 static_cast<std::uint8_t>(GameData::Common::EMapType::Dungeon) == 2,
			"The shared MapType numeric contract differs from Enums.xlsx.") ||
		!Require(static_cast<std::uint8_t>(GameData::Common::ESectorExecutionMode::Serial) == 1 &&
					 static_cast<std::uint8_t>(GameData::Common::ESectorExecutionMode::TaskGraph) == 2,
			"The server SectorExecutionMode numeric contract differs from Enums.xlsx.") ||
		!Require(mapTable.Size() == 2 && serialMap != nullptr && taskGraphMap != nullptr,
			"Generated server Map table did not load both fixture rows.") ||
		!Require(serialMap != nullptr && serialMap->GetKey() == 1 && serialMap->mapType == GameData::Common::EMapType::Dungeon &&
					 serialMap->sectorExecutionMode == GameData::Common::ESectorExecutionMode::Serial,
			"MapDataId 1 did not preserve the Serial execution contract.") ||
		!Require(taskGraphMap != nullptr && taskGraphMap->GetKey() == 2 && taskGraphMap->mapType == GameData::Common::EMapType::Dungeon &&
					 taskGraphMap->sectorExecutionMode == GameData::Common::ESectorExecutionMode::TaskGraph,
			"MapDataId 2 did not preserve the TaskGraph execution contract."))
	{
		return 1;
	}

	GameData::Monster::FMonsterDataTable monsterTable;
	if (!monsterTable.Load(monsterPath, error))
	{
		std::cerr << "[FAIL] Could not load generated Monster.yaml: " << error << " path=" << monsterPath.string() << '\n';
		return 1;
	}
	const GameData::Monster::SMonsterData* trainingSlime = monsterTable.Find(1001);
	const GameData::Monster::SMonsterData* trainingBoss = monsterTable.Find(1002);

	GameData::SpawnArea::FSpawnAreaDataTable spawnAreaTable;
	if (!spawnAreaTable.Load(spawnAreaPath, error))
	{
		std::cerr << "[FAIL] Could not load generated SpawnArea.yaml: " << error << " path=" << spawnAreaPath.string() << '\n';
		return 1;
	}
	if (!spawnAreaTable.ValidateMaps(mapTable, error))
	{
		std::cerr << "[FAIL] SpawnArea and Map relation validation failed: " << error << '\n';
		return 1;
	}
	const GameData::SpawnArea::SSpawnAreaData* serialSpawnArea = spawnAreaTable.Find(1001);
	const GameData::SpawnArea::SSpawnAreaData* taskGraphSpawnArea = spawnAreaTable.Find(1002);

	GameData::MonsterSpawner::FMonsterSpawnerDataTable monsterSpawnerTable;
	if (!monsterSpawnerTable.Load(monsterSpawnerPath, error))
	{
		std::cerr << "[FAIL] Could not load generated MonsterSpawner.yaml: " << error << " path=" << monsterSpawnerPath.string() << '\n';
		return 1;
	}
	if (!monsterSpawnerTable.ValidateReferences(mapTable, monsterTable, spawnAreaTable, error))
	{
		std::cerr << "[FAIL] MonsterSpawner relation validation failed: " << error << '\n';
		return 1;
	}
	const GameData::MonsterSpawner::SMonsterSpawnerData* serialSpawner = monsterSpawnerTable.Find(1001);
	const GameData::MonsterSpawner::SMonsterSpawnerData* taskGraphSpawner = monsterSpawnerTable.Find(1002);
	if (!Require(static_cast<std::uint8_t>(GameData::Common::EMonsterAggroType::Aggressive) == 1 &&
					 static_cast<std::uint8_t>(GameData::Common::EMonsterAggroType::Passive) == 2,
			"The server MonsterAggroType numeric contract differs from Enums.xlsx.") ||
		!Require(static_cast<std::uint8_t>(GameData::Common::EMonsterType::Normal) == 1 &&
					 static_cast<std::uint8_t>(GameData::Common::EMonsterType::Boss) == 2,
			"The shared MonsterType numeric contract differs from Enums.xlsx.") ||
		!Require(monsterTable.Size() == 2 && monsterTable.GetAll().size() == 2 && trainingSlime != nullptr && trainingBoss != nullptr,
			"Generated server Monster table did not load both fixture rows.") ||
		!Require(trainingSlime != nullptr && trainingSlime->GetKey() == 1001 && trainingSlime->name == "Training Slime" &&
					 trainingSlime->monsterType == GameData::Common::EMonsterType::Normal && trainingSlime->maxHp == 50 &&
					 trainingSlime->aggroType == GameData::Common::EMonsterAggroType::Aggressive && trainingSlime->attack == 5 &&
					 trainingSlime->defense == 1 && trainingSlime->moveSpeed == 64.0f && trainingSlime->collisionRadius == 8.0f &&
					 trainingSlime->aggroRadius == 96.0f && trainingSlime->leashRadius == 192.0f && trainingSlime->attackRange == 24.0f &&
					 trainingSlime->attackCooldownMilliseconds == 1000,
			"Training Slime fields differ from generated Monster.yaml.") ||
		!Require(trainingBoss != nullptr && trainingBoss->GetKey() == 1002 && trainingBoss->name == "Training Boss" &&
					 trainingBoss->monsterType == GameData::Common::EMonsterType::Boss && trainingBoss->maxHp == 500 &&
					 trainingBoss->aggroType == GameData::Common::EMonsterAggroType::Passive && trainingBoss->attack == 25 &&
					 trainingBoss->defense == 10 && trainingBoss->moveSpeed == 48.0f && trainingBoss->collisionRadius == 16.0f &&
					 trainingBoss->aggroRadius == 128.0f && trainingBoss->leashRadius == 256.0f && trainingBoss->attackRange == 32.0f &&
					 trainingBoss->attackCooldownMilliseconds == 1500,
			"Training Boss fields differ from generated Monster.yaml.") ||
		!Require(monsterTable.Find(999999) == nullptr, "An unknown MonsterDataId unexpectedly resolved.") ||
		!Require(spawnAreaTable.Size() == 2 && spawnAreaTable.FindByMap(1).size() == 1 && spawnAreaTable.FindByMap(2).size() == 1 &&
					 serialSpawnArea != nullptr && taskGraphSpawnArea != nullptr,
			"Generated server SpawnArea table did not load both Map fixtures.") ||
		!Require(serialSpawnArea != nullptr && serialSpawnArea->GetKey() == 1001 && serialSpawnArea->mapDataId == 1 &&
					 serialSpawnArea->minX == 128.0f && serialSpawnArea->minY == 128.0f && serialSpawnArea->maxX == 384.0f &&
					 serialSpawnArea->maxY == 384.0f,
			"Serial Map SpawnArea fields differ from generated SpawnArea.yaml.") ||
		!Require(taskGraphSpawnArea != nullptr && taskGraphSpawnArea->GetKey() == 1002 && taskGraphSpawnArea->mapDataId == 2,
			"TaskGraph Map SpawnArea fields differ from generated SpawnArea.yaml.") ||
		!Require(spawnAreaTable.Find(999999) == nullptr, "An unknown SpawnAreaDataId unexpectedly resolved.") ||
		!Require(monsterSpawnerTable.Size() == 2 && monsterSpawnerTable.FindByMap(1).size() == 1 &&
					 monsterSpawnerTable.FindByMap(2).size() == 1 && serialSpawner != nullptr && taskGraphSpawner != nullptr,
			"Generated server MonsterSpawner table did not load both Map fixtures.") ||
		!Require(serialSpawner != nullptr && serialSpawner->GetKey() == 1001 && serialSpawner->mapDataId == 1 &&
					 serialSpawner->monsterDataId == 1001 && serialSpawner->spawnAreaDataId == 1001 &&
					 serialSpawner->initialSpawnCount == 4 && serialSpawner->maxAliveCount == 8 &&
					 serialSpawner->respawnIntervalMilliseconds == 3000,
			"Serial Map MonsterSpawner fields differ from generated MonsterSpawner.yaml.") ||
		!Require(taskGraphSpawner != nullptr && taskGraphSpawner->GetKey() == 1002 && taskGraphSpawner->mapDataId == 2 &&
					 taskGraphSpawner->monsterDataId == 1001 && taskGraphSpawner->spawnAreaDataId == 1002,
			"TaskGraph Map MonsterSpawner fields differ from generated MonsterSpawner.yaml.") ||
		!Require(monsterSpawnerTable.Find(999999) == nullptr, "An unknown SpawnerDataId unexpectedly resolved."))
	{
		return 1;
	}

	std::cout << "[PASS] Typed GameData loaders, including Monster spawn YAML and references, passed. path="
			  << itemDataPath.parent_path().string() << '\n';
	return 0;
}
