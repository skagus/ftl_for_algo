#pragma once

#include "types.h"

#if (EN_MAP_CACHE == 1)
#define INIT_LOCK_CNT		(2)
#define NUM_CACHED_MAP		(16)

struct MCacheEntry
{
	uint16 nGrpBase;		///< Map group number.
	uint8 nLockCnt : 6;	///< refence count, don't evict if non-zero.
	uint8 nClock : 4;	///< Current token used in clock algorithm
	uint8 bDirty : 1;	///< Dirty will be flush next time.
};
#endif
