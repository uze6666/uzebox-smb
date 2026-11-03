/* bosses.h -- bosses, special objects and platforms (bosses.c): smbdis.asm lines 10067-11050 */
#pragma once

/* the star flag's sprites (DrawStarFlag, gfx17.c): y and x adders and tiles, four sprites */
extern const u8 StarFlagYPosAdder[4] PROGMEM;
extern const u8 StarFlagXPosAdder[4] PROGMEM;
extern const u8 StarFlagTileData[4] PROGMEM;

void BridgeCollapse(void);
void RunBowser(void);               /* X_: the enemy */
void KillAllEnemies(void);
void SetFlameTimer(void);           /* -> A_: the flame timer */
void ProcBowserFlame(void);         /* X_: the flame */
void RunFireworks(void);            /* X_: the enemy */
void RunStarFlagObj(void);          /* X_: the enemy */
void MovePiranhaPlant(void);        /* X_: the enemy */
void FirebarSpin(void);             /* A_: the speed, X_: the enemy -> A_ the new high byte, C_ */
void BalancePlatform(void);         /* X_: the enemy */
void YMovingPlatform(void);
void XMovingPlatform(void);
void DropPlatform(void);
void RightPlatform(void);
void MoveLargeLiftPlat(void);
void MoveSmallPlatform(void);
void OffscreenBoundsCheck(void);    /* X_: the enemy */
/* HammerChk, ChkFireB, BowserGfxHandler, the star flag's tasks, SetupPlatformRope and the
** platform helpers are bosses.c's own (static) */

/* implemented in gfx17.c */

/* DrawBowserFlame: Bowser's flame, three sprites in a row (the 6502's DrawFlameLoop and what
** follows). In: X_ the flame (an enemy, ObjectOffset), Enemy_Rel_XPos/Enemy_Rel_YPos its
** position, M00 the first tile ($51, then $52, $53), M01 the attributes ($02, or $82 flipped
** vertically), Enemy_OffscreenBits already computed: a sprite whose bit is set is hidden
** (d3 the first sprite, d2 the second, d1 the third; d0 a fourth that does not exist). The 6502
** also added 24 to Enemy_Rel_XPos on the way, which nothing reads. Leave X_ = ObjectOffset. */
void DrawBowserFlame(void);

/* DrawEraseRopeHook: the two ropes of a balance platform, drawn or erased. In: RopeVRAMData,
** the 11 bytes the 6502 wrote to its VRAM buffer: for each rope a name table address (high,
** low: $20-$27xx as SetupPlatformRope computed it), a length 2, and two tiles ($a2 $a3 rope,
** $24 $24 blank), then a 0 terminator. The 6502 dropped the whole update when its buffer was
** past $20 bytes: gfx17.c decides. */
extern u8 RopeVRAMData[11];
void DrawEraseRopeHook(void);
