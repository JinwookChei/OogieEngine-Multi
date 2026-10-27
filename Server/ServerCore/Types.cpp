#include "pch.h"

// 타입 크기는 어디서 include하든 같으므로, 검증은 이 파일에서 한 번만 컴파일한다.
// (Types.h가 CoreMacro.h를 몰라도 되도록 Types.h에서 분리)


/*
	[타입 크기 검증]
*/
STATIC_ASSERT_SIZE(int8, 1);
STATIC_ASSERT_SIZE(int16, 2);
STATIC_ASSERT_SIZE(int32, 4);
STATIC_ASSERT_SIZE(int64, 8);
STATIC_ASSERT_SIZE(uint8, 1);
STATIC_ASSERT_SIZE(uint16, 2);
STATIC_ASSERT_SIZE(uint32, 4);
STATIC_ASSERT_SIZE(uint64, 8);
STATIC_ASSERT_SIZE(uintptr, sizeof(void*));