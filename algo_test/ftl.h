#pragma once

#include "types.h"

#define FREE_BLK_THR			(4)

enum Part
{
	PART_USER,
	PART_SYS,
	NUM_PART,
};

enum Actor
{
	ACT_USER,
	ACT_GC,
	NUM_ACT,
};

uint32 FTL_Init();
void FTL_Flush(Part ePart);
void FTL_Write(Part ePart, uint32 nLPN, uint32 nData);
void FTL_Read(Part ePart, uint32 nLPN, uint32* pnData);
