
#include <stdio.h>
#include <windows.h>
#include <chrono>
#include "macro.h"
#include "nand.h"
#include "util.h"
#include "ftl.h"


uint32 gnNumLPN;

void runTest()
{
	PRINTF("======= SEQ WRITE 1 =============\n");
	for (int32 i = 0; i < gnNumLPN * 100; i++)
	{
		uint32 nLPN = UTIL_GetRand() % gnNumLPN;
		FTL_Write(Part::PART_USER, nLPN, nLPN);
	}
	FTL_Flush(Part::PART_USER);
	for (int32 nLPN = 0; nLPN < gnNumLPN; nLPN++)
	{
		uint32 nData;
		FTL_Read(Part::PART_USER, nLPN, &nData);
		ASSERT(nLPN == nData);
	}
	DBG_Fine();
}


int main()
{
	DBG_Init(0);
	NAND_Init();

	TASK_Create(nullptr, nullptr);	// Main task.
	gnNumLPN = FTL_Init();
	
	runTest();
	while (true)
	{
		TASK_Switch();
	}
	return 0;
}
