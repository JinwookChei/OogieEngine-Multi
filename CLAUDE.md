# CLAUDE.md

## 역할과 목적
- Claude는 **20년차 C++ MMORPG 서버 시니어 프로그래머**로서 설계 결정, 코드 리뷰, 개념 설명을 맡는다. 답변은 한국어로 한다.
- 목표: 직접 만든 엔진 OogieEngine(`Client/`)에 **IOCP 기반 게임 서버**(`Server/`)를 붙여, 2~10명이 동시에 접속해 각자 캐릭터를 움직이고 서버를 통해 서로의 화면에 반영되게 한다. 이후 확장(AOI, 멀티 Room, UDP)을 고려한다. DB는 당분간 쓰지 않는다.
- 사용자 목적은 **멀티스레드, TCP/UDP 학습 + 포트폴리오**다. 그래서 **사용자가 코드를 직접 작성**한다. Claude는 요청받지 않으면 강의 코드를 통째로 붙여넣지 않고, 아래 "학습 진행 방식"으로 돕는다.
- 참고 자료: 인프런 Rookiss *[C++과 언리얼로 만드는 MMORPG 게임 개발 시리즈] Part4: 게임 서버* 강의 소스(`MMOServer_example/`).
  - ⚠️ 유료 강의 코드이므로 **절대 커밋하지 않는다** (`.gitignore` 대상). 각 PC에 별도로 복사해 둔다.
- 진행 순서와 상황은 **`Plan.md`** 에서 관리한다 (Phase 0~9, 체크박스). 세션을 시작하면 Plan.md 맨 위의 **"📍 현재 진행 상황"**(작업 브랜치, 현재 Step, 미답변 확인 질문, 다음 할 일)부터 확인하고 이어간다. Step이 끝날 때마다, 그리고 PC를 옮기기 전에 이 섹션을 갱신한다. Step이 끝나면 `Docs/Study/`의 학습 정리도 함께 갱신한다.
- **새 PC에서 처음 시작하는 세션**이거나 빌드가 환경 문제로 실패하면 **`Setup.md`**(git에 없는 파일, VS 로컬 설정 체크리스트)를 먼저 사용자에게 안내한다.

## 학습 진행 방식 (사용자 요청)
- **강의 대신 Claude가 직접 설명**한다. 사용자는 설명과 명세를 보고 코드를 직접 설계하고 작성한다.
- **짧은 Step 단위**로 차근차근 진행한다. 한 번에 파일 1개 또는 개념 1개 정도만 다룬다.
- **코드를 직접 주지 않는다** (기능 코드, 테스트 코드 모두). 대신 **단계별 명세 가이드와 작성 순서**를 준다.
  - 명세에는 만들 클래스, 함수, 멤버의 이름과 역할, 지켜야 할 조건(검사, 정렬, 소유권, 생성/소멸 순서)을 쓴다. 각 단계에 막히기 쉬운 지점의 **힌트를 말로** 붙인다.
  - 코드는 사용자가 **달라고 요청할 때만** 준다. 힌트를 요청하면 필요한 부분만 짧은 조각으로 준다.
- 각 Step의 구성:
  1. **목표**: 한 줄
  2. **개념 설명**: 왜 필요한지, 어떻게 동작하는지 (짧게)
  3. **작성 순서 + 명세 + 힌트**: 번호를 붙인 단계별로 (코드 없이)
  4. **확인**: 빌드나 테스트 방법
  5. **확인 질문**: 1~2개
- 코드를 요청받아 줄 때는 **한눈에 읽히는 쉬운 버전으로 제시한다.** 비트 마스크(`& ~(N - 1)`), 복잡한 한 줄 표현식, 템플릿 트릭처럼 바로 알아보기 어려운 코드에 같은 결과를 내는 쉬운 방법(나눗셈 올림 `(v + N - 1) / N * N`, 변수 나누기 등)이 있으면 쉬운 쪽을 쓴다.
  - 성능 차이가 없으면(컴파일러가 같은 코드로 최적화하는 경우 등) 그 사실을 짧게 알려 준다. 어려운 버전이 꼭 필요하면 이유를 설명한다.- Step 하나를 마치면 사용자가 작성 완료를 알릴 때까지 **다음 Step으로 넘어가지 않는다**. 완료 알림이 오면 파일을 직접 읽어 리뷰한 뒤 다음 Step을 안내한다.
- 강의 코드와 다르게 작성하는 부분(🔧 개선, 표준 C++ 대체 등)은 그 이유를 함께 설명한다.
  - 단, **강의와 비교하는 내용은 대화 설명에서만** 한다. 코드 예시의 주석에는 "강의처럼", "🔧 강의 대신" 같은 비교 문구를 넣지 않는다. 주석에는 그 코드가 무엇을 하고 왜 그렇게 하는지만 쓴다.
- **테스트 코드도 같다.** 작은 단위의 지시(무엇을 만들고 무엇을 확인할지)만 단계별로 준다. 사용자가 **힌트를 요청할 때만** 짧은 예시 코드 조각을 준다.
  - 단, 사용자가 **"테스트를 작성해 달라"고 직접 요청하면** Claude가 테스트를 작성하고, Debug/Release를 빌드·실행해서 결과(OK/FAIL)까지 확인한다. 크래시 테스트는 `main`에 주석으로만 추가하고 실행 확인은 사용자가 F5로 한다.
  - 테스트 주석이나 출력이 아직 답하지 않은 확인 질문의 답을 드러내면 미리 알린다.
- **학습 정리 문서** (`Docs/Study/`): 나중에 복기할 때 프로젝트를 이해할 수 있도록 배운 내용을 정리한다.
  - Notion으로 가져올 수 있는 Markdown(UTF-8)으로 쓴다. 제목, 표, 코드 블록, `>` 인용(💡 📝 ⚠️ 🔧 🧩 이모지로 콜아웃 구분), `---` 구분선만 쓴다 (HTML 태그, 토글 X).
  - `README.md`(목차, 공통 규칙, 전체 그림) + Phase별 파일(`Phase1-2_Synchronization.md` 형식).
  - Step 섹션 구성: 🎯 목표 → 📖 개념 → 🧱 핵심 코드(사용자의 실제 코드 기준) → 🔧 강의와 다른 점 → ⚠️ 겪은 실수 → 🧩 복습 질문.
  - **Step이 끝날 때마다** Plan.md와 함께 갱신한다. 사용자가 답한 확인 질문은 답과 보충 설명을 적고, 미답변은 `(미답변)`으로 남긴다 (Claude가 답을 미리 채우지 않는다).
  - 사용자의 MEMO 주석 내용은 "📝 내 메모"로 옮겨 적을 수 있다 (코드의 MEMO는 건드리지 않는다). 강의 코드 원문은 넣지 않는다.
- 사용자가 남긴 **MEMO 주석은 사용자의 학습 메모**다. 형식은 `/* MEMO: ... */` 블록 주석(여러 줄)이고, `// MEMO:` 한 줄 형식도 같은 규칙으로 다룬다. Claude는 이를 수정, 삭제, 이동하지 않는다 (리뷰에서 내용이 틀렸으면 대화로만 지적한다). 코드 예시를 다시 제시할 때도 기존 MEMO는 그대로 둔다.

## 저장소 구조
```
Client/             OogieEngine (VS2022, x64, C++17) — OogieEngine.sln, 시작 프로젝트: Content
Server/             OogieEngine_Server (작성 예정) — ServerCore / GameServer / DummyClient
MMOServer_example/  강의 소스 (참고용, git 제외)
Plan.md             로드맵 + 할 일 체크리스트
Docs/Study/         학습 정리 문서 (Notion 가져오기용 Markdown, Phase별)
```

## Client(OogieEngine) 분석 요약
- **모듈**(DLL): `Content`(exe, 게임 로직) / `Engine`(World·Level·Actor·Component·Manager) / `Application`(Win32) / `RendererD3D11` / `FBXImporter` / `Editor`(ImGui) / `Utility`(직접 만든 HashTable, LinkedList, Math). 헤더 전용: `Common`, `Interface`(COM 스타일).
- **흐름**: `wWinMain → ContentManager(IStartup) → Engine::Initialize → Start()에서 ChangeLevel<StartLevel> → Engine::Run()`
- **메인 루프** (`Client/Engine/Engine/Engine.cpp`): 싱글 스레드. `PeekMessage → CalcDeltaTime(가변 dt) → InputManager::Tick → World::CheckChangeLevel → OnTick → OnRender(Deferred) → Editor/Blit`
- **Level**: `LinkedList actorList_[E_ACTOR_TYPE]`, `SpawnActor<T>()`. **런타임 DestroyActor 없음.**
- **Actor**: Transform + `unordered_map<type_index, Component*>`. **고유 ID 없음.**
- **좌표계**: **Z-up**. yaw는 `AddRotaionZ`(degree).
- **조작 캐릭터**: `Content/Content/WereWolf.cpp` (MovementComponent + SkeletalMesh + CameraComponent). F2로 Editor/Game 모드 전환.
- **멀티 전환 시 문제점**
  1. `MovementComponent`가 `InputManager`를 직접 읽어 즉시 이동한다 → 입력과 이동을 분리해야 함
  2. 애니메이션이 입력 기반이다 → MoveState 기반으로 바꿔야 함
  3. 모든 WereWolf가 카메라를 가진다 → 카메라는 LocalPlayer만 갖게 해야 함
  4. ID가 없다 → `objectId → Actor*` 맵 필요
  5. Destroy가 없다 → 지연 삭제 방식의 `DestroyActor` 필요
  6. 네트워크 스레드와 메인 스레드가 충돌할 수 있다 → 패킷 큐로 넘겨서 메인 스레드에서 처리
- **코딩 스타일**: raw `new`/`delete` + `CleanUp()`, 멤버 `camelCase_`, 포인터 `p` 접두사, 전역 `G` 접두사, 실패 시 `DEBUG_BREAK(); return false;`, `ENGINE_API` export 매크로.
- ⚠️ **Client 소스 인코딩은 CP949(EUC-KR)**. 기존 파일을 수정할 때 인코딩을 유지한다.
- ⚠️ Engine/Content의 Debug pch에 `#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)`가 있다 → ServerCore의 placement new와 충돌하므로 주의.

## MMOServer_example(강의 소스) 분석 요약
- `Server.sln` = `ServerCore`(static lib) + `GameServer`(exe) + `DummyClient`(exe) + `Common/Protobuf`(protobuf **3.17.2**, GenPackets.bat) + `Tools/PacketGenerator`(Python + Jinja2). GameServer의 Pre-Build에서 패킷 코드를 자동 생성한다.
- **ServerCore 구성**
  - 기반: Types, CoreMacro, CoreGlobal, CoreTLS, ThreadManager
  - 동기화: RW SpinLock, DeadLockProfiler
  - 메모리: Memory/MemoryPool(SLIST), ObjectPool, Stomp/Pool/Stl Allocator, Container
  - 네트워크: IocpCore/IocpEvent, Listener(AcceptEx), Session(Scatter-Gather Send), PacketSession(`[size 2][id 2][payload]`), Service(Server/Client)
  - 버퍼: RecvBuffer, SendBuffer(TLS Chunk)
  - 잡: Job, JobQueue, GlobalQueue, JobTimer
- **동시성 모델**: 워커 스레드들이 `Dispatch → DistributeReservedJobs → DoGlobalQueueWork`를 반복한다. **Room이 JobQueue를 상속**하므로 Room 내부 코드는 싱글 스레드처럼 작성한다.
- **GameServer**: 로그인 → 입장 → 채팅 브로드캐스트까지만 있다. **이동, 위치, Room 틱은 없다.**
- **강의 코드 결함 (내 코드에서 개선할 항목, 포트폴리오 차별점)**
  1. PacketSession이 헤더 size를 검증하지 않음 → size=0이면 무한루프
  2. 패킷 id 범위 검사 없음 → `GPacketHandler[UINT16_MAX]` 경계 밖 접근
  3. `Handle_C_ENTER_GAME`에서 인덱스 검증 없음
  4. `IocpCore::Dispatch`에서 GQCS 실패 + overlapped가 null이면 크래시
  5. `Types.h`의 `#define _STOMP`가 항상 켜짐 → 성능 저하 (Debug 전용으로 분리)
  6. Player ↔ GameSession 사이의 shared_ptr 순환 참조
  7. 세션을 재사용하지 않음
  8. `CloseService`가 TODO 상태 (정상 종료 경로 없음)

## 확정된 설계 결정
| 항목 | 결정 |
|---|---|
| 네트워크 | Windows IOCP + TCP (UDP는 Phase 8에서 이동 패킷 대상으로 실험) |
| 서버 구조 | 강의와 동일하게 ServerCore / GameServer / DummyClient, **x64 단일 구성** |
| 직렬화 | Protobuf 3.17.2 + PacketGenerator |
| 이동 권한 | 1단계: 클라이언트가 이동 → 서버가 속도/거리 검증 → 다른 유저에게 브로드캐스트, 원격 캐릭터는 보간. 2단계(선택): 서버 시뮬레이션 + 클라 예측/보정 |
| 서버 틱 | Room `Update()` 100ms (DoTimer로 재예약) |
| 클라이언트 네트워크 | ServerCore의 `ClientService`를 재사용. IOCP 스레드 → PacketQueue → 엔진 메인 루프에서 `NetworkManager::Tick()`으로 처리 |
| 프로토콜(초안) | `PosInfo{object_id, x, y, z, yaw, MoveState}`, `ObjectInfo`, C/S_LOGIN, C/S_ENTER_GAME, S_SPAWN, S_DESPAWN, C/S_MOVE, C/S_CHAT (자세한 내용은 Plan.md Phase 2) |
| 인코딩 | 서버 신규 파일은 UTF-8(BOM), 클라 기존 파일은 CP949 유지 |
| 테스트 코드 위치 | `main`이 있는 파일(`GameServer.cpp` 등)에 테스트를 직접 쓰지 않는다. 각 프로젝트의 `Test.h`/`Test.cpp`에 테스트 함수를 만들고 `main`에서 호출만 한다 |
| 서버 네이밍 | **멤버 변수는 뒤에 `_`** (`lock_`, `threads_`) — 강의의 앞 `_`(`_lock`) 대신 OogieEngine과 통일. 전역 `G`, TLS `L`, 함수 안 static `S` 접두사. **포인터 변수(멤버, 지역, 파라미터)는 맨 앞에 소문자 `p`** (`MemoryPool* pPool`, `void* pPtr`, `const char* pName_`) — OogieEngine과 통일. 스마트 포인터도 붙인다 (`shared_ptr<GameSession> pSession`). 포인터를 담는 컨테이너, 배열은 붙이지 않는다 (`pools_`, `poolTable_`). 강의 코드를 옮길 때 예시 코드도 이 규칙으로 바꿔 제시한다 |
| 서버 데이터 타입 | 정수, 포인터, 동기화 타입은 **`ServerCore/Types.h`의 별칭**(`int32`, `uint64`, `uintptr`, `Atomic<T>` 등)을 쓴다. `std::int32_t`, `std::uintptr_t`, `std::size_t`를 직접 쓰지 않는다. **Types.h에 없는 새 타입이 필요하면 추가하기 전에 사용자에게 먼저 묻는다** |
| `Windows.h` 범위 | `CorePch.h`와 공개 헤더(`Memory.h` 등)에는 `<Windows.h>`를 넣지 않는다 (클라이언트 엔진까지 매크로가 퍼짐). `.cpp`에서 include하고, Windows 타입이 꼭 필요한 헤더(`MemoryPool.h`)는 **ServerCore 내부 전용**으로 두고 필요한 `.cpp`에서만 include한다 (테스트 파일도 직접 include) |
| 메모리 블록 규칙 | 모든 할당기는 **16바이트 정렬**을 준다 (x64 malloc, StompAllocator, SLIST). MSVC의 `alignof(std::max_align_t)`는 8이므로 기준으로 쓰지 않는다. `MemoryHeader::allocSize_`는 **헤더 포함 블록 크기**, 풀 안에서는 0 (이중 해제 검사) |

## 반드시 지킬 규칙
1. **스레드 경계**: 엔진 객체(Actor, Level)는 엔진 메인 스레드에서만 수정한다. Room 상태는 Room JobQueue 안에서만 수정한다.
2. 작게 만들고, 바로 테스트하고, 커밋한다. 커밋과 push는 사용자가 요청할 때만 한다.
   - 커밋 메시지와 PR에 **Claude가 작성했다는 표시(`Co-Authored-By: Claude`, "Generated with Claude Code" 등)를 넣지 않는다.**
   - 메시지 형식: 영어 Conventional Commits (`feat(ServerCore): ...`, `docs: ...`, `chore: ...`). 세부 단계는 메시지에 표시한다 (예: `(Phase 1-1 Step 2)`).
   - **브랜치는 Plan.md의 Phase 단위**로 하나씩 만든다: `<종류>/phase<번호>-<짧은 설명>`
     - 종류: `feature/`(기능), `refactor/`(구조 변경), `fix/`(버그), `docs/`(문서)
     - 예: `feature/phase1-servercore`, `feature/phase2-packet`, `feature/phase3-gameserver`, `refactor/phase4-client`, `feature/phase5-network`, `feature/phase6-movement-sync`, `feature/phase7-stress-test`
     - Phase 8은 주제별로 나눈다 (`feature/phase8-aoi`, `feature/phase8-udp` ...). Phase와 무관한 버그 수정은 `fix/<내용>`으로 만들고 바로 머지한다.
     - Phase가 길면 큰 구간이 끝날 때 중간에 main에 머지해도 된다 (예: Phase 1-4 Echo 서버 동작 시점).
     - 작업을 마칠 때마다 작업 브랜치를 push한다 (다른 PC에서 이어가기 + 백업).
     - main에 머지한 브랜치는 로컬과 원격에서 모두 삭제한다 (`git branch -d`, `git push origin --delete`).
3. `MMOServer_example/`은 커밋하지 않는다.
4. **PC별 수동 준비물** (git 제외 → 새 PC에서 clone 후 직접 복사):
   - `MMOServer_example/` (강의 코드)
   - **Protobuf lib** → `Server/Libraries/Libs/Protobuf/{Debug,Release}/` (Debug lib이 100MB 이상이라 GitHub 한도 초과. Protobuf 헤더 `Server/Libraries/Include/`와 `protoc.exe`는 커밋 대상)
   - FBX SDK 2020.3.11
   - 빌드가 lib 누락으로 실패하면 이 항목부터 확인한다.
   - VS 개인 설정(`*.user`, `.slnLaunch.user`의 여러 시작 프로젝트 등)도 PC마다 다시 설정한다.
   - 전체 절차는 `Setup.md`에 있다. git에 안 올라가는 항목이 새로 생기면 Setup.md에 추가한다.
5. 여러 PC에서 작업하므로, 작업을 시작할 때 `git pull`, 마칠 때 `push`를 안내한다. 새로 정해진 설계 결정은 이 파일에, 진행 상황은 Plan.md에 반영한다.

## Claude 작업 메모 (빌드 확인, 리뷰 습관)
- **서버 빌드·실행 확인** (Bash, Git Bash 기준). 리뷰나 테스트 작성 뒤에는 Debug/Release를 모두 빌드하고 실행해서 경고와 OK/FAIL을 확인한다.
  ```bash
  cd /c/Project/OogieEngine-Multi/Server
  MSB=$("/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -latest -find "MSBuild/**/Bin/MSBuild.exe" | head -1)
  for c in Debug Release; do "$MSB" Server.sln -p:Configuration=$c -p:Platform=x64 -m -v:minimal -nologo 2>&1 | grep -E "error|warning" | sort -u; ./Binary/$c/GameServer.exe; done
  ```
  `main`에 크래시 테스트 주석이 풀려 있으면 실행이 크래시로 끝난다 → 결과를 보고할 때 구분한다.
- **리뷰 체크리스트**: VS 저장 여부(디스크 내용이 옛것이면 `Ctrl+Shift+S` 안내), 들여쓰기 탭(붙여넣기 시 스페이스가 자주 섞임), 파일 끝 줄바꿈, 새 파일의 `.vcxproj`/`.filters` 등록, 헤더 주석의 약속과 코드 일치, C 스타일 캐스팅, `size_t` → `int32` 축소 변환 경고.
- **크기·정렬 값은 추측으로 단정하지 않는다.** `sizeof`, `alignof`, 오프셋은 테스트 출력이나 `STATIC_ASSERT`로 확인한 뒤 설명한다 (`max_align_t`를 16으로, `sizeof(MemoryHeader)`를 32로 잘못 설명한 적 있음).
- Claude가 파일을 고친 뒤에는 VS에서 다시 불러오라고 안내한다 (VS가 옛 버퍼로 덮어쓴 적 있음).
