#include "pch.h"


int main(int argc, char* argv[])
{
	cout << "GameServer Start" << endl;

	/*
		[Step 1] 크기 매크로 / STOMP
	*/
	int32 arr[10];
	cout << "size32(arr) = " << size32(arr) << endl;   // 40
	cout << "len32(arr)  = " << len32(arr) << endl;    // 10

#ifdef _STOMP
	cout << "STOMP : ON" << endl;
#else
	cout << "STOMP : OFF" << endl;
#endif

	/*
		[Step 2] ASSERT_CRASH
	*/
	// 1) 조건이 참이면 아무 일도 없다
	ASSERT_CRASH(1 + 1 == 2);
	cout << "ASSERT_CRASH(true) passed" << endl;

	// 2) if/else 안에서도 컴파일되는지 (do-while(0) 확인)
	if (argc > 100)
		ASSERT_CRASH(false);
	else
		cout << "if/else compile OK" << endl;

	// 3) 실제로 크래시가 나는지: 주석을 풀고 F5로 실행 → 확인 후 다시 주석 처리
	// int32 hp = -1;
	// ASSERT_CRASH(hp >= 0);
}
