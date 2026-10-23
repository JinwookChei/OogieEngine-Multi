#pragma once
#include <typeinfo>
#include "Types.h"

/*
┌────────────────────────┬────────────────────────┬──────────────────────────────────┐
│       지금 상태         │     ReadLock 요청      │          WriteLock 요청          │
├────────────────────────┼────────────────────────┼──────────────────────────────────┤
│ 비어 있음               │ ✅                    │ ✅                               │
├────────────────────────┼────────────────────────┼──────────────────────────────────┤
│ 다른 스레드가 Read 중    │ ✅(카운트 + 1)         │ ❌ 대기                         │
├────────────────────────┼────────────────────────┼──────────────────────────────────┤
│ 다른 스레드가 Write 중   │ ❌ 대기                │ ❌ 대기                         │
├────────────────────────┼────────────────────────┼──────────────────────────────────┤
│ 내가 Write 중           │ ✅(Write → Read 허용)   │ ✅(재진입)                      │
├────────────────────────┼────────────────────────┼──────────────────────────────────┤
│ 내가 Read 중            │ ✅                     │ ❌ 영원히 대기 → 타임아웃 크래시	 │
└────────────────────────┴────────────────────────┴──────────────────────────────────┘
*/

class Lock
{
	enum : uint32
	{
		ACQUIRE_TIMEOUT_MS = 10'000,        // 이 시간 동안 못 잡으면 데드락으로 보고 크래시
		MAX_SPIN_COUNT = 5'000,				// yield 하기 전 CAS 시도 횟수
		WRITE_THREAD_MASK = 0xFFFF'0000,
		READ_COUNT_MASK = 0x0000'FFFF,
		EMPTY_FLAG = 0x0000'0000,
	};

public:
	void WriteLock(const char* name);
	void WriteUnlock(const char* name);

	void ReadLock(const char* name);
	void ReadUnlock(const char* name);

private:
	// [WWWWWWWWWWWWWWWW][RRRRRRRRRRRRRRRR]
	// W : 쓰고 있는 Thread의 Id
	// R : 읽고 있는 Thread의 갯수.
	Atomic<uint32> lockFlag_{ EMPTY_FLAG };
	uint16 writeCount_ = 0;
};



/*
		[LockGuard]
		생성자에서 Lock, 소멸자에서 Unlock → 스코프를 벗어나는 모든 경로(return, 예외)에서 해제가 보장된다.
		Lock/Unlock이 같은 스레드, 같은 name으로 짝지어진다.
*/
class ReadLockGuard
{
public:
	ReadLockGuard(Lock& lock, const char* name) : lock_(lock), name_(name) { lock_.ReadLock(name_); }
	~ReadLockGuard() { lock_.ReadUnlock(name_); }

	// 복사되면 Unlock이 두 번 호출된다
	ReadLockGuard(const ReadLockGuard&) = delete;
	ReadLockGuard& operator=(const ReadLockGuard&) = delete;

private:
	Lock& lock_;
	const char* name_;
};

class WriteLockGuard
{
public:
	WriteLockGuard(Lock& lock, const char* name) : lock_(lock), name_(name) { lock_.WriteLock(name_); }
	~WriteLockGuard() { lock_.WriteUnlock(name_); }

	WriteLockGuard(const WriteLockGuard&) = delete;
	WriteLockGuard& operator=(const WriteLockGuard&) = delete;

private:
	Lock& lock_;
	const char* name_;
};



/*
		[Lock 매크로]
		클래스 멤버로 USE_LOCK을 선언하고, 멤버 함수 안에서 READ_LOCK / WRITE_LOCK을 쓴다.
		name은 typeid(this).name()으로 클래스 이름이 자동으로 들어간다.
		⚠️ 같은 스코프에서 READ_LOCK 다음에 WRITE_LOCK을 쓰면 Read → Write 셀프 데드락 (타임아웃 크래시)
*/
#define USE_MANY_LOCKS(count)   Lock locks_[count];
#define USE_LOCK                USE_MANY_LOCKS(1)
#define READ_LOCK_IDX(idx)      ReadLockGuard readLockGuard_##idx(locks_[idx], typeid(this).name());
#define READ_LOCK               READ_LOCK_IDX(0)
#define WRITE_LOCK_IDX(idx)     WriteLockGuard writeLockGuard_##idx(locks_[idx], typeid(this).name());
#define WRITE_LOCK              WRITE_LOCK_IDX(0)


