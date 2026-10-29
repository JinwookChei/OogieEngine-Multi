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
	void PushLock(const char* pName);
	void PopLock(const char* pName);

private:
	int32 GetOrCreateLockId(const char* pName);
	void CheckCycle();
	void Dfs(int32 cur);

private:
	// DFS 중 노드의 상태
	enum class VisitState : uint8
	{
		NotVisited,	// 아직 들어가지 않음
		Visiting,	// 들어갔지만 아직 나오지 않음 (현재 탐색 경로 위에 있음)
		Visited,	// 나가는 간선을 모두 조사하고 나옴 (여기서 시작하는 사이클 없음)
	};

private:
	Mutex lock_;

	std::unordered_map<std::string, int32> nameToId_;
	std::vector<std::string> idToName_;
	std::map<int32, std::set<int32>> lockHistory_;

	std::vector<VisitState> visitStates_;
};

