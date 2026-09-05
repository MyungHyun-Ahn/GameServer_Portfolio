# ConfigGenerator

`Tools/ConfigGenerator`는 `ConfigSchema/**/*.schema.yaml`을 읽어 typed C++ 설정 구조체, 검증 loader와 공개 기본 YAML을 생성하는 .NET 9 도구입니다.

## 입력 스키마

```yaml
WorldServer:
  Backend:
    type: enum
    default: Iocp
    values: [Iocp, Rio]
  BindIp: { type: string, default: 127.0.0.1, required: true }
  Port: { type: uint16, default: 19200, required: true }

Debug:
  Headless: { type: bool, default: false }
```

지원 타입은 `bool`, `int32`, `uint16`, `uint32`, `int64`, `uint64`, `float`, `double`, `string`, `enum`입니다. Loader는 section·key, 필수값, scalar 형식과 enum 허용값을 검사합니다.

현재 주요 서버 스키마의 역할은 다음과 같습니다.

| 스키마 | 주요 설정 |
|---|---|
| `ConfigSchema/Server/CacheServer.schema.yaml` | RPC endpoint, cache shard·lease, GameDB Primary/Replica, GameData, fault injection |
| `ConfigSchema/Server/AuctionHouseServer.schema.yaml` | client endpoint, 인증, Cache RPC, AuctionDB Primary/Replica, 만료·진단 |
| `ConfigSchema/Server/WorldServer.schema.yaml` | client endpoint, Redis ticket, Cache Presence, Map/Sector Worker, GameData |

## 공개 설정과 로컬 설정

생성기가 쓰는 `Config/**/*.yaml`은 실행 가능한 공개 기본값입니다. 서버 주소, 포트와 기능 기본값은 포함하지만 실제 비밀번호는 넣지 않습니다.

통합 실행용 CacheServer·AuctionHouseServer 설정은 저장소 루트의 `.env.example`을 `.env`로 복사하고 로컬 비밀번호를 입력한 뒤 생성합니다. `MYSQL_ROOT_PASSWORD`와 `MYSQL_PASSWORD`는 Docker DB 구성에 필수이며, Redis에 비밀번호를 설정한 환경에서만 `.env`에 `REDIS_PASSWORD` 항목을 추가합니다.

```powershell
Copy-Item -LiteralPath .\.env.example -Destination .\.env
notepad .\.env
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\config\Initialize-LocalServerConfigs.ps1
```

스크립트는 다음 Git 제외 파일을 만듭니다.

```text
Config/Server/CacheServer.local.yaml
Config/Server/AuctionHouseServer.local.yaml
```

두 서버는 명시적인 `--config`가 없으면 실행 파일 옆 `Config/Server`의 local 설정을 우선하고, 없으면 공개 설정을 읽습니다. WorldServer의 환경별 override가 필요하면 공개 설정을 `Config/Server/WorldServer.local.yaml`로 복사하고 `--config`로 명시합니다.

실제 비밀번호와 개인 환경값은 `.env` 또는 `*.local.yaml`에만 보관하며 커밋하지 않습니다.

## 생성

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Configs.ps1 `
  -Configuration Release
```

도구 빌드만 확인하려면 `-BuildOnly`를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Configs.ps1 `
  -Configuration Release -BuildOnly
```

도구를 직접 호출하면 각 루트를 변경할 수 있습니다.

```powershell
dotnet run --project .\Tools\ConfigGenerator\ConfigGenerator.csproj -c Release -- `
  --schema-root .\ConfigSchema `
  --output-root .\Generated\Config `
  --config-root .\Config
```

## 생성 결과

`ConfigSchema/Server/WorldServer.schema.yaml`의 결과는 다음과 같습니다.

```text
Generated/Config/WorldServer/WorldServerConfig.h
Generated/Config/WorldServer/WorldServerConfig.cpp
Config/Server/WorldServer.yaml
```

- `*Config.h`: enum, section 구조체, root document와 loader 선언
- `*Config.cpp`: 알려진 section·key 및 타입·enum 검증
- `Config/**/*.yaml`: 스키마 default를 반영한 공개 실행 템플릿

### 실제 생성 코드 예시: WorldServer

세 출력은 하나의 스키마에서 함께 만들어집니다.

| 실제 파일 | 역할 |
|---|---|
| `ConfigSchema/Server/WorldServer.schema.yaml` | 타입, default, required와 enum 허용값을 정의하는 입력 |
| `Generated/Config/WorldServer/WorldServerConfig.h` | typed enum·section·document와 loader 선언 |
| `Generated/Config/WorldServer/WorldServerConfig.cpp` | YAML key 파싱과 타입·enum 검증 구현 |
| `Config/Server/WorldServer.yaml` | 스키마 default로 만든 공개 실행 설정 |

생성 header의 핵심 형태는 다음과 같습니다.

```cpp
namespace Generated::Config::WorldServer
{
    enum class EBackend
    {
        Iocp,
        Rio
    };

    struct SWorldServerConfig
    {
        EBackend Backend = EBackend::Iocp;
        std::string BindIp = "127.0.0.1";
        std::uint16_t Port = 19200;
        std::uint32_t PacketKey = 55;
        bool CacheEnabled = false;
    };

    struct SWorldServerDebugConfig
    {
        bool Headless = false;
    };

    struct FWorldServerConfigDocument
    {
        SWorldServerConfig WorldServer;
        SWorldServerDebugConfig Debug;
    };

    class FWorldServerConfigLoader
    {
    public:
        static bool LoadFromFile(
            const std::filesystem::path& filePath,
            FWorldServerConfigDocument& outConfig,
            std::string& outError);
    };
}
```

공개 YAML에는 같은 이름과 default가 반영됩니다.

```yaml
WorldServer:
  Backend: Iocp
  BindIp: 127.0.0.1
  Port: 19200
  PacketKey: 55
  CacheEnabled: false

Debug:
  Headless: false
```

애플리케이션에서는 문자열 key를 다시 파싱하지 않고 생성된 document를 사용합니다.

```cpp
Generated::Config::WorldServer::FWorldServerConfigDocument document{};
std::string error;
if (!Generated::Config::WorldServer::FWorldServerConfigLoader::LoadFromFile(
        configPath,
        document,
        error))
{
    throw std::runtime_error(error);
}

const auto& config = document.WorldServer;
if (config.WorkerThreadCount <= 0)
{
    throw std::runtime_error("WorkerThreadCount must be positive.");
}

NetworkLib::Core::SServerConfig serverConfig{};
serverConfig.bindIp = config.BindIp;
serverConfig.port = config.Port;
serverConfig.workerThreadCount =
    static_cast<std::uint32_t>(config.WorkerThreadCount);
const std::uint32_t packetKey = config.PacketKey;
```

실제 적용 코드는 `World/WorldServer/Application/FWorldServerBootstrap.cpp`에 있습니다. Loader는 스키마에 선언된 key·scalar·enum 계약을 검사하고, endpoint 조합이나 기능 간 의존성 같은 애플리케이션 규칙은 bootstrap의 typed config 적용 단계에서 추가로 검사합니다.

애플리케이션은 생성된 `F<Target>ConfigLoader`로 YAML을 읽고 typed document를 사용합니다. 생성 파일과 공개 YAML의 값을 따로 수정하면 다음 생성에서 사라지므로 default·required·enum 변경은 입력 스키마에 반영합니다.
