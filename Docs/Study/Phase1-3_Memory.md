# 🧠 Phase 1-3. 메모리

> 💡 **목표**: `new`/`delete`를 직접 감싸서 **할당기를 한 곳에서 교체**할 수 있게 하고, 그 위에 메모리 풀(빠른 재사용)과 Stomp 할당기(Use-After-Free 즉시 검출)를 얹는다.

| Step | 파일 | 한 줄 요약 | 상태 |
|---|---|---|---|
| 1 | `Allocator.h/.cpp`, `Memory.h` | BaseAllocator + xnew / xdelete (placement new) | ✅ |
| 2 | `Allocator.h/.cpp`, `Memory.h` | StompAllocator (VirtualAlloc, 해제 후 접근 즉시 크래시) + Debug 전용 `DefaultAllocator` | ✅ |
| 3-1 | `MemoryPool.h` | MemoryHeader (블록 앞 크기 머리말 + SLIST 노드) | ✅ |
| 3-2 | `MemoryPool.h/.cpp` | MemoryPool (SLIST Push/Pop) | ⏳ |

---

## Step 1. BaseAllocator + xnew / xdelete

### 🎯 목표
메모리 할당을 우리 함수로 감싸는 통로(`xnew`/`xdelete`)를 만들고, **할당과 생성자 호출을 분리**한다.

### 📖 개념
**`new`는 두 가지 일을 한다**
```
new Knight(100)   = ① operator new(sizeof(Knight))  메모리 확보
                  + ② Knight::Knight(100)           그 위에서 생성자 호출
delete k          = ① k->~Knight()                  소멸자 호출
                  + ② operator delete(k)            메모리 반납
```
- 이 둘을 직접 나누면 ①(할당기)을 마음대로 바꿀 수 있다 → 메모리 풀, Stomp 할당기.
- **placement new** `new(주소) Type(인자...)`: 메모리를 새로 잡지 않고 **이미 있는 주소 위에서 생성자만** 호출한다.
- 해제는 소멸자를 **직접 호출**(`obj->~Type()`)한 뒤 메모리를 반납한다.

**가변 인자 템플릿 + 완벽 전달**
- `Args&&... args`(전달 참조)로 받고 `std::forward<Args>(args)...`로 넘긴다 → lvalue는 lvalue로, rvalue는 rvalue로(move 생성자) 그대로 전달.

**static 멤버 변수는 선언과 정의가 따로다**
- 클래스 안의 `static int32 count;`는 "있다"는 **선언**일 뿐이다. 실제 메모리를 잡는 **정의**가 없으면 컴파일은 되지만 **링크 에러**(LNK2001 unresolved external symbol)가 난다.
- 해결: 클래스 밖 .cpp에 `int32 Foo::count = 0;` 한 번, 또는 C++17 **`inline static int32 count = 0;`** (헤더에 둬도 중복 정의 LNK2005가 없다).

### 🧱 핵심 코드
```cpp
// Allocator.h / .cpp
class BaseAllocator
{
public:
	static void* Alloc(int32 size);		// ::malloc(size)
	static void Release(void* ptr);		// ::free(ptr)
};
```
```cpp
// Memory.h
template<typename Type, typename... Args>
Type* xnew(Args&&... args)
{
	STATIC_ASSERT(alignof(Type) <= alignof(std::max_align_t), "over-aligned type is not supported");

	Type* memory = static_cast<Type*>(BaseAllocator::Alloc(static_cast<int32>(sizeof(Type))));	// ① 할당
	new(memory) Type(std::forward<Args>(args)...);													// ② 생성 (placement new)
	return memory;
}

template<typename Type>
void xdelete(Type* obj)
{
	if (obj == nullptr)		// delete nullptr과 같은 약속
		return;

	obj->~Type();					// ① 소멸
	BaseAllocator::Release(obj);	// ② 반납
}
```

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `xdelete`에 nullptr 검사 | `delete nullptr`은 안전한데 검사가 없으면 `nullptr->~Type()` UB. 대체 함수는 같은 약속을 지켜야 한다 |
| `STATIC_ASSERT`로 정렬 검사 | `max_align_t` 이하 정렬만 허용해서, `alignas(64)` 타입을 조용히 잘못된 주소에 만드는 것을 **컴파일 때** 막는다 (MSVC에서 `max_align_t`는 8이다 → Step 2 ⚠️ 참고) |
| `sizeof` → `static_cast<int32>` | x64에서 `size_t` → `int32` 축소 변환 경고 방지. 크기를 `int32`로 두는 건 이후 MemoryHeader·풀 테이블과 맞추기 위해 |
| `<new>`, `<utility>`, `CoreMacro.h` 직접 include | 각 헤더가 자기가 쓰는 것을 직접 include하는 프로젝트 규칙 (pch에 기대지 않음) |

> 📝 Phase 5(클라이언트) 메모: OogieEngine Debug pch의 `#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)`가 placement new를 깨뜨린다 → `#pragma push_macro("new")`로 처리할 예정.

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| static 멤버를 클래스 안에 선언만 함 | LNK2001 unresolved external symbol | static 멤버는 정의가 따로 필요. C++17이면 `inline static` |
| 테스트가 값을 출력만 함 | 사람이 눈으로 판정해야 함 → 테스트가 늘면 놓친다 | 테스트는 코드가 비교해서 OK/FAIL을 낸다 |
| 테스트 시작 시 static 카운터 초기화 안 함 | 다른 테스트가 먼저 쓰면 기댓값이 틀어짐 | 공유 상태는 테스트 시작 시 초기화 |
| 정상 테스트를 다시 주석 처리 | 메모리 코드 수정이 락을 깨도 모름 | 정상 테스트는 항상 켜 둔다 |
| 헤더를 스페이스로 들여씀 | 다른 파일(탭)과 불일치 | 붙여넣은 뒤 들여쓰기 확인 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `XnewXdelete` | 보조 클래스 `MemoryTestKnight`(생성/소멸 카운터). xnew → 생성자 1회, `hp_ == 100`, `attack_ == 10` / xdelete → 소멸자 1회 / `xdelete(nullptr)` → 소멸자 호출 없음 | `OK` (Debug/Release) |

> 💡 `xdelete(nullptr)`만 쓰면 `nullptr`의 타입이 `std::nullptr_t`라 `Type`을 추론할 수 없다 → 타입 있는 포인터 변수로 넘기거나 `xdelete<MemoryTestKnight>(nullptr)`.

> 📝 테스트 코드는 직접 작성한 버전(값 출력만)을 리뷰받은 뒤, 요청해서 Claude가 판정 로직을 넣은 버전을 받았다.

### 🧩 복습 질문
**Q1. `xdelete`에서 `obj->~Type()`을 빼먹고 `Release`만 하면 무엇이 문제일까? (멤버로 `std::vector`를 가진 클래스)**
> (미답변)

**Q2. `xnew`의 인자를 `Args&&` + `std::forward` 대신 `Args...`(값으로 받기)로 바꾸면 무엇이 달라질까?**
> (미답변)

---

## Step 2. StompAllocator

### 🎯 목표
Debug 빌드에서 **해제 후 접근(Use-After-Free)과 뒤로 넘어가는 오버플로가 일어나는 그 줄에서 바로 크래시**가 나게 만드는 할당기를 만든다.

### 📖 개념 1 — Use-After-Free(UAF)란
**이미 해제한 메모리를 가리키는 포인터(dangling pointer)로 읽거나 쓰는 것.** C++ 표준에서는 미정의 동작(UB)이다.

```cpp
Knight* knight = xnew<Knight>(100);
xdelete(knight);        // 메모리 반납. 하지만 knight 변수에는 옛 주소가 그대로 남아 있다
knight->hp_ = 50;       // ← Use-After-Free
```
- `xdelete`는 포인터 변수를 `nullptr`로 바꿔 주지 않는다. 그리고 **같은 주소를 다른 곳에 복사해 둔 포인터**는 아무도 바꿔 줄 수 없다.
- 그래서 "해제 후 `nullptr` 대입"만으로는 UAF를 막을 수 없다.

### 📖 개념 2 — 왜 `malloc`/`free`에서는 UAF가 안 잡히나
`free()`는 메모리를 OS에 돌려주지 않고 **힙이 계속 들고 있다가** 다음 `malloc`에 재사용한다. 주소는 여전히 유효한 페이지 안에 있으니 CPU 입장에서는 아무 문제가 없다.

| 해제 후 하는 일 | 실제로 일어나는 일 | 증상이 드러나는 시점 |
|---|---|---|
| 읽기 | 옛 값이나 힙 관리 정보(쓰레기)를 읽음 | 잘못된 값으로 계속 실행 → 한참 뒤 엉뚱한 로직 오류 |
| 쓰기 (재사용 전) | 힙의 빈 블록 관리 정보를 덮어씀 | 나중의 `malloc`/`free` 안에서 힙 손상 크래시 |
| 쓰기 (재사용 후) | **그 주소를 새로 받은 다른 객체**를 덮어씀 | 전혀 상관없는 객체의 값이 바뀜 → 원인 추적이 거의 불가능 |

> ⚠️ UAF의 진짜 위험은 크래시가 아니라 **크래시가 나지 않는 것**이다. 버그가 있는 줄이 아니라 몇 초 뒤, 다른 스레드, 다른 객체에서 증상이 나타난다.

> 💡 MSVC Debug CRT는 해제한 블록을 `0xDD`로 채운다. 그래서 디버거에서 `0xDDDDDDDD`가 보이면 UAF를 의심할 수 있다. 하지만 이것은 **읽은 값을 사람이 보고 알아채는 것**일 뿐, 접근하는 순간 멈춰 주지는 않는다.

### 📖 개념 3 — 게임 서버에서 UAF가 생기는 전형적인 상황
| 상황 | 흐름 |
|---|---|
| 접속 종료와 Job의 경쟁 | 세션이 끊겨 `Player`를 해제했는데, 그 전에 Room JobQueue에 넣어 둔 Job이 `Player*`를 들고 있다가 나중에 실행됨 |
| 컨테이너에서 삭제 | `Room`의 플레이어 목록에서 지우고 해제했는데, 브로드캐스트 루프가 미리 복사해 둔 포인터로 접근 |
| 다른 스레드의 해제 | IOCP 워커 A가 세션을 정리하는 동안 워커 B가 같은 세션에 Send |

- 멀티스레드에서는 **해제하는 스레드와 접근하는 스레드가 다르다.** 그래서 순서가 실행마다 달라지고, 재현이 어렵다.
- 이후 Phase에서 `shared_ptr`(Job이 객체를 붙잡아 수명 연장), `weak_ptr`(살아 있는지 확인 후 접근), "Room 상태는 Room JobQueue 안에서만" 규칙으로 **구조적으로** 막는다. StompAllocator는 그 구조에 구멍이 있을 때 **개발 중에 바로 찾아내는 안전망**이다.

### 📖 개념 4 — StompAllocator가 UAF를 잡는 원리
핵심은 **해제한 메모리를 "접근 불가능한 주소"로 만드는 것**이다. 검사 코드를 넣는 게 아니라 CPU의 페이지 보호를 이용한다.

| | `malloc`/`free` | `StompAllocator` |
|---|---|---|
| 할당 단위 | 힙 안의 작은 블록 (객체들이 붙어 있음) | 객체마다 **별도의 OS 페이지** (`VirtualAlloc`) |
| 해제 | 힙에 반납 (페이지는 그대로 유효) | `VirtualFree(MEM_RELEASE)`로 **페이지를 OS에 반납** |
| 해제 후 접근 | 조용히 성공 | **그 명령어에서 즉시 Access Violation** |
| 다른 객체 오염 | 가능 | 불가능 (객체마다 페이지가 따로라 이웃이 없음) |

해제 후 접근할 때 일어나는 일:
```
1. xdelete(knight) → StompAllocator::Release → VirtualFree(MEM_RELEASE)
2. OS가 페이지 테이블에서 그 페이지를 지운다
3. knight->hp_ = 50; 실행
4. CPU(MMU)가 주소 변환 시도 → 페이지 테이블에 없음 → 페이지 폴트
5. OS: 이 프로세스에 할당된 주소가 아님 → 0xC0000005 Access Violation
6. 디버거가 정확히 knight->hp_ = 50; 줄에서 멈춘다 (호출 스택에 범인이 그대로 남는다)
```

> 📝 **증상이 나타나는 위치를 버그가 있는 위치로 끌어당기는 것**이 StompAllocator의 가치다. "몇 초 뒤 다른 곳에서 이상한 값"이 "지금 이 줄에서 크래시"로 바뀐다.

### 📖 개념 5 — 페이지 끝에 붙이는 이유 (오버플로까지 잡기)
- 데이터를 페이지 **시작**에 두면 객체 뒤에 남은 공간도 같은 페이지라서, 뒤로 넘어가 써도 잡히지 않는다.
- 페이지 **끝**에 붙이면 객체 바로 뒤가 할당되지 않은 주소가 된다 → 1바이트만 넘어가도 즉시 크래시.
- 반환 주소는 `malloc`과 같은 16바이트 정렬을 지켜야 하므로 size를 16의 배수로 올려서 붙인다. 그래서 **올림으로 생긴 여분(최대 15B) 안의 오버플로는 잡지 못한다.**

```
[ 페이지 4096B                                 ]
[ ......... 빈 공간 (안 씀) ........ | 객체 | 여분 ]| ← 여기부터 미할당 → 넘어가면 크래시
^ VirtualAlloc 반환값(base)          ^ Alloc 반환값
```
- `Release`는 반환 주소를 4KB 경계로 내려서(`address - address % PAGE_SIZE`) base를 되찾는다. 앞쪽 빈 공간은 항상 한 페이지보다 작으니 반환 주소는 언제나 첫 페이지 안에 있기 때문이다.

### 📖 개념 6 — StompAllocator로도 못 잡는 UAF와 비용
| 한계 | 이유 |
|---|---|
| 주소 재사용 | 해제 후 다시 `VirtualAlloc`하면 OS가 **같은 주소를 다시 줄 수 있다.** 그 뒤의 UAF는 새 객체에 접근하게 되어 잡히지 않는다 (짧은 테스트에서는 드묾) |
| 다른 할당기를 거친 메모리 | `std::vector` 내부 버퍼처럼 `xnew`를 안 거친 메모리는 대상이 아니다 (나중에 `StlAllocator`로 연결) |
| 메모리 | 8바이트 객체에도 RAM 4KB + 주소 공간 64KB |
| 속도 | 할당/해제마다 시스템 콜 → `malloc`보다 훨씬 느림 |

→ 그래서 **Debug 전용**이다. Release에서는 실제 할당기(지금은 `BaseAllocator`, 나중에는 `PoolAllocator`)를 쓴다.

### 📖 개념 7 — VirtualAlloc / VirtualFree
```cpp
LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);	// 실패: NULL
BOOL   VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);							// 실패: 0 (FALSE)
```
| 상태 | 주소 사용 | 실제 메모리 | 접근 |
|---|---|---|---|
| 아무것도 안 함 | 다른 할당이 가져갈 수 있음 | 없음 | ❌ AV |
| `MEM_RESERVE` | 나만 씀 | 없음 | ❌ AV |
| `MEM_RESERVE \| MEM_COMMIT` | 나만 씀 | 있음 | ✅ |

| `dwFreeType` | 하는 일 | 해제 후 |
|---|---|---|
| `MEM_DECOMMIT` | COMMIT만 취소 | 주소는 RESERVE로 남음 (64KB씩 주소 공간 누수) |
| `MEM_RELEASE` | RESERVE까지 취소 | 주소 공간 전체 반납. **`dwSize`는 반드시 0** |

- `VirtualAlloc` 반환 주소는 64KB 경계(할당 단위)라서 4KB 경계에도 맞다.
- Win32 `BOOL` 성공은 "0이 아닌 값"이다 → `== TRUE`가 아니라 `!= FALSE`(또는 그대로)로 검사한다.

### 📖 개념 8 — 포인터 산술과 `std::uintptr_t`
| 포인터에 허용되는 연산 | 예 |
|---|---|
| 포인터 ± 정수 | `p + 16` (타입 크기 단위로 이동) |
| 포인터 − 포인터 | `p2 - p1` |
| 비교 | `p1 < p2` |

- `%`, `*`, `/`, `&`는 포인터에 쓸 수 없다 → "주소가 4096의 배수인가" 같은 계산은 `reinterpret_cast<std::uintptr_t>(ptr)`로 정수로 바꿔서 한다.
- 바이트 단위로 이동하려면 **1바이트 타입 포인터**(`int8*`)로 바꾼 뒤 더한다. `int32*`에 `offset`을 더하면 `offset × 4`바이트를 이동한다.
- 올림은 비트 마스크(`& ~(N - 1)`) 대신 `(v + N - 1) / N * N`로 쓴다. 상수가 2의 거듭제곱이면 컴파일러가 같은 명령어로 바꿔 주므로 성능 차이가 없다.

### 🧱 핵심 코드
```cpp
// Allocator.h
class StompAllocator
{
public:
	// size는 1 이상. 실패하면 크래시.
	static void* Alloc(int32 size);

	// Alloc이 돌려준 주소만 받는다. nullptr이면 아무 일도 하지 않는다
	static void Release(void* ptr);

private:
	static constexpr int32 PAGE_SIZE = 0x1000;
	// x64 Windows malloc과 같은 16바이트 (MEMORY_ALLOCATION_ALIGNMENT, 이후 SLIST도 16을 요구)
	static constexpr int32 ALIGNMENT = 16;
};
```
```cpp
// Allocator.cpp
void* StompAllocator::Alloc(int32 size)
{
	ASSERT_CRASH(size > 0 && size <= INT32_MAX - PAGE_SIZE);	// 올림 계산이 int32를 넘지 않도록 상한도 확인

	// ① 정렬 올림  ②③ 페이지 단위 올림  ④ 앞쪽 빈 공간
	const int32 alignedSize = (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
	const int32 totalSize = (alignedSize + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
	const int32 offset = totalSize - alignedSize;

	// ⑤ 페이지 단위로 받는다
	void* base = ::VirtualAlloc(nullptr, totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	ASSERT_CRASH(base != nullptr);

	// ⑥ 데이터 끝 = 페이지 끝
	return static_cast<int8*>(base) + offset;
}

void StompAllocator::Release(void* ptr)
{
	if (ptr == nullptr)
		return;

	// 4KB 경계에서 떨어진 만큼(= offset) 앞으로 돌아가면 base
	const uintptr remainder = reinterpret_cast<uintptr>(ptr) % PAGE_SIZE;
	void* base = static_cast<int8*>(ptr) - remainder;

	BOOL res = ::VirtualFree(base, 0, MEM_RELEASE);
	ASSERT_CRASH(res);
}
```
```cpp
// Memory.h — 할당기 선택은 이 한 곳에서만
#ifdef _DEBUG
using DefaultAllocator = StompAllocator;
#else
using DefaultAllocator = BaseAllocator;
#endif
// xnew → DefaultAllocator::Alloc, xdelete → DefaultAllocator::Release
```

> 💡 **실패 처리를 다르게 정한 이유**: `Alloc` 실패는 진짜 오류이고 `xnew`가 nullptr을 검사하지 않으므로 **그 자리에서 크래시**. `Release(nullptr)`은 정상 사용(`free(nullptr)`, `delete nullptr`과 같은 약속)이고, `BaseAllocator`와 같은 입력에 같은 동작을 해야 바꿔 끼울 수 있으므로 **아무것도 안 함**.

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| **Debug에서만 Stomp 사용** (`#ifdef _DEBUG` → `using DefaultAllocator = ...`) | 강의는 `#define _STOMP`가 항상 켜져 있어 Release 성능까지 떨어진다 (강의 결함 #5). `_DEBUG`는 VS가 Debug 구성에서 자동으로 정의한다 |
| 매크로 대신 `using` 타입 별칭 | 네임스페이스 규칙을 따르고, 디버거·IntelliSense에서 실제 타입으로 보이고, 같은 이름 토큰을 엉뚱하게 바꾸지 않는다 |
| **반환 주소 16바이트 정렬 보장** | size를 그대로 끝에 붙이면 `size = 4`일 때 주소가 `...FFC`가 된다. x64 malloc과 같은 정렬을 할당기가 바뀌어도 지키기 위해 |
| `VirtualFree` 반환값 검사 | 인자를 잘못 넣으면(`dwSize ≠ 0`, 다른 할당기의 주소) 조용히 실패해서 누수 + UAF 미검출이 된다 |
| 상수와 크기 계산을 모두 `int32`로 (`static constexpr int32 PAGE_SIZE`, `ALIGNMENT`) | `Alloc(int32 size)`, 이후 MemoryHeader·풀 테이블과 타입 통일 → 부호 있음/없음 혼합과 캐스팅이 사라진다. `static constexpr`은 C++17에서 자동 `inline`이라 .cpp 정의 불필요. 대신 `size + PAGE_SIZE - 1`이 int32를 넘지 않도록 상한 `ASSERT_CRASH` |
| 주소 계산에 `uintptr` (`Types.h`의 `std::uintptr_t` 별칭) | 포인터를 정수로 바꿀 때는 포인터 크기의 부호 없는 정수가 맞다. `Types.cpp`에서 `sizeof(void*)`와 같은지 검증. 크기는 int32로 바꿔도 주소는 64비트라 바꾸면 안 된다 |
| `<Windows.h>`는 .cpp에만 | 헤더에 넣으면 ServerCore를 include하는 모든 파일(클라이언트 엔진 포함)로 퍼진다 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `ALIGNMENT = alignof(std::max_align_t)`로 정함 (16이라고 가정) | MSVC에서는 **8**이라 `StompAllocFree`가 Debug/Release 모두 FAIL (`address ...ff8`, `aligned 0`) | 정렬 같은 값은 가정하지 말고 테스트로 확인. `max_align_t`는 컴파일러마다 다르다 (GCC/Clang 16, MSVC 8). x64 Windows malloc의 실제 정렬은 16 |
| 헤더 약속("실패하면 nullptr")과 코드(`ASSERT_CRASH`)가 다름 | 사용하는 쪽이 어느 쪽을 믿어야 할지 모름 | 주석의 약속과 코드는 항상 일치시킨다. 하나를 고치면 다른 쪽도 고친다 |
| `Release`에서 nullptr을 크래시로 처리 | Debug(Stomp)에서만 크래시, Release(Base)는 정상 → 구성마다 동작이 다름 | 바꿔 끼우는 할당기는 같은 입력에 같은 의미로 동작해야 한다 |
| `VirtualFree` 반환값 무시 | 실패해도 모름 | 실패할 수 있는 API는 결과를 변수로 받아 검사 (ASSERT 괄호 안에 호출을 직접 넣지 않기) |
| 포인터에 `%`를 쓰려 함 | 컴파일 에러 | 포인터 산술은 ±정수, 포인터−포인터, 비교뿐. 정수 계산은 `uintptr_t` |
| 스페이스/탭 들여쓰기 섞임 | 파일 안에서 불일치 | 붙여넣은 뒤 들여쓰기 확인 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `StompAllocFree` | `StompAllocator`를 직접 호출 (Release에서도 테스트됨). 크기 8 / 4096 / 5000마다 16바이트 정렬, `(주소 + 올린 크기) % 4096 == 0`(페이지 끝), 전체 쓰기/읽기, 해제. 마지막에 `Release(nullptr)` | `OK` (Debug/Release) |
| `CrashStompUseAfterFree` | `xnew<MemoryTestKnight>` → `xdelete` → `knight->hp_ = 50` | Debug: 그 줄에서 Access Violation / Release: `FAIL` 출력이 정상 (BaseAllocator) |
| `CrashStompOverflow` | `StompAllocator::Alloc(16)` → `bytes[16] = 1` | Debug/Release 모두 그 줄에서 Access Violation |

> 📝 2026-10-27: 정상 테스트 Debug/Release 통과, 크래시 테스트 2개 직접 확인.

> 💡 정상 테스트만으로는 "오류를 잡는 기능"을 확인할 수 없다. `Alloc`/`Release`가 임시 `malloc`/`free` 코드였어도 `XnewXdelete`는 통과했다. **오류를 일부러 만들어서** 크래시가 나는지 봐야 한다.

> 📝 테스트 코드는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `int32` 3개(12B)를 할당하고 `[3]`에 쓰면 크래시가 날까? 이유는?**
> (미답변)

**Q2. 앞쪽으로 넘어가는 접근(`arr[-1]`, Underflow)은 이 방식으로 잡힐까? 안 잡힌다면 어떻게 바꿔야 잡을 수 있을까?**
> (미답변)

**Q3. `(4096 + 4096 - 1) / 4096 * 4096`과 `(4097 + 4096 - 1) / 4096 * 4096`의 결과는?**
> (미답변)

**Q4. 올림 공식에서 `+ N - 1` 대신 `+ N`을 더하면 어떤 입력에서 틀릴까?**
> (미답변)

**Q5. `MEM_COMMIT` 없이 `MEM_RESERVE`만 주고 반환 주소에 값을 쓰면?**
> 💬 (해설 요청) 쓰는 순간 Access Violation. RESERVE는 주소 범위만 내 것으로 표시하고 실제 메모리를 연결하지 않는다. `PAGE_READWRITE`도 커밋된 페이지에만 의미가 있다. RESERVE만 하는 용도는 큰 범위를 미리 잡고 필요할 때 조금씩 COMMIT하는 것 (예: 스레드 스택 1MB 예약 후 쓰는 만큼 커밋).

**Q6. `static_cast<int32*>(base) + offset`으로 잘못 쓰면?**
> ✅ offset × 1바이트가 아니라 offset × 4바이트 이동해서 주소가 잘못된다.
> 💬 보충: 예시 A에서 `0xFF0 × 4 = 0x3FC0` → 받은 4KB 밖 → 생성자에서 바로 AV. 여러 페이지를 받은 큰 객체라면 할당 범위 안에 떨어져서 **조용히** 틀릴 수도 있다 (페이지 끝 배치가 깨져 오버플로 검출이 사라짐).

**Q7. `Alloc`에서는 `int8*`로 더했는데 `Release`에서는 왜 `std::uintptr_t`가 필요할까?**
> 💬 (질문 중 해설) 포인터에는 `%`를 쓸 수 없다. 4KB 경계에서 떨어진 거리(나머지)를 구하려면 주소를 정수로 바꿔야 한다. 빼기 자체는 다시 `int8*`로 할 수 있다.

**Q8. `VirtualFree`의 `dwSize`에 0 대신 4096을 넣으면?**
> ✅ (코드 주석으로 답함) 할당된 크기는 OS가 기억하므로 MEM_RELEASE에서는 0이어야 한다.
> 💬 보충: 넣으면 FALSE 반환(`ERROR_INVALID_PARAMETER` 87), 아무것도 해제되지 않는다. 크래시가 없으니 **누수 + 해제 후 접근이 성공(UAF 미검출)**이 조용히 생긴다 → 반환값 검사가 필요한 이유.

**Q9. Debug에서 `xnew`로 받은 객체를 `::free()`로 해제하면?**
> ❌ 내 답: 페이지 단위로 크게 잡았는데 데이터 크기만 제거되어 메모리 누수.
> 💬 보충: `free`는 크기를 받지 않고, 포인터 **바로 앞의 힙 헤더**를 읽어 동작한다. Stomp가 준 주소 앞에는 헤더가 없으므로 Debug CRT가 `_CrtIsValidHeapPointer` 단언 실패/크래시를 낸다 (표준상 UB). 할당과 해제는 반드시 짝(`malloc↔free`, `VirtualAlloc↔VirtualFree`, `xnew↔xdelete`)을 맞춰야 한다.

**Q10. `XnewXdelete`가 Debug에서 통과하면 StompAllocator가 UAF를 잡는다고 확신할 수 있나?**
> 💬 (모르겠다 → 해설) 없다. 정상 테스트는 정상 경로만 지나간다. 임시 `malloc`/`free` 코드였어도 통과했을 것이다. 잡는 기능은 일부러 UAF/오버플로를 내는 크래시 테스트로 확인한다.

**Q11. `CrashStompOverflow`에서 크기 8을 요청하고 인덱스 8에 쓰면 크래시가 날까?**
> (미답변)

---

## Step 3-1. MemoryHeader

### 🎯 목표
풀이 나눠 줄 메모리 블록의 맨 앞에 **크기 정보를 담은 머리말(header)**을 붙이고 떼는 구조체를 만든다.

### 📖 개념 1 — 왜 머리말이 필요한가
- 해제 함수는 `xdelete(ptr)`처럼 **포인터 하나만** 받는다. 그런데 풀은 크기별로 나뉘므로 "어느 풀로 돌려보낼지"를 알아야 한다.
- 블록 앞에 크기를 적어 두고, 해제할 때 헤더 크기만큼 앞으로 가서 읽는다.
- 💡 `free(ptr)`가 포인터 앞의 숨은 힙 헤더를 읽는 것(Step 2 Q9)과 같은 아이디어를 직접 만드는 것이다.

> 💡 **비유**: 크기별 선반이 있는 그릇 대여점. 손님은 그릇만 반납하므로 그릇 바닥에 "중형" 스티커(= MemoryHeader의 `allocSize_`)를 붙여 둔다.

```
주소:  1000           1016
        |<- 헤더 16B ->|<------ 사용자 데이터 ------>|
        | Next | size  |                             |
        ^ 블록 시작     ^ 사용자에게 주는 주소
  AttachHeader: 1000 → 1016 (header + 1)
  DetachHeader: 1016 → 1000 (ptr - 1)
```

### 📖 개념 2 — SLIST_ENTRY 상속 (침투형 연결 리스트)
| 블록 상태 | 헤더에서 쓰는 칸 |
|---|---|
| 사용자가 쓰는 중 | `allocSize_` (요청 크기) |
| 풀에서 쉬는 중 | `Next` (다음 빈 블록) → 빈 블록들이 줄줄이 연결 |

- `SLIST_ENTRY`는 Windows의 **lock-free 단일 연결 리스트 노드**다 (`Next` 포인터 하나).
- 노드를 따로 할당하지 않고 **블록 자신이 노드가 된다** (intrusive list).

### 📖 개념 3 — 16바이트 정렬
- x64 SLIST는 16바이트 원자적 비교교환(`cmpxchg16b`)을 쓰므로 **노드는 16바이트 경계**에 있어야 한다.
- 데이터 주소(`header + 1`)도 16바이트 정렬이 되려면 **헤더 크기가 16의 배수**여야 한다 → `STATIC_ASSERT`로 검사.
- 실제 `sizeof(MemoryHeader)`는 **16**: `Next` 8B 뒤에 정렬로 남는 공간에 `allocSize_` 4B가 들어간다.

### 📖 개념 4 — 포인터 ± 1은 "타입 1개만큼"
- `MemoryHeader* + 1` = `sizeof(MemoryHeader)`(16)바이트 뒤. Step 2의 `int8*` 규칙을 이번에는 **일부러 이용**한다.
- `+ 1`(Attach)과 `- 1`(Detach)은 **같은 타입 기준**이어야 정확히 되돌아온다.

### 🧱 핵심 코드
```cpp
// MemoryPool.h — ServerCore 내부 전용 (Windows.h가 바깥으로 퍼지지 않게 CorePch.h/Memory.h에서 include하지 않는다)
constexpr int32 SLIST_ALIGNMENT = MEMORY_ALLOCATION_ALIGNMENT;

struct alignas(SLIST_ALIGNMENT) MemoryHeader : public SLIST_ENTRY
{
	explicit MemoryHeader(int32 size) : allocSize_(size) {}

	static void* AttachHeader(MemoryHeader* header, int32 size)
	{
		new(header) MemoryHeader(size);   // placement new
		return header + 1;                // 헤더 1개 뒤 = 데이터 시작
	}

	static MemoryHeader* DetachHeader(void* ptr)
	{
		return static_cast<MemoryHeader*>(ptr) - 1;
	}

	int32 allocSize_ = 0;
};

STATIC_ASSERT(sizeof(MemoryHeader) % SLIST_ALIGNMENT == 0, "MemoryHeader size must be a multiple of SLIST_ALIGNMENT");
```

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `enum` 상수 → `constexpr int32 SLIST_ALIGNMENT = MEMORY_ALLOCATION_ALIGNMENT` | 타입이 있는 상수. 숫자 대신 OS 정의 값 사용 |
| `DECLSPEC_ALIGN` → 표준 `alignas` | MSVC 전용 매크로 대신 C++11 표준 |
| `++header` 후 `reinterpret_cast<void*>` → `return header + 1` | 매개변수를 바꾸지 않고 의도가 바로 읽힘. `void*` 변환은 암시적 |
| `reinterpret_cast<MemoryHeader*>(ptr)` → `static_cast` | `void*` → `T*`는 `static_cast`로 충분. `reinterpret_cast`는 포인터 ↔ 정수에만 |
| `STATIC_ASSERT`로 헤더 크기 검사 | 필드를 추가하다 크기가 어긋나면 컴파일 단계에서 잡음 |
| Windows.h를 내부 헤더에만 | 강의는 pch에 있어 어디서나 보인다. 클라이언트 엔진으로 매크로(`min`/`max` 등)가 퍼지지 않게 막는다 |
| `allocSize` → `allocSize_` | 프로젝트 멤버 네이밍 규칙 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| 테스트에서 쓰려고 `CorePch.h`에 `MemoryPool.h` 추가 | Windows.h가 GameServer/DummyClient/클라이언트 전부로 퍼짐 | 내부 헤더는 필요한 .cpp에서 직접 include. 테스트도 마찬가지 |
| 파일을 폴더에만 만들고 프로젝트에 등록 안 함 (VS 프로젝트 미저장) | 솔루션 탐색기·다른 PC에서 빠짐 | 추가 후 `Ctrl+Shift+S` 모두 저장, `.vcxproj`/`.filters`도 커밋 |
| C 스타일 캐스팅 `(int8*)data` | 어떤 캐스팅이 되든 조용히 통과 → 실수를 숨김 | `static_cast` / `reinterpret_cast`를 명시 |
| 포인터 차이(`ptrdiff_t`, 64비트)를 `int32`에 그냥 대입 | 경고 C4244 | `static_cast<int32>(...)`로 명시 |
| 스페이스 들여쓰기 | 다른 파일과 불일치 | `편집 → 고급 → 탭 지정` |
| (Claude) `sizeof(MemoryHeader)`를 32로 설명 | 테스트 출력으로 실제 16임을 확인 | 크기·정렬은 추측하지 말고 테스트로 출력해서 확인 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `MemoryHeaderAttachDetach` | `BaseAllocator::Alloc(헤더 + 64)` → `AttachHeader` → 바이트 차이 == `sizeof(MemoryHeader)`, 데이터 주소 16바이트 정렬, 데이터 64B를 다 써도 `allocSize_ == 64`, `DetachHeader` == 블록 시작 → 블록 시작으로 해제 | `OK (sizeof(MemoryHeader) = 16)` (Debug/Release) |

> 📝 테스트 틀은 직접 작성(오프셋, Detach 확인)하고, 나머지 단계와 판정 출력은 요청해서 Claude가 완성했다.

### 🧩 복습 질문
**Q1. `DetachHeader`에 MemoryHeader를 거치지 않은 주소(예: `StompAllocator::Alloc`의 주소)를 넘기면?**
> 🔺 내 답: 크래시 발생.
> 💬 보충: `DetachHeader` 자체는 주소 계산(`ptr - 16`)만 하므로 크래시가 나지 않는다. 문제는 `allocSize_`를 **읽을 때**다. Stomp 작은 객체면 같은 페이지의 0을 읽어 조용히 틀리고, offset 0이면 이전 페이지라 AV, malloc 주소면 힙 헤더/다른 객체를 크기로 읽어 나중에 힙 손상. **가장 흔한 결과는 크래시가 아니라 조용히 틀린 값**이다.

**Q2. `return header + 1;`을 `return reinterpret_cast<int8*>(header) + 1;`로 바꾸면?**
> ✅ 내 답: header는 헤더 크기 단위로 포인터 연산을 해야 하는데 `int8*`로 바꾸면 의도와 다르게 1바이트만 이동한다.
> 💬 보충: 블록 1000이면 1001을 준다 → ① 데이터가 헤더(`Next`, `allocSize_`)를 덮음 ② 16바이트 정렬 깨짐 ③ `DetachHeader(1001)`은 −16바이트로 985를 돌려줘 블록 시작과 어긋남 → 잘못된 주소로 해제.
