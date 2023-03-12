#pragma once
/****************************************************************************

****************************************************************************/


#ifndef NULL
#define NULL		(nullptr)
#endif

#define FF08					((uint8) -1)
#define FF16					((uint16) -1)
#define FF32					((uint32) -1)
#define FF64					((uint64) -1)

#define KILO					(1024)			  ///< Kilo
#define MEGA					(KILO * KILO)	   ///< Mega.
#define GIGA					(MEGA * KILO)	   ///< Giga.

typedef signed char				int8;
typedef short					int16;
typedef int						int32;
typedef long long				int64;
typedef unsigned char			uint8;
typedef unsigned short			uint16;
typedef unsigned int			uint32;
typedef unsigned long long		uint64;

