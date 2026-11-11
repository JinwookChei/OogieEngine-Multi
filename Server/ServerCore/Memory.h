#pragma once
#include <new>
#include <vector>
#include <utility>
#include <cstddef>
#include <memory>
#include "CoreMacro.h"
#include "Types.h"
#include "Allocator.h"

/*
				[DefaultAllocator]
				xnew/xdelete가 쓰는 할당기. 빌드 구성에 따라 여기서 한 번만 고른다.
				- Debug  : StompAllocator (해제 후 접근, 오버플로를 그 줄에서 크래시로 잡는다)
				- Release: PoolAllocator  (빠르다. 크기별 풀에서 블록을 재사용한다)
				Alloc과 Release가 항상 같은 할당기를 쓰도록 이 별칭 하나로 묶는다.
*/
#ifdef _DEBUG
using DefaultAllocator = StompAllocator;
#else
using DefaultAllocator = PoolAllocator;
#endif

class MemoryPool;
class Memory
{
public:
	static constexpr Int32 MAX_ALLOC_SIZE = 4096;

public:
	Memory();
	~Memory();

	Memory(const Memory&) = delete;
	Memory& operator=(const Memory&) = delete;

	void* Allocate(Int32 size);
	void Release(void* pPtr);

private:
	void AddPool(Int32 blockSize);
	
	std::vector<MemoryPool*> pools_;
	MemoryPool* poolTable_[MAX_ALLOC_SIZE + 1] = {nullptr};
};

/*
		[xnew / xdelete]
		new/delete 대신 쓰는 생성/해제 함수.
		"메모리 할당"과 "생성자 호출"을 분리해서, 할당기를 이 파일 한 곳에서만 바꿀 수 있게 한다.
*/
template<typename Type, typename... Args>
Type* xnew(Args&&... args)
{
	// 할당기들은 16바이트 정렬을 준다 (x64 malloc, StompAllocator). 확실히 만족하는 max_align_t(MSVC는 8) 이하 정렬만 허용하고, 그보다 큰 정렬을 요구하는 타입은 컴파일 단계에서 막는다
	STATIC_ASSERT(alignof(Type) <= alignof(std::max_align_t), "over-aligned type is not supported");

	// ① 메모리만 확보
	Type* pMemory = static_cast<Type*>(DefaultAllocator::Alloc(static_cast<Int32>(sizeof(Type))));

	// ② 확보한 주소 위에서 생성자만 호출 (placement new). 인자는 받은 그대로 전달한다
	new(pMemory) Type(std::forward<Args>(args)...);
	return pMemory;
}

template<typename Type>
void xdelete(Type* pObj)
{
	// delete nullptr과 같이 아무 일도 하지 않는다
	if (pObj == nullptr)
		return;

	// ① 소멸자를 직접 호출  ② 메모리 반납
	pObj->~Type();
	DefaultAllocator::Release(pObj);
}

/*
		[StlAllocator]
		STL 컨테이너(vector, map ...)가 내부 메모리를 DefaultAllocator에서 받게 하는 어댑터.
		  예) std::vector<Int32, StlAllocator<Int32>>
		- allocate(count)는 "Type count개"를 담을 메모리만 준다. 생성자/소멸자는 컨테이너가 부른다.
		- 컨테이너는 받은 할당기로 다른 타입용 할당기(map의 트리 노드 등)를 만들어 쓴다 → 변환 생성자 (rebind)
		- 멤버 변수가 없으므로 모든 StlAllocator는 서로 같다 → 어느 것으로 받아도 어느 것으로든 해제할 수 있다.
		- xnew와 같이 CoreGlobal이 살아 있는 동안에만 쓴다 (Release는 GMemory를 쓴다).
*/
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
bool operator==(const StlAllocator<T>&, const StlAllocator<U>&)
{
	return true;
}

template<typename T, typename U>
bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&)
{
	return false;
}

/*
		[MakeShared]
		std::make_shared 대신 쓰는 SharedPtr 생성 함수.
		- std::allocate_shared + StlAllocator로 컨트롤 블록(참조 카운트)과 객체를 한 덩어리로, 한 번에 DefaultAllocator에서 받는다.
		  (StlAllocator<Type>은 "컨트롤 블록 + Type" 타입용 할당기로 rebind되어 쓰인다)
		- 강한 참조가 0이 되면 소멸자가 불리고, weak_ptr까지 0이 되어야 메모리가 반납된다 (한 덩어리라 따로 돌려줄 수 없다).
		- 컨트롤 블록이 실제 타입(Type)을 기억하므로, 부모 타입 SharedPtr로 옮겨 담아도 Type의 소멸자가 불린다.
		- 표준 라이브러리 안에서 생성자를 부르므로 Type의 생성자는 public이어야 한다.
		- xnew와 같이 CoreGlobal이 살아 있는 동안에만 쓴다 (Release는 GMemory를 쓴다).
*/
template<typename Type, typename... Args>
SharedPtr<Type> MakeShared(Args&&... args)
{
	return std::allocate_shared<Type>(StlAllocator<Type>(), std::forward<Args>(args)...);
}
