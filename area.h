/* area.h -- the area parser (area.c): a port of AreaParserCore and its objects */
#pragma once

extern u8 *BBPtr;                       /* $06-$07: the cached column GetBlockBufferAddr chose (bb.c) */
void bb_Init(void);                     /* the SPI bus for the SPI RAM */
void bb_Clear(void);                    /* every column blank */
void bb_Select(u8 c);                   /* column c (0-31) current: BBPtr on it */
void bb_Set(u8 r, u8 v);                /* a write to the current column's row r */
u8 bb_Get(u8 c, u8 r);
void bb_Put(u8 c, u8 r, u8 v);
#if TWO_PLAYERS
/* n bytes at p to place at (0-255) of the players' own in SPI RAM (store), or from it */
void bb_Park(u8 at, u8 *p, u8 n, bool store);
#endif
void bb_PutColumn(u8 c, const u8 *src);
void bb_PutView(u8 c, const u8 *src);       /* the column as drawn, scenery included */
u8 bb_GetView(u8 c, u8 r);
void bb_GetColumn(u8 c, u8 *dst);
extern const u8 Bitmasks[8] PROGMEM;
extern const u8 BrickQBlockMetatiles[14] PROGMEM;

void GetBlockBufferAddr(void);          /* A_: the column 0-31 -> M06, BBPtr */
void KillEnemies(void);                 /* A_: the enemy identifier; leaves no register */
void AreaParserCore(void);
void IncrementColumnPos(void);
void LoadAreaPointer(void);
void GetAreaDataAddrs(void);
extern const u8 WorldAddrOffsets[8] PROGMEM;   /* levels.h, defined in area.c */
extern const u8 AreaAddrOffsets[36] PROGMEM;
