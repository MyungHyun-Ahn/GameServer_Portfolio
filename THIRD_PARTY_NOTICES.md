# Third-party components

이 저장소는 빌드 재현을 위해 다음 외부 구성 요소의 헤더와 바이너리를 포함합니다.

- cpp_redis
- tacopie
- MySQL C API client 8.0.46

이 구성 요소는 작성자의 구현으로 주장하지 않습니다. 각 구성 요소의 원본 라이선스와 버전은 함께 제공하거나 패키지 관리자/서브모듈 기반으로 복원해야 합니다.

## MySQL Community Server 8.0.46 - C API client

The C++ database connector uses Oracle's official MySQL C API client library (`libmysql`). Only the headers, import library, runtime DLL, OpenSSL runtime dependencies, and license required by the client are included.

- Project: https://dev.mysql.com/downloads/mysql/
- Binary package: `mysql-8.0.46-winx64.zip`
- Included path: `ThirdParty/mysql-client-8.0.46`
- License: GNU General Public License, version 2, with the Universal FOSS Exception
- OpenSSL runtime license: Apache License 2.0
- License file: `ThirdParty/mysql-client-8.0.46/LICENSE`
