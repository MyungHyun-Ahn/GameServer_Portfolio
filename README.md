# Game Server Portfolio

Windows C++20 기반 게임 서버 프로젝트입니다. 하나의 서버 인터페이스에서 IOCP와 RIO를 선택할 수 있는 네트워크 라이브러리, mailbox 기반 콘텐츠 실행 환경, C++ MySQL/Redis 연결 계층과 이를 사용한 경매장 서버를 구현했습니다.

포트폴리오 PDF는 설계 배경과 측정 결과를 설명하고, 이 저장소는 해당 설명을 확인할 수 있는 전체 코드와 실행에 필요한 최소 문서만 제공합니다.

## 주요 구현

| 영역 | 구현 내용 | 대표 위치 |
|---|---|---|
| 네트워크 | IOCP, RIO Direct, RIO Owner Thread, 세션 수명, packet framing, backpressure | `Libraries/NetworkLib` |
| 비동기 콘텐츠 | Content mailbox, single-writer 실행, instance routing, shard, 세션 이동 순서 보장 | `Libraries/ContentsRuntime` |
| 공통 기반 | 로깅, 설정 로더, 실행 시간 계측, TLS 수집기 | `Libraries/Foundation` |
| 외부 저장소 | Redis ticket consume, MySQL transaction, thread-affined Primary/Replica 연결과 복구 | `Libraries/Connector` |
| 경매장 서버 | 인증, 인벤토리, 등록, 조회, 입찰, 즉시 구매, 취소, 만료, 우편, 정산 | `Auction/AuctionHouseServer` |
| DB | GameDB/AuctionDB 분리, Stored Procedure, Primary/Replica 구성 | `Auction/Database`, `Infra` |
| 코드 생성 | YAML 패킷 스키마와 설정 스키마에서 C++ 코드 생성 | `Tools`, `scripts/generate` |
| 검증 | 더미 클라이언트, 동시성·장애 복구 스모크, 벤치마크 실행기 | `Auction/AuctionDummyClient`, `SmokeTests`, `scripts` |

## 디렉터리 구성

```text
Portfolio/
├─ Libraries/
│  ├─ NetworkLib/          IOCP/RIO 서버와 세션
│  ├─ ContentsRuntime/     콘텐츠 실행·라우팅
│  ├─ Foundation/          로깅·설정·계측
│  ├─ Connector/           Redis/MySQL 연결
│  ├─ GameData/            아이템·경매 정책 데이터
│  └─ ClientNetworkLib/    더미·테스트 클라이언트 네트워크
├─ Auction/
│  ├─ AuctionHouseServer/  C++ 경매장 서버
│  ├─ AuctionDummyClient/  기능·부하 테스트 클라이언트
│  ├─ AuctionClientWinForms/ C# 확인용 UI
│  └─ Database/            GameDB/AuctionDB SQL
├─ Echo/                   네트워크 왕복 테스트
├─ Chatting/               콘텐츠 런타임 사용 예제
├─ Contents/               Echo·Chat 콘텐츠
├─ LoginServer/            계정·ticket 발급용 Node.js 서버
├─ Packet/                 패킷 YAML 스키마
├─ ConfigSchema/           설정 YAML 스키마
├─ Config/                 실행 설정과 게임 데이터
├─ Generated/              생성된 C++ 패킷·설정 코드
├─ Tools/                  PacketGenerator·ConfigGenerator
├─ scripts/                생성·벤치마크·스모크 스크립트
├─ SmokeTests/             자료구조·계측 검증
├─ Infra/                  Docker Compose와 초기화 스크립트
└─ ThirdParty/             빌드에 필요한 외부 라이브러리
```

## 코드 검토 순서

### 1. 공통 네트워크 계약과 백엔드 선택

- `Libraries/NetworkLib/Servers/IServer.h`
- `Libraries/NetworkLib/Servers/IApplicationHandler.h`
- `Libraries/NetworkLib/Servers/Core/FServerFactory.cpp`
- `Libraries/NetworkLib/Servers/Core/FIocpServer.cpp`
- `Libraries/NetworkLib/Servers/Core/FRioServer.cpp`

### 2. IOCP/RIO 세션과 송수신 정책

- `Libraries/NetworkLib/Servers/Session/FIocpSession.cpp`
- `Libraries/NetworkLib/Servers/Session/FRioSession.cpp`
- `Libraries/NetworkLib/Diagnostics/Rio/FRioSendMetricsRuntime.cpp`
- `Libraries/NetworkLib/Packet/View`

### 3. 콘텐츠 실행 환경

- `Libraries/ContentsRuntime/Routing/FContentRuntime.cpp`
- `Libraries/ContentsRuntime/Threading/FContentThread.cpp`
- `Libraries/ContentsRuntime/Core/IContent.h`
- `Libraries/ContentsRuntime/Core/FContentInstanceIdAllocator.cpp`

### 4. C++ DB 연결과 Replica 정책

- `Libraries/Connector/MySql/FMySqlConnection.cpp`
- `Libraries/Connector/MySql/FMySqlTransaction.cpp`
- `Libraries/Connector/MySql/FThreadAffinedMySqlCluster.cpp`
- `Auction/AuctionHouseServer/Database/FContentThreadDbContext.cpp`

### 5. 경매장 기능

- `Auction/AuctionHouseServer/Application/FAuctionHouseServerBootstrap.cpp`
- `Auction/AuctionHouseServer/Contents/FAuctionContentRouter.cpp`
- `Auction/AuctionHouseServer/Contents/Command/FAuctionCommandContent.cpp`
- `Auction/AuctionHouseServer/Service`
- `Auction/AuctionHouseServer/Database`
- `Auction/Database`

### 6. 자동화와 검증

- `Tools/PacketGenerator`
- `Tools/ConfigGenerator`
- `scripts/bench/Run-Benchmark.ps1`
- `scripts/auction`
- `SmokeTests`

## 빌드와 실행

초기 환경 구성과 실행 순서는 [QUICK_START.md](QUICK_START.md)를 참고하십시오.

도구 사용법:

- [Benchmark Runner](docs/BENCHMARK_RUNNER.md)
- [PacketGenerator](docs/PACKET_GENERATOR.md)
- [ConfigGenerator](docs/CONFIG_GENERATOR.md)

코드 규칙은 [CODING_CONVENTIONS.md](CODING_CONVENTIONS.md)에 정리했습니다.