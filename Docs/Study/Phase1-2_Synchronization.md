# 🔒 Phase 1-2. 동기화

> 💡 **목표**: 여러 스레드가 같은 데이터를 안전하게 쓰기 위한 도구를 직접 만든다.
> `std::mutex`보다 서버 상황(짧은 임계 영역, 읽기가 많음)에 맞는 **RW SpinLock**과, 데드락을 미리 잡아내는 **DeadLockProfiler**.

| Step | 파일 | 한 줄 요약 | 상태 |
|---|---|---|---|
| 사전 | — | Atomic vs Mutex, 재귀 lock 크래시 | ✅ |
| 1 | `Lock.h/.cpp` | WriteLock / WriteUnlock (CAS, 재진입, 타임아웃) | ✅ |
| 2 | `Lock.h/.cpp` | ReadLock / ReadUnlock | ✅ |
| 3 | `Lock.h` | ReadLockGuard / WriteLockGuard + `USE_LOCK` 매크로 | ✅ |
| 4 | `DeadLockProfiler.h/.cpp` | 락 스택, 순서 기록, Unlock 순서 검증 | ✅ |
| 5 | `DeadLockProfiler.cpp` | CheckCycle (DFS 사이클 검출) | 🚧 |
| 6 | `GameServer/Test.*` | 경합 테스트, 역순 락 검출 | ⬜ |

---

## 사전 개념. Atomic vs Mutex

### 한 줄 요약
- **Atomic**: 변수 **1개**에 대한 연산 **1개**를 CPU 명령 수준에서 쪼개지지 않게 한다.
- **Mutex**: 코드 **구간 전체**를 한 번에 한 스레드만 실행하게 한다.

### 동작 방식
```
++count        → mov / add / mov            (3단계, 끼어들 수 있음)
fetch_add(1)   → lock xadd [x], eax         (1단계, CPU가 캐시 라인을 독점)

mutex.lock()   → 이미 잡혀 있으면 커널에 요청해서 스레드를 재움 (컨텍스트 스위칭 = 비쌈)
```

| | Atomic | Mutex |
|---|---|---|
| 보호 범위 | 변수 1개, 연산 1개 | 임의의 코드 구간 (여러 변수, 컨테이너) |
| 경쟁 시 | 기다리지 않음 (하드웨어가 잠깐 직렬화) | 대기 / 잠듦 (커널 전환) |
| 비용 | 수~수십 ns | 경쟁 없으면 비슷, 경쟁 있으면 μs 단위 |
| 데드락 | 없음 | 있을 수 있음 |

### Atomic으로 안 되는 것
```cpp
Atomic<int32> hp, mp;   // 각각은 원자적이지만 "hp도 깎고 mp도 깎는다"는 묶음은 원자적이지 않다
std::vector<int32> v;   // push_back은 포인터/크기/재할당이 함께 바뀜 → Atomic으로 보호 불가
```
→ **여러 값을 함께 바꿔야 의미가 맞으면 락**이 필요하다. `ThreadManager::Launch`가 `threads_.push_back`을 Mutex로 감싸는 이유.

### 서버에서 쓰는 곳
- **Atomic**: 세션 ID 발급, 접속자 수, 참조 카운트(`shared_ptr` 내부), `InitTLS`의 `SThreadId`
- **락**: 세션 목록, Send 큐, JobQueue 내부 컨테이너

### ⚠️ 겪은 실수 — 재귀 lock 크래시
```cpp
LockGuard lg(m);   // 생성자에서 이미 m.lock()
m.lock();          // 같은 스레드가 한 번 더 → 💥
```
- `std::mutex`는 **재귀를 허용하지 않는다**. 표준에서는 UB.
- MSVC는 소유 스레드를 기록해 두었다가 감지 → `std::system_error` 예외 → 워커 스레드에서 catch 안 됨 → `std::terminate()` → `abort()`.
- 다른 구현에서는 **영원히 멈춤** (셀프 데드락). lock 2번, unlock 1번이라 짝도 안 맞는다.
- **교훈**: RAII 객체를 쓰면 `lock()`/`unlock()`을 직접 부르지 않는다. 실무에서는 `Room::Enter()`(lock) → `Room::Broadcast()`(같은 lock) 같은 형태로 자주 발생 → **직접 만드는 Lock은 재진입을 허용**하도록 설계한다.

---

## Step 1. Lock — WriteLock / WriteUnlock

### 🎯 목표
Atomic CAS로 **같은 스레드의 재진입을 허용하는 배타적 SpinLock**을 만든다.

### 📖 개념
**SpinLock**: 잠들지 않고 계속 재시도(spin)하며 기다린다. 서버 락은 대부분 아주 짧게 잡으므로, 잠들고 깨는 비용(커널 전환)보다 잠깐 도는 게 싸다. 너무 오래 걸리면 `yield`로 양보.

**32비트 하나에 상태를 모두 담는다**
```
[WWWWWWWW][WWWWWWWW][RRRRRRRR][RRRRRRRR]
 상위 16비트: Write 락을 가진 스레드 ID (0 = 소유자 없음)
 하위 16비트: Read 락 횟수
```
→ 상태가 Atomic **하나**에 있으므로 CAS 한 번으로 "비었는지 확인 + 내가 차지"를 동시에 처리. Atomic의 "변수 1개" 한계를 **상태를 한 변수에 몰아넣어** 해결한다.

**CAS (`compare_exchange_strong(expected, desired)`)**
> 값이 `expected`와 같으면 `desired`로 바꾸고 `true`. 다르면 **`expected`에 현재 값을 써넣고** `false`. → CPU 명령 하나(`lock cmpxchg`).

### 🧱 핵심 코드
```cpp
void Lock::WriteLock(const char* name)
{
	ASSERT_CRASH(LThreadId != 0 && LThreadId <= 0xFFFF);

	// 1) 재진입: 이미 내가 Write 소유자면 횟수만 증가
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		++writeCount_;
		return;
	}

	// 2) EMPTY일 때만 내 ID를 써넣는다
	const uint32 desired = (LThreadId << 16) & WRITE_THREAD_MASK;
	const auto beginTime = std::chrono::steady_clock::now();
	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			uint32 expected = EMPTY_FLAG;      // 실패하면 CAS가 expected를 덮어쓰므로 매번 다시 초기화
			if (lockFlag_.compare_exchange_strong(OUT expected, desired))
			{
				++writeCount_;
				return;
			}
		}
		if (std::chrono::steady_clock::now() - beginTime >= std::chrono::milliseconds(ACQUIRE_TIMEOUT_MS))
			CRASH("LOCK_TIMEOUT");             // 10초 못 잡으면 데드락으로 판단
		std::this_thread::yield();
	}
}

void Lock::WriteUnlock(const char* name)
{
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	ASSERT_CRASH(lockThreadId == LThreadId);   // 내가 잡은 락만 풀 수 있다

	if ((lockFlag_.load() & READ_COUNT_MASK) != 0)
		CRASH("INVALID_UNLOCK_ORDER");         // Write 중 잡은 Read를 먼저 풀어야 한다

	if (--writeCount_ == 0)
		lockFlag_.store(EMPTY_FLAG);
}
```
- `writeCount_`는 Atomic이 아니다 → **락을 가진 스레드만** 수정하기 때문.

### 🔧 강의와 다른 점
| 개선 | 이유 |
|---|---|
| **`LThreadId == 0` 검증** | EMPTY 상태의 소유자 비트도 0이다. `InitTLS`를 거치지 않은 스레드(`std::thread` 직접 생성)는 `LThreadId == 0` → "0 == 0이니 내가 주인" → **락 없이 통과**. 크래시 없이 데이터만 깨지는 최악의 버그 |
| **Unlock 소유자 검증** | 내가 잡지 않은 락을 풀면 즉시 크래시 |
| `GetTickCount64` → `std::chrono::steady_clock` | `<windows.h>` 의존 제거 (winsock include 순서 문제는 Phase 1-4에서 정리) |

### ⚠️ 겪은 실수
- 재진입 테스트를 **서로 다른 Lock 객체 2개**로 작성 → 재진입 경로를 전혀 타지 않음. 크래시가 안 났다고 검증된 게 아니다.
- `Lock.cpp`의 `CoreTLS.h`, `<chrono>`, `<thread>`를 처음엔 빼먹고, 다음엔 `Lock.h`에 넣음 → **구현에서만 쓰는 것은 .cpp에**.
- `#include <TypeInfo>` → Windows는 대소문자 무시라 동작하지만, 표준 헤더는 항상 소문자(`<typeinfo>`).

### 🧩 복습 질문
**Q1. 락을 잡을 때 `load()`로 확인 후 `store()`로 쓰면 안 되고 CAS를 써야 하는 이유는?**
> (미답변)

**Q2. 재진입 검사의 `load()`와 비교 사이에 다른 스레드가 값을 바꿀 수 있는데도 안전한 이유는?** (힌트: 상위 16비트를 *내 ID로* 바꿀 수 있는 스레드는?)
> (미답변)

---

## Step 2. Lock — ReadLock / ReadUnlock

### 🎯 목표
여러 스레드가 **동시에 읽을 수 있는** 공유 락을 추가해 RW SpinLock을 완성한다.

### 📖 개념
서버 데이터는 **읽기가 쓰기보다 훨씬 많다**. 읽기끼리는 서로 망가뜨리지 않으므로 동시에 허용한다.

| 지금 상태 | ReadLock 요청 | WriteLock 요청 |
|---|---|---|
| 비어 있음 | ✅ | ✅ |
| 다른 스레드가 Read 중 | ✅ (카운트 +1) | ❌ 대기 |
| 다른 스레드가 Write 중 | ❌ 대기 | ❌ 대기 |
| **내가** Write 중 | ✅ (Write → Read 허용) | ✅ (재진입) |
| **내가** Read 중 | ✅ | ❌ **영원히 대기 → 타임아웃 크래시** |

**Read → Write가 안 되는 이유**: WriteLock은 `EMPTY_FLAG`(0)일 때만 성공하는데, 내가 잡은 Read 카운트가 남아 있어 **절대 0이 될 수 없다** (셀프 데드락). 승격을 허용하면 두 스레드가 동시에 승격을 시도할 때 서로를 기다린다 → 허용하지 않고 타임아웃으로 잡는다.

### 🧱 핵심 코드
```cpp
void Lock::ReadLock(const char* name)
{
	ASSERT_CRASH(LThreadId != 0 && LThreadId <= 0xFFFF);

	// 1) 내가 Write 소유자면 경쟁 없이 Read 카운트만 증가
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		lockFlag_.fetch_add(1);
		return;
	}

	// 2) Write 소유자가 없을 때만 Read 카운트 +1
	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			uint32 expected = (lockFlag_.load() & READ_COUNT_MASK);
			if (lockFlag_.compare_exchange_strong(OUT expected, expected + 1))
				return;
		}
		// 타임아웃 / yield (WriteLock과 동일)
	}
}

void Lock::ReadUnlock(const char* name)
{
	// fetch_sub은 빼기 *전* 값을 반환 → 이미 0이었으면 Unlock을 더 많이 호출한 것
	if ((lockFlag_.fetch_sub(1) & READ_COUNT_MASK) == 0)
		CRASH("MULTIPLE_UNLOCK");
}
```

### 🔍 `expected + 1`을 숫자로 따라가기
```
[A] 다른 스레드 2개가 Read 중
lockFlag_ = 0x0000'0002 → expected = 0x0000'0002, desired = 0x0000'0003
CAS: 실제값 == expected → 성공 → Read 3명

[B] 스레드 5번이 Write 중
lockFlag_ = 0x0005'0000 → expected = 0x0000'0000 (마스크로 Write 비트 제거), desired = 0x0000'0001
CAS: 실제값 != expected → 실패, 다시 스핀

[C] 두 스레드가 동시에 ReadLock (lockFlag_ = 2)
X: expected 2 → 3  ✅ 먼저 성공
Y: expected 2 → 실제는 3 → 실패 → 다시 계산 expected 3 → 4 ✅
```
- `fetch_add(1)`을 쓰지 않는 이유: **"Write 소유자 없음 확인" + "+1"** 을 한 번에 해야 한다. `fetch_add`는 조건 없이 더하므로 Write 중에도 카운트를 올려 버린다.
- `expected + 1`은 **CAS 호출 전에 인자로 먼저 계산**된다. 실패 시 CAS가 `expected`를 덮어써도 루프 처음에서 다시 계산하므로 상관없다.
- 원칙: WriteLock이든 ReadLock이든 **매 시도마다 기대값을 새로 만든다**.

### 🧩 복습 질문
**Q1. `expected`를 `load() & READ_COUNT_MASK`로 만드는 이유는?**
> 마스크로 Write 비트를 지워 **"Write 소유자가 없는 상태"라는 기대값**을 만든다. 실제 검사는 CAS가 기대값과 실제 값을 비교하면서 한다.
> 마스크를 빼면: Write 중일 때 `expected = 0x0005'0000`, `desired = 0x0005'0001` → 실제값과 같으므로 **CAS가 성공해 버린다** → 쓰는 도중에 읽는 데이터 레이스.

**Q2. ReadUnlock에서는 왜 소유자를 검증할 수 없나?**
> Write는 한 명만 잡으므로 상위 16비트에 **ID**를 저장할 수 있다. Read는 여러 명이 잡으므로 하위 16비트에는 **"몇 번"이라는 숫자만** 있고 "누가"는 없다.
> 읽는 스레드 목록을 저장하면 락이 커지고, 목록 수정에 또 락이 필요하다 → **4바이트의 작고 빠른 락**을 위해 포기하는 트레이드오프.
> 검증이 없으면: B가 A의 Read를 대신 풀어도 통과 → C가 Write 성공(A는 아직 읽는 중) → 나중에 **A에서** `MULTIPLE_UNLOCK` 크래시. 원인(B)과 증상(A)이 다른 곳.
> → 대신 **RAII 가드**(Step 3)로 구조적으로 막고, **DeadLockProfiler**(Step 4)가 스레드별 락 스택으로 Debug에서 검증한다.
> **"락은 작고 빠르게, 검증은 RAII와 Debug 도구에 맡긴다."**

---

## Step 3. ReadLockGuard / WriteLockGuard + USE_LOCK 매크로

### 🎯 목표
Lock/Unlock의 짝을 **사람이 아니라 컴파일러가 보장**하게 한다.

### 📖 개념
- **RAII**: 생성자에서 Lock, 소멸자에서 Unlock. `return`, 예외, `break` 등 **어떤 경로로 나가도** 해제된다.
- 생성자와 소멸자는 **같은 스레드**에서 실행 → "다른 스레드가 내 락을 푸는" 문제가 구조적으로 사라진다.
- 매크로는 `typeid(this).name()`으로 **클래스 이름을 자동으로** 넣는다 → Lock/Unlock 이름이 항상 일치 (DeadLockProfiler가 이 이름으로 락을 구분).

### 🧱 핵심 코드
```cpp
class WriteLockGuard
{
public:
	WriteLockGuard(Lock& lock, const char* name) : lock_(lock), name_(name) { lock_.WriteLock(name_); }
	~WriteLockGuard() { lock_.WriteUnlock(name_); }

	WriteLockGuard(const WriteLockGuard&) = delete;             // 복사되면 Unlock 두 번
	WriteLockGuard& operator=(const WriteLockGuard&) = delete;

private:
	Lock& lock_;
	const char* name_;
};
// ReadLockGuard도 같은 구조

#define USE_MANY_LOCKS(count)   Lock locks_[count];
#define USE_LOCK                USE_MANY_LOCKS(1)
#define READ_LOCK_IDX(idx)      ReadLockGuard readLockGuard_##idx(locks_[idx], typeid(this).name());
#define READ_LOCK               READ_LOCK_IDX(0)
#define WRITE_LOCK_IDX(idx)     WriteLockGuard writeLockGuard_##idx(locks_[idx], typeid(this).name());
#define WRITE_LOCK              WRITE_LOCK_IDX(0)
```
사용 예:
```cpp
class Room
{
public:
	void Enter(int32 playerId) { WRITE_LOCK; playerIds_.push_back(playerId); }
	int32 Count()              { READ_LOCK;  return static_cast<int32>(playerIds_.size()); }
private:
	USE_LOCK;
	std::vector<int32> playerIds_;
};
```

| 포인트 | 설명 |
|---|---|
| `##idx` | 토큰 붙이기 → `readLockGuard_0`, `readLockGuard_1` (여러 락을 잡아도 이름 안 겹침) |
| 같은 스코프에 `WRITE_LOCK` 두 번 | 같은 변수 재정의 → **컴파일 오류** (실수를 빨리 잡음) |
| 매크로는 쓰는 곳에서 펼쳐질 때 컴파일된다 | 빌드가 성공해도 **한 번도 안 쓴 매크로는 검증되지 않은 것** |

### 🔧 강의와 다른 점
- 매크로를 `CoreMacro.h`가 아니라 **`Lock.h`에** 둔다. 매크로가 `Lock`과 가드를 쓰므로 함께 있는 게 자연스럽고, `CoreMacro.h`는 Types.h만 의존하는 아래층으로 남긴다. `Lock.h` 하나로 Lock 기능 전체를 쓸 수 있다 (자립 헤더).

### 🧩 복습 질문
**Q1. `WriteLockGuard(locks_[0], "Room");`처럼 변수 이름 없이 가드를 만들면?**
> (미답변)

**Q2. `Sum()`에서 `READ_LOCK`을 잡은 채로 `Push()`(`WRITE_LOCK`)를 호출하면?**
> (미답변) — 위 상태 전이 표에서 해당 칸을 찾아보기

---

## Step 4. DeadLockProfiler — Push / Pop

### 🎯 목표
스레드마다 **지금 잡고 있는 락 스택**을 기록하고, **락을 잡는 순서(A → B)** 를 그래프 간선으로 쌓는다. Unlock 순서가 틀리면 즉시 크래시.

### 📖 개념
**데드락은 락을 잡는 순서가 엇갈릴 때 생긴다**
```
스레드 1: A 잡음 → B 대기
스레드 2: B 잡음 → A 대기     → 서로 영원히 기다림
```
두 스레드의 타이밍이 정확히 겹쳐야만 터지므로 테스트에서는 잘 안 보이고, 라이브에서 접속자가 많을 때 터진다.

**아이디어: 실제로 엇갈리기 전에 "순서"만 보고 잡아낸다**
- A를 잡은 채 B를 잡으면 **간선 A → B** 기록.
- 나중에 어느 스레드든 B를 잡은 채 A를 잡으면 **간선 B → A** → 그래프에 **사이클**.
- 사이클 = "반대 순서로 잡는 코드가 존재한다" = 데드락 가능성. 타이밍이 겹치지 않았어도 크래시로 알린다. (사이클 검사는 Step 5)

**락 스택 (`LLockStack`, TLS)**
- Push: 스택 맨 위(직전 락) → 새 락 간선 기록 후 Push
- Pop: 맨 위와 풀려는 락이 다르면 **Unlock 순서 위반**
- 락에는 소유자 정보가 없지만(Step 2 Q2), **스레드 쪽 스택**에서 검증한다.

**Profiler 내부는 `std::mutex`와 std 컨테이너만 쓴다**
→ 우리 `Lock`을 쓰면 Lock → Profiler → Lock → … **무한 재귀**. 같은 이유로 Phase 1-3의 메모리 풀 컨테이너도 Profiler 안에서는 쓰지 않는다.

### 🧱 핵심 코드
```cpp
void DeadLockProfiler::PushLock(const char* name)
{
	LockGuard guard(lock_);
	const int32 lockId = GetOrCreateLockId(name);

	if (LLockStack.empty() == false)
	{
		const int32 prevId = LLockStack.top();
		if (prevId != lockId)                                // 같은 락 재진입은 순서가 아니다
		{
			std::set<int32>& history = lockHistory_[prevId];
			if (history.insert(lockId).second)               // 처음 보는 순서일 때만
				CheckCycle();
		}
	}
	LLockStack.push(lockId);
}

void DeadLockProfiler::PopLock(const char* name)
{
	LockGuard guard(lock_);
	if (LLockStack.empty())
		CRASH("MULTIPLE_UNLOCK");

	auto findIt = nameToId_.find(name);                      // operator[]는 없는 이름을 새로 등록해 버린다
	if (findIt == nameToId_.end())
		CRASH("UNKNOWN_LOCK");

	if (LLockStack.top() != findIt->second)
		CRASH("INVALID_UNLOCK_ORDER");                       // 마지막에 잡은 락부터 풀어야 한다
	LLockStack.pop();
}
```
```cpp
// Lock.cpp — 네 함수 모두 맨 앞에
#ifdef _DEBUG
	GDeadLockProfiler->PushLock(name);   // ReadLock도 Push, 두 Unlock은 PopLock
#endif
```
```cpp
// CoreGlobal — Profiler는 ThreadManager 다음에 생성, ThreadManager 다음에 삭제
```
> 📝 **내 메모 — 소멸 순서**
> Worker Thread는 Lock을 잡을 때마다 Profiler를 호출한다. 그래서 Profiler를 먼저 지우면 아직 돌고 있는 Thread가 해제된 Profiler에 접근한다.

### 🔧 강의와 다른 점
| 개선 | 이유 |
|---|---|
| 키를 `const char*` → **`std::string`** | 강의는 문자열 **주소**로 비교. 같은 `"Room"`이라도 다른 .cpp/**DLL**에서 오면 주소가 달라 다른 락으로 인식될 수 있다 (클라이언트 엔진은 DLL 여러 개). Debug 전용이라 비용 감수 |
| Pop에서 `operator[]` → **`find` + `UNKNOWN_LOCK`** | `map[name]`은 없는 이름을 ID 0으로 조용히 등록 → 엉뚱한 비교로 버그가 숨는다 |
| ID → 이름을 `unordered_map` → **`std::vector`** | ID가 0부터 순서대로 발급되므로 인덱스로 바로 찾는다 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `#ifdef DEBUG` | MSVC는 **`_DEBUG`** 를 정의 → 블록이 항상 빠짐. 경고도 없음 | 기대한 크래시(`UNKNOWN_LOCK`)가 안 나는 것도 실패다. `#ifdef` 블록이 실제로 컴파일되는지 확인 |
| 프로파일러 호출을 `WriteLock`에만 넣음 | Push만 쌓이고 Pop 없음 → Step 5에서 잘못된 간선으로 오탐 | Push/Pop은 반드시 짝 |
| 헤더 내용을 두 번 붙여넣음 | `C2011 class 재정의` | `#pragma once`는 다른 파일의 중복 include만 막는다. 한 파일 안의 중복 정의는 못 막음 |
| VS에 파일을 열어 둔 채 외부에서 수정 | VS가 옛 내용으로 다시 저장 → 코드 유실 | "외부 변경 내용 자동 다시 로드" 옵션 켜기 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 기대 결과 |
|---|---|
| `LockReentrancy` | Write → Write → Read 재진입 후 역순 해제, 다른 스레드가 바로 잡힘 → `OK` |
| `LockContention` | 1~10,000 Push(Writer 1) + Snapshot(Reader 3). 항상 `sum == size*(size+1)/2` → `bad reads : 0`, `OK` |
| `CrashInvalidUnlockOrder` | 💥 `INVALID_UNLOCK_ORDER` (Release는 FAIL 출력) |
| `CrashUnknownLock` | 💥 `UNKNOWN_LOCK` (Release는 FAIL 출력) |
| `CrashThreadIdZero` | 💥 `ASSERT_CRASH(LThreadId != 0 ...)` (Release에서도) |

### 🧩 복습 질문
**Q1. 같은 클래스의 서로 다른 객체 두 개(Room 1, Room 2)를 차례로 잡으면 Profiler는 어떻게 인식하나? 한계는?**
> (미답변)

**Q2. `PushLock`을 실제 락 획득 *전*에 호출하는 이유는?** (힌트: 진짜 데드락에 걸린 스레드는 CAS 루프에서 못 나온다)
> (미답변)

---

## Step 5. DeadLockProfiler — CheckCycle (DFS) 🚧

> 작성 중. 핵심 키워드: 3색 DFS(NotVisited / Visiting / Finished), Visiting 노드로 가는 간선 = 역방향 간선 = 사이클, parents로 사이클 경로 복원
