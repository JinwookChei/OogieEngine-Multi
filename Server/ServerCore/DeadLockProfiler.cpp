#include "pch.h"
#include "CoreTLS.h"
#include "DeadLockProfiler.h"

void DeadLockProfiler::PushLock(const char* name)
{
	LockGuard guard(lock_);

	const int32 lockId = GetOrCreateLockId(name);

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

void DeadLockProfiler::PopLock(const char* name)
{
	LockGuard guard(lock_);

	if (LLockStack.empty())
		CRASH("MULTIPLE_UNLOCK");

	auto findIt = nameToId_.find(name);
	if (findIt == nameToId_.end())
		CRASH("UNKNOWN_LOCK");

	// 마지막에 잡은 락부터 풀어야 한다
	if (LLockStack.top() != findIt->second)
		CRASH("INVALID_UNLOCK_ORDER");

	LLockStack.pop();
}

int32 DeadLockProfiler::GetOrCreateLockId(const char* name)
{
	auto findIt = nameToId_.find(name);
	if (findIt != nameToId_.end())
		return findIt->second;

	const int32 lockId = static_cast<int32>(idToName_.size());
	nameToId_.emplace(name, lockId);
	idToName_.push_back(name);
	return lockId;
}

void DeadLockProfiler::CheckCycle()
{
}
