# GameDataGenerator

`Tools/GameDataGenerator`는 `GameData/Excel/*.xlsx`를 게임 데이터 원본으로 읽고, 전체 검증이 성공한 경우에만 서버 YAML, 클라이언트 JSON, schema와 C++/C# 코드를 생성하는 .NET 9 도구입니다. Excel 처리는 ClosedXML을 사용합니다.

## 통합 문서 형식

각 워크시트가 하나의 테이블이며 빈 워크시트는 무시합니다.

| 행 | A열 | B열 이후 |
|---:|---|---|
| 1 | `#Table` | 테이블 이름 |
| 2 | `#Target` | `Shared`, `Server`, `Client` |
| 4 | `#Key` | 기본 키 필드만 `Primary` |
| 5 | `#Scope` | `Shared`, `Server`, `Client`, `Ignore` |
| 6 | `#Type` | scalar 또는 `enum<Name>` |
| 7 | `#Required` | `true` 또는 `false` |
| 8 | `#Min` | 숫자 하한 또는 문자열 최소 길이 |
| 9 | `#Max` | 숫자 상한 또는 문자열 최대 길이 |
| 10 | `#Default` | 빈 셀에 적용할 기본값 |
| 11 | `#Allowed` | 일반 허용값. 공용 enum 필드는 비워 둠 |
| 12 | `#Reference` | `Table.PrimaryKeyField` 형식의 참조 |
| 13 | `#Field` | 생성 코드와 데이터 파일의 필드 이름 |
| 14+ |  | 실제 데이터 |

scalar는 `bool`, `int32`, `uint32`, `int64`, `uint64`, `float`, `double`, `string`을 지원하며 스키마에 따라 더 좁은 enum 기반형도 생성합니다. 수식 셀은 허용하지 않습니다. `#Required=false`인 출력 필드는 C++과 C#에서 같은 값 계약을 유지하도록 `#Default`가 필요합니다.

생성기는 한 번의 실행에서 모든 파일·시트·셀 오류를 모아 보고하고, 유효한 경우에만 임시 출력에서 완성된 결과로 교체합니다.

## Target과 Scope 투영

| `#Target` | 서버 출력 | 클라이언트 출력 |
|---|---|---|
| `Server` | YAML + C++의 `Shared + Server` 필드 | 없음 |
| `Shared` | YAML + C++의 `Shared + Server` 필드 | JSON + C#의 `Shared + Client` 필드 |
| `Client` | 없음 | JSON + C#의 `Shared + Client` 필드 |

예를 들어 Item ID·이름·분류처럼 양쪽에 필요한 값은 `Shared`, 서버 판정에만 쓰는 전투 수치는 `Server`, sprite asset key는 `Client`로 둡니다.

## 공용 enum

`GameData/Excel/Enums.xlsx`에는 `#Target=Shared`인 `Enums` 테이블 하나를 둡니다.

| 필드 | 계약 |
|---|---|
| `EnumName` | Required Shared `string`, Primary Key |
| `Target` | `Shared`, `Server`, `Client` |
| `UnderlyingType` | `uint8`, `uint16`, `int32`, `uint32` |
| `Values` | `Name=Number|Name=Number` |

일반 테이블은 `#Type=enum<Name>`으로 참조하고 `#Allowed`에 값을 중복 정의하지 않습니다. C++에는 대상에 맞는 enum이 생성되고, C#에는 Shared/Client enum만 생성됩니다.

## 새 테이블

`GameData/Templates/GameDataTableTemplate.xlsx`를 입력 폴더로 복사해 시작합니다.

```powershell
Copy-Item -LiteralPath .\GameData\Templates\GameDataTableTemplate.xlsx `
  -Destination .\GameData\Excel\NewTable.xlsx
```

복사한 통합 문서에서 워크시트 이름, `B1`의 테이블 이름과 `B13`의 PK 필드 이름을 먼저 바꾼 뒤 4~13행의 metadata와 14행 이후 데이터를 작성합니다. 템플릿의 `<TableName>` 표기는 의도적으로 유효하지 않으므로 바꾸지 않으면 검증이 실패합니다.

현재 원본은 enum, Item, AuctionPolicy, Map, Monster·Spawn, Character·Level, Combat, Economy 테이블을 관리합니다. 참조 ID, 기본 키 중복, 범위뿐 아니라 장비·경매 가격·맵 경계·Monster spawn·레벨 연속성·단일 정책 행 같은 테이블 간 계약도 검증합니다.

## 생성과 검사

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-GameData.ps1 `
  -Configuration Release
```

출력을 바꾸지 않고 Excel만 검증하려면 `-ValidateOnly`, Git에 저장된 생성물이 현재 Excel과 정확히 같은지 검사하려면 `-Check`를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-GameData.ps1 `
  -Configuration Release -ValidateOnly

powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-GameData.ps1 `
  -Configuration Release -Check
```

`-ValidateOnly`와 `-Check`는 함께 사용할 수 없습니다. 생성기 자체 계약은 독립 self test로 확인합니다.

```powershell
dotnet run --project .\Tools\GameDataGenerator\GameDataGenerator.csproj -- --self-test
```

도구를 직접 실행할 수도 있습니다.

```powershell
dotnet run --project .\Tools\GameDataGenerator\GameDataGenerator.csproj -c Release -- `
  --input-root .\GameData\Excel `
  --output-root .\Generated\GameData
```

## 생성 결과

```text
Generated/GameData/
├─ Schema/
│  ├─ GameDataEnums.schema.yaml
│  └─ <Table>.schema.yaml
├─ Data/
│  ├─ Server/<Table>.yaml
│  └─ Client/<Table>.json
├─ Cpp/
│  ├─ Common/GameDataEnums.g.h
│  └─ <Table>/<Table>Data.g.h
└─ CSharp/
   ├─ GeneratedGameData.csproj
   ├─ Common/GameDataEnums.g.cs
   └─ <Table>/<Table>Data.g.cs
```

### 실제 생성 예시: Item.xlsx

`GameData/Excel/Item.xlsx`의 `Item` worksheet 한 개는 다음 산출물로 투영됩니다.

| 실제 파일 | 포함 범위 |
|---|---|
| `Generated/GameData/Schema/Item.schema.yaml` | 원본 파일, Target, PK, 모든 필드의 Scope·타입·제약 |
| `Generated/GameData/Data/Server/Item.yaml` | Shared + Server 필드 |
| `Generated/GameData/Data/Client/Item.json` | Shared + Client 필드 |
| `Generated/GameData/Cpp/Item/ItemData.g.h` | 서버용 `SItemTemplate` row 타입 |
| `Generated/GameData/CSharp/Item/ItemData.g.cs` | 클라이언트용 `ItemData`와 `ItemDataTable` |
| `Generated/GameData/CSharp/GeneratedGameData.csproj` | 모든 C# 생성 타입을 빌드하는 contract 프로젝트 |

같은 ID 1001은 서버 YAML에는 판정용 능력치까지, 클라이언트 JSON에는 공유 표시 정보만 포함됩니다.

```yaml
# Generated/GameData/Data/Server/Item.yaml
Item1001:
  ItemDataId: 1001
  Name: "Warrior Sword"
  Category: "Equipment"
  EquipmentSlot: "Weapon"
  MaxStack: 1
  Tradable: true
  Attack: 12
  Str: 12
  Dex: 0
  Int: 0
  Luk: 0
```

`Generated/GameData/Data/Client/Item.json`에는 다음 배열이 생성됩니다.

```json
[
  {
    "ItemDataId": 1001,
    "Name": "Warrior Sword",
    "Category": "Equipment",
    "EquipmentSlot": "Weapon"
  }
]
```

생성되는 언어별 타입도 각 projection을 따릅니다.

```cpp
namespace GameData::Item
{
    struct SItemTemplate final
        : GameData::TGameDataRow<SItemTemplate, std::uint32_t>
    {
        std::uint32_t itemDataId{};
        std::string name;
        GameData::Common::EItemCategory category{};
        GameData::Common::EEquipmentSlot equipmentSlot{};
        std::uint32_t maxStack{};
        bool tradable{};
        std::uint32_t attack{};
        std::uint32_t str{};
        std::uint32_t dex{};
        std::uint32_t intelligence{};
        std::uint32_t luk{};
    };
}
```

```csharp
namespace Generated.GameData.Item;

public sealed class ItemData : GameDataRow<uint>
{
    public uint ItemDataId { get; init; }
    public string Name { get; init; } = string.Empty;
    public EItemCategory Category { get; init; }
    public EEquipmentSlot EquipmentSlot { get; init; }
    public override uint DataId => ItemDataId;
}

public static class ItemDataTable
{
    public static IReadOnlyDictionary<uint, ItemData> Load(string path) =>
        GameDataTableLoader.Load<uint, ItemData>(path, ValidateRow);

    // 실제 생성 파일에는 필드별 ValidateRow 구현이 이어집니다.
}
```

### 생성 데이터 사용 예시

C++ 생성물은 row 계약이며, YAML 적재와 index는 `Libraries/GameData`의 hand-written table이 담당합니다.

```cpp
GameData::Item::FItemDataTable items;
std::string error;
if (!items.Load(gameDataDirectory / "Item.yaml", error))
{
    throw std::runtime_error(error);
}

const GameData::Item::SItemTemplate* sword = items.Find(1001);
if (sword == nullptr)
{
    throw std::runtime_error("Unknown ItemDataId.");
}
```

C#에서는 생성된 table facade가 공용 `GameDataTableLoader`를 호출합니다.

```csharp
string path = Path.Combine(
    AppContext.BaseDirectory,
    "GameData",
    "Item.json");

IReadOnlyDictionary<uint, ItemData> items = ItemDataTable.Load(path);
ItemData sword = items[1001];
```

참고 구현은 C++의 `Libraries/GameData/Item/FItemDataTable.cpp`와 C#의 `Auction/AuctionClientWinForms/Models/ItemCatalog.cs`입니다.

`Generated/GameData/CSharp/GeneratedGameData.csproj`는 C# 생성물을 하나의 .NET 9 contract 프로젝트로 빌드합니다. C++ 서버는 `Libraries/GameData`, C# 클라이언트는 `CSharp/GameData`의 loader를 통해 각 projection을 읽습니다.

개발 중 생성한 서버 YAML을 이미 빌드된 CacheServer, AuctionHouseServer와 WorldServer 출력 폴더에 배포하려면 `-DeployRuntime`을 사용할 수 있습니다. 실행 중인 서버는 데이터를 시작 시 적재하므로 재시작해야 합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-GameData.ps1 `
  -Configuration Release -DeployRuntime
```

## C++·C# smoke test

```powershell
dotnet run --project .\SmokeTests\GameDataCSharpSmokeTest\GameDataCSharpSmokeTest.csproj -c Release

msbuild .\SmokeTests\GameDataCppSmokeTest\GameDataCppSmokeTest.vcxproj /m `
  /p:Configuration=Release /p:Platform=x64
.\Out\GameDataCppSmokeTest\Release\GameDataCppSmokeTest.exe
```

두 smoke test는 같은 생성 데이터에서 enum, Shared/Server/Client projection과 주요 table loader 조회 결과를 확인합니다. Excel 임시 파일 `~$*.xlsx`, `*.xlsx.tmp`와 생성 프로젝트의 `bin`·`obj`는 제출하지 않습니다.
