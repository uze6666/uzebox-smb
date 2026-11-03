;=============================================================================
; Video mode 17's compositor, pass 2 (the pixels): a part of m17_sprites.s, which has the
; compositor's header (what the passes do, the registers they keep) and includes this
; file once a part (M17_PART), each where its code goes.
;
; The tables of codes. A pair of a sprite's line is a nibble of its tile (the left pixel in
; bits 0-1), and what is written for it is the code of its two slots: the palette's slots for
; that nibble (T1's page), turned round for a sprite flipped in x, then m17_enc15 of them.
; A table has a palette's 15 codes ready, by the nibble: one for the sprites not flipped in
; x, one for those flipped, 16 bytes each. A table's first byte, which no pair reads (a nibble
; of 0 is two transparent pixels), is the palette it was made for: a sprite whose palette is
; another makes its flip's table again (m17_p2_codes, about 195 cycles: 3 to 4 times a call in
; a crowd, where a table serves 6 sprites). So a pair with both pixels is one byte read and
; one written. A pair with one pixel is its own code too where the byte it goes to is 0 (the
; backdrop twice: the other pixel's slot is 0 in the code, and 0 under it); over another
; byte, the two are taken apart again (m17_pair_merge).
; The tables are in a frame on the stack that this pass takes when it starts (M17_P2_FRAME,
; m17_sprites.s, which gives it back at the call's end: 47 bytes, so that 32 of them start at
; a multiple of 16 wherever the stack is: a table is then in one page, and a nibble is or-ed
; into its address). The stack pointer is moved by libgcc's own routine, the one the call's
; registers are saved by, entered where it makes a frame: no instruction of the compositor
; runs with interrupts off.
;=============================================================================

; r12, the entry's attributes, in this pass: bit 3 (the NES leaves it unused, as it does the
; odd x's bit 2) says of a row of cells that its lines go by the pairs of a sprite behind the
; background (m17_p2_under). m17_p2_place clears it for each sprite
#ifndef M17_SPR_ART
#define M17_SPR_ART     3
#endif
; From the stack pointer, as the pass has it between two sprites, to the first table, before
; that is rounded down to a multiple of 16: the frame's first byte, and 15
#define M17_P2_TABLES   (1 + 15)
#if M17_P2_FRAME < 32 + 15
	#error the frame is too small for the two tables of codes at a multiple of 16
#endif

#if M17_PART == 1
; ---- part 1: the pass: the entries from the last to the first
	;---------------------------------------------------------------------
	; Pass 2: the pixels, the last entry first
	;---------------------------------------------------------------------
	; The pass's frame, under the hand-over's two bytes: libgcc's __prologue_saves__ from where
	; it has saved its registers (as the compiler enters it for a function that saves none):
	; the stack pointer less r27:r26, and on at Z. Y is the new stack pointer
	ldi   r26,     M17_P2_FRAME
	ldi   r27,     0
	ldi   r30,     pm_lo8(m17_p2_framed)
	ldi   r31,     pm_hi8(m17_p2_framed)
	jmp   __prologue_saves__ + ((18 - 0) * 2)
m17_p2_framed:
	; r7, for m17_p2_under: the tile the program named as blank, as a cell holds it, when its
	; byte has no art (a pair code of 0); else 0, which no cell holds
	lds   r7,      m17_blank
	ldi   r16,     M17_TILE_BYTES
	mul   r7,      r16
	movw  ZL,      r0
	add   ZH,      r5
	lpm   r16,     Z
	andi  r16,     15
	breq  1f
	clr   r7
1:	; the two tables of codes: neither is any palette's yet
	in    YL,      _SFR_IO_ADDR(SPL)
	in    YH,      _SFR_IO_ADDR(SPH)
	adiw  YL,      M17_P2_TABLES
	andi  YL,      0xF0
	ldi   r16,     0xFF
	st    Y,       r16
	std   Y+16,    r16
	ldi   XL,      lo8(4 * M17_SPRITES)    ; (the list starts a page, m17.def.h's run: an entry's
	ldi   XH,      hi8(m17_sprites)        ; place is its low byte alone)
m17_p2_entry:
	subi  XL,      4
	ld    r16,     X                       ; its y
	cpi   r16,     M17_SPR_OFF
	brlo  m17_p2_shown
m17_p2_next:
	cpi   XL,      lo8(m17_sprites)
	brne  m17_p2_entry
	rjmp  m17_p2_end

m17_p2_shown:
	movw  YL,      XL
	ldd   r18,     Y+1                     ; its tile
	mov   r11,     r18
	cpi   r18,     M17_SPR_NONE            ; the tile the program named as empty (m17.def.h): it
	breq  m17_p2_next                      ; draws nothing, and is passed as an entry that is off
	lds   r17,     m17_span
	cpi   r17,     M17_SPRITES
	breq  2f                               ; every entry is drawn
	; The call was cut: is this entry one pass 1 came to? By its class against the cut's and,
	; in the cut's class, by how far it is from the start. The first class is the entry's
	; number; the last is its tile's box, which is read only when the rest does not say.
	; r19: the cut's class in bits 6-7, as m17_cut has it (0x40 the second class's trip, 0x80
	; the last's; here both, for an entry of the first class when the cut is in the first)
	lds   r19,     m17_cut
#if M17_FIRST_N
#if M17_FIRST_N < 64
	mov   r18,     XL
	subi  r18,     lo8(m17_sprites + 4 * M17_FIRST_AT)
	cpi   r18,     4 * M17_FIRST_N
	brsh  3f
#endif
	cpi   r19,     0x40
	brsh  2f                               ; an entry of the first class, the cut in a later one: drawn
	ori   r19,     0xC0                    ; ... in the first: by how far it is
	rjmp  4f
3:
#endif
	cpi   r19,     0x40
	brlo  m17_p2_next                      ; the cut is in the first class: every other entry waits
4:	mov   r18,     XL                      ; how far it is from the start
	subi  r18,     lo8(m17_sprites)
	lsr   r18
	lsr   r18
	lds   r0,      m17_rotate
	sub   r18,     r0
	brcc  1f
	subi  r18,     lo8(-(M17_SPRITES))
1:	cp    r18,     r17
	brcs  5f                               ; within what pass 1 came to in the cut's class
	sbrs  r19,     7
	rjmp  m17_p2_next                      ; past it, the cut in the second class: it waits
	sbrc  r19,     6
	rjmp  m17_p2_next                      ; ... an entry of the first class: it waits
	rjmp  6f                               ; ... the cut in the last class: drawn unless it is of the last
5:	sbrc  r19,     7
	rjmp  2f                               ; within, in the last class's trip or the first's: drawn
6:	lds   ZL,      m17_spr_boxes           ; within the second class's trip, or past the last's:
	lds   ZH,      m17_spr_boxes + 1       ; drawn unless its tile is of the last class
	add   ZL,      r11
	adc   ZH,      r6
	add   ZL,      r11
	adc   ZH,      r6
	adiw  ZL,      1
	lpm   r18,     Z
	sbrc  r18,     M17_BOX_LAST
	rjmp  m17_p2_next
2:	mov   r8,      XL                      ; (the list's place: its low byte)
	ldd   r12,     Y+2
	ldd   r19,     Y+3
	; Its place in the picture, as pass 1 has it: x and y become the picture's (the list's less
	; the origin: Screen's spriteX in r3, spriteY), and an entry more than 7 pixels left of the
	; picture or above it, or right of its last column, is out of play. The differences are
	; exact, not modulo 256: the borrow says which side of the origin, so an x of 249 at an
	; origin of 0 is off the right edge and not 7 pixels left of the picture
	sub   r19,     r3
	brcs  m17_p2_left
	cpi   r19,     8 * M17_COLS
	brsh  m17_p2_out
m17_p2_x_in:
	lds   r18,     Screen + M17_SCR_SPRITEY
	sub   r16,     r18
	brcs  m17_p2_above
m17_p2_y_in:
	mov   r10,     r16
	mov   r13,     r19
	; its tile, 16 bytes: from the last line when flipped in y
	lds   r14,     m17_spr_tiles
	lds   r15,     m17_spr_tiles + 1
	ldi   r16,     16
	mul   r11,     r16
	add   r14,     r0
	adc   r15,     r1
	sbrs  r12,     M17_SPR_FLIP_Y
	rjmp  1f
	ldi   r16,     14
	add   r14,     r16
	adc   r15,     r6
1:	; the table of codes for its flip: its palette's already, or made for it now
	in    ZL,      _SFR_IO_ADDR(SPL)
	in    ZH,      _SFR_IO_ADDR(SPH)
	adiw  ZL,      M17_P2_TABLES
	andi  ZL,      0xF0
	bst   r12,     M17_SPR_FLIP_X
	brtc  1f
	adiw  ZL,      16
1:	mov   r11,     ZL
	mov   r9,      ZH
	mov   r16,     r12
	andi  r16,     3
	ld    r17,     Z
	cpse  r17,     r16
	rcall m17_p2_codes
	; where it is in VRAM's pixels: the first cell's column, the pairs that go to each cell
	add   r13,     r2
	rcall m17_p2_place
	; its lines in the row of the first (8 less its line in that row), then those in the next row
	mov   r19,     r10
	ori   r19,     0xF8
	neg   r19
	mov   r18,     r10
	rcall m17_p2_rows
	mov   r19,     r10
	andi  r19,     7
	breq  1f                               ; it had no other
	mov   r18,     r10
	ori   r18,     7
	inc   r18
	rcall m17_p2_rows
1:
m17_p2_done:
	mov   XL,      r8
	ldi   XH,      hi8(m17_sprites)
m17_p2_out:
	rjmp  m17_p2_next
	; (what a sprite seldom comes to, within a branch's reach of where it does)
m17_p2_left:
	cpi   r19,     256 - 7                 ; left of the origin: 7 pixels at most
	brsh  m17_p2_x_in
	rjmp  m17_p2_next
m17_p2_above:
	cpi   r16,     256 - 7                 ; above it: 7 lines at most
	brsh  m17_p2_y_in
	rjmp  m17_p2_next

#elif M17_PART == 2
; ---- part 2: a sprite's place in a section's cells
;-----------------------------------------------------------------------------
; Pass 2: a sprite's place in a section's cells. r13 its x in the section's pixels (the
; scroll added) -> r13 the first cell's column times 8 (its bytes in a row of VRAM), r24 and
; r25 the pairs that go to that cell and to the next (0: none goes there), r12's M17_SPR_ODD
; an odd x (5 pairs)
;-----------------------------------------------------------------------------
m17_p2_place:
	mov   r16,     r13
	andi  r16,     7
	sub   r13,     r16
	mov   r25,     r16
	lsr   r25
	ldi   r24,     4
	sub   r24,     r25
	mov   r17,     r12
	andi  r17,     lo8(~((1 << M17_SPR_ODD) | (1 << M17_SPR_ART)))
	sbrs  r16,     0
	rjmp  1f
	ori   r17,     1 << M17_SPR_ODD
	inc   r25
1:	mov   r12,     r17
	ret

#elif M17_PART == 3
; ---- part 3: its routines: the HUD's row, a row's lines, a line's pairs, behind the background
;-----------------------------------------------------------------------------
; Pass 2: r4 lines of the sprite in the HUD's row, the first the picture's line r18. The
; row does not scroll: the sprite's place in its cells is made again from its x (the entry's,
; less the origin's), and put back for the playfield's lines after
;-----------------------------------------------------------------------------
m17_p2_hud:
	push  r5
	rcall m17_hud_page
	mov   YL,      r8
	ldi   YH,      hi8(m17_sprites)
	ldd   r13,     Y+3
	sub   r13,     r3
	rcall m17_p2_place
	lsr   r13
	lsr   r13
	lsr   r13                              ; the first cell's column
	; behind the background: always by the slower pairs here, and m17_p2_drawn does not ask
	; (the blank's index names another tile in the HUD's bank). r17 is r12, as the place left it
	sbrc  r17,     M17_SPR_BEHIND
	subi  r17,     (1 << M17_SPR_BEHIND) - (1 << M17_SPR_ART)
	mov   r12,     r17
	lsl   r18
	lsl   r18                              ; the line's 4 bytes in a tile
	ldi   YH,      hi8(m17_hud)
	mov   YL,      r13
	rcall m17_p2_hudcell
	movw  r20,     r22
	tst   r21
	breq  1f
	subi  r20,     lo8(-(4))
	sub   r20,     r24                     ; from the pair the sprite starts in: 4 less its pairs here
1:	mov   YL,      r13
	inc   YL
	andi  YL,      M17_VRAM_H - 1
	rcall m17_p2_hudcell
	tst   r25
	brne  2f
	clr   r23                              ; no pair goes to the cell after: it is none
2:	rcall m17_p2_drawn
	pop   r5
	mov   YL,      r8                      ; the playfield's place again, from the entry
	ldi   YH,      hi8(m17_sprites)
	ldd   r12,     Y+2
	ldd   r13,     Y+3
	sub   r13,     r3
	add   r13,     r2
	rjmp  m17_p2_place
; the HUD's cell of column YL: r22:r23 its RAM tile's line r18, r23 0 without one or when the
; column is not the picture's (a sprite's first cell left of it, its second right of it)
m17_p2_hudcell:
	clr   r23
	cpi   YL,      M17_COLS
	brsh  1f
	subi  YL,      lo8(-(m17_hud))
	ld    r17,     Y
	cpi   r17,     M17_RAM_INDEXES
	brsh  1f
	ldi   r19,     M17_TILE_BYTES
	mul   r17,     r19
	movw  r22,     r0
	subi  r23,     hi8(-(m17_ram_tiles))
	or    r22,     r18
1:	ret

; nothing to draw into: past these r4 lines of the tile, 2 bytes each (back, when flipped in y)
m17_p2_passed:
	mov   r16,     r4
	lsl   r16
	sbrc  r12,     M17_SPR_FLIP_Y
	rjmp  1f
	add   r14,     r16
	adc   r15,     r6
	ret
1:	sub   r14,     r16
	sbc   r15,     r6
	ret

;-----------------------------------------------------------------------------
; Pass 2: r19 lines of the sprite, the first at picture line r18, all in one row of cells
;-----------------------------------------------------------------------------
m17_p2_to_hud:
	rjmp  m17_p2_hud                       ; (out of a branch's reach from the row's test)
m17_p2_rows:
	mov   r4,      r19
	cpi   r18,     M17_HUD_LINES
	brlo  m17_p2_to_hud                    ; in the HUD's row
	cpi   r18,     8 * M17_ROWS
	brsh  m17_p2_passed                    ; under the playfield
	mov   r16,     r18
	subi  r16,     M17_HUD_LINES
	lsr   r16
	lsr   r16
	lsr   r16                              ; the row
	mov   YH,      r16
	lsr   YH
	lsr   YH
	lsr   YH
	subi  YH,      hi8(-(vram))
	andi  r16,     7
	andi  r18,     7
	lsl   r18
	lsl   r18                              ; the line's 4 bytes in a tile
	; the first cell: where the line starts in its RAM tile (a high byte of 0: it has none)
	clr   r21
	mov   YL,      r13
	or    YL,      r16
	ld    r17,     Y
	cpi   r17,     M17_RAM_INDEXES
	brsh  1f                               ; no RAM tile: nothing is drawn there
	ldi   r19,     M17_TILE_BYTES
	mul   r17,     r19
	movw  r20,     r0
	subi  r21,     hi8(-(m17_ram_tiles))
	or    r20,     r18
	subi  r20,     lo8(-(4))
	sub   r20,     r24                     ; from the pair the sprite starts in: 4 less its pairs here
1:	; the cell after it: 8 bytes on, round VRAM's 32 columns (the low byte alone). None when
	; no pair goes to it
	clr   r23
	subi  YL,      lo8(-(8))
	tst   r25
	breq  2f
	ld    r17,     Y
	cpi   r17,     M17_RAM_INDEXES
	brsh  2f
	ldi   r19,     M17_TILE_BYTES
	mul   r17,     r19
	movw  r22,     r0
	subi  r23,     hi8(-(m17_ram_tiles))
	or    r22,     r18
2:
m17_p2_drawn:
	mov   r16,     r21
	or    r16,     r23
	breq  m17_p2_passed                    ; nothing to draw into
	sbrc  r12,     M17_SPR_BEHIND
	rjmp  m17_p2_under                     ; behind the background: by which pairs, these rows?

m17_p2_line:
	movw  ZL,      r14
	lpm   r16,     Z+                      ; pixels 0-3, 2 bits each, the left one low
	lpm   r17,     Z+                      ; pixels 4-7
	sbrc  r12,     M17_SPR_FLIP_Y
	sbiw  ZL,      4
	movw  r14,     ZL
	; (a line with no pixel is not looked for: its pairs are passed one by one, which writes
	; nothing either. They are 3 lines in 100, and the look cost every line 4 cycles)
	; The line's pairs in the order they are drawn, to m17_nib: a pair's nibble is its byte's
	; low half (what is above it is masked where the byte is read: m17_pair, m17_pairs_behind).
	; A way each for an even x and an odd one, flipped in x or not: the store is by address
	sbrc  r12,     M17_SPR_ODD
	rjmp  m17_p2_odd
	brts  m17_p2_even_flipped
	sts   m17_nib,     r16                 ; an even x: the tile's 4 pairs
	swap  r16
	sts   m17_nib + 1, r16
	sts   m17_nib + 2, r17
	swap  r17
	sts   m17_nib + 3, r17
m17_p2_stored:
	ldi   XL,      lo8(m17_nib)
	ldi   XH,      hi8(m17_nib)
	sbrc  r12,     M17_SPR_ART
	rjmp  m17_p2_behind

	; the first cell's pairs, then the next cell's, a loop each: r19 pairs, their nibbles at X,
	; into the RAM tile's bytes at Y. Z is the sprite's table of codes
	mov   ZH,      r9
	mov   r19,     r24
	movw  YL,      r20
	tst   r21
	brne  m17_pair
	add   XL,      r24                     ; it has no RAM tile: its pairs are passed
	adc   XH,      r6
	rjmp  m17_pair_cell
	; (the pairs of a line flipped in x at an even x: here, within its branch's reach)
m17_p2_even_flipped:
	sts   m17_nib + 3, r16
	swap  r16
	sts   m17_nib + 2, r16
	sts   m17_nib + 1, r17
	swap  r17
	sts   m17_nib,     r17
	rjmp  m17_p2_stored
m17_pair_none:
	inc   YL                               ; two transparent pixels (a tile's line is in one page)
	dec   r19
	breq  m17_pair_cell
m17_pair:
	ld    r16,     X+
	andi  r16,     15                      ; the pair's nibble
	breq  m17_pair_none                    ; two transparent pixels
	mov   ZL,      r16
	or    ZL,      r11
	ld    r0,      Z                       ; the pair's code, by the sprite's table
	cpi   r16,     4
	brlo  m17_pair_one                     ; the tile's left pixel alone (r16 is not 0)
	andi  r16,     3
	breq  m17_pair_one                     ; its right one alone (r16 is 0)
m17_pair_put:
	st    Y+,      r0
	dec   r19
	brne  m17_pair
m17_pair_cell:
	; the cell after (it is there only when a pair goes to it and it has a RAM tile:
	; m17_p2_rows): the same loop again, which ends the line
	tst   r23
	breq  m17_p2_line_done
	mov   r19,     r25
	movw  YL,      r22
m17_pair2:
	ld    r16,     X+
	andi  r16,     15
	breq  m17_pair2_none
	mov   ZL,      r16
	or    ZL,      r11
	ld    r0,      Z
	cpi   r16,     4
	brlo  m17_pair2_one
	andi  r16,     3
	breq  m17_pair2_one
m17_pair2_put:
	st    Y+,      r0
	dec   r19
	brne  m17_pair2

m17_p2_line_done:
	subi  r20,     lo8(-(4))               ; the next line of both tiles (a tile is in one page;
	subi  r22,     lo8(-(4))               ; a cell with none has 0 for its high byte, which stays)
	dec   r4
	breq  1f
	rjmp  m17_p2_line
1:	ret

m17_pair2_none:
	inc   YL
	dec   r19
	brne  m17_pair2
	rjmp  m17_p2_line_done
; A pair with one pixel. Its code has slot 0 for the other (the translation has slot 0, the
; backdrop's, for a value of 0, whatever the palette: videoMode.c's slot_of[][0]), so over a
; byte that is 0, the backdrop twice, it is the byte to write as it is
m17_pair_one:
	ld    r18,     Y
	cpse  r18,     r6
	rcall m17_pair_merge
	st    Y+,      r0
	dec   r19
	brne  m17_pair
	rjmp  m17_pair_cell
m17_pair2_one:
	ld    r18,     Y
	cpse  r18,     r6
	rcall m17_pair_merge
	st    Y+,      r0
	dec   r19
	brne  m17_pair2
	rjmp  m17_p2_line_done
; ... over another byte: that byte's other pixel stays. r18 the byte, r0 the pair's code, r16
; not 0 for the tile's left pixel alone and 0 for its right one, T the pair turned round: r0
; the byte to write
m17_pair_merge:
	ldi   r17,     0xF0                    ; the slot that stays: the left one under a right pixel,
	cpse  r16,     r6
	swap  r17                              ; the right one under a left pixel,
	brtc  1f
	swap  r17                              ; and the other when the pair was turned round
1:	mov   ZL,      r18
	ldi   ZH,      hi8(m17_dec15)
	lpm   r18,     Z                       ; the two slots under it
	and   r18,     r17
	mov   ZL,      r0
	lpm   r17,     Z                       ; the pair's own two
	or    r17,     r18
	mov   ZL,      r17
	ldi   ZH,      hi8(m17_enc15)
	lpm   r0,      Z
	mov   ZH,      r9
	ret

;-----------------------------------------------------------------------------
; Pass 2, a sprite behind the background, a row of cells it has a RAM tile in: by which pairs
; do its lines there go? By those of a sprite in front when every cell its pairs go to shows
; the tile the program named as blank and that tile's byte has no art: the background has
; no pixel to keep there, and the rule's byte for a pair is the front's. Else by the slower
; pairs: r12's M17_SPR_ART. From m17_p2_drawn (the playfield's rows: Y at the second cell,
; r17 what it holds), and on to the lines.
; (In a level of pipes every pair of the plants that showed was over the blank: 18 sprites
; took 3,000 cycles each where they take 2,000 as sprites in front.)
;-----------------------------------------------------------------------------
m17_p2_under:
	ldi   r19,     1 << M17_SPR_ART
	or    r12,     r19                     ; by the slower pairs, unless ...
	cp    r6,      r6                      ; (no pair goes to the cell after: whatever it shows)
	cpse  r25,     r6
	rcall m17_p2_bare                      ; the cell after: r17 is what it holds, Y is at it
	brne  1f
	subi  YL,      8
	ld    r17,     Y                       ; the first cell
	rcall m17_p2_bare
	brne  1f
	eor   r12,     r19                     ; ... no pair of these rows is over art
1:	rjmp  m17_p2_line
; r17 what a cell holds: the Z flag set when no pair that goes there is over art, by the
; cell's tile alone: it has no RAM tile (nothing is drawn there), or it shows the blank (r7)
m17_p2_bare:
	cpi   r17,     M17_RAM_INDEXES
	brsh  1f
; r17 a RAM tile -> r16 the flash tile its cell shows (the restore list has it); the Z flag
; set when that is the blank with no art (Z, r0 and r1 are used)
m17_p2_shows:
	ldi   r16,     3
	mul   r17,     r16
	movw  ZL,      r0
	subi  ZL,      lo8(-(m17_restore + 2))
	sbci  ZH,      hi8(-(m17_restore + 2))
	ld    r16,     Z
	cp    r16,     r7
	ret
1:	sez
	ret

;-----------------------------------------------------------------------------
; Pass 2: a table of codes made. Z the table (r9, r11), r16 the palette (the attributes' low 2
; bits), T the sprite flipped in x. The table's first byte is the palette; the 15 after it
; the RAM tile's code for each nibble of a sprite's tile in that palette: m17_enc15 of its two
; slots, left << 4 | right (a transparent pixel's is 0), turned round when flipped. X, Y, Z,
; r0, r16 and r17 are used
;-----------------------------------------------------------------------------
m17_p2_codes:
	st    Z+,      r16
	movw  YL,      ZL
	swap  r16
	subi  r16,     lo8(-(M17_SPRX_OFS + 1))
	mov   XL,      r16                     ; the palette's nibbles in the translation, from 1
	ldi   XH,      hi8(m17_t1)
	ldi   ZH,      hi8(m17_enc15)
	ldi   r17,     15
1:	ld    ZL,      X+
	brtc  2f
	swap  ZL
2:	lpm   r0,      Z
	st    Y+,      r0
	dec   r17
	brne  1b
	ret

; A line's pairs to m17_nib (m17_p2_line) at an odd x: the line is a pixel to the right, 5
; pairs, the first and the last of one pixel. Flipped in x (here and at an even x, which is
; beside the pairs' loop): the last pair first (each is turned round by the table of codes,
; or by m17_pairs_behind)
m17_p2_odd:
	clr   r18
	lsl   r16
	rol   r17
	rol   r18
	lsl   r16
	rol   r17
	rol   r18
	brts  1f
	sts   m17_nib,     r16
	swap  r16
	sts   m17_nib + 1, r16
	sts   m17_nib + 2, r17
	swap  r17
	sts   m17_nib + 3, r17
	sts   m17_nib + 4, r18                 ; (the fifth pair: the last pixel)
	rjmp  m17_p2_stored
1:	sts   m17_nib + 4, r16
	swap  r16
	sts   m17_nib + 3, r16
	sts   m17_nib + 2, r17
	swap  r17
	sts   m17_nib + 1, r17
	sts   m17_nib,     r18
	rjmp  m17_p2_stored

; A line of a sprite behind the background: the same two cells, by the slower pairs below
m17_p2_behind:
	tst   r21
	brne  1f
	add   XL,      r24
	adc   XH,      r6
	rjmp  2f
1:	movw  YL,      r20
	mov   r19,     r24
	rcall m17_pairs_behind
2:	tst   r23
	breq  3f
	movw  YL,      r22
	mov   r19,     r25
	rcall m17_pairs_behind
3:	rjmp  m17_p2_line_done


;-----------------------------------------------------------------------------
; Pass 2, behind the background: r19 pairs, their nibbles at X, into the RAM tile's bytes
; at Y. Where the background's pixel is art, it is what is written (over an earlier sprite's
; too); elsewhere the sprite's.
;
; The background's bytes of the line are found once, by the restore list (r20:r21 goes along
; them). A pair with both pixels opaque over art in both is the background's own byte, as
; the tile's copy had it; one with both opaque reads nothing of the RAM tile. (Found a pair
; at a time, and every pair through the two decodings, a list of sprites behind the
; background took half as long again as one in front: a pipe's exit with four plants ran a
; frame in two fields.)
;-----------------------------------------------------------------------------
m17_pairs_behind:
	push  r20
	push  r21
	movw  r16,     YL
	subi  r17,     hi8(m17_ram_tiles)      ; 32 x the RAM tile + the byte in it
	lsl   r16
	rol   r17
	lsl   r16
	rol   r17
	lsl   r16
	rol   r17                              ; r17: the RAM tile
	rcall m17_p2_shows                     ; r16: its cell's flash tile
	ldi   r17,     M17_TILE_BYTES
	mul   r16,     r17
	movw  r20,     r0
	add   r21,     r5
	mov   r16,     YL
	andi  r16,     M17_TILE_BYTES - 1
	or    r20,     r16                     ; the background's byte for the first pair
1:	ld    r16,     X+
	andi  r16,     15                      ; the pair's nibble
	brne  2f
	adiw  YL,      1
	rjmp  8f
2:	mov   r17,     r16                     ; (the nibble, for its slots)
	ldi   r18,     0xFF                    ; where the sprite is opaque
	cpi   r16,     4
	brsh  3f
	ldi   r18,     0xF0
3:	andi  r16,     3
	brne  4f
	ldi   r18,     0x0F
4:	brtc  5f
	swap  r18
5:	movw  ZL,      r20
	lpm   r0,      Z                       ; the background here: palette << 4 | pair
	mov   ZL,      r0
	ori   ZL,      M17_BGMASK4_AT
	ldi   ZH,      hi8(m17_dec15)
	lpm   r1,      Z                       ; 0xF0 / 0x0F where that pair is art
	mov   r16,     r1
	and   r16,     r18
	cpi   r16,     0xFF
	brne  6f
	mov   ZL,      r0                      ; art under both pixels of an opaque pair: the
	subi  ZL,      lo8(-(M17_XLAT_OFS))    ; background's byte as a RAM tile's
	ldi   ZH,      hi8(m17_t1)
	ld    r0,      Z
	st    Y+,      r0
	rjmp  8f
6:	mov   ZL,      r17
	or    ZL,      r11
	mov   ZH,      r9
	ld    ZL,      Z                       ; the pair's code, by the sprite's table ...
	ldi   ZH,      hi8(m17_dec15)
	lpm   r17,     Z                       ; ... its two slots (turned round already when flipped)
	tst   r1
	breq  9f
	mov   ZL,      r0
	subi  ZL,      lo8(-(M17_XLAT_OFS))
	ldi   ZH,      hi8(m17_t1)
	ld    r0,      Z                       ; the background's byte as a RAM tile's ...
	mov   ZL,      r0
	ldi   ZH,      hi8(m17_dec15)
	lpm   r0,      Z                       ; ... its two slots
	and   r0,      r1
	com   r1
	and   r17,     r1
	or    r17,     r0
9:	cpi   r18,     0xFF
	breq  10f                              ; nothing of the byte shows through
	ld    ZL,      Y                       ; under the sprite's opaque pixels
	ldi   ZH,      hi8(m17_dec15)
	lpm   r16,     Z
	and   r17,     r18
	com   r18
	and   r16,     r18
	or    r17,     r16
10:	mov   ZL,      r17
	ldi   ZH,      hi8(m17_enc15)
	lpm   r0,      Z
	st    Y+,      r0
8:	inc   r20                              ; (a line's bytes are in one page)
	dec   r19
	breq  11f
	rjmp  1b
11:	pop   r21
	pop   r20
	ret

; For the checks (test/m17front.py): what this build's pass 2 was made with
.global m17_config_spr_none
.global m17_config_p2_tables
.global m17_config_p2_frame
.equ m17_config_spr_none, M17_SPR_NONE
.equ m17_config_p2_tables, M17_P2_TABLES
.equ m17_config_p2_frame, M17_P2_FRAME

#endif
