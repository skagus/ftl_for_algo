#pragma once
#include "macro.h"
#include "ftl.h"

/**
* NAND는 항상 full plane pgm으로 동작하는 것으로..
*/

#define NUM_DIE			(1)
#define MAX_BPC			(2)
#define MU_PER_PG		(2)
#define WL_PER_BLK		(16)
#define BBLK_PER_DIE	(16)
#define MU_PER_WL		(MU_PER_PG * MAX_BPC)
#define MU_PER_BLK		(MU_PER_WL * WL_PER_BLK)

#define BITS_WL_IN_BLK			CALC_BIT(WL_PER_BLK)
#define BITS_MU_IN_WL			CALC_BIT(MU_PER_WL)
#define BITS_DIES				CALC_BIT(NUM_DIE)
#define BITS_BLK				CALC_BIT(BBLK_PER_DIE)

union VAddr
{
	struct
	{
		uint32 nDie : BITS_DIES;
		uint32 nBBN : BITS_BLK;
		uint32 nWL : BITS_WL_IN_BLK;
		uint32 nMO : BITS_MU_IN_WL;
	};
	uint32 nDW;
};




void NAND_Init();
void NAND_Erase(VAddr stAddr);
void NAND_Program(VAddr stAddr, uint32 bmValidUnit, uint32 anMain[MU_PER_WL], uint32 anExt[MU_PER_WL]);
void NAND_Read(VAddr stAddr, uint32 bmValidUnit, uint32 anMain[MU_PER_WL], uint32 anExt[MU_PER_WL]);

