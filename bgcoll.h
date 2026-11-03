/* bgcoll.h -- background collision and bounding boxes (bgcoll.c): smbdis.asm 11899-13090 */
#pragma once

extern const u8 WarpZoneNumbers[12] PROGMEM;    /* also printed by WriteGameText */

/* The player */
void PlayerBGCollision(void);
void ImpedePlayerMove(void);        /* M00: 1 or 2, the side */

/* Enemies: X_ the enemy */
void EnemyToBGCollisionDet(void);
void EnemyJump(void);
void PlayerEnemyDiff(void);         /* -> M00 x difference, A_/N_ page difference, C_ */
u8 PlayerEnemyDiff_(u8 x);          /* the same, the page difference returned (N_ not set) */
void SetStun_(u8 x);                /* SetStun (collide.c) */
void ChkToStunEnemies_(u8 x, u8 a); /* ChkToStunEnemies: a the identifier (or what was in A) */

/* Fireballs: X_ the fireball */
void FireballBGCollision(void);

/* Bounding boxes */
void GetFireballBoundBox(void);     /* X_: the fireball -> X_ ObjectOffset */
void GetMiscBoundBox(void);         /* X_: the misc object -> X_ ObjectOffset */
void GetEnemyBoundBox(void);        /* X_: the enemy */
void SmallPlatformBoundBox(void);
void LargePlatformBoundBox(void);
void BoundingBoxCore(void);         /* X_ the box slot, Y_ the relative position */
bool SprObjectCollisionCore_(u8 x, u8 y);   /* the boxes' offsets -> true if they overlap */

/* The block buffer */
void BlockBufferCollision(void);    /* A_ flag, X_ SprObject, Y_ adder -> M02-M06, BBPtr */
