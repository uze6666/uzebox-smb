/* player.h -- the player and the game modes (player.c): game setup, the game core, the player's
** routines and physics, its graphics offset, and the relative-position and offscreen-bit routines.
** Register convention of smb.h. The routines no other file calls (the player's routines,
** the physics, ScrollHandler, InitializeMemory...) are static in player.c. */
#pragma once

extern const u8 PlayerGfxTblOffsets[16] PROGMEM;

/* Game setup and modes */
void InitializeGame(void);
void InitializeArea(void);
void PrimaryGameSetup(void);
#if TWO_PLAYERS
void StartPlayers(void);            /* a game starts: the second player as the first */
#endif
void SecondaryGameSetup(void);
void GameOverMode(void);
void TerminateGame(void);
void GameMode(void);
void GameCoreRoutine(void);
void UpdScrollVar(void);
void ScrollScreen(void);                /* Y_: the amount */

/* The player's routines */
void AutoControlPlayer(void);           /* A_: the controller bits forced */

/* The player's graphics (logic only; the drawing is gfx17.c's) */
void ProcessPlayerAction(void);         /* -> A_: the PlayerGraphicsTable offset */
void HandleChangeSize(void);            /* -> A_: the offset while growing/shrinking */
void ChkForPlayerAttrib(void);          /* -> A_: 0, 1 the 4th row, 2 the 3rd and 4th rows unflipped */

/* Positions relative to the screen; X_ the object, X_ back to ObjectOffset, A_ the X, Y_ the slot */
void RelativePlayerPosition(void);
void RelativeBubblePosition(void);
void RelativeFireballPosition(void);
void RelativeMiscPosition(void);
void RelativeEnemyPosition(void);
void RelativeBlockPosition(void);

/* Offscreen bits; X_ the object, X_ back to ObjectOffset, A_ the bits, Y_ the slot */
void GetPlayerOffscreenBits(void);
void GetFireballOffscreenBits(void);
void GetBubbleOffscreenBits(void);
void GetMiscOffscreenBits(void);
void GetEnemyOffscreenBits(void);
void GetBlockOffscreenBits(void);
void GetXOffscreenBits(void);           /* X_: the SprObject -> A_ (X_ kept, Y_ the side) */
