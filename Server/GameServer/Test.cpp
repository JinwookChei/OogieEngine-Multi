#include "pch.h"
#include <thread>
#include <vector>
#include "ThreadManager.h"
#include "Lock.h"
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


