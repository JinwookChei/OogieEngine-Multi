# 🧠 Phase 1-3. 메모리

> 💡 **목표**: `new`/`delete`를 직접 감싸서 **할당기를 한 곳에서 교체**할 수 있게 하고, 그 위에 메모리 풀(빠른 재사용)과 Stomp 할당기(Use-After-Free 즉시 검출)를 얹는다.

> 🗺️ 인터랙티브 다이어그램: Docs/Diagrams/Phase1-3_Memory.html — 입구별 할당 경로(Debug/Release), Stomp 페이지 배치 계산기, 크기 → 풀, MemoryPool Push/Pop 직접 조작, Vector 재할당, ObjectPool, 두 MakeShared의 반납 시점

| Step | 파일 | 한 줄 요약 | 상태 |
|---|---|---|---|
| 1 | `Allocator.h/.cpp`, `Memory.h` | BaseAllocator + xnew / xdelete (placement new) | ✅ |
| 2 | `Allocator.h/.cpp`, `Memory.h` | StompAllocator (VirtualAlloc, 해제 후 접근 즉시 크래시) + Debug 전용 `DefaultAllocator` | ✅ |
| 3-1 | `MemoryPool.h` | MemoryHeader (블록 앞 크기 머리말 + SLIST 노드) | ✅ |
| 3-2 | `MemoryPool.h/.cpp` | MemoryPool (SLIST lock-free Push/Pop, 사용/예약 카운트) | ✅ |
| 4-1 | `Memory.h/.cpp`, `CoreGlobal` | Memory (크기별 풀 48개 + `poolTable_`, 큰 블록 직접 할당, `GMemory`) | ✅ |
| 4-2 | `Allocator.h/.cpp`, `Memory.h` | PoolAllocator (Release의 `DefaultAllocator` 교체) | ✅ |
| 5 | `Memory.h` | MakeShared (`xnew` + 삭제자 `xdelete<Type>`로 SharedPtr 생성) | ✅ |
| 6-1 | `Memory.h`, `Types.h` | StlAllocator (STL 컨테이너가 `DefaultAllocator`를 쓰게 하는 어댑터, `USize` 별칭) | ✅ |
| 6-2 | `Memory.h` | MakeShared → `std::allocate_shared` + `StlAllocator` (컨트롤 블록 + 객체를 한 번에) | ✅ |
| 7 | `Container.h`, `CorePch.h` | 컨테이너 별칭 (`Vector`, `Map`, `HashMap`, `Queue`, `String` ...) | ✅ |
| 8-1 | `ObjectPool.h/.cpp` | ObjectPool (타입 전용 풀, `ObjectPoolBase`로 MemoryPool/Windows.h를 .cpp에 감춤) | ✅ |
| 8-2 | `ObjectPool.h`, `Memory.h` | ObjectPool::MakeShared (객체는 전용 풀, 삭제자 `Push`, 컨트롤 블록은 `StlAllocator`) | ✅ |

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
> 💬 (해설 요청) 소멸자가 해제하던 **자원이 모두 새어 나간다**. 객체 자체의 메모리(`sizeof(Type)`)는 Release가 반납한다. 하지만 `std::vector`가 따로 힙에 잡은 버퍼는 vector 소멸자만 해제하므로 **메모리 누수**가 된다.
> 파일 핸들, `shared_ptr` 참조 카운트(상대 객체가 영원히 안 죽음), 락 해제 같은 정리 작업도 전부 빠진다. `xnew`가 생성자를 직접 불렀으니 `xdelete`도 소멸자를 직접 불러야 짝이 맞는다.

**Q2. `xnew`의 인자를 `Args&&` + `std::forward` 대신 `Args...`(값으로 받기)로 바꾸면 무엇이 달라질까?**
> 💬 (해설 요청) 인자의 **값 범주(lvalue/rvalue)가 사라지고 복사가 늘어난다**.
> - 값으로 받으면 호출할 때 매개변수로 한 번 복사/이동된다. 그리고 매개변수는 이름이 있는 lvalue라서 생성자에 넘길 때 **또 복사**된다. `std::vector` 같은 큰 인자는 2번 복사된다.
> - `std::unique_ptr`처럼 복사가 안 되는 타입은 생성자에 넘기는 줄에서 컴파일 에러가 난다 (`std::move`를 써야 함).
> - 생성자가 `T&`(참조)를 받는 경우, 원본이 아니라 **복사본**에 묶여서 원본 수정이 반영되지 않는다.
> 
> `Args&&` + `std::forward`(완벽 전달)는 lvalue는 lvalue 참조로, rvalue는 rvalue로 **받은 그대로** 생성자에 전달한다 → 복사 없이 `new Type(args...)`를 직접 쓴 것과 같은 결과다.

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
> 💬 (해설 요청) **안 난다.** 12는 16으로 올림되어 데이터 영역이 `[페이지끝 - 16, 페이지끝)`이 된다. `[3]`은 12~15번 바이트이므로 **정렬 여분 4바이트 안**이고, 할당된 페이지 안이라 CPU가 잡지 못한다. 헤더 주석의 한계 "여분(최대 15B) 안의 오버플로는 검출하지 못한다"에 해당하는 예다. `[4]`(16번 바이트)부터는 페이지 밖이라 즉시 AV.

**Q2. 앞쪽으로 넘어가는 접근(`arr[-1]`, Underflow)은 이 방식으로 잡힐까? 안 잡힌다면 어떻게 바꿔야 잡을 수 있을까?**
> 💬 (해설 요청) **안 잡힌다.** 데이터 앞쪽은 같은 페이지의 빈 공간(0~4080B)이라 읽기/쓰기가 가능하다. `arr[-1]`은 조용히 성공한다.
> - 잡으려면 반대로 배치한다. 데이터를 **페이지 시작**에 붙이고, 그 앞 페이지를 접근 금지로 둔다 (한 페이지를 더 RESERVE만 하거나 `VirtualProtect(..., PAGE_NOACCESS)`로 가드 페이지).
> - 대신 이번엔 뒤쪽 오버플로가 여분 공간에 묻힌다. 한 번에 양쪽을 다 잡을 수는 없어서, 보통은 흔한 쪽(뒤로 넘어감)을 기본으로 하고 앞쪽 검사는 옵션으로 둔다 (Windows PageHeap의 `/backwards` 옵션과 같은 아이디어).

**Q3. `(4096 + 4096 - 1) / 4096 * 4096`과 `(4097 + 4096 - 1) / 4096 * 4096`의 결과는?**
> 💬 (해설 요청) `(4096 + 4095) / 4096 * 4096` = `8191 / 4096 * 4096` = `1 * 4096` = **4096** (딱 1페이지).
> `(4097 + 4095) / 4096 * 4096` = `8192 / 4096 * 4096` = `2 * 4096` = **8192** (1바이트만 넘어도 2페이지).
> 정수 나눗셈은 버림이다. `N - 1`을 더해서 "나머지가 1 이상이면 한 칸 올라가게" 만드는 것이 올림의 원리다.

**Q4. 올림 공식에서 `+ N - 1` 대신 `+ N`을 더하면 어떤 입력에서 틀릴까?**
> 💬 (해설 요청) **이미 N의 배수인 입력**에서 틀린다. 한 칸 더 올라가 버린다.
> 예: `(4096 + 4096) / 4096 * 4096 = 8192` → 1페이지면 되는데 2페이지를 받는다. 결과가 "더 크게" 틀려서 크래시는 나지 않는다. 대신 메모리가 낭비되고, Stomp에서는 올린 크기가 4096일 때 앞쪽 빈 공간이 **딱 한 페이지**(8192 − 4096)가 되어, 반환 주소가 두 번째 페이지 시작이 된다 → `Release`의 "4KB 내림 = base" 계산이 base + 4096이라는 **틀린 주소**를 만들어 `VirtualFree` 실패.
> 정렬 올림 ①에서도 16 → 32가 되어 여분이 16B 생긴다.

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
> 💬 (해설 요청) **안 난다.** 크기 8은 16으로 올림되므로 인덱스 8~15는 정렬 여분이다 (Q1과 같은 원리). 그래서 `CrashStompOverflow`는 일부러 **16의 배수인 크기 16**을 요청하고 `bytes[16]`에 쓴다. 여분이 0이라 데이터 바로 뒤가 페이지 경계가 된다.

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

---

## Step 3-2. MemoryPool

### 🎯 목표
**같은 크기의 블록**을 모아 두고, 여러 스레드가 락 없이 꺼내 쓰고(Pop) 돌려놓는(Push) lock-free 풀을 만든다.

### 📖 개념 1 — 왜 풀을 쓰나
| | 매번 `malloc`/`free` | 메모리 풀 |
|---|---|---|
| 해제 | 힙에 반납 | **풀에 보관** |
| 다음 할당 | 힙에서 빈 공간 탐색 | 풀 맨 앞 블록을 바로 꺼냄 |
| 멀티스레드 | 힙 내부 락 경쟁 | SLIST 원자 연산 한 번 |

- 게임 서버는 패킷, Job, 세션처럼 같은 크기의 객체를 초당 수만 번 만들고 지운다.
- LIFO라서 방금 반납된 블록이 바로 다시 나간다 → CPU 캐시에 남아 있을 가능성이 높다.
- 풀의 블록 크기(`allocSize_`)는 **헤더를 포함한 전체 크기**다 (데이터 64B → 블록 16 + 64 = 80B).

### 📖 개념 2 — SLIST_ENTRY (블록 안의 "손")
```cpp
// winnt.h (x64)
typedef struct DECLSPEC_ALIGN(16) _SLIST_ENTRY {
    struct _SLIST_ENTRY *Next;      // 다음 노드 주소 (8B) + 정렬 빈칸 8B = 16B
} SLIST_ENTRY, *PSLIST_ENTRY;
```
- 데이터 칸이 없다. 데이터는 이 노드를 **품은 쪽**(MemoryHeader)이 갖는다 → 침투형(intrusive) 리스트.
- MSVC가 빈칸 8B에 `allocSize_`(4B)를 넣어서 `sizeof(MemoryHeader)`가 16이 된다.

| | 보통 리스트 (`std::list`) | 침투형 리스트 (SLIST) |
|---|---|---|
| 넣을 때 할당 | 노드마다 할당 | **없음** (블록 자신이 노드) |
| 메모리 풀에 쓰면 | 할당을 줄이려다 또 할당 (모순) | 빈 블록 자체를 엮음 ✅ |

- **상속하는 이유**: `SLIST_ENTRY` 부분이 오프셋 0에 놓여 `MemoryHeader*`와 `PSLIST_ENTRY`가 같은 주소가 된다 → 자식 → 부모는 암시적 변환, 부모 → 자식은 `static_cast`.

### 📖 개념 3 — SLIST_HEADER (줄 입구의 "명부")
| | `SLIST_ENTRY` | `SLIST_HEADER` |
|---|---|---|
| 어디 있나 | 블록마다 하나 (MemoryHeader 안) | 풀마다 하나 (`MemoryPool::header_`) |
| 역할 | "내 다음 블록은 저기" | "줄의 맨 앞은 저기" |

| 칸 (x64) | 크기 | 내용 |
|---|---|---|
| `NextEntry` | 60비트 | 맨 앞 블록 주소 (16바이트 정렬이라 끝 4비트는 항상 0 → 생략) |
| `Depth` | 16비트 | 들어 있는 블록 수 |
| `Sequence` | 48비트 | Push/Pop마다 +1 되는 순번 |
| `Reserved` | 4비트 | 예약 |

```
                 NextEntry   Depth   Sequence
처음 (Init)        nullptr     0        0
Push(A)            A           1        1
Push(B)            B           2        2
Pop() → B          A           1        3
```
- Push/Pop은 이 **16바이트 전체를 한 번에** 비교교환(`cmpxchg16b`)한다 → 세 칸이 따로 바뀌는 순간이 없다.
- `cmpxchg16b`는 16바이트 정렬된 주소만 받는다 → `SLIST_HEADER`와 노드 모두 16바이트 정렬 **필수**.

> ⚠️ 반드시 `InitializeSListHead`로 초기화하고, 칸을 직접 읽고 쓰지 않으며(`Interlocked...` 함수만), 복사하지 않는다.

### 📖 개념 4 — ABA 문제와 Sequence
| 시점 | 일어난 일 | NextEntry | Sequence |
|---|---|---|---|
| ① | 스레드 A가 읽음: "맨 앞 X, X의 다음 Y" (멈춤) | X | 10 |
| ② | 스레드 B가 X를 Pop | Y | 11 |
| ③ | 스레드 B가 Y를 Pop (Y 사용 중) | … | 12 |
| ④ | 스레드 B가 X를 다시 Push | X | 13 |
| ⑤ | 스레드 A가 깨어나 비교: "(X, 10)인가?" | X | **13** |

- **주소만 비교**: 맨 앞이 여전히 X → 성공 → 맨 앞 = Y인데 Y는 B가 쓰는 중 💥
- **주소 + 순번 비교**: 10 ≠ 13 → 실패 → A가 다시 읽는다 ✅
- 주소가 A → B → A로 돌아와 변화를 못 알아채는 문제를 **ABA**라고 한다.

| 함수 | 하는 일 | 반환 |
|---|---|---|
| `InitializeSListHead(&head)` | 빈 리스트로 초기화 | 없음 |
| `InterlockedPushEntrySList(&head, entry)` | 맨 앞에 넣기 | 넣기 전 맨 앞 노드 |
| `InterlockedPopEntrySList(&head)` | 맨 앞에서 빼기 | 뺀 노드, 비었으면 `nullptr` |
| `InterlockedFlushSList(&head)` | 전부 떼어 냄 | 떼어 낸 리스트의 첫 노드 |
| `QueryDepthSList(&head)` | 개수 조회 | Depth |

### 📖 개념 5 — 구조 그림: MemoryHeader와 MemoryPool
```
┌──────────────── MemoryPool (풀 하나 = 블록 크기 하나, 예: 80B) ────────────────┐
│                                                                              │
│  header_ : SLIST_HEADER (16B, 풀의 입구 명부)                                  │
│  ┌──────────────────────────────────────────┐                                │
│  │ NextEntry = B │ Depth = 3 │ Sequence = 37 │                                │
│  └───────┬──────────────────────────────────┘                                │
│          │ 맨 앞                                                              │
│          ▼                                                                    │
│   ┌─ 블록 B (80B) ─┐     ┌─ 블록 A (80B) ─┐     ┌─ 블록 D (80B) ─┐            │
│   │ MemoryHeader   │     │ MemoryHeader   │     │ MemoryHeader   │            │
│   │  Next ─────────┼───▶ │  Next ─────────┼───▶ │  Next = nullptr│            │
│   │  allocSize_= 0 │     │  allocSize_= 0 │     │  allocSize_= 0 │            │
│   ├────────────────┤     ├────────────────┤     ├────────────────┤            │
│   │ 데이터 64B 빈칸 │     │ 데이터 64B 빈칸 │     │ 데이터 64B 빈칸 │            │
│   └────────────────┘     └────────────────┘     └────────────────┘            │
│        ↑ 풀 안에서 쉬는 블록들 (reserveCount_ = 3)                              │
│                                                                              │
│  allocSize_ = 80   useCount_ = 1   reserveCount_ = 3                          │
└──────────────────────────────────────────────────────────────────────────────┘

   밖에서 사용 중인 블록 (useCount_ = 1) — 풀은 이 블록을 가리키지 않는다
   ┌─ 블록 C (80B) ─┐
   │ MemoryHeader   │
   │  Next (안 씀)   │
   │  allocSize_=80 │ ← 이름표: "나는 80B 풀 소속"
   ├────────────────┤
   │ Knight 객체 ... │ ← 사용자는 여기 주소만 안다
   └────────────────┘
```

| 구성 요소 | 개수 | 역할 |
|---|---|---|
| `MemoryPool` | 블록 크기마다 1개 | 같은 크기 블록들의 보관소 |
| `SLIST_HEADER` (`header_`) | 풀마다 1개 | 쉬는 블록 줄의 입구 |
| `MemoryHeader` | 블록마다 1개 | 블록 맨 앞 16B. 쉴 때는 `Next`로 줄 서기, 쓸 때는 `allocSize_`로 이름표 |

### 📖 개념 6 — 한 블록의 일생
```
            MemoryPool                         사용하는 쪽 (이후 Memory / xnew)
   ┌───────────────────────────┐
   │ ① Pop()                   │
   │   풀에 있으면 맨 앞을 뗌     │
   │   없으면 _aligned_malloc   │
   └─────────────┬─────────────┘
                 │ MemoryHeader* (이름표 없음)
                 ▼
                                      ② AttachHeader(block, 80)
                                         allocSize_ = 80 기록
                                         반환: block + 1 (데이터 주소)
                                                  │
                                                  ▼
                                      ③ 사용 (생성자 → 게임 로직 → 소멸자)
                                                  │
                                                  ▼
                                      ④ DetachHeader(데이터 주소)
                                         데이터 주소 - 1 → 블록 시작
                                         allocSize_ = 80 읽기 → "80B 풀로"
                 ┌────────────────────────────────┘
                 ▼ MemoryHeader*
   ┌───────────────────────────┐
   │ ⑤ Push(header)            │
   │   allocSize_ > 0 확인      │ ← 0이면 이중 해제 → 크래시
   │   allocSize_ = 0          │ ← "풀 안에 있음" 표시
   │   줄 맨 앞에 끼움           │
   └─────────────┬─────────────┘
                 │
                 └──▶ 다음 ① Pop에서 다시 나감 (재사용)
```

**MemoryHeader의 두 얼굴** — 같은 16B 칸이 상태에 따라 다르게 쓰인다.
```
           풀 안에서 쉬는 중                       밖에서 사용 중
   ┌──────────────────────────┐        ┌──────────────────────────┐
   │ Next = 다음 빈 블록 주소   │ ◀─쓰임  │ Next = (의미 없음)         │
   │ allocSize_ = 0           │         │ allocSize_ = 80          │ ◀─쓰임
   └──────────────────────────┘        └──────────────────────────┘
      MemoryPool이 관리                   사용하는 쪽이 관리
      (SLIST의 노드)                      (해제할 때 풀을 찾는 이름표)
```

### 📖 개념 7 — 다음 단계와의 연결 (미리 보기)
```
xnew<Knight>()                          xdelete(knight)
     │                                        │
     ▼                                        ▼
Memory::Allocate(크기)                   Memory::Release(ptr)
     │ 크기에 맞는 풀 선택                     │ DetachHeader → allocSize_로 풀 선택
     ▼                                        ▼
┌──────────┬──────────┬──────────┬─── … ───┐
│ 32B 풀   │ 64B 풀   │ 80B 풀   │ 4096B 풀 │   ← MemoryPool 여러 개
└──────────┴──────────┴──────────┴─── … ───┘
```

### 🧱 핵심 코드
```cpp
// MemoryPool.h
class alignas(SLIST_ALIGNMENT) MemoryPool
{
public:
	explicit MemoryPool(int32 allocSize);	// allocSize: 블록 전체 크기 (MemoryHeader 포함)
	~MemoryPool();

	void Push(MemoryHeader* ptr);			// AttachHeader로 헤더를 붙인 블록만 받는다
	MemoryHeader* Pop();					// 헤더가 붙지 않은 블록을 돌려준다

	int32 GetAllocSize() const { return allocSize_; }
	int32 GetUseCount() const { return useCount_.load(); }
	int32 GetReserveCount() const { return reserveCount_.load(); }

private:
	SLIST_HEADER header_;
	int32 allocSize_ = 0;
	Atomic<int32> useCount_ = 0;			// 밖에서 사용 중
	Atomic<int32> reserveCount_ = 0;		// 풀 안에서 쉬는 중
};
```
```cpp
// MemoryPool.cpp
MemoryPool::MemoryPool(int32 allocSize) : allocSize_(allocSize)
{
	ASSERT_CRASH(allocSize >= static_cast<int32>(sizeof(MemoryHeader)));
	::InitializeSListHead(&header_);
}

MemoryPool::~MemoryPool()
{
	// 풀 안의 블록만 해제 (사용 중인 블록은 풀이 모른다)
	while (MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&header_)))
		::_aligned_free(memory);
}

void MemoryPool::Push(MemoryHeader* ptr)
{
	ASSERT_CRASH(ptr->allocSize_ > 0);		// 0이면 이중 해제
	ptr->allocSize_ = 0;					// "풀 안에 있음" 표시

	::InterlockedPushEntrySList(&header_, static_cast<PSLIST_ENTRY>(ptr));

	useCount_.fetch_sub(1);
	reserveCount_.fetch_add(1);
}

MemoryHeader* MemoryPool::Pop()
{
	MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&header_));

	if (memory == nullptr)
	{
		memory = static_cast<MemoryHeader*>(::_aligned_malloc(allocSize_, SLIST_ALIGNMENT));
		ASSERT_CRASH(memory != nullptr);
	}
	else
	{
		ASSERT_CRASH(memory->allocSize_ == 0);
		reserveCount_.fetch_sub(1);
	}

	useCount_.fetch_add(1);
	return memory;
}
```

> 💡 `_aligned_malloc`으로 받은 메모리는 반드시 `_aligned_free`로 해제한다 (할당/해제 짝 맞추기).

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `Push`에서 `ASSERT_CRASH(allocSize_ > 0)` | 같은 블록을 두 번 Push(이중 해제)하면 SLIST에 같은 노드가 두 번 들어가 리스트가 고리가 되고, 나중에 두 곳에서 같은 블록을 쓴다 → 그 자리에서 잡는다 |
| `_aligned_malloc` 결과 nullptr 검사 | nullptr 위에 헤더를 쓰는 UB 방지 |
| 생성자에서 블록 크기 하한 검사 | 헤더보다 작은 블록이면 헤더가 블록 밖을 덮는다 |
| `GetUseCount()` 등 조회 함수 | 테스트, 이후 통계 출력 |
| `DECLSPEC_ALIGN` → `alignas`, 멤버 이름 뒤 `_` | Step 3-1과 같은 이유 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `.cpp`를 붙여넣어 스페이스 들여쓰기 (세 번째) | 파일 안에서 탭/스페이스 불일치 | VS `도구 → 옵션 → 텍스트 편집기 → C/C++ → 탭`에서 **탭 유지** 설정 |
| VS에서 고치고 저장 안 함 | 디스크에는 반영 안 됨 (크래시 테스트 주석도 풀린 채) | 리뷰 요청 전에 `Ctrl+Shift+S` 모두 저장. 탭 제목의 `*` 확인 |
| (Claude) 테스트 주석과 출력에 확인 질문 2번의 답을 넣음 | 질문의 힌트가 됨 | 판정 근거 주석은 필요하지만, 확인 질문과 겹치면 미리 알린다 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `MemoryPoolReuse` | Pop(새 할당, 16바이트 정렬, use 1 / reserve 0) → Push(use 0 / reserve 1) → 다시 Pop이 **같은 블록**인지(LIFO 재사용) → Push | `OK` (Debug/Release) |
| `MemoryPoolConcurrent` | 스레드 4개 × 10,000번 Pop → Attach → **스레드마다 다른 값**으로 64B 쓰고 다시 읽기 → Push. Join 후 use 0, reserve 1~4, 값이 섞인 횟수 0 | `OK (blocks created 4)` (Debug/Release, 각 3회) |
| `CrashMemoryPoolDoublePush` | 같은 블록을 두 번 Push | `ASSERT_CRASH(ptr->allocSize_ > 0)` 크래시 (Debug/Release) |

> 📝 2026-10-28: 정상 테스트 통과, 크래시 테스트 직접 확인. 테스트 코드는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `Push`는 블록을 해제하지 않고 쌓기만 한다. 풀의 메모리는 언제 줄어들까? 단점은?**
> ✅ 내 답: 줄어들지 않는다. 메모리 낭비가 생길 수 있다.
> 💬 보충: `_aligned_free`는 소멸자에만 있으므로 풀이 사라질 때(서버 종료)만 반납된다. **최고점(동시에 쓴 최대 개수) 기준으로 유지**되어, 이벤트 피크 후에도 메모리가 내려가지 않는다. 64B 풀의 블록은 128B 요청에 쓸 수 없고, 누수와 구분이 어려워 `useCount`/`reserveCount`가 근거가 된다. 그래도 피크는 매일 다시 오고 예측 가능하므로 게임 서버는 이 방식을 많이 쓴다. 줄이려면 reserve 상한이나 trim을 둔다.

**Q2. `MemoryPoolConcurrent`가 끝난 뒤 reserve가 최대 4인 이유는?**
> 💬 (모르겠다 → 해설) 새 블록은 **풀이 비었을 때만** 만들어진다. 각 스레드는 Pop → 사용 → Push라 동시에 1개만 들고 있으므로, 밖에 나가 있는 블록은 최대 4개다. 5번째를 만들려면 4개가 밖에 있는 상태에서 또 Pop해야 하는데 그럴 스레드가 없다. 반복이 많아 4개가 동시에 나가는 순간이 거의 확실히 생겨 매번 4가 나왔지만, 스레드가 겹치지 않으면 1~3도 가능해서 판정은 1~4로 한다. → **풀의 크기 = 동시 사용 최대 개수**.

---

## Step 4-1. Memory

### 🎯 목표
크기만 받아서 알맞은 풀을 바로 찾아 할당·해제하는 `Memory`를 만든다. 전역 `GMemory`로 어디서나 쓸 수 있게 한다.

> 🗺️ 인터랙티브 다이어그램: Docs/Diagrams/Phase1-3_MemoryPool.html — Memory와 MemoryPool의 역할 분담, pools_ / poolTable_ 관계, Allocate → Release → 재사용을 한 단계씩 따라가기, 여러 크기 섞어 보기, 생성·소멸 순서

### 📖 개념 1 — 크기가 곧 번호인 안내표
`poolTable_`은 **크기가 곧 번호인 사물함 안내표**다. 풀을 반복문으로 찾지 않고 `poolTable_[blockSize]` 한 번으로 찾는다.

```
poolTable_ [1 ... 32]      → 32 풀
           [33 ... 64]     → 64 풀
           ...
           [3841 ... 4096] → 4096 풀
블록 크기 > 4096           → 풀을 거치지 않음 (_aligned_malloc / _aligned_free)
```

| 블록 크기 | 풀 간격 | 풀 개수 |
|---|---|---|
| 32 ~ 1024 | 32 | 32개 |
| 1152 ~ 2048 | 128 | 8개 |
| 2304 ~ 4096 | 256 | 8개 |

> 💡 `AddPool`에 넘기는 값은 범위의 시작이 아니라 **풀 블록의 크기, 즉 담당 범위의 끝**이다. 범위의 시작은 `직전 풀 크기 + 1`로 `AddPool`이 계산한다.

### 📖 개념 2 — 분기 기준은 size가 아니라 blockSize
호출자는 `size`만 말하지만 실제로는 헤더 자리까지 필요하다.

```
[MemoryHeader][      Data (size 바이트)      ]
^ 블록 시작    ^ 호출자가 받는 주소
|<------------- blockSize ------------------>|
```

`sizeof(MemoryHeader)`는 16이다 (테스트 출력으로 확인). 그래서 호출자가 4096을 요청해도 풀 밖으로 가고, 풀에 들어가는 가장 큰 요청은 4080이다.

### 🧱 핵심 코드
```cpp
void* Memory::Allocate(int32 size)
{
	ASSERT_CRASH(size > 0);
	const int32 headerSize = static_cast<int32>(sizeof(MemoryHeader));
	ASSERT_CRASH(size <= INT32_MAX - headerSize);

	MemoryHeader* pHeader = nullptr;
	int32 blockSize = size + headerSize;

	// 풀 크기를 넘는 블록은 직접 할당한다.
	if (blockSize > MAX_ALLOC_SIZE)
	{
		// 데이터 주소의 16바이트 정렬을 지키려고 같은 정렬로 받는다
		pHeader = static_cast<MemoryHeader*>(::_aligned_malloc(blockSize, SLIST_ALIGNMENT));
		ASSERT_CRASH(pHeader != nullptr);
	}
	else
	{
		pHeader = poolTable_[blockSize]->Pop();
	}

	return MemoryHeader::AttachHeader(pHeader, blockSize);
}

void Memory::Release(void* pPtr)
{
	ASSERT_CRASH(pPtr != nullptr);

	MemoryHeader* pHeader = MemoryHeader::DetachHeader(pPtr);
	int32 blockSize = pHeader->allocSize_;
	ASSERT_CRASH(blockSize > 0);

	// Allocate와 같은 기준으로 경로를 고른다. 할당한 곳으로 돌려줘야 한다
	if (blockSize > MAX_ALLOC_SIZE)
	{
		// _aligned_malloc이 준 주소는 데이터가 아니라 헤더 주소다
		::_aligned_free(pHeader);
	}
	else
	{
		poolTable_[blockSize]->Push(pHeader);
	}
}
```

> ⚠️ `GMemory`는 `CoreGlobal`에서 **가장 먼저 만들고 가장 나중에 지운다.** 워커 스레드는 `ThreadManager` 소멸자의 `Join`으로 끝나므로, 그 뒤에 지워야 해제된 `Memory`에 접근하지 않는다.

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `allocSize_`에 헤더 포함 블록 크기를 저장 | 풀과 분기가 쓰는 값과 같아서, `Release`에서 계산을 다시 하지 않는다 |
| `Release`에서 `blockSize > 0` 검사 | 0이면 `poolTable_[0]`이 `nullptr`이므로 표를 읽기 전에 멈춘다 |
| `size` 범위 검사 (덧셈 전에) | `int32` 덧셈 넘침 방지 |
| 생성자 끝에서 마지막 풀 크기 검사 | 풀 개수가 맞아도 표에 빈칸이 생기는 실수를 잡는다 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| 풀 시작값을 1, 1025, 2049로 설정 | 블록 크기 1은 생성자 ASSERT에 걸리고, 마지막 풀이 3841이 되어 `poolTable_[3842~4096]`이 `nullptr` | 넘기는 값은 범위의 시작이 아니라 풀 크기(범위의 끝). 개수뿐 아니라 마지막 풀까지 검사한다 |
| `void* _aligned_malloc(...)` | 호출이 아니라 변수 선언으로 해석되어 빌드 실패 | 앞에 타입을 쓰면 선언이다 |
| `AttachHeader` 없이 `pHeader` 반환 | 호출자가 헤더를 덮어쓰고 `allocSize_`도 기록되지 않음 | 호출자에게는 헤더 뒤 주소를 준다 |
| `Release`에서 `sizeof(MemoryHeader)`를 또 더함 | 다른 풀로 반납되거나 표 범위를 넘음 | `allocSize_`에는 이미 헤더가 포함되어 있다 |
| `Release`에 큰 블록 경로가 없음 | 직접 할당한 블록이 표 범위 밖을 읽음 | 할당 경로와 해제 경로는 항상 짝을 맞춘다 |
| `GMemory`를 가장 먼저 삭제 | 워커가 해제된 `Memory`에 접근할 수 있음 | 다른 것이 의존하는 객체는 마지막에 지운다 |
| (Claude) 풀 범위를 "49~64번"으로 잘못 설명 | 설명이 코드와 달라짐 | 정확한 범위는 33~64번 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `MemoryAllocReuse` | 100바이트 할당 → 16바이트 정렬, 헤더의 블록 크기 → 해제 후 재할당이 같은 주소 | `OK` (Debug/Release) |
| `MemoryBoundarySizes` | 1, 1024, 4080, 4081, 4096, 5000바이트의 정렬, 전체 읽기/쓰기, 헤더 보존 | `OK` (Debug/Release) |
| `MemoryConcurrent` | 스레드 4개가 크기 5종을 함께 들고 2,000번 할당/해제, 값이 섞이지 않음 | `OK` (Debug/Release) |
| `CrashMemoryDoubleRelease` | 100바이트를 두 번 `Release` | `ASSERT_CRASH(blockSize > 0)` 크래시 (사용자 확인) |

> 📝 2026-10-31: 정상 테스트 통과, 크래시 테스트 직접 확인. 테스트 코드는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `Allocate(40)`과 `Allocate(4081)`은 각각 어떤 경로로 가나?**
> ✅ 내 답: `Allocate(40)` → 36, `Allocate(4081)` → 메모리풀 밖.
> 💬 보충: 4081은 맞다. 4081 + 16 = 4097 > 4096이므로 `_aligned_malloc`으로 간다. 40은 36이 아니다. 헤더는 빼지 않고 더하므로 블록 크기는 40 + 16 = 56이고, 33~64번 칸이 가리키는 **64 풀**로 간다. 남는 8바이트는 풀 크기에 맞추면서 생기는 낭비다.

**Q2. 이중 `Release`를 `blockSize > 0` 검사가 항상 잡나?**
> ✅ 내 답: 풀 경로는 잡지만 큰 블록 경로는 못 잡는다.
> 💬 보충: 큰 블록은 첫 `Release`에서 메모리가 해제되므로, 두 번째 `Release`가 헤더를 읽는 것 자체가 Use-After-Free다. 풀 경로도 **블록이 풀에 있는 동안만** 잡는다. 두 `Release` 사이에 다른 `Allocate`가 그 블록을 꺼내 가면 `allocSize_`가 다시 양수가 되어 검사를 통과하고, 남의 블록을 반납하게 된다. 그래서 "항상"은 아니다.

**Q3. `allocSize_`에 풀 크기(64)가 아니라 블록 크기(50)를 적어도 `Release`가 맞는 풀을 찾는 이유는?**
> ✅ 내 답: `AddPool`할 때 풀 크기 단위로 바뀌어서 저장하기 때문에.
> 💬 보충: 방향은 맞다. 다만 값이 바뀌어 저장되는 것은 아니고 `allocSize_`는 50 그대로다. `AddPool`이 33~64번 칸을 **모두 같은 64 풀로** 채워 두었기 때문에, `poolTable_[50]`과 `poolTable_[64]`가 같은 풀이다.

**Q4. 큰 블록 경로에서 `_aligned_malloc` 대신 `malloc`을 쓰면 `Release` 쪽에서 어떤 문제가 생기나?**
> 💬 (모르겠다 → 해설) `Release`의 `_aligned_free`와 짝이 맞지 않는다. `_aligned_malloc`은 정렬용 여유 공간에 원래 할당 주소 정보를 함께 두고, `_aligned_free`는 그 정보를 읽어 해제한다. `malloc`으로 받은 블록에는 그 정보가 없으므로 잘못된 주소를 해제해 크래시나 힙 손상이 생긴다. → **할당 함수와 해제 함수는 항상 짝을 맞춘다.**

---

## Step 4-2. PoolAllocator

### 🎯 목표
`GMemory`를 다른 할당기와 같은 모양으로 감싸고, Release 빌드의 `xnew`/`xdelete`가 풀을 쓰게 바꾼다.

### 📖 개념 — 상속 없이 모양만 맞춘 어댑터
`xnew`는 `DefaultAllocator::Alloc`, `DefaultAllocator::Release`만 호출한다. `PoolAllocator`는 `Memory`를 이 모양에 맞춰 주는 어댑터다.

```
xnew → DefaultAllocator ─ Debug   → StompAllocator
                        └ Release → PoolAllocator → GMemory → 풀 / 큰 블록
```

세 할당기는 상속 관계가 아니다. 함수가 `static`이고 할당기를 컴파일할 때 별칭으로 고르므로, `virtual` 호출 비용 없이 함수 이름과 모양만 맞으면 된다. 모양이 어긋나면 컴파일 오류로 잡힌다.

> 💡 교체는 별칭 한 줄로 끝난다. Step 1에서 할당기 선택을 `DefaultAllocator` 한 곳에 모아 둔 덕분이다.

### 🧱 핵심 코드
```cpp
void* PoolAllocator::Alloc(int32 size)
{
	return GMemory->Allocate(size);
}

void PoolAllocator::Release(void* pPtr)
{
	if (pPtr == nullptr)
		return;

	GMemory->Release(pPtr);
}
```

```cpp
#ifdef _DEBUG
using DefaultAllocator = StompAllocator;
#else
using DefaultAllocator = PoolAllocator;
#endif
```

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `xnew`/`xdelete`가 `DefaultAllocator`를 호출 | 빌드 구성별 할당기를 한 곳에서 고른다. Debug는 Stomp로 메모리 오류를 잡고, Release는 풀로 빠르게 동작한다 |
| `PoolAllocator::Release`의 `nullptr` 검사 | 별칭으로 바꿔 끼우는 다른 할당기와 규칙을 맞춘다 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| 내용 없는 `private:`, 역할 주석 없음, 옛 문구 남음 | 코드와 주석이 달라짐 | 동작을 바꾸면 그 동작을 설명하는 주석과 테스트 문구도 함께 찾아 고친다 |
| (Claude) `MemoryPool.h`의 정렬 이유를 16바이트 비교교환 요구사항으로 설명 | 블록 정렬의 실제 이유와 다름 | 블록은 `SLIST_HEADER`가 주소 끝 4비트를 저장하지 않기 때문에 16의 배수여야 한다 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `PoolAllocatorAllocFree` | 16바이트 정렬, 읽기/쓰기, 재할당 시 같은 주소, `nullptr` 해제 | `OK` (Debug/Release) |
| `XnewPoolReuse` | `xnew` → `xdelete` → `xnew`가 같은 주소, 생성자 값 전달 | Release `OK`, Debug `SKIP` |

> 📝 2026-10-31: 코드 정리와 테스트는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. 전역 객체의 생성자에서 `CoreGlobal` 생성보다 먼저 `xnew`를 호출하면 Debug와 Release의 결과가 어떻게 다른가?**
> ✅ 내 답: 크래시가 발생한다.
> 💬 보충: **Release만 크래시가 난다.** Release의 `PoolAllocator`는 아직 `nullptr`인 `GMemory`를 쓰기 때문이다. Debug의 `StompAllocator`는 `GMemory` 없이 `VirtualAlloc`을 직접 쓰므로 통과한다. 그래서 Debug에서 문제가 안 보이다가 Release에서만 터지는 버그가 된다. `CoreGlobal`이 사라진 뒤의 `xdelete`도 같다. → **`xnew`/`xdelete`는 `CoreGlobal`이 살아 있는 동안에만 쓴다.**

**Q2. Release 빌드에서 `CrashStompUseAfterFree`를 실행하면 크래시가 나나? `BaseAllocator`일 때와 비교해 해제 후 접근의 위험이 어떻게 달라지나?**
> ✅ 내 답: 크래시가 나지 않는다.
> 💬 보충: 맞다. 후반부는 미답변이어서 해설을 붙인다. 풀은 블록을 OS에 돌려주지 않으므로 해제 후에도 주소가 유효해 Access Violation이 나지 않는다. 게다가 LIFO라서 다음 같은 크기 할당이 **방금 해제한 블록을 바로 받는다.** 오래된 포인터로 쓰면 새 객체의 값이 조용히 바뀌고, 증상은 한참 뒤 엉뚱한 곳에서 나타난다. `malloc`/`free`도 잡아 주지는 못하지만, 풀에서는 재사용이 더 확실하게 일어난다. 그래서 Debug의 `StompAllocator`로 이런 버그를 잡아 두는 것이 중요하다.

---

## Step 5. MakeShared

### 🎯 목표
`xnew`로 만든 객체를 `SharedPtr`로 감싸고, 마지막 참조가 사라지면 `delete`가 아니라 `xdelete`로 해제되게 하는 `MakeShared<Type>(args...)`를 만든다.

### 📖 개념 1 — shared_ptr의 두 덩어리
```
SharedPtr<Knight> ──▶ [ Knight 객체 ]   ← xnew로 우리 할당기(Stomp/Pool)에서 받는다
                 └──▶ [ 컨트롤 블록 ]    ← 참조 카운트 + 삭제자(지울 때 부를 함수)
```
- `std::make_shared`나 `std::shared_ptr<T>(new T)`는 우리 할당기를 거치지 않는다. 세션, Room, Job처럼 가장 많이 쓰일 객체가 메모리 시스템 밖에 남게 된다.
- `shared_ptr` 생성자에 **(포인터, 삭제자)**를 함께 넘기면, 참조 카운트가 0이 될 때 `delete` 대신 그 삭제자를 부른다.
- 삭제자는 **만들 때 컨트롤 블록에 저장**된다. 나중에 부모 타입 `SharedPtr`로 옮겨 담아도 처음 정한 삭제자가 그대로 불린다 (타입 소거, type erasure).

> 💡 **비유**: 공유 자전거. 본체(객체)는 우리 대여소(`xnew`)에서 빌렸으니, 마지막 사람이 반납할 때도 같은 대여소(`xdelete`)로 돌려줘야 한다. 컨트롤 블록은 자전거에 붙은 "반납처 스티커 + 사용자 수 카운터"다.

### 📖 개념 2 — 완벽 전달 (`std::forward`)
- `Args&&... args`(전달 참조)로 받으면 컴파일러가 "원래 lvalue였는지 rvalue였는지"를 `Args`에 기록한다.
- 매개변수 `args`는 이름이 있어서 함수 안에서는 lvalue로 취급된다. `std::forward<Args>(args)...`가 `Args`의 기록을 보고 **rvalue였던 것만 다시 rvalue로** 되돌린다.
- `std::move`는 무조건 rvalue로 바꾸고, `std::forward`는 원래 모양대로 되돌린다. 둘 다 타입만 바꾸는 캐스팅이라 실행 비용이 없다.
- 규칙: `Args&&`로 받았으면 `std::forward<Args>(args)...`로 넘긴다. 꺾쇠 안의 `Args`를 생략하면 컴파일 오류가 나고, 같은 인자를 두 번 forward하지 않는다.

> 💡 **택배 비유**: "깨지기 쉬움(임시값)" 스티커가 붙은 상자를 중간 기사(`MakeShared`, `xnew`)가 받으면 스티커가 떨어진다. `std::forward`는 원래 스티커를 다시 붙여서 다음 사람에게 넘기는 일이다.

### 🧱 핵심 코드
```cpp
// Memory.h
template<typename Type, typename... Args>
SharedPtr<Type> MakeShared(Args&&... args)
{
	Type* pObj = xnew<Type>(std::forward<Args>(args)...);
	ASSERT_CRASH(pObj != nullptr);

	return SharedPtr<Type>(pObj, xdelete<Type>);
}
```
- `xdelete`는 템플릿이라 삭제자로 넘길 때 `<Type>`을 붙여야 함수 하나로 정해진다.
- `SharedPtr<T>`는 `Types.h`의 `std::shared_ptr<T>` 별칭이다.

> ⚠️ **한계**: 이번 버전은 **객체만** 우리 할당기에서 받는다. 컨트롤 블록은 표준 라이브러리가 기본 `new`로 따로 할당한다. `StlAllocator`를 만든 뒤 `std::allocate_shared`로 객체와 컨트롤 블록을 한 번에 풀에서 받도록 바꿀 수 있다.

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `using namespace std` 없이 `SharedPtr<T>` 별칭 사용 | 헤더의 `using namespace`는 include하는 모든 파일(클라이언트 엔진 포함)로 퍼지고 되돌릴 수 없다. 별칭은 이름 하나만 만들어 안전하다 |
| `<memory>`를 `Memory.h`에서 직접 include | 자립 헤더 규칙 (Types.h가 이미 include해도 기대지 않는다) |
| 컨트롤 블록이 기본 할당이라는 한계를 헤더 주석에 명시 | 메모리 통계나 Stomp 검사가 컨트롤 블록을 다루지 않는다는 사실을 사용하는 쪽이 알 수 있게 |

> 📝 같은 시기에 `Types.h`의 정수 별칭 이름을 PascalCase로 바꿨다 (`int32` → `Int32`, `uint64` → `UInt64`, `uintptr` → `UIntPtr`). 이미 있던 `Atomic`, `Mutex`와 이름 규칙을 통일하기 위해서다. 이 문서의 앞 Step 코드 예시는 그 시점의 이름(`int32`)으로 남아 있다.

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| 매개변수에 `Type type`을 추가 | 쓰지 않는 값을 하나 더 받아야 하는 시그니처 | 타입은 호출할 때 꺾쇠(`MakeShared<Knight>(...)`)로 정한다. 값으로 받을 필요가 없다 |
| 반환 타입을 `shared_ptr<Type>`으로 씀 | `std::`가 없어 이름을 못 찾아 컴파일 오류 | 헤더에서는 `std::`를 붙이거나 `Types.h` 별칭(`SharedPtr`)을 쓴다 |
| 삭제자로 `xdelete`만 넘김 | 템플릿 인자를 추론할 수 없어 컴파일 오류 | 템플릿 함수를 값으로 넘길 때는 `xdelete<Type>`처럼 타입을 지정한다 |
| `ASSERT_CRASH(...)`, `return ...` 끝 세미콜론 누락 | 컴파일 오류 | `do { } while (0)` 매크로도 문장이므로 `;`가 필요하다 |
| `Type * pObj` 띄어쓰기 | 다른 코드(`Type* pMemory`)와 불일치 | `*`는 타입 쪽에 붙인다 |

> 📝 `ASSERT_CRASH(pObj != nullptr)`는 남겨 두었다. `xnew`는 실패하면 할당기 안에서 이미 크래시하므로 이 검사는 실패하지 않는다. 동작에는 영향이 없다.

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `MakeSharedRefCount` | 생성자 1회, 인자(100, 10) 전달, `use_count` 1 → 복사 후 2 → 복사본 `reset()` 후 1이고 소멸자 0회 → 마지막 `reset()` 후 소멸자 정확히 1회 | `OK` (Debug/Release) |
| `MakeSharedPoolReuse` | `MakeShared` 객체를 `reset()`한 직후 `xnew<MemoryTestKnight>`가 같은 주소를 받는지 (객체 메모리가 풀로 돌아갔다는 증거) | Release `OK`, Debug `SKIP` |

> 📝 2026-11-11: 테스트와 `MakeShared` 헤더 주석은 사용자 요청으로 Claude가 작성했다. 정수 타입 이름 변경도 사용자 요청으로 Claude가 일괄 적용했다.

### 🧩 복습 질문
**Q1. `MakeShared`에서 삭제자를 빼고 `SharedPtr<Type>(xnew<Type>(...))`만 쓰면, 마지막 참조가 사라질 때 무엇이 호출되나? Debug와 Release에서 각각 어떤 일이 생기나?**
> ✅ 내 답: 삭제자를 빼면 `xdelete`가 아닌 일반 `delete`가 호출되어서 할당과 해제의 쌍이 맞지 않는다.
> 💬 보충: `delete`는 ① 소멸자 호출 ② `operator delete`(CRT 힙의 `free`) 두 단계다. ①은 정상이라 소멸자는 불린다. 문제는 ②에서 **힙이 준 적 없는 주소를 힙에 돌려준다**는 것이다.
> - Debug(Stomp): `VirtualAlloc` 페이지 끝 주소 → Debug CRT가 힙 헤더 확인에 실패 → `_CrtIsValidHeapPointer` 단언 창 또는 크래시 (Step 2 Q9와 같은 상황)
> - Release(Pool): 풀 블록 시작 + 16 주소 → 힙이 그 앞을 관리 정보로 읽어 힙 손상. 바로 크래시(`0xC0000374`)하거나 한참 뒤 엉뚱한 곳에서 터진다
> - 블록이 풀로 돌아가지 않으니 `useCount_`도 줄지 않고 블록을 영원히 잃는다. → `MakeShared`가 삭제자를 직접 박아 넣어서 사용하는 쪽의 실수를 막는다.

**Q2. 가상 소멸자가 없는 `Base`와 자식 `Knight`가 있다. `SharedPtr<Base> p = MakeShared<Knight>();`로 담은 뒤 마지막 참조가 사라지면 `Knight`의 소멸자가 불릴까?**
> ✅ 내 답: `Knight`의 소멸자가 불린다.
> 💬 보충: 삭제자 `xdelete<Knight>`가 `MakeShared<Knight>`를 호출하는 순간 `Knight*` 주소와 함께 컨트롤 블록에 저장된다. `SharedPtr<Base>`로 옮겨도 포인터 타입만 바뀌고 컨트롤 블록은 공유되므로, 마지막에 `xdelete<Knight>` → `~Knight()` → `~Base()` 순서로 불린다.
> - 비교: raw 포인터 `Base* p = xnew<Knight>(); xdelete(p);`는 `Type`이 `Base`로 추론되어 `~Base()`만 불린다 (`~Knight()` 누락, UB).
> - ⚠️ 이 덕을 보는 건 `shared_ptr`뿐이다. `unique_ptr<Base>`나 raw 포인터는 구해 주지 않으므로, 상속해서 쓸 클래스에는 가상 소멸자를 붙이는 규칙을 그대로 지킨다.

---

## Step 6-1. StlAllocator

### 🎯 목표
`std::vector`, `std::map` 같은 STL 컨테이너가 내부 메모리(버퍼, 노드)를 **우리 할당기(`DefaultAllocator`)**에서 받게 하는 `StlAllocator<T>`를 만든다.

### 📖 개념 1 — 왜 필요한가
- `xnew`/`MakeShared`로 객체는 우리 할당기에 올렸지만, 그 객체 **안의** `std::vector`, `std::map`, `std::string`은 여전히 기본 `new`를 쓴다.
- 서버에서 메모리를 가장 많이, 가장 자주 쓰는 것이 컨테이너다 (패킷 버퍼, 세션 목록, Job 큐).
- 컨테이너는 마지막 템플릿 인자로 할당기를 받는다: `std::vector<Int32, StlAllocator<Int32>>`

### 📖 개념 2 — STL 할당기 규약 (C++17 최소)
| 항목 | 역할 |
|---|---|
| `value_type` | 이 할당기가 다루는 원소 타입. `allocator_traits`가 이것을 기준으로 `pointer`, `size_type` 등을 자동 계산하고, 컨테이너는 자기 원소 타입과 같은지 검사한다 |
| 기본 생성자 | 컨테이너가 할당기를 만들 수 있게 |
| 변환 생성자 `StlAllocator(const StlAllocator<Other>&)` | 다른 타입용 할당기를 만들 수 있게 (rebind) |
| `allocate(n)` | **T를 n개** 담을 메모리 (바이트 수가 아니라 개수). 생성자는 부르지 않는다 |
| `deallocate(p, n)` | `allocate`로 받은 메모리 반납 |
| `==`, `!=` | 한쪽이 받은 메모리를 다른 쪽이 해제해도 되는지. 상태가 없으니 항상 같다. C++17은 둘 다 필요 (C++20부터 `!=` 자동 생성) |

> 💡 **비유**: `value_type`은 택배 상자의 "내용물" 라벨이다. 물류 센터(`allocator_traits`)는 라벨을 보고 포장 방법을 정하고, 수령인(컨테이너)은 자기가 주문한 물건과 맞는지 확인한다.

### 📖 개념 3 — rebind: map은 pair가 아니라 노드를 할당한다
```
std::map<K, V, std::less<K>, StlAllocator<std::pair<const K, V>>>
  → 실제 할당 대상: _Tree_node { 왼쪽/부모/오른쪽 포인터, 색, 끝 표시, pair<const K, V> }
  → allocator_traits::rebind_alloc<_Tree_node> = StlAllocator<_Tree_node>  (템플릿 첫 인자만 바꿈)
  → 이 할당기 객체를 원래 StlAllocator<pair>에서 만들 때 변환 생성자가 쓰인다
```
- map은 비어 있어도 생성자에서 머리 노드(센티널)를 하나 할당한다.
- MSVC Debug에서는 `vector`도 반복자 검사용 객체(`_Container_proxy`)를 rebind로 따로 할당한다 → 변환 생성자가 없으면 Debug의 vector도 컴파일되지 않는다.

> 💡 **비유**: 같은 회사(우리 할당기) 사원증인데 부서(타입)만 다른 것. 부서를 옮겨도(rebind) 회사 금고(풀)는 같다.

### 📖 개념 4 — 왜 `DefaultAllocator`인가 (설계 선택)
- Debug에서 Stomp를 거치므로 컨테이너 버퍼를 넘어서 쓰는 버그도 그 줄에서 크래시로 잡힌다.
- "할당기 선택은 `DefaultAllocator` 한 곳에서"라는 지금까지의 규칙과 맞는다.
- 대가: Debug에서 컨테이너가 커질 때마다 시스템 콜 → 느리다. 우리 규모에서는 괜찮다.

### 🧱 핵심 코드
```cpp
// Memory.h
template<typename Type>
class StlAllocator
{
	// 할당기들은 16바이트 정렬까지만 보장한다 (xnew와 같은 검사)
	STATIC_ASSERT(alignof(Type) <= alignof(std::max_align_t), "over-aligned type is not supported");

public:
	using value_type = Type;

	StlAllocator() = default;

	// 다른 타입용 StlAllocator에서 만든다. 옮길 상태가 없으므로 아무 일도 하지 않는다
	template<typename Other>
	StlAllocator(const StlAllocator<Other>& /* other */) {}

	Type* allocate(USize count)
	{
		// 아래 곱셈 결과가 Int32를 넘지 않도록 곱하기 전에 나눗셈으로 확인한다
		ASSERT_CRASH(count <= INT32_MAX / sizeof(Type));

		const Int32 size = static_cast<Int32>(count * sizeof(Type));
		Type* pMemory = static_cast<Type*>(DefaultAllocator::Alloc(size));

		ASSERT_CRASH(pMemory != nullptr);
		return pMemory;
	}

	// 크기는 할당기가 이미 안다 (MemoryHeader / Stomp 페이지) → count는 쓰지 않는다
	void deallocate(Type* pPtr, USize /* count */)
	{
		DefaultAllocator::Release(pPtr);
	}
};

// 상태가 없는 할당기라 타입이 달라도 항상 같다
template<typename T, typename U>
bool operator==(const StlAllocator<T>&, const StlAllocator<U>&) { return true; }

template<typename T, typename U>
bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&) { return false; }
```
```cpp
// Types.h
#include <cstddef>
using USize = std::size_t;   // STL 할당기 규약의 크기 인자 등
```

| 포인트 | 설명 |
|---|---|
| 넘침 검사를 곱셈 **전에** 나눗셈으로 | `count * sizeof(Type)`이 넘친 뒤에는 검사할 수 없다. `count <= INT32_MAX / sizeof(Type)`는 검사 자체가 넘치지 않는다 |
| 변환 생성자에 `explicit` 없음 | 컨테이너 내부에서 암시적으로 변환되어야 한다 |
| 매개변수 이름을 주석 처리 (`/* count */`) | 규약 때문에 받아야 하지만 쓰지 않는 값. 이름을 지우면 C4100(참조되지 않은 매개변수) 경고가 안 나고, 주석으로 의미는 남는다. `[[maybe_unused]]`나 `(void)count;`도 같은 효과 |
| `==`/`!=`를 클래스 밖, 템플릿 인자 2개로 | rebind된 할당기(`StlAllocator<Node>`)와 원래 할당기(`StlAllocator<Pair>`)처럼 **타입이 다른 할당기끼리** 비교할 수 있어야 한다 |

> 📝 `==`/`!=`는 지금 테스트에서는 없어도 빌드된다. 멤버가 없는 할당기는 `allocator_traits::is_always_equal`이 `true`라 MSVC가 비교를 건너뛰는 경우가 많기 때문이다. 하지만 move 대입, `swap`, `list::splice`처럼 메모리 소유권을 옮기는 경로에서 쓰일 수 있고, 템플릿이라 쓰는 순간에야 오류가 드러나므로 규약대로 채워 둔다.

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| `PoolAllocator` 대신 `DefaultAllocator` 사용 | Debug에서는 Stomp로 컨테이너 버퍼 오버플로까지 잡고, 할당기 선택을 한 곳에 모은다 |
| `count × sizeof` 넘침 검사 | `Int32` 크기로 넘기기 전에 곱셈 넘침을 막는다 |
| 정렬 `STATIC_ASSERT` | `xnew`와 같은 정렬 보장 범위 검사 |
| `size_t`를 `USize` 별칭으로 | 프로젝트 데이터 타입 규칙 (Types.h 별칭 사용) |
| 위치를 `Memory.h`로 | `DefaultAllocator` 별칭이 `Memory.h`에 있다 |

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `static_cast<Int32>count * sizeof(Type)` | 괄호가 없어 컴파일 오류 | `static_cast<T>(식)` — 변환할 식 전체를 괄호로 감싼다 |
| `Type* p = DefaultAllocator::Alloc(size);` | `void*` → `Type*` 암시적 변환 불가 | C++은 `void*`를 자동 변환하지 않는다. `static_cast<Type*>` |
| `operator==`를 클래스 안에 매개변수 2개 + 선언 안 된 `T`, `U`로 작성 | 컴파일 오류 | 멤버 연산자는 왼쪽이 `this`라 매개변수 1개. 타입이 다른 할당기 비교는 클래스 밖 함수 템플릿으로 |
| `using value_type`을 빠뜨림 | 클래스만 있을 때는 빌드되지만 컨테이너에 넣는 순간 오류 | 템플릿은 쓰는 순간에야 검사된다 → 실제로 쓰는 테스트가 있어야 검증된다 |
| `Types.cpp`에 같은 줄 중복 | 무해하지만 지저분함 | 붙여넣은 뒤 확인 |
| (Claude) 타입 이름 일괄 변경에 쓴 `sed`가 15개 파일의 줄바꿈을 CRLF → LF로 바꿈, 확인 명령도 잘못돼 "CRLF 유지"라고 잘못 보고 | 커밋 내용은 영향 없음 (git이 LF로 저장). 작업 폴더만 LF가 됨 → CRLF로 복구 | 줄바꿈은 바이트 단위(`\r\n` 개수)로 확인한다 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `StlAllocatorAllocFree` | `StlAllocator<Int32>` 직접 호출: `allocate(10)` → 16바이트 정렬 → 10칸 쓰고 읽기 → `deallocate` | `OK` (Debug/Release) |
| `StlAllocatorContainers` | `vector` 1~1000 `push_back`(재할당 여러 번) → 크기·합계 / `map` 키 1~1000 삽입 → 짝수 키 `erase` → 크기 500, 홀수 키의 값이 키 × 10 | `OK` (Debug/Release) |

> 📝 2026-11-11: `StlAllocator` 리뷰 지적 사항 수정(`value_type`, 비교 연산자, 정렬 검사, 주석)과 테스트는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `allocate(count)`에서 `count`를 바이트 수로 착각해 `DefaultAllocator::Alloc(count)`로 넘기면? Debug(Stomp)에서는 어디서 드러날까?**
> ✅ 내 답: `count`를 바이트로 넘기면 너무 작게 할당돼서 오버플로가 발생한다.
> 💬 보충: `vector<Int32>`가 10개를 요청하면 40바이트가 필요한데 10바이트(16으로 올림 = Int32 4칸)만 받는다.
> - Debug(Stomp): 16바이트가 페이지 끝에 붙어 있어 **5번째 원소(`[4]`)를 쓰는 순간 Access Violation**. 크래시 위치는 STL 내부(`push_back` → 원소 생성)지만 호출 스택으로 원인을 찾을 수 있다.
> - Release(Pool): 26바이트 블록이 32 풀에서 나오고(데이터 16바이트), 넘친 쓰기가 옆 메모리를 조용히 덮는다 → 한참 뒤 엉뚱한 곳에서 크래시.
> - 함정: `sizeof(T) == 1`인 타입(`Int8`, `char`)은 개수와 바이트 수가 같아 버그가 드러나지 않는다. 그래서 테스트는 `Int32`로 한다.

**Q2. `std::map<..., StlAllocator<std::pair<const K, V>>>`의 map은 실제로 무엇을 할당하나? 그때 `StlAllocator`의 어느 부분이 쓰이나?**
> ✅ 내 답: 트리 노드를 할당하고, 변환 생성자가 쓰인다.
> 💬 보충: 노드는 자식/부모 포인터, 색, 끝 표시, 그리고 `pair<const K, V>`를 묶은 구조다. `allocator_traits::rebind_alloc`이 `StlAllocator<노드>` **타입**을 만들고, 그 **객체**를 원래 할당기에서 만들 때 변환 생성자가 호출된다. 이후 `allocate(1)`은 `sizeof(노드)`를 요청한다. map은 비어 있어도 머리 노드를 하나 할당하고, MSVC Debug의 vector도 반복자 검사 객체를 rebind로 할당한다.

**📝 세션 중 질문과 답**
- `using value_type = Type;`은 왜 쓰나? → STL 할당기 규약에서 유일하게 반드시 있어야 하는 타입 이름이다. `allocator_traits`가 이것을 기준으로 나머지 타입을 자동으로 채우고, 컨테이너가 자기 원소 타입과 같은지 검사한다. 없으면 컨테이너에 넣는 순간 컴파일 오류.
- `deallocate(Type* pPtr, USize /* count */)`에서 이름을 왜 주석 처리했나? → 규약 때문에 받아야 하지만 쓰지 않는 값이다. 이름을 지워 경고를 막고, 주석으로 의미는 남긴다.
- `==`/`!=`도 꼭 써야 하나? → C++17 규약의 필수 항목이다. move 대입, `swap`, `splice`처럼 메모리 소유권을 옮길 때 쓰일 수 있고, 템플릿이라 그때 가서야 오류가 나므로 처음부터 채워 둔다. 실행 비용은 0.

---

## Step 6-2. MakeShared → allocate_shared

### 🎯 목표
`MakeShared`가 `std::allocate_shared` + `StlAllocator`를 써서 **컨트롤 블록과 객체를 한 번의 할당으로, 우리 할당기에서** 받게 바꾼다. Step 5에서 남긴 한계(컨트롤 블록은 기본 `new`)를 없앤다.

### 📖 개념 1 — 두 방식 비교 (설계 결정)
```
[Step 5]   MakeShared = xnew + 삭제자 xdelete
  객체        ──▶ 우리 할당기 (Stomp/Pool)
  컨트롤 블록 ──▶ 기본 new (CRT 힙)          ← 할당 2번, 서로 다른 곳

[Step 6-2] MakeShared = std::allocate_shared<Type>(StlAllocator<Type>(), args...)
  [ 컨트롤 블록 | 객체 ] ──▶ 우리 할당기     ← 할당 1번, 한 덩어리
```

| | xnew + 삭제자 | allocate_shared |
|---|---|---|
| 할당 횟수 | 2번 | **1번** (카운트와 객체가 붙어 있어 캐시에도 유리) |
| 컨트롤 블록 위치 | CRT 힙 (우리 시스템 밖) | **우리 할당기** (Debug면 Stomp 검사도 받음) |
| 해제 | 강한 참조 0 → 객체 메모리 즉시 반납 | 강한 참조 0 → 소멸자만, **weak_ptr까지 0 → 메모리 반납** |
| 생성자 | `xnew`가 호출 | 표준 라이브러리 내부가 호출 → **public 생성자 필요** |

→ `weak_ptr` 때문에 블록 반납이 늦어지는 것은 우리 서버에서 영향이 작다고 판단하고 **allocate_shared로 결정**했다 (2026-11-11).

> 💡 **비유**: 이삿짐 박스와 송장. 송장(컨트롤 블록: 참조 수 + 내용물 타입)을 박스(객체)에 붙여서 한 번에 배송받는다.

### 📖 개념 2 — 호출 하나가 안에서 하는 일
```
MakeShared<Knight>(100, 10)
  ① 크기 계산: [컨트롤 블록 | Knight] 한 덩어리
  ② StlAllocator<Knight> → "덩어리 타입"용 할당기로 rebind (Step 6-1의 변환 생성자)
  ③ allocate(1) → DefaultAllocator::Alloc(덩어리 크기)   ← 할당은 이 한 번뿐
  ④ 컨트롤 블록 초기화 (강한 참조 1, 약한 참조 1)
  ⑤ 덩어리 안의 Knight 자리에 Knight(100, 10) 생성
```

### 📖 개념 3 — 해제는 두 단계
| 시점 | 일어나는 일 |
|---|---|
| 강한 참조(`SharedPtr`)가 0 | **소멸자 호출**. 객체로서는 끝 |
| 약한 참조(`weak_ptr`)까지 0 | **메모리 반납** (`deallocate` → 풀) |

- `weak_ptr`가 "아직 살아 있나?"에 답하려면 참조 수가 적힌 컨트롤 블록이 남아 있어야 한다. 한 덩어리라 객체 부분만 떼어 돌려줄 수 없다.

### 📖 개념 4 — StlAllocator는 컨테이너 전용이 아니다
- "STL 할당기"는 **표준 라이브러리의 할당기 규약**을 따른다는 뜻이다. 컨테이너뿐 아니라 `std::allocate_shared`, `std::basic_string` 등 할당기를 받는 표준 기능 어디에든 꽂힌다.
- 실제 메모리는 모두 같은 곳(`DefaultAllocator` → Debug: Stomp / Release: Pool)에서 온다.
```
xnew<T>(...)                  ────────────────────────────┐
std::vector<T, StlAllocator>  ─▶ StlAllocator ─▶ DefaultAllocator ─┬─ Debug   → StompAllocator
MakeShared<T>(...)            ─▶ StlAllocator ─┘                   └─ Release → PoolAllocator → GMemory
```

| 상황 | 쓰는 것 |
|---|---|
| 한 곳에서만 소유 (수명이 분명) | `xnew` / `xdelete` |
| 여러 곳이 함께 붙잡음 (세션, Room, Job) | `MakeShared` |
| 컨테이너 | `std::vector<T, StlAllocator<T>>` (→ `Container.h` 별칭) |

> 💡 **비유**: 표준 규격(USB-C)에 맞춘 충전기는 "휴대폰용"으로 샀어도 같은 포트가 있는 노트북에도 꽂힌다.

### 🧱 핵심 코드
```cpp
// Memory.h — StlAllocator가 먼저 선언되어 있어야 하므로 그 아래에 둔다
template<typename Type, typename... Args>
SharedPtr<Type> MakeShared(Args&&... args)
{
	return std::allocate_shared<Type>(StlAllocator<Type>(), std::forward<Args>(args)...);
}
```


### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| (설계 예상) `MakeShared`를 `StlAllocator` 위에 둔 채로 바꾸기 | `StlAllocator` 이름을 찾지 못해 컴파일 오류 | 템플릿도 쓰는 이름은 먼저 선언되어 있어야 한다 → 순서: `xnew`/`xdelete` → `StlAllocator` → `MakeShared` |
| (테스트) `MakeSharedPoolReuse`가 `xnew`와 주소를 비교 | 블록 크기가 "컨트롤 블록 + 객체"로 커져 다른 풀에서 나올 수 있음 | 재사용 비교는 같은 크기의 할당끼리 (`MakeShared` ↔ `MakeShared`) |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `MakeSharedRefCount` | (수정 없음) 생성자 인자, `use_count`, 마지막 참조에서 소멸자 1회 | `OK` (Debug/Release) — 바깥 동작이 같아 그대로 통과 |
| `MakeSharedPoolReuse` | (수정) `MakeShared` → `reset()` → 다음 `MakeShared`가 같은 주소, 생성자 인자 전달 | Release `OK`, Debug `SKIP` |
| `MakeSharedWeakHoldsBlock` | (신규) ① weak만 남기고 `reset()` → 소멸자 1회, `expired()` ② 그동안 새 `MakeShared`는 다른 주소 ③ weak 해제 후 다음 `MakeShared`가 원래 주소 | Release `OK`, Debug `SKIP` |

```
MakeSharedWeakHoldsBlock — 풀 맨 위 따라가기
 ① p1(A) 생성 → weak = p1 → p1.reset()   : ~Knight() 호출, A는 weak가 붙잡음 (풀에 없음)
 ② p2 생성 → A가 아닌 B → p2.reset()      : 풀 [ B ]
 ③ weak.reset()                          : 풀 [ B, A ← 맨 위 ] → p3가 A를 받음
```

> 📝 2026-11-11: Step 6-2 코드(`MakeShared` 변경, 주석)와 테스트는 사용자 요청으로 Claude가 작성했다. Debug는 Stomp가 페이지를 OS에 바로 돌려주므로 "같은 주소 재사용 = 풀 반납" 판단이 의미 없어 `SKIP`한다.

### 🧩 복습 질문
**Q1. `allocate_shared`로 만든 객체에 `weak_ptr` 하나만 남고 `SharedPtr`는 모두 사라졌다. 소멸자와 메모리 반납은 각각 언제 일어날까? Step 5 방식(`xnew` + 삭제자)이었다면 무엇이 달랐을까?**
> ❓ 내 답: (해설 요청)
> 💬 해설: strong 0이면 소멸, weak 0이면 컨트롤 블록이 끝난다. `allocate_shared`는 `[컨트롤 블록 | Knight]`가 **한 블록**이라 strong 0에서 `~Knight()`만 불리고 메모리는 못 돌려준다. weak 0이 되어야 블록 전체가 GMemory 풀로 돌아간다 (`MakeSharedWeakHoldsBlock`). Step 5 방식(`xnew` + `SharedPtr(pObj, xdelete<Type>)`)은 객체와 컨트롤 블록이 **따로**라서, strong 0에서 `xdelete`가 `~Knight()` + Knight 블록을 바로 풀에 돌려주고, OS 힙(기본 `new`)에서 받은 컨트롤 블록만 weak 0까지 남았다가 `delete`된다.
>
> 💡 Step 5 방식은 Step 8-2의 "2인자 `SharedPtr(pObj, Push)`"와 같은 구조다. 그래도 `allocate_shared`로 바꾼 이유: 할당 2번 → 1번, 둘 다 우리 할당기, 카운트와 객체가 붙어 캐시에 유리. 대가는 weak가 오래 남으면 객체 메모리까지 묶인다는 것.

**Q2. 이제 `MakeShared`에 `xdelete<Type>` 삭제자가 없다. 그래도 `SharedPtr<Base> p = MakeShared<Knight>();`의 마지막 참조가 사라질 때 `~Knight()`가 불릴까? 누가 그것을 알고 있을까?**
> ❓ 내 답: (해설 요청)
> 💬 해설: 불린다. `~Base()`가 `virtual`이 아니어도 불린다. 아는 쪽은 **컨트롤 블록**이다. `allocate_shared<Knight>`는 `Knight`를 아는 컨트롤 블록 타입을 만들고, strong 0이 되면 `shared_ptr`는 그 컨트롤 블록의 가상 함수에 소멸을 맡긴다. `SharedPtr<Base>`로 옮겨 담아도 바뀌는 건 겉의 포인터 타입뿐이고 컨트롤 블록은 그대로 공유된다 (타입 소거). Step 5에서는 그 정보가 삭제자 `xdelete<Knight>`였고, 지금은 컨트롤 블록 클래스 자체다.
>
> ⚠️ 이 보호는 처음부터 `Knight` 타입으로 만든 `SharedPtr`에만 있다. `Base*`를 `xdelete`하거나, `Base*` 포인터로 `SharedPtr`를 만들거나, `UniquePtr`(컨트롤 블록 없음)를 쓰면 `~Knight()`가 안 불린다 → 다형성 부모의 소멸자는 여전히 `virtual`로 둔다.

**📝 세션 중 질문과 답**
- `MakeShared`를 쓰면 `StlAllocator`를 쓰게 되나? `StlAllocator`는 STL용 아닌가? → 그렇다. `std::allocate_shared`도 할당기 규약을 받는 표준 라이브러리 함수라 같은 할당기가 꽂힌다. 실제 메모리는 `xnew`, 컨테이너와 같은 `DefaultAllocator`에서 온다 (개념 4).

---

## Step 7. Container.h

### 🎯 목표
`std::vector<T, StlAllocator<T>>`처럼 긴 이름을 `Vector<T>` 같은 **짧은 별칭**으로 묶어, 서버 코드 어디서나 우리 할당기를 쓰는 컨테이너를 쉽게 쓰게 한다.

### 📖 개념 1 — 템플릿 별칭
```cpp
template<typename Type>
using Vector = std::vector<Type, StlAllocator<Type>>;
```
- `Vector<Int32>`는 컴파일러가 `std::vector<Int32, StlAllocator<Int32>>`로 바꿔 읽는다. 새 타입이 아니라 **이름만 짧게** 하는 것이라 실행 비용이 0이다.
- 할당기를 빠뜨리는 실수가 사라진다. 서버 코드에서는 `std::` 컨테이너를 직접 쓰지 않고 이 별칭만 쓴다.

### 📖 개념 2 — 컨테이너마다 할당기 자리가 다르다
| 별칭 | 원래 타입 | 할당기가 받는 타입 |
|---|---|---|
| `Vector<T>`, `List<T>`, `Deque<T>` | `std::vector`, `std::list`, `std::deque` | `T` |
| `Map<K, V, Pred = std::less<K>>` | `std::map` | **`std::pair<const K, V>`** |
| `Set<K, Pred>` | `std::set` | `K` |
| `HashMap<K, V, Hasher, KeyEq>` | `std::unordered_map` | **`std::pair<const K, V>`** |
| `HashSet<K, Hasher, KeyEq>` | `std::unordered_set` | `K` |
| `Queue<T>`, `Stack<T>`, `PriorityQueue<T>` | `std::queue`, `std::stack`, `std::priority_queue` | **할당기 자리가 없음** → 안쪽 컨테이너를 바꾼다 |
| `String`, `WString` | `std::basic_string<char>` / `<wchar_t>` | `char` / `wchar_t` |

- `Map`, `HashMap`은 비교/해시 함수에 기본값을 줘서 보통은 `Map<Int32, Int32>`처럼 두 개만 쓴다.

### 📖 개념 3 — 어댑터는 안쪽 컨테이너를 바꾼다
- `std::queue<T, Container>`의 두 번째 인자는 할당기가 아니라 **안에 담을 컨테이너 타입**이다. 어댑터는 메모리를 직접 관리하지 않고 안쪽 컨테이너(기본값 `std::deque`)에 맡긴다.
- 그래서 안쪽 컨테이너의 기본값을 우리 `Deque<T>`(`PriorityQueue`는 `Vector<T>`)로 바꿔 준다. `Deque`/`Vector` 별칭이 먼저 선언되어 있어야 기본값으로 쓸 수 있다.
- `typename Container::value_type`의 `typename`은 "템플릿 인자 안의 이 이름은 타입이다"라고 컴파일러에 알려 주는 표시다.

> 💡 **비유**: `Queue`는 줄 서는 규칙(앞에서 꺼내고 뒤에 넣기)일 뿐이고, 실제 의자(메모리)는 안쪽 `Deque`가 갖고 있다. 의자를 우리 가구점(할당기)에서 사 오려면 의자 주인인 `Deque`를 바꿔야 한다.

### 📖 개념 4 — 짧은 문자열은 할당하지 않는다 (SSO)
```
String 객체 안 (32바이트)
┌────────────────────────────────┬──────────┬──────────┐
│ 내부 버퍼 16바이트              │ 길이      │ 용량      │
│ "Hello\0"  ← 여기에 바로 저장   │ 5        │ 15       │
└────────────────────────────────┴──────────┴──────────┘
   15자를 넘으면 이 자리에 힙 주소를 넣고 allocate 호출
```
- MSVC `std::basic_string`은 객체 안에 16바이트 버퍼를 갖는다 (`xstring`의 `_BUF_SIZE = 16 / sizeof(문자)`로 확인). `String`은 **15자**, `WString`(wchar_t 2바이트)은 **7자**까지 할당 없이 저장한다.
- 서버의 문자열(이름, 키, 짧은 명령어)은 대부분 짧아서 이 최적화가 크다.
- ⚠️ 그래서 문자열 테스트는 **16자 이상**과 **이어 붙여서 길어지는 경우**를 따로 해야 할당기를 실제로 거친다. 짧은 문자열만 테스트하면 할당기 연결이 틀려도 통과한다.

### 🧱 핵심 코드
```cpp
// Container.h (일부)
template<typename Type>
using Vector = std::vector<Type, StlAllocator<Type>>;

template<typename Key, typename Type, typename Pred = std::less<Key>>
using Map = std::map<Key, Type, Pred, StlAllocator<std::pair<const Key, Type>>>;

template<typename Key, typename Type, typename Hasher = std::hash<Key>, typename KeyEq = std::equal_to<Key>>
using HashMap = std::unordered_map<Key, Type, Hasher, KeyEq, StlAllocator<std::pair<const Key, Type>>>;

template<typename Type, typename Container = Deque<Type>>
using Queue = std::queue<Type, Container>;

template<typename Type, typename Container = Vector<Type>, typename Pred = std::less<typename Container::value_type>>
using PriorityQueue = std::priority_queue<Type, Container, Pred>;

using String = std::basic_string<char, std::char_traits<char>, StlAllocator<char>>;
```
```cpp
// CorePch.h — GameServer / DummyClient에서 바로 쓰도록
#include "Memory.h"
#include "Container.h"
```

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `Container.h`를 폴더에만 만들고 프로젝트에 등록 안 됨 (VS 프로젝트 미저장) | 솔루션 탐색기·다른 PC에서 빠짐 | 새 파일은 `.vcxproj`/`.filters` 등록까지 확인 (Claude가 등록) |
| (확인 질문) 짧은 `String`도 `allocate`를 부른다고 답함 | — | MSVC는 15자까지 객체 안 버퍼에 저장 (SSO). 할당기 테스트는 긴 문자열로 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `ContainerAliases` | 10개 별칭 기본 동작: Vector 순서, List/Deque 앞뒤 넣기, Map/Set 정렬·찾기·중복 무시, HashMap/HashSet 찾기, Queue FIFO, Stack LIFO, PriorityQueue 큰 값부터 | `OK` (Debug/Release) |
| `ContainerString` | `String` 짧은 문자열(5자) / 긴 문자열(25자) / 이어 붙여 32자, `WString` 4자 → 21자 | `OK` (Debug/Release) |

> 📝 2026-11-11: `Container.h`(별칭, 주석, 프로젝트 등록, CorePch 추가)와 테스트는 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `Queue`는 왜 `std::queue<Type, StlAllocator<Type>>`처럼 할당기를 바로 넣지 않고 안쪽 컨테이너 `Deque<Type>`을 넣을까?**
> 🔺 내 답: `Queue`는 내부에서 `Deque`로 구현되어 있기 때문이다.
> 💬 보충: 핵심은 **`std::queue`에는 할당기 자리가 아예 없다**는 것이다. 두 번째 템플릿 인자는 안에 담을 **컨테이너 타입**이라, `StlAllocator`를 넣으면 컨테이너로 착각해 `push_back`, `front`를 찾다가 컴파일 오류가 난다. 기본값 `std::deque<T>`는 `std::allocator`를 쓰므로, 안쪽 컨테이너를 우리 `Deque<T>`로 바꿔야 메모리가 우리 할당기에서 나온다. `Queue<Int32, List<Int32>>`처럼 다른 안쪽 컨테이너도 쓸 수 있다.

**Q2. `String s = "Hello";`처럼 짧은 문자열을 만들 때 `StlAllocator::allocate`가 불릴까?**
> ❌ 내 답: 네 (불린다).
> 💬 정답: **불리지 않는다.** MSVC `std::basic_string`은 객체 안에 16바이트 버퍼를 갖고 있어(SSO), `String`은 15자, `WString`은 7자까지 할당 없이 저장한다. 그보다 길어질 때만 `allocate`를 부른다. 그래서 `ContainerString`은 25자 문자열과 이어 붙여 길어지는 경우를 따로 확인한다.

---

## Step 8-1. ObjectPool

### 🎯 목표
타입 하나만 담는 전용 풀 `ObjectPool<Type>`을 만든다. `Pop(args...)`로 꺼내고 `Push(pObj)`로 돌려준다. Debug는 `StompAllocator`, Release만 전용 풀을 쓴다.

### 📖 개념 1 — 공용 사물함 vs 전용 사물함
| | `GMemory` (Step 4-1) | `ObjectPool<Type>` |
|---|---|---|
| 풀 기준 | 크기 구간 (여러 타입이 같은 풀을 나눠 씀) | 타입 하나 |
| 블록 크기 | 요청보다 조금 클 수 있음 | `sizeof(Type) + 헤더`에 딱 맞음 |
| 개수 확인 | 크기별 개수만 | 타입별 사용 중 개수 (`GetUseCount`) |
| 버그 범위 | 해제 후 접근이 같은 블록을 받은 **다른 타입**을 망가뜨림 | 같은 타입 안으로 좁혀짐 |

> 💡 **비유**: `GMemory`는 크기별 공용 사물함, `ObjectPool<Knight>`는 Knight 전용 사물함이다. 전용 사물함은 칸이 딱 맞고, 몇 칸이 쓰이는지 바로 보이고, 다른 손님이 내 칸을 받지 않는다.

### 📖 개념 2 — 템플릿과 Windows.h 경계 (ObjectPoolBase)
```
ObjectPool.h (공개)                         ObjectPool.cpp (ServerCore 안)
──────────────────────────                  ──────────────────────────────
class MemoryPool;      ← 이름만 알림         #include "MemoryPool.h"  ← 여기서만 Windows.h
class ObjectPoolBase                         new MemoryPool(...)
{  MemoryPool* pPool_; }  ← 포인터만         pPool_->Pop() / Push()
template ObjectPool<T> → ObjectPoolBase만 호출
```
- 템플릿은 쓰는 쪽에서 컴파일되므로 **헤더에 전부 있어야** 한다. `ObjectPool<T>`가 `MemoryPool`을 값 멤버로 가지면 `MemoryPool.h`(Windows.h)가 GameServer와 클라이언트 엔진까지 퍼진다.
- 포인터는 크기가 항상 8바이트라 **전방 선언만으로** 멤버를 만들 수 있다. `MemoryPool`의 정의가 필요한 코드는 템플릿이 아닌 `ObjectPoolBase`의 `.cpp`에만 둔다 (Pimpl 기법).

### 📖 개념 3 — 타입마다 풀 하나 (함수 안 static)
- `GetBase()` 안의 `static ObjectPoolBase SBase(sizeof(Type))`는 템플릿 인자마다 따로 생긴다 → `ObjectPool<Knight>`와 `ObjectPool<Monster>`는 다른 풀.
- 처음 호출될 때 한 번만 만들어지고, C++11부터 이 초기화는 스레드 안전하다.
- Debug에서는 `GetBase()`를 부르지 않으므로 풀이 만들어지지도 않는다.

### 🧱 핵심 코드
```cpp
// ObjectPool.cpp
ObjectPoolBase::ObjectPoolBase(Int32 dataSize)
{
	blockSize_ = dataSize + sizeof(MemoryHeader);   // 헤더 포함 블록 크기
	pPool_ = new MemoryPool(blockSize_);
}

void* ObjectPoolBase::Alloc()
{
	MemoryHeader* pHeader = pPool_->Pop();
	void* pObject = MemoryHeader::AttachHeader(pHeader, blockSize_);
	return pObject;
}

void ObjectPoolBase::Release(void* pPtr)
{
	MemoryHeader* pHeader = MemoryHeader::DetachHeader(pPtr);
	pPool_->Push(pHeader);
}
```
- `AttachHeader(pHeader, size)`: `pHeader`는 헤더를 만들 블록 시작 주소, `size`는 헤더의 `allocSize_`에 적을 **헤더 포함 블록 크기**다. 이 값이 0보다 크면 "사용 중" 표시가 되어 `Push`의 이중 해제 검사가 동작한다.

```cpp
// ObjectPool.h (일부)
template<typename Type>
class ObjectPool
{
public:
	STATIC_ASSERT(alignof(Type) <= 16, "ObjectPool supports alignment up to 16 bytes");

	template<typename... Args>
	static Type* Pop(Args&&... args)
	{
#ifdef _DEBUG
		void* pMemory = StompAllocator::Alloc(static_cast<Int32>(sizeof(Type)));
#else
		void* pMemory = GetBase().Alloc();
#endif
		Type* pObj = new(pMemory) Type(std::forward<Args>(args)...);
		return pObj;
	}

	static void Push(Type* pObj)
	{
		if (pObj == nullptr)
			return;

		pObj->~Type();
#ifdef _DEBUG
		StompAllocator::Release(pObj);
#else
		GetBase().Release(pObj);
#endif
	}

private:
	static ObjectPoolBase& GetBase()
	{
		static ObjectPoolBase SBase(static_cast<Int32>(sizeof(Type)));
		return SBase;
	}
};
```
- `Pop`과 `Push`는 같은 `#ifdef`로 나눠서 **받은 곳으로 돌려주는** 짝을 맞춘다. `Pop`한 객체를 `delete`/`xdelete`로 지우면 다른 곳에 반납된다.

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `MemoryPool.h`에 `#include "ObjectPool.h"`를 넣음 | 포함 방향이 반대 (MemoryPool은 ObjectPool을 몰라도 됨) | 필요한 쪽(`ObjectPool.cpp`)이 include한다 |
| `ObjectPool.h/.cpp`를 프로젝트에 등록 안 함 | `.cpp`가 컴파일되지 않아 Release `Pop`에서 링크 에러 | 새 파일은 `.vcxproj`/`.filters` 등록까지 확인 (Claude가 등록) |
| `ObjectPool.cpp`에 `#include "pch.h"` 없음 | PCH를 쓰는 프로젝트라 C1010 | ServerCore `.cpp`의 첫 줄은 `pch.h` (Claude가 추가) |
| `AttachHeader` 반환값을 버리고 `nullptr` 반환 (작성 중) | 사용자가 데이터 주소를 못 받음 | `AttachHeader`가 돌려주는 헤더 뒤 주소를 그대로 반환 |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `ObjectPoolPopPush` | 생성자 1회·인자 전달·16바이트 정렬, Push 후 소멸자 1회, `Push(nullptr)` 무시 / Release만: useCount 1 → 0, 다음 Pop이 같은 주소 재사용 | `OK` (Debug/Release) |
| `ObjectPoolConcurrent` | 스레드 4개 × 10,000번 `Pop(mark)` → 16칸 확인 → Push, 섞임 0 / Release만: useCount 0. 보조 클래스 `ObjectPoolTestBlock` (`MemoryTestKnight`의 static 카운터는 atomic이 아니라 동시성 테스트에 쓰지 않음) | `OK` (Debug/Release) |

> 📝 2026-11-17: `ObjectPoolBase`는 사용자가 작성했다. `ObjectPool<Type>` 템플릿, `[ObjectPool]` 블록 주석, 테스트, 프로젝트 등록은 사용자 요청으로 Claude가 작성했다.

### 🧩 복습 질문
**Q1. `GMemory`가 있는데 타입별 풀을 따로 두면 좋은 점은? (2가지 이상)**
> ✅ 내 답: 타입별 객체가 몇 개 생성되어 있는지 알 수 있다. 딱 맞게 메모리를 효율적으로 사용할 수 있다.
> 💬 보충: 하나 더 — **오염 격리**. `GMemory`에서는 크기가 비슷한 다른 타입이 같은 블록을 번갈아 받으므로, Knight의 해제 후 접근이 Monster 데이터를 망가뜨리고 크래시도 Monster 코드에서 난다. 전용 풀이면 범위가 Knight 안으로 좁혀진다. 타입별 개수는 누수 추적에 쓴다 (접속자 0인데 `GameSession` 100개 → Push 누락).

**Q2. `MemoryPool`을 헤더 멤버로 두지 않고 `ObjectPoolBase`(.cpp) + 포인터로 감싼 이유는?**
> ❓ 내 답: 모르겠다.
> 💬 해설: 템플릿 `ObjectPool<T>`는 헤더에 전부 있어야 하므로, `MemoryPool`을 값 멤버로 두면 `MemoryPool.h`(Windows.h)가 이 헤더를 쓰는 모든 곳(GameServer, 클라이언트 엔진)으로 퍼진다. 포인터는 전방 선언만으로 둘 수 있으니, `MemoryPool`을 실제로 다루는 코드는 템플릿이 아닌 `ObjectPoolBase`의 `.cpp`에 숨긴다 (Pimpl). 창구 직원(`ObjectPoolBase`)만 창고(`MemoryPool`)에 들어가는 구조다.

---

## Step 8-2. ObjectPool용 MakeShared

### 🎯 목표
`ObjectPool<Type>`에서 꺼낸 객체를 `SharedPtr<Type>`로 감싸서, 마지막 강한 참조가 사라질 때 자동으로 `Push`되게 한다.

### 📖 개념 1 — shared_ptr의 3인자 생성자
| 인자 | 역할 | 넣은 것 |
|---|---|---|
| ① 포인터 | 이미 만들어진 객체 | `Pop(args...)`의 결과 |
| ② 삭제자 | 강한 참조가 0이 될 때 `delete` 대신 호출 | `Push` (정적 멤버 함수 → 함수 포인터) |
| ③ 할당기 | 컨트롤 블록(참조 카운트, 삭제자)을 할당 | `StlAllocator<Type>()` (내부 컨트롤 블록 타입으로 rebind) |

> 💡 컨트롤 블록을 할당하다 예외가 나면 생성자가 삭제자(`Push`)를 먼저 불러 준다. 그래서 try/catch 없이도 객체가 새지 않는다.

### 📖 개념 2 — 객체와 컨트롤 블록이 따로 할당된다
```
[Memory.h MakeShared — allocate_shared]
  ┌─────────── 한 덩어리 (DefaultAllocator) ───────────┐
  │ 컨트롤 블록(strong, weak) │ Type 객체              │
  └────────────────────────────────────────────────────┘

[ObjectPool::MakeShared — 3인자 생성자]
  ┌ 컨트롤 블록 (StlAllocator) ┐      ┌ Type 객체 (ObjectPool<Type>) ┐
  │ strong, weak, 삭제자       │ ───▶ │                              │
  └────────────────────────────┘      └──────────────────────────────┘
```
할당은 2번이지만, 객체 블록과 컨트롤 블록의 수명이 따로 움직인다.

### 📖 개념 3 — 할당기는 불완전 타입으로 인스턴스화될 수 있어야 한다
3인자 생성자가 만드는 컨트롤 블록(`_Ref_count_resource_alloc`)은 **자기 자신용으로 rebind된 할당기를 멤버로** 가진다. 컨트롤 블록 클래스가 다 만들어지기 전에 `StlAllocator<컨트롤 블록>`이 인스턴스화되므로, 할당기 **클래스 본문**에서 `alignof(Type)`, `sizeof(Type)`을 쓰면 불완전 타입 에러가 난다. 타입이 완전해야 하는 검사는 `allocate()` 같은 **멤버 함수 안**에 둔다 (멤버 함수 본문은 호출될 때 인스턴스화된다).

### 🧱 핵심 코드
```cpp
// ObjectPool.h — ObjectPool<Type>
template<typename... Args>
static SharedPtr<Type> MakeShared(Args&&... args)
{
	// ① 객체는 이 풀에서 받는다
	Type* pObj = Pop(std::forward<Args>(args)...);

	// ② 삭제자 = Push, 컨트롤 블록은 StlAllocator로 받는다
	return SharedPtr<Type>(pObj, Push, StlAllocator<Type>());
}
```
```cpp
// Memory.h — StlAllocator<Type>
Type* allocate(USize count)
{
	// 클래스 본문이 아니라 여기서 검사한다 (컨트롤 블록이 아직 불완전 타입일 수 있음)
	STATIC_ASSERT(alignof(Type) <= alignof(std::max_align_t), "over-aligned type is not supported");
	...
}
```
- MakeShared로 받은 객체는 SharedPtr가 관리한다. `get()` 포인터를 직접 `Push`하면 이중 반납이 된다.
- Release의 `StlAllocator`는 `GMemory`를 쓰므로 `CoreGlobal`이 살아 있는 동안에만 쓴다.

### ⚠️ 겪은 실수
| 실수 | 결과 | 교훈 |
|---|---|---|
| `SharedPtr<Type> Pop(std::forward(args))` (작성 중) | `Pop`이라는 이름의 변수 선언으로 해석, `forward`의 타입 인자와 `...` 누락, `return` 없음 | `Pop` 호출 → `Type*` 지역 변수 → 3인자 SharedPtr 반환, 두 단계로 나눈다 |
| `StlAllocator` 클래스 본문의 `STATIC_ASSERT(alignof(Type) ...)` (Step 6-1부터 있던 코드) | 3인자 shared_ptr에서 C2139 "정의되지 않은 클래스" | 템플릿은 쓰여야 검사된다. 할당기 본문에서는 `Type`의 완전성을 요구하지 않는다 (Claude가 `allocate()`로 옮김) |

### 🧪 테스트 (`GameServer/Test.cpp`)
| 함수 | 확인 | 기대 결과 |
|---|---|---|
| `ObjectPoolMakeShared` | `use_count` 1 → 2 → 1, 복사본을 놓아도 소멸자 0회, weak_ptr을 남기고 마지막 reset → 소멸자 1회·`expired()` / Release만: 생성 후 useCount 1, weak_ptr이 살아 있어도 useCount 0, 다시 만들면 같은 주소 재사용. 보조 클래스 `MemoryTestKnight` | `OK` (Debug/Release) |

> 📝 2026-11-18: `MakeShared` 본문, `[MakeShared]` 블록 주석, `StlAllocator` 검사 위치 수정, 테스트는 사용자 요청으로 Claude가 작성했다. `Memory.h` include는 사용자가 넣었다.

### 🧩 복습 질문
**Q1. weak_ptr만 남았을 때 메모리에 남아 있는 것과 이미 돌아간 것은? `Memory.h`의 `MakeShared`였다면?**
> ❓ 내 답: (해설 요청)
> 💬 해설: `weak_ptr`는 `expired()`/`lock()`을 할 때 **컨트롤 블록의 strong 카운트**를 읽는다. 그래서 strong 0이면 객체를 치우지만, 컨트롤 블록은 weak 0까지 남아야 한다. `ObjectPool::MakeShared`는 객체와 컨트롤 블록을 **따로** 할당했으므로, strong 0에서 삭제자 `Push`가 `~Knight()`를 부르고 **객체 블록은 바로 전용 풀로 돌아간다** (다음 `Pop`이 같은 주소를 받는다). 남는 것은 **컨트롤 블록뿐**이고, weak 0에서 저장해 둔 `StlAllocator`로 GMemory 풀에 돌아간다. `Memory.h`의 `MakeShared`(`allocate_shared`)는 `[컨트롤 블록 | Knight]`가 **한 블록**이라 strong 0에서 `~Knight()`만 불리고 메모리는 하나도 못 돌려준다. 죽은 Knight 자리까지 weak 0을 기다렸다가 블록 전체가 GMemory 풀로 돌아간다.
>
> | weak_ptr만 남았을 때 | Memory.h `MakeShared` | `ObjectPool::MakeShared` |
> |---|---|---|
> | `~Knight()` | 불림 | 불림 |
> | 객체 메모리 | 아직 묶여 있음 | 이미 전용 풀로 |
> | 컨트롤 블록 | 남음 (객체 자리와 함께) | 남음 (혼자) |
>
> 💡 두 방식 모두 **소멸(strong 0)과 메모리 반납(weak 0)이 갈라진다.** 한 덩어리는 객체 메모리까지 weak 0을 기다리고, 따로 할당하면 컨트롤 블록 크기만 남는다. 큰 객체를 `weak_ptr`로 오래 참조하면 이 차이가 커진다. 대신 따로 할당은 할당이 두 번이고 카운트와 객체가 떨어져 있다. 테스트: `ObjectPoolMakeShared` 3~5단계, `MakeSharedWeakHoldsBlock`.

**Q2. 할당기 인자를 빼고 `SharedPtr<Type>(pObj, Push)` 2인자로 만들면 무엇이 달라지나? 왜 3번째 인자를 넣었나?**
> ❓ 내 답: (해설 요청)
> 💬 해설: 객체는 이미 `Pop`으로 받았으므로 `SharedPtr` 생성자가 새로 할당하는 것은 **컨트롤 블록**(strong/weak 카운트, 객체 포인터, 삭제자, 할당기 사본)뿐이다. 3번째 인자는 이 컨트롤 블록을 어디서 받을지 정한다. 2인자면 표준 라이브러리가 전역 `new`/`delete`(OS 힙)로 컨트롤 블록을 만들고 지운다 → Debug에서 Stomp 검사를 못 받고, Release에서는 `MakeShared`마다 힙 할당이 한 번씩 생겨 풀의 이점이 절반으로 줄어든다. 3인자면 `StlAllocator<Type>`을 컨트롤 블록 타입으로 rebind해서 `allocate(1)` → `DefaultAllocator`(Stomp 또는 GMemory 풀)로 받고, 컨트롤 블록이 끝날 때는 **자기 안에 저장한 할당기 사본**으로 자신을 `deallocate`한다. 2인자도 동작은 맞다 (객체는 똑같이 `Push`로 전용 풀에 반납, 누수 없음). 차이는 할당 두 번 중 하나가 우리 시스템 밖으로 새는 것이다.
>
> 💡 삭제자(2번째 인자) = **객체를 어떻게 치울지**, 할당기(3번째 인자) = **컨트롤 블록을 어디서 받고 돌려줄지**. `<Type>`은 rebind되므로 실제 할당 타입과 달라도 된다. `allocate_shared`를 못 쓰는 이유: 객체와 컨트롤 블록을 한 덩어리로 직접 할당하므로 "객체는 ObjectPool 블록"이라는 조건을 지킬 수 없다.
