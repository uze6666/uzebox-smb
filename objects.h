/* objects.h -- the objects (objects.c): fireballs, bubbles, the game timer, whirlpools, the
** flagpole, jumpsprings, vines, cannons, hammers, coins, the score, power-ups, block objects,
** floatey numbers and the movement/gravity helpers. Registers as in smb.h. */
#pragma once

extern const u8 FloateyNumTileData[24] PROGMEM;   /* two tiles a FloateyNum_Control value */

/* Floatey numbers and the status bar digits */
void FloateyNumbersRoutine(void);       /* X_: the enemy slot (= ObjectOffset) */
void DigitsMathRoutine(void);           /* Y_: the 6502's display offset of the last digit */
void UpdateTopScore(void);
void GiveOneCoin(void);
void AddToScore(void);                  /* adds DigitModifier to the score */
void GetSBNybbles(void);
void UpdateNumber(void);                /* A_: the status nybbles; X_ = ObjectOffset after */

/* Fireballs, bubbles, timer, whirlpools, flagpole */
void ProcFireball_Bubble(void);
void SetupBubble(void);                 /* X_: the bubble, M07: the pseudorandom bit */
void RunGameTimer(void);
void WarpZoneObject(void);              /* X_: the enemy */
void ProcessWhirlpools(void);
void FlagpoleRoutine(void);

/* Enemy-slot objects */
void JumpspringHandler(void);           /* X_: the enemy */
void Setup_Vine(void);                  /* X_: the enemy slot, Y_: the block object */
void VineObjectHandler(void);           /* X_: the enemy */
void ProcessCannons(void);
void PwrUpJmp(void);
void PowerUpObjHandler(void);

/* Misc objects: hammers and jumping coins */
void SpawnHammerObj(void);              /* C_ set when a hammer was spawned; X_ = ObjectOffset */
void MiscObjectsCore(void);

/* Block objects */
void PlayerHeadCollision(void);         /* A_: the metatile; M02, M06, BBPtr: where */
void InitBlock_XY_Pos(void);            /* X_: the block object */
void BlockObjectsCore(void);            /* X_ = ObjectOffset: the block object */
void BlockObjMT_Updater(void);

/* Movement and gravity (X_: the SprObject index unless noted) */
void MoveEnemyHorizontally(void);       /* X_: the enemy; A_ returned as the 6502's */
void MovePlayerHorizontally(void);
void MoveObjectHorizontally(void);      /* -> A_: the whole-pixel move + carry */
void MovePlayerVertically(void);
void MoveD_EnemyVertically(void);       /* X_: the enemy, and so on below */
void MoveFallingPlatform(void);
void MoveRedPTroopaDown(void);
void MoveRedPTroopaUp(void);
void MoveDropPlatform(void);
void MoveEnemySlowVert(void);
void MoveJ_EnemyVertically(void);
void SetHiMax(void);
void SetXMoveAmt(void);                 /* A_: maximum speed, Y_: downward force */
void ImposeGravityBlock(void);
void ImposeGravitySprObj(void);         /* A_: maximum speed, M00: downward force */
void MovePlatformDown(void);
void MovePlatformUp(void);
void RedPTroopaGrav(void);
void ImposeGravity(void);               /* A_: 0 down only, else up too; M00 M01 M02 */

/* implemented in gfx17.c */
/* The floatey number of enemy slot X_: FloateyNum_Control[X_] (1-11, FloateyNumTileData[c*2],
** [c*2+1]), FloateyNum_X_Pos[X_] (left tile; right at +8), FloateyNum_Y_Pos[X_]: the sprite Y
** is FloateyNum_Y_Pos - 8 - !C_ (the 6502's carry: 9 up while it sits in the status bar), palette 2 */
void DrawFloateyNumber(void);
/* The 6502's "lda VRAM_Buffer1 / bne": true when anything has been queued for the screen since
** the last NMI (a block metatile, a status bar number...): BlockObjMT_Updater replaces one block
** a frame, and none on a frame something else was written */
bool VRAM_Buffer1_Pending(void);
