
#include "types.h"
#include "config.h"
#include "map_cache.h"
#include "ftl.h"

#if (EN_MAP_CACHE == 1)
class MapCache
{
	uint32 mnClkPtr;
	MCacheEntry mastMapCache[NUM_CACHED_MAP];

	uint32 getEvict()
	{
		uint32 nPtr = mnClkPtr;

		do {
			nPtr = (nPtr + 1) % NUM_CACHED_MAP;
			MCacheEntry* pCE = mastMapCache + nPtr;
			if (FF16 == pCE->nGrpBase)
			{
				break;
			}
			else if ((0 == pCE->bDirty) && (0 == pCE->nLockCnt))
			{
				if (0 == pCE->nClock)
				{
					break;
				}
				pCE->nClock--;
			}
		} while (true);
		mnClkPtr = nPtr;
		return nPtr;
	}
	MCacheEntry* prepare(uint16 nGrp)
	{
		for (int i = 0; i < NUM_CACHED_MAP; i++)
		{
			MCacheEntry* pCE = mastMapCache + i;
			if (nGrp == pCE->nGrpBase)
			{
				pCE->nClock = INIT_LOCK_CNT;
				return pCE;
			}
		}
		uint32 nSlot = getEvict();
		MCacheEntry* pCE = mastMapCache + nSlot;
		pCE->bDirty = 0;
		pCE->nGrpBase = nGrp;
		pCE->nLockCnt = 0;
		pCE->nClock = INIT_LOCK_CNT;
		return pCE;
	}
public:
	MapCache() : mnClkPtr(0)
	{
		memset(mastMapCache, 0xFF, sizeof(mastMapCache));
	}

	void Lock(uint16 nGrp)	// Load with lock 1;
	{
		MCacheEntry* pCE = prepare(nGrp);
		pCE->nLockCnt++;
	}

	void UnLock(uint16 nGrp)
	{
		MCacheEntry* pCE = prepare(nGrp);
		ASSERT(pCE->nLockCnt > 0);
		pCE->nLockCnt--;
	}

	void SetupDirty(uint16 nGrp, bool bSet) // Must be Locked & loaded.
	{
		MCacheEntry* pCE = prepare(nGrp);
		pCE->bDirty = bSet ? 1 : 0;
	}

	uint16 GetDirty() // Get dirty, not locked, will clear dirty.
	{
		for (int i = 0; i < NUM_CACHED_MAP; i++)
		{
			MCacheEntry* pCE = mastMapCache + i;
			if ((pCE->bDirty) && (0 == pCE->nLockCnt))
			{
				pCE->bDirty = 0;
				return pCE->nGrpBase;
			}
		}
		return NUM_CACHED_MAP;
	}

};
#endif
