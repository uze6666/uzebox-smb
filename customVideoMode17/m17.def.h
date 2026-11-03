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
 *
 * Video mode 17's own defines (working name). C and assembly both read this.
 *
 * 8 x 8 tiles in pixel pairs, 6 cycles a pixel, 28 columns shown of a VRAM 32
 * wide: a HUD row that stays put over a playfield that scrolls sideways. Flash tiles
 * carry a palette in their bytes; RAM tiles hold the screen's own colours, for
 * the cells sprites are composed into.
 *
 * Build options:
 *   M17_ROWS       rows shown, the HUD's and the playfield's together (25; up to 28)
 *   M17_HUD        1: the first row is the HUD's, which does not scroll; 0: none (1)
 *   M17_RAM_TILES  RAM tiles (44; up to 64)
 *   M17_SPRITES    entries in the sprite list (64)
 *   M17_DRAW_MAX   sprites a composition draws (24): its time's bound
 *   M17_BANDS      1: the picture's rows of blank go to the main program (0: below)
 *
 * ===========================================================================
 */

#pragma once

#define M17_TILE_W 8
#define M17_TILE_H 8
#define M17_COLS   28           /* columns shown */
#define M17_VRAM_H 32           /* columns VRAM holds: a row wraps round them */

#ifndef M17_ROWS
	#define M17_ROWS 25
#endif
#if M17_ROWS < 1 || M17_ROWS > 28
	#error M17_ROWS is 1 to 28
#endif

/* The HUD: one row at the top of the picture, or none. Its cells are not VRAM's: they are the
** M17_HUD_COLS bytes that follow table T2 in its page, a byte a column (28 are shown; the
** frame reads the two after them, which show nothing) */
#define M17_HUD_OFS  226        /* T2's own bytes: m17_tables.h's M17_PAIRS15 + 1 */
#define M17_HUD_COLS (256 - M17_HUD_OFS)

/* The playfield's rows, which are VRAM's: 8 rows a page, vram[(y >> 3) * 256 + 8 * x + (y & 7)],
** so a column step is 8 in the pointer's low byte and a row wraps by itself */
#define M17_PF_ROWS (M17_ROWS - M17_HUD)
#if M17_PF_ROWS < 1
	#error M17_ROWS leaves the playfield no row
#endif
#define M17_VRAM_PAGES ((M17_PF_ROWS + 7) / 8)
#define M17_VRAM_SIZE  (256 * M17_VRAM_PAGES)
#define M17_VRAM_V     (8 * M17_VRAM_PAGES)

/* VRAM indexes 0-63 are RAM tiles, 64-255 flash tiles: flash tile index - 64 + 8 x the bank */
#define M17_RAM_INDEXES 64
#ifndef M17_RAM_TILES
	#define M17_RAM_TILES 44
#endif
#if M17_RAM_TILES < 1 || M17_RAM_TILES > M17_RAM_INDEXES
	#error M17_RAM_TILES is 1 to 64
#endif
#define M17_TILE_BYTES 32       /* a tile: 8 lines of 4 bytes, a byte 2 pixels */

/* The sprite list's entries, 4 bytes each: the NES's 64 */
#ifndef M17_SPRITES
	#define M17_SPRITES 64
#endif
#if M17_SPRITES < 1 || M17_SPRITES > 64
	#error M17_SPRITES is 1 to 64
#endif

/* The sprites one composition gives tiles to and draws: about 2,400 cycles each, so this is
** what bounds the time a composition takes. When more are in the picture the others wait for
** the next call, which starts with them */
/* The sprite list's entries of the compositor's first class: M17_FIRST_N of them from entry
** M17_FIRST_AT. They get their RAM tiles, and their turn under the cap, before the others */
#ifndef M17_FIRST_AT
	#define M17_FIRST_AT 0
#endif
#ifndef M17_FIRST_N
	#define M17_FIRST_N 0
#endif

/* M17_DRAW_VAR=1: the cap is a variable, m17_draw_max (M17_DRAW_MAX at the start), which a
** program lowers for a composition it has less time for; the sprites past it take turns */
#ifndef M17_DRAW_MAX
	#define M17_DRAW_MAX 24
#endif
#if M17_DRAW_MAX < 1 || M17_DRAW_MAX > M17_SPRITES
	#error M17_DRAW_MAX is 1 to M17_SPRITES
#endif

/* M17_SPR_NONE: a sprite tile the program promises has no pixel (256, the default: it names
** none). The compositor's second pass passes an entry of the list that has this tile as it
** does one that is off: such an entry draws nothing either way, and a game's list can have a
** dozen of them on (the halves of an object that are blank). The first pass needs no such
** word (it reads the tile's opaque box first, which is empty). A tile named here that has a
** pixel is not drawn: the program's generator is what must hold it empty */

/* The palettes: 3 colours each over the shared backdrop. Table T1 holds them, 16 entries a
** palette and one more, and any flash tile byte can name any of them. Their colours are the
** screen's: the 15 a RAM tile can show (table T2) are made from them.
** A flash tile a sprite is drawn over uses one of the first M17_BG_PALETTES; a sprite uses
** M17_SPRITE_PAL + its attributes' low 2 bits. (The NES's 4 and 4) */
#define M17_PALETTES    8
#define M17_BG_PALETTES 4
#define M17_SPRITE_PAL  4

/* T1's page past the table: a flash byte of the first M17_BG_PALETTES palettes as a RAM tile's
** byte (64); a sprite tile's nibble in each sprite palette as two slots, left << 4 | right (64).
** They share a byte: the sprites' first entry is two transparent pixels, which nothing reads */
#define M17_XLAT_OFS (16 * M17_PALETTES + 1)
#define M17_SPRX_OFS (M17_XLAT_OFS + 16 * M17_BG_PALETTES - 1)
#if M17_SPRX_OFS + 16 * 4 != 256
	#error the two translation tables do not end the page of T1
#endif

#define M17_FRAME_LINES (8 * M17_ROWS)
#define M17_FIRST_LINE  (20 + (224 - M17_FRAME_LINES) / 2)

/* M17_BANDS=1: the picture in bands. A row of the playfield whose cells in sight (the 28, and
** the one a fine scroll brings in) all hold the tile the program named as blank, that tile
** showing black twice, is 8 lines of black: the very lines the kernel's sync interrupt makes
** by itself in the blank. The frame leaves such rows to it, through the kernel's render window
** (first_render_line, render_lines_count), and the main program has their time: about 12,400
** cycles a row, 8 lines of blank's worth. The picture is the same, and every sync edge is on
** its cycle. Only when the program gives its leave (videoMode.h): the main program then runs
** while a picture is drawn, and must write nothing that picture reads. Without the leave the
** picture is one long interrupt, as it is without the option.
** The frame's state is the three general purpose bytes of the I/O space, GPIOR0 to 2, which
** are the mode's own in such a build: no SRAM. The leave is GPIOR0's bit M17_LEAVE_BIT; its
** bit M17_GAP_BIT is set while a run of a picture's rows is the kernel's, which is the only
** time the main program runs inside a picture (videoMode.h's m17_InPicture()) */
#define M17_LEAVE_BIT 7
#define M17_GAP_BIT   0

/* The Screen structure's bytes (videoMode.h's ScreenType, videoModeCore.s's storage) */
#define M17_SCR_SCROLLX  0      /* the playfield's x scroll, in pixels */
#define M17_SCR_BANK     1      /* the playfield's flash tiles: 8 tiles a step */
#define M17_SCR_HUDBANK  2      /* the HUD's */
#define M17_SCR_SPRITEX  3      /* the sprites' origin: the list's x and y of the picture's */
#define M17_SCR_SPRITEY  4      /* first pixel */
#define M17_SCR_SIZE     5

/* The run: all of the mode's SRAM, one section (.m17run) whose parts come in this order, each
** of the first three a page. The program's Makefile places it on a page boundary and starts
** .data after it:
**     -Wl,--section-start=.m17run=0x800100 -Wl,--section-start=.data=<0x800100 + M17_RUN_SIZE>
** A program that needs one of the parts at a constant address (a sprite list that is its own
** page of memory too) takes that address + the part's offset. The C start-up does not clear
** the run: the mode does, when it is initialised */
#define M17_RUN_T2       0      /* table T2, then the HUD's row (M17_HUD_OFS) */
#define M17_RUN_T1       256    /* table T1 and C's two translations */
#define M17_RUN_SPRITES  512    /* the sprite list */
#define M17_RUN_VRAM     768
#define M17_RUN_TILES    (M17_RUN_VRAM + M17_VRAM_SIZE)
#define M17_RUN_RESTORE  (M17_RUN_TILES + M17_TILE_BYTES * M17_RAM_TILES)
#define M17_RUN_STATE    (M17_RUN_RESTORE + 3 * M17_RAM_TILES)
#define M17_STATE_SIZE   (5 + M17_SCR_SIZE + 16)   /* the frame's bytes, Screen, the compositor's */
#define M17_RUN_SIZE     (M17_RUN_STATE + M17_STATE_SIZE)
