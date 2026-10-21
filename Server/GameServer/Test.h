#pragma once
void ThreadRace();

void WriteLockCounter();

/*
	[Lock / DeadLockProfiler 테스트]
	정상 테스트: 크래시 없이 "OK"가 출력되어야 한다.
*/
void LockReentrancy();      // 같은 락 Write → Write → Read 재진입
void LockContention();      // Writer 1 + Reader 3 경합, 읽은 값의 일관성 검증

/*
	크래시 테스트: 호출하면 반드시 크래시가 나야 정상이다. 한 번에 하나만 호출한다.
	(Debug 전용. Release에서는 Profiler가 꺼져 있어 크래시가 나지 않는다)
*/
void CrashInvalidUnlockOrder();   // A → B 순서로 잡고 A를 먼저 해제 → INVALID_UNLOCK_ORDER
void CrashUnknownLock();          // 등록된 적 없는 이름으로 해제 → UNKNOWN_LOCK
void CrashThreadIdZero();         // InitTLS를 거치지 않은 스레드(LThreadId == 0)가 락 요청 → ASSERT_CRASH