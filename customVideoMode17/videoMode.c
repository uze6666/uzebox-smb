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

/*
 * Video mode 17's C side: VRAM and the colour tables. The frame is videoModeCore.s's.
 */

	#include <stdbool.h>
	#include <avr/io.h>
	#include <string.h>
	#include <avr/pgmspace.h>
	#include "uzebox.h"
	#include "m17_tables.h"

	/* T1's page: 16 entries a palette, a pair code's two colours at [16 p + c] and the entry
	** after it; then the two translations (m17.def.h). T2: the same for a RAM tile's codes */
	extern unsigned char m17_t1[];
	extern unsigned char m17_t2[];
	/* The flash tiles' table, its high byte */
	extern unsigned char m17_tiles_hi;
	/* The sprite tiles and their opaque boxes; the blank tile's VRAM index (m17_sprites.s) */
	extern const char *m17_spr_tiles, *m17_spr_boxes;
	extern unsigned char m17_blank;
	/* A picture's first line was not drawn: none of its lines is (videoModeCore.s) */
	extern volatile unsigned char m17_skipping;

	unsigned char m17_colors_over;

	/* The screen's colours are to be made again from the palettes, at the next composition */
	static unsigned char m17_dirty;

	static unsigned char *m17_cell(unsigned char x, unsigned char y){
		return vram + ((unsigned int)(y >> 3) << 8) + (unsigned char)((x << 3) | (y & 7));
	}

	void SetTileTable(const char *data){
		m17_tiles_hi = (unsigned int)data >> 8;
	}

	void SetTile(char x, char y, unsigned int tileId){
		*m17_cell(x, y) = tileId + M17_RAM_INDEXES;
	}

	unsigned int GetTile(char x, char y){
		return (unsigned char)(*m17_cell(x, y) - M17_RAM_INDEXES);
	}

	void ClearVram(void){
		memset(vram, M17_RAM_INDEXES, M17_VRAM_SIZE);
		memset(m17_hud, M17_RAM_INDEXES, M17_HUD_COLS);
	}

	/* The entries of a palette's 16 whose pair starts with value v; the next palette's */
	static unsigned char *m17_set_value(unsigned char *t, unsigned char v, unsigned char color){
		const unsigned char *d = m17_dec4;
		unsigned char k = M17_PAIRS4;
		do{
			if((pgm_read_byte(d++) >> 4) == v) *t = color;
			t++;
		}while(--k);
		return t;
	}

	void m17_SetBackdrop(unsigned char color){
		unsigned char *t = m17_t1;
		for(unsigned char p = 0; p < M17_PALETTES; p++){
			t = m17_set_value(t, 0, color);
		}
		/* the last palette's last pair ends on it */
		*t = color;
		m17_dirty = 1;
	}

	void m17_SetPalette(unsigned char p, unsigned char c1, unsigned char c2, unsigned char c3){
		if(p >= M17_PALETTES) return;
		unsigned char *t = m17_t1 + 16 * p;
		t = m17_set_value(t, 1, c1) - 16;
		t = m17_set_value(t, 2, c2) - 16;
		m17_set_value(t, 3, c3);
		m17_dirty = 1;
	}

	/* The slot nearest to a colour: red and green are 0-7, blue 0-3 stretched to them (9 x the
	** squared distance, in whole numbers: 9 (dr^2 + dg^2) + 49 db^2, as three squares) */
	static unsigned char m17_nearest(const unsigned char *slots, unsigned char c){
		unsigned char best = 0;
		unsigned int least = 0xffff;
		for(unsigned char k = 0; k < M17_SLOTS; k++){
			signed char dr = 3 * (signed char)((slots[k] & 7) - (c & 7));
			signed char dg = 3 * (signed char)(((slots[k] >> 3) & 7) - ((c >> 3) & 7));
			signed char db = 7 * (signed char)((slots[k] >> 6) - (c >> 6));
			unsigned int d = dr * dr + dg * dg + db * db;
			if(d < least){
				least = d;
				best = k;
			}
		}
		return best;
	}

	#if M17_BG_PALETTES != M17_SPRITE_PAL || M17_SPRITE_PAL + 4 != M17_PALETTES
		#error a palette is either one of the background's first or one of the sprites' 4
	#endif

	/* A slot's colour, read back from T2 at its pair with the backdrop */
	static unsigned char m17_slot_color(unsigned char s){
		return m17_t2[pgm_read_byte(&m17_enc15[s << 4])];
	}

	/* A slot's colour: its 15 entries of T2 (not for the backdrop's slot, which has one more) */
	static void m17_set_slot(unsigned char s, unsigned char color){
		for(unsigned char j = 0; j < M17_SLOTS; j++){
			m17_t2[pgm_read_byte(&m17_enc15[(s << 4) | j])] = color;
		}
	}

	/* A palette's translation, from its 4 values' slots (the first is the backdrop's, 0): a
	** background palette's flash bytes as RAM bytes, a sprite palette's nibbles as two slots */
	static void m17_translate(unsigned char p, const unsigned char *s){
		if(p >= M17_SPRITE_PAL){
			unsigned char *t = m17_t1 + M17_SPRX_OFS + 16 * (p - M17_SPRITE_PAL);
			/* (from 1: a palette's first entry, two transparent pixels, is never read, and the
			** first sprite palette's is the flash bytes' last) */
			for(unsigned char i = 1; i < 16; i++){
				t[i] = (s[i & 3] << 4) | s[i >> 2];
			}
		}else{
			unsigned char *t = m17_t1 + M17_XLAT_OFS + 16 * p;
			for(unsigned char c = 0; c < M17_PAIRS4; c++){
				unsigned char d = pgm_read_byte(&m17_dec4[c]);
				t[c] = pgm_read_byte(&m17_enc15[(s[d >> 4] << 4) | s[d & 15]]);
			}
		}
	}

	/* The screen's colours, from the palettes in T1: each distinct colour takes a slot, the
	** backdrop the first, in the palettes' order; T2 is the slots' colours, and the two
	** translations say which slots a flash byte and a sprite's nibble are. A 16th colour and
	** those after it take the nearest slot and are counted */
	static void m17_build_screen(void){
		unsigned char slots[M17_SLOTS];
		unsigned char slot_of[4];
		unsigned char n = 1, over = 0;

		slots[0] = m17_t1[0];
		slot_of[0] = 0;
		for(unsigned char p = 0; p < M17_PALETTES; p++){
			for(unsigned char v = 1; v < 4; v++){
				unsigned char c = m17_t1[16 * p + M17_T1_VALUE(v)];
				unsigned char k = 0;
				while(k < n && slots[k] != c) k++;
				if(k == n){
					if(n < M17_SLOTS){
						slots[n++] = c;
					}else{
						k = m17_nearest(slots, c);
						over++;
					}
				}
				slot_of[v] = k;
			}
			/* (a palette's translation needs its slots' numbers alone) */
			m17_translate(p, slot_of);
		}
		m17_colors_over = over;
		while(n < M17_SLOTS) slots[n++] = slots[0];

		for(unsigned char k = 0; k < M17_PAIRS15; k++){
			m17_t2[k] = slots[pgm_read_byte(&m17_dec15[k]) >> 4];
		}
		m17_t2[M17_PAIRS15] = slots[0];
	}

	/* The screen's colours made again if a palette asked for it: when the sprites are composed,
	** and at no other time. The slots' numbers change with it, so the RAM tiles made before
	** must not be shown after (m17_sprites.s calls this once the frame is given none) */
	void m17_update_screen(void){
		if(m17_dirty){
			m17_dirty = 0;
			m17_build_screen();
		}
	}

	/* One colour: the least that keeps every value on a slot of its colour. No memory is kept
	** for it: a value's slot is read back from its palette's translation (a background
	** palette's from the RAM byte of its pair backdrop then value, a sprite palette's from the
	** value's nibble), a slot's colour from T2 */
	void m17_SetPaletteColor(unsigned char p, unsigned char v, unsigned char color){
		unsigned char s[4], uses[M17_SLOTS], t;
		const unsigned char *x = m17_t1 + M17_XLAT_OFS;

		if(p >= M17_PALETTES || (unsigned char)(v - 1) > 2) return;
		unsigned char *pal = m17_t1 + 16 * p;
		if(pal[M17_T1_VALUE(v)] == color) return;
		m17_set_value(pal, v, color);
		if(m17_dirty) return;                   /* the whole table is to be made again anyway */
		if(m17_colors_over){                    /* a value without its colour may find a slot now */
			m17_dirty = 1;
			return;
		}
		/* how many values each slot has, and this palette's own 3. (The sprites' table is one
		** byte short of the flash bytes' 4 palettes: its entry w is x[w - 1] from there on) */
		memset(uses, 0, sizeof(uses));
		s[0] = 0;
		for(unsigned char q = 0; q < M17_PALETTES; q++, x += 16){
			for(unsigned char w = 1; w < 4; w++){
				unsigned char k;
				if(q < M17_SPRITE_PAL) k = pgm_read_byte(&m17_dec15[x[M17_PAIR0(w)]]) & 15;
				else k = x[w - 1] >> 4;
				uses[k]++;
				if(q == p) s[w] = k;
			}
		}

		/* the colour is on the screen: the backdrop's slot, or one a value uses */
		for(t = 0; t < M17_SLOTS; t++){
			if((!t || uses[t]) && m17_slot_color(t) == color) break;
		}
		if(t == M17_SLOTS){
			if(s[v] && uses[s[v]] == 1){
				/* its own slot, shared with no other value: the RAM tiles follow as they are */
				m17_set_slot(s[v], color);
				return;
			}
			for(t = 1; t < M17_SLOTS && uses[t]; t++);
			if(t == M17_SLOTS){                 /* a 16th colour: the whole table, which counts it */
				m17_dirty = 1;
				return;
			}
			m17_set_slot(t, color);
		}
		s[v] = t;
		m17_translate(p, s);
	}

	void m17_SetSpriteTiles(const char *tiles, const char *boxes){
		m17_spr_tiles = tiles;
		m17_spr_boxes = boxes;
	}

	/* (an id that is no flash tile gives a RAM tile's index, which no cell to convert holds) */
	void m17_SetBlankTile(unsigned char tileId){
		m17_blank = tileId + M17_RAM_INDEXES;
	}

	/* The kernel's names. The run is not .bss, so the C start-up left it as it was: it is
	** cleared here. All 0 is a black screen: every cell RAM tile 0, its bytes and the tables 0.
	** No sprite is shown */
	void InitializeVideoMode(){
		extern unsigned char m17_run[];
		memset(m17_run, 0, M17_RUN_SIZE);
		GPIOR0 = 0;                             /* no leave, no run of rows under way (a start that
		                                        ** is no reset leaves the I/O space as it was) */
		for(unsigned char i = 0; i < M17_SPRITES; i++){
			m17_sprites[i].y = 0xF0;
		}
	}

	void VideoModeVsync(){
		m17_skipping = 0;                       /* a held picture's count of lines has run out by
		                                        ** now: the coming picture is drawn, unless held */
	}

	void DisplayLogo(){
	}

	/* This mode has no font, only tiles */
	void SetFont(char x, char y, unsigned char tileId){
		SetTile(x, y, tileId);
	}

	void SetFontTilesIndex(unsigned char index){
		(void)index;
	}
