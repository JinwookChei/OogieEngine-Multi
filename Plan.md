# OogieEngine_Server Plan

> 목표: 직접 만든 DirectX11 엔진(OogieEngine)에 **IOCP 기반 C++ 게임 서버**를 붙여 2~10명이 동시에 접속하고, 각자 캐릭터를 움직이면 서버를 통해 다른 유저 화면에 반영되는 멀티플레이를 완성한다.
> 학습 목적(멀티스레드, TCP/UDP) + 포트폴리오 목적이므로 **모든 코드는 직접 작성**한다.
> 참고 자료: 인프런 Rookiss Part4 게임 서버 강의 소스(`MMOServer_example/`)

---

## 📍 현재 진행 상황 (세션을 시작하면 여기부터 확인)

| 항목 | 상태 |
|---|---|
| 작업 브랜치 | `feature/phase1-servercore` |
| 현재 위치 | **Phase 1-1 Step 5 `CoreGlobal`** 시작 전 |
| 완료 | Phase 0 / Phase 1-1 Step 1~4 (Types, CoreMacro, CoreTLS, ThreadManager) |
| 남은 숙제 | Step 4 확인 질문 2개 미답변 ↓ |

**Step 4 확인 질문 (미답변)**
1. `InitTLS`에서 `Atomic` 대신 일반 `static uint32`로 `SThreadId++`를 하면 어떤 문제가 생길 수 있나?
2. `Launch`의 람다를 `[&]`로 바꾸면 왜 위험한가? 테스트의 `[&coutLock]`은 왜 괜찮은가?

**다음에 할 일**
1. Step 4 확인 질문 답변 → Claude가 확인
2. Step 5 `CoreGlobal`: `GThreadManager` 전역 포인터 + 생성/소멸 담당 `CoreGlobal` 전역 객체 (정적 라이브러리에서 .obj가 링크에서 빠지는 함정 주의)
3. Step 6 `CorePch` 정리 + 스레드 테스트 (GameServer 테스트 코드 정리, `std::thread` 사용부의 간접 include 의존 정리)

> 이 섹션은 Step이 끝날 때마다 갱신한다.

---

## 0. 핵심 설계 결정 (권장안)

| 항목 | 결정 | 이유 |
|---|---|---|
| 네트워크 | Windows IOCP + TCP (UDP는 Phase 8에서 실험) | 강의 흐름과 일치, MMORPG 표준 |
| 서버 구조 | `ServerCore`(static lib) + `GameServer`(exe) + `DummyClient`(exe) | 강의 구조 유지, 클라이언트도 ServerCore 재사용 가능 |
| 동시성 모델 | 워커 스레드 N개 + **Room = JobQueue** (Room 내부는 싱글스레드처럼 작성) | 락 최소화, 강의 핵심 |
| 직렬화 | Protobuf 3.17.2 + PacketGenerator(자동 핸들러 생성) | 강의 동일 |
| 이동 동기화 권한 | **1단계: 클라이언트 이동 + 서버 검증(속도/거리 체크) + 브로드캐스트** → 2단계(선택): 서버 시뮬레이션 | MMORPG에서 가장 흔한 방식. 바로 동작하는 결과를 빨리 보고, 이후 권한을 점진적으로 강화 |
| 원격 캐릭터 표현 | 수신한 위치로 **보간(Interpolation)** | 끊김 없는 움직임 |
| 좌표계 | OogieEngine 기준 **Z-up**, yaw = `RotationZ`(degree) | 서버/클라 좌표 일치 |
| 서버 틱 | Room `Update()` 100ms(10Hz) 고정 틱 (`DoTimer` 재귀 예약) | 이후 AOI/몬스터 확장 기반 |
| 인코딩 | 서버 신규 파일: UTF-8(BOM) / 클라 기존 파일: CP949 유지 | 주석 깨짐 방지 |

> ⚠️ **강의 소스 저작권**: `MMOServer_example/`은 유료 강의 코드이므로 공개 저장소에 올리지 않는다. `.gitignore`에 추가한다.

---

## 1. 최종 폴더 구조 (목표)

```
OogieEngine-Multi/
├ Client/                     # 기존 OogieEngine
│  ├ Engine/                  # + ObjectId, DestroyActor, 입력/이동 분리
│  └ Content/                 # + NetworkManager, ServerSession, LocalPlayer/RemotePlayer
├ Server/
│  ├ Server.sln
│  ├ ServerCore/              # IOCP, Session, Lock, Memory, JobQueue ...
│  ├ GameServer/              # GameSession, Room, Player, ClientPacketHandler
│  ├ DummyClient/             # 봇 부하 테스트
│  ├ Common/Protobuf/         # *.proto, protoc, GenPackets.bat
│  ├ Tools/PacketGenerator/   # Python + Jinja2
│  └ Libraries/               # Include(protobuf), Libs(ServerCore, Protobuf)
├ MMOServer_example/          # 참고용 (git 제외)
└ Plan.md
```

---

## 2. 전체 로드맵 한눈에 보기

```
Phase 0  환경 준비
Phase 1  ServerCore 직접 구현 (강의 복습)         ──┐
Phase 2  패킷 파이프라인 (Protobuf + Generator)     │ 서버 단독
Phase 3  GameServer 컨텐츠 (입장/스폰/이동)       ──┘
Phase 4  Client 엔진 리팩터링 (네트워크 없이)      ── 클라 단독 (Phase 1~3과 병행 가능)
Phase 5  Client ↔ Server 연결                      ── ★ 1차 마일스톤: 2명 이상 이동 동기화
Phase 6  이동 동기화 품질 개선 (보간/검증/핑)
Phase 7  부하 테스트 & 프로파일링
Phase 8  확장 (멀티 Room, AOI, UDP, 종료 처리)
Phase 9  포트폴리오 정리
```

---

## Phase 0. 환경 준비

- [x] `.gitignore` 작성: `MMOServer_example/`, `Server/Binary/`, `Server/Libraries/Libs/`, `*.pb.h`, `*.pb.cc`, `x64/`, `.vs/`
- [ ] **PC별 수동 준비물** (git에 포함되지 않음 → 새 PC에서 clone 후 직접 복사)
  - `MMOServer_example/` (유료 강의 코드)
  - Protobuf lib → `Server/Libraries/Libs/Protobuf/{Debug,Release}/` (Debug lib이 100MB 이상이라 GitHub 파일 한도 초과)
  - FBX SDK 2020.3.11 (OogieEngine 빌드용, README 참고)
  - VS 개인 설정: Server.sln **여러 시작 프로젝트**(GameServer + DummyClient), Client 시작 프로젝트(Content)
  - 👉 전체 절차: **`Setup.md`**
- [x] 작업 브랜치 생성 (`feature/server-setup`)
- [x] `Server/Server.sln` 생성, 플랫폼은 **x64 단일**로 통일 (Win32 구성 제거)
- [x] 프로젝트 3개 생성: `ServerCore`(Static Library), `GameServer`(Console), `DummyClient`(Console)
- [x] 공통 설정: C++17, `/MP`, `/utf-8`, 출력 경로 `$(SolutionDir)Binary\$(Configuration)\`, `Server/.editorconfig`(UTF-8 BOM)
- [x] Protobuf 3.17.2 x64 Debug/Release lib 준비 (CRT `/MDd`·`/MD` 확인 완료)
- [ ] Python + Jinja2 설치 (PacketGenerator용)

**완료 기준**: 빈 GameServer가 ServerCore.lib을 링크해 x64 Debug/Release로 빌드된다.

---

## Phase 1. ServerCore 직접 구현 (강의 복습)

> 강의를 보고 **직접 타이핑**한다. 각 단계마다 작은 테스트 코드(GameServer main)로 동작을 확인하고 커밋한다.
> 아래 🔧 표시는 강의 코드의 결함을 내 코드에서 **개선**할 항목이다 (포트폴리오 차별점).

### 1-1. 기반
- [x] Step 1 `Types.h` (고정 크기 정수 `std::int32_t` 등, 동기화 별칭, `USING_SHARED_PTR`)
- [x] Step 1 `CoreMacro.h` 분리 (2층 검증 매크로 `STATIC_ASSERT` → `STATIC_ASSERT_SIZE`, size16/len32) + `Types.cpp`(타입 크기 검증)
- [x] 🔧 `_STOMP`를 Debug에서만 켜도록 분리 (`CoreMacro.h`)
- [x] Step 2 `CRASH` / `ASSERT_CRASH` (2층 구조, `do { } while (0)`, volatile, `#expr`), `OUT`
- [x] Step 3 `CoreTLS` (`LThreadId`만. 나머지 TLS는 해당 Phase에서 추가)
- [x] Step 4 `ThreadManager` (Launch / Join / InitTLS / DestroyTLS, 멤버 `lock_`, `threads_`)
- [ ] Step 5 `CoreGlobal` (전역 매니저 생성/소멸 순서)
- [ ] Step 6 `CorePch` 정리 + 스레드 테스트
- 미룬 항목: Lock 매크로(`USE_LOCK`/`READ_LOCK`/`WRITE_LOCK`) → 1-2, `LLockStack` → 1-2, `LSendBufferChunk` → 1-5, `LEndTickCount`/`LCurrentJobQueue` → 1-6

### 1-2. 동기화
- [ ] `Lock` (RW SpinLock: [WriteThreadId 16bit][ReadCount 16bit], 재귀 Write 허용)
- [ ] `ReadLockGuard` / `WriteLockGuard`
- [ ] `DeadLockProfiler` (Debug 전용, 락 순서 그래프 DFS로 사이클 검출)
- [ ] 테스트: 스레드 여러 개로 공유 컨테이너 Read/Write 경합 → 크래시 없음

### 1-3. 메모리
- [ ] `BaseAllocator` / `StompAllocator` / `PoolAllocator` / `StlAllocator`
- [ ] `MemoryPool` (SLIST lock-free), `MemoryHeader`
- [ ] `Memory` (32~4096 크기별 풀 테이블), `xnew` / `xdelete` / `MakeShared`
- [ ] `ObjectPool<T>`
- [ ] `Container.h` (`Vector`, `Map`, `HashMap`, `Queue`, `String` ...)
- [ ] 테스트: Stomp 모드에서 해제 후 접근(Use-After-Free) 시 즉시 크래시 확인

### 1-4. 네트워크
- [ ] `SocketUtils` (WSAStartup, `AcceptEx`/`ConnectEx`/`DisconnectEx` 함수 포인터 바인딩, 소켓 옵션)
- [ ] `NetAddress`
- [ ] `IocpCore` / `IocpObject` / `IocpEvent`(Connect, Disconnect, Accept, Recv, Send)
- [ ] 🔧 `IocpCore::Dispatch`: GQCS 실패 + overlapped == nullptr 일 때 크래시 방지
- [ ] `Listener` (AcceptEx 미리 N개 등록)
- [ ] `Session` (Connect / Disconnect / Recv / Send, `owner` shared_ptr로 I/O 중 생명주기 보장)
- [ ] `Service` / `ServerService` / `ClientService`
- [ ] 테스트: **Echo 서버** + DummyClient 1개 → 문자열 왕복

### 1-5. 버퍼 & 패킷 조립
- [ ] `RecvBuffer` (read/write 커서, Clean 시 앞으로 당기기)
- [ ] `SendBuffer` / `SendBufferChunk` / `SendBufferManager` (TLS Chunk)
- [ ] Session Send 큐 + Scatter-Gather `WSASend`
- [ ] `PacketSession` (`[size 2][id 2][payload]`)
- [ ] 🔧 패킷 헤더 검증: `size < sizeof(PacketHeader)` 또는 `size > MAX_PACKET_SIZE` → Disconnect (**size=0 무한루프 방지**)
- [ ] `BufferReader` / `BufferWriter`
- [ ] 테스트: DummyClient 100세션 동시 접속 + 브로드캐스트, 1분 이상 안정 동작

### 1-6. Job 시스템
- [ ] `Job` (람다 / 멤버함수+인자 캡처)
- [ ] `LockQueue<T>`
- [ ] `JobQueue` (`DoAsync`, `DoTimer`, 첫 Push한 스레드가 실행, 시간 초과 시 GlobalQueue로 넘김)
- [ ] `GlobalQueue`
- [ ] `JobTimer` (우선순위 큐, `Distribute`)
- [ ] 워커 루프 `DoWorkerJob` (Dispatch → DistributeReservedJobs → DoGlobalQueueWork)
- [ ] 테스트: Room에 여러 스레드가 동시에 DoAsync → 카운터 정확성 검증

**Phase 1 완료 기준**: 워커 스레드 N개 위에서 Echo/Broadcast가 동작하고, DummyClient 100세션으로 장시간 크래시·누수 없음.

---

## Phase 2. 패킷 파이프라인 (Protobuf + PacketGenerator)

- [ ] `Common/Protobuf/bin`에 protoc.exe, `Enum.proto`, `Struct.proto`, `Protocol.proto` 구성
- [ ] `Tools/PacketGenerator` 작성 (ProtoParser.py + Jinja2 템플릿 → `ClientPacketHandler.h` / `ServerPacketHandler.h`)
- [ ] PyInstaller로 `GenPackets.exe` 생성
- [ ] `GenPackets.bat` → GameServer Pre-Build 이벤트로 등록 (Client 쪽 복사 경로도 추가)
- [ ] 🔧 핸들러 테이블 `id` 범위 검사 (`GPacketHandler[UINT16_MAX]` 경계 초과 방지)
- [ ] 🔧 핸들러가 false 반환 시 로그 + Disconnect 정책 결정

### 프로토콜 초안 (1차 마일스톤용)

```protobuf
// Enum.proto
enum PlayerType  { PLAYER_TYPE_NONE = 0; PLAYER_TYPE_WEREWOLF = 1; }
enum MoveState   { MOVE_STATE_IDLE = 0; MOVE_STATE_MOVE = 1; MOVE_STATE_ATTACK = 2; }

// Struct.proto
message PosInfo {
  uint64    object_id = 1;
  float     x = 2;  float y = 3;  float z = 4;   // Z-up
  float     yaw = 5;                            // RotationZ (degree)
  MoveState state = 6;
}
message ObjectInfo {
  uint64     object_id   = 1;
  PlayerType player_type = 2;
  string     name        = 3;
  PosInfo    pos_info    = 4;
}

// Protocol.proto
message C_LOGIN       { }
message S_LOGIN       { bool success = 1; }
message C_ENTER_GAME  { }
message S_ENTER_GAME  { bool success = 1; ObjectInfo player = 2; }   // 내 캐릭터
message C_LEAVE_GAME  { }
message S_LEAVE_GAME  { }
message S_SPAWN       { repeated ObjectInfo objects = 1; }           // 다른 유저 등장
message S_DESPAWN     { repeated uint64 object_ids = 1; }            // 다른 유저 퇴장
message C_MOVE        { PosInfo pos_info = 1; }
message S_MOVE        { PosInfo pos_info = 1; }
message C_CHAT        { string msg = 1; }
message S_CHAT        { uint64 object_id = 1; string msg = 2; }
```

**완료 기준**: proto 수정 → 빌드만 하면 서버/클라 핸들러가 자동 갱신된다.

---

## Phase 3. GameServer 컨텐츠

- [ ] `GameSession` (OnConnected / OnDisconnected / OnRecvPacket)
- [ ] `GameSessionManager`
- [ ] `Player` : `objectId`, `PosInfo`, `weak_ptr<GameSession>` (🔧 Session↔Player **순환 참조 제거**)
- [ ] `ObjectIdGenerator` (Atomic uint64)
- [ ] `Room : JobQueue`
  - [ ] `Enter(player)`: 나에게 `S_ENTER_GAME`, 나에게 기존 유저 `S_SPAWN`, 기존 유저에게 나를 `S_SPAWN`
  - [ ] `Leave(objectId)`: 다른 유저에게 `S_DESPAWN`
  - [ ] `HandleMove(player, C_MOVE)`: 위치 갱신 → **나를 제외한** 유저에게 `S_MOVE`
  - [ ] `Broadcast(sendBuffer, exceptId)`
  - [ ] `Update()` 100ms 틱 (`DoTimer`로 자기 자신을 재예약)
- [ ] 🔧 핸들러 입력 검증 (입장 전 C_MOVE 무시, 중복 입장 차단)
- [ ] DummyClient 확장: 로그인 → 입장 → 랜덤 C_MOVE 주기 전송 봇

### 입장/이동 시퀀스

```
ClientA            GameServer(Room)            ClientB(기존 유저)
   │── C_LOGIN ──────▶│                              │
   │◀── S_LOGIN ──────│                              │
   │── C_ENTER_GAME ─▶│ Room->DoAsync(Enter)         │
   │◀ S_ENTER_GAME(나)│                              │
   │◀ S_SPAWN(B...)   │── S_SPAWN(A) ───────────────▶│
   │── C_MOVE ───────▶│ 검증 → 갱신                   │
   │                  │── S_MOVE(A) ────────────────▶│
   │  (연결 종료)       │ Room->DoAsync(Leave)          │
   │                  │── S_DESPAWN(A) ─────────────▶│
```

**완료 기준**: DummyClient 봇 10개가 접속·이동·종료하면 서버 로그상 Spawn/Move/Despawn이 정확히 오간다.

---

## Phase 4. Client 엔진 리팩터링 (네트워크 없이, 싱글에서 검증)

> 네트워크를 붙이기 전에 **싱글 플레이에서 먼저** 구조를 바꾸고 기존 동작이 유지되는지 확인한다.

- [ ] `Level::DestroyActor(Actor*)` 추가 — Tick 순회 중 삭제 안전하게 (**지연 삭제 리스트** → 프레임 끝에 실제 delete)
- [ ] Actor에 `objectId`(uint64) 필드 추가 (0 = 로컬 전용 오브젝트)
- [ ] 입력과 이동 분리
  - [ ] `MovementComponent`가 `InputManager`를 직접 읽지 않도록 변경 → `SetMoveInput(dir, yawDelta)` 형태로 외부 주입
  - [ ] 로컬 입력 수집은 `LocalPlayerController`(또는 LocalPlayer::Tick)에서 담당
- [ ] 애니메이션을 **입력 기반 → MoveState 기반**으로 변경 (`WereWolf::SetMoveState(state)`)
- [ ] `LocalPlayer` / `RemotePlayer` 분리
  - LocalPlayer: 입력 + MovementComponent + CameraComponent
  - RemotePlayer: 카메라 없음, 목표 위치로 보간 이동 (Phase 6에서 고도화)
- [ ] 테스트: 레벨에 LocalPlayer 1 + RemotePlayer 2(더미 경로 이동)를 배치해 싱글로 확인

**완료 기준**: 네트워크 없이도 "내 캐릭터는 입력으로, 다른 캐릭터는 외부 데이터로" 움직이는 구조가 완성된다.

---

## Phase 5. Client ↔ Server 연결 ★ 1차 마일스톤

### 클라이언트 스레드 모델

```
[IOCP 스레드 1~2개]  ServerSession::OnRecvPacket
        │  (엔진 객체 직접 접근 금지)
        ▼
   PacketQueue (Lock 보호, 또는 SPSC)
        │
[엔진 메인 스레드]  Engine::Run 루프 중 NetworkManager::Tick() 에서 PopAll → 핸들러 실행
                    → Spawn / Move / Despawn → Level / Actor 조작
```

- [ ] Content(또는 별도 `Network` 모듈)에 ServerCore.lib + libprotobuf 링크
- [ ] 🔧 OogieEngine Debug pch의 `#define new new(_NORMAL_BLOCK, ...)` ↔ ServerCore placement new 충돌 해결 (include 순서 / 네트워크 코드 pch 분리)
- [ ] `NetworkManager` : ClientService 생성, IOCP 스레드 기동, 패킷 큐, 메인 스레드 `Tick()`
- [ ] `ServerSession : PacketSession` (OnConnected에서 C_LOGIN)
- [ ] 엔진 루프에 `NetworkManager::Tick()` 호출 지점 추가 (World::OnTick 직전) — IStartup 확장 또는 Level::Tick 활용
- [ ] `ServerPacketHandler` 구현
  - [ ] `S_ENTER_GAME` → LocalPlayer 스폰 (objectId 부여)
  - [ ] `S_SPAWN` → RemotePlayer 스폰, `objectId → Actor*` 맵 등록
  - [ ] `S_MOVE` → 해당 RemotePlayer 목표 위치 갱신
  - [ ] `S_DESPAWN` → DestroyActor + 맵 제거
- [ ] LocalPlayer 이동 시 `C_MOVE` 전송 (**상태 변경 시 즉시 + 이동 중 100ms 주기**)
- [ ] 접속 정보(IP/Port) 설정 파일 또는 커맨드라인 인자

**완료 기준 (1차 마일스톤)**:
클라이언트 2개 이상 실행 → 각자 WereWolf를 조작 → 서로의 화면에 상대 캐릭터가 이동/애니메이션과 함께 보인다 → 한쪽 종료 시 상대 화면에서 사라진다.

---

## Phase 6. 이동 동기화 품질 개선

- [ ] RemotePlayer **보간**: 수신 위치를 버퍼링하고 약 100ms 지연 렌더링으로 선형 보간 (yaw는 최단 각도 보간)
- [ ] 서버 이동 검증: `거리 <= maxSpeed × 경과시간 × 허용오차` 위반 시 보정 `S_MOVE`(본인에게) 전송
- [ ] 서버 시간 동기화: `C_PING` / `S_PING`으로 RTT 측정, 서버 tick 타임스탬프 포함
- [ ] ImGui 네트워크 디버그 창: RTT, 송수신 패킷/초, 접속자 수, 내 objectId
- [ ] (선택) 서버 권한 강화: 클라는 입력(방향/yaw)만 전송, 서버가 시뮬레이션, 클라는 예측 + 보정(Reconciliation)

**완료 기준**: 인위적 지연(100~200ms) 환경에서도 원격 캐릭터가 부드럽게 움직이고, 순간이동 치트가 서버에서 차단된다.

---

## Phase 7. 부하 테스트 & 프로파일링

- [ ] DummyClient: 봇 수 / 이동 주기 / 실행 시간을 인자로 받기
- [ ] 측정: 접속자 수별 CPU, 메모리, 초당 패킷 처리량, Room Job 처리 지연
- [ ] 병목 분석 (Visual Studio Profiler / Concurrency Visualizer)
- [ ] 개선 예: Room Update에서 이동 패킷을 틱 단위로 **모아서(Batching)** 브로드캐스트
- [ ] 결과를 표/그래프로 기록 (README에 활용)

**완료 기준**: "봇 N명 / 10Hz 이동 기준 서버 CPU x%, 처리량 y pkt/s" 같은 수치를 확보한다.

---

## Phase 8. 확장

- [ ] 🔧 Graceful Shutdown (`Service::CloseService`, 워커 종료 플래그, 세션 정리)
- [ ] 🔧 Session 풀링 (`TF_REUSE_SOCKET` 실제 활용)
- [ ] 로그 시스템 (레벨별, 비동기 파일 출력)
- [ ] 멀티 Room / Zone (Room 별 JobQueue → 자연스러운 병렬화)
- [ ] **AOI(시야) 그리드**: 셀 단위로 Spawn/Despawn/Move 브로드캐스트 범위 제한 → 접속자 증가 대비
- [ ] **UDP 실험**: 이동 패킷만 UDP로 전송 (시퀀스 번호, 오래된 패킷 폐기), TCP와 비교 측정
- [ ] (먼 미래) DB 연동, 로그인 서버 분리

---

## Phase 9. 포트폴리오 정리

- [ ] 아키텍처 다이어그램 (스레드 모델, JobQueue, 패킷 흐름)
- [ ] 강의 코드 대비 **개선한 점** 정리 (🔧 항목들: 헤더 검증, 순환참조 제거, IOCP 에러 처리, Stomp 분리 등)
- [ ] 부하 테스트 결과 수치
- [ ] 2~3인 동시 접속 데모 영상
- [ ] 트러블슈팅 기록 (데드락, 크래시, 동기화 이슈를 어떻게 찾고 고쳤는지)

---

## 진행 원칙

1. **작게 만들고, 바로 테스트하고, 커밋한다.** (체크박스 1~3개 단위)
2. 강의 코드를 복사하지 않고 **이해한 뒤 직접 작성**한다. 이해가 안 되는 부분은 질문으로 남긴다.
3. 서버(Phase 1~3)와 클라이언트 리팩터링(Phase 4)은 병행 가능하지만, **Phase 5는 둘 다 끝난 뒤** 시작한다.
4. 스레드 경계 규칙: **엔진 객체(Actor, Level)는 엔진 메인 스레드에서만**, **Room 상태는 Room JobQueue 안에서만** 수정한다.
