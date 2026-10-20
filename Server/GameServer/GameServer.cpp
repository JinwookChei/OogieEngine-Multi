#include "pch.h"


int main()
{
    cout << "GameServer Start" << endl;

    int32 arr[10];
    cout << "size32(arr) = " << size32(arr) << endl;   // 40
    cout << "len32(arr)  = " << len32(arr) << endl;    // 10

#ifdef _STOMP
    cout << "STOMP : ON" << endl;
#else
    cout << "STOMP : OFF" << endl;
#endif
}