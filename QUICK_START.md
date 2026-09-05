# Quick Start

## 1. 요구 환경

- Windows 10/11 x64
- Visual Studio 2022
  - Desktop development with C++
  - MSVC v143
  - Windows 10/11 SDK
  - C++ Clang tools for Windows (`clang-format`)
- .NET SDK 9.0
- PowerShell 5.1 이상
- Node.js 22 LTS: LoginServer를 호스트에서 빌드·테스트할 때 필요
- Docker Desktop: LoginServer·Redis·MySQL을 포함한 통합 실행에만 필요

Echo, 코드 생성기와 대부분의 smoke test는 Docker 없이 확인할 수 있습니다. Chatting benchmark manifest는 인증 인프라가 측정에 영향을 주지 않도록 `LoginAuth.Mode`를 `Disabled`로 덮어씁니다. Redis 인증을 사용하는 Chatting과 전체 인증·Cache·Auction·World 흐름에는 Docker 인프라가 필요합니다.

## 2. 코드 생성

생성 결과는 저장소에 포함되어 있으므로 처음 빌드할 때 반드시 재생성할 필요는 없습니다. Packet, Config, RPC YAML 또는 GameData Excel을 변경했다면 저장소 루트에서 먼저 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Codegen.ps1 `
  -Configuration Release
```

저장소의 RPC·GameData 생성 코드를 갱신하지 않고, 현재 스키마와 일치하는지 비교 검사하는 명령은 다음과 같습니다. 검사 중 도구의 `bin`/`obj` 빌드 산출물은 갱신될 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Rpcs.ps1 `
  -Configuration Release -Check

powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-GameData.ps1 `
  -Configuration Release -Check
```

## 3. 로컬 비밀값과 서버 설정

통합 실행 전 `.env`를 만들고 예시 값을 로컬 비밀번호로 교체합니다.

```powershell
Copy-Item -LiteralPath .\.env.example -Destination .\.env
notepad .\.env
```

```dotenv
MYSQL_ROOT_PASSWORD=<local-root-password>
MYSQL_PASSWORD=<local-app-password>
```

CacheServer와 AuctionHouseServer의 로컬 설정을 생성합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\config\Initialize-LocalServerConfigs.ps1
```

이 스크립트는 공개 기본 설정을 바꾸지 않고 다음 파일을 만듭니다.

```text
Config/Server/CacheServer.local.yaml
Config/Server/AuctionHouseServer.local.yaml
```

실제 비밀번호와 환경별 endpoint는 `.env` 또는 `*.local.yaml`에만 보관합니다. 두 형식은 Git에서 제외됩니다. WorldServer의 인증·Cache 연동 값을 바꿀 때도 `WorldServer.yaml`을 복사한 `WorldServer.local.yaml`을 만들고 실행 시 `--config`로 지정합니다.

## 4. Debug·Release 빌드

Visual Studio Developer PowerShell에서 저장소 루트로 이동해 실행합니다.

```powershell
msbuild .\Portfolio.sln /restore /m /t:Rebuild `
  /p:Configuration=Debug /p:Platform=x64

msbuild .\Portfolio.sln /restore /m /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64
```

주요 Release 산출물은 다음과 같습니다.

```text
Out/CacheServer/Release/CacheServer.exe
Out/CacheRpcPingClient/Release/CacheRpcPingClient.exe
Out/AuctionHouseServer/Release/AuctionHouseServer.exe
Out/AuctionClientWinForms/Release/net9.0-windows/AuctionClientWinForms.exe
Out/WorldServer/Release/WorldServer.exe
Out/WorldClientWinForms/Release/net9.0-windows/WorldClientWinForms.exe
Out/WorldDummyClient/Release/net9.0/WorldDummyClient.exe
```

서버 실행 시에는 아래 예시처럼 저장소의 공개 Config 또는 `*.local.yaml` 경로를 `--config`로 명시합니다. 프로젝트가 필요로 하는 GameData와 런타임 파일은 빌드 과정에서 각 `Out/<Project>/<Configuration>` 경로로 복사됩니다.

## 5. Docker 없이 네트워크 확인

첫 번째 터미널에서 EchoServer를, 두 번째 터미널에서 EchoClient를 실행합니다.

```powershell
.\Out\EchoServer\Release\EchoServer.exe `
  --config .\Config\Server\EchoServer.yaml
```

```powershell
.\Out\EchoClient\Release\EchoClient.exe `
  --config .\Config\Client\EchoClient.yaml
```

## 6. 통합 실행 순서

### 6.1 LoginServer·Redis·AccountDB

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-LoginPlatform.ps1
```

LoginServer는 `http://127.0.0.1:18080`, Redis는 `127.0.0.1:6379`에서 시작합니다.

Auction과 World UI 클라이언트에서는 회원가입한 뒤 같은 계정으로 로그인할 수 있습니다.

호스트에서 LoginServer만 검증하려면 다음 명령을 사용할 수 있습니다.

```powershell
Push-Location .\LoginServer
npm ci
npm run build
npm test
Pop-Location
```

### 6.2 GameDB·AuctionDB

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-AuctionDatabases.ps1
```

| 역할 | 주소 |
|---|---|
| GameDB Primary | `127.0.0.1:3310` |
| GameDB Replica 1·2 | `127.0.0.1:3311`, `127.0.0.1:3312` |
| AuctionDB Primary | `127.0.0.1:3320` |
| AuctionDB Replica 1·2 | `127.0.0.1:3321`, `127.0.0.1:3322` |

### 6.3 CacheServer

```powershell
.\Out\CacheServer\Release\CacheServer.exe `
  --config .\Config\Server\CacheServer.local.yaml
```

기본 RPC 주소는 `127.0.0.1:19103`입니다. GameDB 접근은 CacheServer가 담당하므로 AuctionHouseServer 또는 Cache 연동 WorldServer보다 먼저 시작합니다.

### 6.4 Auction 경로

```powershell
.\Out\AuctionHouseServer\Release\AuctionHouseServer.exe `
  --config .\Config\Server\AuctionHouseServer.local.yaml
```

기본 주소는 `127.0.0.1:19102`입니다. UI 클라이언트는 LoginServer에서 일회용 Auction ticket을 받은 뒤 서버에 접속합니다.

```powershell
.\Out\AuctionClientWinForms\Release\net9.0-windows\AuctionClientWinForms.exe
```

### 6.5 World 경로

인증·Cache 연동을 켤 때는 로컬 World 설정을 준비합니다.

```powershell
Copy-Item -LiteralPath .\Config\Server\WorldServer.yaml `
  -Destination .\Config\Server\WorldServer.local.yaml
notepad .\Config\Server\WorldServer.local.yaml
```

`WorldServer.AuthMode`를 `Redis`, `WorldServer.CacheEnabled`를 `true`로 설정한 뒤 실행합니다.

```powershell
.\Out\WorldServer\Release\WorldServer.exe `
  --config .\Config\Server\WorldServer.local.yaml

.\Out\WorldClientWinForms\Release\net9.0-windows\WorldClientWinForms.exe
```

WorldServer 기본 주소는 `127.0.0.1:19200`입니다. UI 대신 멀티플레이 더미를 사용할 수도 있습니다.

```powershell
.\Out\WorldDummyClient\Release\net9.0\WorldDummyClient.exe `
  --config .\World\WorldDummyClient\appsettings.redis.json `
  --map-data-id 1 --virtual-users 20 --run-seconds 30 --random-seed 20260829
```

Map 1은 Serial, Map 2는 4 Wave TaskGraph 실행 모드를 사용합니다.

## 7. 종료와 초기화

서버 프로세스는 각 콘솔에서 `Ctrl+C`로 종료합니다. 컨테이너는 볼륨을 보존한 채 중지할 수 있습니다.

```powershell
docker compose --env-file .\.env -f .\Infra\docker-compose.auction-databases.yaml down
docker compose --env-file .\.env -f .\Infra\docker-compose.login-platform.yaml down
```

GameDB·AuctionDB Primary/Replica 6개의 Docker 데이터 볼륨을 삭제하고 다시 구성해야 할 때만 다음 명령을 사용합니다. AccountDB와 Redis 볼륨은 이 명령의 대상이 아닙니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-AuctionDatabases.ps1 -Recreate
```
