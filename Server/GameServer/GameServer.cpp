#include "pch.h"
#include "ThreadManager.h"
#include "Test.h"


int main(int argc, char* argv[])
{
	CoreGlobal coreGlobal;
	std::cout << "GameServer Start" << std::endl;

	// ThreadRace();

	WriteLockCounter();
}
