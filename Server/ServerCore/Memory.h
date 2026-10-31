#pragma once
#include <new>
#include <vector>
#include <utility>
#include <cstddef>
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
	static constexpr int32 MAX_ALLOC_SIZE = 4096;

public:
	Memory();
	~Memory();

	Memory(const Memory&) = delete;
	Memory& operator=(const Memory&) = delete;

	void* Allocate(int32 size);
	void Release(void* pPtr);

private:
	void AddPool(int32 blockSize);
	
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
	Type* pMemory = static_cast<Type*>(DefaultAllocator::Alloc(static_cast<int32>(sizeof(Type))));

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

