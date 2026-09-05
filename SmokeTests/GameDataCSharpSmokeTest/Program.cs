using Generated.GameData.Item;
using Generated.GameData.Map;
using Generated.GameData.Monster;
using Generated.GameData.Character;
using Generated.GameData.Common;
using Generated.GameData.Currency;

internal static class Program
{
    public static int Main()
    {
        try
        {
            string itemDataPath = Path.Combine(AppContext.BaseDirectory, "GameData", "Item.json");
            IReadOnlyDictionary<uint, ItemData> table = ItemDataTable.Load(itemDataPath);

            Require((byte)EItemCategory.Equipment == 1 &&
                    (byte)EItemCategory.Consumable == 2 &&
                    (byte)EItemCategory.Material == 3,
                "The shared ItemCategory numeric contract differs from Enums.xlsx.");
            Require(table.Count == 8, "Generated client Item table row count differs from the fixture.");
            Require(table.TryGetValue(1001, out ItemData? warriorSword),
                "Warrior Sword was not found by ItemDataId.");
            Require(warriorSword is not null && warriorSword.DataId == 1001,
                "The base game-data key contract returned an unexpected key.");
            Require(warriorSword is not null && warriorSword.Name == "Warrior Sword" &&
                    warriorSword.Category == EItemCategory.Equipment &&
                    warriorSword.EquipmentSlot == EEquipmentSlot.Weapon,
                "Warrior Sword client fields differ from generated Item.json.");
            Require(table.TryGetValue(2001, out ItemData? healthPotion) &&
                    healthPotion.Name == "Health Potion" &&
                    healthPotion.Category == EItemCategory.Consumable,
                "Health Potion client fields differ from generated Item.json.");
            Require(table.TryGetValue(3002, out ItemData? magicCloth) &&
                    magicCloth.Name == "Magic Cloth" &&
                    magicCloth.Category == EItemCategory.Material,
                "Magic Cloth client fields differ from generated Item.json.");
            Require(!table.ContainsKey(999999), "An unknown ItemDataId unexpectedly resolved.");

            string currencyDataPath = Path.Combine(AppContext.BaseDirectory, "GameData", "Currency.json");
            IReadOnlyDictionary<uint, CurrencyData> currencies = CurrencyDataTable.Load(currencyDataPath);
            Require(currencies.Count == 1 && currencies.TryGetValue(1, out CurrencyData? gold) && gold.Name == "Gold",
                "Currency client fields differ from generated Currency.json.");

            string mapDataPath = Path.Combine(AppContext.BaseDirectory, "GameData", "Map.json");
            IReadOnlyDictionary<uint, MapData> maps = MapDataTable.Load(mapDataPath);
            Require((byte)EMapType.Town == 1 && (byte)EMapType.Dungeon == 2,
                "The shared MapType numeric contract differs from Enums.xlsx.");
            Require(maps.Count == 2,
                "Generated client Map table did not load both fixture rows.");
            Require(maps.TryGetValue(1, out MapData? serialMap) &&
                    serialMap.Name == "Training Dungeon 1" &&
                    serialMap.MapType == EMapType.Dungeon &&
                    serialMap.WorldWidth == 1024 && serialMap.WorldHeight == 1024 &&
                    serialMap.SectorSize == 128 && serialMap.AoiSectorRadius == 1,
                "MapDataId 1 client fields differ from generated Map.json.");
            Require(maps.TryGetValue(2, out MapData? taskGraphMap) &&
                    taskGraphMap.Name == "Training Dungeon 2" &&
                    taskGraphMap.MapType == EMapType.Dungeon &&
                    taskGraphMap.WorldWidth == 1024 && taskGraphMap.WorldHeight == 1024 &&
                    taskGraphMap.SectorSize == 128 && taskGraphMap.AoiSectorRadius == 1,
                "MapDataId 2 client fields differ from generated Map.json.");

            string monsterDataPath = Path.Combine(AppContext.BaseDirectory, "GameData", "Monster.json");
            IReadOnlyDictionary<uint, MonsterData> monsters = MonsterDataTable.Load(monsterDataPath);
            Require((byte)EMonsterType.Normal == 1 && (byte)EMonsterType.Boss == 2,
                "The shared MonsterType numeric contract differs from Enums.xlsx.");
            Require(monsters.Count == 2 &&
                    monsters.TryGetValue(1001, out MonsterData? trainingSlime) &&
                    trainingSlime.Name == "Training Slime" &&
                    trainingSlime.MonsterType == EMonsterType.Normal &&
                    trainingSlime.SpriteAssetKey.EndsWith("TrainingSlime.png", StringComparison.Ordinal),
                "MonsterDataId 1001 client fields differ from generated Monster.json.");
            string[] monsterServerOnlyFields =
            [
                "MaxHp",
                "Attack",
                "Defense",
                "MoveSpeed",
                "CollisionRadius",
                "AggroType",
                "AggroRadius",
                "LeashRadius",
                "AttackRange",
                "AttackCooldownMilliseconds",
            ];
            Require(monsterServerOnlyFields.All(fieldName => typeof(MonsterData).GetProperty(fieldName) is null),
                "Monster client projection leaked server-only combat fields.");

            string characterDataPath = Path.Combine(AppContext.BaseDirectory, "GameData", "Character.json");
            IReadOnlyDictionary<uint, CharacterData> characters = CharacterDataTable.Load(characterDataPath);
            Require(characters.TryGetValue(1, out CharacterData? character) &&
                    character.Name == "Green Ninja" &&
                    character.SpriteAssetKey.EndsWith("NinjaGreen/SpriteSheet.png", StringComparison.Ordinal),
                "Character client fields differ from generated Character.json.");
            Require(typeof(CharacterData).GetProperty("InitialStr") is null &&
                    typeof(CharacterData).GetProperty("InitialUnspentStatPoints") is null,
                "Character client projection leaked server-only initial stat fields.");

            Console.WriteLine($"[PASS] Generated client Item, Map, Monster, and Character data loaded: {itemDataPath}");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[FAIL] Generated client game-data smoke test failed: {exception}");
            return 1;
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
