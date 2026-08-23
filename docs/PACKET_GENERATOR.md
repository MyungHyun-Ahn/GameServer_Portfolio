# PacketGenerator

`Tools/PacketGenerator`는 `Packet` 아래의 YAML 스키마를 읽어 C++ 패킷 타입, 직렬화·역직렬화 코드, handler와 opcode router를 생성하는 .NET 9 도구입니다.

## 사용 순서

1. `Packet/<Content>/<Content>.yaml`에 패킷을 정의합니다.
2. 생성 스크립트를 실행합니다.
3. 생성된 handler base를 상속하거나 인터페이스를 구현해 콘텐츠 로직을 연결합니다.
4. 스키마와 생성 결과를 함께 반영합니다.

## 스키마 예시

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

지원 endpoint는 `Rq/Rp`, `Noti`, `Broadcast`입니다. 요청을 정의할 때는 응답도 함께 정의해야 합니다.

지원 타입:

- `bool`, 고정 폭 정수, `float`, `double`
- `string`, `string_view`
- `bytes`, `bytes_view`
- `vector<T>`, `array<T, N>`
- `map<K, V>`, `unordered_map<K, V>`

## 실행

저장소 루트에서 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Packets.ps1 `
  -Configuration Release
```

빌드만 확인하려면 `-BuildOnly`를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Packets.ps1 `
  -Configuration Release -BuildOnly
```

도구를 직접 호출하면 입력과 출력 경로를 바꿀 수 있습니다.

```powershell
dotnet run --project .\Tools\PacketGenerator\PacketGenerator.csproj -c Release -- `
  --schema-root .\Packet `
  --output-root .\Generated\Packets
```

## 생성 결과

```text
Generated/Packets/
├─ PacketRouter.h
└─ <Content>/
   ├─ <Content>Packets.h
   └─ <Content>PacketHandler.h
```

- `<Content>Packets.h`: opcode, 패킷 타입, serialize/deserialize
- `<Content>PacketHandler.h`: typed handler와 dispatcher base
- `PacketRouter.h`: 전체 content opcode router

생성 파일은 직접 수정하지 않습니다. 패킷 필드는 YAML을, 출력 형식은 `Tools/PacketGenerator/Program.cs`를 수정한 뒤 다시 생성합니다.

## 검증 항목

생성 전에 다음 오류를 검사합니다.

- 빈 content·message 이름
- 0 이하 opcode
- 스키마 내부 및 전체 스키마 사이의 opcode 중복
- Rq/Rp 쌍 누락
- 지원하지 않는 필드 타입
- 잘못된 container 타입 인자

현재 자동 생성 대상은 C++ 서버 코드입니다. C# WinForms 클라이언트의 packet codec은 클라이언트 프로젝트에서 별도로 구현되어 있습니다.
- C# 패킷 코드 생성 기능 추후 구현 예정
