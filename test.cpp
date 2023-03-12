
#include <stdio.h>
#include <windows.h>
#include <chrono>
#include "macro.h"
#include "util.h"
#include "ftl.h"


uint32 gnNumLPN;

int main()
{
	DBG_Init(0);
	TASK_Create(nullptr, nullptr);
	gnNumLPN = FTL_Init();

	PRINTF("======= SEQ WRITE 1 =============\n");
	for (int32 i = 0; i < gnNumLPN * 100; i++)
	{
		uint32 nLPN = DBG_GetRand() % gnNumLPN;
		FTL_Write(Part::PART_USER, nLPN, nLPN);
	}
	FTL_Flush(Part::PART_USER);
	DBG_Fine();
	while (true)
	{
		TASK_Switch();
	}
	return 0;
}
