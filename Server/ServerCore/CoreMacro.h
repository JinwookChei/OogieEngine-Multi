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
	[런타임 검증 매크로]
*/
// [1층] 구현: 프로세스를 즉시 죽이는 유일한 곳. 크래시 방식을 바꾸려면 여기만 수정한다.
//   - cause를 volatile 지역 변수에 담아 두면, 디버거/크래시 덤프에서 원인 문자열을 볼 수 있다.
//   - volatile 포인터로 써야 컴파일러가 "의미 없는 코드"로 보고 최적화로 지우지 않는다.
//   - __analysis_assume: 정적 분석기(/analyze)의 "널 포인터 역참조" 경고를 끈다. (실행에는 영향 없음)
//   ※ 매크로의 줄 끝 '\' 뒤에는 공백이 있으면 안 된다. (있으면 매크로가 그 줄에서 끝나 버림)
#define CRASH(cause)										\
	do {													\
		const char* volatile pCrashCause = (cause);			\
		(void)pCrashCause;									\
		volatile uint32* pCrash = nullptr;					\
		__analysis_assume(pCrash != nullptr);				\
		*pCrash = 0xDEADBEEF;								\
	} while (0)


// [2층] 용도별 매크로: 반드시 CRASH를 거친다.
//   - #expr: 실패한 조건식 자체를 문자열로 남긴다. 예) "count > 0"
//   - __analysis_assume(expr): 이 줄 이후로 expr이 참이라고 분석기에 알려준다.
#define ASSERT_CRASH(expr)									\
	do {													\
		if (!(expr))										\
		{													\
			CRASH("ASSERT_CRASH: " #expr);					\
			__analysis_assume(expr);						\
		}													\
	} while (0)


// 출력 매개변수 표시용 (아무 일도 하지 않음). 예) bool Pop(OUT Job& job);
#define OUT


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
