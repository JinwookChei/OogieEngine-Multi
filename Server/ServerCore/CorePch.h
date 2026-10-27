#pragma once

/*
        [CorePch]
        ServerCore를 쓰는 프로젝트(GameServer, DummyClient, Client)가 include하는 묶음 헤더.
        자주 쓰는 헤더를 미리 컴파일해 빌드를 빠르게 하는 용도다.
        각 헤더는 이 파일에 기대지 않고 자기가 쓰는 헤더를 직접 include한다.
        using namespace std는 두지 않는다 (include하는 모든 파일에 퍼지고 되돌릴 수 없다).
*/

#include <iostream>
#include <vector>
#include <functional>
#include <thread>

#include "Types.h"
#include "CoreMacro.h"
#include "CoreTLS.h"
#include "CoreGlobal.h"
#include "Lock.h"
#include "Memory.h"
