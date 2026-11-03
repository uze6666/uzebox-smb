/*
 * gfx.h -- the platform layer's side of the engine: the routines that drew on the NES (OAM,
 * the PPU, the VRAM buffer) under their asm names, with the register convention of smb.h.
 * Their bodies are in gfx17.c (video mode 17), not ported from the 6502.
 */
#pragma once

/* Background and status bar (the 6502 wrote the VRAM buffer) */
void WriteGameText(void);           /* A_: the text number (warp zone, world/lives, game over...) */
void RemoveCoin_Axe(void);          /* blanks the metatile at M02/M06 on screen */
void ReplaceBlockMetatile(void);    /* X_: the block object; puts Block_Metatile on screen */
void DestroyBlockMetatile(void);    /* blanks the bumped block on screen */
void RemBridge(void);               /* a bridge column gone (BridgeCollapse) */
/* What mode 17's platform (gfx17.c) has nothing to do for: no call is made */
#define RenderAttributeTables()     ((void)0)
#define MoveVOffset()               ((void)0)
#define gfx_OamBegin()              ((void)0)   /* the OAM is a page of SRAM that stays */
#define gfx_OamPlay()               ((void)0)
#define gfx_IntermissionCamera()    ((void)0)   /* a text screen is the view at the screen's left */
void PrintStatusBarNumbers(void);   /* A_: the status nybbles (the 6502's) */
void GetPlayerColors(void);         /* the player's palette for its status */
void ColorRotation(void);           /* the "?" blocks' and coins' colour cycle */
void BowserPaletteRequest(void);    /* the axe object loaded Bowser's palette */

/* Sprites */
void PlayerGfxHandler(void);
void DrawPlayer_Intermediate(void);
void EnemyGfxHandler(void);         /* X_: the enemy */
void DrawPowerUp(void);
void DrawVine(void);                /* Y_: the vine slot */
void DrawHammer(void);              /* X_: the misc object */
void FlagpoleGfxHandler(void);
void JCoinGfxHandler(void);         /* X_: the misc object */
void DrawBlock(void);               /* X_: the block object */
void DrawBrickChunks(void);         /* X_: the block object */
void DrawFireball(void);            /* X_: the fireball */
void DrawExplosion_Fireball(void);  /* X_: the fireball */
void DrawFirebar(void);
void DrawExplosion_Fireworks(void);
void DrawSmallPlatform(void);
void DrawLargePlatform(void);
void DrawBubble(void);              /* X_: the bubble */
void DrawOneSpriteRow(void);
void DumpTwoSpr(void);
void DrawFloateyNumber_Coin(void);
void DrawStarFlag(void);

/* ---------------------------------------------------------------------------------------
** The virtual OAM: the NES's 64 sprites x 4 bytes (Y, tile, attributes, X), addressed by the
** 6502's byte offset 0-255 exactly as Sprite_Data,y was. It is mode 16's page 2 (buffer B and
** the renderer's state bytes), free while the game runs in the vertical blank: gfx17.c restores
** the state after converting the OAM into mode 16 sprites (x at 3/4, the vertical camera, NES
** tiles to RAM tiles), and keeps the OAM in SPI RAM between frames, as the NES's persisted.
** Write and read it only from the engine's frame (never from an interrupt).
** ------------------------------------------------------------------------------------ */
/* Mode 17 (gfx17.c): the mode's own sprite list, which has the NES's format and stays from
** frame to frame as the NES's OAM did: a page of the mode's memory at a constant address (the
** Makefile's M17_OAM_AT). The mode shows it through an origin: nothing is copied or converted */
#define OAM_BASE ((u8 *)M17_OAM_AT)
#define Sprite_Data(o)        (OAM_BASE[(u8)(o)])
#define Sprite_Y_Position(o)  (OAM_BASE[(u8)(o)])
#define Sprite_Tilenumber(o)  (OAM_BASE[(u8)((o) + 1)])
#define Sprite_Attributes(o)  (OAM_BASE[(u8)((o) + 2)])
#define Sprite_X_Position(o)  (OAM_BASE[(u8)((o) + 3)])
void gfx_Commit(void);              /* gfx17.c: what the engine's frame made, given to the mode */
void gfx_LiftsOff(void);            /* ... round the composition: a lift's row is composed once, */
void gfx_LiftCells(bool show);      /* its other segments off meanwhile and given back after */
#define gfx_LiftsOn() gfx_LiftCells(true)

/* ---------------------------------------------------------------------------------------
** The platform's own (gfx17.c, text.c)
** ------------------------------------------------------------------------------------ */
void gfx_Init(void);                /* the mode's state: VRAM clear, the caches, the bar row */
bool gfx_ScreenOff(void);           /* the engine's DisableScreenFlag: an area being set up */
void gfx_ResetView(void);           /* a new view from the engine's camera: its rows drawn over the next frames */
u8 gfx_LinesLeft(void);             /* picture lines left before the renderer starts */
void gfx_BeginFrame(void);          /* every OAM sprite offscreen, before the engine draws */
void gfx_FrameStart(void);          /* the NMI's start: the VRAM buffer is free again */
#define gfx_Frame()                 ((void)0)   /* mode 17's platform does its part in gfx_Commit() */
void gfx_LoadAreaPalette(void);     /* the area type's 8 palettes */
void gfx_DrawStatusBar(void);
void gfx_Text(u8 n);                /* WriteGameText's screens: 1 world/lives, 2 time up, ... */
void gfx_LoadPalette(u8 t);         /* area type t's palettes (VRAM_Buffer_AddrCtrl 1-4) */
void gfx_LoadAltPalette(u8 c);      /* VRAM_Buffer_AddrCtrl 9-11: the snow and mushroom palettes */
void gfx_ClearScreen(void);         /* the playfield blank, the level hidden (a text screen) */
void gfx_PutText(u8 col, u8 row, const u8 *s, u8 len);   /* NES tiles at a playfield cell */
void gfx_LevelText(u16 tc, s8 tr, const u8 *s, u8 len);  /* text pinned to the level (flash) */
void gfx_ClearOverlays(void);
u16 gfx_NameTableColumn(u8 nt, u8 col);
#define gfx_FireworksReady()        true        /* no vertical camera: the fireworks are in sight */
/* bb.c's, for mode 17's platform: the view is the name table, and gfx17.c writes it */
void bb_PutViewCell(u8 c, u8 r, u8 v);
void bb_GetViewColumn(u8 c, u8 *dst);          /* 13 metatiles */
#define LEVEL_Y(tr) (32 + 8 * (tr)) /* the NES y of level tile row tr */
void gfx_WarpZoneText(u8 n);        /* text.c */
/* SD=1 (the .uze's build, sd17.c): the level data and the music are not in the flash. They
** are a section of their own, read from the game's file on the card into SPI RAM when the
** console is switched on, and a pointer to them is their address there */
#ifndef SD
	#define SD 0
#endif
#if SD
#define SDDATA __attribute__((section(".sddata")))
u8 sd_byte(const u8 *p);            /* a byte of that data */
void sd_Load(void);                 /* at power-on: the data, or the fault on the screen for good */
#endif
void gfx_Message(u8 c);
void gfx_DrawTitleScreen(void);
/* ATTRACT=1: the title is the logo over 1-1's first screen, pinned to the level, and the demo
** plays under it and scrolls it away (the 6502's); 0: text on black, static, no demo */
/* NESTITLE=1 (the Makefile: the .uze's build has it, which has the room): with ATTRACT, the
** title as the cartridge has it: the board with every tile its own, the copyright line under
** it in the board's palette (tools/gen_gfx17.py's second set of files: 13 tiles more, and a
** window of the title's own) */
#ifndef NESTITLE
	#define NESTITLE 0
#endif
void gfx_ShowTitle(void);           /* the title: over the coming redraw (ATTRACT), or its own screen */
extern bool gfx_title;              /* the title is shown (ATTRACT: is in the level) */
u8 text_TitleCell(u8 c, u8 r);      /* text.c: the title's text at view cell (c, r), or 0xff (mode
                                    ** 17: at the name table's column c, the playfield's row r) */
/* The title's rows, the player line and the top score, and their columns: the cursor's, the
** line's, the top score's digits. Mode 17 with ATTRACT has the 6502's own (its name table
** rows 18 and 23, less the 4 of the status bar) */
#define TITLE_ROW_PLAYER 14
#define TITLE_ROW_TOP    19
#define TITLE_COL_CURSOR 9
#define TITLE_COL_TEXT   11
#define TITLE_COL_TOP    12
#define TITLE_COL_SCORE  16
void gfx_TitleCursor(void);
void gfx_TopScore(void);
void gfx_WorldSelect(u8 w);
extern u8 gfx_bar_world, gfx_bar_level;   /* text.c: the world and level the bar shows */
