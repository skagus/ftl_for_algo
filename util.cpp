
#include <windows.h>
#include <stdio.h>
#include <chrono>
#include <random>

#include "types.h"
#include "util.h"

FILE* gpLog = nullptr;
std::mt19937* gpRand;


void DBG_Init(uint32 nSeed)
{
	char aBuf[32];
	time_t t = time(0);   // get time now
	tm now;
	localtime_s(&now, &t);
	sprintf_s(aBuf, 32, "%02d%02d_%02d%02d%02d.txt", now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);

	//	ASSERT(0 == fopen_s(&gpLog, aBuf, "w"));
	gpLog = _fsopen(aBuf, "a+", _SH_DENYWR);
	
	gpRand = new std::mt19937(nSeed);
}


void DBG_Print(const char* pFmt, ...)
{
	char aOrgBuf[1024];
	uint32 nSize;
	va_list ap;
	va_start(ap, pFmt);
	nSize = vsprintf_s(aOrgBuf, 1024, pFmt, ap);
	va_end(ap);
	printf(aOrgBuf);
	fwrite(aOrgBuf, 1, nSize, gpLog);
}

void DBG_Flush()
{
	fflush(gpLog);
}

void DBG_Fine()
{
	fclose(gpLog);
}

uint32 DBG_GetRand()
{
	return (*gpRand)();
}

#define MAX_TASK		(4)
#define STK_SIZE		(0)
uint32 gnNumTask;
uint32 gnCurTask;
HANDLE gaTask[MAX_TASK];

void TASK_Create(Routine pfRun, void* pParam)
{
	if (nullptr == pfRun)
	{
		gaTask[gnNumTask] = ConvertThreadToFiber(nullptr);
		gnCurTask = gnNumTask;
	}
	else
	{
		gaTask[gnNumTask] = CreateFiber(STK_SIZE, (LPFIBER_START_ROUTINE)pfRun, pParam);
	}
	gnNumTask++;
}

void TASK_Switch()
{
	gnCurTask = (gnCurTask + 1) % gnNumTask;
	SwitchToFiber(gaTask[gnCurTask]);
}
