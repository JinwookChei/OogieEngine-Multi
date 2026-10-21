#include "pch.h"
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

