#pragma once
#include "Types.h"


/*
		[BaseAllocator]
		OS 힙(malloc/free)을 그대로 쓰는 가장 기본 할당기.
		메모리만 다루고 생성자/소멸자는 호출하지 않는다 (그건 xnew/xdelete가 한다).
*/
class BaseAllocator
{
public:
	static void* Alloc(int32 size);
	static void Release(void* ptr);
};

