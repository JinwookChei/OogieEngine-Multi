#pragma once
#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Types.h는 가장 아래층 헤더다. 다른 ServerCore 헤더를 include하지 않는다.
// (CoreMacro.h가 Types.h를 include하는 방향으로만 의존)

/*
	[정수 타입]
*/
using BYTE = unsigned char;
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
// 타입 크기 검증은 Types.cpp에서 한다.

/*
	[포인터 타입]
*/
using uintptr = std::uintptr_t;

/*
	[동기화 타입]
*/
template<typename T>
using Atomic = std::atomic<T>;
using Mutex = std::mutex;
using CondVar = std::condition_variable;
using UniqueLock = std::unique_lock<std::mutex>;
using LockGuard = std::lock_guard<std::mutex>;


/*
	[shared_ptr]
*/
#define USING_SHARED_PTR(name)  using name##Ref = std::shared_ptr<class name>;
// 실제 별칭(IocpCoreRef, SessionRef ...)은 해당 클래스를 만들 때 여기에 추가한다.
