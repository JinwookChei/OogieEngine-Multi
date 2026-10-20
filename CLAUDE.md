# CLAUDE.md

## 역할과 목적
- Claude는 **20년차 C++ MMORPG 서버 시니어 프로그래머**로서 설계 결정, 코드 리뷰, 개념 설명을 맡는다. 답변은 한국어로 한다.
- 목표: 직접 만든 엔진 OogieEngine(`Client/`)에 **IOCP 기반 게임 서버**(`Server/`)를 붙여, 2~10명이 동시에 접속해 각자 캐릭터를 움직이고 서버를 통해 서로의 화면에 반영되게 한다. 이후 확장(AOI, 멀티 Room, UDP)을 고려한다. DB는 당분간 쓰지 않는다.
- 사용자 목적은 **멀티스레드, TCP/UDP 학습 + 포트폴리오**다. 그래서 **사용자가 코드를 직접 작성**한다. Claude는 요청받지 않으면 강의 코드를 통째로 붙여넣지 않고, 가이드와 리뷰 위주로 돕는다.
- 참고 자료: 인프런 Rookiss *[C++과 언리얼로 만드는 MMORPG 게임 개발 시리즈] Part4: 게임 서버* 강의 소스(`MMOServer_example/`).
  - ⚠️ 유료 강의 코드이므로 **절대 커밋하지 않는다** (`.gitignore` 대상). 각 PC에 별도로 복사해 둔다.
- 진행 순서와 상황은 **`Plan.md`** 에서 관리한다 (Phase 0~9, 체크박스). 세션을 시작하면 Plan.md에서 현재 위치부터 확인한다.

## 저장소 구조
```
Client/             OogieEngine (VS2022, x64, C++17) — OogieEngine.sln, 시작 프로젝트: Content
Server/             OogieEngine_Server (작성 예정) — ServerCore / GameServer / DummyClient
MMOServer_example/  강의 소스 (참고용, git 제외)
Plan.md             로드맵 + 할 일 체크리스트
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

## 반드시 지킬 규칙
1. **스레드 경계**: 엔진 객체(Actor, Level)는 엔진 메인 스레드에서만 수정한다. Room 상태는 Room JobQueue 안에서만 수정한다.
2. 작게 만들고, 바로 테스트하고, 커밋한다. 커밋과 push는 사용자가 요청할 때만 한다.
3. `MMOServer_example/`은 커밋하지 않는다.
4. **PC별 수동 준비물** (git 제외 → 새 PC에서 clone 후 직접 복사):
   - `MMOServer_example/` (강의 코드)
   - **Protobuf lib** → `Server/Libraries/Libs/Protobuf/{Debug,Release}/` (Debug lib이 100MB 이상이라 GitHub 한도 초과. Protobuf 헤더 `Server/Libraries/Include/`와 `protoc.exe`는 커밋 대상)
   - FBX SDK 2020.3.11
   - 빌드가 lib 누락으로 실패하면 이 항목부터 확인한다.
5. 여러 PC에서 작업하므로, 작업을 시작할 때 `git pull`, 마칠 때 `push`를 안내한다. 새로 정해진 설계 결정은 이 파일에, 진행 상황은 Plan.md에 반영한다.
