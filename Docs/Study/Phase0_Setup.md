# 🛠️ Phase 0. 환경 준비

> 💡 **목표**: 빈 GameServer가 ServerCore.lib을 링크해서 x64 Debug/Release로 빌드되는 상태를 만든다.

---

## 1. 솔루션 구조

```
Server/
├ Server.sln
├ ServerCore/     Static Library (.lib)  ← 네트워크/스레드/메모리 등 엔진 공통 기능
├ GameServer/     Console (.exe)         ← 실제 게임 서버 (ServerCore 사용)
├ DummyClient/    Console (.exe)         ← 접속/부하 테스트용 봇 (ServerCore 사용)
├ Libraries/
│  ├ Include/     Protobuf 헤더 (커밋 대상)
│  └ Libs/        ServerCore.lib 출력, Protobuf lib (git 제외)
└ Binary/         exe 출력 (git 제외)
```

| 결정 | 이유 |
|---|---|
| ServerCore를 **static lib**으로 | GameServer, DummyClient, 나중에는 **클라이언트(OogieEngine)** 까지 같은 코드를 재사용 |
| **x64 단일** 구성 | Win32 구성을 지워서 설정 실수를 줄임. Protobuf lib도 x64만 준비 |
| C++17, `/MP`, `/utf-8` | 병렬 빌드, 소스를 UTF-8로 해석 (한글 주석 깨짐 방지) |
| `.editorconfig` (UTF-8 BOM) | VS에서 새 파일을 만들 때 인코딩을 자동으로 맞춤 |

---

## 2. exe가 ServerCore.lib을 링크하는 방법

`GameServer/pch.h`
```cpp
#pragma once
#define WIN32_LEAN_AND_MEAN     // windows.h에서 잘 안 쓰는 부분을 빼서 빌드 속도 향상 + winsock 충돌 방지

#ifdef _DEBUG
#pragma comment(lib, "ServerCore\\Debug\\ServerCore.lib")
#else
#pragma comment(lib, "ServerCore\\Release\\ServerCore.lib")
#endif

#include "CorePch.h"
```
- `#pragma comment(lib, ...)`: 프로젝트 설정 대신 **코드에서 링크할 lib을 지정**한다. 라이브러리 검색 경로는 `Libraries/Libs/`.
- Debug/Release lib을 따로 링크한다. CRT(`/MDd` vs `/MD`)가 다르면 링크 오류나 런타임 크래시가 난다.

---

## 3. git에 올라가지 않는 것 (PC마다 직접 준비)

| 항목 | 위치 | 이유 |
|---|---|---|
| 강의 소스 | `MMOServer_example/` | 유료 강의 코드 → 절대 커밋 금지 |
| Protobuf lib | `Server/Libraries/Libs/Protobuf/{Debug,Release}/` | Debug lib이 100MB 이상 (GitHub 한도 초과) |
| FBX SDK 2020.3.11 | 시스템 설치 | 클라이언트 빌드용 |
| VS 개인 설정 | `*.user`, 여러 시작 프로젝트 | 사용자별 설정 |

> 📌 전체 절차는 저장소 루트의 `Setup.md` 참고. 빌드가 lib 누락으로 실패하면 이 표부터 확인한다.
