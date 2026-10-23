#include "pch.h"
#include "CoreGlobal.h"
#include "ThreadManager.h"
#include "DeadLockProfiler.h"

ThreadManager* GThreadManager = nullptr;
DeadLockProfiler* GDeadLockProfiler = nullptr;

CoreGlobal::CoreGlobal()
{
	// CoreGlobal을 두 번 만드는 실수 차단
	ASSERT_CRASH(GThreadManager == nullptr);
	GThreadManager = new ThreadManager();
	GDeadLockProfiler = new DeadLockProfiler();
}

CoreGlobal::~CoreGlobal()
{
	// MEMO : Worker Thread는 Lock 잡을 때 마다 Profiler를 호출 한다. 그래서 Profiler를 먼저 지우면 아직 돌고 있는 Thread가 해제된 Profiler에 접근 한다.
	delete GThreadManager;
	GThreadManager = nullptr;

	delete GDeadLockProfiler;
	GDeadLockProfiler = nullptr;
}
