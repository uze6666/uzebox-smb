/*
 * Super Uze Brothers -- the game state, under the names of doppelganger's SMB disassembly
 * (ref/smbdis.asm), so every ported routine reads like the 6502 it came from.
 *
 * The 6502 kept per-object fields as parallel arrays indexed by an object slot; they are
 * kept that way (one C array a family) with the original slot order:
 *   0 player, 1-6 enemies, 7-8 fireballs, 9-12 block objects, then MISC_SLOTS misc objects
 *   (jumping coins, hammers), then BUBBLE_SLOTS air bubbles.
 * A name the 6502 used for one slot (Player_X_Position) is that array's element; a name it
 * indexed (Enemy_X_Position,x) is a pointer into the array (Enemy_X_Position[x]). Names that
 * shared an address in the original share the C object. Code uses the OFS_ constants, never
 * the 6502's literal slot offsets ($07, $09, $0d), so the slot counts can shrink.
 *
 * The original cleared RAM by address range: InitializeArea up to $074b, InitializeGame up to
 * $076f, a cold boot everything. So the variables are three structs by address range:
 *   ra (< $074c), rg ($074c-$076f), rp ($0770 and up), each cleared with one memset.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;

/* ---------------------------------------------------------------------------------------
** Object slots
** ------------------------------------------------------------------------------------ */
#define MISC_SLOTS     6          /* the original: 9 (hammers need them all; coins 3) */
#define BUBBLE_SLOTS   1          /* the original: 3 */
#define ENEMY_SLOTS    6
#define OFS_ENEMY      1
#define OFS_FIREBALL   7
#define OFS_BLOCK      9
#define OFS_MISC       13
#define OFS_BUBBLE     (OFS_MISC + MISC_SLOTS)
#define N_MOVERS       (OFS_MISC + MISC_SLOTS)          /* state, speeds, forces */
#define N_SPROBJ       (OFS_BUBBLE + BUBBLE_SLOTS)      /* positions */

/* Bounding boxes and their size control: player, enemies, fireballs, then misc */
#define BB_FIREBALL    7
#define BB_MISC        9
#define N_BBOX         (BB_MISC + MISC_SLOTS)

/* The relative-position and offscreen-bit arrays have their own layout */
#define REL_PLAYER 0
#define REL_ENEMY  1
#define REL_FBALL  2
#define REL_BUBBLE 3
#define REL_BLOCK  4
#define REL_MISC   6
#define N_REL      11

/* The block buffer: 13 rows a column, BB_COLS columns in a ring (the 6502: two 16-column
** buffers at $0500 and $05d0, chosen by the page's low bit). BB_COLS a multiple of 16: the
** 6502 takes a column's low nybble as its x / 16 */
#define BB_COLS 32

/* ---------------------------------------------------------------------------------------
** < $074c: cleared by InitializeArea (and everything above)
**
** Two runs of it are lists, so the AVR build can put them in the plane tails (tails.h) as
** structs of their own, rab and rac, while the PC keeps one struct in the original order (its
** traces hash it whole). A member there is reached by its name only: offsetof(struct RamArea)
** and pointer arithmetic from &ra do not reach it. RA_CLEAR() clears all of ra.
** ------------------------------------------------------------------------------------ */
#define RA_0400 	u8 SprObject_X_MoveForce_[N_MOVERS];       /* $0400 */ 	u8 SprObject_YMF_Dummy_[N_SPROBJ];         /* $0416 */ 	u8 SprObject_Y_MoveForce_[N_SPROBJ];       /* $0433 */ 	s8 MaximumLeftSpeed_, MaximumRightSpeed_;   /* $0450, $0456 */ 	u8 Cannon_Offset_;                         /* $046a */ 	u8 Cannon_PageLoc_[6], Cannon_X_Position_[6], Cannon_Y_Position_[6], Cannon_Timer_[6];
#define RA_0483 	u8 BowserHitPoints_, StompChainCounter_;    /* $0483 */ 	u8 SprObject_CollisionBits_[OFS_ENEMY + ENEMY_SLOTS + 2];   /* $0490 */ 	u8 SprObj_BoundBoxCtrl_[N_BBOX];           /* $0499 */ 	u8 BoundingBox_UL_Corner_[N_BBOX * 4];     /* $04ac */

struct RamArea {
	/* zero page */
	u8 M_[8];                                  /* $00-$07: the scratch bytes */
	u8 ObjectOffset_, FrameCounter_;
	u8 A_B_Buttons_, Up_Down_Buttons_, Left_Right_Buttons_, PreviousA_B_Buttons_;
	u8 GameEngineSubroutine_;
	u8 Enemy_Flag_[ENEMY_SLOTS + 1];           /* $0f */
	u8 Enemy_ID_[ENEMY_SLOTS + 1];             /* $16 */
	u8 SprObject_State_[N_MOVERS];             /* $1d */
	u8 PlayerFacingDir_;                       /* $33 */
	u8 DestinationPageLoc_;                    /* $34 */
	u8 FirebarSpinDirection_[ENEMY_SLOTS];     /* $34,x (enemy slots: never slot 5's $39) */
	u8 VictoryWalkControl_, PowerUpType_, FireballBouncingFlag_[2];   /* $35, $39, $3a by fireball */
	u8 HammerBroJumpTimer_[ENEMY_SLOTS];       /* $3c */
	u8 SprObject_MovingDir_[OFS_ENEMY + ENEMY_SLOTS + 2];   /* $45 */
	s8 SprObject_X_Speed_[N_MOVERS];           /* $57 */
	u8 SprObject_PageLoc_[N_SPROBJ];           /* $6d */
	u8 SprObject_X_Position_[N_SPROBJ];        /* $86 */
	s8 SprObject_Y_Speed_[N_MOVERS];           /* $9f */
	u8 SprObject_Y_HighPos_[N_SPROBJ];         /* $b5 */
	u8 SprObject_Y_Position_[N_SPROBJ];        /* $ce */
	const u8 *AreaData_, *EnemyData_;           /* $e7, $e9: pointers into the level data */
	u8 M_EB_, M_EC_, M_ED_, M_EF_;                /* $eb-$ef: more scratch */

	/* $0100: floatey numbers */
	u8 VerticalFlipFlag_, FlagpoleFNum_Y_Pos_, FlagpoleFNum_YMFDummy_, FlagpoleScore_;
	u8 FloateyNum_Control_[ENEMY_SLOTS + 1], FloateyNum_X_Pos_[ENEMY_SLOTS + 1];
	u8 FloateyNum_Y_Pos_[ENEMY_SLOTS + 1], ShellChainCounter_[ENEMY_SLOTS + 1];
	u8 FloateyNum_Timer_[ENEMY_SLOTS + 2];
	u8 DigitModifier_[6];

	/* $0300 */
	u8 BowserBodyControls_, BowserFeetCounter_, BowserMovementSpeed_, BowserOrigXPos_;
	u8 BowserFlameTimerCtrl_, BowserFront_Offset_, BridgeCollapseOffset_, BowserGfxFlag_;
	u8 FirebarSpinSpeed_[ENEMY_SLOTS];         /* $0388 */
	u8 VineFlagOffset_, VineHeight_, VineObjOffset_[3], VineStart_Y_Position_;
	u8 BalPlatformAlignment_, Platform_X_Scroll_;   /* $03a0 */
	u8 PlatformCollisionFlag_[ENEMY_SLOTS];      /* $03a2, by enemy (also HammerThrowingTimer) */
	u8 SprObject_Rel_XPos_[N_REL];             /* $03ad */
	u8 SprObject_Rel_YPos_[N_REL];             /* $03b8 */
	u8 SprObject_SprAttrib_[OFS_ENEMY + ENEMY_SLOTS + 5];   /* $03c4 */
	u8 SprObject_OffscrBits_[8 + ENEMY_SLOTS];  /* $03d0; $03d8 on: masked, by enemy */
	u8 Block_Orig_YPos_[2], Block_BBuf_Low_[2], Block_Metatile_[2], Block_PageLoc2_[2];
	u8 Block_RepFlag_[2], SprDataOffset_Ctrl_, Block_Orig_XPos_[2];

	/* $0400: RA_0400 and RA_0483 below, in the plane tails in the AVR build */

	/* $0500: the block buffer lives in SPI RAM (bb.c) */

	/* $06a0 */
	u8 BlockBufferColumnPos_, MetatileBuffer_[13];
	u8 HammerEnemyOffset_[MISC_SLOTS], JumpCoinMiscOffset_;
	u8 BrickCoinTimerFlag_, Misc_Collision_Flag_[MISC_SLOTS];
	u8 EnemyFrenzyBuffer_, SecondaryHardMode_, EnemyFrenzyQueue_, FireballCounter_;
	u8 DuplicateObj_Offset_, LakituReappearTimer_, NumberofGroupEnemies_;
	u8 ColorRotateOffset_, PlayerGfxOffset_, WarpZoneControl_, FireworksCounter_;
	u8 MultiLoopCorrectCntr_, MultiLoopPassCntr_, JumpspringForce_, MaxRangeFromOrigin_;
	u8 BitMFilter_, ChangeAreaTimer_;
	u8 SprShuffleAmtOffset_, SprShuffleAmt_[3]; /* $06e0 */
	u8 SprDataOffset_[0x18];                    /* $06e4-$06fb: OAM offsets, by object */
	u8 SavedJoypadBits_, Player_X_Scroll_;      /* $06fc, $06ff */

	/* $0700 */
	u8 Player_XSpeedAbsolute_, FrictionAdderHigh_, FrictionAdderLow_, RunningSpeed_;
	u8 SwimmingFlag_, Player_X_MoveForce_, DiffToHaltJump_, JumpOrigin_Y_HighPos_;
	u8 JumpOrigin_Y_Position_, VerticalForce_, VerticalForceDown_, PlayerChangeSizeFlag_;
	u8 PlayerAnimTimerSet_, PlayerAnimCtrl_, JumpspringAnimCtrl_, FlagpoleCollisionYPos_;
	u8 PlayerEntranceCtrl_, FireballThrowingTimer_, DeathMusicLoaded_, FlagpoleSoundQueue_;
	u8 CrouchingFlag_, GameTimerSetting_, DisableCollisionDet_, DemoAction_, DemoActionTimer_;
	u8 PrimaryMsgCounter_;
	u8 ScreenLeft_PageLoc__[2];                /* $071a: [0] left, [1] right */
	u8 ScreenLeft_X_Pos__[2];                  /* $071c: [0] left, [1] right */
	u8 ColumnSets_, AreaParserTaskNum_, Sprite0HitDetectFlag_, ScrollLock_;
	u8 CurrentPageLoc_, CurrentColumnPos_, TerrainControl_, BackloadingFlag_;
	u8 BehindAreaParserFlag_, AreaObjectPageLoc_, AreaObjectPageSel_, AreaDataOffset_;
	u8 AreaObjOffsetBuffer_[3], AreaObjectLength_[3], AreaStyle_, StaircaseControl_;
	u8 AreaObjectHeight_, MushroomLedgeHalfLen_[3], EnemyDataOffset_, EnemyObjectPageLoc_;
	u8 EnemyObjectPageSel_, ScreenRoutineTask_, ScrollThirtyTwo_, HorizontalScroll_;
	u8 VerticalScroll_, ForegroundScenery_, BackgroundScenery_, CloudTypeOverride_;
	u8 BackgroundColorCtrl_, LoopCommand_, StarFlagTaskControl_, TimerControl_;
	u8 CoinTallyFor1Ups_, SecondaryMsgCounter_, JoypadBitMask_;
};

/* ---------------------------------------------------------------------------------------
** $074c-$076f: cleared by InitializeGame (and a cold boot)
** ------------------------------------------------------------------------------------ */
struct RamGame {
	u8 AreaType_, AreaAddrsLOffset_, AreaPointer_, EntrancePage_, AltEntranceControl_;
	u8 CurrentPlayer_, PlayerSize_, Player_Pos_ForScroll_, PlayerStatus_;
	u8 FetchNewGameTimerFlag_, JoypadOverride_, GameTimerExpiredFlag_;
	u8 NumberofLives_, HalfwayPage_, LevelNumber_, Hidden1UpFlag_, CoinTally_;  /* $075a */
	u8 WorldNumber_, AreaNumber_;
	u8 ScrollFractional_, DisableIntermediate_, PrimaryHardMode_, WorldSelectNumber_;
};

/* ---------------------------------------------------------------------------------------
** $0770 and up: kept until a cold boot (the timers are cleared on their own)
** ------------------------------------------------------------------------------------ */
struct RamPersist {
	u8 OperMode_, OperMode_Task_, VRAM_Buffer_AddrCtrl_, DisableScreenFlag_, ScrollAmount_;
	u8 GamePauseStatus_, GamePauseTimer_, NumberOfPlayers_, IntervalTimerControl_;
	u8 Timers_[0x24];                          /* $0780 */
	u8 PseudoRandomBitReg_[7];                 /* $07a7 */
	/* sound queues (the 6502's zero page $fa-$ff and $07b1) */
	u8 PauseSoundQueue_, AreaMusicQueue_, EventMusicQueue_, NoiseSoundQueue_;
	u8 Square2SoundQueue_, Square1SoundQueue_, EventMusicBuffer_, AreaMusicBuffer_;
	/* $07d7: one digit a byte; 1 player */
	u8 TopScoreDisplay_[6], PlayerScoreDisplay_[6], CoinDisplay_[2], GameTimerDisplay_[3];
	u8 WorldSelectEnableFlag_, ContinueWorld_;
};

extern struct RamArea ra;
		#include "tails17.h"
	struct RamAreaB { RA_0400 };
	struct RamAreaC { RA_0483 };
	#define rab (*(struct RamAreaB *)TAIL_RAB)
	#define rac (*(struct RamAreaC *)TAIL_RAC)
	#define rg (*(struct RamGame *)TAIL_RG)
	#define rp (*(struct RamPersist *)TAIL_RP)
	#define RA_CLEAR() (memset(&ra, 0, sizeof ra), memset(&rab, 0, sizeof rab), memset(&rac, 0, sizeof rac))

/* ---------------------------------------------------------------------------------------
** The 6502's names
** ------------------------------------------------------------------------------------ */
#define M00 ra.M_[0]
#define M01 ra.M_[1]
#define M02 ra.M_[2]
#define M03 ra.M_[3]
#define M04 ra.M_[4]
#define M05 ra.M_[5]
#define M06 ra.M_[6]
#define M07 ra.M_[7]

#define ObjectOffset          ra.ObjectOffset_
#define FrameCounter          ra.FrameCounter_
#define A_B_Buttons           ra.A_B_Buttons_
#define Up_Down_Buttons       ra.Up_Down_Buttons_
#define Left_Right_Buttons    ra.Left_Right_Buttons_
#define PreviousA_B_Buttons   ra.PreviousA_B_Buttons_
#define GameEngineSubroutine  ra.GameEngineSubroutine_
#define Enemy_Flag            ra.Enemy_Flag_
#define Enemy_ID              ra.Enemy_ID_

#define SprObject_State       ra.SprObject_State_
#define Player_State          ra.SprObject_State_[0]
#define Enemy_State           (ra.SprObject_State_ + OFS_ENEMY)
#define Fireball_State        (ra.SprObject_State_ + OFS_FIREBALL)
#define Block_State           (ra.SprObject_State_ + OFS_BLOCK)
#define Misc_State            (ra.SprObject_State_ + OFS_MISC)

#define PlayerFacingDir       ra.PlayerFacingDir_
#define DestinationPageLoc    ra.DestinationPageLoc_
#define FirebarSpinDirection  ra.FirebarSpinDirection_
#define VictoryWalkControl    ra.VictoryWalkControl_
#define PowerUpType           ra.PowerUpType_
#define FireballBouncingFlag  ra.FireballBouncingFlag_
#define HammerBroJumpTimer    ra.HammerBroJumpTimer_

#define SprObject_MovingDir   ra.SprObject_MovingDir_
#define Player_MovingDir      ra.SprObject_MovingDir_[0]
#define Enemy_MovingDir       (ra.SprObject_MovingDir_ + OFS_ENEMY)

#define SprObject_X_Speed     ra.SprObject_X_Speed_
#define Player_X_Speed        ra.SprObject_X_Speed_[0]
#define Enemy_X_Speed         (ra.SprObject_X_Speed_ + OFS_ENEMY)
#define Fireball_X_Speed      (ra.SprObject_X_Speed_ + OFS_FIREBALL)
#define Block_X_Speed         (ra.SprObject_X_Speed_ + OFS_BLOCK)
#define Misc_X_Speed          (ra.SprObject_X_Speed_ + OFS_MISC)
/* enemy-specific uses of Enemy_X_Speed ($58): unsigned views of the same bytes */
#define Enemy_X_SpeedU        ((u8 *)(ra.SprObject_X_Speed_ + OFS_ENEMY))
#define BlooperMoveSpeed      Enemy_X_SpeedU
#define CheepCheepMoveMFlag   Enemy_X_SpeedU
#define ExplosionGfxCounter   Enemy_X_SpeedU
#define FirebarSpinState_Low  Enemy_X_SpeedU
#define Jumpspring_FixedYPos  Enemy_X_SpeedU
#define LakituMoveSpeed       Enemy_X_SpeedU
#define PiranhaPlant_Y_Speed  Enemy_X_SpeedU
#define RedPTroopaCenterYPos  Enemy_X_SpeedU
#define XMoveSecondaryCounter Enemy_X_SpeedU
#define YPlatformCenterYPos   Enemy_X_SpeedU

#define SprObject_PageLoc     ra.SprObject_PageLoc_
#define Player_PageLoc        ra.SprObject_PageLoc_[0]
#define Enemy_PageLoc         (ra.SprObject_PageLoc_ + OFS_ENEMY)
#define Fireball_PageLoc      (ra.SprObject_PageLoc_ + OFS_FIREBALL)
#define Block_PageLoc         (ra.SprObject_PageLoc_ + OFS_BLOCK)
#define Misc_PageLoc          (ra.SprObject_PageLoc_ + OFS_MISC)
#define Bubble_PageLoc        (ra.SprObject_PageLoc_ + OFS_BUBBLE)

#define SprObject_X_Position  ra.SprObject_X_Position_
#define Player_X_Position     ra.SprObject_X_Position_[0]
#define Enemy_X_Position      (ra.SprObject_X_Position_ + OFS_ENEMY)
#define Fireball_X_Position   (ra.SprObject_X_Position_ + OFS_FIREBALL)
#define Block_X_Position      (ra.SprObject_X_Position_ + OFS_BLOCK)
#define Misc_X_Position       (ra.SprObject_X_Position_ + OFS_MISC)
#define Bubble_X_Position     (ra.SprObject_X_Position_ + OFS_BUBBLE)

#define SprObject_Y_Speed     ra.SprObject_Y_Speed_
#define Player_Y_Speed        ra.SprObject_Y_Speed_[0]
#define Enemy_Y_Speed         (ra.SprObject_Y_Speed_ + OFS_ENEMY)
#define Fireball_Y_Speed      (ra.SprObject_Y_Speed_ + OFS_FIREBALL)
#define Block_Y_Speed         (ra.SprObject_Y_Speed_ + OFS_BLOCK)
#define Misc_Y_Speed          (ra.SprObject_Y_Speed_ + OFS_MISC)
#define Enemy_Y_SpeedU        ((u8 *)(ra.SprObject_Y_Speed_ + OFS_ENEMY))
#define BlooperMoveCounter    Enemy_Y_SpeedU
#define ExplosionTimerCounter Enemy_Y_SpeedU
#define FirebarSpinState_High Enemy_Y_SpeedU
#define LakituMoveDirection   Enemy_Y_SpeedU
#define PiranhaPlant_MoveFlag Enemy_Y_SpeedU
#define XMovePrimaryCounter   Enemy_Y_SpeedU

#define SprObject_Y_HighPos   ra.SprObject_Y_HighPos_
#define Player_Y_HighPos      ra.SprObject_Y_HighPos_[0]
#define Enemy_Y_HighPos       (ra.SprObject_Y_HighPos_ + OFS_ENEMY)
#define Fireball_Y_HighPos    (ra.SprObject_Y_HighPos_ + OFS_FIREBALL)
#define Block_Y_HighPos       (ra.SprObject_Y_HighPos_ + OFS_BLOCK)
#define Misc_Y_HighPos        (ra.SprObject_Y_HighPos_ + OFS_MISC)
#define Bubble_Y_HighPos      (ra.SprObject_Y_HighPos_ + OFS_BUBBLE)

#define SprObject_Y_Position  ra.SprObject_Y_Position_
#define Player_Y_Position     ra.SprObject_Y_Position_[0]
#define Enemy_Y_Position      (ra.SprObject_Y_Position_ + OFS_ENEMY)
#define Fireball_Y_Position   (ra.SprObject_Y_Position_ + OFS_FIREBALL)
#define Block_Y_Position      (ra.SprObject_Y_Position_ + OFS_BLOCK)
#define Misc_Y_Position       (ra.SprObject_Y_Position_ + OFS_MISC)
#define Bubble_Y_Position     (ra.SprObject_Y_Position_ + OFS_BUBBLE)

#define AreaData              ra.AreaData_
#define EnemyData             ra.EnemyData_
#define M_EB                  ra.M_EB_
#define M_EC                  ra.M_EC_
#define M_ED                  ra.M_ED_
#define M_EF                  ra.M_EF_

#define VerticalFlipFlag      ra.VerticalFlipFlag_
#define FlagpoleFNum_Y_Pos    ra.FlagpoleFNum_Y_Pos_
#define FlagpoleFNum_YMFDummy ra.FlagpoleFNum_YMFDummy_
#define FlagpoleScore         ra.FlagpoleScore_
#define FloateyNum_Control    ra.FloateyNum_Control_
#define FloateyNum_X_Pos      ra.FloateyNum_X_Pos_
#define FloateyNum_Y_Pos      ra.FloateyNum_Y_Pos_
#define ShellChainCounter     ra.ShellChainCounter_
#define FloateyNum_Timer      ra.FloateyNum_Timer_
#define DigitModifier         ra.DigitModifier_

#define BowserBodyControls    ra.BowserBodyControls_
#define BowserFeetCounter     ra.BowserFeetCounter_
#define BowserMovementSpeed   ra.BowserMovementSpeed_
#define BowserOrigXPos        ra.BowserOrigXPos_
#define BowserFlameTimerCtrl  ra.BowserFlameTimerCtrl_
#define BowserFront_Offset    ra.BowserFront_Offset_
#define BridgeCollapseOffset  ra.BridgeCollapseOffset_
#define BowserGfxFlag         ra.BowserGfxFlag_
#define FirebarSpinSpeed      ra.FirebarSpinSpeed_
#define VineFlagOffset        ra.VineFlagOffset_
#define VineHeight            ra.VineHeight_
#define VineObjOffset         ra.VineObjOffset_
#define VineStart_Y_Position  ra.VineStart_Y_Position_
#define BalPlatformAlignment  ra.BalPlatformAlignment_
#define Platform_X_Scroll     ra.Platform_X_Scroll_
#define PlatformCollisionFlag ra.PlatformCollisionFlag_
#define HammerThrowingTimer   ra.PlatformCollisionFlag_    /* $03a2, shared */

#define SprObject_Rel_XPos    ra.SprObject_Rel_XPos_
#define Player_Rel_XPos       ra.SprObject_Rel_XPos_[REL_PLAYER]
#define Enemy_Rel_XPos        ra.SprObject_Rel_XPos_[REL_ENEMY]
#define Fireball_Rel_XPos     (ra.SprObject_Rel_XPos_ + REL_FBALL)
#define Bubble_Rel_XPos       ra.SprObject_Rel_XPos_[REL_BUBBLE]
#define Block_Rel_XPos        (ra.SprObject_Rel_XPos_ + REL_BLOCK)
#define Misc_Rel_XPos         ra.SprObject_Rel_XPos_[REL_MISC]
#define SprObject_Rel_YPos    ra.SprObject_Rel_YPos_
#define Player_Rel_YPos       ra.SprObject_Rel_YPos_[REL_PLAYER]
#define Enemy_Rel_YPos        ra.SprObject_Rel_YPos_[REL_ENEMY]
#define Fireball_Rel_YPos     (ra.SprObject_Rel_YPos_ + REL_FBALL)
#define Bubble_Rel_YPos       ra.SprObject_Rel_YPos_[REL_BUBBLE]
#define Block_Rel_YPos        (ra.SprObject_Rel_YPos_ + REL_BLOCK)
#define Misc_Rel_YPos         ra.SprObject_Rel_YPos_[REL_MISC]

#define SprObject_SprAttrib   ra.SprObject_SprAttrib_
#define Player_SprAttrib      ra.SprObject_SprAttrib_[0]
#define Enemy_SprAttrib       (ra.SprObject_SprAttrib_ + OFS_ENEMY)

#define SprObject_OffscrBits  ra.SprObject_OffscrBits_
#define Player_OffscreenBits  ra.SprObject_OffscrBits_[REL_PLAYER]
#define Enemy_OffscreenBits   ra.SprObject_OffscrBits_[REL_ENEMY]
#define FBall_OffscreenBits   ra.SprObject_OffscrBits_[REL_FBALL]
#define Bubble_OffscreenBits  ra.SprObject_OffscrBits_[REL_BUBBLE]
#define Block_OffscreenBits   (ra.SprObject_OffscrBits_ + REL_BLOCK)
#define Misc_OffscreenBits    ra.SprObject_OffscrBits_[REL_MISC]
#define EnemyOffscrBitsMasked (ra.SprObject_OffscrBits_ + 8)

#define Block_Orig_YPos       ra.Block_Orig_YPos_
#define Block_BBuf_Low        ra.Block_BBuf_Low_
#define Block_Metatile        ra.Block_Metatile_
#define Block_PageLoc2        ra.Block_PageLoc2_
#define Block_RepFlag         ra.Block_RepFlag_
#define SprDataOffset_Ctrl    ra.SprDataOffset_Ctrl_
#define Block_Orig_XPos       ra.Block_Orig_XPos_

#define SprObject_X_MoveForce rab.SprObject_X_MoveForce_
#define Enemy_X_MoveForce     (rab.SprObject_X_MoveForce_ + OFS_ENEMY)
#define RedPTroopaOrigXPos    Enemy_X_MoveForce
#define YPlatformTopYPos      Enemy_X_MoveForce
#define SprObject_YMF_Dummy   rab.SprObject_YMF_Dummy_
#define Player_YMF_Dummy      rab.SprObject_YMF_Dummy_[0]
#define Enemy_YMF_Dummy       (rab.SprObject_YMF_Dummy_ + OFS_ENEMY)
#define Bubble_YMF_Dummy      (rab.SprObject_YMF_Dummy_ + OFS_BUBBLE)
#define BowserFlamePRandomOfs Enemy_YMF_Dummy
#define PiranhaPlantUpYPos    Enemy_YMF_Dummy
#define SprObject_Y_MoveForce rab.SprObject_Y_MoveForce_
#define Player_Y_MoveForce    rab.SprObject_Y_MoveForce_[0]
#define Enemy_Y_MoveForce     (rab.SprObject_Y_MoveForce_ + OFS_ENEMY)
#define Block_Y_MoveForce     (rab.SprObject_Y_MoveForce_ + OFS_BLOCK)
#define CheepCheepOrigYPos    Enemy_Y_MoveForce
#define PiranhaPlantDownYPos  Enemy_Y_MoveForce
#define MaximumLeftSpeed      rab.MaximumLeftSpeed_
#define MaximumRightSpeed     rab.MaximumRightSpeed_

#define Cannon_Offset         rab.Cannon_Offset_
#define Cannon_PageLoc        rab.Cannon_PageLoc_
#define Cannon_X_Position     rab.Cannon_X_Position_
#define Cannon_Y_Position     rab.Cannon_Y_Position_
#define Cannon_Timer          rab.Cannon_Timer_
#define Whirlpool_Offset      rab.Cannon_Offset_
#define Whirlpool_PageLoc     rab.Cannon_PageLoc_
#define Whirlpool_LeftExtent  rab.Cannon_X_Position_
#define Whirlpool_Length      rab.Cannon_Y_Position_
#define Whirlpool_Flag        rab.Cannon_Timer_[0]
#define BowserHitPoints       rac.BowserHitPoints_
#define StompChainCounter     rac.StompChainCounter_

#define SprObject_CollisionBits rac.SprObject_CollisionBits_
#define Player_CollisionBits  rac.SprObject_CollisionBits_[0]
#define Enemy_CollisionBits   (rac.SprObject_CollisionBits_ + OFS_ENEMY)
#define SprObj_BoundBoxCtrl   rac.SprObj_BoundBoxCtrl_
#define Player_BoundBoxCtrl   rac.SprObj_BoundBoxCtrl_[0]
#define Enemy_BoundBoxCtrl    (rac.SprObj_BoundBoxCtrl_ + OFS_ENEMY)
#define Fireball_BoundBoxCtrl (rac.SprObj_BoundBoxCtrl_ + BB_FIREBALL)
#define Misc_BoundBoxCtrl     (rac.SprObj_BoundBoxCtrl_ + BB_MISC)
#define BoundingBox_UL_Corner rac.BoundingBox_UL_Corner_
#define BoundingBox_UL_XPos   rac.BoundingBox_UL_Corner_
#define BoundingBox_UL_YPos   (rac.BoundingBox_UL_Corner_ + 1)
#define BoundingBox_DR_XPos   (rac.BoundingBox_UL_Corner_ + 2)
#define BoundingBox_LR_Corner (rac.BoundingBox_UL_Corner_ + 2)
#define BoundingBox_DR_YPos   (rac.BoundingBox_UL_Corner_ + 3)
#define EnemyBoundingBoxCoord (rac.BoundingBox_UL_Corner_ + 4)

#define BlockBufferColumnPos  ra.BlockBufferColumnPos_
#define MetatileBuffer        ra.MetatileBuffer_
#define HammerEnemyOffset     ra.HammerEnemyOffset_
#define JumpCoinMiscOffset    ra.JumpCoinMiscOffset_
#define BrickCoinTimerFlag    ra.BrickCoinTimerFlag_
#define Misc_Collision_Flag   ra.Misc_Collision_Flag_
#define EnemyFrenzyBuffer     ra.EnemyFrenzyBuffer_
#define SecondaryHardMode     ra.SecondaryHardMode_
#define EnemyFrenzyQueue      ra.EnemyFrenzyQueue_
#define FireballCounter       ra.FireballCounter_
#define DuplicateObj_Offset   ra.DuplicateObj_Offset_
#define LakituReappearTimer   ra.LakituReappearTimer_
#define NumberofGroupEnemies  ra.NumberofGroupEnemies_
#define ColorRotateOffset     ra.ColorRotateOffset_
#define PlayerGfxOffset       ra.PlayerGfxOffset_
#define WarpZoneControl       ra.WarpZoneControl_
#define FireworksCounter      ra.FireworksCounter_
#define MultiLoopCorrectCntr  ra.MultiLoopCorrectCntr_
#define MultiLoopPassCntr     ra.MultiLoopPassCntr_
#define JumpspringForce       ra.JumpspringForce_
#define MaxRangeFromOrigin    ra.MaxRangeFromOrigin_
#define BitMFilter            ra.BitMFilter_
#define ChangeAreaTimer       ra.ChangeAreaTimer_
#define SprShuffleAmtOffset   ra.SprShuffleAmtOffset_
#define SprShuffleAmt         ra.SprShuffleAmt_
#define SprDataOffset         ra.SprDataOffset_
#define Player_SprDataOffset  ra.SprDataOffset_[0x00]
#define Enemy_SprDataOffset   (ra.SprDataOffset_ + 0x01)
#define Alt_SprDataOffset     (ra.SprDataOffset_ + 0x08)
#define Block_SprDataOffset   (ra.SprDataOffset_ + 0x08)
#define Bubble_SprDataOffset  (ra.SprDataOffset_ + 0x0a)
#define FBall_SprDataOffset   (ra.SprDataOffset_ + 0x0d)
#define Misc_SprDataOffset    (ra.SprDataOffset_ + 0x0f)
#define SavedJoypadBits       ra.SavedJoypadBits_
#define SavedJoypad1Bits      ra.SavedJoypadBits_
#define Player_X_Scroll       ra.Player_X_Scroll_

#define Player_XSpeedAbsolute ra.Player_XSpeedAbsolute_
#define FrictionAdderHigh     ra.FrictionAdderHigh_
#define FrictionAdderLow      ra.FrictionAdderLow_
#define RunningSpeed          ra.RunningSpeed_
#define SwimmingFlag          ra.SwimmingFlag_
#define Player_X_MoveForce    ra.Player_X_MoveForce_
#define DiffToHaltJump        ra.DiffToHaltJump_
#define JumpOrigin_Y_HighPos  ra.JumpOrigin_Y_HighPos_
#define JumpOrigin_Y_Position ra.JumpOrigin_Y_Position_
#define VerticalForce         ra.VerticalForce_
#define VerticalForceDown     ra.VerticalForceDown_
#define PlayerChangeSizeFlag  ra.PlayerChangeSizeFlag_
#define PlayerAnimTimerSet    ra.PlayerAnimTimerSet_
#define PlayerAnimCtrl        ra.PlayerAnimCtrl_
#define JumpspringAnimCtrl    ra.JumpspringAnimCtrl_
#define FlagpoleCollisionYPos ra.FlagpoleCollisionYPos_
#define PlayerEntranceCtrl    ra.PlayerEntranceCtrl_
#define FireballThrowingTimer ra.FireballThrowingTimer_
#define DeathMusicLoaded      ra.DeathMusicLoaded_
#define FlagpoleSoundQueue    ra.FlagpoleSoundQueue_
#define CrouchingFlag         ra.CrouchingFlag_
#define GameTimerSetting      ra.GameTimerSetting_
#define DisableCollisionDet   ra.DisableCollisionDet_
#define DemoAction            ra.DemoAction_
#define DemoActionTimer       ra.DemoActionTimer_
#define PrimaryMsgCounter     ra.PrimaryMsgCounter_
#define ScreenEdge_PageLoc    ra.ScreenLeft_PageLoc__
#define ScreenLeft_PageLoc    ra.ScreenLeft_PageLoc__[0]
#define ScreenRight_PageLoc   ra.ScreenLeft_PageLoc__[1]
#define ScreenEdge_X_Pos      ra.ScreenLeft_X_Pos__
#define ScreenLeft_X_Pos      ra.ScreenLeft_X_Pos__[0]
#define ScreenRight_X_Pos     ra.ScreenLeft_X_Pos__[1]
#define ColumnSets            ra.ColumnSets_
#define AreaParserTaskNum     ra.AreaParserTaskNum_
#define Sprite0HitDetectFlag  ra.Sprite0HitDetectFlag_
#define ScrollLock            ra.ScrollLock_
#define CurrentPageLoc        ra.CurrentPageLoc_
#define CurrentColumnPos      ra.CurrentColumnPos_
#define TerrainControl        ra.TerrainControl_
#define BackloadingFlag       ra.BackloadingFlag_
#define BehindAreaParserFlag  ra.BehindAreaParserFlag_
#define AreaObjectPageLoc     ra.AreaObjectPageLoc_
#define AreaObjectPageSel     ra.AreaObjectPageSel_
#define AreaDataOffset        ra.AreaDataOffset_
#define AreaObjOffsetBuffer   ra.AreaObjOffsetBuffer_
#define AreaObjectLength      ra.AreaObjectLength_
#define AreaStyle             ra.AreaStyle_
#define StaircaseControl      ra.StaircaseControl_
#define AreaObjectHeight      ra.AreaObjectHeight_
#define MushroomLedgeHalfLen  ra.MushroomLedgeHalfLen_
#define EnemyDataOffset       ra.EnemyDataOffset_
#define EnemyObjectPageLoc    ra.EnemyObjectPageLoc_
#define EnemyObjectPageSel    ra.EnemyObjectPageSel_
#define ScreenRoutineTask     ra.ScreenRoutineTask_
#define ScrollThirtyTwo       ra.ScrollThirtyTwo_
#define HorizontalScroll      ra.HorizontalScroll_
#define VerticalScroll        ra.VerticalScroll_
#define ForegroundScenery     ra.ForegroundScenery_
#define BackgroundScenery     ra.BackgroundScenery_
#define CloudTypeOverride     ra.CloudTypeOverride_
#define BackgroundColorCtrl   ra.BackgroundColorCtrl_
#define LoopCommand           ra.LoopCommand_
#define StarFlagTaskControl   ra.StarFlagTaskControl_
#define TimerControl          ra.TimerControl_
#define CoinTallyFor1Ups      ra.CoinTallyFor1Ups_
#define SecondaryMsgCounter   ra.SecondaryMsgCounter_
#define JoypadBitMask         ra.JoypadBitMask_

#define AreaType              rg.AreaType_
#define AreaAddrsLOffset      rg.AreaAddrsLOffset_
#define AreaPointer           rg.AreaPointer_
#define EntrancePage          rg.EntrancePage_
#define AltEntranceControl    rg.AltEntranceControl_
#define CurrentPlayer         rg.CurrentPlayer_
#define PlayerSize            rg.PlayerSize_
#define Player_Pos_ForScroll  rg.Player_Pos_ForScroll_
#define PlayerStatus          rg.PlayerStatus_
#define FetchNewGameTimerFlag rg.FetchNewGameTimerFlag_
#define JoypadOverride        rg.JoypadOverride_
#define GameTimerExpiredFlag  rg.GameTimerExpiredFlag_
#define NumberofLives         rg.NumberofLives_
#define HalfwayPage           rg.HalfwayPage_
#define LevelNumber           rg.LevelNumber_
#define Hidden1UpFlag         rg.Hidden1UpFlag_
#define CoinTally             rg.CoinTally_
#define WorldNumber           rg.WorldNumber_
#define AreaNumber            rg.AreaNumber_
#define ScrollFractional      rg.ScrollFractional_
#define DisableIntermediate   rg.DisableIntermediate_
#define PrimaryHardMode       rg.PrimaryHardMode_
#define WorldSelectNumber     rg.WorldSelectNumber_

#define OperMode              rp.OperMode_
#define OperMode_Task         rp.OperMode_Task_
#define VRAM_Buffer_AddrCtrl  rp.VRAM_Buffer_AddrCtrl_
#define DisableScreenFlag     rp.DisableScreenFlag_
#define ScrollAmount          rp.ScrollAmount_
#define GamePauseStatus       rp.GamePauseStatus_
#define GamePauseTimer        rp.GamePauseTimer_
#define NumberOfPlayers       rp.NumberOfPlayers_
#define IntervalTimerControl  rp.IntervalTimerControl_
#define Timers                rp.Timers_
#define SelectTimer           rp.Timers_[0x00]
#define PlayerAnimTimer       rp.Timers_[0x01]
#define JumpSwimTimer         rp.Timers_[0x02]
#define RunningTimer          rp.Timers_[0x03]
#define BlockBounceTimer      rp.Timers_[0x04]
#define SideCollisionTimer    rp.Timers_[0x05]
#define JumpspringTimer       rp.Timers_[0x06]
#define GameTimerCtrlTimer    rp.Timers_[0x07]
#define ClimbSideTimer        rp.Timers_[0x09]
#define EnemyFrameTimer       (rp.Timers_ + 0x0a)
#define FrenzyEnemyTimer      rp.Timers_[0x0f]
#define BowserFireBreathTimer rp.Timers_[0x10]
#define StompTimer            rp.Timers_[0x11]
#define AirBubbleTimer        rp.Timers_[0x12]
#define ScrollIntervalTimer   rp.Timers_[0x15]
#define EnemyIntervalTimer    (rp.Timers_ + 0x16)
#define BrickCoinTimer        rp.Timers_[0x1d]
#define InjuryTimer           rp.Timers_[0x1e]
#define StarInvincibleTimer   rp.Timers_[0x1f]
#define ScreenTimer           rp.Timers_[0x20]
#define WorldEndTimer         rp.Timers_[0x21]
#define DemoTimer             rp.Timers_[0x22]
#define PseudoRandomBitReg    rp.PseudoRandomBitReg_
#define PauseSoundQueue       rp.PauseSoundQueue_
#define AreaMusicQueue        rp.AreaMusicQueue_
#define EventMusicQueue       rp.EventMusicQueue_
#define NoiseSoundQueue       rp.NoiseSoundQueue_
#define Square2SoundQueue     rp.Square2SoundQueue_
#define Square1SoundQueue     rp.Square1SoundQueue_
#define EventMusicBuffer      rp.EventMusicBuffer_
#define AreaMusicBuffer       rp.AreaMusicBuffer_
#define TopScoreDisplay       rp.TopScoreDisplay_
#define PlayerScoreDisplay    rp.PlayerScoreDisplay_
#define CoinDisplay           rp.CoinDisplay_
#define GameTimerDisplay      rp.GameTimerDisplay_
#define WorldSelectEnableFlag rp.WorldSelectEnableFlag_
#define ContinueWorld         rp.ContinueWorld_

/* ---------------------------------------------------------------------------------------
** Constants of the original
** ------------------------------------------------------------------------------------ */
enum {
	A_Button = 0x80, B_Button = 0x40, Select_Button = 0x20, Start_Button = 0x10,
	Up_Dir = 0x08, Down_Dir = 0x04, Left_Dir = 0x02, Right_Dir = 0x01
};

enum {  /* Square1SoundQueue */
	Sfx_SmallJump = 0x80, Sfx_Flagpole = 0x40, Sfx_Fireball = 0x20, Sfx_PipeDown_Injury = 0x10,
	Sfx_EnemySmack = 0x08, Sfx_EnemyStomp = 0x04, Sfx_Bump = 0x02, Sfx_BigJump = 0x01
};
enum {  /* Square2SoundQueue */
	Sfx_BowserFall = 0x80, Sfx_ExtraLife = 0x40, Sfx_PowerUpGrab = 0x20, Sfx_TimerTick = 0x10,
	Sfx_Blast = 0x08, Sfx_GrowVine = 0x04, Sfx_GrowPowerUp = 0x02, Sfx_CoinGrab = 0x01
};
enum {  /* NoiseSoundQueue */
	Sfx_BowserFlame = 0x02, Sfx_BrickShatter = 0x01
};
enum {  /* AreaMusicQueue / EventMusicQueue */
	Silence = 0x80,
	StarPowerMusic = 0x40, PipeIntroMusic = 0x20, CloudMusic = 0x10, CastleMusic = 0x08,
	UndergroundMusic = 0x04, WaterMusic = 0x02, GroundMusic = 0x01,
	TimeRunningOutMusic = 0x40, EndOfLevelMusic = 0x20, AltGameOverMusic = 0x10,
	EndOfCastleMusic = 0x08, VictoryMusic = 0x04, GameOverMusic = 0x02, DeathMusic = 0x01
};
enum {  /* enemy object identifiers */
	GreenKoopa = 0x00, BuzzyBeetle = 0x02, RedKoopa = 0x03, HammerBro = 0x05, Goomba = 0x06,
	Bloober = 0x07, BulletBill_FrenzyVar = 0x08, TallEnemy = 0x09, GreyCheepCheep = 0x0a,
	RedCheepCheep = 0x0b, Podoboo = 0x0c, PiranhaPlant = 0x0d, GreenParatroopaJump = 0x0e,
	RedParatroopa = 0x0f, GreenParatroopaFly = 0x10, Lakitu = 0x11, Spiny = 0x12,
	FlyCheepCheepFrenzy = 0x14, FlyingCheepCheep = 0x14, BowserFlame = 0x15, Fireworks = 0x16,
	BBill_CCheep_Frenzy = 0x17, Stop_Frenzy = 0x18, Bowser = 0x2d, PowerUpObject = 0x2e,
	VineObject = 0x2f, FlagpoleFlagObject = 0x30, StarFlagObject = 0x31,
	JumpspringObject = 0x32, BulletBill_CannonVar = 0x33, RetainerObject = 0x35
};
enum { World1, World2, World3, World4, World5, World6, World7, World8 };
enum { Level1, Level2, Level3, Level4 };
enum { TitleScreenModeValue, GameModeValue, VictoryModeValue, GameOverModeValue };
