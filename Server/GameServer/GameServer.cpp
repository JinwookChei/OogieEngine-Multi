#include "pch.h"
#include "ThreadManager.h"
#include "Test.h"


int main(int argc, char* argv[])
{
	CoreGlobal coreGlobal;
	std::cout << "GameServer Start" << std::endl;

	// ThreadRace();
	// WriteLockCounter();

	LockReentrancy();
	LockContention();
	NoCycleLock();

	// 크래시 테스트: 하나씩 주석을 풀고 실행 → 크래시 확인 후 다시 주석 처리
	// CrashInvalidUnlockOrder();
	// CrashUnknownLock();
	// CrashThreadIdZero();
	// CrashDeadLockCycle();

	std::cout << "GameServer End" << std::endl;
}
