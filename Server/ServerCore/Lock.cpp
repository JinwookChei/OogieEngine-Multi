#include "pch.h"
#include <chrono>
#include <thread>
#include "CoreTLS.h"
#include "Lock.h"

void Lock::WriteLock(const char* name)
{
	// 0은 EMPTY_FLAG의 소유자 값과 같다 → InitTLS 안 된 스레드가 재진입으로 오인해 락을 그냥 통과한다
	ASSERT_CRASH(LThreadId != 0 && LThreadId <= 0xFFFF);

	// 1) 재진입: 이미 내가 Write 락을 갖고 있으면 횟수만 늘린다
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		++writeCount_;
		return;
	}

	// 2) 아무도 Read/Write 하지 않는 상태(EMPTY)일 때만 내 ID를 써넣는다
	const uint32 desired = (LThreadId << 16) & WRITE_THREAD_MASK;
	const auto beginTime = std::chrono::steady_clock::now();

	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			// 실패하면 CAS가 expected에 현재 값을 써넣으므로, 매번 다시 EMPTY로 초기화한다
			uint32 expected = EMPTY_FLAG;
			if (lockFlag_.compare_exchange_strong(OUT expected, desired))
			{
				++writeCount_;
				return;
			}
		}

		// 너무 오래 못 잡으면 데드락으로 판단
		if (std::chrono::steady_clock::now() - beginTime >= std::chrono::milliseconds(ACQUIRE_TIMEOUT_MS))
			CRASH("LOCK_TIMEOUT");

		// 스핀으로 못 잡았으면 CPU를 다른 스레드에 양보
		std::this_thread::yield();
	}
}

void Lock::WriteUnlock(const char* name)
{
	// 내가 잡은 락만 풀 수 있다
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	ASSERT_CRASH(lockThreadId == LThreadId);

	// Write 중에 잡은 Read 락이 남아 있으면 순서 위반 (Read를 먼저 풀어야 한다)
	if ((lockFlag_.load() & READ_COUNT_MASK) != 0)
		CRASH("INVALID_UNLOCK_ORDER");

	// 재진입한 만큼 다 풀었을 때만 락을 비운다
	if (--writeCount_ == 0)
		lockFlag_.store(EMPTY_FLAG);
}


/*
	MEMO : if (lockFlag_.compare_exchange_strong(OUT expected, expected + 1)) 에서 expected에+1 하는 이유는 뭔가요?

	+1은 "Read하는 스레드가 1명 늘었다"는 뜻입니다
	하위 16비트는 지금 Read 락을 잡고 있는 스레드 수입니다. ReadLock은 "나도 읽겠다"는 요청이므로, 이 카운트를 1 올리는 것이 곧 락을 잡는 동작입니다. compare_exchange_strong(expected, desired)의 의미를 그대로 풀어 쓰면 이렇습니다.
	"lockFlag_가 아직 expected이면(그사이 아무도 건드리지 않았으면) expected + 1로 바꿔라"

	lockFlag_ -> [WWWWWWWW][RRRRRRRR]
	-> WriteFlag : 잡고있는 ThreadId
	-> ReadFlag : Read하고 있는 Thread 갯수.


	- 숫자로 따라가 보기

	상황 A: 다른 스레드 2개가 이미 Read 중
	lockFlag_            = 0x0000'0002     (Write 소유자 0, Read 2명)
	expected             = 0x0000'0002 & 0x0000'FFFF = 0x0000'0002
	desired (expected+1) = 0x0000'0003

	CAS: 실제값(0x0000'0002) == expected(0x0000'0002) → 성공
	lockFlag_            = 0x0000'0003

	상황 B: 스레드 5번이 Write 중
	lockFlag_            = 0x0005'0000     (Write 소유자 5, Read 0)
	expected             = 0x0005'0000 & 0x0000'FFFF = 0x0000'0000   ← Write 비트를 지운 값
	desired (expected+1) = 0x0000'0001

	CAS: 실제값(0x0005'0000) != expected(0x0000'0000) → 실패, 다시 스핀
	expected는 "Write 소유자가 없는 상태"를 가정하고 만든 값입니다. 그래서 실제로 Write 중이면 값이 서로 다르고, CAS가 실패합니다. 이것이 확인 질문 1번의 답과도 연결됩니다.

	상황 C: 동시에 두 스레드가 ReadLock을 시도
	lockFlag_ = 0x0000'0002
	스레드 X: expected = 2, desired = 3
	스레드 Y: expected = 2, desired = 3

	X의 CAS 먼저 성공 → lockFlag_ = 3
	Y의 CAS: 실제값(3) != expected(2) → 실패
	Y가 다시 시도: expected = 3, desired = 4 → 성공 → lockFlag_ = 4
	그냥 fetch_add(1)을 쓰지 않고 CAS  . "Write 소유자가 없는지 확인"과
	"+1"을 한 번에 원자적으로 처리해야 하기 때문입니다. fetch_add는 조건 없이 무조건 더하므로, Write 중에도 Read 카운트를 올려 버립니다.
*/
void Lock::ReadLock(const char* name)
{
	ASSERT_CRASH(LThreadId != 0 && LThreadId <= 0xFFFF);

	// 1) 내가 Write 락을 갖고 있으면 다른 스레드는 들어올 수 없다 → 경쟁 없이 Read 카운트만 올린다
	const uint32 lockThreadId = (lockFlag_.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		lockFlag_.fetch_add(1);
		return;
	}

	// 2) Write 소유자가 없을 때(상위 16비트 == 0)만 Read 카운트를 +1 한다
	const auto beginTime = std::chrono::steady_clock::now();

	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			uint32 expected = (lockFlag_.load() & READ_COUNT_MASK);
			if (lockFlag_.compare_exchange_strong(OUT expected, expected + 1))
				return;
		}

		if (std::chrono::steady_clock::now() - beginTime >= std::chrono::milliseconds(ACQUIRE_TIMEOUT_MS))
			CRASH("LOCK_TIMEOUT");

		std::this_thread::yield();
	}
}

void Lock::ReadUnlock(const char* name)
{
	// fetch_sub은 빼기 전 값을 돌려준다 → 이미 0이었다면 Lock보다 Unlock을 더 많이 호출한 것
	if ((lockFlag_.fetch_sub(1) & READ_COUNT_MASK) == 0)
		CRASH("MULTIPLE_UNLOCK");
}
