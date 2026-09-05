# PacketGenerator

`Tools/PacketGenerator`는 `Packet/**/*.yaml`을 검증한 뒤 동일한 protocol에서 C++과 C# 패킷 코드, handler, 통합 opcode router를 함께 생성하는 .NET 9 도구입니다.

## 스키마

```yaml
content: Echo

messages:
  - name: Echo
    rq:
      opcode: 1000
      fields:
        - { name: message, type: string_view }
    rp:
      opcode: 1001
      fields:
        - { name: message, type: string_view }
    noti:
      opcode: 1002
      fields:
        - { name: message, type: string_view }
```

- endpoint는 `rq`, `rp`, `noti`, `broadcast`를 지원합니다.
- Request를 정의하면 Response도 같은 message에 함께 정의해야 합니다.
- opcode는 전체 스키마에서 고유한 양수여야 합니다.
- 스키마는 경로순으로 처리하므로 생성 순서가 결정적입니다.

지원하는 scalar는 `bool`, 8/16/32/64비트 signed·unsigned 정수, `float`, `double`, `string`, `string_view`, `bytes`, `bytes_view`입니다. `vector<T>`와 `array<T, N>`도 지원합니다. 쉼표가 있는 고정 배열 타입은 `type: "array<float, 3>"`처럼 YAML 문자열로 감쌉니다.

C++ 형식에는 `map`과 `unordered_map` 표현이 있지만 현재 C# 동시 생성 경로에서는 두 형식과 `vector<bool>`을 거부합니다. 두 언어가 함께 빌드되는 스키마에는 사용하지 않습니다. C++의 borrowed `string_view`·`bytes_view`는 C#에서 소유형 `string`·`byte[]`로 생성됩니다.

## 실행

저장소 루트에서 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Packets.ps1 `
  -Configuration Release
```

스크립트는 PacketGenerator를 빌드·실행한 뒤 생성된 C# contract 프로젝트도 빌드합니다. 생성 없이 도구 빌드만 확인하려면 `-BuildOnly`를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Packets.ps1 `
  -Configuration Release -BuildOnly
```

입력과 출력 루트를 바꿔 직접 실행할 수도 있습니다.

```powershell
dotnet run --project .\Tools\PacketGenerator\PacketGenerator.csproj -c Release -- `
  --schema-root .\Packet `
  --output-root .\Generated\Packets
```

## 생성 결과

```text
Generated/Packets/
├─ Cpp/
│  ├─ PacketRouter.h
│  └─ <Content>/
│     ├─ <Content>Packets.h
│     └─ <Content>PacketHandler.h
└─ CSharp/
   ├─ GeneratedPackets.csproj              # 생성 코드를 묶는 수동 유지 프로젝트
   ├─ PacketRouter.g.cs
   └─ <Content>/
      ├─ <Content>Packets.g.cs
      └─ <Content>PacketHandler.g.cs
```

생성기는 C++ `*.h`와 C# `*.g.cs`만 생성합니다. `GeneratedPackets.csproj`는 생성 대상이 아니라 생성된 C# 코드를 묶는 수동 유지 프로젝트이며, `Generate-Packets.ps1`가 해당 프로젝트의 존재와 빌드를 검증합니다. 이 프로젝트는 `CSharp/ClientNetwork/ClientNetwork.csproj`를 참조합니다. 생성 패킷은 C++ NetworkLib와 C# ClientNetwork가 공유하는 최대 8192-byte frame, 암복호화, checksum 계약 위에서 동작합니다.

### 실제 생성 예시: Echo

`Packet/Echo/Echo.yaml`의 `Echo` Request/Response/Notification은 다음 파일에 반영됩니다.

| 입력·출력 | 실제 파일 | 생성 내용 |
|---|---|---|
| 입력 | `Packet/Echo/Echo.yaml` | opcode 1000/1001/1002와 `message: string_view` 계약 |
| C++ 패킷 | `Generated/Packets/Cpp/Echo/EchoPackets.h` | `FEchoRq`, `FEchoRp`, `FEchoNoti`와 직렬화 코드 |
| C++ handler | `Generated/Packets/Cpp/Echo/EchoPacketHandler.h` | interface, 기본 handler, opcode dispatcher, 전송 helper |
| C# 패킷 | `Generated/Packets/CSharp/Echo/EchoPackets.g.cs` | `EchoRq`, `EchoRp`, `EchoNoti`와 body codec |
| C# handler | `Generated/Packets/CSharp/Echo/EchoPacketHandler.g.cs` | interface, 기본 handler와 opcode dispatcher |
| 전체 router | `Generated/Packets/Cpp/PacketRouter.h`, `Generated/Packets/CSharp/PacketRouter.g.cs` | Echo handler slot과 세 opcode 분기 |

생성된 C++ Request의 핵심 형태는 다음과 같습니다.

```cpp
namespace Generated::Echo
{
    class FEchoRq final
        : public NetworkLib::Packet::Serialization::IContentPacket
    {
    public:
        static constexpr std::uint16_t kOpcode = 1000;

        void SetMessageValue(std::string_view value) noexcept;
        std::string_view GetMessageValue() const noexcept;

        std::size_t GetEstimatedBodySize() const noexcept override;
        void Serialize(
            NetworkLib::Packet::Serialization::FPacketWriter& writer) const override;
        bool Deserialize(
            NetworkLib::Packet::Serialization::FPacketReader& reader) override;
    };
}
```

같은 YAML에서 C#에는 다음 형태가 생성됩니다.

```csharp
namespace Generated.Packets.Echo;

public sealed class EchoRq : IContentPacket
{
    public const ushort OpcodeValue = 1000;
    public ushort Opcode => OpcodeValue;
    public EContentPacketKind PacketKind => EContentPacketKind.Request;
    public string Message { get; set; } = string.Empty;

    public void SerializeBody(FPacketWriter writer);
    public static bool TryDeserializeBody(
        ReadOnlySpan<byte> body,
        out EchoRq? packet);
}
```

위 코드는 구조를 보여 주기 위해 method body를 생략한 발췌입니다. 실제 생성 파일에는 body 크기, trailing byte, borrowed view 수명 검사와 컬렉션 필드가 있을 때의 원소 수 검사가 포함됩니다.

## 생성 코드와 직접 구현 영역

| 영역 | 생성기 | 개발자 |
|---|---|---|
| wire 계약 | opcode, packet kind, 필드 순서와 타입을 코드에 반영 | YAML에서 호환 가능한 ID·필드 계약 설계 |
| packet codec | C++/C# 모델과 직렬화·역직렬화 생성 | 생성 파일을 직접 편집하지 않음 |
| dispatch | content별 handler interface/base와 전체 opcode router 생성 | 필요한 `Handle...` override와 handler 등록 |
| transport | 생성하지 않음 | 연결, send queue, receive loop와 router 호출 구현 |
| 요청 수명 | 생성하지 않음 | request ID, pending 요청, timeout·취소·재접속 처리 |
| 업무 규칙 | 생성하지 않음 | 인증·권한·입력 검증·상태 변경·오류 응답·로그 구현 |
| thread 경계 | 생성하지 않음 | UI callback queue 또는 Content mailbox 소유권 유지 |

생성된 handler base의 기본 `Handle...`은 `false`를 반환합니다. 필요한 endpoint만 상속해 구현하고, 해당 handler를 router에 등록해야 패킷이 소비됩니다.

### C# 클라이언트 연결 예시

현재 C# 클라이언트와 같은 연결 방식의 최소 예시는 다음과 같습니다. 세션 연결과 receive loop 시작은 이 코드보다 먼저 완료되어 있어야 합니다.

```csharp
using ClientNetwork.Transport;
using Generated.Packets;
using Generated.Packets.Echo;

private readonly FClientSession m_session = new();
private readonly PacketRouter m_packetRouter = new();

private void RegisterHandlers()
{
    m_packetRouter.SetEchoHandler(new EchoResponseHandler());
}

private async Task SendEchoAsync(CancellationToken cancellationToken)
{
    bool accepted = await m_session.SendAsync(
        new EchoRq { Message = "hello" },
        cancellationToken);
    if (!accepted)
    {
        throw new InvalidOperationException("Not connected.");
    }
}

private async Task ReceivePacketsAsync(CancellationToken cancellationToken)
{
    while (await m_session.WaitToReadPacketAsync(cancellationToken))
    {
        while (m_session.TryDequeuePacket(out FReceivedPacket packet))
        {
            if (!m_packetRouter.DispatchPacket(packet.Opcode, packet.Body.Span))
            {
                throw new InvalidDataException(
                    $"Unhandled or invalid packet: {packet.Opcode}");
            }
        }
    }
}

private sealed class EchoResponseHandler : EchoPacketHandlerBase
{
    public override bool HandleEchoRp(EchoRp packet)
    {
        Console.WriteLine(packet.Message);
        return true;
    }
}
```

실제 구현에서는 receive loop의 connection generation 검사, callback 직렬화와 종료 순서도 함께 유지해야 합니다. 참고 구현은 `Chatting/ChattingClientWinForms/Networking/ChattingTcpClient.cs`와 `World/WorldClientCore/Networking/WorldTcpClient.cs`입니다.

### C++ ContentRuntime 서버 연결 예시

현재 서버는 Content mailbox에서 session과 route generation을 검증해야 하므로 생성된 전체 `FPacketRouter` 대신 content별 opcode switch를 사용합니다. 패킷 타입과 codec은 그대로 생성 코드를 사용합니다.

```cpp
switch (opcode)
{
    case Generated::Echo::FEchoRq::kOpcode:
        HandleEchoRequest(sessionId, payload, bridge);
        break;
}

void HandleEchoRequest(
    const std::uint64_t sessionId,
    const std::span<const char> payload,
    ContentsRuntime::Bridge::IContentBridge& bridge)
{
    Generated::Echo::FEchoRq request;
    NetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;
    if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(
            Generated::Echo::FEchoRq::kOpcode,
            payload,
            borrowedViewScope,
            request))
    {
        return;
    }

    std::string responseMessage(request.GetMessageValue());
    Generated::Echo::FEchoRp response;
    response.SetMessageValue(responseMessage);
    ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
}
```

C++에서 역직렬화된 `string_view`와 `bytes_view` 필드는 dispatch/borrowed scope 동안만 유효합니다. callback, mailbox, 비동기 작업 또는 컨테이너에 보관할 때는 `std::string`이나 `std::vector` 같은 소유형으로 복사합니다. 송신 packet의 `Set...Value`에 전달한 view는 원본 owned buffer가 살아 있는 동안만 유효하므로, 그 값도 전송 호출이 끝날 때까지 유지합니다.

생성기는 현재 스키마에 없는 오래된 `.h`와 `.g.cs` 생성물을 정리하되 `bin`·`obj`는 건드리지 않습니다. 생성 파일을 직접 수정하지 말고 YAML 또는 생성기를 변경한 뒤 다시 실행합니다.

## 검증

생성 전에 다음 항목을 검사합니다.

- content·message·field 이름과 언어별 예약어 충돌
- opcode 누락·범위·전체 중복
- Request/Response 쌍
- 타입과 generic 인자
- 생성되는 C# 이름 충돌
- 고정 배열의 최소 크기와 8192-byte frame 한도

C# wire와 adapter 연동은 다음 smoke test로 확인합니다.

```powershell
dotnet run --project .\SmokeTests\ClientNetworkCSharpSmokeTest\ClientNetworkCSharpSmokeTest.csproj -c Release
dotnet run --project .\SmokeTests\GeneratedPacketCSharpSmokeTest\GeneratedPacketCSharpSmokeTest.csproj -c Release
dotnet run --project .\SmokeTests\ClientAdaptersCSharpSmokeTest\ClientAdaptersCSharpSmokeTest.csproj -c Release
```
