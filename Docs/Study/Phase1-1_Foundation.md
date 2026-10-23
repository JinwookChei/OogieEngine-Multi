# 🧱 Phase 1-1. ServerCore 기반

> 💡 **목표**: 서버 코드 전체가 공통으로 쓰는 바닥 계층(타입, 검증 매크로, TLS, 스레드 관리, 전역 매니저)을 만든다.
> 이후 모든 Phase가 이 위에 올라간다.

| Step | 파일 | 한 줄 요약 |
|---|---|---|
| 1 | `Types.h`, `Types.cpp`, `CoreMacro.h` | 고정 크기 타입 별칭, 컴파일 타임 검증, 크기 매크로 |
| 2 | `CoreMacro.h` | `CRASH` / `ASSERT_CRASH` 런타임 검증 |
| 3 | `CoreTLS.h/.cpp` | 스레드마다 따로 갖는 전역 변수 `LThreadId` |
| 4 | `ThreadManager.h/.cpp` | 스레드 생성/Join + TLS 초기화 |
| 5 | `CoreGlobal.h/.cpp` | 전역 매니저 생성/소멸 순서 관리 |
| 6 | `CorePch.h`, `GameServer/Test.*` | 자립 헤더 정리 + 데이터 레이스 실험 |

---

## Step 1. Types.h / CoreMacro.h (컴파일 타임)

### 🎯 목표
플랫폼마다 크기가 다를 수 있는 `int`, `long` 대신 **크기가 고정된 타입**을 쓰고, 그 크기를 컴파일 시점에 검증한다.

### 📖 개념
- 패킷은 네트워크로 **바이트 단위**로 오간다. 서버와 클라에서 타입 크기가 다르면 패킷 해석이 어긋난다 → `int32`, `uint16`처럼 **크기를 이름에 박는다**.
- `static_assert`: 조건이 거짓이면 **컴파일 오류**. 실행하기 전에 잘못을 잡는다.
- `sizeof`는 `size_t`(x64에서 8바이트)를 돌려준다 → 패킷 크기 필드(`uint16`)에 넣을 때 매번 캐스팅이 필요 → `size16`/`size32` 매크로.

### 🧱 핵심 코드
```cpp
// Types.h — 가장 아래층. 다른 ServerCore 헤더를 include하지 않는다
using int32  = std::int32_t;
using uint16 = std::uint16_t;
template<typename T>
using Atomic    = std::atomic<T>;
using Mutex     = std::mutex;
using LockGuard = std::lock_guard<std::mutex>;
```
```cpp
// CoreMacro.h — 2층 구조
#define STATIC_ASSERT(expr, msg)        static_assert((expr), msg)      // [1층] 구현은 여기 한 곳
#define STATIC_ASSERT_SIZE(type, size) \
	STATIC_ASSERT(sizeof(type) == (size), #type " must be " #size " bytes")   // [2층] 용도별

#define size32(val)   static_cast<int32>(sizeof(val))
#define len32(arr)    static_cast<int32>(sizeof(arr) / sizeof(arr[0]))
```
```cpp
// Types.cpp — 검증은 한 번만 컴파일되도록 .cpp에
STATIC_ASSERT_SIZE(int32, 4);
```

| 개념 | 설명 |
|---|---|
| **2층 매크로** | 실제 구현(`static_assert`)은 1층 한 곳에만 둔다. 방식을 바꿀 때 한 줄만 고치면 된다 |
| `#type` | 매크로 인자를 **문자열로** 바꾼다 → 에러 메시지 `"int32 must be 4 bytes"` |
| 의존 방향 | `CoreMacro.h → Types.h` 한 방향만. Types.h가 CoreMacro를 모르도록 검증 코드를 Types.cpp로 분리 |

### 🔧 강의와 다른 점
- `_STOMP`(디버깅용 할당기)가 강의에서는 **항상 켜져** 있음 → 매우 느림. `#ifdef _DEBUG` 안에서만 켜도록 분리.

---

## Step 2. CRASH / ASSERT_CRASH (런타임)

### 🎯 목표
"절대 일어나면 안 되는 상황"에서 **즉시 프로세스를 죽여** 크래시 덤프로 원인을 남긴다.

### 📖 개념
- 서버에서 잘못된 상태로 계속 돌면 데이터가 조용히 망가진다. **빨리, 크게 죽는 것**이 디버깅에 유리하다.
- 의도적으로 `nullptr`에 값을 써서 Access Violation을 낸다.

### 🧱 핵심 코드
```cpp
#define CRASH(cause)										\
	do {													\
		const char* volatile crashCause = (cause);			\
		(void)crashCause;									\
		volatile uint32* crash = nullptr;					\
		__analysis_assume(crash != nullptr);				\
		*crash = 0xDEADBEEF;								\
	} while (0)

#define ASSERT_CRASH(expr)									\
	do {													\
		if (!(expr))										\
		{													\
			CRASH("ASSERT_CRASH: " #expr);					\
			__analysis_assume(expr);						\
		}													\
	} while (0)
```

| 포인트 | 이유 |
|---|---|
| `do { ... } while (0)` | 매크로를 **문장 하나**처럼 만든다. `if (x) ASSERT_CRASH(y); else ...`에서도 깨지지 않음 |
| `volatile` | 컴파일러가 "의미 없는 코드"로 보고 최적화로 지우지 못하게 함 |
| `crashCause` 지역 변수 | 디버거/덤프에서 **원인 문자열**을 볼 수 있음 |
| `#expr` | 실패한 조건식 자체를 문자열로 남김 (`"ASSERT_CRASH: hp >= 0"`) |
| `__analysis_assume` | 정적 분석기의 null 역참조 경고를 끔 (실행에는 영향 없음) |
| 줄 끝 `\` 뒤 공백 금지 | 공백이 있으면 매크로가 그 줄에서 끝나 버린다 |

`OUT` 매크로: 아무 일도 하지 않음. **출력 매개변수 표시용** (`compare_exchange_strong(OUT expected, ...)`).

---

## Step 3. CoreTLS (Thread Local Storage)

### 🎯 목표
스레드마다 **따로 저장 공간을 갖는** 전역 변수를 만든다.

### 📖 개념
- `thread_local`: 이름은 하나지만 **스레드마다 별도 복사본**이 있다. 다른 스레드와 공유하지 않으므로 **락이 필요 없다**.
- 이름 규칙: `G` = 전역 1개, `L` = 스레드마다 1개.

### 🧱 핵심 코드
```cpp
// CoreTLS.h
extern thread_local uint32 LThreadId;
// CoreTLS.cpp
thread_local uint32 LThreadId = 0;
```

### 🧪 실험 결과
```
main:   LThreadId = 100
worker: before = 0     ← 메인의 100이 보이지 않는다 (자기 복사본은 초기값 0)
worker: after  = 200
main:   LThreadId = 100  ← 워커가 바꾼 200이 보이지 않는다
```

---

## Step 4. ThreadManager

### 🎯 목표
스레드 생성, TLS 초기화, Join을 한 곳에서 관리한다.

### 🧱 핵심 코드
```cpp
void ThreadManager::Launch(std::function<void(void)> callback)
{
	LockGuard guard(lock_);              // threads_ (vector)를 여러 스레드가 동시에 건드릴 수 있음
	threads_.push_back(std::thread([=]()
		{
			InitTLS();                   // 스레드 시작 시 TLS 초기화 (LThreadId 발급)
			callback();
			DestroyTLS();
		}));
}

void ThreadManager::InitTLS()
{
	static Atomic<uint32> SThreadId = 1; // 여러 스레드가 동시에 발급받으므로 Atomic
	LThreadId = SThreadId.fetch_add(1);
}
```

| 포인트 | 설명 |
|---|---|
| 생성자에서 `InitTLS()` | ThreadManager를 만든 스레드(= 메인)도 ID를 받는다 → 메인 = 1 |
| 소멸자에서 `Join()` | 객체가 사라지기 전에 모든 스레드가 끝나도록 보장 |
| `Join()` 후 `threads_.clear()` | 끝난 `std::thread` 객체 정리 → 다시 Launch 가능 |

> 📝 **내 메모 — 왜 `[=]` 캡처인가?**
> `callback`은 `Launch` 함수의 매개변수(지역 변수)다. 참조(`[&]`)로 캡처하면 `Launch`가 반환된 뒤 스택에서 사라진 `callback`을 새 스레드가 계속 쓰려고 해서 **댕글링 참조**가 된다. 값으로 복사(`[=]`)해서 스레드가 자기 복사본을 갖게 한다.

> ⚠️ **나중에 다룰 문제**: 워커가 `while (true)`로 무한히 돌면 `Join()`이 영원히 끝나지 않는다 → **종료 신호**가 필요 (Phase 1-4 이후, 강의 결함 8번 `CloseService`와 연결).

---

## Step 5. CoreGlobal

### 🎯 목표
전역 매니저들의 **생성/소멸 순서**를 한 곳에서 관리한다.

### 🧱 핵심 코드
```cpp
// CoreGlobal.h
extern class ThreadManager* GThreadManager;    // 선언만 (정의는 .cpp 한 곳)

class CoreGlobal
{
public:
	CoreGlobal();
	~CoreGlobal();
	CoreGlobal(const CoreGlobal&) = delete;            // 복사되면 소멸자가 두 번 → 이중 delete
	CoreGlobal& operator=(const CoreGlobal&) = delete;
};
```
```cpp
// CoreGlobal.cpp
ThreadManager* GThreadManager = nullptr;

CoreGlobal::CoreGlobal()
{
	ASSERT_CRASH(GThreadManager == nullptr);   // 두 번 만드는 실수 차단
	GThreadManager = new ThreadManager();
}
CoreGlobal::~CoreGlobal()
{
	delete GThreadManager;                     // 스레드부터 정리
	GThreadManager = nullptr;
}
```
```cpp
// main
int main()
{
	CoreGlobal coreGlobal;     // 첫 줄. main이 끝날 때 자동 소멸
	...
}
```

### 🔧 강의와 다른 점
- 강의는 `CoreGlobal`을 **전역 객체**로 둔다. 여기서는 **`main`의 지역 객체**로 만든다.
  - 정적 lib 안의 전역 객체는, 그 .obj를 아무도 참조하지 않으면 **링커가 빼 버릴 수 있다** → 생성자가 안 불림.
  - 전역 객체끼리는 **초기화 순서가 보장되지 않는다** (static initialization order fiasco).
  - 지역 객체로 두면 생성/소멸 시점이 코드에 드러난다. 클라이언트에서는 `NetworkManager`의 멤버로 둘 예정.

### 🧩 복습 질문
**Q1. 헤더에 `ThreadManager* GThreadManager = nullptr;`처럼 정의를 쓰면?**
> `#include`는 헤더 내용을 .cpp에 복사한다. include한 .cpp마다 같은 변수가 정의되어 **링크 오류 LNK2005 (ODR 위반)**. `#pragma once`는 **한 .cpp 안에서의** 중복만 막는다. `extern`은 선언만 하고, 정의는 CoreGlobal.cpp 한 곳에 둔다.

**Q2. 소멸자에서 ThreadManager를 다른 매니저보다 먼저 delete하는 이유는?**
> 워커 스레드가 다른 매니저(메모리, SendBuffer, JobQueue, DeadLockProfiler ...)를 **사용하는 쪽**이기 때문이다. 매니저를 먼저 지우면 아직 도는 스레드가 해제된 메모리에 접근한다 (use-after-free). **사용하는 쪽(스레드)을 먼저 없애고, 사용되는 쪽(매니저)을 나중에 없앤다.**

---

## Step 6. CorePch 정리 + 데이터 레이스 실험

### 🎯 목표
헤더 의존성을 정리하고, 보호 없는 공유 변수가 실제로 깨지는 것을 눈으로 확인한다.

### 📖 개념 1 — 자립 헤더 (include what you use)
- 각 헤더는 **자기가 쓰는 것을 직접 include**한다. pch 덕분에 우연히 빌드되는 상태에 기대지 않는다.
  - 예: `ThreadManager.h`가 `Mutex`를 쓰므로 `Types.h`를 직접 include.
- **헤더**에는 선언에 필요한 것만, **구현에만 필요한 것은 .cpp에** include한다.
  - 헤더에 넣으면 그 헤더를 include하는 모든 파일로 퍼진다 → 컴파일 시간 증가, 구현을 바꿀 때 전체 재빌드.
- `CorePch.h`는 의존성을 해결하는 곳이 아니라 **빌드 속도용 묶음**이다.

### 📖 개념 2 — `using namespace std`를 헤더에서 쓰지 않는다
- CorePch는 GameServer, DummyClient, 그리고 **클라이언트 엔진**에도 들어간다.
- 헤더의 `using namespace`는 include한 모든 파일로 퍼지고 **되돌릴 수 없다** → 엔진의 `HashTable`, `LinkedList` 등과 이름 충돌 위험.

### 📖 개념 3 — 데이터 레이스
`++count`는 한 줄이지만 CPU에서는 3단계다.
```
mov eax, [count]    ; 1. 읽기
add eax, 1          ; 2. 더하기
mov [count], eax    ; 3. 쓰기     ← 두 스레드가 같은 값을 읽으면 증가 하나가 사라진다
```
`atomic::fetch_add`는 `lock xadd` **명령 하나**로 처리 → 끼어들 틈이 없다.

### 🧪 실험 결과 (스레드 4개 × 1,000,000번 증가, Debug)
| 실행 | 보호 없음 | Atomic |
|---|---|---|
| 1 | 2,175,792 | 4,000,000 |
| 2 | 2,055,144 | 4,000,000 |
| 3 | 1,946,172 | 4,000,000 |

→ 보호 없는 변수는 **절반 가까이 유실**되고, 매번 값이 다르다.

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `#pragma once=`, `#include <functional>'` 오타 | 경고 C4081/C4067 (빌드는 됨) | 경고를 0개로 유지해야 진짜 문제가 보인다 |
| 테스트 함수 안에 `ASSERT_CRASH(hp >= 0)`을 활성 상태로 둠 | 호출하면 무조건 크래시 | 크래시 확인 코드는 분리하거나 막아 둔다 |
| TLS 테스트에서 메인 스레드의 `LThreadId = 100` | 이후 메인 스레드 ID가 100으로 남음 | 테스트가 전역 상태를 바꾸면 원래대로 돌려놓는다 |

### 🧩 복습 질문
**Q1. 카운터 1개를 증가시킬 때 Atomic과 Mutex 중 무엇이 적합한가? 왜?**
> (미답변) — [Phase 1-2 문서의 Atomic vs Mutex](Phase1-2_Synchronization.md) 참고 후 직접 적어 보기

**Q2. Release에서 보호 없는 카운터가 우연히 기댓값과 같게 나오는 이유는? 그러면 레이스가 없다고 할 수 있나?**
> (미답변)
