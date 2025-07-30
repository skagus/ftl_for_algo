#include "util.h"
#include "nand.h"

struct BWL
{
	uint32 nSeqNo;
	bool bFull;
	union
	{
		Main* aMain[MU_PER_WL];
		uint32 anHdr[MU_PER_WL];
	};
	Ext aExt[MU_PER_WL];
};

struct BBlk
{
	uint16 nEC;
	uint16 nCPO;
	BWL aWL[WL_PER_BLK];
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
		for (uint32 nWL = 0; nWL < pBlk->nCPO; nWL++)
		{
			BWL* pWL = pBlk->aWL + nWL;
			if (pBlk->aWL[nWL].bFull)
			{
				for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
				{
					if (nullptr != pWL->aMain[nMO])
					{
						MEM_Free(MemId::MEM_NAND_4KB, pWL->aMain[nMO]);
//						free(pWL->aMain[nMO]);
					}
				}
			}
			memset(pWL, 0x00, sizeof(*pWL));
		}
		pBlk->nCPO = 0;
		pBlk->nEC++;
	}

	uint32 READ(VAddr stAddr, uint32 bmMU, Main* aMain, Ext* aExt)
	{
		BBlk* pBlk = aBlk + stAddr.nBBN;
		BWL* pWL = pBlk->aWL + stAddr.nWL;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			if (BIT(nMO) & bmMU)
			{
				if (pWL->bFull)
				{
					memcpy(aMain + nMO, pWL->aMain[nMO], sizeof(*(pWL->aMain[nMO])));
				}
				else
				{
					aMain[nMO].nHeader = pWL->anHdr[nMO];
				}
				aExt[nMO] = pWL->aExt[nMO];
			}
		}
		return 0;
	}

	void PGM(VAddr stAddr, uint32 bmMU, Main* aMain, Ext* aExt, uint32 bmOpt)
	{
		PRINTF("PGM %X {%X,%X}\n", stAddr.nDie, stAddr.nBBN, stAddr.nWL);
		BBlk* pBlk = aBlk + stAddr.nBBN;
		ASSERT(pBlk->nCPO == stAddr.nWL);
		pBlk->nCPO++;
		BWL* pWL = pBlk->aWL + stAddr.nWL;
		pWL->nSeqNo = UTIL_GetSeqNo();
		pWL->bFull = (0 != (bmOpt & NOPT_FULL_DATA));
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			if (BIT(nMO) & bmMU)
			{
				if (pWL->bFull)
				{
					pWL->aMain[nMO] = (Main*)MEM_Alloc(MemId::MEM_NAND_4KB);
					memcpy(pWL->aMain[nMO], aMain + nMO, DW_PER_MAIN * sizeof(uint32));
				}
				else
				{
					pWL->anHdr[nMO] = aMain[nMO].nHeader;
				}
				pWL->aExt[nMO] = aExt[nMO];
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
	MEM_Init(MemId::MEM_NAND_4KB, DW_PER_MAIN * sizeof(uint32), MU_PER_BLK * BBLK_PER_DIE * NUM_DIE);
}

void NAND_Erase(VAddr stAddr)
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->ERS(stAddr.nBBN);
}


void NAND_Program(VAddr stAddr, uint32 mbmValid, Main aMain[MU_PER_WL], Ext aExt[MU_PER_WL], uint32 bmOpt)
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->PGM(stAddr, mbmValid, aMain, aExt, bmOpt);
}

void NAND_Read(VAddr stAddr, uint32 mbmValid, Main aMain[MU_PER_WL], Ext aExt[MU_PER_WL])
{
	NAND* pDie = gaDie + stAddr.nDie;
	pDie->READ(stAddr, mbmValid, aMain, aExt);
}
