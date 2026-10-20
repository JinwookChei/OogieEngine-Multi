#pragma once
#include <thread>
#include <vector>
#include <functional>

class ThreadManager
{
public:
	ThreadManager();
	~ThreadManager();

	void Launch(std::function<void(void)> callback);

	void Join();

	static void InitTLS();

	static void DestroyTLS();

private:
	Mutex lock_;
	std::vector<std::thread> threads_;
};

