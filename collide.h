/* collide.h -- object collisions (collide.c): smbdis.asm lines 11060-11892 */
#pragma once

void FireballEnemyCollision(void);  /* X_: the fireball -> X_ ObjectOffset */
void PlayerHammerCollision(void);   /* X_: the misc object -> X_ ObjectOffset when it hit */
void PlayerEnemyCollision(void);    /* X_: the enemy (ObjectOffset) -> X_ ObjectOffset */
void InjurePlayer(void);            /* X_ not set: its callers reload it */
void ForceInjury(void);             /* A_: the new PlayerStatus when not small */
void SetupFloateyNumber(void);      /* A_: the points control, X_: the enemy */
void EnemiesCollision(void);        /* X_: the enemy (ObjectOffset); X_ not set */
void LargePlatformCollision(void);  /* X_: the platform */
void SmallPlatformCollision(void);  /* X_: the platform */
void PositionPlayerOnS_Plat(void);  /* A_: the collision flag (1, 2), X_: the platform */
void PositionPlayerOnVPlat(void);   /* X_: the platform */

/* For bgcoll.c: the same routines with C parameters */
void ShellOrBlockDefeat_(u8 x);     /* ShellOrBlockDefeat: enemy x */
void SetupFloateyNumber_(u8 x, u8 a);
void RXSpd_(u8 x);                  /* RXSpd: enemy x */
