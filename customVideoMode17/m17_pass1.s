;=============================================================================
; Video mode 17's compositor, pass 1 (the tiles): a part of m17_sprites.s, which has the
; compositor's header (what the passes do) and includes this file once a part (M17_PART),
; each where its code goes.
;
; The three classes are three trips, and no trip looks at an entry it has no use for twice:
;   the first class   its own entries only (M17_FIRST_N from M17_FIRST_AT), from m17_rotate
;                     round when that is one of them
;   the second        the list from m17_rotate round, once, but for the first class's
;                     entries: a run up to them, a run from after them. A sprite of the last
;                     class is passed, and the first and the last of them are kept
;   the last          from the first of them to the last, when the second's trip saw one
; The order the sprites are taken in, and so the RAM tiles' and the restore list's, is that
; of three whole trips from m17_rotate, a class each: the model's.
;
; A sprite: its tile's opaque box (an entry whose tile has no pixel leaves there), its place
; in the picture (the list's less the origin, in line), its class; the two bands of 8 lines its
; first and last opaque lines are in, the cell its first opaque column is in and whether its
; last is in the next one. Then a cell at a time, the first band's before the second's, the
; first column's before the next's: a cell that holds a RAM tile already is seen in line, one
; that shows a flash tile calls m17_p1_tile.
;
; The list is a page (m17_sprites is at a page's start: m17.def.h's run, which the checks
; hold to), so an entry's place is XL alone and the list's end is XL's own wrap. The RAM
; tiles start a page too, 32 bytes each: the fill reads its end from where it writes.
;
; This pass's registers (r2, r3, r5 and r6 are the compositor's; r7, r14:r15 and r25 are
; handed in by m17_sprites.s and r7, r25 handed on at m17_p1_end):
;   X    the list. From where an entry's x is read to the entry's end it stays at that byte,
;        the entry's last (the tile's routine keeps it): XL is the entry's place
;   r4   where this run of the list ends (XL)
;   r11  the trip: bits 6-7 its class as m17_cut has it (0 the first's, 0x40 the second's,
;        0x80 the last's); bit 0: it has a second run, up to m17_rotate
;   r8:r9 the sprite tiles' boxes, r12 the origin's y
;   r13  the blank's index, r10 its byte as a RAM tile holds it
;   r18 y, r19 x, r20 tile, r23 attributes: the entry, its y the picture's, its x the
;        picture's and then, with the scroll added, the playfield's
;   r16 r17 its box, then the cell a tile is being made for
;   r20 r21 the bands of its first and last lines, r18 the first cell's column (x 8),
;   r22  bit 0 the first column's cell is in the picture, bit 3 the next one's is; bits 4-7
;        its cells that count
;   r25  the sprites that took a turn, r7 RAM tiles handed out, r14:r15 the restore list's end
;   T    set while the columns are made again for the HUD's row
; m17_span and m17_nib (which pass 2 writes before it reads them) hold the place of the
; first and of the last sprite of the last class while the second's trip goes.
;=============================================================================

#if M17_PART == 1
; ---- part 1: the pass: a trip a class, a sprite's cells
#if M17_FIRST_AT + M17_FIRST_N > M17_SPRITES
	#error the first class's entries (M17_FIRST_AT, M17_FIRST_N) are not all in the sprite list
#endif
#define M17_P1_FIRST     (4 * M17_FIRST_AT)
#define M17_P1_FIRST_END ((4 * (M17_FIRST_AT + M17_FIRST_N)) % (4 * M17_SPRITES))

	;---------------------------------------------------------------------
	; Pass 1: the tiles
	;---------------------------------------------------------------------
	clt
	lds   r8,      m17_spr_boxes
	lds   r9,      m17_spr_boxes + 1
	lds   r12,     Screen + M17_SCR_SPRITEY
	lds   r13,     m17_blank
	ldi   r16,     M17_TILE_BYTES          ; the blank's byte as a RAM tile holds it (its flash
	mul   r13,     r16                     ; tile's first byte through the translation): what a
	movw  ZL,      r0                      ; cell that shows the blank is filled with
	add   ZH,      r5
	lpm   XL,      Z
	subi  XL,      lo8(-(M17_XLAT_OFS))
	ldi   XH,      hi8(m17_t1)
	ld    r10,     X
	sts   m17_span, r6                     ; no sprite of the last class seen
	ldi   XH,      hi8(m17_sprites)
#if M17_FIRST_N
	; the first class's trip: its entries, from m17_rotate when that is one of them (and
	; not the first: that is all of them in their order)
	clr   r11
	ldi   XL,      M17_P1_FIRST
	ldi   r16,     M17_P1_FIRST_END
	mov   r4,      r16
	lds   r16,     m17_rotate
	lsl   r16
	lsl   r16
	mov   r17,     r16
	subi  r17,     lo8(M17_P1_FIRST + 4)
	cpi   r17,     4 * M17_FIRST_N - 4
	brsh  1f
	mov   XL,      r16                     ; from that one to their last, then a second run
	inc   r11
1:
#else
	ldi   r16,     0x40                    ; the second class's trip: the list from m17_rotate
	mov   r11,     r16                     ; round to the entry before
	lds   XL,      m17_rotate
	lsl   XL
	lsl   XL
	mov   r4,      XL
#endif
	rjmp  m17_p1_entry

	; A run's end: the trips (come to four times a call at most, and so out of the entries' way)
m17_p1_run:
#if M17_FIRST_N
	sbrs  r11,     0
	rjmp  1f
	dec   r11                              ; this trip has a second run, up to m17_rotate:
	lds   r4,      m17_rotate
	lsl   r4
	lsl   r4
	ldi   XL,      M17_P1_FIRST            ; the first class's from its first entry,
	sbrc  r11,     6
	ldi   XL,      M17_P1_FIRST_END        ; the second's from the entry after the first class's
	cpse  XL,      r4                      ; (which may be m17_rotate itself: no entry then)
	rjmp  m17_p1_entry
1:
#endif
	sbrc  r11,     7
m17_p1_ended:
	rjmp  m17_p1_end                       ; the last class is done
#if M17_FIRST_N
	sbrc  r11,     6
	rjmp  3f                               ; the second is
#if M17_FIRST_N < M17_SPRITES
	; The first is. The second's trip: the list from m17_rotate round but for the first class's
	; entries, which no entry is then tested for: up to them, then from after them
	lds   XL,      m17_rotate
	lsl   XL
	lsl   XL
	ldi   r16,     M17_P1_FIRST
	mov   r4,      r16
	mov   r16,     XL
	subi  r16,     M17_P1_FIRST
	cpi   r16,     4 * M17_FIRST_N
	ldi   r16,     0x41                    ; (the flags stay)
	brsh  2f
	ldi   XL,      M17_P1_FIRST_END        ; m17_rotate is one of them: from after them, one run
	ldi   r16,     0x40
2:	mov   r11,     r16
	rjmp  m17_p1_entry
#else
	rjmp  m17_p1_end                       ; (every entry is the first class's)
#endif
3:
#endif
	lds   XL,      m17_span                ; the second is. The last's trip, from the first of its
	subi  XL,      3                       ; sprites to the last (kept: their last bytes' places)
	brcs  m17_p1_ended                     ; there was none
	lds   r4,      m17_nib
	inc   r4
#if M17_SPRITES < 64
	ldi   r16,     4 * M17_SPRITES
	cpse  r4,      r16
	rjmp  1f
	clr   r4
1:
#endif
	ldi   r16,     0x80
	mov   r11,     r16
	rjmp  m17_p1_entry

	; (what a sprite seldom comes to, within a branch's reach of where it does)
m17_p1_left:
	cpi   r19,     256 - 7                 ; left of the origin: 7 pixels at most
	brsh  m17_p1_x_in
	rjmp  m17_p1_done
m17_p1_above:
	cpi   r18,     256 - 7                 ; above it: 7 lines at most
	brsh  m17_p1_y_in
m17_p1_out:
	rjmp  m17_p1_done
m17_p1_empty:
	subi  XL,      -2                      ; no opaque pixel: the next entry (X is at its third byte)
	rjmp  m17_p1_next
m17_p1_other:
	; a sprite whose tile is marked last, or any sprite in the last class's trip
	sbrc  r11,     6
	rjmp  2f                               ; the second class's trip: it is the last's
#if M17_FIRST_N
	sbrs  r11,     7
	rjmp  m17_p1_mine                      ; the first's: every entry it comes to is its own
#endif
	sbrs  r17,     M17_BOX_LAST
	rjmp  m17_p1_done                      ; the last's trip: a sprite that is not marked,
#if M17_FIRST_N && M17_FIRST_N < M17_SPRITES
	mov   r24,     XL                      ; or an entry of the first class (X is at its last
	subi  r24,     M17_P1_FIRST + 3        ; byte): done
	cpi   r24,     4 * M17_FIRST_N
	brlo  m17_p1_out
#endif
	rjmp  m17_p1_mine
2:	lds   r24,     m17_span                ; the last's trip will be from the first of them (its
	cpse  r24,     r6                      ; last byte's place, which is never 0)
	rjmp  1f
	sts   m17_span, XL
1:	sts   m17_nib, XL                      ; ... to the last
	rjmp  m17_p1_done
m17_p1_to_run:
	rjmp  m17_p1_run

	; The list, an entry at a time: 8 cycles for one that is off
m17_p1_off:
	subi  XL,      -3                      ; (the list is a page: XL alone)
m17_p1_next:
#if M17_SPRITES < 64
	cpi   XL,      4 * M17_SPRITES         ; the list's end: on from its start
	brne  1f
	clr   XL
1:
#endif
	cp    XL,      r4
	breq  m17_p1_to_run                    ; this run's end
m17_p1_entry:
	ld    r18,     X+                      ; its y
	cpi   r18,     M17_SPR_OFF
	brsh  m17_p1_off
m17_p1_shown:
	; its tile's opaque box: lines, columns (first | last << 4); 0xFF: no opaque pixel. That
	; is looked at first: a game's list has many entries on with an empty tile
	ld    r20,     X+                      ; its tile
	movw  ZL,      r8
	add   ZL,      r20
	adc   ZH,      r6
	add   ZL,      r20
	adc   ZH,      r6
	lpm   r16,     Z+
	cpi   r16,     0xFF
	breq  m17_p1_empty
	lpm   r17,     Z
	ld    r23,     X+                      ; its attributes
	ld    r19,     X                       ; its x (X stays at this byte, the entry's last)
	; its place in the picture, as pass 2 has it: x and y become the picture's, and an
	; entry more than 7 pixels left of the picture or above it, or right of its last column,
	; is out of play
	sub   r19,     r3
	brcs  m17_p1_left
	cpi   r19,     8 * M17_COLS
	brsh  m17_p1_out
m17_p1_x_in:
	sub   r18,     r12
	brcs  m17_p1_above
m17_p1_y_in:
	mov   r24,     r17                     ; its class: the last when its tile is marked
	or    r24,     r11                     ; (M17_BOX_LAST, bit 7), and whose trip this is
	brmi  m17_p1_other                     ; (bit 7 the last's): looked at there
m17_p1_mine:
	sbrs  r23,     M17_SPR_FLIP_X
	rjmp  1f
	swap  r17                              ; flipped: 7 - last, 7 - first (a nibble's low 3 bits:
	com   r17                              ; the others are masked where it is read)
1:	sbrs  r23,     M17_SPR_FLIP_Y
	rjmp  1f
	swap  r16
	com   r16
1:	; the bands of 8 lines its first and last opaque lines are in, 0 to 31: the picture's
	; rows, and past them the lines under it and above it (which are 249 to 255)
	mov   r20,     r16
	andi  r20,     7
	add   r20,     r18
	lsr   r20
	lsr   r20
	lsr   r20
	mov   r21,     r16
	swap  r21
	andi  r21,     7
	add   r21,     r18
	lsr   r21
	lsr   r21
	lsr   r21
	add   r19,     r2                      ; its x in the playfield's pixels
m17_p1_cols:
	; the cell its first opaque column is in, in the section whose scroll is r2 and whose
	; pixels r19 is in (the playfield; the HUD's row, which has no scroll, comes here again
	; with T set), and whether its last one is in the next cell
	mov   r18,     r17
	andi  r18,     7
	add   r18,     r19                     ; its first opaque column
	mov   r24,     r17
	swap  r24
	andi  r24,     7
	add   r24,     r19                     ; its last one
	eor   r24,     r18
	andi  r24,     8                       ; 8: in the next cell
	andi  r18,     0xF8                    ; the first cell's column, 8 bytes each in a row of VRAM
	mov   r22,     r18
	sub   r22,     r2                      ; ... and its left edge in the picture: less the scroll
	cpi   r22,     8 * M17_COLS - 8
	brsh  m17_p1_edge                      ; at an edge of the picture
	ldi   r22,     1                       ; both cells are in it: bit 0 the first, bit 3 the next
	or    r22,     r24                     ; (and bits 4-7 its cells that count, so far: none)
m17_p1_cols_made:
	brts  m17_p1_to_hud_cols
m17_p1_cells:
m17_p1_band:
	mov   YL,      r20
	subi  YL,      1                       ; the playfield's row: the HUD's is the band before
	brcs  m17_p1_hud                       ; its first
	cpi   YL,      M17_PF_ROWS
	brsh  m17_p1_band_next                 ; above the picture or under it
	cpi   r25,     M17_DRAW_MAX
	brsh  m17_p1_to_cut                    ; it has a cell, and is one more than a call draws
	mov   YH,      YL
	lsr   YH
	lsr   YH
	lsr   YH
	subi  YH,      hi8(-(vram))            ; 8 rows a page (vram starts one)
	andi  YL,      7
	or    YL,      r18                     ; ... 8 bytes a column
	sbrs  r22,     0
	rjmp  1f
	subi  r22,     -16                     ; it counts (m17_p1_tile takes it off when it does not)
	ld    r24,     Y
	cpi   r24,     M17_RAM_INDEXES
	brlo  1f                               ; a RAM tile already
	rcall m17_p1_tile
1:	sbrs  r22,     3
	rjmp  m17_p1_band_next
	subi  YL,      -8                      ; the next column (a row wraps round by itself)
	subi  r22,     -16
	ld    r24,     Y
	cpi   r24,     M17_RAM_INDEXES
	brlo  m17_p1_band_next
	rcall m17_p1_tile
m17_p1_band_next:
	cp    r20,     r21
	breq  m17_p1_taken
	mov   r20,     r21                     ; its last line's band is another
	rjmp  m17_p1_band
m17_p1_taken:
	cpi   r22,     16                      ; a turn taken, unless nothing of it can show (no cell
	sbci  r25,     -1                      ; counted): every cell of it solid art over a sprite behind
m17_p1_done:
	inc   XL                               ; the next entry (the list's last: XL wraps)
	rjmp  m17_p1_next

m17_p1_to_cut:
	rjmp  m17_p1_cut
m17_p1_to_hud_cols:
	rjmp  m17_p1_hud_cols

m17_p1_edge:
	; At an edge of the picture. A cell is in it when its left edge is under 8 x M17_COLS,
	; or 7 pixels at most left of the picture: with 7 added, under 8 x M17_COLS + 7
	subi  r22,     -7
	cpi   r22,     8 * M17_COLS + 7
	sbc   r16,     r16
	andi  r16,     1                       ; the first cell
	subi  r22,     -8
	cpi   r22,     8 * M17_COLS + 7
	brsh  1f
	or    r16,     r24                     ; the next one, when the box reaches it
1:	mov   r22,     r16
	brts  m17_p1_hud_cols
	tst   r22
	brne  m17_p1_cells
	rjmp  m17_p1_done                      ; it has no cell in the picture

m17_p1_hud:
	; The HUD's row. It does not scroll: its columns are the box's made again with no scroll
	; (the box in r17 is still the sprite's, and r19 its x with the playfield's scroll in it:
	; the HUD's is the first band that has cells). Its cells are a byte a column, their flash
	; tiles its bank's (and the blank, which is named by its index in the playfield's bank, is
	; another tile there: none)
	push  r18
	push  r22
	sub   r19,     r2                      ; its x in the picture again
	clr   r2
	set
	rjmp  m17_p1_cols
m17_p1_hud_cols:
	clt
	lds   r2,      Screen + M17_SCR_SCROLLX
	tst   r22
	breq  2f                               ; no cell of it is in the row
	cpi   r25,     M17_DRAW_MAX
	brsh  m17_p1_hud_cut
	push  r5
	push  r13
	clr   r13
	rcall m17_hud_page
	sbrc  r22,     0
	rcall m17_p1_hudcell
	subi  r18,     -8                      ; the next column
	sbrc  r22,     3
	rcall m17_p1_hudcell
	pop   r13
	pop   r5
2:	andi  r22,     0xF0                    ; (its cells that counted here)
	pop   r24                              ; the playfield's columns again, for the row under
	or    r22,     r24
	pop   r18
	rjmp  m17_p1_band_next
m17_p1_hudcell:
	mov   YL,      r18                     ; the column's left edge in the picture
	lsr   YL
	lsr   YL
	lsr   YL
	subi  YL,      lo8(-(m17_hud))         ; (the row is in one page)
	ldi   YH,      hi8(m17_hud)
	subi  r22,     -16
	ld    r24,     Y
	cpi   r24,     M17_RAM_INDEXES
	brlo  1f
	rjmp  m17_p1_tile
1:	ret
m17_p1_hud_cut:
	pop   r22
	pop   r18

m17_p1_cut:
	mov   ZL,      XL                      ; this entry (X is at its last byte, in the list's
	lsr   ZL                               ; page) and those after it wait for the next call
	lsr   ZL
	mov   r24,     r11                     ; with its class in bits 6-7, which are the trip's
	andi  r24,     0xC0
	or    ZL,      r24
	sts   m17_cut, ZL

#elif M17_PART == 2
; ---- part 2: its routine: a cell's RAM tile
;-----------------------------------------------------------------------------
; Pass 1: the cell at Y, which shows the flash tile whose index is r24, gets a RAM tile if one
; is left; not for a sprite behind the background (r23) when that tile is solid, and the cell
; is then taken off r22's high nibble, which counts the cells that have a RAM tile, get one or
; go without. r5 is the page the cell's flash tiles start in, r13 the index of the blank
; among them (0: none) and r10 the byte a RAM tile of the blank is filled with. Y is the cell
; again at the end and X is kept; r16, r17, r24, r0, r1 and Z are used
;-----------------------------------------------------------------------------
m17_p1_solid:
	subi  r22,     16                      ; nothing of the sprite shows here: no tile for it
	ret
m17_p1_tile:
	ldi   r16,     M17_TILE_BYTES
	mul   r24,     r16
	movw  ZL,      r0
	add   ZH,      r5                      ; the flash tile
	sbrs  r23,     M17_SPR_BEHIND
	rjmp  4f
	cpi   ZL,      lo8(m17_solid_lo)       ; behind the background: is the tile one of the
	ldi   r16,     hi8(m17_solid_lo)       ; solid run's?
	cpc   ZH,      r16
	brlo  4f
	cpi   ZL,      lo8(m17_solid_hi)
	ldi   r16,     hi8(m17_solid_hi)
	cpc   ZH,      r16
	brlo  m17_p1_solid
4:	ldi   r16,     M17_RAM_TILES
	cp    r7,      r16
	brsh  m17_p1_short
	movw  r16,     YL                      ; the cell, for when its tile is whole
	movw  YL,      r14                     ; the restore list: the cell, its flash tile
	st    Y+,      r16
	st    Y+,      r17
	st    Y+,      r24
	movw  r14,     YL
	ldi   YL,      M17_TILE_BYTES
	mul   r7,      YL
	movw  YL,      r0
	subi  YH,      hi8(-(m17_ram_tiles))   ; the RAM tile (they start a page)
	cp    r24,     r13                     ; the blank? (The tile most cells under a sprite show;
	brne  m17_p1_copy                      ; r13 is 0, no tile's index, in the HUD's row)
m17_p1_fill:
#if M17_TILE_BYTES != 32 || (M17_RUN_TILES & 31)
	#error the fill is two runs of 16 bytes, and a RAM tile starts at a multiple of 32
#endif
1:	st    Y+,      r10                     ; its one byte, 32 times: 16 and 16 (a RAM tile
	st    Y+,      r10                     ; starts at a multiple of 32, so bit 4 of where the
	st    Y+,      r10                     ; next byte goes says which run this was)
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	st    Y+,      r10
	sbrc  YL,      4
	rjmp  1b
m17_p1_shows:
	movw  YL,      r16                     ; the cell shows the RAM tile, now that it is whole: a
	st    Y,       r7                      ; picture that cuts this pass draws no tile half made
	inc   r7
	ret
m17_p1_copy:
	mov   r0,      XL                      ; (the list's place, kept: X is the translation's here)
	ldi   XH,      hi8(m17_t1)
	ldi   r24,     M17_TILE_BYTES / 4
1:	lpm   XL,      Z+                      ; a flash byte, palette and pair ...
	subi  XL,      lo8(-(M17_XLAT_OFS))
	ld    r1,      X                       ; ... as a pair of the screen's colours
	st    Y+,      r1
	lpm   XL,      Z+
	subi  XL,      lo8(-(M17_XLAT_OFS))
	ld    r1,      X
	st    Y+,      r1
	lpm   XL,      Z+
	subi  XL,      lo8(-(M17_XLAT_OFS))
	ld    r1,      X
	st    Y+,      r1
	lpm   XL,      Z+
	subi  XL,      lo8(-(M17_XLAT_OFS))
	ld    r1,      X
	st    Y+,      r1
	dec   r24
	brne  1b
	mov   XL,      r0
	ldi   XH,      hi8(m17_sprites)
	rjmp  m17_p1_shows

m17_p1_short:
	lds   r24,     m17_cells_short
	tst   r24
	brne  1f
	mov   ZL,      XL                      ; the first: its entry (X is at its last byte, in the
	lsr   ZL                               ; list's page) is where the next call starts
	lsr   ZL
	sts   m17_next, ZL
1:	inc   r24
	breq  2f                               ; (255 and no more)
	sts   m17_cells_short, r24
2:	ret


#endif
