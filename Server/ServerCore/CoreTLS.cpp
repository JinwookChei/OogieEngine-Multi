#include "pch.h"
#include "CoreTLS.h"

thread_local UInt32 LThreadId = 0;
thread_local std::stack<Int32> LLockStack;