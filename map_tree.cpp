
#include "map_tree.h"

#define MAX_PENDING_GRP			(NUM_CACHE_ENTRY)	// (0x20)
#define PRINTF(fmt, ...)		DBG_Print("%8d: " ## fmt, gnTick, __VA_ARGS__)

class RealMapTree : public MapTree
{
	static uint32 mnGrpSize;
	uint32 mnId;
	/**
	* 1. Tree --> Flush를 MAX cached 기준이 아닌 다른..방법?
	* 2. Tree내에 있는 GRP에 대해서만 valid count를 keey하는 방법.
	*/
	uint32 mnValidPend;
	PendCount maPending[MAX_PENDING_GRP];

	/**
	Tree내에 Group가 포함되어 있는지 찾아보고, 없으면 새로운 pend slot할당.
	Pend와 TreeEntry를 같이 따로 관리해야 하기 때문에, 
	두 종류 모두 자원이 가능해야 update를 할 수 있음.
	*/
	PendCount* searchPend(uint32 nGrpBase)
	{
		PendCount* pEmpty = nullptr;
		for (uint32 i = 0; i < MAX_PENDING_GRP; i++)
		{
			PendCount* pPend = maPending + i;
			if ((nGrpBase == pPend->nGrpBase) && (0 != pPend->nCount))
			{
				return pPend;
			}
			else if ((nullptr == pEmpty) && (0 == pPend->nCount))
			{
				pEmpty = pPend;
			}
		}

		return pEmpty;
	}
	uint32 getBase(uint32 nLPN)
	{
		return nLPN - (nLPN % mnGrpSize);
	}

public:
	static void Init(uint32 nGrpSize)
	{
		mnGrpSize = nGrpSize;
	}

	RealMapTree(uint32 nId)
	{
		mnId = nId;
		mnValid = 0;
		mnValidPend = 0;
		memset(maPending, 0, sizeof(maPending));
		memset(maMap, 0, sizeof(maMap));
	};

	/**
	Pending count디버깅..
	*/
	void DbgCheck()
	{
		uint32 nValid = 0;
		uint32 nPend = 0;
		for (uint32 nIdx = 0; nIdx < MAX_PENDING_GRP; nIdx++)
		{
			PendCount* pPend = maPending + nIdx;
			if (pPend->nCount > 0)
			{
				nPend++;
				nValid += pPend->nCount;
				uint32 nGrpValid = 0;
				uint32 nBase = pPend->nGrpBase;
				for (uint32 nEIdx = 0; nEIdx < mnValid; nEIdx++)
				{
					if (nBase <= maMap[nEIdx].nLPN &&
						maMap[nEIdx].nLPN < nBase + mnGrpSize)
					{
						nGrpValid++;
					}
				}
				ASSERT(nGrpValid == pPend->nCount);
			}
		}
		ASSERT(nPend == mnValidPend);
		ASSERT(nValid == mnValid);
	}

	uint32 CountFree()
	{
		uint32 nFreePend = MAX_PENDING_GRP - mnValidPend;
		uint32 nFreeMap = NUM_CACHE_ENTRY - mnValid;
		// return MIN.
		return (nFreePend < nFreeMap) ? nFreePend : nFreeMap;
	}

	bool DbgCompare(MapTree* pDst)
	{
		RealMapTree* pThat = (RealMapTree*)pDst;
		if (mnValid != pThat->mnValid)
		{
			return false;
		}
		for (uint32 i = 0; i < mnValid; i++)
		{
			if (pThat->Search(maMap[i].nLPN).nDW != maMap[i].stNA.nDW)
			{
				return false;
			}
		}
		return true;
	}

	void Print()
	{
		PRINTF("TREE: V: %d, P: %d\n", mnValid, mnValidPend);
	}

	/**
	가장 많이 바뀐 Group를 가져오는 함수임.
	만약, Cached map에 대한 update는 tree에 하지 않는 경우라면, 
	Next flush candidate는 정해진 것이므로, 
	이 함수 대신 다른 기준(cache dirty queue같은..)으로 선정해야 함.
	*/
	uint32 GetMaxDirty(uint32* pnNum)
	{
		uint32 nMaxValid = 0;
		uint32 nMaxGrpBase = FF32;
		for (uint32 i = 0; i < MAX_PENDING_GRP; i++)
		{
			if (maPending[i].nCount > nMaxValid)
			{
				nMaxValid = maPending[i].nCount;
				nMaxGrpBase = maPending[i].nGrpBase;
			}
		}
		if (nullptr != pnNum)
		{
			*pnNum = nMaxValid;
		}
		return nMaxGrpBase;
	}

	/**
	Map tree에 속한 (dirty) map중에서 가장 오래전에 update된 것의 age.
	이 age기준으로 replay해야 함.
	*/
	uint32 GetMinAge(uint32* pnGrpBase)
	{
		uint32 nMinAge = FF32;
		uint32 nGrpBase;
		for (uint32 i = 0; i < MAX_PENDING_GRP; i++)
		{
			PendCount* pPend = maPending + i;

			if ((pPend->nCount > 0) && (pPend->nMtAge < nMinAge))
			{
				nMinAge = pPend->nMtAge;
				nGrpBase = pPend->nGrpBase;
			}
		}
		if (FF32 != nMinAge && nullptr != pnGrpBase)
		{
			*pnGrpBase = nGrpBase;
		}
		return nMinAge;
	}

	bool AddEntry(uint32 nLPN, VAddr stDst, BlkInfo* maBI)
	{
		uint16 nPBN = stDst.nVBBN;
		uint32 nBase = getBase(nLPN);
		PendCount* pPend = searchPend(nBase);
		if ((nullptr == pPend)
			|| (mnValid >= NUM_CACHE_ENTRY))
		{	// No space to add.
			return false;
		}

		if (0 == pPend->nCount)
		{
			mnValidPend++;
			pPend->nMtAge = gnMetaAge;
			pPend->nGrpBase = nBase;
		}
		ASSERT(pPend->nGrpBase == getBase(nLPN));
		for (uint32 i = 0; i < mnValid; i++)
		{
			if (nLPN == maMap[i].nLPN)
			{
				uint16 nSrcBN = maMap[i].stNA.nVBBN;
				if (nullptr != maBI && nSrcBN != nPBN)
				{
					maBI[nPBN].nVPC++;
					maBI[nSrcBN].nVPC--;
				}
				maMap[i].stNA = stDst;
				return true;
			}
		}
		if (nullptr != maBI)
		{
			maBI[nPBN].nVPC++;
		}
		maMap[mnValid].nLPN = nLPN;
		maMap[mnValid].stNA = stDst;
		mnValid++;
		pPend->nCount++;
		DbgCheck();
		return true;
	}

	/**
	Tree내 map search.
	현재 array로 구현되어 있지만, 좀 더 효율적인 방식이 필요함.
	*/
	VAddr Search(uint32 nLPN)
	{
		uint32 nIdx = 0;
		while (nIdx < mnValid)
		{
			if (nLPN == maMap[nIdx].nLPN)
			{
				return maMap[nIdx].stNA;
			}
			nIdx++;
		}
		VAddr stNA;
		stNA.nDW = FF32;
		return stNA;
	}

	/**
	Base LPN기준 nRange에 해당하는 map을 astL2P로 추려낸다. 
	이때, VPC의 일관성을 유지해야 해야한다!!!
	*/
	uint32 Flush(uint32 nBaseLPN, uint32 nRange, VAddr* astL2P, BlkInfo* maBI)
	{
		DbgCheck();

		uint32 nIdx = 0;
		uint32 nHitCnt = 0;
		ASSERT(0 == nBaseLPN % mnGrpSize);
		PendCount* pPend = searchPend(nBaseLPN);
		if (nullptr != pPend)
		{
			if (0 != pPend->nCount)
			{
				pPend->nCount = 0;
				pPend->nGrpBase = FF16;
				mnValidPend--;
			}
		}
		else
		{
			ASSERT(false);
		}

		while (nIdx < mnValid)
		{
			if ((nBaseLPN <= maMap[nIdx].nLPN)
				&& (maMap[nIdx].nLPN < nBaseLPN + nRange))
			{
				uint32 nLPO = maMap[nIdx].nLPN % nRange;
				VAddr stSrc = astL2P[nLPO];	// to dec.
				astL2P[nLPO] = maMap[nIdx].stNA;
				if (nullptr != maBI)
				{
					if (FF32 != stSrc.nDW)
					{
						uint16 nBN = stSrc.nVBBN;
#if (1) // 0 == EN_GC_VPC)
						ASSERT(maBI[nBN].nVPC > 0);
#endif
						maBI[nBN].nVPC--;
					}
				}
				nHitCnt++;
			}
			else if (nHitCnt > 0)
			{
				maMap[nIdx - nHitCnt] = maMap[nIdx];
			}
			nIdx++;
		}
		mnValid -= nHitCnt;
		DbgCheck();
		return nHitCnt;
	}
};

uint32 RealMapTree::mnGrpSize;

MapTree* MakeMapTree(uint32 nGrpSize)
{
	RealMapTree::Init(nGrpSize);
	RealMapTree* pRMT = new RealMapTree(0);
	return pRMT;
}
