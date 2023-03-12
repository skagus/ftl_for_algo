#include "util.h"
#include "nand.h"

struct BWL
{
	uint32 anData[MU_PER_WL];
	uint32 anExt[MU_PER_WL];
};

struct BBlk
{
	uint32 nCPO;
	BWL aBPg[WL_PER_BLK];
};

class NAND
{
	BBlk aBlk[BBLK_PER_DIE];

public:
	void Init()
	{
		memset(aBlk, 0x00, sizeof(aBlk));
	}

	void ERS(uint16 nBBN)
	{
		BBlk* pBlk = aBlk + nBBN;
		memset(pBlk, 0xFF, sizeof(*pBlk));
		pBlk->nCPO = 0;
	}

	uint32 READ(uint16 nBBN, uint16 nWL, uint32 bmMU, uint32* anData, uint32* anExt)
	{
		BBlk* pBlk = aBlk + nBBN;
		BWL* pWL = pBlk->aBPg + nWL;
		for (uint32 i = 0; i < MU_PER_WL; i++)
		{
			if (BIT(i) & bmMU)
			{
				anData[i] = pWL->anData[i];
				anExt[i] = pWL->anExt[i];
			}
		}
		return 0;
	}


	void PGM(uint16 nBBN, uint16 nWL, uint32 bmMU, uint32* anData, uint32* anExt)
	{
		BBlk* pBlk = aBlk + nBBN;
		ASSERT(pBlk->nCPO == nWL);
		pBlk->nCPO++;
		BWL* pWL = pBlk->aBPg + nWL;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			if (BIT(nMO) & bmMU)
			{
				pWL->anData[nMO] = anData[nMO];
				pWL->anExt[nMO] = anExt[nMO];
			}
		}
	}
};

NAND gaDie[NUM_DIE];

void NAND_Init()
{
	for (uint32 i = 0; i < NUM_DIE; i++)
	{
		gaDie[i].Init();
	}
}

void NAND_Erase(VAddr stAddr)
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->ERS(stAddr.nBBN);
}


void NAND_Program(VAddr stAddr, uint32 mbmValid, uint32 anMain[MU_PER_WL], uint32 anExt[MU_PER_WL])
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->PGM(stAddr.nBBN, stAddr.nWL, mbmValid, anMain, anExt);
}

void NAND_Read(VAddr stAddr, uint32 mbmValid, uint32 anMain[MU_PER_WL], uint32 anExt[MU_PER_WL])
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->READ(stAddr.nBBN, stAddr.nWL, mbmValid, anMain, anExt);
}
