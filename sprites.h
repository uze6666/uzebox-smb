/* sprites.h -- the sprite drawing (sprites.c): smbdis.asm's Draw* and *GfxHandler routines,
** ported onto the virtual OAM (gfx.h). Register convention of smb.h. */
#pragma once

/* OAM offsets and flicker */
void InitSprOffsets(void);          /* SecondaryGameSetup's ShufAmtLoop: default OAM offsets */
void SpriteShuffler(void);
void MoveAllSpritesOffscreen(void);
void MoveSpritesOffscreen(void);    /* all but sprite 0 */

/* Objects */
void DrawVine(void);                /* Y_: the vine slot, returned */
void DrawHammer(void);              /* X_: the misc object */
void FlagpoleGfxHandler(void);      /* X_: the flag */
void DrawLargePlatform(void);
void JCoinGfxHandler(void);         /* X_: the misc object */
void DrawPowerUp(void);
void EnemyGfxHandler(void);         /* X_: the enemy */
void DrawBlock(void);               /* X_: the block object */
void DrawBrickChunks(void);         /* X_: the block object */
void DrawFireball(void);            /* X_: the fireball */
void DrawFirebar(void);             /* Y_: the OAM offset, kept */
void DrawExplosion_Fireball(void);  /* X_: the fireball */
void DrawExplosion_Fireworks(void); /* A_: the frame, Y_: the OAM offset */
void DrawSmallPlatform(void);
void DrawBubble(void);              /* X_: the bubble */
void DrawStarFlag(void);            /* X_: the star flag */

/* Drawing tails of routines ported elsewhere */
void DrawFloateyNumber(void);       /* FloateyNumbersRoutine's OAM part; X_ the slot, C_ */
void DrawBowserFlame(void);         /* ProcBowserFlame from DrawFlameLoop; X_, M00, M01 */

/* The player */
void PlayerGfxHandler(void);
void DrawPlayer_Intermediate(void);
