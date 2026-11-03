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
; Video mode 17 (working name)
;
; 224 pixels a line (28 tiles of 8), 6 cycles a pixel, up to 224 lines: mode 13
; as the frame goes (the sync inlined, the first partial tile through a jump
; table for the fine x scroll, a loop of 48 cycles a tile, Timer1 overflow
; ending the line, a HUD row that stays put over a scrolling playfield).
;
; A tile line is 4 bytes and a byte is 2 pixels, read as table[b] and
; table[b + 1]. What a byte means depends on the tile:
; - a flash tile (VRAM index 64-255): palette << 4 | pair code, table m17_t1
; - a RAM tile (VRAM index 0-63): a pair of the screen colours, table m17_t2
; (generators/tables.py has the codes).
;
; The loop: the next tile kind is a compare of its VRAM index made after the
; multiply (which writes carry) and read by a branch 4 cycles later, past
; instructions that leave the flags alone. Each of the four cases (this tile
; flash or RAM, the next one flash or RAM) has its own tail, which adds its own
; tile base and writes XH only when the kind changes. The flash loop has no
; spare cycle. test/m17check.py checks every out's cycle and value on the machine.
;
; Registers, a line through:
;   r2      the line within the tile row, 0-7       r3   lines left to draw
;   r4      32, a tile's bytes                      r5   0, the black after a line
;   r6:r7   the first whole tile's line, past its first byte
;   r8,r16  the two pixel registers                 r17  the next tile's index, a pixel in the first tile
;   r9      this section's column step: 8 bytes in VRAM, 1 in the HUD's row
;   r10:r11 the playfield's first row (with the coarse x scroll), kept through the HUD
;   r12     the playfield's x scroll                r13  this section's
;   r14     this section's flash tiles, high byte (the bank in, index 64 its tile 0)
;   r15     the playfield's                         r22  the RAM tiles', high byte
;   r20     rows left in this section               r21  the playfield's rows
;   r23     the first whole tile's table page       r24  4 x r2
;   r25     the first tile's index                  r18, r19  scratch
;   X the table (XL a tile byte)   Y VRAM, 8 bytes a column   Z a tile's line
;   T: the first whole tile is a flash tile
;=============================================================================

.global sub_video_mode17
.global TIMER1_OVF_vect
.global m17_run
.global m17_run_end
.global vram
.global m17_t1
.global m17_t2
.global m17_hud
.global m17_ram_tiles
.global m17_restore
.global m17_ram_used
.global Screen
.global m17_tiles_hi
.global m17_hold
.global m17_skipping
.global m17_shown
.global m17_held_end

.global m17_back
.global m17_setup
.global m17_look
.global m17_band_end
.global m17_gap
.global m17_window
.global m17_picture_out
.global m17_picture_done
.global m17_timer_back
.global m17_row_asked
.global m17_switch_pad

#include "m17_tables.h"

#if M17_HUD_OFS != M17_PAIRS15 + 1
	#error the HUD's row does not start where table T2 ends
#endif

#define VIDEO _SFR_IO_ADDR(DATA_PORT)

; Cycle counts the waits below are made of. A line is 1,820 cycles from its sync edge:
; the first pixel at M17_FIRST_PIXEL, 28 x 48 cycles of picture, the black one cycle later
#define M17_FIRST_PIXEL  346            /* from the sync edge: mode 13's, measured, centre 1,022 */
#define M17_CELLS        (19 * M17_RAM_TILES + 16)   /* then m17_shown, and the RAM tiles' cells */
#define M17_PRE_SYNC     22             /* m17_line's entry to the sync edge */
#define M17_PROLOGUE     59             /* after sei to the first pixel */
; The picture in bands (m17.def.h). Its state is three bytes of the I/O space:
#define M17_GP0          _SFR_IO_ADDR(GPIOR0)   /* the bits below, and the program's leave (M17_LEAVE_BIT) */
#define M17_GP1          _SFR_IO_ADDR(GPIOR1)   /* the playfield's row after the one being drawn; in a run of rows that went to the kernel: its last so far */
#define M17_GP2          _SFR_IO_ADDR(GPIOR2)   /* not 0: that row has a cell in sight that is not the blank tile */
#define M17_B_GAP        M17_GAP_BIT    /* a run of rows is the kernel's: the next call is the way back in (0; the program reads it: m17_InPicture()) */
#define M17_B_WHOLE      1              /* no row of this picture goes to the kernel */
; Its cycles: 26 of a line's right margin are the frame's, for its look at the row to come
#define M17_HELD         (9 + 2)        /* the frame's start: is it the way back in; is the picture to be drawn */
#define M17_ASK          29             /* the picture's first call: may its rows go to the kernel */
#define M17_SETUP        (17 + 2 + M17_ASK + 9 * M17_HUD + 5 * (1 - M17_HUD))   /* the frame's registers */
#define M17_MARGIN       22             /* the handler's wait, of the right margin's 48 */
#define M17_LOOK(n)      (27 + 8 * (n)) /* rcall m17_look, n cells */
#define M17_LOOKED       (M17_COLS + 1) /* the cells of a row that are looked at: those in sight, and the one a fine scroll brings in */
#define M17_LINE_END     (M17_FIRST_PIXEL + 48 * M17_COLS + 1 + 1 + 8 + M17_MARGIN + 4)   /* the handler's ret done: 1,752 (in bands 1,726) */
; Timer1 counts the picture out: mode 13's preload is for 57 cycles from sei to the first pixel
#define M17_TIMER        (0xffff - (48 * M17_COLS) - 51 - (M17_PROLOGUE - 57))


; The run: all of the mode's SRAM, a section of its own that the program's Makefile places on
; a page boundary, with .data after it (m17.def.h's M17_RUN_ offsets are these labels'). The
; compositor's bytes follow these (m17_sprites.s), and the run's size is checked there
.section .m17run, "aw", @nobits

	.balign 256
m17_run:

	; The two colour tables, the sprite list, VRAM and the RAM tiles: each starts a page
	m17_t2:         .space M17_HUD_OFS                      ; the table's 226 bytes
	m17_hud:        .space M17_HUD_COLS                     ; the HUD's row, a byte a column: the rest of the page
	m17_t1:         .space 256                              ; 16 x M17_PALETTES + 1, then C's two translations
	m17_sprites:    .space 256                              ; 4 x M17_SPRITES: y, tile, attributes, x
	vram:           .space M17_VRAM_SIZE
	m17_ram_tiles:  .space M17_TILE_BYTES * M17_RAM_TILES

	m17_restore:    .space 3 * M17_RAM_TILES                ; a RAM tile's cell (2) and its flash index
	m17_ram_used:   .space 1                                ; RAM tiles the frame gives their cells (0: the sprites are being composed)
	Screen:         .space M17_SCR_SIZE                     ; m17.def.h's M17_SCR_ bytes
	m17_tiles_hi:   .space 1                                ; the flash tiles' table, high byte
	m17_hold:       .space 1                                ; not 0: the coming picture is not drawn
	m17_skipping:   .space 1                                ; this picture's first line was not, so no line is
	m17_shown:      .space 1                                ; set by every picture drawn: the compositor's, to know it was cut


.section .text

;-----------------------------------------------------------------------------
; The frame. Entered at cycle 467 of a line; the first sync edge is 1,356 later
;-----------------------------------------------------------------------------
sub_video_mode17:

	; A picture that starts while m17_hold is set is not drawn: the program is at work on what
	; it would show (an area's first screen). The kernel goes on with the sync, the screen
	; black, and comes here for each of the picture's lines: m17_skipping counts them down and
	; each returns. The last sets the vsync flag and turns the field, as the end of a picture
	; that is drawn does, so a program that waits for the flag goes on while the screen is held
	; (In bands: called in the last line of a run of rows that went to the kernel, this is
	; the way back in. Asked first: the hold is the next picture's by then)
	sbic  M17_GP0, M17_B_GAP               ; 2 / 1
	rjmp  m17_back                         ; 2
	lds   ZL,      m17_skipping            ; 2  lines left of a picture that is not drawn
	tst   ZL                               ; 1
	brne  m17_held                         ; 1 / 2
	lds   ZL,      m17_hold                ; 2
	tst   ZL                               ; 1
	breq  m17_draw                         ; 2 / 1  = M17_HELD
	lds   ZL,      render_lines_count      ; held: this is its first line
m17_held:
	dec   ZL
	sts   m17_skipping, ZL
	brne  1f
m17_held_end:
	lds   ZL,      sync_flags              ; its last line: the vsync flag, and the other field
	ldi   ZH,      SYNC_FLAG_FIELD
	ori   ZL,      SYNC_FLAG_VSYNC
	eor   ZL,      ZH
	sts   sync_flags, ZL
1:	ret

m17_draw:
	WAIT  r16,     1356 - M17_PRE_SYNC - 4 - M17_SETUP - 9 - M17_CELLS - M17_HELD

	; It is drawn: a composition that this picture cuts will know it was shown unfinished
	ldi   r16,     1                       ; 1
	sts   m17_shown, r16                   ; 2

	; The RAM tiles go into their cells: m17_ram_used of them, which is none while the sprites
	; are being composed (the cells the compositor has written show their RAM tiles already,
	; and VRAM is its own until it is done). VRAM holds flash indexes outside the frame, and
	; the program may have written it since the compositor ran: what a cell holds now is kept
	; for the frame's end. A cell that holds a RAM tile's index is one the compositor's last
	; loop had not come to yet: the list has its flash tile, and keeps it.
	; 19 cycles a RAM tile, used or not: M17_CELLS in all
	lds   r18,     m17_ram_used            ; 2
	ldi   r19,     M17_RAM_TILES           ; 1
	sub   r19,     r18                     ; 1  those not used
	clr   r16                              ; 1
	ldi   ZL,      lo8(m17_restore)        ; 1
	ldi   ZH,      hi8(m17_restore)        ; 1
	tst   r18                              ; 1
	breq  m17_cells_pad                    ; 1 / 2
	rjmp  .                                ; 2
m17_cells:
	ld    XL,      Z+                      ; 2
	ld    XH,      Z+                      ; 2
	ld    r17,     X                       ; 2  the cell as the program left it
	ld    r20,     Z                       ; 2  the list's flash tile
	cpi   r17,     M17_RAM_INDEXES         ; 1
	brcs  1f                               ; 1 / 2
	mov   r20,     r17                     ; 1 / 0  a flash tile: it is what the frame's end puts back
1:	st    Z+,      r20                     ; 2
	st    X,       r16                     ; 2  the RAM tile
	inc   r16                              ; 1
	cp    r16,     r18                     ; 1
	brlo  m17_cells                        ; 2 / 1
m17_cells_pad:
	tst   r19                              ; 1
	breq  m17_cells_done                   ; 1 / 2
	rjmp  .                                ; 2
m17_cells_wait:
	ldi   r17,     4                       ; 1
	dec   r17                              ; 3 x 4 - 1
	brne  .-4
	rjmp  .                                ; 2
	rjmp  .                                ; 2
	dec   r19                              ; 1
	brne  m17_cells_wait                   ; 2 / 1
m17_cells_done:

m17_setup:                                 ; (the way back in joins here)
	; Timer1 free running, its overflow the only interrupt: it ends each line   (9 cycles)
	ldi   ZL,      (1 << OCF1B) + (1 << OCF1A) + (1 << TOV1)
	sts   _SFR_MEM_ADDR(TIFR1), ZL
	ldi   r16,     (1 << TOIE1)
	sts   _SFR_MEM_ADDR(TIMSK1), r16
	ldi   r16,     (0 << WGM12) + (1 << CS10)
	sts   _SFR_MEM_ADDR(TCCR1B), r16

	; The frame's registers   (M17_SETUP cycles)
	lds   r3,      render_lines_count      ; 2
	clr   r2                               ; 1
	ldi   r22,     hi8(m17_ram_tiles)      ; 1
	lds   r16,     m17_tiles_hi            ; 2
	subi  r16,     (M17_RAM_INDEXES * M17_TILE_BYTES) >> 8   ; 1  index 64 is flash tile 0
	lds   r15,     Screen + M17_SCR_BANK   ; 2
	add   r15,     r16                     ; 1  the playfield's flash tiles
	lds   r12,     Screen + M17_SCR_SCROLLX ; 2
	mov   YL,      r12                     ; 1
	andi  YL,      0xf8                    ; 1  8 bytes a column
	ldi   YH,      hi8(vram)               ; 1
	movw  r10,     YL                      ; 1  the playfield's row 0
	ldi   r21,     M17_PF_ROWS             ; 1  = 17
	sbic  M17_GP0, M17_B_GAP               ; 2 / 1  the way back in: a band that starts at a row of the playfield
	rjmp  m17_back_row                     ; 2

	; May rows of this picture go to the kernel? When the program has given its leave, a flash
	; tile is named as the blank (a RAM tile's index is none), and that tile's byte shows black
	; twice: its pair has no art and the backdrop is black, as the kernel's lines are. The
	; look starts with the row after this band's first   (M17_ASK cycles; r16 is kept: the
	; HUD's tiles are found from it)
	lds   r17,     m17_blank               ; 2
	ldi   r18,     M17_TILE_BYTES          ; 1
	mul   r17,     r18                     ; 2
	movw  ZL,      r0                      ; 1
	add   ZH,      r15                     ; 1
	lpm   XL,      Z                       ; 3  the blank's first byte
	ldi   XH,      hi8(m17_t1)             ; 1
	ld    r19,     X+                      ; 2  its two colours
	ld    r18,     X                       ; 2
	or    r19,     r18                     ; 1  not 0: not black
	cpi   r17,     M17_RAM_INDEXES         ; 1  carry: no flash tile is named
	sbc   r18,     r18                     ; 1
	or    r19,     r18                     ; 1
	in    r17,     M17_GP0                 ; 1
	sbrs  r17,     M17_LEAVE_BIT           ; 2  no leave: (1 and the next, or 2)
	ser   r19
	andi  r17,     1 << M17_LEAVE_BIT      ; 1
	cpse  r19,     r2                      ; 2  (r2 is 0)
	ori   r17,     1 << M17_B_WHOLE
	out   M17_GP0, r17                     ; 1
	ldi   r18,     1 - M17_HUD             ; 1
	out   M17_GP1, r18                     ; 1
	out   M17_GP2, r2                      ; 1  = 29
	lds   r14,     Screen + M17_SCR_HUDBANK ; 2  the HUD's row first: its own flash tiles,
	add   r14,     r16                     ; 1
	ldi   YL,      lo8(m17_hud)            ; 1  its cells after T2, a byte a column,
	ldi   YH,      hi8(m17_hud)            ; 1
	clr   r13                              ; 1  not scrolled,
	ldi   r20,     1                       ; 1  one row
	ldi   r16,     1                       ; 1
	mov   r9,      r16                     ; 1  = 9

m17_next_line:
	call  m17_line                         ; back at M17_LINE_END

	; In bands the frame does not wait between two lines: it looks at the row after the one
	; it is drawing (m17_look), 4 of its cells after each of the row's first 7 lines and the
	; 29th after the last, in constant time
	dec   r3                               ; 1
	breq  m17_frame_end                    ; 1 / 2
	inc   r2                               ; 1
	mov   r16,     r2                      ; 1
	cpi   r16,     M17_TILE_H              ; 1
	breq  m17_next_row                     ; 1 / 2  = 6 here, 7 there

	ldi   r17,     4                       ; 1
	rcall m17_look
	WAIT  r16,     1820 - M17_LINE_END - 7 - M17_LOOK(4) - 2 - 4 - M17_PRE_SYNC
	rjmp  m17_next_line

m17_next_row:
	ldi   r17,     1                       ; 1
	rcall m17_look
m17_row_asked:
	sbic  M17_GP0, M17_B_WHOLE             ; 2  no row of this picture goes to the kernel:
	ser   r19                              ;    as if the row to come had art
	tst   r19                              ; 1
	breq  m17_band_end                     ; 1 / 2  it is of blank: this band ends here
	WAIT  r16,     1820 - M17_LINE_END - 8 - M17_LOOK(1) - 4 - 5 - 13 - 4 - M17_PRE_SYNC
m17_row_on:                                ; (the way back in joins here, at the end of a section of one row)
	clr   r2                               ; 1
	out   M17_GP2, r2                      ; 1  nothing seen yet of the row after the next
	in    r16,     M17_GP1                 ; 1
	inc   r16                              ; 1
	out   M17_GP1, r16                     ; 1  = 5; the paths from here: 13 cycles
	dec   r20
	brne  m17_same_section

	mov   r20,     r21                     ; the HUD is done: the playfield
	movw  YL,      r10
	mov   r13,     r12
	mov   r14,     r15
	ldi   r16,     8                       ; VRAM's column step
	mov   r9,      r16
m17_switch_pad:
	rjmp  .
	nop
	rjmp  m17_next_line

m17_same_section:
	mov   r16,     YL                      ; the next row: 8 to a page
	andi  r16,     7
	cpi   r16,     7
	breq  1f
	inc   YL
	rjmp  2f
1:
	andi  YL,      0xf8
	inc   YH
2:
	nop
	rjmp  m17_next_line

; The row after this one is of blank: the band ends as a frame does (Timer1 the kernel's
; again, the pulse of the line to come), and that row's lines are the kernel's own
m17_band_end:
	WAIT  r16,     1820 - M17_LINE_END - 8 - M17_LOOK(1) - 5 - 2 - 6 - 3
	rjmp  m17_last_pulse                   ; 2

m17_frame_end:
	WAIT  r16,     1820 - M17_LINE_END - 3 - 6 - 3
m17_last_pulse:

	; Timer1 as the kernel left it, moved on to where it would be now (mode 13's sequence)
	ldi   r16,     hi8(101 - TIMER1_DISPLACE)
	sts   _SFR_MEM_ADDR(TCNT1H), r16
	ldi   r16,     lo8(101 - TIMER1_DISPLACE)
	sts   _SFR_MEM_ADDR(TCNT1L), r16

	rcall hsync_pulse                      ; the frame's last pulse

	tst   r3                               ; a band's end, the picture's other rows to come?
	brne  m17_gap
m17_picture_out:                           ; (and from the way back in, when no row is left)
	; The cells get their flash indexes back: a program never reads a RAM tile's index. (None
	; while the sprites are being composed: the compositor finds its RAM tiles in VRAM)
	lds   r18,     m17_ram_used
	ldi   ZL,      lo8(m17_restore)
	ldi   ZH,      hi8(m17_restore)
	tst   r18
	breq  2f
1:
	ld    XL,      Z+
	ld    XH,      Z+
	ld    r17,     Z+
	st    X,       r17
	dec   r18
	brne  1b
2:
m17_picture_done:
	lds   ZL,      sync_flags              ; the vsync flag, and the other field
	ldi   r20,     SYNC_FLAG_FIELD
	ori   ZL,      SYNC_FLAG_VSYNC
	eor   ZL,      r20
	sts   sync_flags, ZL

	; The kernel's window is the picture's again, where a run of rows may have moved it
	sbic  M17_GP0, M17_B_WHOLE
	rjmp  m17_timer_back
	ldi   r16,     FIRST_RENDER_LINE
	sts   first_render_line, r16
	ldi   r16,     FRAME_LINES
	sts   render_lines_count, r16
	cbi   M17_GP0, M17_B_GAP
m17_timer_back:
	ldi   ZL,      (1 << OCF1B) + (1 << OCF1A) + (1 << TOV1)
	sts   _SFR_MEM_ADDR(TIFR1), ZL
	ldi   r16,     (1 << WGM12) + (1 << CS10)      ; Timer1 back to the kernel: CTC, compare A
	sts   _SFR_MEM_ADDR(TCCR1B), r16
	ldi   r16,     (1 << OCIE1A)
	sts   _SFR_MEM_ADDR(TIMSK1), r16

	ret

;-----------------------------------------------------------------------------
; The picture in bands (m17.def.h's M17_BANDS)
;
; A row of the playfield that shows nothing but black is 8 of the very lines the kernel's
; sync interrupt makes by itself in the blank: a pulse, the sound's sample, and the main
; program has the rest. The frame leaves such rows to it. While it draws a row it looks at
; the row after (m17_look); at the row's end, if that one is of blank, the band ends as a
; frame does, and the kernel's render window is set to that row's last line, alone. Called
; in that line, the frame looks at the row after it, whole: of blank too, the window moves
; on by a row and the call returns; else it waits for its cycle, as at a picture's start,
; and draws on from that row; when no row is left the picture is out: the cells, the vsync
; flag, and the window back where it was.
;
; A row is of blank when its M17_LOOKED cells from the scroll's column (those in sight, and
; the one a fine scroll brings in) all hold the tile named as the blank: so none is a RAM
; tile's, which a sprite's cell holds from the picture's first line to its last. Whether
; rows may go at all is asked once a picture (M17_ASK, in the frame's set-up).
;-----------------------------------------------------------------------------

; A band is out and the row after it is of blank: the kernel's lines from here, and its
; call in the last line of row GPIOR1
m17_gap:
	sbi   M17_GP0, M17_B_GAP
m17_window:
	in    r16,     M17_GP1
	subi  r16,     -(1 + M17_HUD)          ; the rows over it, and itself
	lsl   r16
	lsl   r16
	lsl   r16                              ; its last line in the picture
	subi  r16,     lo8(-(FIRST_RENDER_LINE))
	sts   first_render_line, r16
	ldi   r16,     1
	sts   render_lines_count, r16
	rjmp  m17_timer_back                   ; (Timer1 is the kernel's already on the way back in: no harm)

; The way back in: the frame's entry in the last line of a run of rows. Its first sync edge
; is 1,356 cycles after that entry, as a picture's first is
m17_back:                                  ; (3 cycles in)
	in    r16,     M17_GP1                 ; 1  the last row that went to the kernel
	inc   r16                              ; 1  the row after it
	out   M17_GP1, r16                     ; 1
	cpi   r16,     M17_PF_ROWS             ; 1
	brne  1f                               ; 2
	rjmp  m17_picture_out                  ;    there is none: the picture is out
1:
	clr   r24                              ; 1  from its first cell
	ldi   r17,     M17_LOOKED              ; 1
	rcall m17_look
	tst   r19                              ; 1
	breq  m17_window                       ; 1 / 2  of blank too: the window moves on by a row
	WAIT  r16,     1356 - M17_PRE_SYNC - 4 - 18 - 13 - (9 + 17 + 3) - 2 - (3 + 6 + 2 + M17_LOOK(M17_LOOKED) + 2)
	rjmp  m17_setup                        ; 2  Timer1 and the frame's registers (9 + 17, and 3 to here:)

; ... and its registers: a band that starts at the playfield's row GPIOR1
m17_back_row:
	cbi   M17_GP0, M17_B_GAP               ; 2  the run of rows is over
	in    r16,     M17_GP1                 ; 1
	sub   r21,     r16                     ; 1  the rows left: the section's
	mov   r3,      r21                     ; 1
	lsl   r3                               ; 3  their lines
	lsl   r3
	lsl   r3
	subi  XL,      lo8(8 * M17_LOOKED)     ; 1  the row's first cell: the look left X that many cells on
	movw  r10,     XL                      ; 1  where the playfield starts now
	ldi   r20,     1                       ; 1  as at the end of a section of one row
	rjmp  m17_row_on                       ; 2  = 13; 18 from there to m17_next_line

; The look: r17 cells of the playfield's row GPIOR1, from the scroll's column and r24 / 4
; lines of 4 cells on, against the blank tile. GPIOR2 gathers what differs, and r19 has it.
; In constant time: M17_LOOK(r17) cycles with the rcall. X is left after the last cell
m17_look:
	in    XL,      M17_GP1                 ; 1
	mov   XH,      XL                      ; 1
	andi  XL,      7                       ; 1  8 rows a page
	lsr   XH                               ; 3
	lsr   XH
	lsr   XH
	ldi   r16,     hi8(vram)               ; 1
	add   XH,      r16                     ; 1
	lds   r16,     Screen + M17_SCR_SCROLLX ; 2
	andi  r16,     0xf8                    ; 1  8 bytes a column
	or    XL,      r16                     ; 1
	mov   r16,     r24                     ; 1  4 x the line
	lsl   r16                              ; 3  4 cells a line
	lsl   r16
	lsl   r16
	add   XL,      r16                     ; 1  (the low byte alone: a row wraps)
	lds   r18,     m17_blank               ; 2
	in    r19,     M17_GP2                 ; 1  = 20
1:
	ld    r0,      X                       ; 2
	eor   r0,      r18                     ; 1
	or    r19,     r0                      ; 1
	subi  XL,      -8                      ; 1
	dec   r17                              ; 1
	brne  1b                               ; 2 / 1  = 8 a cell, less 1
	out   M17_GP2, r19                     ; 1
	ret                                    ; 4


;-----------------------------------------------------------------------------
; A line. r2 the line in the tile row, r13 the x scroll, r14 the flash tiles,
; Y the row (kept: the overflow handler takes it back off the stack)
;-----------------------------------------------------------------------------
m17_line:
	mov   r24,     r2                      ; 1
	lsl   r24                              ; 1
	lsl   r24                              ; 1  4 bytes a line
	ldi   r16,     M17_TILE_BYTES          ; 1
	mov   r4,      r16                     ; 1
	clr   r5                               ; 1
	push  YL                               ; 2
	push  YH                               ; 2
	ld    r25,     Y                       ; 2  the first tile, shown from the x scroll's pixel
	add   YL,      r9                      ; 1  the next column (the low byte alone: a row wraps)
	ld    r19,     Y                       ; 2  the first whole tile
	ldi   r16,     lo8(M17_TIMER)          ; 1
	ldi   r17,     hi8(M17_TIMER)          ; 1
	lds   ZL,      sync_pulse              ; 2
	dec   ZL                               ; 1
	sts   sync_pulse, ZL                   ; 2  = M17_PRE_SYNC

m17_sync:
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN ; the sync edge, as hsync_pulse makes it
	ldi   ZL,      2
	rjmp  .
	call  update_sound                     ; 4 + AUDIO_OUT_HSYNC_CYCLES

	WAIT  ZL,      M17_FIRST_PIXEL - 5 - 4 - AUDIO_OUT_HSYNC_CYCLES - 4 - 1 - M17_PROLOGUE

m17_picture:
	sts   _SFR_MEM_ADDR(TCNT1H), r17       ; 2
	sts   _SFR_MEM_ADDR(TCNT1L), r16       ; 2
	sei                                    ; 1

	; The first whole tile: its line, its table, its first two pixels   (20 cycles)
	mul   r19,     r4                      ; 2
	cpi   r19,     M17_RAM_INDEXES         ; 1
	brcs  1f                               ; 1 / 2
	add   r0,      r24                     ; 1  a flash tile
	adc   r1,      r14                     ; 1
	movw  ZL,      r0                      ; 1
	ldi   r23,     hi8(m17_t1)             ; 1
	mov   XH,      r23                     ; 1
	set                                    ; 1
	lpm   XL,      Z+                      ; 3
	rjmp  2f                               ; 2
1:
	add   r0,      r24                     ; 1  a RAM tile
	adc   r1,      r22                     ; 1
	movw  ZL,      r0                      ; 1
	ldi   r23,     hi8(m17_t2)             ; 1
	mov   XH,      r23                     ; 1
	clt                                    ; 1
	ld    XL,      Z+                      ; 2
	rjmp  .                                ; 2
2:
	movw  r6,      ZL                      ; 1
	ld    r16,     X+                      ; 2
	ld    r8,      X                       ; 2

	; The first tile, from pixel (x scroll & 7): a jump into its 8 pixels   (34 cycles)
	mul   r25,     r4                      ; 2
	cpi   r25,     M17_RAM_INDEXES         ; 1
	brcs  m17_first_ram                    ; 1 / 2

	add   r0,      r24                     ; 1
	adc   r1,      r14                     ; 1
	movw  ZL,      r0                      ; 1
	ldi   XH,      hi8(m17_t1)             ; 1
	mov   r19,     r13                     ; 1
	andi  r19,     7                       ; 1
	mov   r0,      r19                     ; 1
	lsr   r0                               ; 1  the byte its first pixel shown is in
	add   ZL,      r0                      ; 1
	adc   ZH,      r5                      ; 1
	lpm   XL,      Z                       ; 3
	sbrc  r19,     0                       ; 2  from an odd pixel: the pair's second entry
	inc   XL
	ldi   r17,     3                       ; 1  3 instructions a pixel
	mul   r19,     r17                     ; 2
	ldi   r17,     lo8(pm(m17_first_flash_px)) ; 1
	ldi   r19,     hi8(pm(m17_first_flash_px)) ; 1
	add   r17,     r0                      ; 1
	adc   r19,     r1                      ; 1
	push  r17                              ; 2
	push  r19                              ; 2
	ret                                    ; 4

m17_first_ram:
	add   r0,      r24
	adc   r1,      r22
	movw  ZL,      r0
	ldi   XH,      hi8(m17_t2)
	mov   r19,     r13
	andi  r19,     7
	mov   r0,      r19
	lsr   r0
	add   ZL,      r0
	adc   ZH,      r5
	ld    XL,      Z                       ; 2
	sbrc  r19,     0
	inc   XL
	ldi   r17,     4                       ; 4 instructions a pixel
	mul   r19,     r17
	ldi   r17,     lo8(pm(m17_first_ram_px))
	ldi   r19,     hi8(pm(m17_first_ram_px))
	add   r17,     r0
	adc   r19,     r1
	push  r17
	push  r19
	ret

; The first tile's pixels, 6 cycles each: entered at the first one shown
m17_first_flash_px:
	lpm   XL,      Z
	ld    r17,     X+
	out   VIDEO,   r17
	lpm   r0,      Z+
	ld    r17,     X
	out   VIDEO,   r17
	lpm   XL,      Z
	ld    r17,     X+
	out   VIDEO,   r17
	lpm   r0,      Z+
	ld    r17,     X
	out   VIDEO,   r17
	lpm   XL,      Z
	ld    r17,     X+
	out   VIDEO,   r17
	lpm   r0,      Z+
	ld    r17,     X
	out   VIDEO,   r17
	lpm   XL,      Z
	ld    r17,     X+
	out   VIDEO,   r17
	lpm   r0,      Z+
	ld    r17,     X
	out   VIDEO,   r17

	movw  ZL,      r6                      ; the first whole tile: its line, its table
	mov   XH,      r23
	brts  m17_flash_in
	rjmp  m17_ram_in

m17_first_ram_px:
	ld    XL,      Z
	ld    r17,     X+
	nop
	out   VIDEO,   r17
	ld    r0,      Z+
	ld    r17,     X
	nop
	out   VIDEO,   r17
	ld    XL,      Z
	ld    r17,     X+
	nop
	out   VIDEO,   r17
	ld    r0,      Z+
	ld    r17,     X
	nop
	out   VIDEO,   r17
	ld    XL,      Z
	ld    r17,     X+
	nop
	out   VIDEO,   r17
	ld    r0,      Z+
	ld    r17,     X
	nop
	out   VIDEO,   r17
	ld    XL,      Z
	ld    r17,     X+
	nop
	out   VIDEO,   r17
	ld    r0,      Z+
	ld    r17,     X
	nop
	out   VIDEO,   r17

	movw  ZL,      r6
	mov   XH,      r23
	brts  m17_flash_in
	rjmp  m17_ram_in


; The tiles, 48 cycles each. The number is the cycle within the tile; the outs are at 3 (the
; tile before's last pixel), 9, 15, 21, 27, 33, 39 and 45
m17_flash:                                 ; this tile: flash, table m17_t1
	lpm   XL,      Z+                      ;  0
	out   VIDEO,   r8                      ;  3
	ld    r16,     X+                      ;  4
	ld    r8,      X                       ;  6
m17_flash_in:
	add   YL,      r9                      ;  8
	out   VIDEO,   r16                     ;  9
	lpm   XL,      Z+                      ; 10
	ld    r16,     X+                      ; 13
	out   VIDEO,   r8                      ; 15
	ld    r8,      X                       ; 16
	lpm   XL,      Z+                      ; 18
	out   VIDEO,   r16                     ; 21
	ld    r17,     Y                       ; 22  the next tile
	mul   r17,     r4                      ; 24
	cpi   r17,     M17_RAM_INDEXES         ; 26  carry: a RAM tile
	out   VIDEO,   r8                      ; 27
	ld    r16,     X+                      ; 28
	brcs  m17_flash_ram                    ; 30
	add   r0,      r24                     ; 31
	adc   r1,      r14                     ; 32
	out   VIDEO,   r16                     ; 33
	ld    r8,      X                       ; 34
	lpm   XL,      Z                       ; 36
	out   VIDEO,   r8                      ; 39
	ld    r16,     X+                      ; 40
	ld    r8,      X                       ; 42
	movw  ZL,      r0                      ; 44
	out   VIDEO,   r16                     ; 45
	rjmp  m17_flash                        ; 46
m17_flash_ram:                             ; 32  the next one is a RAM tile
	add   r0,      r24                     ; 32
	out   VIDEO,   r16                     ; 33
	ld    r8,      X                       ; 34
	lpm   XL,      Z                       ; 36
	out   VIDEO,   r8                      ; 39
	ld    r16,     X+                      ; 40
	ld    r8,      X                       ; 42
	adc   r1,      r22                     ; 44
	out   VIDEO,   r16                     ; 45
	movw  ZL,      r0                      ; 46
	ldi   XH,      hi8(m17_t2)             ; 47
m17_ram:                                   ; this tile: RAM, table m17_t2
	ld    XL,      Z+                      ;  0
	nop                                    ;  2
	out   VIDEO,   r8                      ;  3
	ld    r16,     X+                      ;  4
	ld    r8,      X                       ;  6
	nop                                    ;  8
m17_ram_in:
	out   VIDEO,   r16                     ;  9
	ld    XL,      Z+                      ; 10
	ld    r16,     X+                      ; 12
	add   YL,      r9                      ; 14
	out   VIDEO,   r8                      ; 15
	ld    r8,      X                       ; 16
	ld    XL,      Z+                      ; 18
	nop                                    ; 20
	out   VIDEO,   r16                     ; 21
	ld    r17,     Y                       ; 22
	mul   r17,     r4                      ; 24
	cpi   r17,     M17_RAM_INDEXES         ; 26
	out   VIDEO,   r8                      ; 27
	ld    r16,     X+                      ; 28
	brcs  m17_ram_ram                      ; 30
	add   r0,      r24                     ; 31  the next one is a flash tile
	adc   r1,      r14                     ; 32
	out   VIDEO,   r16                     ; 33
	ld    r8,      X                       ; 34
	ld    XL,      Z                       ; 36
	movw  ZL,      r0                      ; 38
	out   VIDEO,   r8                      ; 39
	ld    r16,     X+                      ; 40
	ld    r8,      X                       ; 42
	ldi   XH,      hi8(m17_t1)             ; 44
	out   VIDEO,   r16                     ; 45
	rjmp  m17_flash                        ; 46
m17_ram_ram:                               ; 32
	add   r0,      r24                     ; 32
	out   VIDEO,   r16                     ; 33
	ld    r8,      X                       ; 34
	ld    XL,      Z                       ; 36
	adc   r1,      r22                     ; 38
	out   VIDEO,   r8                      ; 39
	ld    r16,     X+                      ; 40
	ld    r8,      X                       ; 42
	movw  ZL,      r0                      ; 44
	out   VIDEO,   r16                     ; 45
	rjmp  m17_ram                          ; 46


; Timer1 overflow: the picture is out. The loop has no exit of its own, so its return
; address is dropped and the line returns to the frame
TIMER1_OVF_vect:
m17_line_end:
	out   VIDEO,   r5                      ; 1  black
	pop   r0                               ; 2
	pop   r0                               ; 2
	pop   YH                               ; 2  the row
	pop   YL                               ; 2
	WAIT  r16,     M17_MARGIN              ; the picture's right margin (in bands: a part of it)
	ret                                    ; 4


#include "m17_sprites.s"

#include "m17_tables.inc"

.section .text
