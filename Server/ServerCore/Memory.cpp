#include "pch.h"
#include "MemoryPool.h"
#include "Memory.h"
#include <malloc.h>      // _aligned_malloc, _aligned_free

Memory::Memory()
{
	pools_.reserve(48);
	for (int32 blockSize = 32; blockSize <= 1024; blockSize+=32)
		AddPool(blockSize);

	for (int32 blockSize = 1152; blockSize <= 2048; blockSize += 128)
		AddPool(blockSize);

	for (int32 blockSize = 2304; blockSize <= 4096; blockSize += 256)
		AddPool(blockSize);

	// 마지막 풀이 MAX_ALLOC_SIZE여야 poolTable_의 끝까지 빈칸 없이 채워진다
	ASSERT_CRASH(pools_.back()->GetAllocSize() == MAX_ALLOC_SIZE);
}

Memory::~Memory()
{
	for (MemoryPool* pPool : pools_)
		delete pPool;
}

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

void Memory::AddPool(int32 blockSize)
{
	int32 startIndex = 1;
	if (!pools_.empty())
		startIndex = pools_.back()->GetAllocSize()+1;

	MemoryPool* pPool = new MemoryPool(blockSize);
	pools_.push_back(pPool);

	for (int32 index = startIndex; index <= blockSize; ++index)
		poolTable_[index] = pPool;
}
