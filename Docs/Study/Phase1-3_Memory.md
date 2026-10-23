# 🧠 Phase 1-3. 메모리

> 💡 **목표**: `new`/`delete`를 직접 감싸서 **할당기를 한 곳에서 교체**할 수 있게 하고, 그 위에 메모리 풀(빠른 재사용)과 Stomp 할당기(Use-After-Free 즉시 검출)를 얹는다.

| Step | 파일 | 한 줄 요약 | 상태 |
|---|---|---|---|
| 1 | `Allocator.h/.cpp`, `Memory.h` | BaseAllocator + xnew / xdelete (placement new) | ✅ |
| 2 | `Allocator.h/.cpp` | StompAllocator (VirtualAlloc, 해제 후 접근 즉시 크래시) | ⬜ |

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
