# C++ Coding Conventions

이 문서는 `GameServerPortfolio`의 C++ 코드에 공통으로 적용하는 규칙을 정의한다. 규칙의 목적은 스타일 통일뿐 아니라 네트워크·콘텐츠·DB 스레드의 소유권과 실행 경계를 코드에서 명확하게 드러내는 것이다.

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
| 클래스 | `F` + PascalCase | `FRioServer` |
| 구조체 | `S` + PascalCase | `SServerConfig` |
| 인터페이스 | `I` + PascalCase | `IServer` |
| 열거형 | `E` + PascalCase | `EBackendKind` |
| 템플릿 타입 | `T` + PascalCase | `TPacket` |
| 함수 | PascalCase | `StartWorkers()` |
| 지역 변수·매개변수 | camelCase | `sessionId` |
| 멤버 변수 | `m_` + camelCase | `m_isRunning` |
| 상수 | `k` + PascalCase | `kMaxSendBatchCount` |
| bool | 상태 또는 질문을 나타내는 이름 | `isRunning`, `HasPendingWork()` |

- 약어도 타입과 함수명에서는 단어처럼 취급한다. 예: `FIocpServer`, `GetRioSendMetrics()`.
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
- 구체 Content, Registry, Service, Repository와 Generated 코드는 PCH에 넣지 않는다.
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
- 로그는 `Foundation::ILogger::Log()`를 사용한다.
- format overload는 가독성이 좋아지는 경우 사용한다. 기존 문자열 조합과 `std::ostringstream`도 복잡한 값 표현이나 기존 코드와의
  일관성이 더 좋은 경우 허용하며, 형식 선택 자체를 결함으로 판정하지 않는다.
- 로그에는 `sessionId`, `userId`, `listingId`, `workerIndex`처럼 동작을 추적할 수 있는 식별자를 포함한다.
- 인증 토큰, 비밀번호와 같은 비밀 값은 기록하지 않는다.

```cpp
Log(Foundation::ELogLevel::Warn,
	"send queue is growing. sessionId={} queuedBytes={}",
	sessionId,
	queuedBytes);
```

## 7. 비동기와 스레드 안전성

- 세션 상태는 지정된 Owner Thread에서 변경한다.
- 콘텐츠 상태는 해당 Content Thread의 single-writer 규칙을 따른다.
- 네트워크 worker에서 동기 DB 작업을 실행하지 않는다.
- DB connection은 연결을 획득한 콘텐츠 스레드에 귀속한다.
- 공유 상태를 추가할 때 mutex, atomic, owner-thread 위임 중 어느 방식으로 보호하는지 명확히 한다.
- mutex는 `std::lock_guard` 또는 `std::unique_lock`으로 관리한다.
- atomic은 기본 `seq_cst` 사용을 허용한다. 완화된 memory order는 알고리즘의 happens-before 관계가 명확하고 성능상 필요할 때만
  명시하며, x64 전용 구현에서 memory order 미지정 자체를 결함으로 판정하지 않는다.
- 락을 보유한 상태에서 외부 콜백, 네트워크 전송, DB 호출을 수행하지 않는다.
- queue 제한은 개수인지 byte 크기인지 이름과 타입에서 구분한다.

## 8. 네트워크와 패킷

- wire format은 직접 중복 구현하지 않고 PacketGenerator의 생성 타입을 사용한다.
- 수신 payload는 역직렬화 성공과 길이를 검증한 후 사용한다.
- 세션 종료, backpressure, slow consumer 정책은 로그와 결과 코드로 원인을 구분한다.
- borrowed packet view는 유효 범위를 벗어나 보관하지 않는다.
- IOCP와 RIO가 동일한 `IServer` 계약을 유지하도록 backend 전용 동작이 콘텐츠 계층으로 새지 않게 한다.

## 9. 콘텐츠와 DB

- 패킷 라우팅은 인증된 `userId`를 기준으로 동일 shard에 직렬화한다.
- 콘텐츠 객체는 DB connection을 소유하지 않고 현재 콘텐츠 스레드의 DB context에서 획득한다.
- Primary 쓰기와 Replica 읽기를 구분하고, Replica 연결 실패 시에만 정의된 Primary fallback을 사용한다.
- GameDB Stored Procedure는 `sp_gd_{crud}_{name}` 형식을 사용한다.
- AuctionDB Stored Procedure는 `sp_ad_{crud}_{name}` 형식을 사용한다.
- `{crud}`에는 `c`, `r`, `u`, `d` 또는 복합 동작인 `cu`처럼 실제 동작을 표시한다.
- 두 DB에 걸친 부분 성공은 성공으로 숨기지 않고 식별 가능한 오류 로그를 남긴다.

## 10. 주석과 문서

- 코드가 무엇을 하는지 반복하는 주석보다 선택 이유, 불변식, 스레드 경계를 기록한다.
- 임시 우회는 `TODO`만 남기지 말고 제거 조건 또는 관련 문서를 함께 명시한다.
- public API의 이름만으로 수명·스레드·오류 계약이 불분명하면 선언부에 짧게 설명한다.
- 구현과 문서가 다르면 구현을 기준으로 문서를 즉시 갱신한다.
