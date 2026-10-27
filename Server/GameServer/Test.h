#pragma once
void ThreadRace();

void WriteLockCounter();

/*
	[Lock / DeadLockProfiler 테스트]
	정상 테스트: 크래시 없이 "OK"가 출력되어야 한다.
*/
void LockReentrancy();      // 같은 락 Write → Write → Read 재진입
void LockContention();      // Writer 1 + Reader 3 경합, 읽은 값의 일관성 검증
void NoCycleLock();         // ◇ 모양(X → Y, X → Z, Z → Y) + 분리된 덩어리(C → D) → 사이클 아님

/*
	[Memory 테스트]
*/
void XnewXdelete();         // 생성자 인자 전달, 소멸자 호출, nullptr 해제
void StompAllocFree();      // StompAllocator 직접 호출: 정렬, 페이지 끝 배치, 읽기/쓰기, nullptr 해제 (크기 8 / 4096 / 5000)
void MemoryHeaderAttachDetach();  // 헤더 붙이기/떼기: 데이터 주소 = 블록 + sizeof(헤더), 16바이트 정렬, 데이터가 헤더를 덮지 않음
void MemoryPoolReuse();           // Pop → Push → Pop이 같은 블록을 돌려주는지 (LIFO 재사용), use/reserve 카운트, 16바이트 정렬
void MemoryPoolConcurrent();      // 스레드 4개 × 10,000번 Pop/Push → use 0, reserve 1~4, 블록을 두 스레드가 동시에 받지 않음

/*
	크래시 테스트: 호출하면 반드시 크래시가 나야 정상이다. 한 번에 하나만 호출한다.
	(Debug 전용. Release에서는 Profiler가 꺼져 있어 크래시가 나지 않는다)
*/
void CrashInvalidUnlockOrder();   // A → B 순서로 잡고 A를 먼저 해제 → INVALID_UNLOCK_ORDER
void CrashUnknownLock();          // 등록된 적 없는 이름으로 해제 → UNKNOWN_LOCK
void CrashThreadIdZero();         // InitTLS를 거치지 않은 스레드(LThreadId == 0)가 락 요청 → ASSERT_CRASH
void CrashDeadLockCycle();        // 스레드 1: P → Q, 스레드 2: Q → R → P (시간 겹침 없음) → DEADLOCK_DETECTED

// Memory 크래시 테스트 (Access Violation 0xC0000005가 나야 정상)
void CrashStompUseAfterFree();    // xdelete 후 멤버에 쓰기 → Debug만 크래시 (Release는 BaseAllocator)
void CrashStompOverflow();        // StompAllocator로 16바이트 받고 [16]에 쓰기 → Debug/Release 모두 크래시
void CrashMemoryPoolDoublePush(); // 같은 블록을 두 번 Push → ASSERT_CRASH(allocSize_ > 0), Debug/Release 모두 크래시
