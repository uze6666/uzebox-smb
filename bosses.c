/*
 * Bosses, special objects and platforms: a port of smbdis.asm lines 10067-11050 (Bowser and
 * the bridge, the flame's movement, fireworks and the star flag, the piranha plant, the firebar's
 * spin, the balance, moving, dropping and lift platforms, the offscreen bounds check).
 * Routine names are the 6502's; registers and flags are A_ X_ Y_ C_ Z_ N_ (smb.h).
 * What drew on the NES is gfx17.c's: DrawBowserFlame, DrawEraseRopeHook, DrawStarFlag.
 */
#include <avr/pgmspace.h>
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

/* FlameYPosData (asm line 8548): the flame's target heights, in enemies.c */
extern const u8 FlameYPosData[4] PROGMEM;

#define RD(t, i) pgm_read_byte(&(t)[i])
/* inlined or kept out of line as measured smallest (a call boundary costs reloads and saves) */
#define INLINE static inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
/* An object's fields through one base pointer, reached with ldd/std and a displacement:
** b = opq(&B[i]) (the asm hides its origin, or gcc folds it back into each address), then
** E(b, B, f) is f[i], for f an array of the same RAM struct 0-63 bytes past B (checked) */
static inline u8 *opq(u8 *p){
	__asm__("" : "+b"(p));
	return p;
}
#define EOFS(B, f) ((__UINTPTR_TYPE__)(f) - (__UINTPTR_TYPE__)(B))
#define E(b, B, f) (*({ _Static_assert(__builtin_constant_p(EOFS(B, f)) && EOFS(B, f) < 64, 	"E: B and f in one struct, f 0-63 bytes past B"); (__typeof__(&(f)[0]))((b) + EOFS(B, f)); }))

/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
/* low bytes of the name table addresses: the axe, the chain, then the bridge */
static const u8 BridgeCollapseData[15] PROGMEM = { ROM_BridgeCollapseData };
static const u8 PRandomRange[4] PROGMEM = { ROM_PRandomRange };
static const u8 FlameTimerData[8] PROGMEM = { ROM_FlameTimerData };
const u8 StarFlagYPosAdder[4] PROGMEM = { ROM_StarFlagYPosAdder };
const u8 StarFlagXPosAdder[4] PROGMEM = { ROM_StarFlagXPosAdder };
const u8 StarFlagTileData[4] PROGMEM = { ROM_StarFlagTileData };

u8 RopeVRAMData[11];

/* ---------------------------------------------------------------------------------------
** Bowser and the bridge
** ------------------------------------------------------------------------------------ */
INLINE void BowserControl(u8 x);
INLINE void HammerChk_(void);
INLINE void ChkFireB_(void);
static void BowserGfxHandler_(void);
static void ProcessBowserHalf_(void);
static void EndAreaPoints_(void);
INLINE void GameTimerFireworks_(void);
INLINE void AwardGameTimerPoints_(void);
INLINE void RaiseFlagSetoffFWorks_(void);
INLINE void DelayToAreaEnd_(void);
static void ChkYPCollision_(void);
static void PositionPlayerOnHPlat_(void);
static void MoveLiftPlatforms_(void);
INLINE void ChkSmallPlatCollision_(void);

/* PRandomRange by the enemy's pseudo-random bits */
static u8 prandom_range(u8 x){
	return RD(PRandomRange, PseudoRandomBitReg[x] & 3);
}

/* MoveD_Bowser: the defeated Bowser sinks (X_ the enemy) */
INLINE void MoveD_Bowser(void){
	MoveEnemySlowVert();
	BowserGfxHandler_();
}

/* RemoveBridge: Bowser in its normal state, the bridge goes a column every 4 frames */
INLINE void RemoveBridge(u8 x){
	if(!--BowserFeetCounter){
		BowserFeetCounter = 4;
		BowserBodyControls ^= 1;
		M05 = 0x22;                                    /* name table address high byte */
		M04 = RD(BridgeCollapseData, BridgeCollapseOffset);
		X_ = 0x0c;                                     /* the blank metatile's tiles */
		RemBridge();
		X_ = ObjectOffset;
		MoveVOffset();
		Square2SoundQueue = Sfx_Blast;
		NoiseSoundQueue = Sfx_BrickShatter;
		if(++BridgeCollapseOffset == 0x0f){
			X_ = x;
			InitVStf();
			Enemy_State[x] = 0x40;                     /* a defeated state, d6 set */
			Square2SoundQueue = Sfx_BowserFall;
		}
	}
	X_ = x;
	BowserGfxHandler_();
}

/* BridgeCollapse: the victory mode's first task, in the castle */
void BridgeCollapse(void){
	u8 x = BowserFront_Offset;
	if(Enemy_ID[x] == Bowser){
		ObjectOffset = x;
		u8 s = Enemy_State[x];
		if(!s){
			RemoveBridge(x);
			return;
		}
		if((s & 0x40) && Enemy_Y_Position[x] < 0xe0){
			X_ = x;
			MoveD_Bowser();
			return;
		}
	}
	EventMusicQueue = Silence;                         /* SetM2 */
	OperMode_Task++;
	KillAllEnemies();
}

/* RunBowser: Bowser's enemy routine */
void RunBowser(void){
	u8 x = X_;
	if(!(Enemy_State[x] & 0x20)) BowserControl(x);
	else if(Enemy_Y_Position[x] < 0xe0) MoveD_Bowser();
	else KillAllEnemies();
}

/* KillAllEnemies: empties enemy slots 0-4 and the frenzy buffer */
void KillAllEnemies(void){
	X_ = 4;
	do EraseEnemyObject(); while((s8)--X_ >= 0);       /* EraseEnemyObject keeps X */
	EnemyFrenzyBuffer = 0;
	A_ = 0;
	X_ = ObjectOffset;
}

/* BowserControl: Bowser's feet, walking back and forth, then the jumps and the fire */
INLINE void BowserControl(u8 x){
	EnemyFrenzyBuffer = 0;
	if(TimerControl){
		ChkFireB_();                                    /* SkipToFB */
		return;
	}
	if(!(BowserBodyControls & 0x80)){                  /* mouth closed */
		if(!--BowserFeetCounter){                      /* FeetTmr */
			BowserFeetCounter = 0x20;
			BowserBodyControls ^= 1;
		}
		u8 *md = &Enemy_MovingDir[x], *xp = &Enemy_X_Position[x];
		if(!(FrameCounter & 0x0f)) *md = 2;            /* ResetMDr */
		if(EnemyFrameTimer[x]){                        /* B_FaceP */
			PlayerEnemyDiff();
			if(N_){                                    /* Bowser left of the player */
				*md = 1;
				BowserMovementSpeed = 2;
				EnemyFrameTimer[x] = 0x20;
				BowserFireBreathTimer = 0x20;
				if(*xp >= 0xc8) goto Hammer;
			}
		}
		if(!(FrameCounter & 3)){                       /* GetPRCmp */
			u8 a = *xp;
			if(a == BowserOrigXPos) MaxRangeFromOrigin = prandom_range(x);
			a += BowserMovementSpeed;                  /* GetDToO */
			*xp = a;
			if(*md != 1){
				u8 y = 0xff;
				a -= BowserOrigXPos;
				if(a & 0x80){
					a = -a;
					y = 1;
				}
				if(a >= MaxRangeFromOrigin) BowserMovementSpeed = y;   /* CompDToO */
			}
		}
Hammer:
		X_ = x;
	}
	HammerChk_();
}

/* HammerChk: Bowser on the floor moves down, throws hammers (world 6+), times a jump */
INLINE void HammerChk_(void){
	u8 x = X_;
	u8 t = EnemyFrameTimer[x];
	if(t){
		if(t == 1){                                    /* MakeBJump */
			Enemy_Y_Position[x]--;
			InitVStf();
			Enemy_Y_Speed[x] = -2;
		}
	}else{
		MoveEnemySlowVert();
		x = X_;
		if(WorldNumber >= World6 && !(FrameCounter & 3)){
			SpawnHammerObj();
			x = X_;
		}
		if(Enemy_Y_Position[x] >= 0x80)                /* SetHmrTmr */
			EnemyFrameTimer[x] = prandom_range(x);
	}
	X_ = x;
	ChkFireB_();                                        /* SkipToFB */
}

/* the next of the eight flame timings */
static u8 flame_timer(void){
	u8 y = BowserFlameTimerCtrl;
	BowserFlameTimerCtrl = (y + 1) & 0x07;
	Y_ = y;
	return RD(FlameTimerData, y);
}

/* ChkFireB: in worlds 1-5 and 8, opens and closes the mouth and breathes fire */
INLINE void ChkFireB_(void){
	u8 w = WorldNumber;
	if((w == World8 || w < World6) && !BowserFireBreathTimer){   /* SpawnFBr */
		BowserFireBreathTimer = 0x20;
		if(!((BowserBodyControls ^= 0x80) & 0x80)){    /* mouth closed: the flame */
			u8 a = flame_timer();                      /* SetFlameTimer: A dead (GetEnemyOffscreenBits next) */
			if(SecondaryHardMode) a -= 0x10;
			BowserFireBreathTimer = a;                 /* SetFBTmr */
			EnemyFrenzyBuffer = BowserFlame;
		}
	}
	BowserGfxHandler_();
}

/* BowserGfxHandler: processes Bowser's front, then copies it to the rear and processes that */
static void BowserGfxHandler_(void){
	ProcessBowserHalf_();
	u8 x = X_, y = DuplicateObj_Offset;
	Enemy_X_Position[y] = ((Enemy_MovingDir[x] & 1) ? 0xf0 : 0x10) + Enemy_X_Position[x];   /* CopyFToR */
	Enemy_Y_Position[y] = Enemy_Y_Position[x] + 8;
	Enemy_State[y] = Enemy_State[x];
	Enemy_MovingDir[y] = Enemy_MovingDir[x];
	u8 saved = ObjectOffset;                           /* pha */
	ObjectOffset = y;
	Enemy_ID[y] = Bowser;
	X_ = y;
	ProcessBowserHalf_();
	ObjectOffset = saved;                              /* pla */
	X_ = saved;
	BowserGfxFlag = 0;
}

/* ProcessBowserHalf: draws one half, then its bounding box and player collision */
static void ProcessBowserHalf_(void){
	BowserGfxFlag++;
	RunRetainerObj();
	u8 x = X_;
	if(Enemy_State[x]) return;
	Enemy_BoundBoxCtrl[x] = 0x0a;
	GetEnemyBoundBox();
	PlayerEnemyCollision();
}

/* SetFlameTimer: the next of the eight flame timings in A_ */
void SetFlameTimer(void){
	A_ = flame_timer();
}

/* ProcBowserFlame: moves the flame left and toward its height, then draws it */
void ProcBowserFlame(void){
	u8 x = X_;
	if(!TimerControl){
		u8 f = SecondaryHardMode ? 0x60 : 0x40;
		M00 = f;
		u8 m = Enemy_X_MoveForce[x];
		Enemy_X_MoveForce[x] = m - f;
		u16 p = ((u16)Enemy_PageLoc[x] << 8 | Enemy_X_Position[x]) - 1 - (m < f);
		Enemy_X_Position[x] = p;
		Enemy_PageLoc[x] = p >> 8;
		u8 a = Enemy_Y_Position[x];
		if(a != RD(FlameYPosData, BowserFlamePRandomOfs[x])) Enemy_Y_Position[x] = a + Enemy_Y_MoveForce[x];
	}
	X_ = x;                                            /* SetGfxF */
	RelativeEnemyPosition();
	if(Enemy_State[X_]) return;
	GetEnemyOffscreenBits();                           /* the 6502 did it after drawing */
	M00 = 0x51;
	M01 = (FrameCounter & 0x02) ? 0x82 : 0x02;          /* vertical flip every 2 frames */
	X_ = ObjectOffset;
	DrawBowserFlame();
	X_ = ObjectOffset;
}

/* ---------------------------------------------------------------------------------------
** Fireworks and the star flag
** ------------------------------------------------------------------------------------ */

/* RunFireworks: an explosion's three frames, then its points */
void RunFireworks(void){
	u8 x = X_;
	if(!--ExplosionTimerCounter[x]){
		ExplosionTimerCounter[x] = 8;
		if(++ExplosionGfxCounter[x] >= 3){
			Enemy_Flag[x] = 0;                         /* FireworksSoundScore */
			Square2SoundQueue = Sfx_Blast;
			DigitModifier[4] = 5;                      /* 500 points */
			EndAreaPoints_();
			return;
		}
	}
	RelativeEnemyPosition();                           /* SetupExpl */
	x = X_;
	Fireball_Rel_YPos[0] = Enemy_Rel_YPos;
	Fireball_Rel_XPos[0] = Enemy_Rel_XPos;
	Y_ = Enemy_SprDataOffset[x];                       /* ldy Enemy_SprDataOffset,x */
	A_ = ExplosionGfxCounter[x];
	DrawExplosion_Fireworks();
}

/* RunStarFlagObj: the star flag's tasks at the castle */
void RunStarFlagObj(void){
	EnemyFrenzyBuffer = 0;
	switch(StarFlagTaskControl){
	case 1: GameTimerFireworks_(); break;
	case 2: AwardGameTimerPoints_(); break;
	case 3: RaiseFlagSetoffFWorks_(); break;
	case 4: DelayToAreaEnd_(); break;
	default: break;                                    /* 0, 5 and up: StarFlagExit */
	}
}

/* GameTimerFireworks: the timer's last digit 1, 3 or 6 gives that many fireworks */
INLINE void GameTimerFireworks_(void){
	u8 a = GameTimerDisplay[2], y = 5;
	if(a != 1){
		y = 3;
		if(a != 3){
			y = 0;
			if(a != 6) a = 0xff;                       /* no fireworks */
		}
	}
	FireworksCounter = a;                              /* SetFWC */
	Enemy_State[X_] = y;
	StarFlagTaskControl++;                             /* IncrementSFTask1 */
}

/* AwardGameTimerPoints: 50 points a timer interval, until the timer is empty */
INLINE void AwardGameTimerPoints_(void){
	if(!(GameTimerDisplay[0] | GameTimerDisplay[1] | GameTimerDisplay[2])){
		StarFlagTaskControl++;                         /* IncrementSFTask1 */
		return;
	}
	if(FrameCounter & 0x04) Square2SoundQueue = Sfx_TimerTick;
	Y_ = 0x23;                                         /* the timer's last digit */
	DigitModifier[5] = 0xff;
	DigitsMathRoutine();
	DigitModifier[5] = 5;
	EndAreaPoints_();
}

/* EndAreaPoints: adds DigitModifier to the score, prints the score and the timer */
static void EndAreaPoints_(void){
#if TWO_PLAYERS
	u8 p = 0;                                          /* (the live score is the player's whose turn it is: smb.h) */
#else
	u8 p = CurrentPlayer;
#endif
	Y_ = p ? 0x11 : 0x0b;                              /* luigi's or mario's score */
	DigitsMathRoutine();
	A_ = (p << 4) | 0x04;
	UpdateNumber();
}

/* RaiseFlagSetoffFWorks: raises the flag, then queues the fireworks */
INLINE void RaiseFlagSetoffFWorks_(void){
	u8 x = X_;
	if(Enemy_Y_Position[x] >= 0x72) Enemy_Y_Position[x]--;
	else{
		u8 f = FireworksCounter;                       /* SetoffF */
		if(!f || (f & 0x80)){
			DrawStarFlag();                            /* DrawFlagSetTimer */
			EnemyIntervalTimer[ObjectOffset] = 6;      /* DrawStarFlag leaves X ObjectOffset */
			StarFlagTaskControl++;                     /* IncrementSFTask2 */
			return;
		}
		if(gfx_FireworksReady()) EnemyFrenzyBuffer = Fireworks;   /* not the 6502's: the view first */
	}
	DrawStarFlag();
}

/* DelayToAreaEnd: waits for the timer and the music before the next task */
INLINE void DelayToAreaEnd_(void){
	DrawStarFlag();
	if(EnemyIntervalTimer[ObjectOffset]) return;
	if(!EventMusicBuffer) StarFlagTaskControl++;      /* IncrementSFTask2 */
}

/* ---------------------------------------------------------------------------------------
** Piranha plant and firebar
** ------------------------------------------------------------------------------------ */

/* MovePiranhaPlant: rises and sinks every other frame, waits when the player is near */
void MovePiranhaPlant(void){
	u8 x = X_;
	if(!Enemy_State[x] && !EnemyFrameTimer[x]){
		u8 *sp = &PiranhaPlant_Y_Speed[x];
		if(!PiranhaPlant_MoveFlag[x]){
			if(!(*sp & 0x80)){                         /* not rising */
				PlayerEnemyDiff();
				if(N_) M00 = -M00;
				if(M00 < 0x21) goto PutinPipe;         /* ChkPlayerNearPipe */
			}
			*sp = -*sp;                                /* ReversePlantSpeed */
			PiranhaPlant_MoveFlag[x]++;
		}
		u8 a = (*sp & 0x80) ? PiranhaPlantUpYPos[x] : PiranhaPlantDownYPos[x];
		M00 = a;                                       /* RiseFallPiranhaPlant */
		if((FrameCounter & 1) && !TimerControl){
			u8 ny = Enemy_Y_Position[x] + *sp;
			Enemy_Y_Position[x] = ny;
			if(ny == a){
				PiranhaPlant_MoveFlag[x] = 0;
				EnemyFrameTimer[x] = 0x40;
			}
		}
	}
PutinPipe:
	Enemy_SprAttrib[x] = 0x20;                         /* behind the background */
}

/* FirebarSpin: adds or subtracts the speed (A_) to the spin state, high byte in A_ (its only
** caller reads A_ alone: the 6502's residual Y and the carry are not kept) */
void FirebarSpin(void){
	u8 x = X_, s = A_;
	M07 = s;
	u16 v = (u16)FirebarSpinState_High[x] << 8 | FirebarSpinState_Low[x];
	if(!FirebarSpinDirection[x]) v += s;
	else v -= s;                                       /* SpinCounterClockwise */
	FirebarSpinState_Low[x] = v;
	A_ = v >> 8;
}

/* ---------------------------------------------------------------------------------------
** Balance platforms
** ------------------------------------------------------------------------------------ */
static void DrawEraseRope(void);

/* StopPlatforms: no vertical speed for platforms X_ and y (its A and Y are dead: only
** BalancePlatform, whose registers are, calls it) */
static NOINLINE void stop_platforms(u8 y){
	InitVStf();
	Enemy_Y_Speed[y] = 0;
	Enemy_Y_MoveForce[y] = 0;
}

/* InitPlatformFall: 1000 points, and the platforms fall (y the other platform) */
INLINE void init_platform_fall(u8 y){
	X_ = y;
	GetEnemyOffscreenBits();
	/* GetEnemyOffscreenBits leaves X ObjectOffset and Y 1: what follows works on this
	** platform, and StopPlatforms clears enemy slot 1's speed (the 6502's) */
	u8 x = ObjectOffset;
	A_ = 0x06;
	X_ = x;
	SetupFloateyNumber();
	FloateyNum_X_Pos[x] = Player_Rel_XPos;
	FloateyNum_Y_Pos[x] = Player_Y_Position;
	Enemy_MovingDir[x] = 1;
	stop_platforms(1);                                 /* SetupFloateyNumber keeps X */
}

/* the player stands on the platform the collision flag f names, d7 clear */
INLINE void on_vplat(u8 f){
	if(f & 0x80) return;
	X_ = f;
	PositionPlayerOnVPlat();
}

/* PlatformFall: both platforms fall, the player with them (y the other) */
INLINE void platform_fall(u8 y){
	MoveFallingPlatform();
	X_ = y;
	MoveFallingPlatform();
	on_vplat(PlatformCollisionFlag[ObjectOffset]);
}

/* BalancePlatform: two platforms on a pulley, x and the other in its state */
void BalancePlatform(void){
	u8 x = X_;
	if(Enemy_Y_HighPos[x] == 0x03){
		EraseEnemyObject();
		return;
	}
	u8 y = Enemy_State[x];                             /* DoBPl */
	if(y & 0x80) return;                               /* the other platform does the work */
	u8 f = PlatformCollisionFlag[x];
	M00 = f;                                           /* CheckBalPlatform */
	if(Enemy_MovingDir[x]){
		platform_fall(y);
		return;
	}
	u8 t, o;                                           /* the one at the top, the other */
	if(0x2d >= Enemy_Y_Position[x]){                   /* ChkForFall */
		t = x;
		o = y;
		goto Top;
	}
	if(0x2d >= Enemy_Y_Position[y]){                   /* ChkOtherForFall */
		t = y;
		o = x;
Top:
		if(o == f) init_platform_fall(y);
		else{
			Enemy_Y_Position[t] = 0x2f;
			stop_platforms(y);
		}
		return;
	}
	u8 old = Enemy_Y_Position[x];                      /* ChkToMoveBalPlat: pha */
	u8 dir;                                            /* 0 down, 1 up, 2 stop */
	if(!(f & 0x80)){
		dir = (f == ObjectOffset) ? 0 : 1;             /* ColFlg */
	}else{
		u16 s = Enemy_Y_MoveForce[x] + 5;
		u8 m = s;
		M00 = m;
		u8 a = (u8)Enemy_Y_Speed[x] + (s >> 8);
		dir = 1;
		if(a & 0x80) dir = 0;
		else if(!a && m < 0x0b) dir = 2;
	}
	X_ = x;
	if(dir == 1) MovePlatformUp();                     /* PlatUp */
	else if(dir == 2){                                 /* PlatSt */
		stop_platforms(y);
	}else MovePlatformDown();                          /* PlatDn */
	x = X_;
	y = Enemy_State[x];                                /* DoOtherPlatform: pla */
	Enemy_Y_Position[y] += old - Enemy_Y_Position[x];  /* the other moves the other way */
	on_vplat(PlatformCollisionFlag[x]);
	DrawEraseRope();
}

/* DrawEraseRope: the ropes above both platforms, drawn when going down, erased going up: for
** each, RopeVRAMData gets SetupPlatformRope's name table address, 2 and two tiles (the other
** platform's speed is this one's reversed; the 6502's A and Y here are dead) */
static NOINLINE void DrawEraseRope(void){
	u8 y = ObjectOffset;
	if((u8)Enemy_Y_Speed[y] | Enemy_Y_MoveForce[y]){
		u8 sp = Enemy_Y_Speed[y];
		u8 *r = RopeVRAMData;
		for(;;){
			/* SetupPlatformRope: name table address in M01/M00, page in M02 */
			u8 lo = Enemy_X_Position[y] + 8, c = lo < 8;
			if(!SecondaryHardMode) c = (lo += 0x10) < 0x10;
			u8 p = Enemy_PageLoc[y] + c;
			M02 = p;                                   /* GetLRp */
			u8 v = Enemy_Y_Position[y];
			bool up = sp & 0x80;                       /* going up: erased, 8 lower */
			if(up) v += 8;
			u8 h = (v >> 6) | 0x20 | ((p & 1) << 2);   /* GetHRp: asl, rol: d7 and d6 */
			M01 = h;
			u8 a = ((v << 2) & 0xe0) | ((lo & 0xf0) >> 3);
			if(Enemy_Y_Position[y] >= 0xe8) a &= 0xbf;
			M00 = a;
			r[0] = h;
			r[1] = a;
			r[2] = 2;
			r[3] = up ? 0x24 : 0xa2;
			r[4] = up ? 0x24 : 0xa3;
			r += 5;
			if(r == RopeVRAMData + 10) break;
			sp = ~sp;                                  /* OtherRope */
			y = Enemy_State[y];
		}
		*r = 0;                                        /* EndRp */
		DrawEraseRopeHook();
	}
}

/* ---------------------------------------------------------------------------------------
** Moving platforms
** ------------------------------------------------------------------------------------ */

/* YMovingPlatform: goes up and down around its centre */
void YMovingPlatform(void){
	u8 x = X_;
	u8 *yp = &Enemy_Y_Position[x], a = *yp;
	if(!((u8)Enemy_Y_Speed[x] | Enemy_Y_MoveForce[x])){
		Enemy_YMF_Dummy[x] = 0;
		if(a < YPlatformTopYPos[x]){
			if(!(FrameCounter & 0x07)) *yp = a + 1;
			goto SkipIY;
		}
	}
	if(a < YPlatformCenterYPos[x]) MovePlatformDown();   /* ChkYCenterPos */
	else MovePlatformUp();
SkipIY:
	ChkYPCollision_();
}

static void ChkYPCollision_(void){
	if(!(PlatformCollisionFlag[X_] & 0x80)) PositionPlayerOnVPlat();
}

/* XMovingPlatform: goes left and right, the player with it */
void XMovingPlatform(void){
	A_ = 0x0e;
	XMoveCntr_Platform();
	MoveWithXMCntrs();
	if(PlatformCollisionFlag[X_] & 0x80) return;
	PositionPlayerOnHPlat_();
}

/* PositionPlayerOnHPlat: moves the player by M00, the platform's move */
static void PositionPlayerOnHPlat_(void){
	u8 d = M00;
	u16 p = ((u16)Player_PageLoc << 8 | Player_X_Position) + (s8)d;
	Player_X_Position = p;
	Player_PageLoc = p >> 8;
	Platform_X_Scroll = d;                             /* SetPVar */
	PositionPlayerOnVPlat();
}

/* DropPlatform: drops fast while the player stands on it */
void DropPlatform(void){
	if(PlatformCollisionFlag[X_] & 0x80) return;
	MoveDropPlatform();
	PositionPlayerOnVPlat();
}

/* RightPlatform: starts moving right when the player lands */
void RightPlatform(void){
	MoveEnemyHorizontally();
	M00 = A_;
	u8 x = X_;
	if(PlatformCollisionFlag[x] & 0x80) return;
	Enemy_X_Speed[x] = 0x10;
	PositionPlayerOnHPlat_();
}

void MoveLargeLiftPlat(void){
	MoveLiftPlatforms_();
	ChkYPCollision_();
}

void MoveSmallPlatform(void){
	MoveLiftPlatforms_();
	ChkSmallPlatCollision_();
}

/* MoveLiftPlatforms: adds the vertical speed and force */
static void MoveLiftPlatforms_(void){
	u8 x = X_;
	if(TimerControl) return;
	u16 s = Enemy_YMF_Dummy[x] + Enemy_Y_MoveForce[x];
	Enemy_YMF_Dummy[x] = s;
	Enemy_Y_Position[x] += (u8)Enemy_Y_Speed[x] + (s >> 8);
}

INLINE void ChkSmallPlatCollision_(void){
	u8 f = PlatformCollisionFlag[X_];
	if(!f) return;
	A_ = f;
	PositionPlayerOnS_Plat();
}

/* ---------------------------------------------------------------------------------------
** OffscreenBoundsCheck: erases an enemy 72 pixels past either side of the screen (hammer
** bros and piranha plants 16 pixels on the left), some never on the right
** ------------------------------------------------------------------------------------ */
__attribute__((optimize("no-store-merging"))) void OffscreenBoundsCheck(void){
	u8 x = X_;
	u8 id = Enemy_ID[x];
	if(id == FlyingCheepCheep) return;
	u8 lo = ScreenLeft_X_Pos;
	u8 sub = 0x48;
	if(id == HammerBro || id == PiranhaPlant){         /* LimitB: carry set by the cpy */
		if(lo < 0xc7) sub++;                           /* ExtendLB: borrow without the carry */
		lo += 0x39;                                    /* the page keeps no carry */
	}else if(id < PiranhaPlant) sub++;                 /* the cpy's carry */
	u16 l0 = (u16)ScreenLeft_PageLoc << 8 | lo;
	u16 l = l0 - sub;
	M01 = l;
	M00 = l >> 8;
	u16 r = ((u16)ScreenRight_PageLoc << 8 | ScreenRight_X_Pos) + 0x48;
	if(l0 >= sub) r++;                                 /* the page's carry */
	M03 = r;
	M02 = r >> 8;
	u8 *p = opq(&Enemy_PageLoc[x]);
	u16 e = (u16)p[0] << 8 | E(p, Enemy_PageLoc, Enemy_X_Position);
	if((s16)(e - l) >= 0){
		if((s16)(e - r) < 0) return;                   /* on the screen */
		if(Enemy_State[x] == HammerBro) return;        /* spiny's egg state */
		if(id == PiranhaPlant || (u8)(id - FlagpoleFlagObject) < 3) return;   /* and the star flag, jumpspring */
	}
	X_ = x;                                            /* TooFar */
	EraseEnemyObject();
}
