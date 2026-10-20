#include "pch.h"
#include "ThreadManager.h"

ThreadManager::ThreadManager()
{
	InitTLS();
}

ThreadManager::~ThreadManager()
{
	Join();
}

void ThreadManager::Launch(std::function<void(void)> callback)
{
	LockGuard guard(lock_);
	
	/*
		MEMO : std::thread에서 [=]캡처 쓰는 이유는 ? 
		참조로 받았을 경우 호출 스택(Launch)가 종료 될때 callback도 함께 스택에서 내려가게 되는데, 새로운 Thread에서 계속 callBack을 쓰려고 하니, 댕글링 포인터가 발생.
	*/
	threads_.push_back(std::thread([=]()
		{
			InitTLS();
			callback();
			DestroyTLS();
		}));
}

void ThreadManager::Join()
{
	for (std::thread& t : threads_)
	{
		if (t.joinable())
			t.join();
	}
	threads_.clear();
}

void ThreadManager::InitTLS()
{
	static Atomic<uint32> SThreadId = 1;
	LThreadId = SThreadId.fetch_add(1);
}

void ThreadManager::DestroyTLS()
{
}
