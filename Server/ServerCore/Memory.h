#pragma once
#include <new>
#include <utility>
#include <cstddef>
#include "CoreMacro.h"
#include "Types.h"
#include "Allocator.h"


/*
		[xnew / xdelete]
		new/delete 대신 쓰는 생성/해제 함수.
		"메모리 할당"과 "생성자 호출"을 분리해서, 할당기를 이 파일 한 곳에서만 바꿀 수 있게 한다.
*/
template<typename Type, typename... Args>
Type* xnew(Args&&... args)
{
	// malloc은 max_align_t 단위까지만 정렬을 보장한다. 그보다 큰 정렬을 요구하는 타입은 컴파일 단계에서 막는다
	STATIC_ASSERT(alignof(Type) <= alignof(std::max_align_t), "over-aligned type is not supported");

	// ① 메모리만 확보
	Type* memory = static_cast<Type*>(BaseAllocator::Alloc(static_cast<int32>(sizeof(Type))));

	// ② 확보한 주소 위에서 생성자만 호출 (placement new). 인자는 받은 그대로 전달한다
	new(memory) Type(std::forward<Args>(args)...);
	return memory;
}

template<typename Type>
void xdelete(Type* obj)
{
	// delete nullptr과 같이 아무 일도 하지 않는다
	if (obj == nullptr)
		return;

	// ① 소멸자를 직접 호출  ② 메모리 반납
	obj->~Type();
	BaseAllocator::Release(obj);
}
