# 📚 OogieEngine_Server 학습 정리

> 💡 OogieEngine(직접 만든 DirectX11 엔진)에 **IOCP 기반 C++ 게임 서버**를 붙이면서 배운 내용을 Phase/Step 단위로 정리한 문서입니다.
> 나중에 다시 읽을 때 **"왜 이렇게 만들었는지"** 를 이해하는 것이 목표입니다. 코드 전문은 저장소에 있고, 여기에는 핵심 코드와 이유만 담습니다.

---

## 🗂️ 목차

| Phase | 문서 | 주제 | 상태 |
|---|---|---|---|
| 0 | [Phase0_Setup.md](Phase0_Setup.md) | 솔루션 구성, 빌드 설정, PC별 준비물 | ✅ 완료 |
| 1-1 | [Phase1-1_Foundation.md](Phase1-1_Foundation.md) | Types, CoreMacro, CoreTLS, ThreadManager, CoreGlobal, CorePch | ✅ 완료 |
| 1-2 | [Phase1-2_Synchronization.md](Phase1-2_Synchronization.md) | Atomic vs Mutex, RW SpinLock, LockGuard, DeadLockProfiler(DFS 사이클 검출) | ✅ 완료 |
| 1-3 | — | 메모리 (Allocator, MemoryPool, ObjectPool) | ⬜ |
| 1-4 | — | 네트워크 (IOCP, Session, Service) | ⬜ |
| 1-5 | — | 버퍼 & 패킷 조립 | ⬜ |
| 1-6 | — | Job 시스템 | ⬜ |

---

## 🧭 전체 그림 (현재까지)

```
GameServer (exe) ──include──▶ pch.h ──▶ CorePch.h ─┬─ Types.h        (정수/동기화 타입 별칭)
DummyClient (exe)                                   ├─ CoreMacro.h    (CRASH, ASSERT_CRASH, size32 ...)
                                                    ├─ CoreTLS.h      (LThreadId, LLockStack)
        ServerCore.lib (static) ◀──link──           ├─ CoreGlobal.h   (GThreadManager, GDeadLockProfiler)
                                                    └─ Lock.h         (RW SpinLock, LockGuard, USE_LOCK)

main()
 └─ CoreGlobal coreGlobal;   ← 전역 매니저 생성 (소멸은 main 종료 시 역순)
     ├─ GThreadManager       ← 스레드 생성/Join, 스레드마다 TLS 초기화
     └─ GDeadLockProfiler    ← (Debug) 락 순서 검사
```

---

## 🏷️ 프로젝트 공통 규칙 요약

| 항목 | 규칙 |
|---|---|
| 네이밍 | 멤버 변수 뒤 `_` (`lock_`), 전역 `G`, TLS `L`, 함수 안 static `S` |
| 인코딩 | 서버 소스 UTF-8(BOM), 클라 기존 소스 CP949 |
| 헤더 | **자립 헤더**: 쓰는 곳에서 직접 include. 헤더에는 선언에 필요한 것만, 구현에만 필요한 것은 .cpp에 |
| `using namespace std` | 헤더에서 쓰지 않는다 (CorePch는 클라이언트 엔진에도 들어감) |
| 테스트 | `main`에 직접 쓰지 않고 `Test.h`/`Test.cpp`의 함수로 만들어 `main`에서 호출 |
| 커밋 | 영어 Conventional Commits, Step 표기 (`feat(ServerCore): ... (Phase 1-2 Step 1)`) |

---

## ✍️ 이 문서 작성 방식

- Step이 끝날 때마다 해당 Phase 문서에 섹션을 추가한다.
- 각 Step 섹션 구성: **🎯 목표 → 📖 개념 → 🧱 핵심 코드 → 🔧 강의와 다른 점 → ⚠️ 겪은 실수 → 🧩 복습 질문**
- 복습 질문 중 아직 답하지 않은 것은 `(미답변)`으로 남겨 두고, 답한 뒤 채운다.
- Notion으로 가져오기: Notion → `가져오기` → `Text & Markdown` → 이 폴더의 .md 파일 선택
