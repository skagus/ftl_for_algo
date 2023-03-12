#pragma once

#if 0
#define EN_MAP_CACHE			(0)

#define NUM_USER_BLK			(64)
#define PPN_PER_BLK				(32)
#define NUM_JNL_ENTRY			(32)
#define ENTRY_PER_MAP_GRP		(16)	///< Map slice당 entry개수. : 이건 size로 정의되어야 함.
#define NUM_CACHE_ENTRY			(PPN_PER_BLK*2)
#define GRP_PER_SAVE			(4)			///< Meta저장 한번에 같이 저장되는 meta slice개수.
#define NUM_LV					(3)
 
//#define ENTRY_PER_TRIM_GRP		(ENTRY_PER_MAP_GRP * 2)
#define BITS_PG_IN_BLK			(5)
static_assert((1 << BITS_PG_IN_BLK) == PPN_PER_BLK, "Check Bit count");
#define BITS_MU_IN_PAGE			(2)
static_assert((1 << BITS_MU_IN_PAGE) >= GRP_PER_SAVE, "Check Bit count");


#define NUM_TOTAL_PPN			(NUM_USER_BLK * PPN_PER_BLK)
#define NUM_TOTAL_LPN			(NUM_TOTAL_PPN * 3 / 4)
//#define NUM_TRIM_GRP			DIV_CEIL(NUM_TOTAL_LPN, ENTRY_PER_TRIM_GRP)

#define NUM_L0_ENTRY			NUM_TOTAL_LPN
#define NUM_L1_ENTRY			DIV_CEIL(NUM_TOTAL_LPN, ENTRY_PER_MAP_GRP)	//< L2P group count.
#define NUM_L2_ENTRY			DIV_CEIL(NUM_L1_ENTRY, ENTRY_PER_MAP_GRP)	//< map of L2P group count.
#define NUM_L3_ENTRY			DIV_CEIL(NUM_L2_ENTRY, ENTRY_PER_MAP_GRP)	//< map of map of L2P group count.

#define PPN_PER_META_BLK		(PPN_PER_BLK / 2)
#define NUM_META_BLK			(7)
#define NUM_NV_SAVE				(NUM_META_BLK * PPN_PER_META_BLK)

#define INV_PPA					(FF32)
#define GET_BLK(ppn)			((ppn) / PPN_PER_BLK)

#define IS_INV_ADDR(addr)		(addr.nDW == INV_PPA)
#define IS_VALID_ADDR(addr)		(addr.nDW != INV_PPA)

#define SET_GLPN(lv,lpn)		(((lv) << 24) | lpn)
#define GET_LV(laddr)			(laddr >> 24)
#define GET_ADDR(nVal)			((nVal) & (BIT(24)-1))
#define GET_BASE(lpn)			((lpn) - (lpn) % ENTRY_PER_MAP_GRP)

#endif


/////////////////////


