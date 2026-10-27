#include "pch.h"
#include "Allocator.h"
#include <cstdlib>
#include <Windows.h>


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
	// 아래 올림 계산(size + PAGE_SIZE - 1 까지)이 int32를 넘지 않도록 상한도 확인한다
	ASSERT_CRASH(size > 0 && size <= INT32_MAX - PAGE_SIZE);

	// ① size를 ALIGNMENT(16)의 배수로 올린다 (값 + 단위 - 1) / 단위 * 단위
	const int32 alignedSize = (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;

	// ②③ alignedSize를 PAGE_SIZE(4096)의 배수로 올린다 = VirtualAlloc에 요청할 크기
	const int32 totalSize = (alignedSize + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;

	// ④ 데이터 끝을 페이지 끝에 붙이기 위한 앞쪽 빈 공간 (항상 PAGE_SIZE 미만)
	const int32 offset = totalSize - alignedSize;

	// ⑤ 페이지 단위로 메모리를 받는다
	void* base = ::VirtualAlloc(nullptr, totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	ASSERT_CRASH(base != nullptr);

	// ⑥ 데이터 끝이 페이지 끝에 오도록 offset만큼 뒤의 주소를 돌려준다
	return static_cast<int8*>(base) + offset;
}

void StompAllocator::Release(void* ptr)
{
	if (ptr == nullptr)
		return;

	// 4KB 경계에서 얼마나 떨어져 있는지 (= Alloc의 offset)
	const uintptr remainder = reinterpret_cast<uintptr>(ptr) % PAGE_SIZE;

	// 그만큼 앞으로 돌아가면 VirtualAlloc이 돌려준 base
	void* base = static_cast<int8*>(ptr) - remainder;

	// 할당된 크기는 OS가 기억하고 있으므로 2번째 인자 무조건 0 이어야 한다.
	BOOL res = ::VirtualFree(base, 0, MEM_RELEASE);
	ASSERT_CRASH(res);
}
