# 🧠 Phase 1-3. 메모리

> 💡 **목표**: `new`/`delete`를 직접 감싸서 **할당기를 한 곳에서 교체**할 수 있게 하고, 그 위에 메모리 풀(빠른 재사용)과 Stomp 할당기(Use-After-Free 즉시 검출)를 얹는다.

| Step | 파일 | 한 줄 요약 | 상태 |
|---|---|---|---|
| 1 | `Allocator.h/.cpp`, `Memory.h` | BaseAllocator + xnew / xdelete (placement new) | ✅ |
| 2 | `Allocator.h/.cpp`, `Memory.h` | StompAllocator (VirtualAlloc, 해제 후 접근 즉시 크래시) | 🔄 진행 중 |

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
| `STATIC_ASSERT`로 정렬 검사 | malloc은 `max_align_t`까지만 정렬 보장. `alignas(64)` 타입을 조용히 잘못된 주소에 만드는 것을 **컴파일 때** 막는다 |
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

> 📝 진행 중. 지금은 개념(Use-After-Free와의 관계)까지 정리했다. 🧱 핵심 코드, ⚠️ 겪은 실수, 🧪 테스트는 구현을 마친 뒤 채운다.

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

### 🔧 강의와 다른 점
| 변경 | 이유 |
|---|---|
| **Debug에서만 Stomp 사용** (`_DEBUG`일 때 `USE_STOMP_ALLOCATOR` 정의 → `using DefaultAllocator = ...`) | 강의는 `#define _STOMP`가 항상 켜져 있어 Release 성능까지 떨어진다 (강의 결함 #5). 매크로 한 줄로 Debug에서도 끌 수 있어 나중에 MemoryPool을 Debug로 테스트할 수 있다 |
| **반환 주소 16바이트 정렬 보장** | size를 그대로 끝에 붙이면 `size = 4`일 때 주소가 `...FFC`가 된다. `xnew`의 정렬 약속(`max_align_t`)을 할당기가 바뀌어도 지키기 위해 |
| 상수를 `static constexpr std::size_t`로 (`PAGE_SIZE`, `ALIGNMENT`) | C++17에서 자동 `inline`이라 .cpp 정의 불필요. 크기 계산의 타입을 `size_t`로 통일 |
| 주소 계산에 `std::uintptr_t` | 포인터를 정수로 바꿀 때는 포인터 크기의 부호 없는 정수가 맞다 |
| `<Windows.h>`는 .cpp에만 | 헤더에 넣으면 ServerCore를 include하는 모든 파일(클라이언트 엔진 포함)로 퍼진다 |

### 🧩 복습 질문
**Q1. `int32` 3개(12B)를 할당하고 `[3]`에 쓰면 크래시가 날까? 이유는?**
> (미답변)

**Q2. 앞쪽으로 넘어가는 접근(`arr[-1]`, Underflow)은 이 방식으로 잡힐까? 안 잡힌다면 어떻게 바꿔야 잡을 수 있을까?**
> (미답변)
