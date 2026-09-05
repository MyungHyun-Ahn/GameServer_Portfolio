# Game Server Portfolio

Windows C++20 기반의 비동기 게임 서버 포트폴리오입니다. 하나의 서버 계약에서 IOCP와 RIO 백엔드를 선택할 수 있는 네트워크 계층, mailbox 기반 콘텐츠 런타임, 서버 간 RPC, CacheServer, 경매장과 월드 서버를 함께 구현했습니다. C# WinForms·더미 클라이언트도 C++ 서버와 같은 생성 패킷 및 wire format을 사용합니다.

## 주요 구현

| 영역 | 구현 내용 | 대표 위치 |
|---|---|---|
| 네트워크 | IOCP, RIO Direct/Owner Thread, 세션 수명, framing, backpressure | `Libraries/NetworkLib` |
| 콘텐츠 런타임 | Content mailbox, single-writer 실행, instance routing, shard, 원 요청 mailbox로 돌아오는 RPC callback | `Libraries/ContentsRuntime` |
| 서버 RPC | Call/Response/Noti, pending call, timeout, target·routing 검증, 생성 descriptor | `Libraries/RpcLib`, `Libraries/ServerProtocol`, `Rpc`, `Generated/Rpc` |
| 캐시·GameDB | 사용자 Get-or-Load, 인벤토리·재화·우편·캐릭터 상태, Presence lease, 스레드 귀속 Primary/Replica 연결 | `Cache/CacheServer`, `Libraries/Connector`, `Database/GameDB` |
| 경매장 | 인증, 등록, 조회, 입찰, 환급, 즉시 구매, 취소, 만료, 우편 수령과 AuctionDB 처리 | `Auction/AuctionHouseServer`, `Database/AuctionDB` |
| 월드 | 로그인 ticket, Cache Presence, Map/Sector/AOI, Serial/TaskGraph 실행, 이동, 몬스터 AI, 전투, 사망·부활, 최종 능력치 계산 | `World/WorldCore`, `World/WorldServer` |
| C# 클라이언트 | 공용 TCP·packet 계층, Auction/Chatting/World WinForms, World 멀티플레이 더미 | `CSharp/ClientNetwork`, `World/WorldClientCore`, `World/WorldDummyClient` |
| 코드 생성 | C++/C# Packet, C++ Config, C++ RPC, Excel 기반 서버·클라이언트 GameData | `Tools`, `scripts/generate`, `Generated` |
| 검증 | lock-free·RPC·TaskGraph·GameData·C# adapter smoke test와 Chatting benchmark runner | `SmokeTests`, `scripts` |

## 디렉터리 구성

```text
Portfolio/
├─ Auction/                  경매장 서버, C++ 더미, C# WinForms 클라이언트
├─ Cache/                    CacheServer와 RPC ping 클라이언트
├─ Chatting/                 콘텐츠 런타임 기반 채팅 예제와 클라이언트
├─ CSharp/                   공용 ClientNetwork와 GameData 런타임
├─ Config/                   공개 기본 실행 설정
├─ ConfigSchema/             typed C++ Config 입력 스키마
├─ Database/                 GameDB·AuctionDB SQL과 복제 초기화 스크립트
├─ Echo/                     네트워크 왕복 예제
├─ GameData/                 Excel 원본, 공용 enum, 새 테이블 템플릿
├─ Generated/
│  ├─ Config/                생성된 C++ 설정 구조체·로더
│  ├─ GameData/              Schema, 서버 YAML, 클라이언트 JSON, C++/C# 코드
│  ├─ Packets/               C++/C# 패킷·handler·router
│  └─ Rpc/                   C++ RPC descriptor와 method catalog
├─ Infra/                    Login/Redis 및 GameDB/AuctionDB Docker 구성
├─ Libraries/                Network, Contents, Foundation, Connector, RPC 등 공용 계층
├─ LoginServer/              Node.js 계정·일회용 ticket 발급 서버
├─ Packet/                   패킷 YAML 스키마
├─ Rpc/                      RPC YAML과 `rpc-schema.lock.json`
├─ scripts/                  생성, 설정, benchmark, smoke, 유지보수 스크립트
├─ SmokeTests/               C++/C# 단위·통합 smoke test
├─ Tools/                    Packet/Config/RPC/GameData 생성기
├─ World/                    WorldCore, 서버, C# UI·더미 클라이언트
└─ ThirdParty/               재현 가능한 빌드에 필요한 외부 구성 요소
```

`Generated`의 자동 생성 산출물(`.h`, `.cpp`, `.g.cs`, YAML, JSON)은 직접 수정하지 않습니다. 입력 YAML·Excel 또는 생성기를 변경한 뒤 다시 생성합니다. `GeneratedPackets.csproj`처럼 빌드 연결을 위한 프로젝트 파일은 수동으로 유지합니다.

## 빌드

Visual Studio Developer PowerShell에서 실행합니다.

```powershell
msbuild .\Portfolio.sln /restore /m /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64
```

환경 구성과 서버 실행 순서는 [QUICK_START.md](QUICK_START.md)를 참고하십시오.

## 문서

- [Quick Start](QUICK_START.md)
- [Coding Conventions](CODING_CONVENTIONS.md)
- [Benchmark Runner](docs/BENCHMARK_RUNNER.md)
- [PacketGenerator](docs/PACKET_GENERATOR.md)
- [ConfigGenerator](docs/CONFIG_GENERATOR.md)
- [RpcGenerator](docs/RPC_GENERATOR.md)
- [GameDataGenerator](docs/GAME_DATA_GENERATOR.md)
- [Third-party Notices](THIRD_PARTY_NOTICES.md)
