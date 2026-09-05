# C++ Coding Conventions

이 문서는 `Portfolio`의 C++ 코드에 공통으로 적용하는 규칙을 정의한다. 규칙의 목적은 스타일 통일뿐 아니라 네트워크·콘텐츠·RPC·DB 스레드의 소유권과 실행 경계를 코드에서 명확하게 드러내는 것이다.

## 1. 언어와 파일

- C++20과 MSVC v143을 기준으로 한다.
- 클래스 또는 주요 타입 하나를 중심으로 `.h`와 `.cpp`를 구성한다.
- 파일명은 주요 타입명과 일치시킨다. 예: `FRioServer.h`, `FRioServer.cpp`.
- 서버 실행 파일의 `Main.cpp`는 Bootstrap 함수 호출만 담당한다.
- 더미 클라이언트와 테스트 클라이언트의 `Main.cpp`는 시나리오 구성이 단순할 때 직접 실행 흐름을 포함할 수 있다.
- 외부 라이브러리 원본은 포맷 및 컨벤션 적용 대상에서 제외한다.

## 2. 이름 규칙

| 대상 | 규칙 | 예시 |
|---|---|---|
| 런타임 객체·서비스·공개 계약 타입 (`class`/`struct` 무관) | `F` + PascalCase | `FRioServer`, `FRpcTarget` |
| 내부 상태·옵션·스냅샷 등 단순 값 묶음 | `S` + PascalCase | `SServerConfig` |
| 인터페이스 | `I` + PascalCase | `IServer` |
| 열거형 | `E` + PascalCase | `EBackendKind` |
| 템플릿 타입 | `T` + PascalCase | `TPacket` |
| 함수 | PascalCase | `StartWorkers()` |
| 지역 변수·매개변수 | camelCase | `sessionId` |
| 멤버 변수 | `m_` + camelCase | `m_isRunning` |
| 상수 | `k` + PascalCase | `kMaxSendBatchCount` |
| bool | 상태 또는 질문을 나타내는 이름 | `isRunning`, `HasPendingWork()` |

- 약어도 타입과 함수명에서는 단어처럼 취급한다. 예: `FIocpServer`, `GetRioSendMetrics()`.
- `F`와 `S`는 C++의 `class`/`struct` 문법이 아니라 타입의 역할을 기준으로 구분한다.
- 단위가 중요한 값은 이름에 단위를 포함한다. 예: `timeoutMilliseconds`, `queuedBytes`.
- 의미 없는 `data`, `value`, `temp`는 범위가 매우 짧고 의미가 명확할 때만 사용한다.

## 3. 포맷

- 저장소 루트의 `.clang-format`을 기준으로 한다.
- 들여쓰기는 폭 4의 탭을 사용한다.
- 중괄호는 Allman 형식을 사용한다.
- 한 줄은 최대 140자를 기준으로 한다.
- 생성자 초기화 목록은 선행 쉼표 형식을 사용한다.
- include는 자동 정렬하지 않는다. PCH-first와 의존성 순서를 보존해야 한다.

```cpp
FContentThread::FContentThread(
	Bridge::IContentBridge& bridge,
	const Core::SContentRuntimeConfig& config)
	: m_bridge(bridge)
	, m_config(config)
{
}
```

```powershell
# 포맷 적용 및 검사
powershell -ExecutionPolicy Bypass -File scripts/maintenance/Format-Cpp.ps1
powershell -ExecutionPolicy Bypass -File scripts/maintenance/Format-Cpp.ps1 -Check
```

## 4. 헤더와 PCH

- 프로젝트 소유 일반 `.h`에는 `#include`를 작성하지 않는다.
- 헤더 include는 프로젝트별 `*Pch.h`에서만 허용한다.
- 모든 `.cpp`의 첫 include는 해당 프로젝트 PCH여야 한다.
- 포인터나 참조만 필요한 타입은 전방 선언한다.
- 부모 인터페이스, 값 멤버, inline/template 본문에 필요한 안정 타입은 프로젝트 PCH에 둔다.
- 구체 Content, Registry, Service, Repository와 Generated Packet·Config·GameData 코드는 PCH에 넣지 않는다.
- 변경이 드물고 lock 파일로 wire signature를 보호하는 Generated RPC contract는 프로젝트 PCH에 둘 수 있다.
- MySQL 및 Redis SDK는 사용하는 구현 `.cpp`에서 include한다.
- 다른 프로젝트의 PCH를 include하지 않는다.

세부 검사는 `scripts/maintenance/Test-HeaderIncludePolicy.ps1`과 `Test-PchPolicy.ps1`로 자동화한다.

## 5. API와 소유권

- 단독 소유권은 `std::unique_ptr`로 표현한다.
- 실제 공유 수명이 필요한 객체에만 `std::shared_ptr`를 사용한다.
- 일반 애플리케이션 코드의 raw pointer는 소유하지 않는 nullable 참조에 사용한다.
- 네트워크 라이브러리의 커스텀 메모리 풀, intrusive 자료구조와 lock-free 컨테이너는 raw pointer 소유를 허용한다. 이 경우 풀의
  `Acquire`/`Release` 경계와 더미 노드처럼 알고리즘이 의도적으로 유지하는 메모리의 수명을 구현 또는 테스트에서 명확히 한다.
- null을 허용하지 않는 비소유 인자는 참조를 우선한다.
- 복사 또는 이동이 의미 없는 런타임 객체는 생성자와 대입 연산자를 명시적으로 삭제한다.
- 단일 인자 변환 생성자는 `explicit`로 선언한다.
- 결과를 무시하면 안 되는 함수에는 `[[nodiscard]]` 적용을 검토한다.
- 예외를 내보내지 않는 종료·정리 함수에는 `noexcept`를 사용한다.

## 6. 오류 처리와 로깅

- 예상 가능한 런타임 실패는 `bool` 또는 도메인 결과 코드로 반환한다.
- 상세 원인이 필요한 경계에서는 `std::string& outError`를 함께 사용한다.
- 예외는 초기화 실패나 복구할 수 없는 내부 불변식 위반처럼 예외적인 상황에 제한한다.
- 예외를 잡고 무시하지 않는다. 복구하거나, 문맥을 기록한 뒤 다시 throw하거나, 명확한 실패 코드로 변환한다.
- 로그는 `Foundation::ILogger::Log()`에 level, category와 message를 전달한다.
- format overload, 문자열 조합과 `std::ostringstream` 중 값의 의미와 기존 코드의 흐름을 가장 명확하게 드러내는 형식을 선택한다.
- 로그에는 `sessionId`, `userId`, `listingId`, `workerIndex`처럼 동작을 추적할 수 있는 식별자를 포함한다.
- 인증 토큰, 비밀번호와 같은 비밀 값은 기록하지 않는다.

```cpp
logger.Log(
	Foundation::ELogLevel::Warn,
	"Network",
	"send queue is growing. sessionId={} queuedBytes={}",
	sessionId,
	queuedBytes);
```

## 7. 비동기와 스레드 안전성

- IOCP 세션은 atomic·lock-free 상태와 generation·reference count 수명 경계로 여러 completion worker의 동시 접근을 보호한다.
- RIO Direct 전송 모드는 send ring을 세션별 mutex로 보호하고, RIO OwnerThread 전송 모드는 send ring 변경을 지정된 owner worker에 위임한다.
- 콘텐츠 상태는 네트워크 backend와 독립적으로 해당 Content Thread의 mailbox에서 변경하며 single-writer 규칙을 따른다.
- 네트워크 worker에서 동기 DB 작업을 실행하지 않는다.
- DB connection은 연결을 획득한 콘텐츠 스레드에 귀속한다.
- 공유 상태를 추가할 때 mutex, atomic, owner-thread 위임 중 어느 방식으로 보호하는지 명확히 한다.
- mutex는 `std::lock_guard` 또는 `std::unique_lock`으로 관리한다.
- atomic은 기본 `seq_cst`를 사용한다. 완화된 memory order는 알고리즘의 happens-before 관계와 성능상 필요성이 명확할 때 명시한다.
- 락을 보유한 상태에서 외부 콜백, 네트워크 전송, DB 호출을 수행하지 않는다.
- queue 제한은 개수인지 byte 크기인지 이름과 타입에서 구분한다.
- RPC 응답 callback은 임의의 Worker에서 애플리케이션 상태를 바꾸지 않고 원 요청 Content mailbox로 재진입한다.
- Sector Worker는 live entity·sector 상태를 직접 변경하지 않고 Result/Intent만 만든다. 4 Wave 실행과 최종 상태 반영은 Map Owner 경계를 지킨다.

## 8. 네트워크와 패킷

- wire format은 직접 중복 구현하지 않고 PacketGenerator와 RpcGenerator의 생성 타입을 사용한다.
- 수신 payload는 역직렬화 성공과 길이를 검증한 후 사용한다.
- 세션 종료, backpressure, slow consumer 정책은 로그와 결과 코드로 원인을 구분한다.
- borrowed packet view는 유효 범위를 벗어나 보관하지 않는다.
- IOCP와 RIO가 동일한 `IServer` 계약을 유지하도록 backend 전용 동작이 콘텐츠 계층으로 새지 않게 한다.

## 9. 콘텐츠와 DB

- 패킷 라우팅은 인증된 `userId`를 기준으로 동일 shard에 직렬화한다.
- 콘텐츠 객체는 DB connection을 소유하지 않고 현재 콘텐츠 스레드의 DB context에서 획득한다.
- Primary 쓰기와 Replica 읽기를 구분하고, Replica 연결 실패 시에만 정의된 Primary fallback을 사용한다.
- 서비스 코드에서 GameDB를 직접 읽거나 변경하는 경계는 CacheServer로 한정한다. AuctionHouseServer는 AuctionDB만 직접 처리하고 GameDB 작업은 CacheServer RPC로 요청한다.
- GameDB Stored Procedure는 `sp_gd_{crud}_{name}` 형식을 사용한다.
- AuctionDB Stored Procedure는 `sp_ad_{crud}_{name}` 형식을 사용한다.
- `{crud}`에는 `c`, `r`, `u`, `d` 또는 복합 동작인 `cu`처럼 실제 동작을 표시한다.
- 일회성 스키마 보정 프로시저는 `sp_gd_migrate_{name}` 또는 `sp_ad_migrate_{name}` 형식을 사용하고 migration 완료 후 제거한다.
- 두 DB에 걸친 부분 성공은 성공으로 숨기지 않고 식별 가능한 오류 로그를 남긴다.

## 10. 주석과 문서

- 코드가 무엇을 하는지 반복하는 주석보다 선택 이유, 불변식, 스레드 경계를 기록한다.
- 임시 우회는 `TODO`만 남기지 말고 제거 조건 또는 관련 문서를 함께 명시한다.
- public API의 이름만으로 수명·스레드·오류 계약이 불분명하면 선언부에 짧게 설명한다.
- 구현과 문서가 다르면 구현을 기준으로 문서를 즉시 갱신한다.
- Packet·Config·RPC·GameData의 자동 생성 산출물(`.h`, `.cpp`, `.g.cs`, YAML, JSON)은 직접 수정하지 않고 입력 YAML·Excel 또는 생성기를 수정한 뒤 재생성한다. `GeneratedPackets.csproj`처럼 빌드 연결을 위한 프로젝트 파일은 수동으로 유지한다.
