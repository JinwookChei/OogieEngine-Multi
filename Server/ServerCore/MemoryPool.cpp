#include "pch.h"
#include "MemoryPool.h"
#include <malloc.h>      // _aligned_malloc, _aligned_free

MemoryPool::MemoryPool(int32 allocSize) : allocSize_(allocSize)
{
	// 블록 안에 최소한 헤더는 들어가야 한다
	ASSERT_CRASH(allocSize >= static_cast<int32>(sizeof(MemoryHeader)));
	::InitializeSListHead(&header_);
}

MemoryPool::~MemoryPool()
{
	// 풀에서 쉬고 있는 블록만 해제한다 (사용 중인 블록은 Push되기 전까지 풀이 알 수 없다)
	while (MemoryHeader* pMemory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&header_)))
		::_aligned_free(pMemory);
}

void MemoryPool::Push(MemoryHeader* pPtr)
{
	// 사용 중인 블록은 allocSize_ > 0이다. 0이면 이미 풀에 있는 블록을 또 넣는 것(이중 해제)
	ASSERT_CRASH(pPtr->allocSize_ > 0);

	// "풀 안에 있음" 표시
	pPtr->allocSize_ = 0;

	::InterlockedPushEntrySList(&header_, static_cast<PSLIST_ENTRY>(pPtr));

	useCount_.fetch_sub(1);
	reserveCount_.fetch_add(1);
}

MemoryHeader* MemoryPool::Pop()
{
	MemoryHeader* pMemory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&header_));

	if (pMemory == nullptr)
	{
		// 풀이 비었으면 새로 만든다. SLIST 노드가 되므로 16바이트 정렬로 받는다
		pMemory = static_cast<MemoryHeader*>(::_aligned_malloc(allocSize_, SLIST_ALIGNMENT));
		ASSERT_CRASH(pMemory != nullptr);
	}
	else
	{
		// 풀에서 꺼낸 블록은 Push에서 0으로 표시해 둔 상태여야 한다
		ASSERT_CRASH(pMemory->allocSize_ == 0);
		reserveCount_.fetch_sub(1);
	}

	useCount_.fetch_add(1);
	return pMemory;
}
