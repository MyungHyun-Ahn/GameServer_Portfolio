# ConfigGenerator

`Tools/ConfigGenerator`는 `ConfigSchema`의 YAML 스키마를 읽어 typed C++ 설정 구조체, 검증 로더와 실행용 YAML 템플릿을 생성하는 .NET 9 도구입니다.

설정 항목을 추가할 때 구조체·파싱·기본값·enum 검증 코드를 반복해서 수정하는 문제를 줄이기 위해 만들었습니다.

## 사용 순서

1. `ConfigSchema/<Category>/<Target>.schema.yaml`에 설정을 정의합니다.
2. 생성 스크립트를 실행합니다.
3. 생성된 `F<Target>ConfigLoader`로 YAML을 읽습니다.
4. 반환된 typed config 값을 서버·클라이언트 초기화에 사용합니다.

## 스키마 예시

```yaml
EchoServer:
  Backend:
    type: enum
    default: Iocp
    values: [Iocp, Rio, BoostAsio]
  BindIp: { type: string, default: 127.0.0.1, required: true }
  Port: { type: uint16, default: 19000, required: true }
  WorkerThreadCount: { type: int32, default: 2 }

Debug:
  Headless: { type: bool, default: false }
```

지원 타입은 `bool`, `int32`, `uint16`, `uint32`, `int64`, `uint64`, `float`, `double`, `string`, `enum`입니다.

## 실행

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Configs.ps1 `
  -Configuration Release
```

빌드만 확인하려면 `-BuildOnly`를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate\Generate-Configs.ps1 `
  -Configuration Release -BuildOnly
```

도구를 직접 호출하면 각 루트를 변경할 수 있습니다.

```powershell
dotnet run --project .\Tools\ConfigGenerator\ConfigGenerator.csproj -c Release -- `
  --schema-root .\ConfigSchema `
  --output-root .\Generated\Config `
  --config-root .\Config
```

## 생성 결과

`ConfigSchema/Server/EchoServer.schema.yaml`을 예로 들면 다음 파일이 생성됩니다.

```text
Generated/Config/EchoServer/EchoServerConfig.h
Generated/Config/EchoServer/EchoServerConfig.cpp
Config/Server/EchoServer.yaml
```

- `*Config.h`: enum과 typed 설정 구조체, loader 선언
- `*Config.cpp`: section/key 검사와 타입별 읽기·enum 검증
- `Config/**/*.yaml`: 기본값을 반영한 실행용 설정 템플릿

애플리케이션 코드는 생성된 loader를 호출하고 typed 값을 사용하면 됩니다.

```cpp
Generated::Config::EchoServer::SEchoServerConfig config{};
std::string error;
if (!Generated::Config::EchoServer::FEchoServerConfigLoader::LoadFromFile(path, config, error))
{
	return false;
}

serverConfig.port = config.EchoServer.Port;
```

생성 파일과 실행용 YAML을 직접 고치는 대신 스키마의 default·required·enum 값을 변경하고 다시 생성합니다.

