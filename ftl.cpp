#include "macro.h"
#include "util.h"
#include "nand.h"
#include "ftl.h"

#define MU_PER_SB		(MU_PER_BLK * NUM_DIE)
#define NUM_LPN			(MU_PER_SB * BBLK_PER_DIE / 2)
#define NUM_BLK_USER_PART	(16)

enum BlkSt
{
	CLOSED,
	USER,
	GC,
	NUM_BLK_STATE,
};

struct BBlkInfo
{
	uint32 nAge;
	uint32 nEC;
	uint32 nValid;
	uint32 abmValid[MU_PER_SB / 32];
	BlkSt eState;

	bool Get(uint32 nWL, uint32 nMO)
	{
		uint32 nBOff = nWL * MU_PER_WL + nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		return (0 != (abmValid[nDW] & BIT(nDWOff)));
	}

	void Set(uint32 nWL, uint32 nMO)
	{
		uint32 nBOff = nWL * MU_PER_WL + nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		abmValid[nDW] |= BIT(nDWOff);
		nValid++;
	}

	bool Clear(uint32 nWL, uint32 nMO)
	{
		uint32 nBOff = nWL * MU_PER_WL + nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		abmValid[nDW] &= ~BIT(nDWOff);
		nValid--;
		return 0 == nValid;
	}
};

struct ActBlk
{
	uint16 nBN;
	uint16 nCPO;
};

class FTL
{
public:
	uint32 nBaseBBN;
	uint32 nNumBBN;
	virtual void Init(uint32 nBase, uint32 nBBN) = 0;
	virtual void Flush() = 0;
	virtual void Write(uint32 nLPN) = 0;
	virtual uint32 Read(uint32 nLPN) = 0;
};

struct WrtQ
{
	uint32 aLpnBuf[MU_PER_WL];
	uint32 aDataBuf[MU_PER_WL];
	uint32 bmValid;
	uint32 nQueued;
	void Reset()
	{
		bmValid = 0;
		nQueued = 0;
	}
	void Add(uint32 nLPN, uint32 nData)
	{
		aLpnBuf[nQueued] = nLPN;
		aDataBuf[nQueued] = nData;
		bmValid |= BIT(nQueued);
		nQueued++;
	}
};

class UserPart
{
	uint32 mnAge;

	VAddr maMap[NUM_LPN];
	BBlkInfo maBI[NUM_BLK_USER_PART];
	uint16 mnFree;
	uint16 mnSBScan;
	VAddr mstUser;
	VAddr mstGcDst;

	WrtQ mstUWQ;
	bool mbFlush;
public:
	void Flush()
	{
		if (mstUWQ.nQueued > 0)
		{
			mbFlush = true;
		}
	}

	void Write(uint32 nLPN, uint32 nData)
	{
		while (mstUWQ.nQueued >= MU_PER_WL)
		{
			TASK_Switch();
		}
		mstUWQ.Add(nLPN, nData);
	}
	
	void Read(uint32 nLPN, uint32* pnData)
	{

	}

	void MapUpdate(uint32 nLPN, VAddr stAddr)
	{
		VAddr stPrv = maMap[nLPN];
		PRINTF("%X, {%X,%X,%X} -> {%X,%X,%X}\n", nLPN,
			stPrv.nBBN, stPrv.nWL, stPrv.nMO,
			stAddr.nBBN, stAddr.nWL, stAddr.nMO);
		maMap[nLPN] = stAddr;
		maBI[stAddr.nBBN].Set(stAddr.nWL, stAddr.nMO);
		if (FF32 != stPrv.nDW)
		{
			ASSERT(maBI[stPrv.nBBN].Get(stPrv.nWL, stPrv.nMO));
			if (maBI[stPrv.nBBN].Clear(stPrv.nWL, stPrv.nMO))
			{
				mnFree++;
			}
		}
	}

	void SetBlkState(uint16 nBN, BlkSt eSt)
	{
		maBI[nBN].eState = eSt;
		maBI[nBN].nAge = mnAge++;
		maBI[nBN].nEC++;
	}

	uint16 GetMinValid(uint16 nPrvMin)
	{
		uint16 nMinBN;
		uint32 nMinCnt = FF32;
		for (uint32 nBN = 0; nBN < NUM_BLK_USER_PART; nBN++)
		{
			if ((nPrvMin != nBN) 
				&& (maBI[nBN].eState == CLOSED)
				&& (maBI[nBN].nValid < nMinCnt)
				&& (maBI[nBN].nValid != 0))
			{
				nMinCnt = maBI[nBN].nValid;
				nMinBN = nBN;
			}
		}

		return nMinBN;
	}
	/**
	* GC인 경우에는 bSecure == true
	*/
	uint16 GetFree(bool bSecure)
	{
		uint16 nBN = mnSBScan;
		while ((false == bSecure) && (mnFree < 2))
		{
			TASK_Switch();
		}
		do
		{
			nBN = (nBN + 1) % NUM_BLK_USER_PART;
			if ((maBI[nBN].nValid <= 0) && (maBI[nBN].eState == CLOSED))
			{
				mnFree--;
				mnSBScan = nBN;
				return nBN;
			}
		} while (nBN != mnSBScan);
		ASSERT(false);
	}

	void RunWrite()
	{
	BEGIN:
		while ((false == mbFlush) && (mstUWQ.nQueued < MU_PER_WL))
		{
			TASK_Switch();
		}
		if (mstUser.nWL >= WL_PER_BLK)
		{
			if (mstUser.nBBN < BBLK_PER_DIE)
			{
				SetBlkState(mstUser.nBBN, CLOSED);
			}
			mstUser.nDW = 0;
			mstUser.nBBN = GetFree(false);
			ASSERT(mstUser.nBBN < NUM_BLK_USER_PART);
			SetBlkState(mstUser.nBBN, USER);
			NAND_Erase(mstUser);
		}
		NAND_Program(mstUser, mstUWQ.bmValid, mstUWQ.aDataBuf, mstUWQ.aLpnBuf);
		VAddr stTmp = mstUser;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			stTmp.nMO = nMO;
			MapUpdate(mstUWQ.aLpnBuf[nMO], stTmp);
		}
		mstUser.nWL++;
		mstUWQ.Reset();
		mbFlush = false;
		goto BEGIN;
	}

	void RunGC()
	{
		uint16 nMinBN = FF16;
		BBlkInfo* pBI = nullptr;
		uint32 nSrcBlkOff = 0;
		
		VAddr stSrc;
		WrtQ stQue;
		stQue.Reset();

	BEGIN:
		while (mnFree > 2)
		{
			TASK_Switch();
		}
	BLK_MOV:
		///////// Read. //////
		while (stQue.nQueued < MU_PER_WL)
		{
			if ((nMinBN >= NUM_BLK_USER_PART)
				|| (nSrcBlkOff >= MU_PER_SB))
			{
				nMinBN = GetMinValid(nMinBN);
				pBI = maBI + nMinBN;
				nSrcBlkOff = 0;
				stSrc.nDW = 0;
				stSrc.nBBN = nMinBN;
				PRINTF("GC Victim: %X\n", nMinBN);
			}
			while (nSrcBlkOff < MU_PER_SB)
			{
				if (pBI->Get(nSrcBlkOff / MU_PER_WL, nSrcBlkOff % MU_PER_WL))
				{
					uint32 anData[MU_PER_WL];
					uint32 anExt[MU_PER_WL];
					stSrc.nWL = nSrcBlkOff / MU_PER_WL;
					uint32 nOffiWL = nSrcBlkOff % MU_PER_WL;
					NAND_Read(stSrc, BIT(nOffiWL), anData, anExt);
					stQue.Add(anExt[nOffiWL], anData[nOffiWL]);
				}
				nSrcBlkOff++;
				if (stQue.nQueued >= MU_PER_WL)
				{
					break;
				}
			}
		}
		///// Write ////
		if (mstGcDst.nWL >= WL_PER_BLK)
		{
			if (mstGcDst.nBBN < BBLK_PER_DIE)
			{
				SetBlkState(mstGcDst.nBBN, CLOSED);
			}
			mstGcDst.nDW = 0;
			mstGcDst.nBBN = GetFree(true);
			SetBlkState(mstGcDst.nBBN, GC);
			NAND_Erase(mstGcDst);
			PRINTF("GC Open: %X\n", mstGcDst.nBBN);
		}
		NAND_Program(mstGcDst, stQue.bmValid, stQue.aDataBuf, stQue.aLpnBuf);
		VAddr stTmp = mstGcDst;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			stTmp.nMO = nMO;
			MapUpdate(stQue.aLpnBuf[nMO], stTmp);
		}
		mstGcDst.nWL++;
		stQue.Reset();

		// Yield to Write.
		TASK_Switch();

		/// Check Idle /////
		if (mstGcDst.nWL >= WL_PER_BLK)
		{
			goto BEGIN;
		}
		goto BLK_MOV;

	}

	uint32 Init()
	{
		mstUWQ.Reset();
		mbFlush = false;
		mnFree = NUM_BLK_USER_PART;
		mnAge = 0;
		memset(maBI, 0, sizeof(maBI));
		mstUser.nDW = FF32;
		mstGcDst.nDW = FF32;
		memset(maMap, 0xFF, sizeof(maMap));
		return NUM_LPN;
	}
};

UserPart gstUserPart;

void ftl_Task(void* pParam)
{
	if (0 == pParam)
	{
		gstUserPart.RunWrite();
	}
	else
	{
		gstUserPart.RunGC();
	}
}

uint32 FTL_Init()
{
	TASK_Create(ftl_Task, (void*)0);
	TASK_Create(ftl_Task, (void*)1);
	return gstUserPart.Init();
}

void FTL_Write(Part ePart, uint32 nLPN, uint32 nData)
{
	gstUserPart.Write(nLPN, nData);
}

void FTL_Flush(Part ePart)
{
	gstUserPart.Flush();
}

void FTL_Read(Part ePart, uint32 nLPN, uint32* pnData)
{
	gstUserPart.Read(nLPN, pnData);
}


