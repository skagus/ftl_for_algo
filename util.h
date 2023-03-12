#pragma once
#include <stdio.h>
#include "types.h"

#define PRINTF(...)		DBG_Print(__VA_ARGS__)

typedef void(*Routine)(void* pParam);

#define ASSERT(cond)										\
	do {													\
		if (!(cond))										\
		{													\
			DBG_Print("ASSERT %s(%d)\n", __FUNCTION__, __LINE__);	\
			DBG_Flush();									\
			__debugbreak();									\
		}													\
	} while(0)


void DBG_Init(uint32 nSeed = 0);
void DBG_Print(const char* pFmt, ...);
void DBG_Flush();
void DBG_Fine();

uint32 UTIL_GetRand();

void TASK_Create(Routine pfRun, void* pParam);
void TASK_Switch();

enum MemId
{
	MEM_NAND_4KB,
	NUM_MEM_POOL,
};

void MEM_Init(MemId eId, uint32 nChunkSize, uint32 nNumChunk);
void* MEM_Alloc(MemId eId);
void MEM_Free(MemId eId, void* pMem);

