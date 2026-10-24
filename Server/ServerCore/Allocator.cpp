#include "pch.h"
#include "Allocator.h"
#include <cstdlib>
#include <Windows.h>
#include <cstdint>


void* BaseAllocator::Alloc(int32 size)
{
	return ::malloc(size);
}

void BaseAllocator::Release(void* ptr)
{
	::free(ptr);
}


void* StompAllocator::Alloc(int32 size)
{
	ASSERT_CRASH(size > 0);


	return ::malloc(size);
}

void StompAllocator::Release(void* ptr)
{
	::free(ptr);
}
