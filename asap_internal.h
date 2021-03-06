#ifndef _ASAP_INTERNAL_H_
#define _ASAP_INTERNAL_H_

#include "config.h"
#include <string.h>	/* memcpy, memset */

#ifndef FALSE
#define FALSE  0
#endif
#ifndef TRUE
#define TRUE   1
#endif

/* SBYTE and UBYTE must be exactly 1 byte long. */
/* SWORD and UWORD must be exactly 2 bytes long. */
/* SLONG and ULONG must be exactly 4 bytes long. */
#define SBYTE signed char
#define SWORD signed short
#define SLONG signed int
#define UBYTE unsigned char
#define UWORD unsigned short
#define ULONG unsigned int

#define Util_malloc malloc

/* Current clock cycle in a scanline.
   Normally 0 <= xpos && xpos < LINE_C, but in some cases xpos >= LINE_C,
   which means that we are already in line (ypos + 1). */
extern int ANTIC_xpos;

/* xpos limit for the currently running 6502 emulation. */
extern int ANTIC_xpos_limit;

/* Number of cycles per scanline. */
#define LINE_C   114

/* STA WSYNC resumes here. */
#define ANTIC_WSYNC_C  106

/* Number of memory refresh cycles per scanline.
   In the first scanline of a font mode there are actually less than DMAR
   memory refresh cycles. */
#define DMAR     9

extern int ANTIC_wsync_halt;

#define SndSave_CloseSoundFile()

#define MEMORY_dGetByte(x)				(memory[x])
#define MEMORY_dPutByte(x, y)			(memory[x] = y)

#ifndef WORDS_BIGENDIAN
#ifdef WORDS_UNALIGNED_OK
#define MEMORY_dGetWord(x)				(*(const UWORD *) &memory[x])
#define MEMORY_dPutWord(x, y)			(*(UWORD *) &memory[x] = (y))
#define MEMORY_dGetWordAligned(x)		MEMORY_dGetWord(x)
#define MEMORY_dPutWordAligned(x, y)	MEMORY_dPutWord(x, y)
#else	/* WORDS_UNALIGNED_OK */
#define MEMORY_dGetWord(x)				(memory[x] + (memory[(x) + 1] << 8))
#define MEMORY_dPutWord(x, y)			(memory[x] = (UBYTE) (y), memory[(x) + 1] = (UBYTE) ((y) >> 8))
/* faster versions of dGetWord and dPutWord for even addresses */
/* TODO: guarantee that memory is UWORD-aligned and use UWORD access */
#define MEMORY_dGetWordAligned(x)		MEMORY_dGetWord(x)
#define MEMORY_dPutWordAligned(x, y)	MEMORY_dPutWord(x, y)
#endif	/* WORDS_UNALIGNED_OK */
#else	/* WORDS_BIGENDIAN */
/* can't do any word optimizations for big endian machines */
#define MEMORY_dGetWord(x)				(memory[x] + (memory[(x) + 1] << 8))
#define MEMORY_dPutWord(x, y)			(memory[x] = (UBYTE) (y), memory[(x) + 1] = (UBYTE) ((y) >> 8))
#define MEMORY_dGetWordAligned(x)		MEMORY_dGetWord(x)
#define MEMORY_dPutWordAligned(x, y)	MEMORY_dPutWord(x, y)
#endif	/* WORDS_BIGENDIAN */

extern UBYTE memory[65536 + 2];

#define MEMORY_GetByte(addr)			(((addr) & 0xf900) == 0xd000 ? ASAP_GetByte(addr) : memory[addr])
#define MEMORY_PutByte(addr, byte)		do { if (((addr) >> 8) == 0xd2) ASAP_PutByte(addr, byte); else memory[addr] = byte; } while (0)

/* Reads a byte from the specified special address (not RAM or ROM). */
UBYTE ASAP_GetByte(UWORD addr);

/* Stores a byte at the specified special address (not RAM or ROM). */
void ASAP_PutByte(UWORD addr, UBYTE byte);

void ASAP_CIM(void);

#endif /* _ASAP_INTERNAL_H_ */
