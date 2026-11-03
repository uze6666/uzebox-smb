/*
 *  Uzebox Kernel - Video Mode 17
 *  Copyright (C) 2026 Alec Bourque
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Uzebox is a reserved trade mark
*/

/**
 * ===========================================================================
 * Function prototypes for video mode 17
 * ===========================================================================
 */

#pragma once

#include <avr/io.h>

/* What the frame reads when it starts (videoModeCore.s holds it; m17.def.h's M17_SCR_ bytes) */
typedef struct{
	unsigned char scrollX;      /* the playfield's x scroll in pixels: 0-255, round VRAM's 32 columns */
	unsigned char tileBank;     /* the playfield's flash tiles: tile 0 of its 192 is tile 8 x tileBank */
	unsigned char hudTileBank;  /* the HUD's */
	unsigned char spriteX;      /* the sprites' origin: the picture's first pixel is the list's */
	unsigned char spriteY;      /* (spriteX, spriteY) */
} ScreenType;

extern ScreenType Screen;

/* VRAM is the playfield's rows, 8 a page: cell (x, y) is vram[(y >> 3) * 256 + 8 * x + (y & 7)],
** y 0 to M17_PF_ROWS - 1. A cell holds 64 + a flash tile of the playfield's 192 (0-63 are RAM
** tiles, which are the mode's to hand out) */
extern unsigned char vram[];

/* The HUD's row (M17_HUD: the picture's first row, which does not scroll): m17_hud[x], x 0 to
** 27, each 64 + a flash tile of the HUD's 192. A byte a column, in the page of one of the
** mode's tables: M17_HUD_COLS cells are its memory, and no more */
extern unsigned char m17_hud[];

/* The flash tiles: 32 bytes each (generators/m17tiles.py), the table on a 256-byte boundary */
void SetTileTable(const char *data);

/* A cell of the playfield: x 0-31, y a VRAM row, tileId 0-191 a flash tile of its 192 */
void SetTile(char x, char y, unsigned int tileId);
unsigned int GetTile(char x, char y);

/* Every cell flash tile 0 of its section's 192: the playfield's, and the HUD's row */
void ClearVram(void);

/* The colours, BBGGGRRR: the backdrop, value 0 of every palette; and values 1-3 of palette p
** (0 to M17_PALETTES - 1). Flash tiles show a change in the next picture. RAM tiles (the cells
** under sprites) show the screen's 15 colours, which these two calls have made again from the
** palettes when the sprites are next composed: palette writes first, then the composition */
void m17_SetBackdrop(unsigned char color);
void m17_SetPalette(unsigned char p, unsigned char c1, unsigned char c2, unsigned char c3);

/* One colour, value v (1-3) of palette p: for a colour that cycles. A fifth of the time the
** whole table takes, which is not made again unless this is a 16th colour. RAM tiles show it
** from the next composition; at once when no other value had the colour it replaces and none
** has the new one */
void m17_SetPaletteColor(unsigned char p, unsigned char v, unsigned char color);

/* Palette colours that found no place among the screen's 15 when they were last made (0 when
** all is well): in a RAM tile each shows as the nearest of the 15 */
extern unsigned char m17_colors_over;

/* The sprites: the NES's list. Entry 0 is drawn over the others. A y of M17_SPRITE_OFF or more
** is not shown. x and y are the program's own: the picture's are x - Screen.spriteX and
** y - Screen.spriteY, as whole numbers (a sprite up to 7 pixels left of the picture or above
** it shows its part that is inside; an x of 249 at an origin of 0 is right of the picture,
** not left of it). So a list kept in a screen's coordinates is shown through a window that
** moves over it, with no pass over its entries. A sprite is drawn over the HUD's row as over
** the playfield (2026-10-06): its lines there are composed into the row's cells, m17_hud */
struct m17_SpriteStruct{
	unsigned char y;
	unsigned char tile;         /* of the sprite tiles */
	unsigned char attr;         /* its palette is M17_SPRITE_PAL + bits 0-1; and the bits below */
	unsigned char x;
};
#define M17_SPRITE_BEHIND 0x20  /* behind the background: its pixels show where that is the backdrop */
#define M17_SPRITE_FLIP_X 0x40
#define M17_SPRITE_FLIP_Y 0x80
#define M17_SPRITE_OFF    0xEF
extern struct m17_SpriteStruct m17_sprites[M17_SPRITES];

/* The sprite tiles: 16 bytes each, 8 lines of 4 pixels a byte, 2 bits a pixel, the left one
** low, 0 transparent; and each tile's opaque box, 2 bytes (generators/m17tiles.py makes both) */
void m17_SetSpriteTiles(const char *tiles, const char *boxes);

/* A flash tile of the playfield's 192 whose 32 bytes are all the same (the sky): a cell that
** shows it is given its RAM tile faster. 0xFF: none (as at the start) */
void m17_SetBlankTile(unsigned char tileId);

/* The sprites into RAM tiles, for the coming picture: called in the blank, when the list, VRAM
** and the scroll are that picture's. A cell under a sprite takes a RAM tile; when they run out
** the sprites that go without change at every call, and m17_cells_short counts their cells.
** A picture that starts before the end is drawn with the sprites drawn till then (the last
** entries first), the background whole; the rest follows in the next blank. The value returned
** is then not 0: the next picture shows every sprite, if nothing else is started before it.
** (r25 has the sprites that took a turn in this call, for a program that declares the routine
** as returning 16 bits; m17_cut is 0xFF when none was left waiting for the next call.)
** Not to be called again before it returns (a program whose frame is the kernel's vsync
** callback guards against the next blank's call) */
unsigned char m17_ComposeSprites(void);
extern unsigned char m17_cells_short;
/* Who gets the RAM tiles, and a turn under the cap, before the others: three classes. First
** the build's M17_FIRST_N entries from M17_FIRST_AT (m17.def.h: none unless given); last the
** sprites whose tile is marked (bit 7 of its box's second byte: generators/m17tiles.py's
** sprite_box(pix, last)); the rest between them. Within a class, what goes without changes at
** every call */

/* Not 0: a picture that starts now is not drawn (the screen is black for it, the sync goes
** on). The program's own, for a screen it is still writing */
extern volatile unsigned char m17_hold;

/* The picture in bands (m17.def.h's M17_BANDS): the program's leave. While it is given, a
** picture leaves its rows of black to the kernel's own lines, and the main program runs in
** their time: so from m17_GiveLeave() to m17_TakeLeave() the program writes nothing a picture
** reads: VRAM, the HUD's row, the palettes and the two tables made from them, Screen, the tile
** table, the blank tile, the RAM tiles; it does not compose the sprites (the sprite list itself
** is read by the composition alone: it may be written); and the kernel's render window is the
** mode's own (FIRST_RENDER_LINE, FRAME_LINES). Give it when the frame is whole, after
** m17_ComposeSprites(), and take it back before the next frame's first write. Not given (as
** at the start), every picture is one long interrupt. One instruction each (sbi, cbi), which
** an interrupt cannot cut.
** A picture drawn so sets the vsync flag in its last line when that line is the kernel's (a
** row of black ends the picture), a line sooner than one that draws it.
** Taking the leave back stops no picture that is under way: a picture asks for the leave
** once, at its first line, and its rows of black go to the kernel to its end whatever the
** leave is by then. So a program whose work may still be running when a picture starts asks
** m17_InPicture() before a frame's first write, and where it is not 0 waits for that picture's
** end (the vsync flag) first. It is not 0 from the end of a picture's band to that picture's
** end, the only time the main program runs inside a picture; 0 in the blank, and always in a
** program that never gives the leave */
static inline void m17_GiveLeave(void){ GPIOR0 |= (unsigned char)(1 << M17_LEAVE_BIT); }
static inline void m17_TakeLeave(void){ GPIOR0 &= (unsigned char)~(1 << M17_LEAVE_BIT); }
static inline unsigned char m17_InPicture(void){ return GPIOR0 & (unsigned char)(1 << M17_GAP_BIT); }

/* The RAM tiles, 32 bytes each, a byte a pair of the screen's colours (m17_tables.h). The
** first m17_ram_used are on screen for the frame, RAM tile n in the cell m17_restore[n] names;
** the frame puts each cell's flash tile back when it is done, so VRAM never shows a program a
** RAM tile. m17_ram_used is 0 while the sprites are being composed. They are the sprites': a
** program does not write them */
struct m17_RestoreStruct{
	unsigned char *cell;        /* the VRAM cell */
	unsigned char tile;         /* what it held, kept by the frame */
};
extern unsigned char m17_ram_tiles[];
extern struct m17_RestoreStruct m17_restore[];
extern unsigned char m17_ram_used;
