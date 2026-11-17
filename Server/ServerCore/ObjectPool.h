#pragma once
#include <new>
#include <utility>
#include "Types.h"
#include "CoreMacro.h"
#include "Allocator.h"

/*
				[ObjectPool]
				타입 하나만 담는 전용 풀. ObjectPool<Knight>, ObjectPool<Monster>는 서로 다른 풀을 쓴다.
				- Pop(args...): 블록을 꺼내 그 위에 생성자를 호출한다 (placement new)
				- Push(pObj)  : 소멸자를 호출하고 블록을 풀에 돌려준다
				- 블록 크기가 sizeof(Type)에 딱 맞고, 타입별 사용 중 개수(GetUseCount)를 볼 수 있다
				- Debug는 StompAllocator를 쓴다 (해제 후 접근, 오버플로를 그 줄에서 크래시로 잡는다). Release만 풀을 쓴다

				[ObjectPoolBase를 따로 둔 이유]
				템플릿은 헤더에 전부 있어야 해서, ObjectPool<Type>이 MemoryPool을 직접 다루면
				MemoryPool.h(Windows.h)가 이 헤더를 include하는 모든 곳(GameServer, 클라이언트 엔진)으로 퍼진다.
				그래서 템플릿이 아닌 ObjectPoolBase가 .cpp 안에서만 MemoryPool을 다루고,
				헤더에는 전방 선언과 포인터(pPool_)만 남긴다.

				⚠️ Pop으로 받은 객체는 반드시 Push로 돌려준다. delete, xdelete로 지우면 받은 곳과 다른 곳에 반납된다.
*/
class MemoryPool;
class ObjectPoolBase
{
public:
	explicit ObjectPoolBase(Int32 dataSize);
	~ObjectPoolBase();

	ObjectPoolBase(const ObjectPoolBase&) = delete;
	ObjectPoolBase& operator=(const ObjectPoolBase&) = delete;

	void* Alloc();
	void Release(void* pPtr);

	Int32 GetUseCount() const;

private:
	Int32 blockSize_;
	MemoryPool* pPool_;
};

template<typename Type>
class ObjectPool
{
public:
	// 블록의 데이터 주소는 16바이트 정렬이다 (StompAllocator, MemoryPool 모두). 그보다 큰 정렬은 받지 않는다
	STATIC_ASSERT(alignof(Type) <= 16, "ObjectPool supports alignment up to 16 bytes");

	template<typename... Args>
	static Type* Pop(Args&&... args)
	{
		// ① 메모리만 확보
#ifdef _DEBUG
		void* pMemory = StompAllocator::Alloc(static_cast<Int32>(sizeof(Type)));
#else
		void* pMemory = GetBase().Alloc();
#endif

		// ② 확보한 주소 위에서 생성자만 호출 (placement new). 인자는 받은 그대로 전달한다
		Type* pObj = new(pMemory) Type(std::forward<Args>(args)...);
		return pObj;
	}

	static void Push(Type* pObj)
	{
		// delete nullptr과 같이 아무 일도 하지 않는다
		if (pObj == nullptr)
			return;

		// ① 소멸자를 직접 호출
		pObj->~Type();

		// ② Pop에서 받은 곳으로 메모리를 돌려준다
#ifdef _DEBUG
		StompAllocator::Release(pObj);
#else
		GetBase().Release(pObj);
#endif
	}

	// 지금 밖에서 사용 중인 객체 수. Debug(Stomp)는 세지 않으므로 항상 0이다
	static Int32 GetUseCount()
	{
#ifdef _DEBUG
		return 0;
#else
		return GetBase().GetUseCount();
#endif
	}

private:
	// 타입마다 풀이 하나씩 생긴다 (ObjectPool<Knight>, ObjectPool<Monster>는 서로 다른 SBase)
	// 처음 호출될 때 한 번만 만들어지고, C++11부터 이 초기화는 스레드 안전하다
	static ObjectPoolBase& GetBase()
	{
		static ObjectPoolBase SBase(static_cast<Int32>(sizeof(Type)));
		return SBase;
	}
};
