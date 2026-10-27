#include "pch.h"
#include <thread>
#include <vector>
#include "ThreadManager.h"
#include "Lock.h"
#include "Memory.h"
#include "MemoryPool.h"
#include "Test.h"


void ThreadRace()
{
	int threadNum = 4;
	int repeatCnt = 1000000;

	int32 v1 = 0;
	int32 v2 = 0;
	Mutex m;

	for (int i = 0; i < threadNum; ++i)
	{
		GThreadManager->Launch([&]() 
			{
				for(int j = 0; j < repeatCnt; ++j)
				{ 
					LockGuard lg(m);
					v1 += 1;
					v2 += 1;
				}
			});
	}
	GThreadManager->Join();
	
	std::cout << v1 << " " << v2 << std::endl;
}

void WriteLockCounter()
{
	Lock lck1;
	lck1.WriteLock("Hello1");
	lck1.WriteLock("Hello2");


	lck1.WriteUnlock("Bye");
	lck1.WriteUnlock("Bye");
}


/*
	[Lock / DeadLockProfiler 테스트]
*/
void LockReentrancy()
{
	Lock lock;

	// Lock과 Unlock에는 같은 이름을 넘긴다 (Profiler가 이름으로 락을 구분한다)
	lock.WriteLock("Reentrancy");
	lock.WriteLock("Reentrancy");     // Write 재진입 → writeCount_ = 2
	lock.ReadLock("Reentrancy");      // Write → Read 허용

	// 잡은 역순으로 해제한다
	lock.ReadUnlock("Reentrancy");
	lock.WriteUnlock("Reentrancy");
	lock.WriteUnlock("Reentrancy");   // writeCount_ = 0 → EMPTY

	// 완전히 풀렸다면 다른 스레드가 바로 잡을 수 있어야 한다 (안 풀렸으면 10초 뒤 LOCK_TIMEOUT)
	GThreadManager->Launch([&lock]()
		{
			lock.WriteLock("Reentrancy");
			lock.WriteUnlock("Reentrancy");
		});
	GThreadManager->Join();

	std::cout << "[LockReentrancy] OK" << std::endl;
}


namespace
{
	// 1, 2, 3, ... 을 순서대로 Push한다. 락이 제대로 동작하면
	// 어느 순간에 읽어도 sum == size * (size + 1) / 2 가 성립해야 한다.
	class SharedNumbers
	{
	public:
		void Push(int32 value)
		{
			WRITE_LOCK;
			numbers_.push_back(value);
		}

		// size와 sum을 같은 READ_LOCK 안에서 읽어야 서로 맞는 값이 된다
		void Snapshot(OUT int64& size, OUT int64& sum)
		{
			READ_LOCK;
			size = static_cast<int64>(numbers_.size());
			sum = 0;
			for (int32 value : numbers_)
				sum += value;
		}

	private:
		USE_LOCK;
		std::vector<int32> numbers_;
	};
}

void LockContention()
{
	constexpr int32 PUSH_COUNT = 10'000;
	constexpr int32 READER_COUNT = 3;
	constexpr int32 READ_COUNT = 1'000;

	SharedNumbers shared;
	Atomic<int32> badReadCount{ 0 };

	// Writer 1개
	GThreadManager->Launch([&shared]()
		{
			for (int32 i = 1; i <= PUSH_COUNT; ++i)
				shared.Push(i);
		});

	// Reader 3개
	for (int32 r = 0; r < READER_COUNT; ++r)
	{
		GThreadManager->Launch([&shared, &badReadCount]()
			{
				for (int32 i = 0; i < READ_COUNT; ++i)
				{
					int64 size = 0;
					int64 sum = 0;
					shared.Snapshot(OUT size, OUT sum);

					if (sum != size * (size + 1) / 2)
						badReadCount.fetch_add(1);
				}
			});
	}

	GThreadManager->Join();

	int64 finalSize = 0;
	int64 finalSum = 0;
	shared.Snapshot(OUT finalSize, OUT finalSum);

	std::cout << "[LockContention] size : " << finalSize << " (expected " << PUSH_COUNT << ")" << std::endl;
	std::cout << "[LockContention] bad reads : " << badReadCount.load() << " (expected 0)" << std::endl;

	ASSERT_CRASH(finalSize == PUSH_COUNT);
	ASSERT_CRASH(badReadCount.load() == 0);

	std::cout << "[LockContention] OK" << std::endl;
}

void NoCycleLock()
{
	Lock lockX;
	Lock lockY;
	Lock lockZ;
	Lock lockC;
	Lock lockD;

	// ◇ 모양: X → Y, X → Z, Z → Y
	// 갈라졌다 다시 만나지만 되돌아오는 길은 없다 → Visited 노드에 도착하는 경로를 검증
	GThreadManager->Launch([&lockX, &lockY, &lockZ]()
		{
			lockX.WriteLock("NoCycle_X");
			lockY.WriteLock("NoCycle_Y");     // 간선 X → Y
			lockY.WriteUnlock("NoCycle_Y");

			lockZ.WriteLock("NoCycle_Z");     // 간선 X → Z
			lockY.WriteLock("NoCycle_Y");     // 스택 top이 Z이므로 간선 Z → Y
			lockY.WriteUnlock("NoCycle_Y");
			lockZ.WriteUnlock("NoCycle_Z");

			lockX.WriteUnlock("NoCycle_X");
		});
	GThreadManager->Join();

	// 서로 연결되지 않은 덩어리: C → D (모든 노드를 시작점 후보로 순회하는지 검증)
	GThreadManager->Launch([&lockC, &lockD]()
		{
			lockC.WriteLock("NoCycle_C");
			lockD.WriteLock("NoCycle_D");     // 간선 C → D
			lockD.WriteUnlock("NoCycle_D");
			lockC.WriteUnlock("NoCycle_C");
		});
	GThreadManager->Join();

	std::cout << "[NoCycleLock] OK" << std::endl;
}


/*
	[크래시 테스트] 한 번에 하나만 호출한다
*/
void CrashInvalidUnlockOrder()
{
	Lock lockA;
	Lock lockB;

	lockA.WriteLock("A");
	lockB.WriteLock("B");

	// B를 먼저 풀어야 하는데 A를 먼저 푼다 → Profiler: INVALID_UNLOCK_ORDER
	lockA.WriteUnlock("A");

	std::cout << "[CrashInvalidUnlockOrder] FAIL : 크래시가 나지 않았다" << std::endl;
}

void CrashUnknownLock()
{
	Lock lock;

	lock.WriteLock("Known");

	// 한 번도 Lock에 쓰인 적 없는 이름 → Profiler: UNKNOWN_LOCK
	lock.WriteUnlock("NeverLocked");

	std::cout << "[CrashUnknownLock] FAIL : 크래시가 나지 않았다" << std::endl;
}

void CrashThreadIdZero()
{
	Lock lock;

	// GThreadManager가 아닌 std::thread로 직접 만든 스레드는 InitTLS를 거치지 않는다 → LThreadId == 0
	std::thread t([&lock]()
		{
			lock.WriteLock("ThreadIdZero");   // ASSERT_CRASH(LThreadId != 0 ...)
			lock.WriteUnlock("ThreadIdZero");
		});
	t.join();

	std::cout << "[CrashThreadIdZero] FAIL : 크래시가 나지 않았다" << std::endl;
}

void CrashDeadLockCycle()
{
	Lock lockP;
	Lock lockQ;
	Lock lockR;

	// 스레드 1: P → Q
	GThreadManager->Launch([&lockP, &lockQ]()
		{
			lockP.WriteLock("Cycle_P");
			lockQ.WriteLock("Cycle_Q");       // 간선 P → Q
			lockQ.WriteUnlock("Cycle_Q");
			lockP.WriteUnlock("Cycle_P");
		});

	// 스레드 1이 락을 모두 푼 뒤에 시작한다 (실행 시간이 겹치지 않아도 검출되는지 확인)
	GThreadManager->Join();

	// 스레드 2: Q → R → P
	GThreadManager->Launch([&lockP, &lockQ, &lockR]()
		{
			lockQ.WriteLock("Cycle_Q");
			lockR.WriteLock("Cycle_R");       // 간선 Q → R
			lockP.WriteLock("Cycle_P");       // 간선 R → P → 사이클 P → Q → R → P → DEADLOCK_DETECTED
			lockP.WriteUnlock("Cycle_P");
			lockR.WriteUnlock("Cycle_R");
			lockQ.WriteUnlock("Cycle_Q");
		});
	GThreadManager->Join();

	std::cout << "[CrashDeadLockCycle] FAIL : 크래시가 나지 않았다" << std::endl;
}


class MemoryTestKnight
{
public:
	MemoryTestKnight(int32 hp, int32 attack) : hp_(hp), attack_(attack) 
	{
		++SConstructCount;
	}
	~MemoryTestKnight()
	{
		++SDestructCount;
	}

	inline static int32 SConstructCount = 0;
	inline static int32 SDestructCount = 0;

	int32 hp_;
	int32 attack_;
};

void XnewXdelete()
{
	// 다른 테스트가 먼저 MemoryTestKnight를 써도 기댓값이 틀어지지 않게 초기화한다
	MemoryTestKnight::SConstructCount = 0;
	MemoryTestKnight::SDestructCount = 0;

	// 1) 생성: 생성자가 한 번 호출되고, 인자가 그대로 전달되어야 한다
	MemoryTestKnight* knight = xnew<MemoryTestKnight>(100, 10);
	if (MemoryTestKnight::SConstructCount != 1 || MemoryTestKnight::SDestructCount != 0)
	{
		std::cout << "[XnewXdelete] FAIL : xnew 후 생성자/소멸자 호출 횟수 (construct "
			<< MemoryTestKnight::SConstructCount << ", destruct " << MemoryTestKnight::SDestructCount << ")" << std::endl;
		return;
	}
	if (knight->hp_ != 100 || knight->attack_ != 10)
	{
		std::cout << "[XnewXdelete] FAIL : 생성자 인자 전달 (hp " << knight->hp_ << ", attack " << knight->attack_ << ")" << std::endl;
		return;
	}

	// 2) 해제: 소멸자가 한 번 호출되어야 한다
	xdelete(knight);
	if (MemoryTestKnight::SDestructCount != 1)
	{
		std::cout << "[XnewXdelete] FAIL : xdelete 후 소멸자 호출 횟수 (destruct " << MemoryTestKnight::SDestructCount << ")" << std::endl;
		return;
	}

	// 3) nullptr 해제: 크래시 없이 아무 일도 하지 않아야 한다 (소멸자 호출 X)
	// nullptr만 넘기면 Type을 추론할 수 없으므로 타입이 있는 포인터 변수로 넘긴다
	MemoryTestKnight* nullKnight = nullptr;
	xdelete(nullKnight);
	if (MemoryTestKnight::SDestructCount != 1)
	{
		std::cout << "[XnewXdelete] FAIL : xdelete(nullptr)가 소멸자를 호출함 (destruct " << MemoryTestKnight::SDestructCount << ")" << std::endl;
		return;
	}

	std::cout << "[XnewXdelete] OK" << std::endl;
}

void StompAllocFree()
{
	// StompAllocator의 PAGE_SIZE, ALIGNMENT는 private이라 테스트에서 같은 값을 따로 둔다
	constexpr uintptr PAGE = 4096;
	constexpr uintptr ALIGN = 16;

	// 8: 1페이지 안의 작은 객체 / 4096: 딱 1페이지 (앞쪽 빈 공간 0) / 5000: 2페이지에 걸침
	const int32 sizes[] = { 8, 4096, 5000 };

	for (int32 size : sizes)
	{
		void* ptr = StompAllocator::Alloc(size);
		const uintptr address = reinterpret_cast<uintptr>(ptr);
		const uintptr alignedSize = (static_cast<uintptr>(size) + ALIGN - 1) / ALIGN * ALIGN;

		// 1) 정렬: 반환 주소가 16의 배수
		const bool aligned = (address % ALIGN == 0);

		// 2) 페이지 끝에 붙었는지: 데이터 끝(올림한 크기 기준)이 페이지 경계와 일치
		const bool atPageEnd = ((address + alignedSize) % PAGE == 0);

		// 3) 받은 메모리 전체를 쓰고 다시 읽는다 (커밋이 안 된 바이트가 있으면 여기서 크래시)
		int8* bytes = static_cast<int8*>(ptr);
		for (int32 i = 0; i < size; ++i)
			bytes[i] = static_cast<int8>(i);

		bool dataOk = true;
		for (int32 i = 0; i < size; ++i)
		{
			if (bytes[i] != static_cast<int8>(i))
				dataOk = false;
		}

		// 4) 해제: VirtualFree가 실패하면 Release 안의 ASSERT_CRASH에서 멈춘다
		StompAllocator::Release(ptr);

		if (aligned == false || atPageEnd == false || dataOk == false)
		{
			std::cout << "[StompAllocFree] FAIL : size " << size << " (address 0x" << std::hex << address << std::dec
				<< ", aligned " << aligned << ", atPageEnd " << atPageEnd << ", dataOk " << dataOk << ")" << std::endl;
			return;
		}
	}

	// 5) nullptr 해제: 아무 일도 하지 않아야 한다 (free(nullptr)과 같은 규칙)
	StompAllocator::Release(nullptr);

	std::cout << "[StompAllocFree] OK" << std::endl;
}


/*
	[크래시 테스트] 한 번에 하나만 호출한다
*/
void CrashStompUseAfterFree()
{
	MemoryTestKnight* knight = xnew<MemoryTestKnight>(100, 10);
	xdelete(knight);

	// 해제된 객체에 쓴다 → Debug(StompAllocator): 페이지가 OS에 반납되어 이 줄에서 Access Violation
	knight->hp_ = 50;

	std::cout << "[CrashStompUseAfterFree] FAIL : 크래시가 나지 않았다 (Release는 BaseAllocator라 정상)" << std::endl;
}

void CrashStompOverflow()
{
	// 데이터 끝이 정확히 페이지 끝에 오는 크기를 쓴다
	constexpr int32 SIZE = 16;

	// xnew를 거치지 않고 StompAllocator를 직접 쓰므로 Release 빌드에서도 크래시가 나야 한다
	int8* bytes = static_cast<int8*>(StompAllocator::Alloc(SIZE));

	bytes[SIZE - 1] = 1;   // 마지막 바이트: 정상
	bytes[SIZE] = 1;       // 한 바이트 넘어감 → 할당되지 않은 주소 → 이 줄에서 Access Violation

	std::cout << "[CrashStompOverflow] FAIL : 크래시가 나지 않았다" << std::endl;
	StompAllocator::Release(bytes);
}


/*
	[Memory 테스트] MemoryHeader
*/
void MemoryHeaderAttachDetach()
{
	constexpr int32 DATA_SIZE = 64;
	const int32 headerSize = static_cast<int32>(sizeof(MemoryHeader));

	// 블록 = [MemoryHeader][Data 64B]. malloc은 16바이트 정렬을 주므로 SLIST 조건을 만족한다
	void* memory = BaseAllocator::Alloc(headerSize + DATA_SIZE);
	MemoryHeader* block = static_cast<MemoryHeader*>(memory);
	void* data = MemoryHeader::AttachHeader(block, DATA_SIZE);

	// 1) 데이터 주소 = 블록 시작 + 헤더 크기 (바이트 차이는 int8*로 빼서 구한다)
	const int32 offset = static_cast<int32>(static_cast<int8*>(data) - static_cast<int8*>(memory));
	const bool offsetOk = (offset == headerSize);

	// 2) 데이터 주소도 16바이트 정렬
	const bool aligned = (reinterpret_cast<uintptr>(data) % SLIST_ALIGNMENT == 0);

	// 3) 데이터 영역 전체에 써도 헤더가 그대로인지 (데이터가 헤더를 덮지 않는다)
	int8* bytes = static_cast<int8*>(data);
	for (int32 i = 0; i < DATA_SIZE; ++i)
		bytes[i] = static_cast<int8>(0x7F);
	const bool headerIntact = (block->allocSize_ == DATA_SIZE);

	// 4) 데이터 주소에서 되찾은 헤더 = 블록 시작
	const bool detachOk = (MemoryHeader::DetachHeader(data) == block);

	// 5) 해제는 데이터 주소가 아니라 블록 시작으로 한다 (malloc이 돌려준 주소)
	BaseAllocator::Release(memory);

	if (offsetOk == false || aligned == false || headerIntact == false || detachOk == false)
	{
		std::cout << "[MemoryHeaderAttachDetach] FAIL : offset " << offset << " (expected " << headerSize << ")"
			<< ", aligned " << aligned << ", headerIntact " << headerIntact << ", detachOk " << detachOk << std::endl;
		return;
	}

	std::cout << "[MemoryHeaderAttachDetach] OK (sizeof(MemoryHeader) = " << headerSize << ")" << std::endl;
}