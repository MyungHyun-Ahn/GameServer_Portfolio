using ClosedXML.Excel;

internal static class FGameDataGeneratorSelfTest
{
    public static void Run()
    {
        string testRoot = Path.Combine(Path.GetTempPath(), $"GameDataGeneratorSelfTest-{Guid.NewGuid():N}");
        string validInputRoot = Path.Combine(testRoot, "Valid", "Input");
        string validOutputRoot = Path.Combine(testRoot, "Valid", "Output");
        string invalidInputRoot = Path.Combine(testRoot, "Invalid", "Input");
        string invalidOutputRoot = Path.Combine(testRoot, "Invalid", "Output");
        Directory.CreateDirectory(validInputRoot);
        Directory.CreateDirectory(invalidInputRoot);

        try
        {
            CreateValidWorkbook(Path.Combine(validInputRoot, "Item.xlsx"));
            var validDiagnostics = new FDiagnosticBag();
            IReadOnlyList<FGameDataTable> validTables = FWorkbookParser.ParseDirectory(validInputRoot, validDiagnostics);
            IReadOnlyDictionary<string, FGameDataEnumDefinition> validEnums =
                FGameDataValidator.Validate(validTables, validDiagnostics);
            Assert(!validDiagnostics.HasErrors, "valid workbook produced diagnostics:\n" + string.Join('\n', validDiagnostics.GetSorted()));
            IReadOnlyList<SGeneratedFile> generatedFiles = FGameDataGenerators.Generate(validTables, validEnums);
            FOutputManager.Apply(validOutputRoot, generatedFiles, check: false);
            FOutputManager.Apply(validOutputRoot, generatedFiles, check: true);

            string serverYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "ScopeProbe.yaml"));
            string itemServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "Item.yaml"));
            string auctionPolicyServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "AuctionPolicy.yaml"));
            string mapServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "Map.yaml"));
            string mapClientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "Map.json"));
            string monsterServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "Monster.yaml"));
            string monsterClientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "Monster.json"));
            string spawnAreaServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "SpawnArea.yaml"));
            string monsterSpawnerServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "MonsterSpawner.yaml"));
            string characterServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "Character.yaml"));
            string characterClientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "Character.json"));
            string characterLevelServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "CharacterLevel.yaml"));
            string combatFormulaServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "CombatFormulaPolicy.yaml"));
            string statConversionServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "StatConversion.yaml"));
            string inventoryPolicyServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "InventoryPolicy.yaml"));
            string currencyServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "Currency.yaml"));
            string currencyClientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "Currency.json"));
            string mailPolicyServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "MailPolicy.yaml"));
            string mailTemplateServerYaml = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Server", "MailTemplate.yaml"));
            string clientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "Item.json"));
            string scopeClientJson = File.ReadAllText(Path.Combine(validOutputRoot, "Data", "Client", "ScopeProbe.json"));
            string cppHeader = File.ReadAllText(Path.Combine(validOutputRoot, "Cpp", "Item", "ItemData.g.h"));
            string csharpCode = File.ReadAllText(Path.Combine(validOutputRoot, "CSharp", "Item", "ItemData.g.cs"));
            string characterCppHeader = File.ReadAllText(Path.Combine(validOutputRoot, "Cpp", "Character", "CharacterData.g.h"));
            string characterCsharpCode = File.ReadAllText(Path.Combine(validOutputRoot, "CSharp", "Character", "CharacterData.g.cs"));
            string characterLevelCppHeader = File.ReadAllText(Path.Combine(validOutputRoot, "Cpp", "CharacterLevel", "CharacterLevelData.g.h"));
            string monsterCppHeader = File.ReadAllText(Path.Combine(validOutputRoot, "Cpp", "Monster", "MonsterData.g.h"));
            string monsterCsharpCode = File.ReadAllText(Path.Combine(validOutputRoot, "CSharp", "Monster", "MonsterData.g.cs"));
            string cppEnums = File.ReadAllText(Path.Combine(validOutputRoot, "Cpp", "Common", "GameDataEnums.g.h"));
            string csharpEnums = File.ReadAllText(Path.Combine(validOutputRoot, "CSharp", "Common", "GameDataEnums.g.cs"));
            Assert(serverYaml.Contains("ServerValue", StringComparison.Ordinal), "server projection omitted a Server field");
            Assert(!serverYaml.Contains("ClientValue", StringComparison.Ordinal), "server projection leaked a Client field");
            Assert(scopeClientJson.Contains("ClientValue", StringComparison.Ordinal), "client projection omitted a Client field");
            Assert(!scopeClientJson.Contains("ServerValue", StringComparison.Ordinal), "client projection leaked a Server field");
            Assert(clientJson.Contains("IconKey", StringComparison.Ordinal), "Item client-only extension was not generated");
            Assert(!itemServerYaml.Contains("IconKey", StringComparison.Ordinal), "Item client-only extension leaked into the server projection");
            Assert(auctionPolicyServerYaml.Contains("MaxActiveListings: 5", StringComparison.Ordinal), "AuctionPolicy server projection omitted its policy values");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "AuctionPolicy.json")), "Server-only AuctionPolicy unexpectedly generated client data");
            Assert(File.Exists(Path.Combine(validOutputRoot, "Cpp", "AuctionPolicy", "AuctionPolicyData.g.h")), "AuctionPolicy C++ contract was not generated");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "CSharp", "AuctionPolicy", "AuctionPolicyData.g.cs")), "Server-only AuctionPolicy unexpectedly generated C# code");
            Assert(mapServerYaml.Contains("MapType: \"Dungeon\"", StringComparison.Ordinal), "Map server projection omitted its shared MapType");
            Assert(mapServerYaml.Contains("SpawnX: 512", StringComparison.Ordinal), "Map server projection omitted its spawn point");
            Assert(mapServerYaml.Contains("SectorExecutionMode: \"Serial\"", StringComparison.Ordinal),
                "Map server projection omitted its sector execution mode");
            Assert(!mapServerYaml.Contains("MapAssetKey", StringComparison.Ordinal), "Map server projection leaked a Client field");
            Assert(mapClientJson.Contains("MapAssetKey", StringComparison.Ordinal), "Map client projection omitted its asset key");
            Assert(!mapClientJson.Contains("SpawnX", StringComparison.Ordinal), "Map client projection leaked a Server field");
            Assert(!mapClientJson.Contains("SectorExecutionMode", StringComparison.Ordinal),
                "Map client projection leaked the Server-only sector execution mode");
            Assert(monsterServerYaml.Contains("MonsterType: \"Normal\"", StringComparison.Ordinal) &&
                    monsterServerYaml.Contains("AggroType: \"Aggressive\"", StringComparison.Ordinal) &&
                    monsterServerYaml.Contains("LeashRadius: 192", StringComparison.Ordinal) &&
                    monsterServerYaml.Contains("AttackCooldownMilliseconds: 1000", StringComparison.Ordinal),
                "Monster server projection omitted its type or combat values");
            Assert(!monsterServerYaml.Contains("SpriteAssetKey", StringComparison.Ordinal),
                "Monster server projection leaked a Client field");
            Assert(monsterClientJson.Contains("SpriteAssetKey", StringComparison.Ordinal) &&
                    !monsterClientJson.Contains("MaxHp", StringComparison.Ordinal) &&
                    !monsterClientJson.Contains("AggroType", StringComparison.Ordinal) &&
                    !monsterClientJson.Contains("LeashRadius", StringComparison.Ordinal),
                "Monster client projection is inconsistent");
            Assert(spawnAreaServerYaml.Contains("MapDataId: 1", StringComparison.Ordinal) &&
                    spawnAreaServerYaml.Contains("MaxX: 384", StringComparison.Ordinal),
                "SpawnArea server projection omitted its map or bounds");
            Assert(monsterSpawnerServerYaml.Contains("MonsterDataId: 1001", StringComparison.Ordinal) &&
                    monsterSpawnerServerYaml.Contains("RespawnIntervalMilliseconds: 3000", StringComparison.Ordinal),
                "MonsterSpawner server projection omitted its references or respawn policy");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "SpawnArea.json")) &&
                    !File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "MonsterSpawner.json")),
                "Server-only spawn tables unexpectedly generated client data");
            Assert(monsterCppHeader.Contains("::GameData::Common::EMonsterType", StringComparison.Ordinal) &&
                    monsterCppHeader.Contains("::GameData::Common::EMonsterAggroType", StringComparison.Ordinal) &&
                    monsterCppHeader.Contains("float leashRadius", StringComparison.Ordinal) &&
                    monsterCppHeader.Contains("std::uint32_t attackCooldownMilliseconds", StringComparison.Ordinal),
                "Monster C++ contract omitted its shared enum or server fields");
            Assert(monsterCsharpCode.Contains("EMonsterType MonsterType", StringComparison.Ordinal) &&
                    monsterCsharpCode.Contains("SpriteAssetKey", StringComparison.Ordinal) &&
                    !monsterCsharpCode.Contains("MaxHp", StringComparison.Ordinal) &&
                    !monsterCsharpCode.Contains("MonsterAggroType", StringComparison.Ordinal) &&
                    !monsterCsharpCode.Contains("AggroType", StringComparison.Ordinal) &&
                    !monsterCsharpCode.Contains("LeashRadius", StringComparison.Ordinal),
                "Monster C# contract does not match its client projection");
            Assert(characterServerYaml.Contains("InitialLevel: 1", StringComparison.Ordinal) &&
                    characterServerYaml.Contains("InitialStr: 4", StringComparison.Ordinal) &&
                    characterServerYaml.Contains("InitialUnspentStatPoints: 0", StringComparison.Ordinal) &&
                    characterServerYaml.Contains("MoveSpeed: 96", StringComparison.Ordinal),
                "Character server projection omitted its initial state");
            Assert(!characterServerYaml.Contains("SpriteAssetKey", StringComparison.Ordinal), "Character server projection leaked a Client field");
            Assert(characterClientJson.Contains("SpriteAssetKey", StringComparison.Ordinal), "Character client projection omitted its sprite key");
            Assert(!characterClientJson.Contains("InitialLevel", StringComparison.Ordinal) &&
                    !characterClientJson.Contains("InitialStr", StringComparison.Ordinal) &&
                    !characterClientJson.Contains("InitialUnspentStatPoints", StringComparison.Ordinal),
                "Character client projection leaked a Server field");
            Assert(characterCppHeader.Contains("std::uint32_t initialStr", StringComparison.Ordinal) &&
                    characterCppHeader.Contains("std::uint32_t initialDex", StringComparison.Ordinal) &&
                    characterCppHeader.Contains("std::uint32_t initialInt", StringComparison.Ordinal) &&
                    characterCppHeader.Contains("std::uint32_t initialLuk", StringComparison.Ordinal) &&
                    characterCppHeader.Contains("std::uint32_t initialUnspentStatPoints", StringComparison.Ordinal),
                "Character C++ projection omitted server-only initial stat fields");
            Assert(!characterCsharpCode.Contains("InitialStr", StringComparison.Ordinal) &&
                    !characterCsharpCode.Contains("InitialUnspentStatPoints", StringComparison.Ordinal),
                "Character C# projection leaked server-only initial stat fields");
            Assert(characterLevelServerYaml.Contains("RequiredExpToNextLevel: 100", StringComparison.Ordinal), "CharacterLevel server projection omitted progression data");
            Assert(characterLevelServerYaml.Contains("StatPointReward: 5", StringComparison.Ordinal) &&
                    !characterLevelServerYaml.Contains("BaseStr", StringComparison.Ordinal),
                "CharacterLevel server projection did not use the stat-point reward contract");
            Assert(characterLevelCppHeader.Contains("std::uint32_t statPointReward", StringComparison.Ordinal) &&
                    !characterLevelCppHeader.Contains("baseStr", StringComparison.Ordinal),
                "CharacterLevel C++ projection did not use the stat-point reward contract");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "CharacterLevel.json")), "Server-only CharacterLevel unexpectedly generated client data");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "CSharp", "CharacterLevel", "CharacterLevelData.g.cs")), "Server-only CharacterLevel unexpectedly generated C# code");
            Assert(!clientJson.Contains("MaxStack", StringComparison.Ordinal), "client projection leaked a Server-only Item field");
            Assert(cppHeader.Contains("TGameDataRow<SItemTemplate, std::uint32_t>", StringComparison.Ordinal), "C++ row base contract was not generated");
            Assert(cppHeader.Contains("GetKeyValue()", StringComparison.Ordinal), "C++ key accessor contract was not generated");
            Assert(cppHeader.Contains("std::uint32_t intelligence", StringComparison.Ordinal), "Item.Int was not mapped to C++ intelligence");
            Assert(cppHeader.Contains("std::uint32_t attack", StringComparison.Ordinal) &&
                    cppHeader.Contains("::GameData::Common::EEquipmentSlot", StringComparison.Ordinal),
                "Item combat fields were not generated");
            Assert(cppHeader.Contains("::GameData::Common::EItemCategory", StringComparison.Ordinal), "C++ Item did not reference the shared enum type");
            Assert(!cppHeader.Contains("enum class EItemCategory : std::uint8_t\n\t{", StringComparison.Ordinal),
                "C++ Item duplicated the shared enum definition");
            Assert(csharpCode.Contains("namespace Generated.GameData.Item;", StringComparison.Ordinal), "C# namespace is incorrect");
            Assert(csharpCode.Contains("using Generated.GameData.Common;", StringComparison.Ordinal), "C# Item did not import shared enum types");
            Assert(!csharpCode.Contains("enum EItemCategory", StringComparison.Ordinal), "C# Item duplicated the shared enum definition");
            Assert(cppEnums.Contains("enum class EItemCategory : std::uint8_t", StringComparison.Ordinal) &&
                    cppEnums.Contains("enum class EEquipmentSlot : std::uint8_t", StringComparison.Ordinal) &&
                    cppEnums.Contains("enum class EMapType : std::uint8_t", StringComparison.Ordinal) &&
                    cppEnums.Contains("enum class EMonsterAggroType : std::uint8_t", StringComparison.Ordinal) &&
                    cppEnums.Contains("enum class EMonsterType : std::uint8_t", StringComparison.Ordinal),
                "C++ shared enum definitions were not generated");
            Assert(csharpEnums.Contains("enum EItemCategory : byte", StringComparison.Ordinal) &&
                    csharpEnums.Contains("enum EEquipmentSlot : byte", StringComparison.Ordinal) &&
                    csharpEnums.Contains("enum EMapType : byte", StringComparison.Ordinal) &&
                    csharpEnums.Contains("enum EMonsterType : byte", StringComparison.Ordinal) &&
                    !csharpEnums.Contains("EMonsterAggroType", StringComparison.Ordinal),
                "C# shared enum definitions were not generated");
            Assert(cppEnums.Contains("enum class EServerOnlyState : std::uint16_t", StringComparison.Ordinal) &&
                    cppEnums.Contains("enum class ESectorExecutionMode : std::uint8_t", StringComparison.Ordinal) &&
                    !cppEnums.Contains("EClientOnlyState", StringComparison.Ordinal),
                "C++ enum generation did not honor Server/Client targets");
            Assert(csharpEnums.Contains("enum EClientOnlyState : int", StringComparison.Ordinal) &&
                    !csharpEnums.Contains("ESectorExecutionMode", StringComparison.Ordinal) &&
                    !csharpEnums.Contains("EServerOnlyState", StringComparison.Ordinal),
                "C# enum generation did not honor Client/Server targets");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "Data", "Server", "Enums.yaml")) &&
                    !File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "Enums.json")) &&
                    !File.Exists(Path.Combine(validOutputRoot, "Cpp", "Enums", "EnumsData.g.h")) &&
                    !File.Exists(Path.Combine(validOutputRoot, "CSharp", "Enums", "EnumsData.g.cs")),
                "Enums metadata table unexpectedly generated runtime row data");
            Assert(csharpCode.Contains("ItemData : GameDataRow<uint>", StringComparison.Ordinal), "C# row base contract was not generated");
            Assert(csharpCode.Contains("static class ItemDataTable", StringComparison.Ordinal), "C# typed table facade was not generated");
            Assert(File.Exists(Path.Combine(validOutputRoot, "CSharp", "GeneratedGameData.csproj")), "generated C# project was not preserved by atomic publishing");
            Assert(combatFormulaServerYaml.Contains("MinimumDamage: 1", StringComparison.Ordinal), "CombatFormulaPolicy values were not generated");
            Assert(statConversionServerYaml.Contains("ValuePerPointPermille: 5000", StringComparison.Ordinal), "StatConversion values were not generated");
            Assert(inventoryPolicyServerYaml.Contains("MaxInventorySlots: 100", StringComparison.Ordinal), "InventoryPolicy values were not generated");
            Assert(currencyServerYaml.Contains("MaxAmount: 999999999999", StringComparison.Ordinal) && currencyClientJson.Contains("Gold", StringComparison.Ordinal),
                "Currency server/client projections are inconsistent");
            Assert(mailPolicyServerYaml.Contains("ExpirationSeconds: 2592000", StringComparison.Ordinal), "MailPolicy values were not generated");
            Assert(mailTemplateServerYaml.Contains("Purpose: \"AuctionPurchase\"", StringComparison.Ordinal), "MailTemplate values were not generated");
            Assert(!File.Exists(Path.Combine(validOutputRoot, "Data", "Client", "MailTemplate.json")), "Server-only MailTemplate unexpectedly generated client data");

            CreateInvalidWorkbook(Path.Combine(invalidInputRoot, "BrokenItem.xlsx"));
            CreateRuleFailuresWorkbook(Path.Combine(invalidInputRoot, "RuleFailures.xlsx"));
            CreateItemCasingWorkbook(Path.Combine(invalidInputRoot, "ItemCasing.xlsx"));
            CreateInvalidAuctionPolicyWorkbook(Path.Combine(invalidInputRoot, "BrokenAuctionPolicy.xlsx"));
            CreateInvalidMapWorkbook(Path.Combine(invalidInputRoot, "BrokenMap.xlsx"));
            CreateInvalidCurrencyWorkbook(Path.Combine(invalidInputRoot, "BrokenCurrency.xlsx"));
            CreateInvalidEconomyPolicyWorkbook(Path.Combine(invalidInputRoot, "BrokenEconomyPolicy.xlsx"));
            CreateIncompleteMailTemplateWorkbook(Path.Combine(invalidInputRoot, "IncompleteMailTemplate.xlsx"));
            Directory.CreateDirectory(invalidOutputRoot);
            string sentinelPath = Path.Combine(invalidOutputRoot, "sentinel.txt");
            File.WriteAllText(sentinelPath, "unchanged");
            var invalidDiagnostics = new FDiagnosticBag();
            IReadOnlyList<FGameDataTable> invalidTables = FWorkbookParser.ParseDirectory(invalidInputRoot, invalidDiagnostics);
            FGameDataValidator.Validate(invalidTables, invalidDiagnostics);
            string[] diagnosticCodes = invalidDiagnostics.GetSorted().Select(diagnostic => diagnostic.Code).ToArray();
            Assert(diagnosticCodes.Contains("GD0101", StringComparer.Ordinal), "formula rejection was not diagnosed");
            Assert(diagnosticCodes.Contains("GD0120", StringComparer.Ordinal), "numeric #Target was not rejected");
            Assert(diagnosticCodes.Contains("GD0121", StringComparer.Ordinal), "numeric #Scope was not rejected");
            Assert(diagnosticCodes.Contains("GD0911", StringComparer.Ordinal), "undefined enum default/value was not diagnosed");
            Assert(diagnosticCodes.Contains("GD0323", StringComparer.Ordinal), "default range violation was not diagnosed");
            Assert(diagnosticCodes.Contains("GD0324", StringComparer.Ordinal), "optional field without default was not rejected");
            Assert(diagnosticCodes.Contains("GD0325", StringComparer.Ordinal), "invalid/reserved generated C++ identifier was not rejected");
            Assert(diagnosticCodes.Contains("GD0326", StringComparer.Ordinal), "empty generated C# identifier was not rejected");
            Assert(diagnosticCodes.Contains("GD0327", StringComparer.Ordinal), "C# DataId collision was not rejected");
            Assert(diagnosticCodes.Contains("GD0907", StringComparer.Ordinal), "enum underlying-type overflow was not rejected");
            Assert(diagnosticCodes.Contains("GD0903", StringComparer.Ordinal), "case-insensitive duplicate enum name was not rejected");
            Assert(diagnosticCodes.Contains("GD0904", StringComparer.Ordinal), "invalid enum target was not rejected");
            Assert(diagnosticCodes.Contains("GD0905", StringComparer.Ordinal), "invalid enum underlying type was not rejected");
            Assert(diagnosticCodes.Contains("GD0906", StringComparer.Ordinal), "malformed or duplicate enum values were not rejected");
            Assert(diagnosticCodes.Contains("GD0908", StringComparer.Ordinal), "undefined enum reference was not rejected");
            Assert(diagnosticCodes.Contains("GD0909", StringComparer.Ordinal), "table-local enum #Allowed redefinition was not rejected");
            Assert(diagnosticCodes.Contains("GD0910", StringComparer.Ordinal), "enum target and field scope mismatch was not rejected");
            Assert(diagnosticCodes.Contains("GD0330", StringComparer.Ordinal), "duplicate primary key was not diagnosed");
            Assert(diagnosticCodes.Contains("GD0405", StringComparer.Ordinal), "Item domain rule was not diagnosed");
            Assert(diagnosticCodes.Contains("GD0408", StringComparer.Ordinal), "incorrect Item table casing was not rejected");
            Assert(diagnosticCodes.Contains("GD0409", StringComparer.Ordinal), "extra Item field was not rejected");
            Assert(diagnosticCodes.Contains("GD0506", StringComparer.Ordinal), "multiple AuctionPolicy rows were not rejected");
            Assert(diagnosticCodes.Contains("GD0507", StringComparer.Ordinal), "invalid AuctionPolicyId was not rejected");
            Assert(diagnosticCodes.Contains("GD0508", StringComparer.Ordinal), "invalid AuctionPolicy duration relationship was not rejected");
            Assert(diagnosticCodes.Contains("GD0605", StringComparer.Ordinal), "invalid Map sector geometry was not rejected");
            Assert(diagnosticCodes.Contains("GD0606", StringComparer.Ordinal), "out-of-bounds Map SpawnX was not rejected");
            Assert(diagnosticCodes.Contains("GD0607", StringComparer.Ordinal), "out-of-bounds Map SpawnY was not rejected");
            Assert(diagnosticCodes.Contains("GD0705", StringComparer.Ordinal), "non-positive Character MoveSpeed was not rejected");
            Assert(diagnosticCodes.Contains("GD0706", StringComparer.Ordinal), "non-positive Character CollisionRadius was not rejected");
            Assert(diagnosticCodes.Contains("GD0707", StringComparer.Ordinal), "missing Character InitialLevel row was not rejected");
            Assert(diagnosticCodes.Contains("GD0805", StringComparer.Ordinal), "duplicate CharacterLevel pair was not rejected");
            Assert(diagnosticCodes.Contains("GD0806", StringComparer.Ordinal), "non-contiguous CharacterLevel sequence was not rejected");
            Assert(diagnosticCodes.Contains("GD0807", StringComparer.Ordinal), "invalid CharacterLevel experience boundary was not rejected");
            Assert(diagnosticCodes.Contains("GD0808", StringComparer.Ordinal), "non-zero initial-level StatPointReward was not rejected");
            Assert(diagnosticCodes.Contains("GD0809", StringComparer.Ordinal), "cumulative stat-point uint32 overflow was not rejected");
            Assert(diagnosticCodes.Contains("GD0810", StringComparer.Ordinal), "allocatable primary-stat uint32 overflow was not rejected");
            Assert(diagnosticCodes.Contains("GD1106", StringComparer.Ordinal), "invalid InventoryPolicy page size was not rejected");
            Assert(diagnosticCodes.Contains("GD1205", StringComparer.Ordinal), "out-of-range CurrencyDataId was not rejected");
            Assert(diagnosticCodes.Contains("GD1206", StringComparer.Ordinal), "case-insensitive duplicate Currency name was not rejected");
            Assert(diagnosticCodes.Contains("GD1306", StringComparer.Ordinal), "invalid MailPolicy page size was not rejected");
            Assert(diagnosticCodes.Contains("GD1407", StringComparer.Ordinal), "out-of-range MailTemplate values were not rejected");
            Assert(diagnosticCodes.Contains("GD1408", StringComparer.Ordinal), "incomplete MailTemplate Purpose coverage was not rejected");
            Assert(diagnosticCodes.Contains("GD1605", StringComparer.Ordinal), "invalid MonsterType contract was not rejected");
            Assert(diagnosticCodes.Contains("GD1607", StringComparer.Ordinal), "Monster AttackRange above AggroRadius was not rejected");
            Assert(diagnosticCodes.Contains("GD1608", StringComparer.Ordinal), "Monster AggroRadius above LeashRadius was not rejected");
            Assert(diagnosticCodes.Contains("GD1705", StringComparer.Ordinal), "invalid SpawnArea rectangle was not rejected");
            Assert(diagnosticCodes.Contains("GD1706", StringComparer.Ordinal), "out-of-bounds SpawnArea was not rejected");
            Assert(diagnosticCodes.Contains("GD1707", StringComparer.Ordinal), "missing SpawnArea Map reference contract was not rejected");
            Assert(diagnosticCodes.Contains("GD1805", StringComparer.Ordinal), "invalid MonsterSpawner counts were not rejected");
            Assert(diagnosticCodes.Contains("GD1806", StringComparer.Ordinal), "MonsterSpawner and SpawnArea map mismatch was not rejected");
            Assert(diagnosticCodes.Count(code => string.Equals(code, "GD1807", StringComparison.Ordinal)) == 3,
                "all MonsterSpawner reference contracts were not rejected");
            Assert(diagnosticCodes.Contains("GD1808", StringComparer.Ordinal),
                "Monster AggroRadius outside the Map Sector dependency range was not rejected");
            Assert(diagnosticCodes.Contains("GD1809", StringComparer.Ordinal),
                "SpawnArea too small for the Monster CollisionRadius was not rejected");
            Assert(File.ReadAllText(sentinelPath) == "unchanged", "invalid input modified the existing output");
        }
        finally
        {
            if (Directory.Exists(testRoot))
            {
                Directory.Delete(testRoot, recursive: true);
            }
        }
    }

    private static void CreateValidWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        CreateEnumDefinitionsSheet(
            workbook,
            [
                ["ItemCategory", "Shared", "uint8", "Equipment=1|Consumable=2|Material=3"],
                ["EquipmentSlot", "Shared", "uint8", "None=0|Weapon=1|Armor=2|Accessory=3"],
                ["MapType", "Shared", "uint8", "Town=1|Dungeon=2"],
                ["MonsterType", "Shared", "uint8", "Normal=1|Boss=2"],
                ["MonsterAggroType", "Server", "uint8", "Aggressive=1|Passive=2"],
                ["SectorExecutionMode", "Server", "uint8", "Serial=1|TaskGraph=2"],
                ["PrimaryStatType", "Server", "uint8", "Str=1|Dex=2|Int=3|Luk=4"],
                ["DerivedStatType", "Server", "uint8", "Attack=1|Defense=2|MaxHp=3|MaxMp=4|MoveSpeed=5"],
                ["MailTemplatePurpose", "Server", "uint8", "AuctionPurchase=1|AuctionSaleProceeds=2|AuctionCancellationReturn=3|AuctionExpirationReturn=4"],
                ["ServerOnlyState", "Server", "uint16", "Idle=0|Busy=1"],
                ["ClientOnlyState", "Client", "int32", "Hidden=-1|Visible=1"],
            ]);
        IXLWorksheet sheet = workbook.AddWorksheet("Item");
        WriteMetadataLabels(sheet, "Item", "Shared");
        string[] fields = ["ItemDataId", "Name", "Category", "EquipmentSlot", "MaxStack", "Tradable", "Attack", "Str", "Dex", "Int", "Luk", "IconKey"];
        string[] scopes = ["Shared", "Shared", "Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Client"];
        string[] types = ["uint32", "string", "enum<ItemCategory>", "enum<EquipmentSlot>", "uint32", "bool", "uint32", "uint32", "uint32", "uint32", "uint32", "string"];
        for (int index = 0; index < fields.Length; ++index)
        {
            int column = index + 2;
            sheet.Cell(4, column).Value = index == 0 ? "Primary" : string.Empty;
            sheet.Cell(5, column).Value = scopes[index];
            sheet.Cell(6, column).Value = types[index];
            sheet.Cell(7, column).Value = "true";
            sheet.Cell(13, column).Value = fields[index];
        }
        sheet.Cell(8, 2).Value = 1;
        sheet.Cell(9, 3).Value = 100;
        WriteDataRow(sheet, 14, [1001u, "Warrior Sword", "Equipment", "Weapon", 1u, true, 12u, 10u, 2u, 0u, 0u, "sword_icon"]);
        WriteDataRow(sheet, 15, [2001u, "Health Potion", "Consumable", "None", 99u, true, 0u, 0u, 0u, 0u, 0u, "potion_icon"]);

        IXLWorksheet scopeSheet = workbook.AddWorksheet("ScopeProbe");
        WriteMetadataLabels(scopeSheet, "ScopeProbe", "Shared");
        ConfigureFields(
            scopeSheet,
            ["ProbeId", "SharedValue", "ServerValue", "ClientValue"],
            ["Shared", "Shared", "Server", "Client"],
            ["uint32", "string", "int32", "string"]);
        WriteDataRow(scopeSheet, 14, [1u, "shared", 7, "client"]);

        IXLWorksheet auctionPolicySheet = workbook.AddWorksheet("AuctionPolicy");
        WriteMetadataLabels(auctionPolicySheet, "AuctionPolicy", "Server");
        ConfigureFields(
            auctionPolicySheet,
            ["AuctionPolicyId", "MaxActiveListings", "SearchPageSize", "MinimumListingDurationSeconds", "MaximumListingDurationSeconds", "DefaultListingDurationSeconds", "DefaultCurrencyDataId", "MinimumBidIncrement", "MinimumListingPrice", "MaximumListingPrice"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint64", "uint64", "uint64"]);
        auctionPolicySheet.Cell(12, 8).Value = "Currency.CurrencyDataId";
        WriteDataRow(auctionPolicySheet, 14, [1u, 5u, 20u, 3600u, 604800u, 86400u, 1u, 1ul, 1ul, 999999999999ul]);

        IXLWorksheet mapSheet = workbook.AddWorksheet("Map");
        WriteMetadataLabels(mapSheet, "Map", "Shared");
        ConfigureFields(
            mapSheet,
            ["MapDataId", "Name", "MapType", "WorldWidth", "WorldHeight", "SectorSize", "AoiSectorRadius", "SpawnX", "SpawnY", "SectorExecutionMode", "MapAssetKey"],
            ["Shared", "Shared", "Shared", "Shared", "Shared", "Shared", "Shared", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "enum<MapType>", "uint32", "uint32", "uint32", "uint32", "float", "float", "enum<SectorExecutionMode>", "string"]);
        mapSheet.Cell(8, 2).Value = 1;
        mapSheet.Cell(8, 3).Value = 1;
        mapSheet.Cell(9, 3).Value = 100;
        mapSheet.Cell(8, 5).Value = 1;
        mapSheet.Cell(8, 6).Value = 1;
        mapSheet.Cell(8, 7).Value = 1;
        mapSheet.Cell(8, 8).Value = 1;
        mapSheet.Cell(9, 8).Value = 4;
        mapSheet.Cell(8, 9).Value = 0;
        mapSheet.Cell(8, 10).Value = 0;
        mapSheet.Cell(8, 12).Value = 1;
        mapSheet.Cell(9, 12).Value = 200;
        WriteDataRow(mapSheet, 14, [1u, "Training Dungeon", "Dungeon", 1024u, 1024u, 128u, 1u, 512.0f, 512.0f, "Serial", "Backgrounds/Tilesets/TilesetDungeon.png"]);

        IXLWorksheet monsterSheet = workbook.AddWorksheet("Monster");
        WriteMetadataLabels(monsterSheet, "Monster", "Shared");
        ConfigureFields(
            monsterSheet,
            ["MonsterDataId", "Name", "MonsterType", "AggroType", "MaxHp", "Attack", "Defense", "MoveSpeed", "CollisionRadius", "AggroRadius", "LeashRadius", "AttackRange", "AttackCooldownMilliseconds", "SpriteAssetKey"],
            ["Shared", "Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "enum<MonsterType>", "enum<MonsterAggroType>", "uint32", "uint32", "uint32", "float", "float", "float", "float", "float", "uint32", "string"]);
        WriteDataRow(monsterSheet, 14, [1001u, "Training Slime", "Normal", "Aggressive", 50u, 5u, 1u, 64.0f, 8.0f, 96.0f, 192.0f, 24.0f, 1000u, "Actor/Monsters/TrainingSlime.png"]);
        WriteDataRow(monsterSheet, 15, [1002u, "Training Boss", "Boss", "Passive", 500u, 25u, 10u, 48.0f, 16.0f, 128.0f, 256.0f, 32.0f, 1500u, "Actor/Monsters/TrainingBoss.png"]);

        IXLWorksheet spawnAreaSheet = workbook.AddWorksheet("SpawnArea");
        WriteMetadataLabels(spawnAreaSheet, "SpawnArea", "Server");
        ConfigureFields(
            spawnAreaSheet,
            ["SpawnAreaDataId", "MapDataId", "MinX", "MinY", "MaxX", "MaxY"],
            ["Shared", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "float", "float", "float", "float"]);
        spawnAreaSheet.Cell(12, 3).Value = "Map.MapDataId";
        WriteDataRow(spawnAreaSheet, 14, [1001u, 1u, 128.0f, 128.0f, 384.0f, 384.0f]);

        IXLWorksheet monsterSpawnerSheet = workbook.AddWorksheet("MonsterSpawner");
        WriteMetadataLabels(monsterSpawnerSheet, "MonsterSpawner", "Server");
        ConfigureFields(
            monsterSpawnerSheet,
            ["SpawnerDataId", "MapDataId", "MonsterDataId", "SpawnAreaDataId", "InitialSpawnCount", "MaxAliveCount", "RespawnIntervalMilliseconds"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32"]);
        monsterSpawnerSheet.Cell(12, 3).Value = "Map.MapDataId";
        monsterSpawnerSheet.Cell(12, 4).Value = "Monster.MonsterDataId";
        monsterSpawnerSheet.Cell(12, 5).Value = "SpawnArea.SpawnAreaDataId";
        WriteDataRow(monsterSpawnerSheet, 14, [1001u, 1u, 1001u, 1001u, 4u, 8u, 3000u]);

        IXLWorksheet characterSheet = workbook.AddWorksheet("Character");
        WriteMetadataLabels(characterSheet, "Character", "Shared");
        ConfigureFields(
            characterSheet,
            ["CharacterDataId", "Name", "InitialLevel", "InitialStr", "InitialDex", "InitialInt", "InitialLuk", "InitialUnspentStatPoints", "MoveSpeed", "CollisionRadius", "SpriteAssetKey"],
            ["Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "float", "float", "string"]);
        characterSheet.Cell(8, 2).Value = 1;
        characterSheet.Cell(8, 3).Value = 1;
        characterSheet.Cell(9, 3).Value = 100;
        characterSheet.Cell(8, 4).Value = 1;
        for (int column = 5; column <= 9; ++column)
        {
            characterSheet.Cell(8, column).Value = 0;
        }
        characterSheet.Cell(8, 10).Value = 0.01;
        characterSheet.Cell(8, 11).Value = 0.01;
        characterSheet.Cell(8, 12).Value = 1;
        characterSheet.Cell(9, 12).Value = 200;
        WriteDataRow(characterSheet, 14, [1u, "Green Ninja", 1u, 4u, 4u, 4u, 4u, 0u, 96.0f, 6.0f, "Actor/CharacterAnimated/NinjaGreen/SpriteSheet.png"]);

        IXLWorksheet characterLevelSheet = workbook.AddWorksheet("CharacterLevel");
        WriteMetadataLabels(characterLevelSheet, "CharacterLevel", "Server");
        ConfigureFields(
            characterLevelSheet,
            ["CharacterLevelDataId", "CharacterDataId", "Level", "RequiredExpToNextLevel", "MaxHp", "MaxMp", "Attack", "Defense", "StatPointReward"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint64", "uint32", "uint32", "uint32", "uint32", "uint32"]);
        characterLevelSheet.Cell(8, 2).Value = 1;
        characterLevelSheet.Cell(8, 3).Value = 1;
        characterLevelSheet.Cell(8, 4).Value = 1;
        characterLevelSheet.Cell(8, 5).Value = 0;
        characterLevelSheet.Cell(8, 6).Value = 1;
        characterLevelSheet.Cell(8, 7).Value = 0;
        characterLevelSheet.Cell(8, 8).Value = 1;
        characterLevelSheet.Cell(8, 9).Value = 0;
        characterLevelSheet.Cell(8, 10).Value = 0;
        characterLevelSheet.Cell(12, 3).Value = "Character.CharacterDataId";
        WriteDataRow(characterLevelSheet, 14, [1001u, 1u, 1u, 100ul, 100u, 50u, 10u, 5u, 0u]);
        WriteDataRow(characterLevelSheet, 15, [1002u, 1u, 2u, 150ul, 120u, 55u, 12u, 6u, 5u]);
        WriteDataRow(characterLevelSheet, 16, [1003u, 1u, 3u, 0ul, 140u, 60u, 14u, 7u, 5u]);

        IXLWorksheet currencySheet = workbook.AddWorksheet("Currency");
        WriteMetadataLabels(currencySheet, "Currency", "Shared");
        ConfigureFields(currencySheet,
            ["CurrencyDataId", "Name", "MaxAmount"],
            ["Shared", "Shared", "Server"],
            ["uint32", "string", "uint64"]);
        currencySheet.Cell(8, 2).Value = 1;
        currencySheet.Cell(8, 3).Value = 1;
        currencySheet.Cell(9, 3).Value = 50;
        currencySheet.Cell(8, 4).Value = 1;
        WriteDataRow(currencySheet, 14, [1u, "Gold", 999999999999ul]);

        IXLWorksheet inventoryPolicySheet = workbook.AddWorksheet("InventoryPolicy");
        WriteMetadataLabels(inventoryPolicySheet, "InventoryPolicy", "Server");
        ConfigureFields(inventoryPolicySheet,
            ["InventoryPolicyId", "MaxInventorySlots", "InventoryListPageSize"],
            ["Shared", "Server", "Server"],
            ["uint32", "uint32", "uint32"]);
        WriteDataRow(inventoryPolicySheet, 14, [1u, 100u, 20u]);

        IXLWorksheet mailPolicySheet = workbook.AddWorksheet("MailPolicy");
        WriteMetadataLabels(mailPolicySheet, "MailPolicy", "Server");
        ConfigureFields(mailPolicySheet,
            ["MailPolicyId", "MailListPageSize", "ExpirationSeconds"],
            ["Shared", "Server", "Server"],
            ["uint32", "uint32", "uint32"]);
        WriteDataRow(mailPolicySheet, 14, [1u, 20u, 2592000u]);

        IXLWorksheet mailTemplateSheet = workbook.AddWorksheet("MailTemplate");
        WriteMetadataLabels(mailTemplateSheet, "MailTemplate", "Server");
        ConfigureFields(mailTemplateSheet,
            ["MailTemplateDataId", "Purpose", "MailType", "Subject", "Body"],
            ["Shared", "Server", "Server", "Server", "Server"],
            ["uint32", "enum<MailTemplatePurpose>", "uint32", "string", "string"]);
        WriteDataRow(mailTemplateSheet, 14, [1001u, "AuctionPurchase", 2u, "Auction purchase", "Purchased item"]);
        WriteDataRow(mailTemplateSheet, 15, [1002u, "AuctionSaleProceeds", 2u, "Auction sold", "Sale proceeds"]);
        WriteDataRow(mailTemplateSheet, 16, [1003u, "AuctionCancellationReturn", 3u, "Auction cancelled", "Returned listing item"]);
        WriteDataRow(mailTemplateSheet, 17, [1004u, "AuctionExpirationReturn", 4u, "Auction expired", "Returned unsold item"]);

        IXLWorksheet combatPolicySheet = workbook.AddWorksheet("CombatFormulaPolicy");
        WriteMetadataLabels(combatPolicySheet, "CombatFormulaPolicy", "Server");
        ConfigureFields(combatPolicySheet,
            ["CombatFormulaPolicyId", "MinimumDamage", "PlayerBasicAttackRange", "PlayerBasicAttackCooldownMilliseconds", "PlayerRespawnDelayMilliseconds"],
            ["Shared", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "float", "uint32", "uint32"]);
        WriteDataRow(combatPolicySheet, 14, [1u, 1u, 64.0f, 1000u, 3000u]);

        IXLWorksheet statConversionSheet = workbook.AddWorksheet("StatConversion");
        WriteMetadataLabels(statConversionSheet, "StatConversion", "Server");
        ConfigureFields(statConversionSheet,
            ["StatConversionDataId", "CharacterDataId", "SourceStat", "TargetStat", "ValuePerPointPermille"],
            ["Shared", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "enum<PrimaryStatType>", "enum<DerivedStatType>", "uint32"]);
        statConversionSheet.Cell(12, 3).Value = "Character.CharacterDataId";
        WriteDataRow(statConversionSheet, 14, [1001u, 1u, "Str", "Attack", 1000u]);
        WriteDataRow(statConversionSheet, 15, [1002u, 1u, "Str", "MaxHp", 5000u]);
        workbook.SaveAs(path);
    }

    private static void CreateInvalidWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        CreateEnumDefinitionsSheet(
            workbook,
            [
                ["ItemCategory", "Shared", "uint8", "Equipment=300|Consumable=2|Material=3"],
                ["EquipmentSlot", "Shared", "uint8", "None=0|Weapon=1|Armor=2|Accessory=3"],
                ["MapType", "Shared", "uint8", "Town=1|Dungeon=2"],
                ["MonsterType", "Shared", "uint8", "Normal=1|Boss=3"],
                ["MonsterAggroType", "Server", "uint8", "Aggressive=1|Passive=2"],
                ["SectorExecutionMode", "Server", "uint8", "Serial=1|TaskGraph=2"],
                ["DefaultMode", "Shared", "uint8", "A=0|B=1"],
                ["HugeState", "Shared", "uint8", "Huge=300"],
                ["ClientOnlyState", "Client", "uint8", "Ready=1"],
                ["DuplicateValue", "Shared", "uint8", "First=1|Second=1"],
                ["ImplicitValue", "Shared", "uint8", "First"],
                ["BrokenTarget", "Both", "uint8", "First=1"],
                ["BrokenUnderlying", "Shared", "byte", "First=1"],
                ["itemcategory", "Shared", "uint8", "Duplicate=1"],
            ]);
        IXLWorksheet sheet = workbook.AddWorksheet("Item");
        WriteMetadataLabels(sheet, "Item", "Shared");
        string[] fields = ["ItemDataId", "Name", "Category", "EquipmentSlot", "MaxStack", "Tradable", "Attack", "Str", "Dex", "Int", "Luk", "DebugValue"];
        string[] scopes = ["Shared", "Shared", "Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server"];
        string[] types = ["uint32", "string", "enum<ItemCategory>", "enum<EquipmentSlot>", "uint32", "bool", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32"];
        for (int index = 0; index < fields.Length; ++index)
        {
            int column = index + 2;
            sheet.Cell(4, column).Value = index == 0 ? "Primary" : string.Empty;
            sheet.Cell(5, column).Value = scopes[index];
            sheet.Cell(6, column).Value = types[index];
            sheet.Cell(7, column).Value = "true";
            sheet.Cell(13, column).Value = fields[index];
        }
        sheet.Cell(9, 3).Value = 100;
        sheet.Cell(14, 2).FormulaA1 = "=1000+1";
        sheet.Cell(14, 3).Value = "Broken Sword";
        sheet.Cell(14, 4).Value = "Equipment";
        sheet.Cell(14, 5).Value = "None";
        sheet.Cell(14, 6).Value = 2u;
        sheet.Cell(14, 7).Value = true;
        sheet.Cell(14, 8).Value = 0u;
        sheet.Cell(14, 9).Value = 0u;
        sheet.Cell(14, 10).Value = 0u;
        sheet.Cell(14, 11).Value = 0u;
        sheet.Cell(14, 12).Value = 0u;
        sheet.Cell(14, 13).Value = 1u;
        WriteDataRow(sheet, 15, [1001u, "Another Sword", "Equipment", "Weapon", 1u, true, 0u, 0u, 0u, 0u, 0u, 1u]);
        WriteDataRow(sheet, 16, [1001u, "Third Sword", "Equipment", "Weapon", 1u, true, 0u, 0u, 0u, 0u, 0u, 1u]);
        workbook.SaveAs(path);
    }

    private static void CreateRuleFailuresWorkbook(string path)
    {
        using var workbook = new XLWorkbook();

        IXLWorksheet numericTarget = workbook.AddWorksheet("NumericTarget");
        WriteMetadataLabels(numericTarget, "NumericTarget", "Shared");
        numericTarget.Cell("B2").Value = 1;
        ConfigureFields(numericTarget, ["RowId"], ["Shared"], ["uint32"]);
        WriteDataRow(numericTarget, 14, [1u]);

        IXLWorksheet numericScope = workbook.AddWorksheet("NumericScope");
        WriteMetadataLabels(numericScope, "NumericScope", "Shared");
        ConfigureFields(numericScope, ["RowId", "Value"], ["Shared", "Shared"], ["uint32", "uint32"]);
        numericScope.Cell(5, 3).Value = 1;
        WriteDataRow(numericScope, 14, [1u, 10u]);

        IXLWorksheet defaultRules = workbook.AddWorksheet("DefaultRules");
        WriteMetadataLabels(defaultRules, "DefaultRules", "Shared");
        ConfigureFields(
            defaultRules,
            ["RowId", "Mode", "Count", "OptionalName", "MissingState", "ClientOnlyState"],
            ["Shared", "Shared", "Shared", "Shared", "Shared", "Shared"],
            ["uint32", "enum<DefaultMode>", "uint32", "string", "enum<MissingState>", "enum<ClientOnlyState>"]);
        defaultRules.Cell(7, 3).Value = "false";
        defaultRules.Cell(10, 3).Value = "C";
        defaultRules.Cell(11, 3).Value = "A=0|B=1";
        defaultRules.Cell(7, 4).Value = "false";
        defaultRules.Cell(9, 4).Value = 10;
        defaultRules.Cell(10, 4).Value = 20;
        defaultRules.Cell(7, 5).Value = "false";
        WriteDataRow(defaultRules, 14, [1u]);
        defaultRules.Cell(14, 7).Value = "Ready";

        IXLWorksheet identifierRules = workbook.AddWorksheet("IdentifierRules");
        WriteMetadataLabels(identifierRules, "IdentifierRules", "Shared");
        ConfigureFields(
            identifierRules,
            ["RowId", "DataId", "_", "Class", "namespace"],
            ["Shared", "Shared", "Shared", "Shared", "Shared"],
            ["uint32", "uint32", "uint32", "uint32", "uint32"]);
        WriteDataRow(identifierRules, 14, [1u, 2u, 3u, 4u, 5u]);

        IXLWorksheet enumRange = workbook.AddWorksheet("EnumRange");
        WriteMetadataLabels(enumRange, "EnumRange", "Shared");
        ConfigureFields(enumRange, ["RowId", "State"], ["Shared", "Shared"], ["uint32", "enum<HugeState>"]);
        WriteDataRow(enumRange, 14, [1u, "Huge"]);

        workbook.SaveAs(path);
    }

    private static void CreateItemCasingWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        IXLWorksheet sheet = workbook.AddWorksheet("ItemCasing");
        WriteMetadataLabels(sheet, "item", "Shared");
        ConfigureFields(
            sheet,
            ["ItemDataId", "Name", "Category", "EquipmentSlot", "MaxStack", "Tradable", "Attack", "Str", "Dex", "Int", "Luk"],
            ["Shared", "Shared", "Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "string", "enum<ItemCategory>", "enum<EquipmentSlot>", "uint32", "bool", "uint32", "uint32", "uint32", "uint32", "uint32"]);
        sheet.Cell(9, 3).Value = 100;
        WriteDataRow(sheet, 14, [3001u, "Iron Ore", "Material", "None", 99u, true, 0u, 0u, 0u, 0u, 0u]);
        workbook.SaveAs(path);
    }

    private static void CreateInvalidAuctionPolicyWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        IXLWorksheet sheet = workbook.AddWorksheet("AuctionPolicy");
        WriteMetadataLabels(sheet, "AuctionPolicy", "Server");
        ConfigureFields(
            sheet,
            ["AuctionPolicyId", "MaxActiveListings", "SearchPageSize", "MinimumListingDurationSeconds", "MaximumListingDurationSeconds", "DefaultListingDurationSeconds", "DefaultCurrencyDataId", "MinimumBidIncrement", "MinimumListingPrice", "MaximumListingPrice"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint64", "uint64", "uint64"]);
        WriteDataRow(sheet, 14, [2u, 5u, 20u, 3600u, 1800u, 1200u, 1u, 1ul, 100ul, 50ul]);
        WriteDataRow(sheet, 15, [3u, 5u, 20u, 3600u, 1800u, 1200u, 1u, 1ul, 100ul, 50ul]);
        workbook.SaveAs(path);
    }

    private static void CreateInvalidMapWorkbook(string path)
    {
        using var workbook = new XLWorkbook();

        IXLWorksheet mapSheet = workbook.AddWorksheet("Map");
        WriteMetadataLabels(mapSheet, "Map", "Shared");
        ConfigureFields(
            mapSheet,
            ["MapDataId", "Name", "MapType", "WorldWidth", "WorldHeight", "SectorSize", "AoiSectorRadius", "SpawnX", "SpawnY", "SectorExecutionMode", "MapAssetKey"],
            ["Shared", "Shared", "Shared", "Shared", "Shared", "Shared", "Shared", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "enum<MapType>", "uint32", "uint32", "uint32", "uint32", "float", "float", "enum<SectorExecutionMode>", "string"]);
        WriteDataRow(mapSheet, 14, [1u, "Broken Dungeon", "Dungeon", 1000u, 1024u, 128u, 1u, 1000.0f, -1.0f, "Serial", "BrokenMap"]);
        WriteDataRow(mapSheet, 15, [2u, "Valid Geometry Dungeon", "Dungeon", 1024u, 1024u, 128u, 1u, 512.0f, 512.0f, "Serial", "ValidMap"]);

        IXLWorksheet monsterSheet = workbook.AddWorksheet("Monster");
        WriteMetadataLabels(monsterSheet, "Monster", "Shared");
        ConfigureFields(
            monsterSheet,
            ["MonsterDataId", "Name", "MonsterType", "AggroType", "MaxHp", "Attack", "Defense", "MoveSpeed", "CollisionRadius", "AggroRadius", "LeashRadius", "AttackRange", "AttackCooldownMilliseconds", "SpriteAssetKey"],
            ["Shared", "Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "enum<MonsterType>", "enum<MonsterAggroType>", "uint32", "uint32", "uint32", "float", "float", "float", "float", "float", "uint32", "string"]);
        WriteDataRow(monsterSheet, 14, [1001u, "Broken Range Monster", "Normal", "Aggressive", 50u, 5u, 0u, 64.0f, 8.0f, 256.0f, 128.0f, 300.0f, 1000u, "BrokenMonster"]);

        IXLWorksheet spawnAreaSheet = workbook.AddWorksheet("SpawnArea");
        WriteMetadataLabels(spawnAreaSheet, "SpawnArea", "Server");
        ConfigureFields(
            spawnAreaSheet,
            ["SpawnAreaDataId", "MapDataId", "MinX", "MinY", "MaxX", "MaxY"],
            ["Shared", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "float", "float", "float", "float"]);
        spawnAreaSheet.Cell(12, 3).Clear();
        WriteDataRow(spawnAreaSheet, 14, [1001u, 1u, 900.0f, 900.0f, 912.0f, 1200.0f]);
        WriteDataRow(spawnAreaSheet, 15, [1002u, 2u, 200.0f, 200.0f, 100.0f, 300.0f]);

        IXLWorksheet monsterSpawnerSheet = workbook.AddWorksheet("MonsterSpawner");
        WriteMetadataLabels(monsterSpawnerSheet, "MonsterSpawner", "Server");
        ConfigureFields(
            monsterSpawnerSheet,
            ["SpawnerDataId", "MapDataId", "MonsterDataId", "SpawnAreaDataId", "InitialSpawnCount", "MaxAliveCount", "RespawnIntervalMilliseconds"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32"]);
        monsterSpawnerSheet.Cell(12, 3).Clear();
        monsterSpawnerSheet.Cell(12, 4).Value = "Monster.MissingMonsterDataId";
        monsterSpawnerSheet.Cell(12, 5).Value = "SpawnArea.MissingSpawnAreaDataId";
        WriteDataRow(monsterSpawnerSheet, 14, [1001u, 2u, 1001u, 1001u, 9u, 8u, 3000u]);

        IXLWorksheet characterSheet = workbook.AddWorksheet("Character");
        WriteMetadataLabels(characterSheet, "Character", "Shared");
        ConfigureFields(
            characterSheet,
            ["CharacterDataId", "Name", "InitialLevel", "InitialStr", "InitialDex", "InitialInt", "InitialLuk", "InitialUnspentStatPoints", "MoveSpeed", "CollisionRadius", "SpriteAssetKey"],
            ["Shared", "Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Client"],
            ["uint32", "string", "uint32", "uint32", "uint32", "uint32", "uint32", "uint32", "float", "float", "string"]);
        WriteDataRow(characterSheet, 14, [1u, "Broken Character", 2u, 4u, 4u, 4u, 4u, 0u, 0.0f, 0.0f, "BrokenSprite"]);
        WriteDataRow(characterSheet, 15, [2u, "Overflow Character", 1u, 1u, 1u, 1u, 1u, 1u, 96.0f, 6.0f, "OverflowSprite"]);
        WriteDataRow(characterSheet, 16, [3u, "Stat Overflow Character", 1u, uint.MaxValue, 1u, 1u, 1u, 1u, 96.0f, 6.0f, "StatOverflowSprite"]);

        IXLWorksheet characterLevelSheet = workbook.AddWorksheet("CharacterLevel");
        WriteMetadataLabels(characterLevelSheet, "CharacterLevel", "Server");
        ConfigureFields(
            characterLevelSheet,
            ["CharacterLevelDataId", "CharacterDataId", "Level", "RequiredExpToNextLevel", "MaxHp", "MaxMp", "Attack", "Defense", "StatPointReward"],
            ["Shared", "Server", "Server", "Server", "Server", "Server", "Server", "Server", "Server"],
            ["uint32", "uint32", "uint32", "uint64", "uint32", "uint32", "uint32", "uint32", "uint32"]);
        characterLevelSheet.Cell(12, 3).Value = "Character.CharacterDataId";
        WriteDataRow(characterLevelSheet, 14, [1001u, 1u, 1u, 0ul, 100u, 50u, 10u, 5u, 0u]);
        WriteDataRow(characterLevelSheet, 15, [1002u, 1u, 1u, 100ul, 120u, 55u, 12u, 6u, 5u]);
        WriteDataRow(characterLevelSheet, 16, [1003u, 1u, 3u, 50ul, 140u, 60u, 14u, 7u, 5u]);
        WriteDataRow(characterLevelSheet, 17, [2001u, 2u, 1u, 100ul, 100u, 50u, 10u, 5u, 1u]);
        WriteDataRow(characterLevelSheet, 18, [2002u, 2u, 2u, 0ul, 120u, 55u, 12u, 6u, uint.MaxValue]);
        WriteDataRow(characterLevelSheet, 19, [3001u, 3u, 1u, 0ul, 100u, 50u, 10u, 5u, 0u]);

        workbook.SaveAs(path);
    }

    private static void CreateInvalidCurrencyWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        IXLWorksheet sheet = workbook.AddWorksheet("Currency");
        WriteMetadataLabels(sheet, "Currency", "Shared");
        ConfigureFields(
            sheet,
            ["CurrencyDataId", "Name", "MaxAmount"],
            ["Shared", "Shared", "Server"],
            ["uint32", "string", "uint64"]);
        WriteDataRow(sheet, 14, [1u, "Gold", 1000ul]);
        WriteDataRow(sheet, 15, [70000u, "GOLD", 1000ul]);
        workbook.SaveAs(path);
    }

    private static void CreateInvalidEconomyPolicyWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        IXLWorksheet inventorySheet = workbook.AddWorksheet("InventoryPolicy");
        WriteMetadataLabels(inventorySheet, "InventoryPolicy", "Server");
        ConfigureFields(
            inventorySheet,
            ["InventoryPolicyId", "MaxInventorySlots", "InventoryListPageSize"],
            ["Shared", "Server", "Server"],
            ["uint32", "uint32", "uint32"]);
        WriteDataRow(inventorySheet, 14, [1u, 1000u, 100u]);

        IXLWorksheet mailSheet = workbook.AddWorksheet("MailPolicy");
        WriteMetadataLabels(mailSheet, "MailPolicy", "Server");
        ConfigureFields(
            mailSheet,
            ["MailPolicyId", "MailListPageSize", "ExpirationSeconds"],
            ["Shared", "Server", "Server"],
            ["uint32", "uint32", "uint32"]);
        WriteDataRow(mailSheet, 14, [1u, 100u, 3600u]);
        workbook.SaveAs(path);
    }

    private static void CreateIncompleteMailTemplateWorkbook(string path)
    {
        using var workbook = new XLWorkbook();
        IXLWorksheet sheet = workbook.AddWorksheet("MailTemplate");
        WriteMetadataLabels(sheet, "MailTemplate", "Server");
        ConfigureFields(
            sheet,
            ["MailTemplateDataId", "Purpose", "MailType", "Subject", "Body"],
            ["Shared", "Server", "Server", "Server", "Server"],
            ["uint32", "enum<MailTemplatePurpose>", "uint32", "string", "string"]);
        WriteDataRow(sheet, 14, [1001u, "AuctionPurchase", 256u, new string('S', 201), "Purchased item"]);
        workbook.SaveAs(path);
    }

    private static void ConfigureFields(
        IXLWorksheet sheet,
        IReadOnlyList<string> fields,
        IReadOnlyList<string> scopes,
        IReadOnlyList<string> types)
    {
        if (fields.Count != scopes.Count || fields.Count != types.Count)
        {
            throw new ArgumentException("Self-test field metadata lengths must match.");
        }

        for (int index = 0; index < fields.Count; ++index)
        {
            int column = index + 2;
            sheet.Cell(4, column).Value = index == 0 ? "Primary" : string.Empty;
            sheet.Cell(5, column).Value = scopes[index];
            sheet.Cell(6, column).Value = types[index];
            sheet.Cell(7, column).Value = "true";
            sheet.Cell(13, column).Value = fields[index];
        }
    }

    private static void CreateEnumDefinitionsSheet(
        XLWorkbook workbook,
        IReadOnlyList<IReadOnlyList<object>> definitions)
    {
        IXLWorksheet sheet = workbook.AddWorksheet(FGameDataEnumCatalog.TableName);
        WriteMetadataLabels(sheet, FGameDataEnumCatalog.TableName, "Shared");
        ConfigureFields(
            sheet,
            ["EnumName", "Target", "UnderlyingType", "Values"],
            ["Shared", "Shared", "Shared", "Shared"],
            ["string", "string", "string", "string"]);
        for (int index = 0; index < definitions.Count; ++index)
        {
            WriteDataRow(sheet, 14 + index, definitions[index]);
        }
    }

    private static void WriteMetadataLabels(IXLWorksheet sheet, string tableName, string target)
    {
        sheet.Cell("A1").Value = "#Table";
        sheet.Cell("B1").Value = tableName;
        sheet.Cell("A2").Value = "#Target";
        sheet.Cell("B2").Value = target;
        sheet.Cell("A4").Value = "#Key";
        sheet.Cell("A5").Value = "#Scope";
        sheet.Cell("A6").Value = "#Type";
        sheet.Cell("A7").Value = "#Required";
        sheet.Cell("A8").Value = "#Min";
        sheet.Cell("A9").Value = "#Max";
        sheet.Cell("A10").Value = "#Default";
        sheet.Cell("A11").Value = "#Allowed";
        sheet.Cell("A12").Value = "#Reference";
        sheet.Cell("A13").Value = "#Field";
    }

    private static void WriteDataRow(IXLWorksheet sheet, int row, IReadOnlyList<object> values)
    {
        for (int index = 0; index < values.Count; ++index)
        {
            sheet.Cell(row, index + 2).Value = XLCellValue.FromObject(values[index]);
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Self-test assertion failed: {message}");
        }
    }
}
