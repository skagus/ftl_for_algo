#pragma once

#include "types.h"
#include "ftl.h"

struct TreeEntry
{
	uint32 nLPN;
	VAddr stNA;
};

struct PendCount
{
	uint32 nGrpBase;
	uint16 nCount;
	uint32 nMtAge;
};

class MapTree
{
	friend void DBG_CheckVPC();
	friend void MT_Flush();
protected:
	uint32 mnValid;
	TreeEntry maMap[NUM_CACHE_ENTRY];
public:

	virtual void DbgCheck() = 0;
	virtual bool DbgCompare(MapTree* pThat) = 0;
	virtual void Print() = 0;

	virtual uint32 CountFree() = 0;
	virtual uint32 GetMaxDirty(uint32* pnNum) = 0;
	virtual uint32 GetMinAge(uint32* pnGrpBase) = 0;
	virtual bool AddEntry(uint32 nLPN, VAddr stDst, BlkInfo* maBI) = 0;
	virtual VAddr Search(uint32 nLPN) = 0;
	virtual uint32 Flush(uint32 nBaseLPN, uint32 nRange, VAddr* astL2P, BlkInfo* maBI) = 0;
};

MapTree* MakeMapTree(uint32 nGrpSize);
