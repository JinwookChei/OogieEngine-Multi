#include "pch.h"
#include "CoreGlobal.h"
#include "ThreadManager.h"

ThreadManager* GThreadManager = nullptr;

CoreGlobal::CoreGlobal()
{
	// CoreGlobal을 두 번 만드는 실수 차단
	ASSERT_CRASH(GThreadManager == nullptr);
	GThreadManager = new ThreadManager();
}

CoreGlobal::~CoreGlobal()
{
	delete GThreadManager;
	GThreadManager = nullptr;
}
