/*
 * gfx17.c -- the platform layer on video mode 17 (customVideoMode17): what gfx17.c is on
 * mode 16, reached by the engine through gfx.h alone.
 *
 * The picture. One row of status bar (the mode's HUD row), then 24 rows of playfield: the
 * level's tile rows 0 to 23 (block rows 0 to 11 of the engine's 13), each always in the same
 * VRAM row. x is the NES's, 1:1: the view is 224 of its screen's 256 pixels and follows the
 * player inside it. VRAM is a ring: level tile column c is VRAM column c & 31, and 32 columns
 * are valid from ring_lo; the commit draws the columns that come in. A cell holds 64 + a flash
 * tile - 8 x its section's bank (gfxdata17.h: two windows, the level's and the one with the
 * font). The sprites are the engine's OAM as it is: it is the mode's sprite list, shown
 * through the mode's origin, the view's offset and the NES's line 24.
 *
 * The frame. The engine runs in the main loop, and a picture can start while it does. What it
 * changes on a screen that is shown is kept, and drawn by gfx_Commit() in the blank, with the
 * scroll and before the sprites are composed: the cells of the blocks it changed (a queue of
 * their places), the texts it pinned to the level, the bar, the colour cycle's step. So a
 * picture is one frame's cells, scroll and sprites. What is written at once is what no picture
 * shows half done: the text screens (the screen is off), the static title's lines.
 *
 * The view of the block store (bb.c) is the NES's name table here: the parser's columns, and
 * what these routines write where the 6502 wrote the name table (bb_PutViewCell). The engine's
 * other writes to the block buffer are not drawn, as they were not on the NES.
 */
#include <string.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include "smb.h"
#if NESTITLE
/* (the generator's second set: the title as the cartridge has it. Its board's cells are
** read once each when the title is drawn: they are on the card with the levels when those
** are, 264 bytes the flash does not hold) */
#if SD
	#define BOARD17_AT SDDATA
	#define BOARD17(i) sd_byte(&Board17[i])
#else
	#define BOARD17_AT PROGMEM
	#define BOARD17(i) pgm_read_byte(&Board17[i])
#endif
#include "gfxdata17n.h"
#else
#include "gfxdata17.h"
#endif
/* the window the title is drawn through: the text window, or with NESTITLE its own */
#ifndef GFX17_BANK_TITLE
	#define GFX17_BANK_TITLE GFX17_BANK_TEXT
#endif


/* tiles17.s: the cartridge's tiles */
extern const char gfx17_bg_tiles[] PROGMEM;
extern const char gfx17_spr_tiles[] PROGMEM;
extern const char gfx17_spr_boxes[] PROGMEM;

#define PF_ROWS     M17_PF_ROWS
#define VIEW_COLS   M17_COLS
#define VIEW_W      (8 * VIEW_COLS)        /* 224 of the NES screen's 256 pixels */
#define VIEW_SLACK  (256 - VIEW_W)         /* how far the view's left edge goes from the screen's */
#define VIEW_FOLLOW 96                     /* the player's x in the view when it starts to follow him */
/* While the title is up (ATTRACT) the view's left edge is not left of this pixel of the title's
** screen: 16 is the screen's middle 28 columns, the board 3 columns from either edge and "1 PLAYER
** GAME" 9 and 6 for the cartridge's 11 and 8. **0 gives the screen's left 28 columns back** (the
** board then 5 columns from the left edge and 1 from the right). It is a place in the level and
** not an offset kept: once the demo has scrolled the screen's own edge past it (16 pixels) the
** view is a game's again, and a player who comes back to the screen's left edge is in the
** picture as he is in a game (the title is up for the whole demo, not only while it is in
** sight). Before that he is right of it: he starts at x 40 and the demo walks him right, and
** the title's first frame, which has him at x 0, has no sprite of his. A choice made in the
** user's absence, 2026-10-08 */
#define TITLE_VIEW_MIN 16
_Static_assert(M17_HUD == 1 && PF_ROWS == 24 && VIEW_COLS == 28,
               "gfx17.c: a status bar and 24 rows of playfield (M17_ROWS=25), 28 columns");

/* The engine's OAM is the mode's sprite list: the same page, at the address the engine's
** files were compiled with (the Makefile's M17_OAM_AT: the run's place and the list's in it) */
_Static_assert(M17_OAM_AT == M17_RUN_AT + M17_RUN_SPRITES, "gfx17.c: the OAM is not the mode's sprite list");
_Static_assert(M17_SPRITES == 64, "gfx17.c: the OAM's 64 entries");

/* The NES's line of the playfield's first, and the picture's of it: under the bar's row. (A
** sprite is drawn at its OAM y, the line above the NES's: standing on a block it is on the
** cells' own rows, and takes half the RAM tiles) */
#define NES_PF_Y 32
#define SPRITE_Y (NES_PF_Y - 8 * M17_HUD)

/* A cell of VRAM: column 0-31 (a level tile column's low five bits: the others fall out), row
** 0-23 of the playfield. Its address in two bytes: 8 x the column and the row's low three bits
** are the low one, with no carry into the row's page */
__attribute__((noinline)) static u8 *cell(u8 col, u8 row){
	return vram + (((u16)(u8)(row >> 3) << 8) | (u8)((u8)(col << 3) | (row & 7)));
}

/* a text cell's byte of flash tile t: the status bar's row and the text screens */
#define TEXT_OFS ((u8)(64 - 8 * GFX17_BANK_TEXT))

bool gfx_title;
static bool vram_pending;          /* VRAM_Buffer1_Pending */
static u8 pf_ofs;                  /* a playfield cell's byte is a flash tile + this */

/* The view */
static bool view_ready;            /* a level is shown: the view follows, the queue is drawn */
/* ... and main17.c may give the mode its leave (the picture in bands: the engine's frame goes
** on while a picture is drawn), for nothing the engine does to a level's screen is written
** before the commit: view_ready under a name main17.c reads */
extern bool gfx_level __attribute__((alias("view_ready")));
static bool view_all;              /* its 32 columns are to be drawn again */
static u16 ring_lo;                /* VRAM holds level tile columns ring_lo to ring_lo + 31 */

/* ---------------------------------------------------------------------------------------
** The windows
** ------------------------------------------------------------------------------------ */
__attribute__((noinline)) static void set_bank(u8 bank){
	u8 ofs = (u8)(8 - bank) << 3;      /* 64 - 8 x bank */
	Screen.tileBank = bank;
	pf_ofs = ofs;
	m17_SetBlankTile(ofs + (u8)(GFX17_BLANK - 64));
}

/* The flash tile of NES font tile chr (palette 2) */
__attribute__((noinline)) static u8 font_tile(u8 chr){
	if(chr < 0x30) return pgm_read_byte(&FontTiles17[chr]);
	if(chr == 0xce) return GFX17_FONT_CE;          /* the title's cursor */
	if(chr == 0xaf) return GFX17_FONT_AF;          /* a period */
	if(chr == 0x9f) return GFX17_FONT_9F;          /* the crown */
	return GFX17_BLANK;
}

/* The metatiles' flash tiles by palette (the metatile's top 2 bits) */
static const u8 *const MTilesTab[4] PROGMEM = { MTiles17_0[0], MTiles17_1[0], MTiles17_2[0], MTiles17_3[0] };

/* ---------------------------------------------------------------------------------------
** Text over the level: pinned to it where the 6502 wrote its name table (the warp zone's
** welcome, the castles' messages), NES tiles in palette 2 from (tc, tr); with ATTRACT, the
** title over 1-1: the cartridge's board (gfxdata17.h's Board17, from tools/gen_gfx17.py) and
** text.c's lines under it. A cell reads them, so a column that comes back keeps its text. The
** areas that show any are drawn through the text window (GFX17_TEXT_AREA)
** ------------------------------------------------------------------------------------ */
#define OVERLAYS 4
static struct Overlay { u16 tc; u8 tr, len; const u8 *s; } overlay[OVERLAYS];
static u8 overlay_new;             /* bit i: entry i is not drawn yet */

/* The title is in the 6502's name table 0, a cell of it at its own column there: level tile
** columns 0 to 31. It is always on the level's first page: the title's mode starts with
** InitializeGame, which clears HalfwayPage, and InitializeArea then starts the screen on that
** page (player.c); the demo scrolls on from there and nothing brings it back */

/* Whether any cell has a text: text_cell() is 0 for every cell when not */
#define TEXTS() (gfx_title || overlay[0].len)

/* The cell's byte of the text at level tile (tc, tr), 0 when there is none (no cell is 0: the
** first 64 are the mode's RAM tiles) */
static u8 text_cell(u16 tc, u8 tr){
	if(gfx_title && tc < 32){
		u8 c = (u8)tc - GFX17_BOARD_COL;
		u8 r = tr - GFX17_BOARD_ROW;
		/* (the title is drawn through the text window alone: view_redraw) */
#if NESTITLE
		/* (... through its own, and the table holds a cell's byte as it is there: the board's,
		** the line's under it; 0 where the cartridge wrote nothing and the level shows) */
		if(r < GFX17_BOARD_ROWS && c < GFX17_BOARD_COLS){
			u8 b = BOARD17((u16)(c * GFX17_BOARD_ROWS) + r);
			if(b) return b;
		}
#else
		if(r < GFX17_BOARD_ROWS && c < GFX17_BOARD_COLS)
			return pgm_read_byte(&Board17[(u8)(c * GFX17_BOARD_ROWS) + r]) + TEXT_OFS;
#endif
		u8 t = text_TitleCell((u8)tc, tr);
		if(t != 0xff) return font_tile(t) + pf_ofs;
	}
	const struct Overlay *o = overlay;
	if(!o->len) return 0;              /* they fill from the first: none there, none at all */
	for(u8 i = OVERLAYS; i; i--, o++){
		/* tc - o.tc < o.len (unsigned) is tc in [o.tc, o.tc + len); a free entry's len is 0 */
		u16 d = tc - o->tc;
		if(o->tr == tr && d < o->len) return font_tile(pgm_read_byte(o->s + d)) + pf_ofs;
	}
	return 0;
}

void gfx_LevelText(u16 tc, s8 tr, const u8 *s, u8 len){
	struct Overlay *o = overlay;       /* the first free entry, or the last */
	u8 bit = 1;
	while(bit != 1 << (OVERLAYS - 1) && o->len){
		o++;
		bit <<= 1;
		__asm__("" : "+r"(bit));       /* (a loop: unrolled it is three times the code) */
	}
	o->tc = tc;
	o->tr = (u8)tr;
	o->s = s;
	o->len = len;
	overlay_new |= bit;
}

void gfx_ClearOverlays(void){
	memset(overlay, 0, sizeof overlay);
	overlay_new = 0;
}

/* ---------------------------------------------------------------------------------------
** Cells
** ------------------------------------------------------------------------------------ */
/* A rope that ends in the middle of a block: the rope in its top row of cells, the balance
** rope's blank in its bottom one. The view's alone, and no metatile of the tables */
#define ROPE_HALF 0x7f

/* The cell's byte of metatile mt at level tile (tc, tr): its quarter by their low bits */
static u8 mt_cell(u8 mt, u8 tc, u8 tr){
	if(mt == ROPE_HALF) mt = (tr & 1) ? 0x44 : 0x40;
	/* a water area's blank is its water: the 6502 drew a taken coin there as its blank
	** metatile, tile $26 */
	if(!mt && !AreaType) return GFX17_WATER + pf_ofs;
	const u8 *mtab = (const u8 *)pgm_read_ptr(&MTilesTab[mt >> 6]);
	return pgm_read_byte(mtab + (u8)((mt << 2) | ((tc & 1) << 1) | (tr & 1))) + pf_ofs;
}

/* VRAM's column of level tile column tc: 12 metatiles of the store, 24 cells */
static void draw_column(u16 tc){
	u8 mts[13];
	bb_GetViewColumn((u8)(tc >> 1), mts);
	u8 *p = vram + (u8)((u8)tc << 3);                 /* the cells are a page's 8 rows, then the next's */
	u8 texts = TEXTS();
	for(u8 tr = 0; tr < PF_ROWS; tr++){
		u8 c = 0;
		if(texts) c = text_cell(tc, tr);
		if(!c) c = mt_cell(mts[tr >> 1], (u8)tc, tr);
		*p++ = c;
		if(!((u8)(tr + 1) & 7)) p += 256 - 8;
	}
}

#if TWO_PLAYERS
/* DrawMushroomIcon, where the game may be of two: the cursor is beside the line chosen and
** nothing beside the other (text.c's text_TitleCell), so its column of the title is drawn
** again. (Select moves it while the title stands still, the screen on its first page) */
void gfx_TitleCursor(void){
	draw_column(TITLE_COL_CURSOR);
}
#endif

/* The 4 cells of the store's block (bbcol, row), where the ring holds them */
__attribute__((noinline)) static void draw_block(u8 bbcol, u8 row){
	u16 bc = ring_lo >> 1;                            /* the block column of the ring's first */
	u8 d = (u8)(bbcol - (u8)bc) & (BB_COLS - 1);
	if(d > 16) return;                                /* not in the ring */
	u16 tc = (bc + d) << 1;
	u8 mt = bb_GetView(bbcol, row);
	u8 tr = row << 1;
	for(u8 k = 4; k; k--){                            /* a column's two cells, then the next's */
		if((u16)(tc - ring_lo) < 32){                 /* (the ring starts or ends inside the block) */
			u8 c = text_cell(tc, tr);
			if(!c) c = mt_cell(mt, (u8)tc, tr);
			*cell((u8)tc, tr) = c;
		}
		tr ^= 1;
		if(!(tr & 1)) tc++;
	}
}

/* ---------------------------------------------------------------------------------------
** The 6502's VRAM buffer writers. It queued a block's tiles in VRAM_Buffer1 and uploaded them
** at the next NMI (BlockObjMT_Updater writes no block back on a frame the buffer is in use:
** vram_pending). Here the view's metatile is written, and the block's place is queued for the
** commit. The queue full: the commit draws the whole view
** ------------------------------------------------------------------------------------ */
#define QUEUE 6
static u8 queue[QUEUE][2];         /* the store's column, its row */
static u8 queued;

static void view_put(u8 bbcol, u8 row, u8 mt){
	vram_pending = true;
	if(row >= 13) return;          /* past the store: the 6502's write went nowhere it shows */
	bb_PutViewCell(bbcol, row, mt);
	if(row >= PF_ROWS / 2) return; /* the store's last row is under the picture */
	if(queued == QUEUE){
		view_all = true;
		return;
	}
	queue[queued][0] = bbcol;
	queue[queued][1] = row;
	queued++;
}

bool VRAM_Buffer1_Pending(void){
	return vram_pending;
}

void gfx_FrameStart(void){
	vram_pending = false;
}

/* PrintStatusBarNumbers: the 6502 reprinted parts of the bar; the commit draws all of it */
void PrintStatusBarNumbers(void){
	vram_pending = true;
}

void RemoveCoin_Axe(void){
	view_put(M06, M02 >> 4, 0);
}

/* PlayerHeadCollision calls it before it puts the bouncing block's stand-in in the block
** buffer: the 6502 wrote the blank metatile to the name table */
void DestroyBlockMetatile(void){
	view_put(M06, M02 >> 4, 0);
}

/* (the 6502's decrement of Block_RepFlag is not here: its one caller clears the flag next) */
void ReplaceBlockMetatile(void){
	u8 x = X_;
	view_put(Block_BBuf_Low[x], Block_Orig_YPos[x] >> 4, Block_Metatile[x]);
}

/* The level tile column of NES name table column col on the screen's page of parity nt */
u16 gfx_NameTableColumn(u8 nt, u8 col){
	u8 page = ScreenLeft_PageLoc;
	if((page & 1) != nt) page++;
	return ((u16)page << 5) + col;
}

/* Metatile mt at name table address hi:lo, in the view: where the address is in the store's
** 13 rows (name table rows 4 to 29). The 6502 wrote the name table alone here, and the block buffer takes a blank only
** (the bridge's): a rope put in it was a block to land on and to walk into, which no one
** saw (the user's run 24, 3-3: Mario standing on the balance lifts' rope) */
__attribute__((noinline)) static void nt_put(u8 hi, u8 lo, u8 mt){
	u8 row = (u8)((hi & 3) << 3) | (lo >> 5);
	if(row < 4 || row >= 4 + 2 * 13) return;
	row = (u8)(row - 4) >> 1;
	u8 bbcol = (u8)(gfx_NameTableColumn((hi >> 2) & 1, lo & 31) >> 1) & (BB_COLS - 1);
	if(!mt) bb_Put(bbcol, row, mt);
	view_put(bbcol, row, mt);
}

/* RemBridge: one of Bowser's bridge metatiles gone. The 6502 blanked it in the name table only
** (M05:M04); here the block buffer's copy goes too, which nothing reads by then. (Its one
** caller's addresses are $2200 + BridgeCollapseData: name table rows 16, 18 and 20) */
void RemBridge(void){
	nt_put(M05, M04, 0);
}

/* DrawEraseRopeHook: a balance platform's two ropes, an 8-line row each grown ($a2 $a3) or cut
** ($24 $24) at the name table addresses in RopeVRAMData. The view holds 16-line blocks: a
** block's top row grown, or its bottom row cut, makes it ROPE_HALF; its bottom row grown the
** vertical rope ($40), its top row cut the balance rope's blank ($44). (Until 2026-10-09 a
** rope's end moved a block at a time, and stuck out under its lift by up to 8 lines) */
void DrawEraseRopeHook(void){
	if(vram_pending) return;                  /* the 6502 skipped a busy buffer the same way */
	const u8 *r = RopeVRAMData;
	for(u8 i = 2; i; i--, r += 5){
		u8 mt = ROPE_HALF;                     /* (name table row 4 is a block's top row) */
		if(r[3] == 0xa2){ if(r[1] & 0x20) mt = 0x40; }
		else if(!(r[1] & 0x20)) mt = 0x44;
		nt_put(r[0], r[1], mt);
	}
	vram_pending = true;
}

/* ---------------------------------------------------------------------------------------
** Palettes: the NES's 8 four-colour palettes are the mode's (0-3 background, 4-7 sprites).
** The mode makes its colour table from them when the sprites are next composed, so a load
** shows with the frame that asked for it
** ------------------------------------------------------------------------------------ */
/* A colour of the game's tables as the mode takes it: on the AVR the tables hold the Uzebox's
** colours (tools/gen_gfx17.py converts them: no table of 64 in the ROM, no look-up); on the PC
** they hold the 6502's bytes, converted here, and the reference converts on its side */
	#define nes_color(c) (c)

__attribute__((noinline)) static void set_backdrop_P(const u8 *p){
	m17_SetBackdrop(nes_color(pgm_read_byte(p)));
}

/* NES palette p (0-7), colours 1-3, from flash */
static void set_palette_P(u8 p, const u8 *nes){
	m17_SetPalette(p, nes_color(pgm_read_byte(nes + 1)), nes_color(pgm_read_byte(nes + 2)), nes_color(pgm_read_byte(nes + 3)));
}

/* The 8 palettes of area type t (water, ground, underground, castle) and the backdrop */
void gfx_LoadPalette(u8 t){
	const u8 *pal = AreaPalettes[t];
	set_backdrop_P(pal);
	for(u8 p = 0; p < 8; p++) set_palette_P(p, pal + 4 * p);
}

void gfx_LoadAreaPalette(void){
	gfx_LoadPalette(AreaType);
}

/* VRAM_Buffer_AddrCtrl 9-11: day snow, night snow, mushroom: the backdrop and palette 0 */
void gfx_LoadAltPalette(u8 c){
	const u8 *p = (c == 9) ? DaySnowPaletteData : (c == 10) ? NightSnowPaletteData : MushroomPaletteData;
	set_backdrop_P(p);
	set_palette_P(0, p);
}

/* GetPlayerColors: the player's palette (sprite palette 0) for its status, and the backdrop */
void GetPlayerColors(void){
	u8 y = (PlayerStatus == 2) ? 8 : 0;
#if TWO_PLAYERS
	if(!y && CurrentPlayer) y = 4;                 /* Luigi's colours; the fiery ones are both players' */
#endif
	set_palette_P(4, &PlayerColors[y]);
	u8 b = BackgroundColorCtrl ? BackgroundColorCtrl : AreaType;
	set_backdrop_P(&BackgroundColors[b]);
}

/* ColorRotation: palette 3's first colour cycles every eighth frame (the "?" blocks, coins).
** One colour written alone, which the mode does without making its colour table again, and at
** once: so the commit writes it, with the frame's cells */
static u8 cycle;                   /* the step to write, + 1 (0: none) */
void ColorRotation(void){
	if(FrameCounter & 7) return;
	cycle = ColorRotateOffset + 1;
	if(++ColorRotateOffset >= 6) ColorRotateOffset = 0;
}

void BowserPaletteRequest(void){
	set_palette_P(5, BowserPaletteData);   /* the 6502's PPU $3f14: sprite palette 1 */
}

/* ---------------------------------------------------------------------------------------
** The status bar: the mode's HUD row, through the text window: "00000 O00 1-1 000" (score,
** coins, world, time). The score's 5 places are the 6502's last four digits and the 0 its
** status bar always printed after them
** ------------------------------------------------------------------------------------ */
#define SB_SCORE 1
#define SB_COIN  10
#define SB_WORLD 16
#define SB_TIME  23

static void sb_put(u8 c, u8 tile){
	m17_hud[c] = tile + TEXT_OFS;
}

static void sb_digits(u8 c, const u8 *d, u8 n){
	for(u8 i = 0; i < n; i++) sb_put(c + i, font_tile(d[i]));
}


void gfx_DrawStatusBar(void){
	sb_digits(SB_SCORE, PlayerScoreDisplay + 2, 4);
	sb_put(SB_SCORE + 4, font_tile(0x00));
	sb_put(SB_COIN, GFX17_COIN);           /* the coin, in the palette that cycles */
	sb_digits(SB_COIN + 1, CoinDisplay, 2);
	sb_put(SB_WORLD, font_tile(gfx_bar_world + 1));
	sb_put(SB_WORLD + 1, font_tile(0x28));
	sb_put(SB_WORLD + 2, font_tile(gfx_bar_level + 1));
	sb_digits(SB_TIME, GameTimerDisplay, 3);
}

/* ---------------------------------------------------------------------------------------
** Screens drawn straight into VRAM (the level is not shown): text, through the text window,
** the view at the screen's left
** ------------------------------------------------------------------------------------ */
void gfx_ClearScreen(void){
	set_bank(GFX17_BANK_TEXT);
	memset(vram, GFX17_BLANK + TEXT_OFS, 256 * (PF_ROWS / 8));
	gfx_ClearOverlays();
	view_ready = false;
	view_all = false;
	gfx_title = false;                 /* the title is in the name table, and goes with it */
}

/* NES tiles s[0..len) at playfield cell (col, row), in palette 2 */
void gfx_PutText(u8 col, u8 row, const u8 *s, u8 len){
	u8 *p = cell(col, row);            /* the next column's cell is 8 on */
	for(; len && col < 32; len--, col++, p += 8) *p = font_tile(*s++) + pf_ofs;
}

/* The title. ATTRACT: the board and text.c's lines pinned to the level at their name table
** columns, as the NES's were: the demo scrolls them away; 1-1 is drawn again through
** the text window, which has its tiles too. Else a screen of its own: text.c's lines on
** black, static. It is up until the screen is next cleared, as it was in the NES's name
** table: through the frame between Start and the screen going off for the new area */
void gfx_ShowTitle(void){
	view_all = true;
	gfx_title = true;
}

/* WriteGameText: the status bar line (0) is the commit's; the world/lives screen, time up and
** game over (1-3) and the warp zone's welcome (4-6) are text.c's */
void WriteGameText(void){
	gfx_Text(A_);
}

/* ---------------------------------------------------------------------------------------
** The view
** ------------------------------------------------------------------------------------ */
/* The NES's screen was off while an area was loaded and set up (DisableScreenFlag): no picture
** then. The title's too: it shows from its set-up's end, as the 6502's did, and goes off with
** the flag when its demo ends. Called once a frame, when the engine's frame ends, and true a
** frame longer than the flag: the picture that follows shows what was committed before that
** frame of the engine, so it is drawn when the flag was clear before it, as the NES drew it,
** and still is after it (the last picture before a screen-off is the one not drawn). Were a
** picture to start before the engine's frame ends, it would find the last frame's answer:
** the first picture after a screen-on is then the one not drawn */
bool gfx_ScreenOff(void){
	static bool was;
	bool off = DisableScreenFlag, r = off || was;
	was = off;
	return r;
}

/* The whole lines of blank left before the next picture's interrupt, from the kernel's sync
** counters: sync_pulse counts a field's lines down from SYNC_HSYNC_PULSES, the picture starts
** at the line that brings it to FIRST - 1 (FIRST lines not drawn), and comes back from the
** picture with the lines after it; at 0 the vertical sync's 18 half lines are counted, in the
** other phase. Never more than is left; a line less at most */
extern volatile u8 sync_phase, sync_pulse;
#define PICT_AT (SYNC_HSYNC_PULSES - FIRST_RENDER_LINE - 1)
u8 gfx_LinesLeft(void){
	u8 ph, p;
	do{
		ph = sync_phase;
		p = sync_pulse;
	}while(ph != sync_phase);
	if(!(ph & 1)) return (p >> 1) + FIRST_RENDER_LINE;
	if(p > PICT_AT) return p - PICT_AT - 1;
	return p + (SYNC_PRE_EQ_PULSES + SYNC_EQ_PULSES + SYNC_POST_EQ_PULSES) / 2 + FIRST_RENDER_LINE;
}

/* An area is parsed: its level is what is shown from the next commit on */
void gfx_ResetView(void){
	view_ready = true;
	view_all = true;
}

/* The ring follows the view: the columns that came into sight are drawn, on either side. A
** jump that leaves every cell right moves the ring and draws nothing: a whole number of turns
** of the store's ring, 64 tile columns (the castles' loops go back 4 pages, the store's
** columns the same ones). Any other jump, and an area's first view: the 32 columns, through
** the area's window */
__attribute__((noinline)) static void view_redraw(u16 lo){
	u8 bank = GFX17_TEXT_AREA(AreaPointer) ? GFX17_BANK_TEXT : GFX17_BANK_LEVEL;
	if(gfx_title) bank = GFX17_BANK_TITLE;
	set_bank(bank);
	ring_lo = lo;
	for(u8 c = 32; c; c--) draw_column(lo++);
}

/* The texts not drawn yet (overlay_new), where the ring holds their cells */
__attribute__((noinline)) static void draw_texts(void){
	const struct Overlay *o = overlay;
	for(u8 m = overlay_new; m; m >>= 1, o++){
		if(!(m & 1)) continue;
		u16 tc = o->tc;
		u8 tr = o->tr;
		for(u8 k = o->len; k; k--, tc++){
			if((u16)(tc - ring_lo) < 32 && tr < PF_ROWS) *cell((u8)tc, tr) = text_cell(tc, tr);
		}
	}
}

/* The view where the player took it, with what changed in it: the scroll (the low byte) and
** the sprites' origin (the high one).
** The view's left edge in level pixels, and its offset from the NES screen's (0 to
** VIEW_SLACK): the screen's left edge until the player is VIEW_FOLLOW pixels into the view,
** then it follows him to the screen's right edge. Scrolling keeps him left of that; a screen
** that does not scroll (a coin room, a boss, a level's end) does not. (The title's view starts
** further in, at the level's pixel TITLE_VIEW_MIN, until the screen has scrolled that far: the
** title is at the level's column 0. A frame of a level pays the compare with the screen's
** place, and reads gfx_title only in a level's first 16 pixels) */
__attribute__((noinline)) static u16 view_update(void){
	u16 cx = ((u16)ScreenLeft_PageLoc << 8) | ScreenLeft_X_Pos;
	s16 o = (s16)((((u16)Player_PageLoc << 8) | Player_X_Position) - cx) - VIEW_FOLLOW;
	u8 least = 0;
	if(cx < TITLE_VIEW_MIN && gfx_title) least = TITLE_VIEW_MIN - (u8)cx;
	if(o < (s16)least) o = least;
	if(o > VIEW_SLACK) o = VIEW_SLACK;
	u16 left = cx + (u8)o;
	u16 xo = ((u16)(u8)o << 8) | (u8)left;
	u16 lo = left >> 3, hi = (left + (VIEW_W - 1)) >> 3;
	u16 ring = ring_lo;                        /* (no column's drawing reads ring_lo) */
	if(!view_all){
		/* the first column in sight, in the ring (0 to 4 as the view scrolls), less its whole
		** turns: -32 to 31. From -4 to 8 the ring is that far behind it, and has its cells */
		s8 r = (s8)(((u8)(lo - ring) & 63) ^ 32) - 32;
		if(r >= -4 && r <= 8) ring = lo - r;
		else view_all = true;
	}
	if(view_all){
		view_all = false;
		view_redraw(lo);                       /* what was waiting is in these cells */
		return xo;
	}
	while((s16)(lo - ring) < 0) draw_column(--ring);
	while((s16)(hi - ring) > 31) draw_column(++ring + 31);
	ring_lo = ring;
	const u8 *q = queue[0];
	for(u8 i = queued; i; i--, q += 2) draw_block(q[0], q[1]);
	if(overlay_new) draw_texts();
	return xo;
}

/* ---------------------------------------------------------------------------------------
** The lifts: a row of one is composed once.
** A lift is six sprites in a row (sprites.c's DrawLargePlatform), all the same tile and y, 8
** pixels apart. On the cells' grid (enemies.c's LIFT_X puts the lifts that go up and down
** there) and over blank cells, each segment is the same picture in its own column of cells.
** So one is composed, the source, and the others' cells, the copies, show its RAM tiles: 2
** RAM tiles a lift where it took 12 or 14 (1 when its y is on the grid too), and 1 sprite of
** the composition's 24 where it took 6.
**   gfx_LiftsOff(), before the composition: the rows are found and their copies passed as off
**     (y $f8, the engine's own).
**   gfx_LiftsOn(), after it: the copies' y is given back (the engine's OAM is the engine's),
**     and their cells get the RAM tiles the composition gave the source's two cells (the
**     restore list names them). A cell it gave none is not written: what the composition did
**     not draw, the copies do not show, and a row of cells under a lift whose y is on the
**     grid is left to whatever else is there (another lift's copies).
**   gfx_Commit(), before VRAM is next written or composed: the copies' cells are the blank
**     again (the mode puts back the cells of its own RAM tiles; it does not know these). A
**     cell holds a RAM tile's number where a frame is made only as a copy: one that holds a
**     flash tile was written since (a text screen) and is left.
** The rows are those of the engine's slots that draw through DrawLargePlatform ($24 to $2a):
** six entries from the slot's offset in the OAM, each with the slot's y or off ($f8), 8 pixels
** apart, one tile, one palette. (The small lifts' rows of three are not looked at: the game's
** two ride a rope, and are never over blank cells.) A segment is the source or a copy only if
** all of this holds; any other is composed as ever:
**   - the row's y is in the playfield's rows (32 to 215: two rows of cells in VRAM, always)
**     and its first x is on the cells' grid;
**   - the segment is on (it has the row's y);
**   - it is in the picture by the mode's own rule: its own x less the sprites' origin, as a
**     whole number, is -7 to 223 (an x of 249 at an origin of 0 is right of the picture, not
**     left of it: lift_row's count holds while the origin is 32 at most, VIEW_SLACK);
**   - its two cells hold the blank;
**   - no other entry of the OAM is within the 8 by 8 of a cell of its two (the entry's
**     column of cells and the one left of it, which is one more than it touches at most);
**   - the mode's pass does not come to it after the other sprites: when the pass starts
**     inside the row (m17_rotate: the entry that went without a tile or its turn), the
**     segments before that entry are the last of their class, and are composed as ever.
** The first such segment is the source, the others the copies; with no other the row is left
** alone. So what a composition short of tiles would give any of them, it gives the source.
** An entry's place in the OAM is counted in a byte, as the 6502 does: nothing here reads or
** writes outside the OAM's page and VRAM's, whatever the engine's slot holds.
** The mode is as it was: a RAM tile's number in several cells is drawn in each, and a row of
** cells with a copy is not a row of blank to the bands.
**
** It is assembly (the C of the first cut was 740 bytes).
** A record is 4 bytes a row: the source's upper cell in VRAM (its address's two bytes; on the
** PC its place in the array), the OAM's offset of the source, and the copies (bit k: the
** segment k + 1 after the source). Five of them (a row a slot, and the sixth slot takes no
** lift: enemies.c's CheckEndofBuffer), and lift_used, their bytes in use.
** ------------------------------------------------------------------------------------ */
#define LIFT_STR_(x) #x
#define LIFT_STR(x) LIFT_STR_(x)
extern u8 m17_rotate;              /* the mode's: the entry its next pass starts from */

_Static_assert(!((M17_RUN_AT + M17_RUN_VRAM) & 0x3ff) && !(M17_OAM_AT & 0xff),
               "gfx17.c: a cell's page is bits 8 and 9 of its address, the OAM is a page");
_Static_assert(__builtin_offsetof(ScreenType, scrollX) == 0 && __builtin_offsetof(ScreenType, spriteX) == 3 &&
               sizeof(struct m17_RestoreStruct) == 3 && M17_RAM_INDEXES == 64 && VIEW_W == 224 &&
               NES_PF_Y == 32 && PF_ROWS == 24 && VIEW_SLACK <= 32, "gfx17.c: the lifts' assembly counts on these");
#define LIFT_OAM   LIFT_STR(((M17_OAM_AT) >> 8))                    /* the OAM's page */
#define LIFT_VRAM  LIFT_STR(((M17_RUN_AT + M17_RUN_VRAM) >> 10))    /* VRAM's first, a quarter of it */
#define LIFT_BLANK LIFT_STR(GFX17_BLANK)
/* Registers (none is saved: r18 to r27, r30, r31, r0 and T are the caller's to lose):
**   r21 the row's entry in the OAM, then the source's; r22 its y; r24:r25 the cell of the
**   segment looked at, then the source's; r20 the segments that share, then the copies;
**   r18 the sprites' origin, then the row's x less 7; r19 the blank, then the first
**   line of the row's box; X, Z, r23, r0 what is at hand */
__asm__(
	".section .bss.lift,\"aw\",@nobits\n"
"lift:\t.space 20\n"
"lift_used:\t.space 1\n"
	".section .text.lift,\"ax\",@progbits\n"
	".global gfx_LiftCells\n"
	".type gfx_LiftCells, @function\n"
	".type lift_row, @function\n"
/* X: the cell under the cell at X (r23 is used) */
"lift_under:\n"
	"\tadiw  r26, 1\n"
	"\tmov   r23, r26\n"
	"\tandi  r23, 7\n"
	"\tbrne  lift_ret\n"
	"\tsubi  r26, 8\n"                           /* the next page, 7 back */
	"\tsbci  r27, 0xff\n"
"lift_ret:\n"
	"\tret\n"
/* lift_row: the row of six entries from the OAM's offset r21, its y the slot's (r22) */
"lift_row:\n"
	"\tmov   r23, r22\n"
	"\tsubi  r23, 32\n"                          /* NES_PF_Y */
	"\tcpi   r23, 184\n"                         /* 8 * (PF_ROWS - 1) */
	"\tbrsh  lift_ret\n"
	"\tmov   r30, r21\n"
	"\tldi   r31, " LIFT_OAM "\n"
	"\tldd   r24, Z+3\n"
	"\tlds   r18, Screen+3\n"                    /* spriteX */
	"\tsub   r24, r18\n"
	"\tlds   r0, Screen\n"                       /* scrollX */
	"\tadd   r24, r0\n"                          /* its place in the level, the low byte */
	"\tmov   r25, r24\n"
	"\tandi  r25, 7\n"
	"\tbrne  lift_ret\n"
	"\tldi   r25, " LIFT_VRAM "\n"               /* the row of cells, 0 to 22: its top two bits */
	"\tlsl   r23\n"                              /* are the cell's page, from VRAM's first */
	"\trol   r25\n"
	"\tlsl   r23\n"
	"\trol   r25\n"
	"\tswap  r23\n"                              /* its low three are in the low byte */
	"\tlsr   r23\n"
	"\tandi  r23, 7\n"
	"\tor    r24, r23\n"                         /* r24:r25: the first segment's upper cell */
	"\tlds   r19, pf_ofs\n"
	"\tsubi  r19, -" LIFT_BLANK "\n"
	"\tldi   r20, 0x20\n"                        /* (the bit that falls out after six) */
"lift_seg:\n"
	"\tld    r0, Z\n"                            /* on? */
	"\tcp    r0, r22\n"
	"\tbrne  1f\n"
	"\tldd   r23, Z+3\n"                         /* in the picture? */
	"\tsub   r23, r18\n"
	"\tbrcc  8f\n"
	"\tsubi  r23, -7\n"                          /* left of the origin: 7 at most */
"8:\tcpi   r23, 224\n"                         /* VIEW_W */
	"\tbrsh  1f\n"
	"\tmovw  r26, r24\n"                         /* its two cells the blank? */
	"\tld    r0, X\n"
	"\tcp    r0, r19\n"
	"\tbrne  1f\n"
	"\trcall lift_under\n"
	"\tld    r0, X\n"
	"\tcp    r0, r19\n"
	"\tbrne  1f\n"
	"\tori   r20, 0x40\n"
"1:\tsubi  r30, -4\n"
	"\tsubi  r24, -8\n"
	"\tlsr   r20\n"
	"\tbrcc  lift_seg\n"
	"\tbreq  lift_ret\n"                         /* none: not over the blank, say */
	"\tsubi  r24, 8 * 6\n"
	/* the other entries that reach the row's two rows of cells: the segments each is over */
	"\tmov   r30, r21\n"
	"\tldd   r18, Z+3\n"
	"\tsubi  r18, 7\n"
	"\tmov   r19, r22\n"
	"\tandi  r19, 0xf8\n"
	"\tsubi  r19, 7\n"
	"\tldi   r26, 0\n"
	"\tldi   r27, " LIFT_OAM "\n"
"lift_oth:\n"
	"\tmov   r31, r26\n"
	"\tsub   r31, r21\n"
	"\tld    r30, X+\n"                          /* its y */
	"\tadiw  r26, 2\n"
	"\tld    r23, X+\n"                          /* its x */
	"\tcpi   r31, 4 * 6\n"
	"\tbrlo  2f\n"                               /* one of the row's own */
	"\tsub   r30, r19\n"
	"\tcpi   r30, 23\n"
	"\tbrsh  2f\n"
	"\tsub   r23, r18\n"
	"\tcpi   r23, 8 * 6 + 7\n"
	"\tbrsh  2f\n"
	"\tldi   r31, 3\n"
"3:\tsubi  r23, 8\n"
	"\tbrcs  4f\n"
	"\tlsl   r31\n"
	"\trjmp  3b\n"
"4:\tlsr   r31\n"
	"\tcom   r31\n"
	"\tand   r20, r31\n"
"2:\tcpse  r26, r1\n"
	"\trjmp  lift_oth\n"
	/* the segments before the pass's start are out; then the first left is the source */
	"\tlds   r23, m17_rotate\n"
	"\tlsl   r23\n"
	"\tlsl   r23\n"
	"\tsub   r23, r21\n"
	"\tcpi   r23, 4 * 6\n"
	"\tbrlo  5f\n"
	"\tldi   r23, 0\n"
"5:\tcpse  r23, r1\n"
	"\trjmp  6f\n"
	"\tsbrc  r20, 0\n"
	"\trjmp  7f\n"
	"\tsubi  r23, -4\n"
"6:\tsubi  r23, 4\n"
	"\tsubi  r21, -4\n"
	"\tsubi  r24, -8\n"
	"\tlsr   r20\n"
	"\tbrne  5b\n"
"8:\tret\n"
"7:\tlsr   r20\n"
	"\tbreq  8b\n"                               /* no other: the row as ever */
	"\tlds   r26, lift_used\n"                   /* its record */
	"\tldi   r27, 0\n"
	"\tsubi  r26, lo8(-(lift))\n"
	"\tsbci  r27, hi8(-(lift))\n"
	"\tst    X+, r24\n"
	"\tst    X+, r25\n"
	"\tst    X+, r21\n"
	"\tst    X+, r20\n"
	"\tsubi  r26, lo8(lift)\n"
	"\tsts   lift_used, r26\n"
	"\tmov   r26, r21\n"                         /* the copies off */
	"\tldi   r27, " LIFT_OAM "\n"
	"\tldi   r18, 0xf8\n"
	"\tldi   r23, 4\n"
	"\tset\n"
/* the byte r18 into the places r23 apart after X, in a page, that r20's bits name (bit 0 the
** first); T clear: only where the place holds a RAM tile's number (r22 and r0 are used) */
"lift_put:\n"
	"\tmov   r0, r20\n"
"1:\tadd   r26, r23\n"
	"\tlsr   r0\n"
	"\tbrcc  2f\n"
	"\tbrts  3f\n"
	"\tld    r22, X\n"
	"\tcpi   r22, 64\n"                          /* M17_RAM_INDEXES */
	"\tbrsh  2f\n"
"3:\tst    X, r18\n"
"2:\ttst   r0\n"
	"\tbrne  1b\n"
	"\tret\n"
/* gfx_LiftCells(show): the rows kept. show (T): their copies on again, and their cells the
** source's RAM tiles; or their cells the blank again */
"gfx_LiftCells:\n"
	"\tbst   r24, 0\n"
	"\tlds   r19, pf_ofs\n"
	"\tsubi  r19, -" LIFT_BLANK "\n"
	"\tlds   r30, lift_used\n"
	"\tldi   r31, 0\n"
	"\tsubi  r30, lo8(-(lift))\n"
	"\tsbci  r31, hi8(-(lift))\n"
"1:\tcpi   r30, lo8(lift)\n"
	"\tbreq  lift_ret2\n"
	"\tld    r20, -Z\n"                          /* the copies */
	"\tld    r26, -Z\n"                          /* the source's entry */
	"\tld    r25, -Z\n"
	"\tld    r24, -Z\n"                          /* its upper cell */
	"\tbrtc  2f\n"
	"\tldi   r27, " LIFT_OAM "\n"
	"\tld    r18, X\n"                           /* its y: the copies' again */
	"\tldi   r23, 4\n"
	"\trcall lift_put\n"
"2:\trcall lift_cell\n"
	"\tmovw  r26, r24\n"
	"\trcall lift_under\n"
	"\tmovw  r24, r26\n"
	"\trcall lift_cell\n"
	"\trjmp  1b\n"
/* the copies' cells of the row of cells whose source's is r24:r25: the blank again, or (T) the
** RAM tile the composition gave the source's: its place in the restore list; none, nothing */
"lift_cell:\n"
	"\tmov   r18, r19\n"
	"\tbrtc  3f\n"
	"\tldi   r26, lo8(m17_restore)\n"
	"\tldi   r27, hi8(m17_restore)\n"
	"\tlds   r0, m17_ram_used\n"
	"\tldi   r18, 0\n"
"1:\tcp    r18, r0\n"
	"\tbrsh  4f\n"
	"\tld    r22, X+\n"
	"\tld    r23, X+\n"
	"\tadiw  r26, 1\n"
	"\tcp    r22, r24\n"
	"\tcpc   r23, r25\n"
	"\tbreq  3f\n"
	"\tinc   r18\n"
	"\trjmp  1b\n"
"3:\tmovw  r26, r24\n"
	"\tldi   r23, 8\n"
	"\trjmp  lift_put\n"
"4:\n"
"lift_ret2:\n"
	"\tret\n"
	".size gfx_LiftCells, . - gfx_LiftCells\n"
	".text\n"
);

/* Before the composition: the lifts' rows, by the engine's slots (the last frame's are
** dropped: gfx_Commit() took their copies back) */
void gfx_LiftsOff(void){
	__asm__ volatile(
		"sts   lift_used, r1\n"
		"ldi   r17, 4\n"
	"1:\tmov   r30, r17\n"
		"ldi   r31, 0\n"
		"subi  r30, lo8(-(%[id]))\n"
		"sbci  r31, hi8(-(%[id]))\n"
		"ld    r24, Z\n"
		"subi  r24, 0x24\n"
		"cpi   r24, 7\n"
		"brsh  2f\n"
		"subi  r30, lo8(-(%[y]))\n"                 /* (from one array of the engine's to the next) */
		"sbci  r31, hi8(-(%[y]))\n"
		"ld    r22, Z\n"
		"subi  r30, lo8(-(%[o]))\n"
		"sbci  r31, hi8(-(%[o]))\n"
		"ld    r21, Z\n"
		"rcall lift_row\n"
	"2:\tsubi  r17, 1\n"
		"brcc  1b\n"
		: : [id] "i" (Enemy_ID), [y] "n" ((int)(Enemy_Y_Position - Enemy_ID)),
		    [o] "n" ((int)(Enemy_SprDataOffset - Enemy_Y_Position))
		: "r17", "r18", "r19", "r20", "r21", "r22", "r23", "r24", "r25", "r26", "r27", "r30", "r31", "r0", "cc", "memory");
}

/* What the engine's frame made, given to the mode, in the blank and before the sprites are
** composed: the view where the player took it, the blocks and the texts that changed, the
** scroll and the sprites' origin, the bar, the colour cycle's step */
void gfx_Commit(void){
	u16 xo = 0;
	gfx_LiftCells(false);            /* the last frame's copies, before a cell is written */
	if(view_ready) xo = view_update();
	Screen.scrollX = (u8)xo;
	Screen.spriteX = xo >> 8;
	queued = 0;
	overlay_new = 0;
	gfx_DrawStatusBar();
	if(cycle){
		m17_SetPaletteColor(3, 1, nes_color(pgm_read_byte(&ColorRotatePalette[cycle - 1])));
		cycle = 0;
	}
}

/* ---------------------------------------------------------------------------------------
** The mode's set-up
** ------------------------------------------------------------------------------------ */
void gfx_Init(void){
	SetTileTable(gfx17_bg_tiles);
	m17_SetSpriteTiles(gfx17_spr_tiles, gfx17_spr_boxes);
	Screen.hudTileBank = GFX17_BANK_TEXT;
	Screen.spriteY = SPRITE_Y;
	gfx_ClearScreen();
	for(u8 c = 0; c < M17_HUD_COLS; c++) sb_put(c, GFX17_BLANK);
	memset(OAM_BASE, 0, 256);          /* the NES's page 2 after its reset: zeroes, and every
	                                   ** sprite off screen */
	MoveAllSpritesOffscreen();
}
