#pragma once
/****************************************************************************
간단한 Macro 모음
****************************************************************************/
#include <intrin.h>
#include <windows.h>
#include "types.h"

/// get size of array. 주의: dynamic allocated array는 적용안됨..
#define DIM(x)						(sizeof(x) / sizeof((x)[0]))
#define MIN(a, b)					(((a) < (b)) ? (a) : (b))
#define MAX(a, b)					(((a) > (b)) ? (a) : (b))

#define DIV_LOW(x, unit)			((x) / (unit))	   ///< div to lower bound.
#define DIV_UP(x,unit)				(((x)+(unit)-1) / (unit))	   ///< div to upper bound.
#define ALIGN_LOW(x, unit)			(DIV_LOW(x, unit) * (unit))	 ///< align to lower bound
#define ALIGN_UP(x, unit)			(DIV_UP(x, unit) * (unit))	  ///< align to upper bound
#define NOT(x)						(!(x))
#define ALIGN_DIFF(x, y, align)		(((align) + (x) - (y)) % (align))
#define ALIGN_INC(x, align)			(((x) + 1) % (align))
#define ALIGN_DEC(x, align)			(((align) + (x) - 1) % (align))

#define BIT(x)						((uint32)(1<<(x)))
#define BIT_CLR(x, bm)				((x) &= ~(bm))
#define BIT_SET(x, bm)				((x) |= (bm))
#define BIT_FLIP(y, mask)			((y) ^= (mask))
#define BIT_ALL(x)					(BIT(x)-1)

#define UNUSED(x)					(void)(x)

#define LID							(FILE_ID << 16 | __LINE__)	///< Source상의 위치.

/**
Overflow를 고려한 uint16 값의 비교.
실제로 두 값의 차이는 uint15 보다 작다는 조건 필요.
*/
inline int16 diff_u16(uint16 nBig, uint16 nSmall)
{
	const uint16 MAX_GAB_U16 = BIT(15);
	return ((nBig - nSmall) < MAX_GAB_U16) ? (nBig - nSmall) : -(int16)(nSmall - nBig);
}

inline int32 diff_u32(uint32 nBig, uint32 nSmall)
{
	const uint32 MAX_GAB_U32 = BIT(31);
	return ((nBig - nSmall) < MAX_GAB_U32) ? (nBig - nSmall) : -(int32)(nSmall - nBig);
}

inline int32 diff_u24(uint32 nBig, uint32 nSmall)
{
#define MASK(x)		(BIT_ALL(24) & (x))
	nBig = MASK(nBig);
	nSmall = MASK(nSmall);
	const uint32 MAX_GAB_U31 = BIT(23);
	uint32 nResult = ((nBig - nSmall) < MAX_GAB_U31) ? (nBig - nSmall) : -(int32)((nSmall - nBig));
	int32 nRet;
	if (nResult & BIT(23))
	{
		nRet = -(int32)(BIT(24) - MASK(nResult));
	}
	else
	{
		nRet = MASK(nResult);
	}
	return nRet;
}

inline uint8 BIT_SCAN_LSB(uint32 bitmap)
{
	uint32 pos;
//	ASSERT(bitmap != 0);
	_BitScanForward((DWORD*)&pos, bitmap);

	return (uint8)pos;
};

inline uint8 BIT_SCAN_MSB(uint32 bitmap)
{
#if 1
	for (int32 nIdx = 31; nIdx >= 0; nIdx--)
	{
		if (BIT(nIdx) & bitmap)
		{
			return (uint8)nIdx;
		}
	}
//	ASSERT(false);
	return 32;
#else // Not work.
	uint32 pos;
	ASSERT(bitmap != 0);
	_BitScanReverse((DWORD*)&pos, bitmap);
	return (uint8)pos;
#endif
}

template<typename T>
inline uint8 BIT_CLR_LSB(T& bitmap)
{
	uint32 pos;
//	ASSERT(bitmap != 0);
	pos = BIT_SCAN_LSB(bitmap);
	BIT_CLR(bitmap, BIT(pos));
	return (uint8)pos;
}

template<typename T>
inline uint8 BIT_CLR_MSB(T& bitmap)
{
	uint32 pos;
//	ASSERT(bitmap != 0);
	pos = BIT_SCAN_MSB(bitmap);
	BIT_CLR(bitmap, BIT(pos));
	return (uint8)pos;
}

template<typename T>
uint8 BIT_COUNT(T bitmap)
{
	uint32 pos = (uint32)__popcnt64((uint64)bitmap);
	return (uint8)pos;
}

template <typename T>
inline uint8 GetNextSet(T bmVal, uint8 nBit, uint8 nRange)
{
	do
	{
		nBit = (nBit + 1) % nRange;
	} while (0 == (BIT(nBit) & bmVal));
	return nBit;
}


#define CHRS_TO_INT(str)		(((uint32)str[3] << 24) | ((uint32)str[2] << 16) | ((uint32)str[1] << 8) | ((uint32)str[0] << 0))
/// Build time assert. (define을 검증하는데 사용가능)
#define BUILD_IF(cond)			static_assert(cond)



#ifdef _WIN32
#define _STR(x) #x
#define STR(x) _STR(x)
#define TODO(x)			 __pragma(message(__FILE__"("STR(__LINE__)",1): TODO: "#x))
#else
#define TODO(x)
#endif

template <int N>
struct Log2 {
	static const int value = Log2<N/2>::value + 1;
};

template <>
struct Log2<1> {
	static const int value = 0;
};

#define LOG2(x)		Log2<x>::value

#define CALC_BIT(cnt)		(LOG2(cnt) + 1)		///<




#undef FILE_ID
