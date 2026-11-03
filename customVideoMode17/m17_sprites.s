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

;=============================================================================
; Video mode 17's compositor: the sprite list into RAM tiles. C-callable,
; m17_ComposeSprites(): a program calls it in the blank, once its sprite list,
; VRAM and scroll are the coming picture's. generators/m17model.py's compose()
; does the same on the PC, byte for byte.
;
; The two passes are files of their own, included here in parts where their code
; goes: m17_pass1.s and m17_pass2.s. This file has what both use: the call's start
; and end, the hand-over between the passes.
;
; Pass 1, the tiles, from the entry m17_rotate: each cell a sprite tile's
; opaque box reaches (up to 4, in the playfield and in the picture) that shows
; a flash tile gets a RAM tile while there are some: the cell and its flash
; tile go to the restore list, the flash tile is copied through its palette's
; translation (the blank tile, if the program named one, is a fill), the cell
; shows the RAM tile. A call takes M17_DRAW_MAX sprites at most: the pass ends at
; the one after them. The first entry that went without, a tile or its turn,
; starts the next call's pass, so what is missing changes.
;
; A sprite behind the background shows nothing in a cell whose flash tile has
; art in every pixel. The program's tile table has those tiles in one run, from
; m17_solid_lo to m17_solid_hi (labels round their bytes; weak here: without
; them the run is empty). Such a cell gets no RAM tile for such a sprite, and a
; sprite counts for the cap only when one of its cells has a RAM tile, gets one
; or goes without: a plant down in its pipe takes no tile and no turn, and pass
; 2 finds nothing to draw it into. (They were 10 of a frame's 25 sprites in a
; level of pipes, 12 of its 30 RAM tiles, and a third of its composition.)
;
; The HUD's row (M17_HUD) is drawn over as the playfield is: a sprite's lines
; in the picture's first 8 are composed into the row's own cells (m17_hud, a
; byte a column), whose flash tiles are the HUD's bank's. The row does not
; scroll, so the sprite's columns there, and in pass 2 its place in their
; bytes, are made again from its x alone.
;
; The pass takes the sprites a class at a time, so the tiles and the turns go
; to the first class before the others: class 0 the entries the build named
; (M17_FIRST_N of them from M17_FIRST_AT: none unless given), class 2 the
; sprites whose tile is marked last (bit 7 of its box's second byte), class 1
; the rest. The order they are taken in is that of three whole trips through
; the list from the entry m17_rotate, a class each; the trips themselves look
; at no entry twice for nothing (m17_pass1.s's header has them).
;
; Pass 2, the pixels, from the last entry to entry 0 (which ends on top), of
; the entries pass 1 came to. A line of a sprite is 2 bytes, 4 pixel pairs. At an odd x the line moves a
; pixel right and is 5 pairs, so a pair of the sprite is always a pair of the
; tile. A pair's nibble (the left pixel in bits 0-1) gives its two slots
; through the palette's translation; it is written whole, or merged into the
; byte's own pair when one of its pixels is transparent. Flipped in x, the
; pairs are taken from the last and each turned round. (The codes of a
; palette's 15 nibbles are made once, turned round or not, and kept in a
; frame on the stack while the pass runs: m17_pass2.s's header.) A sprite behind the
; background writes the background's own pixel where that is art. A row of
; cells that shows nothing but the blank tile has no such pixel: there its
; lines go by the pairs of a sprite in front (m17_p2_under asks the restore
; list, once a row).
;
; VRAM holds the RAM tiles' indexes while this runs and its flash tiles again
; when it returns: the frame puts the RAM tiles in for itself.
;
; A picture that starts before the end is drawn all the same, with the sprites
; drawn till then. m17_ram_used is 0 from the start to the pixels' end, so the
; frame leaves VRAM as the compositor has it; a cell shows its RAM tile only
; once that tile is whole; every other write is one byte, a pair. The count is
; given to the frame before VRAM gets its flash tiles back, so a picture from
; then on shows every sprite. The value returned is not 0 when a picture came
; before the pixels were done: the program lets the next one show them all.
;
; Registers, kept through both passes:
;   r2  the x scroll          r3  the sprites' origin, x  r5  the flash tiles, high byte
;   r6  0
;   pass 1: its registers are in m17_pass1.s's header. It is handed r7 (0), r14:r15 (the
;   restore list's start) and r25 (0), and hands on at m17_p1_end r25 the sprites that took
;   a turn and r7 the RAM tiles handed out, with m17_cut, m17_cells_short and m17_next
;   pass 2: r8 the sprite list, where it is (its low byte: the list is a page); r10 y, r11
;   tile, r12 attributes, r13 x: the entry, its y and x the picture's (each pass makes them
;   in line, and leaves an entry that is out of play there)
;   pass 2: r7 the blank tile as a cell holds it when its byte has no art (0: none:
;   m17_p2_under; the entries left are the list pointer's low byte), r9 and r11 the sprite's
;   table of codes, r12's bit 2 an odd x and its bit 3 a row of cells that goes by the
;   slower pairs of a sprite behind the background, r13 the first cell's column times 8, r4
;   lines left, r14:r15 the tile's line, r20:r21 and r22:r23 where the line goes in the two
;   cells' RAM tiles (a high byte of 0: none, or no pair goes there), r24 and r25 the pairs
;   for each, T flipped in x
;=============================================================================

.global m17_ComposeSprites
.global m17_sprites
.global m17_spr_tiles
.global m17_spr_boxes
.global m17_cells_short
.global m17_rotate
.global m17_blank
.global m17_cut
.weak   m17_solid_lo
.weak   m17_solid_hi

; An entry: the NES's
#define M17_SPR_OFF     0xEF            /* a y from here on: not shown */
#define M17_SPR_ODD     2               /* attribute bits: this one the NES leaves unused */
#define M17_SPR_BEHIND  5
#define M17_SPR_FLIP_X  6
#define M17_SPR_FLIP_Y  7
#define M17_BOX_LAST    7               /* a tile's box, its second byte: the last class */

#define M17_SPR_END     (m17_sprites + 4 * M17_SPRITES)
#define M17_P2_FRAME    47              /* pass 2's frame on the stack: its two tables of codes (m17_pass2.s) */
#define M17_HUD_LINES   (8 * M17_HUD)   /* the picture's lines over the playfield */

; The compositor's bytes: the end of the run (videoModeCore.s has its start, the list's page
; among it)
.section .m17run

	m17_spr_tiles:   .space 2                       ; the sprite tiles, 16 bytes each
	m17_spr_boxes:   .space 2                       ; their opaque boxes, 2 bytes each
	m17_blank:       .space 1                       ; the VRAM index of a tile whose bytes are all one (0: none)
	m17_cells_short: .space 1                       ; cells that went without a RAM tile
	m17_rotate:      .space 1                       ; the entry pass 1 starts from
	m17_next:        .space 1                       ; the first entry that went without a tile, this time
	m17_cut:         .space 1                       ; the first entry left for the next call, its class in bits 6-7 (0xFF: none)
	m17_span:        .space 1                       ; entries from the start that pass 1 came to in the cut's class (in pass 1: its scratch)
	                 .space 1                       ; (not used: pass 2 stores a line's pairs by their addresses in m17_nib)
	m17_nib:         .space 5                       ; a line's pairs, in the order they are drawn
m17_run_end:

.if m17_run_end - m17_run != M17_RUN_SIZE
	.error "the run is not m17.def.h's M17_RUN_SIZE: a part's size or M17_STATE_SIZE is off"
.endif
.if (m17_sprites - m17_run != M17_RUN_SPRITES) || (vram - m17_run != M17_RUN_VRAM) || (m17_ram_tiles - m17_run != M17_RUN_TILES)
	.error "a part of the run is not at m17.def.h's offset"
.endif
.if (m17_restore - m17_run != M17_RUN_RESTORE) || (m17_ram_used - m17_run != M17_RUN_STATE)
	.error "a part of the run is not at m17.def.h's offset"
.endif


.section .text.m17_ComposeSprites

m17_ComposeSprites:
	; r2 to r17, r28 and r29 are kept by the compiler's own two routines (libgcc's, which a
	; program built with -mcall-prologues has already): 50 bytes less than the pushes and pops
	ldi   r26,     0                       ; no frame (pass 2 takes one for itself)
	ldi   r27,     0
	ldi   r30,     pm_lo8(m17_cs_saved)
	ldi   r31,     pm_hi8(m17_cs_saved)
	jmp   __prologue_saves__
m17_cs_saved:
	clr   r6
m17_cs_clear:
	sts   m17_shown, r6                    ; no picture has shown this composition
m17_cs_begin:
	sts   m17_ram_used, r6                 ; VRAM is the compositor's: the frame gives no cell a RAM tile
	call  m17_update_screen                ; the screen's colours made again, if a palette asked

	lds   r2,      Screen + M17_SCR_SCROLLX
	lds   r3,      Screen + M17_SCR_SPRITEX
	lds   r5,      m17_tiles_hi
	lds   r16,     Screen + M17_SCR_BANK
	add   r5,      r16
	ldi   r16,     (M17_RAM_INDEXES * M17_TILE_BYTES) >> 8
	sub   r5,      r16                     ; index 64 is the bank's tile 0
	clr   r7
	clr   r25
	sts   m17_cells_short, r6
	ldi   r16,     0xFF
	sts   m17_cut, r16
	ldi   r16,     lo8(m17_restore)
	mov   r14,     r16
	ldi   r16,     hi8(m17_restore)
	mov   r15,     r16

#define M17_PART 1
#include "m17_pass1.s"
#undef M17_PART
m17_p1_end:
	push  r25                              ; the sprites that took a turn: returned, in r25
	push  r7                               ; the RAM tiles handed out: the frame is told at the end
	; the entries pass 2 draws: from the start round to the cut, all of them when there is none
	ldi   r17,     M17_SPRITES
	lds   r16,     m17_cut
	cpi   r16,     0xFF
	breq  2f
	andi  r16,     0x3F                    ; the cut's entry: its class is bits 6-7
	lds   r17,     m17_rotate
	sub   r16,     r17
	brcc  1f
	subi  r16,     lo8(-(M17_SPRITES))
1:	mov   r17,     r16
2:	sts   m17_span, r17

#define M17_PART 1
#include "m17_pass2.s"
#undef M17_PART
m17_p2_end:
	;---------------------------------------------------------------------
	; The pixels are done. The frame is told how many RAM tiles to give their cells, and only
	; then does VRAM get its flash tiles back: a picture that starts in between shows them all
	; (the frame keeps the list's flash tile for a cell that still holds a RAM tile's index)
	;---------------------------------------------------------------------
m17_cs_sample:
	lds   r24,     m17_shown               ; the value returned: a picture was drawn before this
	in    r28,     _SFR_IO_ADDR(SPL)       ; (the hand-over's two bytes are over pass 2's frame,
	in    r29,     _SFR_IO_ADDR(SPH)       ; which is at the stack's top to the call's end)
	ldd   r16,     Y + M17_P2_FRAME + 1
	ldd   r25,     Y + M17_P2_FRAME + 2
m17_cs_publish:
	sts   m17_ram_used, r16
	ldi   XL,      lo8(m17_restore)
	ldi   XH,      hi8(m17_restore)
	tst   r16
	breq  2f
1:	ld    YL,      X+
	ld    YH,      X+
	ld    r0,      X+
	st    Y,       r0
	dec   r16
	brne  1b
2:	lds   r16,     m17_cells_short
	tst   r16
	breq  3f
	lds   r16,     m17_next                ; the next call starts with who went without a tile ...
	rjmp  4f
3:	lds   r16,     m17_cut                 ; ... or with who waited for it
	cpi   r16,     0xFF
	breq  5f
	andi  r16,     0x3F                    ; the cut's entry: its class is bits 6-7
4:	sts   m17_rotate, r16
5:
	clr   r1
	in    r28,     _SFR_IO_ADDR(SPL)       ; the saved registers are over pass 2's frame and the
	in    r29,     _SFR_IO_ADDR(SPH)       ; hand-over's two bytes
	adiw  r28,     M17_P2_FRAME + 2
	ldi   r30,     18
m17_cs_ret:
	jmp   __epilogue_restores__            ; ... and returns (r24, r25 kept)


#define M17_PART 2
#include "m17_pass1.s"
#undef M17_PART
#define M17_PART 2
#include "m17_pass2.s"
#undef M17_PART
;-----------------------------------------------------------------------------
; Both passes: r5 the HUD's flash tiles' page, as the playfield's is made (r24 is used)
;-----------------------------------------------------------------------------
m17_hud_page:
	lds   r5,      m17_tiles_hi
	lds   r24,     Screen + M17_SCR_HUDBANK
	add   r5,      r24
	ldi   r24,     (M17_RAM_INDEXES * M17_TILE_BYTES) >> 8
	sub   r5,      r24
	ret

#define M17_PART 3
#include "m17_pass2.s"
#undef M17_PART
m17_sprites_end:

.section .text
