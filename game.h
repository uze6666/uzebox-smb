/* game.h -- see smb.h */
#pragma once
void game_Init(void);
#if TWO_PLAYERS
u8 game_Pad2(void);                 /* the second pad's bits, for the second player's turn */
#endif
void game_Frame(void);
/* The frame's two halves (game_Frame is both in a row): what the last frame asked of the
** screen, the 6502's upload at its NMI's start, which mode 17's loop does with its commit
** before the sprites are composed; then the rest, the engine's frame */
void game_Upload(void);
void game_Engine(void);
void ScreenRoutines(void);
void AreaParserTaskHandler(void);
void sound_Init(void);            /* sound.c */
