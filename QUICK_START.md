# Quick Start

## 1. 요구 환경

- Windows 10/11 x64
- Visual Studio 2022
  - Desktop development with C++
  - MSVC v143
  - Windows 10/11 SDK
- .NET SDK 9.0
- PowerShell 5.1 이상
- Docker Desktop: 로그인·Redis·MySQL 경매장 기능 실행 시 필요

Node.js 로그인 서버를 Docker 없이 직접 실행하려면 Node.js 22 LTS가 추가로 필요합니다.

## 2. 솔루션 빌드

Visual Studio Developer PowerShell에서 저장소 루트로 이동해 실행합니다.

```powershell
msbuild .\Portfolio.sln /restore /m /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64
```

생성 코드는 저장소에 포함되어 있으므로 첫 빌드 전에 생성기를 실행할 필요는 없습니다. 스키마를 변경했을 때만 코드 생성 파이프라인을 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Codegen.ps1 `
  -Configuration Release
```

주요 산출물:

```text
Out/EchoServer/Release/EchoServer.exe
Out/EchoClient/Release/EchoClient.exe
Out/ChattingServer/Release/ChattingServer.exe
Out/ChattingDummyClient/Release/ChattingDummyClient.exe
Out/AuctionHouseServer/Release/AuctionHouseServer.exe
Out/AuctionDummyClient/Release/AuctionDummyClient.exe
Out/AuctionClientWinForms/Release/net9.0-windows/AuctionClientWinForms.exe
```

## 3. Docker 없이 네트워크 경로 확인

첫 번째 터미널에서 EchoServer를 실행합니다.

```powershell
.\Out\EchoServer\Release\EchoServer.exe
```

두 번째 터미널에서 EchoClient를 실행합니다.

```powershell
.\Out\EchoClient\Release\EchoClient.exe
```

기본 설정은 각각 `Config/Server/EchoServer.yaml`, `Config/Client/EchoClient.yaml`에 있습니다.

## 4. 경매장 전체 실행

### 4.1 로컬 비밀번호 준비

```powershell
Copy-Item .\.env.example .\.env
notepad .\.env
```

`.env`의 두 값을 로컬 비밀번호로 변경합니다.

```dotenv
MYSQL_ROOT_PASSWORD=<local-root-password>
MYSQL_PASSWORD=<local-app-password>
```

`.env`는 Git에 포함되지 않습니다.

### 4.2 LoginServer·Redis·AccountDB 시작

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-LoginPlatform.ps1
```

정상 기동 후 LoginServer는 `http://127.0.0.1:18080`, Redis는 `127.0.0.1:6379`를 사용합니다.

### 4.3 GameDB·AuctionDB Primary/Replica 시작

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-AuctionDatabases.ps1
```

기본 포트:

| 역할 | 주소 |
|---|---|
| GameDB Primary | `127.0.0.1:3310` |
| GameDB Replica 1·2 | `127.0.0.1:3311`, `127.0.0.1:3312` |
| AuctionDB Primary | `127.0.0.1:3320` |
| AuctionDB Replica 1·2 | `127.0.0.1:3321`, `127.0.0.1:3322` |

### 4.4 AuctionHouseServer 시작

`.env`에 넣은 `MYSQL_PASSWORD`와 같은 값을 현재 터미널 환경변수로 설정합니다.

```powershell
$env:MYSQL_PASSWORD = '<local-app-password>'
.\Out\AuctionHouseServer\Release\AuctionHouseServer.exe `
  --database-enabled --redis-auth-enabled
```

기본 경매장 주소는 `127.0.0.1:19102`입니다.

### 4.5 C# 클라이언트 시작

```powershell
.\Out\AuctionClientWinForms\Release\net9.0-windows\AuctionClientWinForms.exe
```

클라이언트는 `Auction/AuctionClientWinForms/appsettings.json`의 LoginServer 주소를 사용합니다. 회원가입 후 로그인하면 LoginServer가 발급한 일회용 ticket으로 AuctionHouseServer 인증을 수행합니다.

## 5. 종료

서버 프로세스는 각 콘솔에서 `Ctrl+C`로 종료합니다. 컨테이너는 데이터 볼륨을 유지한 채 다음 명령으로 중지할 수 있습니다.

```powershell
docker compose --env-file .\.env -f .\Infra\docker-compose.auction-databases.yaml down
docker compose --env-file .\.env -f .\Infra\docker-compose.login-platform.yaml down
```

DB를 완전히 초기화해야 할 때만 다음 명령을 사용합니다. 이 옵션은 로컬 DB 볼륨을 삭제합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Infra\Start-AuctionDatabases.ps1 -Recreate
```
