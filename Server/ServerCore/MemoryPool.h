#pragma once
#include <new>
#include <Windows.h>      // SLIST_ENTRY, MEMORY_ALLOCATION_ALIGNMENT
#include "Types.h"
#include "CoreMacro.h"

/*
				MemoryPool.h는 ServerCore 내부 전용이다 (Memory.cpp, MemoryPool.cpp에서만 include).
				공개 헤더(Memory.h)에서 include하지 않아서 Windows.h가 바깥(GameServer, 클라이언트 엔진)으로 퍼지지 않게 한다.
*/

// SLIST 노드는 16바이트 경계에 있어야 한다 (x64 Interlocked 16바이트 비교교환 요구사항)
constexpr int32 SLIST_ALIGNMENT = MEMORY_ALLOCATION_ALIGNMENT;

/*
				[MemoryHeader]
				풀이 나눠 주는 블록의 맨 앞에 붙는 머리말.
				  [MemoryHeader][Data ...]
				  ^ 블록 시작    ^ 사용자에게 돌려주는 주소
				- 사용 중  : allocSize_에 요청 크기를 적어 둔다 → 해제할 때 포인터만 받아도 어느 풀로 돌려보낼지 안다
				- 풀 안에서: SLIST_ENTRY의 Next로 다음 빈 블록을 가리킨다 → 노드를 따로 만들지 않는 연결 리스트
*/
struct alignas(SLIST_ALIGNMENT) MemoryHeader : public SLIST_ENTRY
{
	explicit MemoryHeader(int32 size) : allocSize_(size) {}

	// 블록 시작 주소에 헤더를 만들고, 헤더 바로 뒤(데이터 시작) 주소를 돌려준다
	static void* AttachHeader(MemoryHeader* pHeader, int32 size)
	{
		new(pHeader) MemoryHeader(size);   // 메모리는 이미 있으므로 생성자만 호출 (placement new)
		return pHeader + 1;                // MemoryHeader 1개(= sizeof(MemoryHeader) 바이트) 뒤
	}

	// 데이터 주소에서 MemoryHeader 1개만큼 앞으로 돌아가 헤더를 되찾는다
	static MemoryHeader* DetachHeader(void* pPtr)
	{
		return static_cast<MemoryHeader*>(pPtr) - 1;
	}

	int32 allocSize_ = 0;
};

// 데이터 시작 주소(pHeader + 1)도 16바이트 정렬이 되려면 헤더 크기가 16의 배수여야 한다
STATIC_ASSERT(sizeof(MemoryHeader) % SLIST_ALIGNMENT == 0, "MemoryHeader size must be a multiple of SLIST_ALIGNMENT");


/*
				[MemoryPool]
				같은 크기(allocSize_)의 블록만 모아 두는 lock-free 풀.
				- Pop : 풀에서 쉬는 블록을 하나 꺼낸다. 없으면 새로 할당한다
				- Push: 다 쓴 블록을 해제하지 않고 풀에 넣어 다음 Pop에서 재사용한다
				SLIST가 원자적으로 처리하므로 여러 스레드가 락 없이 동시에 Push/Pop 할 수 있다.
*/
class alignas(SLIST_ALIGNMENT) MemoryPool
{
public:
	// allocSize: 블록 하나의 전체 크기 (MemoryHeader 포함)
	explicit MemoryPool(int32 allocSize);
	~MemoryPool();

	// 사용하는 쪽에서 AttachHeader로 헤더를 붙인 블록만 받는다
	void Push(MemoryHeader* pPtr);

	// 헤더가 아직 붙지 않은 블록을 돌려준다 (사용하는 쪽에서 AttachHeader)
	MemoryHeader* Pop();

	int32 GetAllocSize() const { return allocSize_; }
	int32 GetUseCount() const { return useCount_.load(); }
	int32 GetReserveCount() const { return reserveCount_.load(); }

private:
	SLIST_HEADER header_;              // 빈 블록 리스트의 머리 (top 주소 + 순번, 16바이트)
	int32 allocSize_ = 0;
	Atomic<int32> useCount_ = 0;       // 밖에서 사용 중인 블록 수
	Atomic<int32> reserveCount_ = 0;   // 풀 안에서 쉬는 블록 수
};
