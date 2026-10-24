#include "pch.h"
#include <Windows.h>
#include "ThreadManager.h"
#include "Test.h"


int main(int argc, char* argv[])
{
	// exe의 문자열은 UTF-8(/utf-8)이므로 콘솔도 UTF-8로 해석하게 맞춘다 (기본값 CP949면 한글이 깨진다)
	::SetConsoleOutputCP(CP_UTF8);

	CoreGlobal coreGlobal;
	std::cout << "GameServer Start" << std::endl;

	// ThreadRace();
	// WriteLockCounter();

	// LockReentrancy();
	// LockContention();
	// NoCycleLock();
	// XnewXdelete();

	// 크래시 테스트: 하나씩 주석을 풀고 실행 → 크래시 확인 후 다시 주석 처리
	// CrashInvalidUnlockOrder();
	// CrashUnknownLock();
	// CrashThreadIdZero();
	// CrashDeadLockCycle();


	Atomic<int32> a = 0;
	a.fetch_add(1);
	a.fetch_add(2);

	std::cout << "GameServer End" << std::endl;
}
