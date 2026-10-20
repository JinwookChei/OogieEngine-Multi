#pragma once

#define WIN32_LEAN_AND_MEAN

#ifdef _DEBUG
#pragma comment(lib, "ServerCore\\Debug\\ServerCore.lib")
//#pragma comment(lib, "Protobuf\\Debug\\libprotobufd.lib")    // Phase 2에서 활성화
#else
#pragma comment(lib, "ServerCore\\Release\\ServerCore.lib")
//#pragma comment(lib, "Protobuf\\Release\\libprotobuf.lib")   // Phase 2에서 활성화
#endif

#include "CorePch.h"
