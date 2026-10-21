#pragma once
#include "Types.h"
#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>


/*
		[DeadLockProfiler] (Debug 전용)
		락을 잡는 순서를 방향 그래프로 기록한다. A를 잡은 채로 B를 잡으면 간선 A → B.
		새 간선이 생길 때마다 사이클을 검사한다. 사이클(A → B → A)이 있으면
		두 스레드가 반대 순서로 잡을 때 데드락이 날 수 있으므로, 실제로 터지기 전에 크래시로 알린다.
		내부는 std::mutex와 std 컨테이너만 쓴다 (Lock을 쓰면 Lock → Profiler → Lock ... 무한 재귀).
*/
class DeadLockProfiler
{
public:
	void PushLock(const char* name);
	void PopLock(const char* name);

private:
	int32 GetOrCreateLockId(const char* name);
	void CheckCycle();

private:
	Mutex lock_;

	std::unordered_map<std::string, int32> nameToId_;
	std::vector<std::string> idToName_;
	std::map<int32, std::set<int32>> lockHistory_;
};

