/* enemies.h -- the enemies (enemies.c): loading them from the enemy data, initialising and
** moving them, a port of smbdis.asm lines 7763-10066. See smb.h */
#pragma once

/* The routines other modules call (X_: the enemy slot, as the 6502's) */
void EnemiesAndLoopsCore(void);     /* X_: the slot (ObjectOffset); no register on return */
void InitVStf(void);                /* A_ 0 on return */
void InitPiranhaPlant(void);
void RunRetainerObj(void);
void EraseEnemyObject(void);        /* A_ 0 on return */
void SetHJ(void);                   /* Y_: the vertical speed, M00 the jump-length bitmask */
void MoveNormalEnemy(void);
void MoveJumpingEnemy(void);
void XMoveCntr_Platform(void);      /* A_: the secondary counter's maximum */
void MoveWithXMCntrs(void);         /* M00: the amount moved (MoveEnemyHorizontally's A) */
