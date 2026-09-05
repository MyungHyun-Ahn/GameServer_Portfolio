# RpcGenerator

`Tools/RpcGenerator`는 `Rpc` 아래의 `*.rpc.yaml` 또는 `*.rpc.yml`을 검증해 RpcLib에서 사용하는 C++ method descriptor와 전체 method catalog를 생성하는 .NET 9 도구입니다. 일반 YAML 파일은 입력에서 제외합니다. 호출 timeout은 wire schema가 아니라 호출부 또는 서버 Config에서 정합니다.

## 스키마

```yaml
schema-version: 1
namespace: Example::Protocol
output: Example/ExampleRpcMethods.h

service:
  name: Example
  id: 42
  reserved-method-ids: [9]

aliases:
  - { name: FUserId, type: uint64 }

enums:
  - name: EResult
    underlying: uint8
    values:
      - { name: Success, value: 0 }
      - { name: Failed, value: 1 }

structs:
  - name: FPayload
    fields:
      - { name: text, type: string }
      - { name: values, type: vector<uint32> }

methods:
  - name: Query
    id: 1
    routing-key: userId
    request:
      fields:
        - { name: userId, type: FUserId }
    response:
      fields:
        - { name: result, type: EResult }
        - { name: payload, type: FPayload }

  - name: Changed
    id: 2
    routing-key: userId
    noti:
      fields:
        - { name: userId, type: FUserId }
```

한 method는 다음 형태를 가질 수 있습니다.

- Request/Response: `request`와 `response`를 함께 정의
- Notification: `noti`만 정의
- Request/Response + Notification: 세 endpoint가 같은 Service/Method ID를 공유

Request/Response는 `F<Name>Rpc::FRequestArguments`와 `FResponseArguments`, Notification은 `F<Name>Noti::FArguments`로 생성됩니다.

지원 타입은 `bool`, 고정 폭 정수, `float`, `double`, 소유형 `string`, `bytes`, `vector<T>`와 같은 문서의 alias·enum·struct입니다. 비동기 전송 이후 수명을 보장할 수 없는 `string_view`와 `bytes_view`는 허용하지 않습니다.

## ID와 routing 계약

- Service ID는 전체 스키마에서 고유하고 Method ID는 해당 Service 안에서 고유해야 합니다.
- ID는 자동 배정하지 않으며 YAML에 명시합니다.
- `routing-key`는 Request와 Notification에 존재하는 같은 이름의 unsigned 정수 필드를 가리켜야 합니다.
- 송신 측은 `FRpcTarget.routingKey`와 payload 값을 비교하고, 수신 dispatcher도 역직렬화 후 다시 검사합니다.
- routing 값이 다른 Request는 `InvalidPayload` 응답, Notification은 응답 없는 로컬 실패로 처리됩니다.

Notification에는 Request ID, Response, pending call, timeout, 완료 callback이 없습니다. `Notify()` 성공은 transport가 전송을 수락했다는 의미이며 원격 handler 성공을 뜻하지 않습니다.

## lock 파일과 reserved ID

`Rpc/rpc-schema.lock.json`은 각 Service/Method ID의 endpoint별 `필드명:wire 타입` 순서와 `routing-key`를 기록합니다. 기존 ID에서 타입, 필드 순서 또는 routing key를 바꾸면 생성기가 wire signature 변경으로 거부합니다.

메서드를 제거할 때는 ID를 재사용하지 말고 Service의 `reserved-method-ids`로 옮깁니다. 생성이 성공하면 YAML과 lock 파일을 함께 반영합니다.

## 생성

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Rpcs.ps1 `
  -Configuration Release
```

도구 빌드만 수행하려면 `-BuildOnly`를 사용합니다. Git에 저장된 header, catalog, manifest와 lock을 갱신하지 않고 현재 YAML에서 같이 재현되는지 검사하려면 `-Check`를 사용합니다. 검사 중 도구의 `bin`/`obj` 빌드 산출물은 갱신될 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Rpcs.ps1 `
  -Configuration Release -Check
```

도구를 직접 실행할 때는 다음 옵션을 사용할 수 있습니다.

```powershell
dotnet run --project .\Tools\RpcGenerator\RpcGenerator.csproj -c Release -- `
  --schema-root .\Rpc `
  --output-root .\Generated\Rpc `
  --lock-file .\Rpc\rpc-schema.lock.json
```

## 생성 결과

```text
Generated/Rpc/
├─ Cache/CacheRpcMethods.h
├─ ServerProtocol/UserPresenceRpcMethods.h
├─ RpcMethodCatalog.h
└─ .rpc-generator-manifest.json
```

### 실제 생성 예시: CachePing

실제 `Rpc/Cache/Cache.rpc.yaml`에는 Request/Response와 Notification을 함께 가진 다음 method가 있습니다.

```yaml
- name: CachePing
  id: 1
  routing-key: userId
  request:
    fields:
      - { name: sequence, type: uint64 }
      - { name: userId, type: uint64 }
      - { name: clientTimeUnixMs, type: uint64 }
  response:
    fields:
      - { name: sequence, type: uint64 }
      - { name: userId, type: uint64 }
      - { name: clientTimeUnixMs, type: uint64 }
      - { name: serverTimeUnixMs, type: uint64 }
      - { name: shardIndex, type: uint32 }
      - { name: shardCount, type: uint32 }
      - { name: contentInstanceId, type: uint64 }
      - { name: workerThreadId, type: uint32 }
  noti:
    fields:
      - { name: sequence, type: uint64 }
      - { name: userId, type: uint64 }
      - { name: clientTimeUnixMs, type: uint64 }
```

이 정의는 `Generated/Rpc/Cache/CacheRpcMethods.h`에 다음 descriptor를 생성합니다.

```cpp
struct FCachePingRpc final
{
    static constexpr RpcLib::Protocol::FRpcServiceId kServiceId =
        kCacheServiceId;
    static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
    static constexpr const char* kName = "Cache.CachePing";
    static constexpr bool kHasRoutingKey = true;
    static constexpr std::size_t kRoutingKeyArgumentIndex = 1;

    using FRequestArguments =
        std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    using FResponseArguments =
        std::tuple<std::uint64_t, std::uint64_t, std::uint64_t,
            std::uint64_t, std::uint32_t, std::uint32_t,
            std::uint64_t, std::uint32_t>;
};

struct FCachePingNoti final
{
    static constexpr RpcLib::Protocol::FRpcServiceId kServiceId =
        kCacheServiceId;
    static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
    static constexpr const char* kName = "Cache.CachePing";
    static constexpr bool kHasRoutingKey = true;
    static constexpr std::size_t kRoutingKeyArgumentIndex = 1;

    using FArguments =
        std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
};
```

전체 catalog인 `Generated/Rpc/RpcMethodCatalog.h`에도 다음 행이 추가됩니다.

```cpp
{1, 1, "Cache.CachePing", "userId", true, true},
```

실제 입력과 생성 파일의 관계는 다음과 같습니다.

| 입력·출력 | 실제 파일 | 내용 |
|---|---|---|
| 입력 | `Rpc/Cache/Cache.rpc.yaml` | Cache service의 enum, struct와 22개 method |
| 입력 | `Rpc/ServerProtocol/UserPresence.rpc.yaml` | Game↔Cache presence 계약 |
| Cache 출력 | `Generated/Rpc/Cache/CacheRpcMethods.h` | 직렬화 가능한 타입과 Request/Response/Notification descriptor |
| Presence 출력 | `Generated/Rpc/ServerProtocol/UserPresenceRpcMethods.h` | presence method descriptor |
| 전체 입력 통합 | `Generated/Rpc/RpcMethodCatalog.h` | Service/Method ID, routing key와 endpoint 종류 |
| 생성물 목록 | `Generated/Rpc/.rpc-generator-manifest.json` | stale 생성 파일 정리 대상 |

YAML의 enum은 `enum class`로, struct는 필드와 `Serialize`/`Deserialize`를 가진 C++ 타입으로 생성됩니다. RPC method는 이름 있는 Request 객체가 아니라 YAML 필드 순서와 정확히 대응하는 tuple descriptor로 생성됩니다.

## 생성 영역과 직접 구현 영역

RpcGenerator는 transport proxy나 서버 stub을 생성하지 않습니다.

| 생성기가 만드는 것 | 직접 구현해야 하는 것 |
|---|---|
| alias, enum, serializable struct | business model 변환과 업무 결과 처리 |
| Service/Method ID와 이름 | server type·instance와 `FRpcTarget` 선택 |
| routing-key 인자 위치 | routing key 값과 shard/content routing |
| Request/Response tuple descriptor | timeout, 성공·실패 callback과 재시도 정책 |
| Notification tuple descriptor | `Register`/`RegisterNotification` handler |
| 전체 method catalog와 manifest | Request/Response/Notification wire dispatch |
| lock 파일의 wire signature | peer 인증·권한·멱등성·로그·thread ownership |

## 호출 구현

### Request/Response

`Cache/CacheRpcPingClient/Main.cpp`와 같은 호출 흐름은 다음과 같습니다.

```cpp
RpcLib::Protocol::FRpcTarget target;
target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
target.serverInstanceId = cacheInstanceId;
target.routingKey = userId;

const auto callResult = rpcCommon.Call<Cache::Protocol::FCachePingRpc>(
    target,
    std::chrono::seconds(3),
    [](std::uint64_t responseSequence,
        std::uint64_t responseUserId,
        std::uint64_t responseClientTimeUnixMs,
        std::uint64_t serverTimeUnixMs,
        std::uint32_t shardIndex,
        std::uint32_t shardCount,
        std::uint64_t contentInstanceId,
        std::uint32_t workerThreadId)
    {
        // 성공 RPC payload 처리
    },
    [](const RpcLib::Protocol::FRpcCallFailure& failure)
    {
        // timeout, disconnect, remote/protocol 오류 처리
    },
    sequence,
    userId,
    clientTimeUnixMs);

if (!callResult.accepted)
{
    // 세션 선택, 인자, 직렬화 또는 send 단계에서 시작하지 못한 호출 처리
}
```

- 마지막 가변 인자는 YAML Request 필드 순서와 생성된 `FRequestArguments` 타입이 정확히 같아야 하며 컴파일 타임에 검사됩니다.
- `target.routingKey`는 YAML의 `routing-key` 필드 값과 같아야 합니다.
- `accepted == false`이면 호출이 시작되지 않았으며 failure callback도 호출되지 않습니다.
- `accepted == true` 이후 성공 응답은 success callback, timeout·disconnect·remote 오류는 failure callback으로 전달됩니다.
- `FRpcCallFailure::error`는 local call 상태이고 `remoteResponseCode`는 dispatcher가 돌려준 wire 오류입니다.
- `ECacheQueryResult` 같은 업무 결과 enum은 정상 RPC payload이므로 success callback 안에서 별도로 판정합니다.
- timeout은 원격 업무가 실행되지 않았다는 보장이 아닙니다. 상태 변경 RPC는 request token·version 같은 멱등성 근거 없이 무조건 재시도하지 않습니다.

### Notification

Notification은 같은 target에 `Notify`로 전송합니다.

```cpp
const auto notifyResult =
    rpcCommon.Notify<Cache::Protocol::FCachePingNoti>(
        target,
        sequence,
        userId,
        clientTimeUnixMs);

if (!notifyResult.accepted)
{
    // 로컬 session 선택, 직렬화 또는 transport 전송 실패 처리
}
```

Notification에는 Response, pending call, timeout과 completion callback이 없습니다. `accepted`는 선택된 transport가 전송을 수락했다는 뜻이며 원격 handler 성공을 보장하지 않습니다.

## 수신부 구현

### Request handler 등록과 응답

수신 Content가 소유한 `FRpcCommon`에 생성 descriptor와 handler를 등록합니다.

```cpp
const bool registered =
    m_rpcCommon.Register<Cache::Protocol::FCachePingRpc>(
        [this](
            RpcLib::Dispatch::TRpcReply<
                Cache::Protocol::FCachePingRpc>& reply,
            std::uint64_t sequence,
            std::uint64_t userId,
            std::uint64_t clientTimeUnixMs)
        {
            const std::uint64_t serverTimeUnixMs =
                GetUnixTimeMilliseconds();
            const std::uint32_t workerThreadId =
                static_cast<std::uint32_t>(GetCurrentThreadId());

            reply.Send(
                sequence,
                userId,
                clientTimeUnixMs,
                serverTimeUnixMs,
                m_shardIndex,
                m_shardCount,
                m_contentInstanceId,
                workerThreadId);
        });

if (!registered)
{
    throw std::runtime_error("CachePing RPC registration failed.");
}
```

요청 peer를 검사해야 하면 handler의 첫 인자로 `const FRpcCallContext&`를 받습니다. 아래는 `TMethod` placeholder를 사용한 형식 예시입니다.

```cpp
m_rpcCommon.Register<TMethod>(
    [this](
        const RpcLib::Dispatch::FRpcCallContext& context,
        RpcLib::Dispatch::TRpcReply<TMethod>& reply,
        /* YAML Request 필드 순서의 인자 */)
    {
        // context.peerServerType/peerServerInstanceId와 routingKey 검사
        // 업무 로직 실행
        reply.Send(/* YAML Response 필드 순서의 값 */);
    });
```

`reply.Send(...)`의 인자도 `FResponseArguments`와 정확히 일치해야 합니다. handler가 반환되기 전에 한 번 호출해야 하며, 호출하지 않으면 `HandlerDidNotReply`, handler 예외는 `HandlerException` 응답이 됩니다. 두 번째 `Send`는 실패합니다. 등록 실패는 보통 같은 Service/Method의 중복 등록이므로 서버 시작을 중단해야 합니다.

### Notification handler 등록

```cpp
const bool registered =
    m_rpcCommon.RegisterNotification<Cache::Protocol::FCachePingNoti>(
        [this](
            const RpcLib::Dispatch::FRpcCallContext& context,
            std::uint64_t sequence,
            std::uint64_t userId,
            std::uint64_t clientTimeUnixMs)
        {
            HandlePingNotification(
                context,
                sequence,
                userId,
                clientTimeUnixMs);
        });

if (!registered)
{
    throw std::runtime_error("CachePing notification registration failed.");
}
```

Notification handler는 reply를 받지 않습니다. 역직렬화, routing 또는 handler 실행 실패는 수신 측 local 결과로만 남고 송신 측에 전달되지 않습니다.

### wire 수신과 timeout 연결

handler 등록만으로 network callback이 연결되지는 않습니다. 현재 서버처럼 수신 데이터를 owner Content mailbox로 복사한 다음 wire opcode에 따라 다음 API를 호출해야 합니다.

```cpp
// Response
RpcLib::Protocol::FRpcResponse response;
if (RpcLib::Protocol::DeserializeRpcResponse(payload, response))
{
    m_rpcCommon.ProcessResponse(rpcSessionId, response);
}

// Request
RpcLib::Protocol::FRpcRequest request;
if (RpcLib::Protocol::DeserializeRpcRequest(payload, request))
{
    const auto response =
        m_rpcCommon.DispatchRequest(rpcSessionId, request);
    if (!m_transport.SendResponse(rpcSessionId, response))
    {
        // 응답 전송 실패 기록 또는 session 정리
    }
}

// Notification
RpcLib::Protocol::FRpcNotification notification;
if (RpcLib::Protocol::DeserializeRpcNotification(payload, notification))
{
    m_rpcCommon.DispatchNotification(rpcSessionId, notification);
}

// Content frame/tick
m_rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());

// RPC session disconnect
m_rpcCommon.FailSession(
    rpcSessionId,
    RpcLib::Protocol::ERpcCallError::Disconnected);
```

이 네 dispatch·완료 API와 timeout/disconnect 처리는 같은 `FRpcCommon`을 소유한 Content thread에서 실행합니다. 실제 서버에서는 dispatch 전에 RPC handshake 완료 여부, 인증된 peer, routing key와 대상 shard도 검사합니다. 참고 구현은 `Cache/CacheServer/Contents/FPlayerCacheContent.cpp`이며, 호출 측 production 예시는 `Auction/AuctionHouseServer/Contents/Command/FAuctionCommandContent.cpp`입니다.

일반 생성은 내용이 바뀐 파일만 다시 쓰고, 이전 manifest에 등록된 stale header만 제거합니다. 출력은 UTF-8 BOM 없이 저장됩니다. 생성 header와 manifest를 직접 수정하지 않습니다.

## 검증

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Test-RpcGenerator.ps1 `
  -Configuration Release

msbuild .\SmokeTests\RpcLibSmokeTest\RpcLibSmokeTest.vcxproj /m `
  /p:Configuration=Release /p:Platform=x64
.\Out\RpcLibSmokeTest\Release\RpcLibSmokeTest.exe
```

`Test-RpcGenerator.ps1`은 golden output, 선언 의존 순서, `-Check`, 변경 파일만 쓰는 정책, stale 정리와 잘못된 입력 거부를 확인합니다. `RpcLibSmokeTest`는 Call/Response/Noti, pending call, timeout, target·routing 검증을 확인합니다.
