#pragma once

extern class ThreadManager* GThreadManager;
extern class DeadLockProfiler* GDeadLockProfiler;

/*
    [CoreGlobal]
    서버 전역 매니저의 생성/소멸 순서를 한 곳에서 관리한다.
    main에서 지역 객체로 생성한다 → 생성/소멸 시점이 코드에 드러나고, main이 끝날 때 자동 정리된다.
*/
class CoreGlobal
{
public:
    CoreGlobal();
    ~CoreGlobal();

    // 복사되면 소멸자가 두 번 불려 이중 delete → 복사 금지
    CoreGlobal(const CoreGlobal&) = delete;
    CoreGlobal& operator=(const CoreGlobal&) = delete;

};

