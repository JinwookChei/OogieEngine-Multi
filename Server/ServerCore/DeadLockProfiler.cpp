#include "pch.h"
#include "CoreTLS.h"
#include "DeadLockProfiler.h"
#include <cstdio>

void DeadLockProfiler::PushLock(const char* pName)
{
	LockGuard guard(lock_);

	const int32 lockId = GetOrCreateLockId(pName);

	// 이미 잡고 있는 락이 있으면 "직전 락 → 새 락" 간선을 기록한다
	if (LLockStack.empty() == false)
	{
		const int32 prevId = LLockStack.top();

		// 같은 락 재진입은 순서가 아니다
		if (prevId != lockId)
		{
			std::set<int32>& history = lockHistory_[prevId];

			// 처음 보는 순서일 때만 사이클 검사 (insert().second == 새로 들어갔는지)
			if (history.insert(lockId).second)
				CheckCycle();
		}
	}

	LLockStack.push(lockId);
}

void DeadLockProfiler::PopLock(const char* pName)
{
	LockGuard guard(lock_);

	if (LLockStack.empty())
		CRASH("MULTIPLE_UNLOCK");

	auto findIt = nameToId_.find(pName);
	if (findIt == nameToId_.end())
		CRASH("UNKNOWN_LOCK");

	// 마지막에 잡은 락부터 풀어야 한다
	if (LLockStack.top() != findIt->second)
		CRASH("INVALID_UNLOCK_ORDER");

	LLockStack.pop();
}

int32 DeadLockProfiler::GetOrCreateLockId(const char* pName)
{
	auto findIt = nameToId_.find(pName);
	if (findIt != nameToId_.end())
		return findIt->second;

	const int32 lockId = static_cast<int32>(idToName_.size());
	nameToId_.emplace(pName, lockId);
	idToName_.push_back(pName);
	return lockId;
}

void DeadLockProfiler::CheckCycle()
{
	// 락 id는 0부터 빈틈없이 부여되므로 노드 수 == 이름 수
	const int32 lockCount = static_cast<int32>(idToName_.size());
	visitStates_.assign(lockCount, VisitState::NotVisited);

	// 그래프가 여러 조각일 수 있으므로 모든 노드를 시작점 후보로 본다.
	// 이미 조사한 노드는 상태를 유지한 채 건너뛴다 (전체 비용 = 노드 수 + 간선 수)
	for (int32 lockId = 0; lockId < lockCount; ++lockId)
	{
		if (visitStates_[lockId] == VisitState::NotVisited)
			Dfs(lockId);
	}

	visitStates_.clear();
}

void DeadLockProfiler::Dfs(int32 cur)
{
	visitStates_[cur] = VisitState::Visiting;

	// 나가는 간선이 없는 노드는 lockHistory_에 키가 없다 (operator[]는 빈 set을 삽입하므로 find)
	auto findIt = lockHistory_.find(cur);
	if (findIt != lockHistory_.end())
	{
		for (const int32 nxt : findIt->second)
		{
			switch (visitStates_[nxt])
			{
			case VisitState::NotVisited:
				Dfs(nxt);
				break;

			case VisitState::Visiting:
				// 현재 탐색 경로 위의 노드로 되돌아옴 = 사이클
				printf("DEADLOCK: %s -> %s\n", idToName_[cur].c_str(), idToName_[nxt].c_str());
				fflush(stdout);		// 바로 크래시하므로 버퍼에 남은 출력이 사라지지 않게 비운다
				CRASH("DEADLOCK_DETECTED");
				break;

			case VisitState::Visited:
				// 이미 조사가 끝난 노드, 사이클 아님
				break;
			}
		}
	}

	// 자식 재귀가 모두 반환된 시점 = 이 노드의 탐색 완료
	visitStates_[cur] = VisitState::Visited;
}
