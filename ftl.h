#pragma once

#include "types.h"

enum Part
{
	PART_USER,
	PART_SYS,
	NUM_PART,
};

uint32 FTL_Init();
void FTL_Flush(Part ePart);
void FTL_Write(Part ePart, uint32 nLPN, uint32 nData);
void FTL_Read(Part ePart, uint32 nLPN, uint32* pnData);
