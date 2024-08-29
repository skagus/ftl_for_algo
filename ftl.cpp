#include "macro.h"
#include "util.h"
#include "nand.h"
#include "ftl.h"

#define NUM_BLK_USER_PART		(16)
#define MU_PER_SB				(MU_PER_BLK * NUM_DIE)
#define NUM_LPN					((NUM_BLK_USER_PART - 3) * MU_PER_SB)

enum BlkSt
{
	CLOSED,
	USER,
	GC,
	NUM_BLK_STATE,
};

struct Flow
{
	bool bEnable;	///< true if flow control is enabled.
	int nTick;		///< Current ticket count.
	int nGcUnit;	///< plus this from nTick per GC write.
	int nUserUnit;	///< minus this from nTick per User Write.
	int16 nGcLoop;
public:
	bool GcRunnable()
	{
		if ((nTick <= 0) || (nGcLoop <= 0))
		{
			nTick += nGcUnit;
			nGcLoop = 10;
			return true;
		}
		nGcLoop--;
		return false;
	}
	bool UserRunnable()
	{
		if (bEnable)
		{
			if (nTick >= 0)
			{
				nTick -= nUserUnit;
				return true;
			}
			return false;
		}
		return true;
	}
	void Enable(int nGc, int nUser)
	{
		bEnable = true;
		nTick = 0;
		if (0 != (nGc + nUser))
		{
			// 여기에서 서로 상대의 실행비율을 가져와야 한다.
			nGcUnit = nUser;
			nUserUnit = nGc;
		}
	}
	void Disable()
	{
		bEnable = false;
		nTick = 0;
//		nGcUnit = 1;
//		nUserUnit = 1;
	}
	void Init()
	{
		Disable();
	}
};

struct BBlkInfo
{
	uint32 nAge;
	uint32 nEC;
	uint32 nValid;
	uint32 abmValid[MU_PER_SB / 32];	// in WL --> Die --> WL 순으로..
	BlkSt eState;

	bool Get(VAddr stVA)
	{
		uint32 nBOff = (stVA.nWL * NUM_DIE + stVA.nDie) * MU_PER_WL + stVA.nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		return (0 != (abmValid[nDW] & BIT(nDWOff)));
	}

	static VAddr Trans(uint32 nBOff)
	{
		VAddr stVA;
		stVA.nDW = 0;
		stVA.nMO = nBOff % MU_PER_WL;
		nBOff /= MU_PER_WL;
		stVA.nDie = nBOff % NUM_DIE;
		stVA.nWL = nBOff / NUM_DIE;
		return stVA;
	}

	bool Get(uint32 nBOff)
	{
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		return (0 != (abmValid[nDW] & BIT(nDWOff)));
	}

	bool Get(uint32 nDie, uint32 nWL, uint32 nMO)
	{
		uint32 nBOff = (nWL * NUM_DIE + nDie) * MU_PER_WL + nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		return (0 != (abmValid[nDW] & BIT(nDWOff)));
	}

	void Set(VAddr stVA)
	{
		uint32 nBOff = (stVA.nWL * NUM_DIE + stVA.nDie) * MU_PER_WL + stVA.nMO;
		uint32 nDW = nBOff / 32;
		uint32 nDWOff = nBOff % 32;
		abmValid[nDW] |= BIT(nDWOff);
		nValid++;
	}

	bool Clear(VAddr stVA)
	{
		uint32 nBOff = (stVA.nWL * NUM_DIE + stVA.nDie) * MU_PER_WL + stVA.nMO;
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
	Main aMainBuf[MU_PER_WL];
	Ext aExtBuf[MU_PER_WL];
	uint32 bmValid;
	uint32 nQueued;
	void Reset()
	{
		bmValid = 0;
		nQueued = 0;
	}
	void Add(uint32 nLPN, uint32 nData)
	{
		aMainBuf[nQueued].nHeader = nData;
		aExtBuf[nQueued].nLPN = nLPN;
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
	Flow mstFC;

	WrtQ mstUWQ;
	bool mbFlush;
public:
	void Flush()
	{
		if (mstUWQ.nQueued > 0)
		{
			mbFlush = true;
			while (mbFlush)
			{
				TASK_Switch();
			}
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
		Main aData[MU_PER_WL];
		Ext aExt[MU_PER_WL];
		VAddr stVA = maMap[nLPN];
		if (FF32 != stVA.nDW)
		{
			NAND_Read(stVA, BIT(stVA.nMO), aData, aExt);
			*pnData = aData[stVA.nMO].nHeader;
		}
		else
		{
			*pnData = FF32;
		}
	}

	void MapUpdate(uint32 nLPN, VAddr stAddr, Actor eAct)
	{
		VAddr stPrv = maMap[nLPN];
#if 1
		PRINTF("%5X (%d), {%X,%X,%X,%X} -> {%X,%X,%X,%X}\n",
			nLPN, eAct,
			stPrv.nDie, stPrv.nBBN, stPrv.nWL, stPrv.nMO,
			stAddr.nDie, stAddr.nBBN, stAddr.nWL, stAddr.nMO);
#endif
		maMap[nLPN] = stAddr;
		maBI[stAddr.nBBN].Set(stAddr);
		if (FF32 != stPrv.nDW)
		{
			ASSERT(maBI[stPrv.nBBN].Get(stPrv));
			if (maBI[stPrv.nBBN].Clear(stPrv))
			{
				mnFree++;
			}
		}
	}

	void SetBlkState(uint16 nBN, BlkSt eSt)
	{
		maBI[nBN].eState = eSt;
		maBI[nBN].nAge = mnAge++;
		if (BlkSt::CLOSED != eSt)
		{
			maBI[nBN].nEC++;
		}
	}

	uint16 GetMinValid(uint16 nPrvMin, uint32* pnValid)
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
		*pnValid = nMinCnt;
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
				PRINTF("Alloc Blk %X for %s\n", nBN, bSecure ? "GC" : "User");
				return nBN;
			}
		} while (true); //  nBN != mnSBScan);
		ASSERT(false);
		return FF16;
	}

	void RunWrite()
	{
	BEGIN:
		while (NOT(mstFC.UserRunnable()) ||
			((false == mbFlush) && (mstUWQ.nQueued < MU_PER_WL)))
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
			for (uint32 nDie = 0; nDie < NUM_DIE; nDie++)
			{
				mstUser.nDie = nDie;
				NAND_Erase(mstUser);
			}
			mstUser.nDie = 0;
		}

		NAND_Program(mstUser, mstUWQ.bmValid, mstUWQ.aMainBuf, mstUWQ.aExtBuf);
		VAddr stTmp = mstUser;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			stTmp.nMO = nMO;
			MapUpdate(mstUWQ.aExtBuf[nMO].nLPN, stTmp, ACT_USER);
		}
		mstUser.Inc();
		mstUWQ.Reset();
		mbFlush = false;
		goto BEGIN;
	}

	void RunGC()
	{
		uint16 nMinBN = FF16;
		BBlkInfo* pBI = nullptr;
		uint32 nSrcBlkOff = 0;
		
		WrtQ stQue;
		stQue.Reset();

	BEGIN:
		mstFC.Disable();
		while (mnFree > 2)
		{
			TASK_Switch();
		}
		mstFC.Enable(0, 0);
	BLK_MOV:
		while (NOT(mstFC.GcRunnable()))
		{
			TASK_Switch();
		}
		///////// Read. //////
		
		while (stQue.nQueued < MU_PER_WL)
		{
			if ((nMinBN >= NUM_BLK_USER_PART)
				|| (nSrcBlkOff >= MU_PER_SB))
			{
				uint32 nValid;
				nMinBN = GetMinValid(nMinBN, &nValid);
				mstFC.Enable(nValid, MU_PER_SB - nValid);
				pBI = maBI + nMinBN;
				nSrcBlkOff = 0;
				PRINTF("GC Victim: %X\n", nMinBN);
			}
			while (nSrcBlkOff < MU_PER_SB)
			{
				if (pBI->Get(nSrcBlkOff))
				{
					Main aData[MU_PER_WL];
					Ext aExt[MU_PER_WL];
					VAddr stSrc = BBlkInfo::Trans(nSrcBlkOff);
					stSrc.nBBN = nMinBN;
					NAND_Read(stSrc, BIT(stSrc.nMO), aData, aExt);
					stQue.Add(aExt[stSrc.nMO].nLPN, aData[stSrc.nMO].nHeader);
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
			for (uint32 nDie = 0; nDie < NUM_DIE; nDie++)
			{
				mstGcDst.nDie = nDie;
				NAND_Erase(mstGcDst);
			}
			mstGcDst.nDie = 0;
		}
		NAND_Program(mstGcDst, stQue.bmValid, stQue.aMainBuf, stQue.aExtBuf);
		VAddr stTmp = mstGcDst;
		for (uint32 nMO = 0; nMO < MU_PER_WL; nMO++)
		{
			stTmp.nMO = nMO;
			MapUpdate(stQue.aExtBuf[nMO].nLPN, stTmp, ACT_GC);
		}
		mstGcDst.Inc();
		stQue.Reset();

		// Yield to Write.
//		TASK_Switch();

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
		mstFC.Init();
		return NUM_LPN;
	}
};

UserPart gstUserPart;

void task_RunWrite(void* pParam)
{
	UserPart* pPart = (UserPart*)pParam;
	pPart->RunWrite();
}

void task_RunGC(void* pParam)
{
	UserPart* pPart = (UserPart*)pParam;
	pPart->RunGC();
}

uint32 FTL_Init()
{
	TASK_Create(task_RunWrite, &gstUserPart);
	TASK_Create(task_RunGC, &gstUserPart);
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


