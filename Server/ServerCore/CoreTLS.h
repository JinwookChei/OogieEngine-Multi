#pragma once
#include "Types.h"


/*
        [TLS (Thread Local Storage)]
*/
// 스레드마다 따로 갖는 전역 변수. 같은 이름이지만 스레드별로 저장 공간이 다르므로 락이 필요 없다.
// 이름 규칙: L 접두사 (G = 전역 1개, L = 스레드마다 1개)
extern thread_local uint32 LThreadId;   