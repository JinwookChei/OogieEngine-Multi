#include "pch.h"

// 타입 크기는 어디서 include하든 같으므로, 검증은 이 파일에서 한 번만 컴파일한다.
// (Types.h가 CoreMacro.h를 몰라도 되도록 Types.h에서 분리)


/*
	[타입 크기 검증]
*/
STATIC_ASSERT_SIZE(Int8, 1);
STATIC_ASSERT_SIZE(Int16, 2);
STATIC_ASSERT_SIZE(Int32, 4);
STATIC_ASSERT_SIZE(Int64, 8);
STATIC_ASSERT_SIZE(UInt8, 1);
STATIC_ASSERT_SIZE(UInt16, 2);
STATIC_ASSERT_SIZE(UInt32, 4);
STATIC_ASSERT_SIZE(UInt64, 8);
STATIC_ASSERT_SIZE(UIntPtr, sizeof(void*));