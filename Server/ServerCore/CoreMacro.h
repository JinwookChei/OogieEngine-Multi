#pragma once
#include "Types.h"


/*
	[검증 매크로]
*/
// [1층] 구현: static_assert를 직접 쓰는 유일한 곳. 검증 방식을 바꾸려면 이 줄만 수정한다.
//       msg에는 문자열 리터럴만 넘긴다. (예: "[ServerCore] " msg 처럼 접두어를 붙일 수 있도록)
//       expr에 템플릿 식처럼 쉼표가 들어가면 괄호로 한 번 더 감싼다.
//       예) STATIC_ASSERT((std::is_same_v<int32, int>), "int32 must be int");
#define STATIC_ASSERT(expr, msg)        static_assert((expr), msg)

// [2층] 용도별 매크로: 반드시 STATIC_ASSERT를 거친다.
//       예) STATIC_ASSERT_SIZE(int32, 4) → 실패 시 "int32 must be 4 bytes"
#define STATIC_ASSERT_SIZE(type, size) \
	STATIC_ASSERT(sizeof(type) == (size), #type " must be " #size " bytes")


/*
	[크기 매크로]
*/
// sizeof는 size_t(x64에서 8바이트)를 반환 → 패킷 크기 필드(uint16)에 넣을 때 캐스팅 필요
#define size16(val)             static_cast<int16>(sizeof(val))
#define size32(val)             static_cast<int32>(sizeof(val))
#define len16(arr)              static_cast<int16>(sizeof(arr) / sizeof(arr[0]))
#define len32(arr)              static_cast<int32>(sizeof(arr) / sizeof(arr[0]))


/*
	[빌드 설정]
*/
// Stomp 할당기: 메모리 오염을 즉시 잡아내는 디버깅용 할당기. (Phase 1-3에서 사용)
// 매우 느리므로 Debug에서만 켠다.
#ifdef _DEBUG
#define _STOMP
#endif
