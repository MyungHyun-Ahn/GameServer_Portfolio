# Third-party components

이 저장소는 빌드 재현과 데모 실행을 위해 다음 외부 구성 요소를 사용합니다. 각 구성 요소는 작성자의 구현으로 주장하지 않으며 아래 upstream 라이선스를 따릅니다.

## cpp_redis and tacopie

C++ Redis ticket 저장소는 cpp_redis와 그 네트워크 의존성 tacopie를 사용합니다. 저장소에는 Windows x64 Debug/Release 정적 라이브러리가 `ThirdParty/lib/cpp_redis` 아래 포함됩니다.

- Project: [cpp_redis](https://github.com/Cylix/cpp_redis)
- Dependency: [tacopie](https://github.com/Cylix/tacopie)
- License: MIT

## MySQL Community Server 8.0.46 - C API client

C++ database connector는 Oracle의 공식 MySQL C API client `libmysql`을 사용합니다. client 빌드와 실행에 필요한 header, import library, runtime DLL, OpenSSL runtime dependency 및 라이선스만 포함합니다.

- Project: [MySQL Community Downloads](https://dev.mysql.com/downloads/mysql/)
- Binary package: `mysql-8.0.46-winx64.zip`
- Included path: `ThirdParty/mysql-client-8.0.46`
- License: GNU General Public License, version 2, with the Universal FOSS Exception
- OpenSSL runtime license: Apache License 2.0
- License file: `ThirdParty/mysql-client-8.0.46/LICENSE`

## ClosedXML 0.105.1

`GameDataGenerator`는 Excel `.xlsx` 원본을 읽고 검증하기 위해 ClosedXML을 사용합니다.

- Project: [ClosedXML](https://github.com/ClosedXML/ClosedXML)
- Package: [ClosedXML 0.105.1 on NuGet](https://www.nuget.org/packages/ClosedXML/0.105.1)
- Referenced by: `Tools/GameDataGenerator/GameDataGenerator.csproj`
- License: MIT

ClosedXML의 transitive NuGet dependencies에는 각 패키지의 라이선스가 별도로 적용됩니다.

## YamlDotNet 16.3.0

PacketGenerator, ConfigGenerator와 RpcGenerator는 YAML 스키마와 설정을 읽고 검증하기 위해 YamlDotNet을 사용합니다.

- Project: [YamlDotNet](https://github.com/aaubry/YamlDotNet)
- NuGet: [YamlDotNet 16.3.0](https://www.nuget.org/packages/YamlDotNet/16.3.0)
- Referenced by:
  - `Tools/PacketGenerator/PacketGenerator.csproj`
  - `Tools/ConfigGenerator/ConfigGenerator.csproj`
  - `Tools/RpcGenerator/RpcGenerator.csproj`
- License: MIT

## Ninja Adventure - Asset Pack

`WorldClientWinForms`는 Pixel-boy와 AAA의 Ninja Adventure Asset Pack에 포함된 sprite sheet를 사용합니다.

- Project: [Ninja Adventure - Asset Pack](https://pixel-boy.itch.io/ninja-adventure-asset-pack)
- Included files:
  - `World/WorldClientWinForms/Assets/NinjaGreenIdle.png`
  - `World/WorldClientWinForms/Assets/NinjaGreenWalk.png`
  - `World/WorldClientWinForms/Assets/Actor/Monsters/TrainingSlime.png`
  - `World/WorldClientWinForms/Assets/Actor/Monsters/TrainingBoss.png`
- License: Creative Commons Zero v1.0 Universal (CC0 1.0)
- License text: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)
