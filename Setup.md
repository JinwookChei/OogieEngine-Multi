# Setup — 새 PC에서 이어서 작업하기

> git으로 옮겨지지 않는 것들을 새 PC(또는 오랜만에 쓰는 PC)에서 다시 준비하는 체크리스트.
> **처음 한 번**은 전부, **이후에는 "매번 할 일"만** 하면 된다.

---

## 매번 할 일 (작업 시작 / 종료)

```bash
# 시작할 때
git pull

# 끝낼 때
git add <파일들>      # git add . 대신 파일 지정 권장
git commit
git push
```

> 다른 PC에서 작업한 뒤 push를 잊으면 진행 상황(Plan.md 체크박스, CLAUDE.md 결정사항)이 어긋난다.

---

## 처음 한 번: PC 준비

### 1. 저장소 받기
```bash
git clone https://github.com/JinwookChei/OogieEngine-Multi.git
cd OogieEngine-Multi
git switch <작업 중인 브랜치>     # Plan.md 맨 위 "📍 현재 진행 상황"의 작업 브랜치 (예: feature/phase1-servercore)
```

### 2. git에 없는 파일 복사 (USB / 개인 클라우드 / 강의 사이트)

| 대상 | 넣을 위치 | 비고 |
|---|---|---|
| 강의 소스 | `MMOServer_example/` | 유료 강의 코드 → **절대 커밋 금지** (`.gitignore`로 제외됨) |
| Protobuf lib | `Server/Libraries/Libs/Protobuf/Debug/libprotobufd.lib`<br>`Server/Libraries/Libs/Protobuf/Release/libprotobuf.lib` | 강의 소스의 `Libraries/Libs/Protobuf/`에서 복사. Debug lib이 100MB 이상이라 GitHub에 올릴 수 없음 |
| FBX SDK 2020.3.11 | `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.11` | OogieEngine(Client) 빌드용. 설치 방법은 `README.md` 참고 |

> Protobuf 헤더(`Server/Libraries/Include/`)는 git에 포함되어 있으므로 복사할 필요 없다.

### 3. 도구 설치
- [ ] Visual Studio 2022 + "C++를 사용한 데스크톱 개발" 워크로드
- [ ] Python 3 + 패키지: `pip install jinja2 pyinstaller` (PacketGenerator용, Phase 2부터 사용)
- [ ] Claude Code

### 4. Visual Studio 로컬 설정 (`*.user` / `.vs/`에 저장되어 git에 없음)

#### Server (`Server/Server.sln`)
- [ ] 상단 구성이 **`Debug | x64`** 인지 확인
- [ ] **여러 시작 프로젝트 설정** (`Server.slnLaunch.user`에 저장되어 git에서 제외됨)
  1. 솔루션 우클릭 → **시작 프로젝트 설정**
  2. **여러 시작 프로젝트** 선택
  3. `GameServer` → **시작**, `DummyClient` → **시작** (GameServer가 위로 오도록 순서 조정)
  4. `ServerCore` → 없음

#### Client (`Client/OogieEngine.sln`)
- [ ] **`Content`** 를 시작 프로젝트로 설정 (우클릭 → 시작 프로젝트로 설정)
- [ ] 첫 빌드 때 NuGet(DirectXTex)이 자동 복원되는지 확인 (인터넷 필요)

### 5. 빌드 확인
- [ ] `Server.sln` → `Debug | x64` 솔루션 빌드 성공
- [ ] `Server.sln` → `Release | x64` 솔루션 빌드 성공
- [ ] F5 → GameServer / DummyClient 콘솔 2개가 뜬다
- [ ] `OogieEngine.sln` → `Debug | x64` 빌드 후 Content 실행

> 빌드가 **lib을 찾지 못해서** 실패하면 → 2번(Protobuf lib 복사) 먼저 확인.
> ServerCore.lib은 빌드할 때 자동 생성되므로 복사할 필요 없다.

### 6. Claude Code로 이어가기
저장소 루트에서 `claude`를 실행한 뒤 다음처럼 시작한다.
```
CLAUDE.md, Plan.md, Setup.md 읽고 이어서 진행하자
```
> Claude의 대화 기록과 메모리는 PC마다 따로 저장되어 넘어오지 않는다.
> 맥락은 **CLAUDE.md**(결정사항), 진행 상황은 **Plan.md**(체크박스)로만 공유된다.
> 새로 정한 내용은 그때그때 이 두 파일에 반영하고 push한다.

---

## git에 안 올라가는 것 요약

| 항목 | 이유 | 새 PC에서 |
|---|---|---|
| `MMOServer_example/` | 유료 강의 저작권 | 직접 복사 |
| `Server/Libraries/Libs/Protobuf/` | 용량 (100MB 초과) | 직접 복사 |
| `Server/Libraries/Libs/ServerCore/` | 빌드 결과물 | 빌드하면 생성 |
| `Server/Binary/` | 빌드 결과물 | 빌드하면 생성 |
| `*.pb.h`, `*.pb.cc`, `*PacketHandler.h` | 자동 생성 코드 | 빌드(Pre-Build)하면 생성 |
| `*.user`, `*.slnLaunch.user`, `.vs/` | VS 개인 설정 | 4번 다시 설정 |
| Claude 대화 기록 / 메모리 | PC 로컬 저장 | CLAUDE.md + Plan.md로 대체 |

> 이 목록에 새 항목이 생기면 (예: 새 외부 라이브러리, 새 VS 개인 설정) **이 파일에 바로 추가**한다.
