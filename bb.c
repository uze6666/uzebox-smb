/*
 * The block buffer in SPI RAM. The 6502 kept two 16-column buffers of 13 rows in its RAM
 * ($0500, $05d0); here the 32 columns live in the console's 128K SPI RAM (bank 0, 16 bytes a
 * column), and a few recently used columns are cached in SRAM. GetBlockBufferAddr points
 * BBPtr at a cached column, so the ported code reads BBPtr[row] as it read ($06),y; a write
 * goes through bb_set(), which also writes the SPI RAM.
 */
#include <string.h>
#include <spiram.h>
#include "smb.h"

#define BB_BANK  0
#define BB_ADDR(c) ((u16)(c) << 4)
/* The view: each column's metatiles as the parser made them, scenery included (the block
** buffer keeps 0 for what nothing collides with), for drawing. A write to the block buffer
** goes to the view too: what the game changes, the picture shows.
** On mode 17 it does not: there the view is the NES's name table, written by the parser and by
** the platform's routines where the 6502 wrote it (gfx17.c, bb_PutViewCell), and by nothing
** else. Two of the engine's writes to the block buffer had no name table write: a grown
** vine's climbable blank, over whatever scenery is behind it, and the cover under the player
** at a vine's top; mode 17 draws the columns that come back into its view from this store,
** and would show them */
#define VIEW_ADDR(c) (BB_COLS * 16 + ((u16)(c) << 4))
#ifndef BB_CACHE
	#define BB_CACHE 4
#endif

	#define cache ((u8 (*)[13])TAIL_BB_CACHE)
	TAIL17_FITS(TAIL_BB_CACHE, BB_CACHE * 13, M17_GAME_AT + TAIL17_SIZE);
static u8 tag[BB_CACHE];           /* the column cached, 0xff none */
static u8 next;                    /* the entry to evict next */
static u8 cur;                     /* the entry BBPtr points at */
u8 *BBPtr;

/* No column cached, every cached byte 0 */
static void cache_clear(void){
	memset(tag, 0xff, sizeof tag);
	memset(cache, 0, BB_CACHE * 13);
}

void bb_Init(void){
	SPCR = (1 << SPE) | (1 << MSTR);
	SPSR = (1 << SPI2X);
	DDRB |= (1 << PB7) | (1 << PB5);
	PORTA |= (1 << PA4);               /* the SPI RAM's chip select, high */
	DDRA |= (1 << PA4);
	PORTD |= (1 << PD6);               /* the SD card's, high: no card selected */
	DDRD |= (1 << PD6);
	cache_clear();
	BBPtr = cache[0];
}

#if TWO_PLAYERS
/* The players' own bytes while the other plays (player.c's TransposePlayers): a page of bank
** 1, which the block store does not use, under the place of the .uze's data (sd17.c: from
** 0x7FFC) */
#define PARK_BANK 1
#define PARK_AT   0x7f00
void bb_Park(u8 at, u8 *p, u8 n, bool store){
	(store ? SpiRamWriteFrom : SpiRamReadInto)(PARK_BANK, PARK_AT + at, p, n);
}
#endif

/* Every column blank (InitializeMemory cleared the 6502's buffers with the rest of its RAM) */
void bb_Clear(void){
	u8 z[16];
	memset(z, 0, sizeof z);
	for(u8 c = 0; c < 2 * BB_COLS; c++) SpiRamWriteFrom(BB_BANK, BB_ADDR(c), z, 16);
	cache_clear();
}

/* The entry caching column c (0-31), BB_CACHE if none (a column is in one entry at most) */
static u8 find(u8 c){
	u8 i = 0;
	while(i < BB_CACHE && tag[i] != c) i++;
	return i;
}

/* The cache entry of column c, loaded if needed; never the entry BBPtr is on unless it is c */
static u8 entry(u8 c){
	u8 i = find(c);
	if(i < BB_CACHE) return i;
	i = next;
	if(i == cur) i = (u8)(i + 1) % BB_CACHE;
	next = (u8)(i + 1) % BB_CACHE;
	tag[i] = c;
	SpiRamReadInto(BB_BANK, BB_ADDR(c), cache[i], 13);
	return i;
}

/* GetBlockBufferAddr's work: column c (0-31) current, BBPtr on it */
void bb_Select(u8 c){
	c &= BB_COLS - 1;
	cur = entry(c);
	BBPtr = cache[cur];
}

/* Column c's row r in the SPI RAM, the block buffer's and the view's */
static void put2(u8 c, u8 r, u8 v){
	u16 a = BB_ADDR(c) + r;
	SpiRamWriteU8(BB_BANK, a, v);
}

/* A write to the current column, row r (0-12) */
void bb_Set(u8 r, u8 v){
	BBPtr[r] = v;
	put2(tag[cur], r, v);
}

/* Any column's row, read or written (the renderer, the bridge, the rope) */
u8 bb_Get(u8 c, u8 r){
	c &= BB_COLS - 1;
	u8 i = find(c);
	if(i < BB_CACHE) return cache[i][r];
	return SpiRamReadU8(BB_BANK, BB_ADDR(c) + r);
}

void bb_Put(u8 c, u8 r, u8 v){
	c &= BB_COLS - 1;
	u8 i = find(c);
	if(i < BB_CACHE) cache[i][r] = v;
	put2(c, r, v);
}

/* The parser's whole column, scenery included, into the view */
void bb_PutView(u8 c, const u8 *src){
	SpiRamWriteFrom(BB_BANK, VIEW_ADDR(c & (BB_COLS - 1)), (void *)src, 13);
}

/* A metatile of the view */
u8 bb_GetView(u8 c, u8 r){
	return SpiRamReadU8(BB_BANK, VIEW_ADDR(c & (BB_COLS - 1)) + r);
}

/* A cell of the view alone: the platform's, where the 6502 wrote the name table */
void bb_PutViewCell(u8 c, u8 r, u8 v){
	SpiRamWriteU8(BB_BANK, VIEW_ADDR(c & (BB_COLS - 1)) + r, v);
}

/* A whole column of the view, 13 metatiles, for drawing it */
void bb_GetViewColumn(u8 c, u8 *dst){
	SpiRamReadInto(BB_BANK, VIEW_ADDR(c & (BB_COLS - 1)), dst, 13);
}

/* A whole column (the parser's), 13 bytes */
void bb_PutColumn(u8 c, const u8 *src){
	c &= BB_COLS - 1;
	u8 i = find(c);
	if(i < BB_CACHE) memcpy(cache[i], src, 13);
	SpiRamWriteFrom(BB_BANK, BB_ADDR(c), (void *)src, 13);
}

/* A whole column into dst, for drawing it */
void bb_GetColumn(u8 c, u8 *dst){
	c &= BB_COLS - 1;
	u8 i = find(c);
	if(i < BB_CACHE) memcpy(dst, cache[i], 13);
	else SpiRamReadInto(BB_BANK, BB_ADDR(c), dst, 13);
}
