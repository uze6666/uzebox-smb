/*
 * The screens the 6502 printed: the world/lives display, time up, game over, the warp zone's
 * welcome, the victory messages and the title screen. Text is the NES font (background tiles
 * $00-$2b) in background palette 2, as the 6502's attribute fill made it. Positions are mode
 * 16's: the playfield is 28 columns by 13 rows under the status bar. On mode 17 (VMODE == 17:
 * the NES's own 8 x 8 cells, 28 columns of its 32 and 24 rows under the bar) they are the
 * 6502's: a name table row less 4 is a playfield row, a column is a column.
 */
#include <string.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include "smb.h"

#define TXT_PAL 2

void gfx_PutText(u8 col, u8 row, const u8 *s, u8 len);   /* gfx17.c: NES tiles at a VRAM cell */

/* The messages are the cartridge's, read from your ROM by tools/gen_text.py (textdata.h): the 6502's
** VRAM update records named beside each. The world and the lives: its WorldLivesDisplay, laid out
** for 28 columns */
#include "textdata.h"
static const u8 TxtWorld[] PROGMEM = { TXT_WORLD };   /* WorldLivesDisplay */
static const u8 TxtTimeUp[] PROGMEM = { TXT_TIMEUP };   /* OnePlayerTimeUp */
static const u8 TxtGameOver[] PROGMEM = { TXT_GAMEOVER };   /* OnePlayerGameOver */
static const u8 TxtWarp[] PROGMEM = { TXT_WARP };   /* WarpZoneWelcome */
static const u8 TxtThanks[] PROGMEM = { TXT_THANKS };   /* MarioThanksMessage */
static const u8 TxtButOur[] PROGMEM = { TXT_BUTOUR };   /* MushroomRetainerSaved, first line */
static const u8 TxtAnother[] PROGMEM = { TXT_ANOTHER };   /* MushroomRetainerSaved, second line */
static const u8 TxtQuest[] PROGMEM = { TXT_QUEST };   /* PrincessSaved1 */
static const u8 TxtNewQuest[] PROGMEM = { TXT_NEWQUEST };   /* PrincessSaved2 */
static const u8 TxtPushB[] PROGMEM = { TXT_PUSHB };   /* WorldSelectMessage1 */
static const u8 TxtSelect[] PROGMEM = { TXT_SELECT };   /* WorldSelectMessage2 */

#if TWO_PLAYERS
/* The player's name over the time-up and game-over lines in a game of two (the 6502's $21cd, its
** TwoPlayerTimeUp and TwoPlayerGameOver, LuigiName), and the victory message's second form */
static const u8 TxtNames[] PROGMEM = { TXT_NAMES };   /* the two names */
static const u8 TxtThanksL[] PROGMEM = { TXT_THANKSL };   /* LuigiThanksMessage */
#endif

static u8 ram_text[4];

static void put_P(u8 col, u8 row, const u8 *s, u8 len){
	u8 buf[28];
	if(len > sizeof buf) len = sizeof buf;
	memcpy_P(buf, s, len);
	gfx_PutText(col, row, buf, len);
}


/* The world and level the bar shows: what they were when the 6502 last wrote the bar's line
** (a screen's set-up, the world select), not what they are: a warp pipe changes WorldNumber
** a second before the screen goes */
u8 gfx_bar_world, gfx_bar_level;

void gfx_Text(u8 n){
	switch(n){
	case 0:
		gfx_bar_world = WorldNumber;
		gfx_bar_level = LevelNumber;
		gfx_DrawStatusBar();
		break;
	case 1: {                                 /* WORLD w-l, and Mario x lives */
		gfx_ClearScreen();
		put_P(11, 6, TxtWorld, 5);            /* the 6502's $214b */
		ram_text[0] = WorldNumber + 1;
		ram_text[1] = 0x28;
		ram_text[2] = LevelNumber + 1;
		gfx_PutText(17, 6, ram_text, 3);
		u8 lives = NumberofLives + 1;
		ram_text[0] = 0x29;                   /* the cross */
		ram_text[1] = 0x24;
		if(lives >= 10){                      /* two digits, the code's 99 or lives won: no crown
			                                  ** (the user, 2026-10-11: its test is 22 bytes) */
			u8 t = 0;
			if(lives > 99) lives = 99;        /* 1-ups past 99: the tens ran into the letters */
			do{ lives -= 10; t++; }while(lives >= 10);
			ram_text[1] = t;
		}
		ram_text[2] = lives;
		/* the 6502's $21cd: the cross at column 15, the crown at 17, the lives at 18; its
		** player stands at x 96, y 104 to 119, beside them */
		gfx_PutText(15, 10, ram_text, 1);
		gfx_PutText(17, 10, ram_text + 1, 2);
		gfx_IntermissionCamera();
		break;
	}
	case 2:
		gfx_ClearScreen();
		put_P(12, 12, TxtTimeUp, sizeof TxtTimeUp);        /* the 6502's $220c */
#if TWO_PLAYERS
		/* CheckPlayerName: whose time was up. The turn has passed since, but in the game over
		** mode (and where the other had no turn to take, the 6502 names him all the same) */
		n = CurrentPlayer ^ (OperMode != GameOverModeValue);
		goto name;
#endif
		gfx_IntermissionCamera();
		break;
	case 3:
		gfx_ClearScreen();
		put_P(11, 12, TxtGameOver, sizeof TxtGameOver);    /* the 6502's $220b */
#if TWO_PLAYERS
		n = CurrentPlayer;
	name:                                     /* the player's name over it, in a game of two */
		if(NumberOfPlayers) put_P(13, 10, n ? TxtNames + 5 : TxtNames, 5);
#endif
		gfx_IntermissionCamera();
		break;
	default:                                  /* 4-6: the warp zone and its numbers */
		gfx_WarpZoneText(n);
		break;
	}
}

/* The warp zone: its welcome (WarpZoneWelcome) and a world number over each pipe, pinned to the level
** where the 6502 wrote them (name table 1: row 12 col 4, row 17 cols 5, 13 and 21) */

void gfx_WarpZoneText(u8 n){
	gfx_LevelText(gfx_NameTableColumn(1, 4), 12 - 4, TxtWarp, sizeof TxtWarp);
	u8 i = (n - 4) << 2;
	for(u8 p = 0; p < 3; p++){
		gfx_LevelText(gfx_NameTableColumn(1, 5 + 8 * p), 17 - 4, &WarpZoneNumbers[i + p], 1);
	}
}

/* The victory messages (VRAM_Buffer_AddrCtrl $0c-$12), pinned to the room the player walked
** into: the 6502's columns, rows moved down to share the 92-line view with the player and the
** retainer, the camera raised to them. On mode 17 the 6502's rows too (name table 1's rows 10,
** 14, 16, 13, 15, 18 and 20, less 4); one column is not its own: PrincessSaved2's line
** is 27 cells from column 3 there, and the view is the screen's first 28 */
void gfx_Message(u8 c){
	switch(c){
	case 0x0c: case 0x0d:
#if TWO_PLAYERS
		/* (the 6502's message $0d, which its PrintVictoryMessages asks for in the second
		** player's place: the port's asks for $0c, and the name is chosen here) */
		gfx_LevelText(gfx_NameTableColumn(1, 8), 6, CurrentPlayer ? TxtThanksL : TxtThanks, sizeof TxtThanks);
#else
		gfx_LevelText(gfx_NameTableColumn(1, 8), 6, TxtThanks, sizeof TxtThanks);
#endif
		break;
	case 0x0e:
		gfx_LevelText(gfx_NameTableColumn(1, 5), 10, TxtButOur, sizeof TxtButOur);
		gfx_LevelText(gfx_NameTableColumn(1, 5), 12, TxtAnother, sizeof TxtAnother);
		break;
	case 0x0f:
		gfx_LevelText(gfx_NameTableColumn(1, 7), 9, TxtQuest, sizeof TxtQuest);
		break;
	case 0x10:
		gfx_LevelText(gfx_NameTableColumn(1, 1), 11, TxtNewQuest, sizeof TxtNewQuest);
		break;
	case 0x11:
		gfx_LevelText(gfx_NameTableColumn(1, 10), 14, TxtPushB, sizeof TxtPushB);
		break;
	default:
		gfx_LevelText(gfx_NameTableColumn(1, 8), 16, TxtSelect, sizeof TxtSelect);
		break;
	}
}

/* The title screen: the name, the player line and the top score under it, in the font; with
** ATTRACT the logo (gfx17.c) over 1-1's first screen and the same two lines */
static const u8 TxtPlayer[] PROGMEM = { TXT_PLAYER };   /* the title's first line */
static const u8 TxtTop[] PROGMEM = { TXT_TOP };   /* the title's top score */

void gfx_DrawTitleScreen(void){
	gfx_ShowTitle();
}

/* The title's text (gfx17.c draws the logo above it): view cell (c, r) of the title's screen */
u8 text_TitleCell(u8 c, u8 r){
#if TWO_PLAYERS
	/* the two player lines, the second two rows down with its digit 2, the cursor beside the
	** one chosen (the 6502's name table rows 18 and 20; DrawMushroomIcon) */
	u8 two = r - TITLE_ROW_PLAYER;
	if(!(two & 0xfd)){
		two >>= 1;
		if(c == TITLE_COL_CURSOR) return two == NumberOfPlayers ? 0xce : 0xff;
		if(c == TITLE_COL_TEXT) return 1 + two;
		if((u8)(c - TITLE_COL_TEXT) < sizeof TxtPlayer) return pgm_read_byte(&TxtPlayer[c - TITLE_COL_TEXT]);
	}else if(r == TITLE_ROW_TOP){
#else
	if(r == TITLE_ROW_PLAYER){
		if(c == TITLE_COL_CURSOR) return 0xce;    /* the mushroom cursor */
		if((u8)(c - TITLE_COL_TEXT) < sizeof TxtPlayer) return pgm_read_byte(&TxtPlayer[c - TITLE_COL_TEXT]);
	}else if(r == TITLE_ROW_TOP){
#endif
		if((u8)(c - TITLE_COL_TOP) < sizeof TxtTop) return pgm_read_byte(&TxtTop[c - TITLE_COL_TOP]);
		/* the top score as the cartridge prints it: the six digits, a blank for the first when
		** it is 0 (UpdateNumber's zero suppression), and the 0 its title has after them ($22f6):
		** "TOP- 000000", the ones always 0. (Until 2026-10-11 the six digits alone, from the
		** dash on: a tenth of the score to the eye) */
		u8 d = c - TITLE_COL_SCORE;
		if(d < 6){
			u8 v = TopScoreDisplay[d];
			return (d || v) ? v : 0x24;
		}
		if(d == 6) return 0x00;
	}
	return 0xff;
}

#if !TWO_PLAYERS   /* (with it gfx17.c's: the cursor's column again) */
void gfx_TitleCursor(void){
	ram_text[0] = 0xce;                       /* the mushroom icon */
#if TWO_PLAYERS
	/* ... beside the line chosen, a blank beside the other (the 6502's DrawMushroomIcon) */
	u8 r = TITLE_ROW_PLAYER + 2 * NumberOfPlayers;
	gfx_PutText(TITLE_COL_CURSOR, r, ram_text, 1);
	ram_text[0] = 0x24;
	gfx_PutText(TITLE_COL_CURSOR, (u8)(2 * TITLE_ROW_PLAYER + 2 - r), ram_text, 1);
#else
	gfx_PutText(TITLE_COL_CURSOR, TITLE_ROW_PLAYER, ram_text, 1);
#endif
}
#endif

/* WriteTopScore. With ATTRACT the line is text_TitleCell's alone: the title's redraw has
** drawn it (and six digits written over it here were the score without its blank). The
** static title's is written here, the same seven cells */
void gfx_TopScore(void){
}

/* The 6502 wrote the chosen world in the status bar ($2073, WSelectBufferTemplate): the bar's
** world, which gfx_DrawStatusBar draws every frame */
void gfx_WorldSelect(u8 w){
	gfx_bar_world = w - 1;
}
