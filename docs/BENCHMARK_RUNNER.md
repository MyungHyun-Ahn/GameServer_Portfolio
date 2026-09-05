# Benchmark Runner

`scripts/bench/Run-Benchmark.ps1`은 YAML manifest에 정의한 순서대로 ChattingServer와 ChattingDummyClient를 실행하고, 설정 snapshot·로그·RTT·성공 판정 결과를 한 디렉터리에 수집합니다.

## 실행 전 준비

Release x64로 ChattingServer와 ChattingDummyClient를 빌드합니다.

```powershell
msbuild .\Chatting\ChattingServer\ChattingServer.vcxproj /restore /m /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64

msbuild .\Chatting\ChattingDummyClient\ChattingDummyClient.vcxproj /restore /m /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64
```

## 기본 실행

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bench\Run-Benchmark.ps1 `
  -Manifest .\scripts\bench\manifests\chatting-smoke.yaml `
  -OutputLabel smoke
```

기본 결과 경로는 `Out/bench/<timestamp>_<label>`입니다. `Out`은 Git에서 제외됩니다.

## Manifest 구성

```yaml
Scenario: Chatting
OutputRoot: Out/bench
ContinueOnError: false

Defaults:
  Timing:
    WarmupSeconds: 0
    MeasureSeconds: 5
    CooldownSeconds: 0
    StartupTimeoutSeconds: 10
    RunTimeoutSeconds: 20
  Server:
    Executable: Out/ChattingServer/Release/ChattingServer.exe
    ConfigTemplate: Config/Server/ChattingServer.yaml
    Overrides:
      ChattingServer.Port: 19100
      LoginAuth.Mode: Disabled
      Debug.Headless: true
  Client:
    Executable: Out/ChattingDummyClient/Release/ChattingDummyClient.exe
    ConfigTemplate: Config/Client/ChattingDummy.yaml
    Overrides:
      ChattingDummy.SessionCount: 8
      ChattingDummy.SendIntervalMs: 250

Runs:
  - Name: iocp_chatting_smoke
    Server:
      Overrides:
        ChattingServer.Backend: Iocp
```

- `Defaults`: 모든 run이 공유하는 시간·실행 파일·설정값입니다.
- `Runs`: 비교할 실행 순서입니다. 각 run의 값이 `Defaults`를 덮어씁니다.
- `RepeatCount`: 같은 run의 반복 횟수입니다.
- `ContinueOnError`: 실패 이후 다음 run을 계속할지 결정합니다.
- `Overrides`: 원본 YAML을 수정하지 않고 해당 run에만 적용할 `Section.Key` 값입니다.

IOCP/RIO 비교 manifest는 `scripts/bench/manifests`에 있습니다.

## 생성 결과

```text
Out/bench/<timestamp>_<label>/
├─ manifest.snapshot.yaml
├─ sequence-summary.csv
├─ sequence-summary.json
└─ <run-name>/
   ├─ effective.server.yaml
   ├─ effective.client.yaml
   ├─ run-summary.json
   ├─ server.stdout.log
   ├─ server.stderr.log
   ├─ server_logs/                                  # 서버 로그가 기록될 때 생성
   ├─ rtt.csv                                       # 클라이언트 RTT 수집 시 생성
   └─ client.stdout.log / client.stderr.log  # 실패 시 보존
```

실패 run이 있으면 최상위에 `failed-runs.txt`를 추가하고 프로세스 종료 코드는 1이 됩니다. 서버가 Ready 상태에 도달하기 전에 실패하면 `server_logs/`와 `rtt.csv`는 생성되지 않을 수 있습니다. 성공한 run의 client stdout/stderr는 제거하지만 결과 요약과 서버 계측 로그는 유지합니다.

## 성공 판정

다음 조건을 모두 만족해야 성공입니다.

- startup timeout 안에 서버 초기화 완료 후 stdout에 `[BenchmarkReady]` 준비 신호가 기록됨
- 서버 조기 종료 없음
- 클라이언트 timeout 없음
- 클라이언트 종료 코드 0
- 클라이언트 최종 summary 존재
- `permanentFailure`, `timeout`, `unexpectedDisconnect`, `sessionError`, `selfBroadcast`, `invalidRoomBroadcast`, `payloadValidationFailure`가 모두 0

단순히 실행 파일이 종료되었거나 결과 파일이 생성되었다는 이유만으로 성공 처리하지 않습니다.

현재 runner가 지원하는 Scenario는 `Chatting`입니다. 서버 stdout의 마지막 `[ChattingStats]`와 `[ContentStats]`, 세션이 존재하는 server sample의 평균·최대 TPS 및 CPU, client 최종 summary를 `run-summary.json`과 sequence summary에 기록합니다.
