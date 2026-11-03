/*
 * The objects: a port of smbdis.asm's lines 6273-7762 (ProcFireball_Bubble to ImposeGravity),
 * 1283-1441 (FloateyNumbersRoutine) and 2583-2637 (DigitsMathRoutine, the top score).
 * Routine names are the 6502's; registers as in smb.h. Drawing is gfx17.c's.
 * Labels only this file reaches are static helpers named with a trailing underscore.
 */
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */
/* three passes that cost this file size: hoisting under register pressure, partial redundancy
** elimination and value-range propagation (measured, about 45 bytes) */
#pragma GCC optimize("no-ira-hoist-pressure", "no-tree-pre", "no-tree-vrp")


/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
const u8 FloateyNumTileData[24] PROGMEM = { ROM_FloateyNumTileData };   /* dummy; 100 200 400 500 800; 1000 2000 4000 5000 8000; 1-UP */

/* high nybble: the digit; low nybble: the amount to add to it */
static const u8 ScoreUpdateData[12] PROGMEM = { ROM_ScoreUpdateData };
/* FlagpoleScoreDigits << 4 | FlagpoleScoreMods */
static const u8 FlagpoleScoreData[5] PROGMEM = { 0x35, 0x32, 0x48, 0x44, 0x41 };
/* by PlayerFacingDir (the 6502's FireballXSpdData-1,y): 3 read past the table into
** Bubble_MForceData's $ff; 0 never throws (the facing is 1 or 2 from the first frame) */
static const u8 FireballXSpdData[4] PROGMEM = { 0x00, 0x40, 0xc0, 0xff };
static const u8 Jumpspring_Y_PosData[4] PROGMEM = { ROM_Jumpspring_Y_PosData };
/* a third vine reads past the table into the next instruction: cpx #$05 is $e0 */
static const u8 VineHeightData[3] PROGMEM = { 0x30, 0x60, 0xe0 };
/* BlockCode by BrickQBlockMetatiles index (the bricks' set at index - 5): a power-up type
** (MushFlower 0, Star 2, ExtraLifeMush 3), a coin or a vine */
#define BC_COIN 0x10
#define BC_VINE 0x20
static const u8 BlockCodeData[14] PROGMEM = {
	0, BC_COIN, BC_COIN, 3, 0, BC_VINE, 2, BC_COIN, 3,
	0, BC_VINE, 2, BC_COIN, 3
};

/* The 6502's offsets from DisplayDigits ($07d7) of a number's last digit; 1 player: player 1's
** (CoinTallyOffsets, ScoreOffsets and StatusBarNybbles had a second entry for Luigi) */
#define DD_SCORE   0x0b
#define DD_COIN    0x17
#define DD_TIMER   0x23
#define SB_NYBBLES 0x02

#define rd(t, i) pgm_read_byte(&(t)[i])

/* An address gcc must keep in a pointer register: the fields around it are then reached with
** ldd/std and a displacement instead of a full address each */
	#define OPQ(p) ({ __typeof__(p) p_ = (p); __asm__("" : "+b"(p_)); p_; })
	#define OPZ(p) ({ __typeof__(p) p_ = (p); __asm__("" : "+z"(p_)); p_; })   /* in Z: no call */
	#define OPX(p) ({ __typeof__(p) p_ = (p); __asm__("" : "+x"(p_)); p_; })   /* in X */
	/* a byte held in a register from here: a constant loaded once (gcc loads it again for each
	** register global and store), a load done before what follows */
	#define K1(v) ({ u8 v_ = (v); __asm__("" : "+d"(v_)); v_; })
/* jmp f, a tail call: -mcall-prologues keeps gcc from making one (call f; ret). Only at the
** very end of a function that saves no register (a jmp past an epilogue would not restore
** it: check the disassembly for push/__prologue_saves__), and the function noipa (or gcc takes
** the __builtin_unreachable for "never returns" and drops the code after its calls) */
	#define TAILJMP(f) do{ __asm__ volatile("jmp %x0" :: "i"(f) : "memory"); __builtin_unreachable(); }while(0)
/* entry i of array f as a byte pointer, and the distance from array a to array b (one
** object's entries in two parallel arrays of ra): a constant the compiler folds */
#define SOP(f, i) ((u8 *)(f) + (i))
#define RO(a, b)  ((int)((const u8 *)(b) - (const u8 *)(a)))
/* scalar f through r = &b, f lying 0-63 bytes after b (an ldd/std each) */
#define AT(r, b, f) ((r)[(const u8 *)&(f) - (const u8 *)&(b)])

/* ---------------------------------------------------------------------------------------
** Floatey numbers (1283-1355)
** ------------------------------------------------------------------------------------ */

/* DigitModifier from a ScoreUpdateData-style byte, then AddToScore */
static __attribute__((noipa)) void AddScoreCode_(u8 d){
	DigitModifier[d >> 4] = d & 0x0f;
	TAILJMP(AddToScore);
}

/* one more life, with its sound */
static __attribute__((noinline)) void OneUp_(void){
	NumberofLives++;
	Square2SoundQueue = Sfx_ExtraLife;
}

/* FloateyNumbersRoutine: counts a floatey number down, awards its points once, moves it up */
void FloateyNumbersRoutine(void){
	u8 x = X_;
	u8 *p = OPQ(SOP(FloateyNum_Control, x));
	u8 ctrl = p[0];
	if(!ctrl) return;
	if(ctrl >= 0x0b) p[0] = ctrl = 0x0b;
	u8 t = p[RO(FloateyNum_Control, FloateyNum_Timer)];
	if(!t){
		p[0] = 0;
		return;
	}
	p[RO(FloateyNum_Control, FloateyNum_Timer)] = --t;
	if(t == 0x2a){
		if(ctrl == 0x0b) OneUp_();
		AddScoreCode_(rd(ScoreUpdateData, ctrl));
		x = X_;                                 /* UpdateNumber's ldx ObjectOffset */
		p = OPZ(SOP(FloateyNum_Control, x));
	}
	/* ChkTallEnemy..GetAltOffset only chose the OAM offset (gfx); it reloads X from ObjectOffset */
	u8 y = p[RO(FloateyNum_Control, FloateyNum_Y_Pos)];
	C_ = y >= 0x18;                             /* the carry SetupNumSpr's sbc #$08 takes */
	if(C_) p[RO(FloateyNum_Control, FloateyNum_Y_Pos)] = y - 1;
	DrawFloateyNumber();                        /* X_ as it is; no ldx after: GameEngine writes X_ */
}

/* ---------------------------------------------------------------------------------------
** The status bar digits (2583-2637)
** ------------------------------------------------------------------------------------ */

/* DigitsMathRoutine: adds DigitModifier to the six digits ending at display offset Y_. The
** 6502 walked six bytes down from a number's last digit, so the coin and timer updates also
** touched unused bytes above them; those only carried into DigitModifier and the byte before
** it (FloateyNum_Timer's last), both cleared at the end, so only the real digits are done */
void DigitsMathRoutine(void){
	u8 y = Y_;
	if(OperMode != TitleScreenModeValue){
		u8 *d = &PlayerScoreDisplay[6], n = 6;  /* past the last digit */
		if(y == DD_COIN){ d = &CoinDisplay[2]; n = 2; }
		else if(y == DD_TIMER){ d = &GameTimerDisplay[3]; n = 3; }
		else if(y != DD_SCORE) n = 0;
		u8 *m = &DigitModifier[5];
		for(; n; n--){
			d = OPX(d - 1);
			u8 a = *m + *d;
			m--;                                /* DigitModifier-1,x: FloateyNum_Timer's last at 0 */
			if(a & 0x80){                       /* BorrowOne */
				(*m)--;
				a = 9;
			}else if(a >= 10){                  /* CarryOne */
				a -= 10;
				(*m)++;
			}
			*d = a;                             /* StoreNewD */
		}
		y -= 6;                                 /* the 6502's dey, six digits */
	}
	/* EraseDMods: x 6 down to 0 clears DigitModifier-1,x, the byte before included */
	u8 *e = &FloateyNum_Timer[ENEMY_SLOTS + 1];
	for(u8 i = 7; i; i--) *e++ = 0;
	A_ = 0;
	X_ = 0xff;
	Y_ = y;
}

/* UpdateTopScore: Mario's score to the top score if it is not lower (TopScoreCheck, x = 5);
** Luigi's is left out, 1 player. The digits compare as one number, the first the highest;
** equal digits need no copy, so the copy starts at the first one that differs */
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void UpdateTopScore(void){
	X_ = 0x05;
	u8 *t = TopScoreDisplay;                    /* PlayerScoreDisplay follows it */
	u8 i = 0;
	while(t[i + 6] == t[i]) if(++i == 6) return;
	if(t[i + 6] < t[i]) return;
	do t[i] = t[i + 6]; while(++i != 6);
}

/* ---------------------------------------------------------------------------------------
** Fireballs and air bubbles (6273-6433)
** ------------------------------------------------------------------------------------ */

/* SprObject d at the 16-bit position pos (page:x), Y position y, Y high byte 1 */
static __attribute__((noinline)) void Place_(u8 d, u16 pos, u8 y){
	u8 *p = OPQ(SOP(SprObject_PageLoc, d));
	p[0] = pos >> 8;
	p[RO(SprObject_PageLoc, SprObject_X_Position)] = pos;
	p[RO(SprObject_PageLoc, SprObject_Y_HighPos)] = 1;
	SprObject_Y_Position[d] = y;
}

/* SprObject s's page and x as one 16-bit position */
static inline __attribute__((always_inline)) u16 SPos_(u8 s){
	u8 *p = SOP(SprObject_PageLoc, s);
	return (u16)p[0] << 8 | p[RO(SprObject_PageLoc, SprObject_X_Position)];
}

/* ImposeGravity on SprObject slot with M00 down and M02 maximum, down only */
static __attribute__((noipa)) void Grav_(u8 slot, u8 down, u8 max){
	X_ = slot;
	M00 = down;
	M02 = max;
	A_ = 0;
	TAILJMP(ImposeGravity);
}

/* FireballObjCore: launches a fireball from the player, moves it, collides, draws */
static void FireballObjCore_(u8 x){
	X_ = x;
	ObjectOffset = x;
	u8 *sp = &Fireball_State[x];
	u8 st = *sp;
	if(st & 0x80){                              /* FireballExplosion */
		RelativeFireballPosition();
		DrawExplosion_Fireball();
		return;
	}
	if(!st) return;
	if(st != 1){
		/* carry clear here: the asl of a state without d7 */
		u16 pos = SPos_(0) + 4;
		u8 *p = OPQ(SOP(Fireball_X_Speed, x));
		p[0] = rd(FireballXSpdData, PlayerFacingDir);
		p[RO(SprObject_X_Speed, SprObject_PageLoc)] = pos >> 8;
		p[RO(SprObject_X_Speed, SprObject_X_Position)] = pos;
		p[RO(SprObject_X_Speed, SprObject_Y_Speed)] = 4;
		p = SOP(Fireball_Y_HighPos, x);
		p[0] = 1;
		p[RO(SprObject_Y_HighPos, SprObject_Y_Position)] = Player_Y_Position;
		Fireball_BoundBoxCtrl[x] = 7;
		*sp = st - 1;
	}
	/* RunFB */
	Grav_(x + OFS_FIREBALL, 0x50, 0x03);
	MoveObjectHorizontally();
	X_ = x;                                     /* = ObjectOffset */
	RelativeFireballPosition();
	GetFireballOffscreenBits();
	GetFireballBoundBox();
	FireballBGCollision();
	if(FBall_OffscreenBits & 0xcc){
		Fireball_State[X_] = 0;                 /* EraseFB */
		return;
	}
	FireballEnemyCollision();
	DrawFireball();
}

/* MoveBubl: floats the bubble up by a pseudorandom amount (bit b); off at the status bar */
static void MoveBubl_(u8 x, u8 b){
	u8 f = b ? 0x50 : 0xff;                     /* Bubble_MForceData */
	u8 *d = &Bubble_YMF_Dummy[x];
	u8 v = *d;
	*d = v - f;
	u8 a = Bubble_Y_Position[x] - (v < f);
	if(a < 0x20) a = 0xf8;
	Bubble_Y_Position[x] = a;
}

/* SetupBubble: a bubble X_ at the player's mouth (pseudorandom bit M07), then MoveBubl */
void SetupBubble(void){
	u8 x = X_, b = M07;
	AirBubbleTimer = b ? 0x20 : 0x40;           /* BubbleTimerData */
	/* Entrance_GameTimerSetup comes here with whatever X it has; the 6502 wrote past the
	** bubbles then: only the timer is kept for an X beyond them */
	if(x >= BUBBLE_SLOTS) return;
	/* facing right adds 8 plus the carry of lsr PlayerFacingDir: 9 */
	Place_(x + OFS_BUBBLE, SPos_(0) + ((PlayerFacingDir & 1) ? 9 : 0), Player_Y_Position + 8);
	MoveBubl_(x, b);
}

/* ProcFireball_Bubble: throws a fireball on B, runs both, then the air bubbles */
void ProcFireball_Bubble(void){
	if(PlayerStatus >= 2){
		if((A_B_Buttons & B_Button) && !(PreviousA_B_Buttons & B_Button)){
			u8 x = FireballCounter & 1;
			if(!Fireball_State[x] && Player_Y_HighPos == 1 && !CrouchingFlag && Player_State != 3){
				Square1SoundQueue = Sfx_Fireball;
				Fireball_State[x] = 2;
				u8 y = PlayerAnimTimerSet;
				FireballThrowingTimer = y;
				PlayerAnimTimer = y - 1;
				FireballCounter++;
			}
		}
		FireballObjCore_(0);
		FireballObjCore_(1);
	}
	if(AreaType) return;                        /* ProcAirBubbles: water only */
	for(s8 x = BUBBLE_SLOTS - 1; x >= 0; x--){
		ObjectOffset = x;
		X_ = x;
		u8 b = PseudoRandomBitReg[1 + x] & 0x01;   /* BubbleCheck */
		M07 = b;
		if(Bubble_Y_Position[x] != 0xf8) MoveBubl_(x, b);
		else if(!AirBubbleTimer) SetupBubble();   /* X_ = x, M07 = b; neither writes X_ */
		RelativeBubblePosition();
		X_ = x;
		GetBubbleOffscreenBits();
		X_ = x;
		DrawBubble();
	}
}

/* ---------------------------------------------------------------------------------------
** The game timer, warp zones, whirlpools (6436-6567)
** ------------------------------------------------------------------------------------ */

/* RunGameTimer: counts the time down; time up kills the player */
__attribute__((noipa)) void RunGameTimer(void){
	if(!OperMode) return;
	u8 g = GameEngineSubroutine;
	if(g < 0x08 || g == 0x0b) return;
	if(Player_Y_HighPos >= 2) return;
	u8 *t = OPQ(&GameTimerCtrlTimer);
#define GT(f) AT(t, GameTimerCtrlTimer, f)
	if(t[0]) return;
	u8 lo = GT(GameTimerDisplay[1]) | GT(GameTimerDisplay[2]);
	u8 hi = GT(GameTimerDisplay[0]);
	if(!(hi | lo)){
		PlayerStatus = 0;                       /* TimeUpOn */
		A_ = 0;
		ForceInjury();
		GameTimerExpiredFlag++;
		return;
	}
	if(hi == 1 && !lo) GT(EventMusicQueue) = TimeRunningOutMusic;
	t[0] = 0x18;
#undef GT
	Y_ = DD_TIMER;
	DigitModifier[5] = 0xff;
	DigitsMathRoutine();
	A_ = 0xa4;
	TAILJMP(PrintStatusBarNumbers);
}

/* WarpZoneObject: once the player is up on the screen, unlocks the scroll for the warp zone */
__attribute__((noipa)) void WarpZoneObject(void){
	if(!ScrollLock) return;
	if(Player_Y_Position & Player_Y_HighPos) return;
	ScrollLock = 0;
	WarpZoneControl++;
	TAILJMP(EraseEnemyObject);
}

/* ProcessWhirlpools: finds a whirlpool the player is over (water levels); the 6502's 8-bit
** compares with borrow are the signs of 16-bit page:x differences */
void ProcessWhirlpools(void){
	if(AreaType) return;
	Whirlpool_Flag = 0;
	if(TimerControl) return;
	u16 pl = SPos_(0);
	for(s8 y = 4; y >= 0; y--){
		u8 *w = OPQ(SOP(Cannon_PageLoc, y));    /* Whirlpool_PageLoc, LeftExtent, Length */
		u8 len = w[RO(Cannon_PageLoc, Cannon_Y_Position)];
		u16 l = (u16)w[0] << 8 | w[RO(Cannon_PageLoc, Cannon_X_Position)];
		u16 r = l + len;
		M02 = r;
		if(!w[0]) continue;
		M01 = r >> 8;
		if((s16)(pl - l) < 0 || (s16)(r - pl) < 0) continue;
		/* WhirlpoolActivate: on odd frames toward the centre, then down */
		u16 c = l + (len >> 1);
		M01 = c;
		if(FrameCounter & 1){
			if((s16)(c - pl) < 0) pl--;         /* right of the centre: one pixel left */
			else if(Player_CollisionBits & 1) pl++;   /* LeftWh: one pixel right */
			Player_X_Position = pl;
			Player_PageLoc = pl >> 8;           /* SetPWh */
		}
		Whirlpool_Flag = 1;                     /* WhPull: ImposeGravity on the player */
		Grav_(0, 0x10, 1);
		return;
	}
}

/* ---------------------------------------------------------------------------------------
** The flagpole, jumpsprings (6579-6668)
** ------------------------------------------------------------------------------------ */

/* FlagpoleRoutine: lowers the flag while the player slides, awards the flagpole score */
__attribute__((noipa)) void FlagpoleRoutine(void){
	ObjectOffset = X_ = K1(5);
	if(Enemy_ID[5] != FlagpoleFlagObject) return;
	if(GameEngineSubroutine == 0x04 && Player_State == 0x03){
		u8 *r = OPQ(&Player_Y_Position);
#define FP(f) AT(r, Player_Y_Position, f)
		if(FP(Enemy_Y_Position[5]) >= 0xaa || r[0] >= 0xa2){
			AddScoreCode_(rd(FlagpoleScoreData, FP(FlagpoleScore)));   /* GiveFPScr */
			GameEngineSubroutine = 0x05;
		}else{
			/* carry clear from the cmp: the flag moves $ff/256 + 1 a frame */
			u8 m = Enemy_YMF_Dummy[5];
			Enemy_YMF_Dummy[5] = m - 1;
			FP(Enemy_Y_Position[5]) += 1 + (m != 0);
			m = FP(FlagpoleFNum_YMFDummy);
			FP(FlagpoleFNum_YMFDummy) = m + 1;
			FP(FlagpoleFNum_Y_Pos) -= 1 + (m != 0xff);
		}
#undef FP
	}
	GetEnemyOffscreenBits();                    /* FPGfx */
	RelativeEnemyPosition();
	TAILJMP(FlagpoleGfxHandler);
}

/* JumpspringHandler: compresses the jumpspring under the player; draws it, steps the frame */
void JumpspringHandler(void){
	GetEnemyOffscreenBits();
	u8 c = JumpspringAnimCtrl;
	if(!TimerControl && c){
		u8 y = c - 1;
		Player_Y_Position += (y & 2) ? -2 : 2;  /* DownJSpr: up two pixels */
		Y_ = y;
		/* PosJSpr: the frame position; A on the third frame sets the high bounce */
		u8 x = X_;
		u8 fy = K1(Jumpspring_FixedYPos[x]);
		Enemy_Y_Position[x] = fy + rd(Jumpspring_Y_PosData, y);
		if(y && (A_B_Buttons & A_Button) && !(PreviousA_B_Buttons & A_Button))
			JumpspringForce = 0xf4;
		if(y == 3){                             /* BounceJS */
			Player_Y_Speed = JumpspringForce;
			JumpspringAnimCtrl = 0;
		}
	}
	RelativeEnemyPosition();                    /* DrawJSpr */
	EnemyGfxHandler();
	OffscreenBoundsCheck();
	c = JumpspringAnimCtrl;
	if(!c || JumpspringTimer) return;
	JumpspringTimer = 4;
	JumpspringAnimCtrl = c + 1;
}

/* ---------------------------------------------------------------------------------------
** Vines (6677-6749)
** ------------------------------------------------------------------------------------ */

/* Setup_Vine: a vine in enemy slot X_ at block object Y_ */
void Setup_Vine(void){
	u8 x = X_, y = Y_;
	Enemy_ID[x] = VineObject;
	Enemy_Flag[x] = 1;
	u8 *e = SOP(SprObject_PageLoc, OFS_ENEMY + x);
	u8 *b = SOP(SprObject_PageLoc, OFS_BLOCK + y);
#define XO RO(SprObject_PageLoc, SprObject_X_Position)
#define YO RO(SprObject_PageLoc, SprObject_Y_Position)
	e[0] = b[0];
	e[XO] = b[XO];
	u8 a = b[YO];
	e[YO] = a;
#undef XO
#undef YO
	y = VineFlagOffset;
	if(!y) VineStart_Y_Position = a;
	if(y < 3) VineObjOffset[y] = x;             /* the 6502 has room for three */
	VineFlagOffset = y + 1;
	Square2SoundQueue = A_ = Sfx_GrowVine;
	Y_ = y;
}

/* VineObjectHandler: grows the vine, draws it, kills it offscreen, plants the climbing tile */
void VineObjectHandler(void){
	if(X_ == 5){
		u8 h = VineHeight;
		if(h != rd(VineHeightData, (u8)(VineFlagOffset - 1)) && (FrameCounter & 2)){
			Enemy_Y_Position[5]--;              /* sbc #1 with the lsr's carry set */
			VineHeight = ++h;
		}
		if(h >= 0x08){
			RelativeEnemyPosition();
			GetEnemyOffscreenBits();
			u8 y = 0;
			do{
				Y_ = y;
				DrawVine();                     /* returns Y as it was */
			}while(++y != VineFlagOffset);
			if(Enemy_OffscreenBits & 0x0c){
				do{
					X_ = VineObjOffset[--y];
					EraseEnemyObject();         /* keeps Y, returns A = 0 */
				}while(y);
				VineFlagOffset = 0;
				VineHeight = h = 0;
			}
			if(h >= 0x20){                      /* WrCMTile */
				X_ = OFS_ENEMY + 5;
				A_ = 1;
				Y_ = 0x1b;
				BlockBufferCollision();
				u8 r = M02 >> 4;
				if(r < 0x0d && !BBPtr[r]) bb_Set(r, 0x26);
			}
		}
	}
	/* no ExitVH ldx ObjectOffset: EnemiesAndLoopsCore writes X_ after RunEnemyObjectsCore */
}

/* ---------------------------------------------------------------------------------------
** Cannons and bullet bills (6763-6864)
** ------------------------------------------------------------------------------------ */

/* BulletBillHandler: aims a new bullet bill away from the player, moves and draws it */
static inline __attribute__((always_inline)) void BulletBillHandler_(u8 x){
	if(!TimerControl){
		if(!Enemy_State[x]){
			if((Enemy_OffscreenBits & 0x0c) == 0x0c){
				EraseEnemyObject();             /* KillBB */
				return;
			}
			PlayerEnemyDiff();                  /* M00 the difference; N_, C_ of its high byte */
			Enemy_MovingDir[x] = N_ ? 1 : 2;
			Enemy_X_Speed[x] = N_ ? 0x18 : (s8)0xe8;   /* BulletBillXSpdData */
			if((u8)(M00 + 0x28 + C_) < 0x50){   /* too close to the cannon */
				X_ = x;
				EraseEnemyObject();
				return;
			}
			Enemy_State[x] = 1;
			EnemyFrameTimer[x] = 0x0a;
			Square2SoundQueue = Sfx_Blast;
		}
		X_ = x;
		if(Enemy_State[x] & 0x20) MoveD_EnemyVertically();     /* ChkDSte */
		MoveEnemyHorizontally();                /* BBFly */
	}
	X_ = x;                                     /* RunBBSubs */
	GetEnemyOffscreenBits();
	RelativeEnemyPosition();
	GetEnemyBoundBox();
	PlayerEnemyCollision();
	EnemyGfxHandler();
}

/* ProcessCannons: fires bullet bills from the cannons into the first three enemy slots */
void ProcessCannons(void){
	if(!AreaType) return;
	for(s8 x = 2; x >= 0; x--){
		ObjectOffset = x;
		if(!Enemy_Flag[x]){
			u8 y = PseudoRandomBitReg[1 + x] & (SecondaryHardMode ? 0x07 : 0x0f);   /* CannonBitmasks */
			if(y < 6){
				u8 *c = OPQ(SOP(Cannon_PageLoc, y));
				if(c[0]){
					u8 tm = c[RO(Cannon_PageLoc, Cannon_Timer)];
					if(tm){
						c[RO(Cannon_PageLoc, Cannon_Timer)] = tm - 1;   /* sbc #0 with the carry clear */
					}else if(!TimerControl){    /* FireCannon */
						c[RO(Cannon_PageLoc, Cannon_Timer)] = 0x0e;
						Place_(x + OFS_ENEMY, (u16)c[0] << 8 | c[RO(Cannon_PageLoc, Cannon_X_Position)],
						       c[RO(Cannon_PageLoc, Cannon_Y_Position)] - 8);
						Enemy_Flag[x] = 1;
						Enemy_State[x] = 0;
						Enemy_BoundBoxCtrl[x] = 0x09;
						Enemy_ID[x] = BulletBill_CannonVar;
						continue;               /* Next3Slt */
					}
				}
			}
		}
		if(Enemy_ID[x] != BulletBill_CannonVar) continue;     /* Chk_BB */
		X_ = x;
		OffscreenBoundsCheck();
		if(!Enemy_Flag[x]) continue;
		X_ = x;
		GetEnemyOffscreenBits();
		X_ = x;
		BulletBillHandler_(x);
	}
}

/* ---------------------------------------------------------------------------------------
** Hammers (6873-6958)
** ------------------------------------------------------------------------------------ */

/* SpawnHammerObj: a hammer for the hammer bro ObjectOffset in a pseudorandom misc slot */
void SpawnHammerObj(void){
	u8 y = PseudoRandomBitReg[1] & 0x07;
	if(!y) y = PseudoRandomBitReg[1] & 0x08;
	u8 x = ObjectOffset;
	bool c = false;
	/* y is the 6502's slot, 0-8: its 6-8 are the last three here, as for the coins (DrawHammer
	** draws them with 6-8's OAM offsets); a slot between them and the first ones is taken as busy */
	u8 s = (y >= 6) ? (u8)(y - (9 - MISC_SLOTS)) : y;
	u8 e = (y >= 6) ? 6 : (y >= 3) ? 5 : 4;      /* HammerEnemyOfsData */
	if(s < MISC_SLOTS && !Misc_State[s] && !Enemy_Flag[e]){
		y = s;
		HammerEnemyOffset[y] = x;
		Misc_State[y] = 0x90;
		Misc_BoundBoxCtrl[y] = 0x07;
		A_ = 0x07;
		c = true;
	}
	X_ = x;                                     /* NoHammer */
	Y_ = y;
	C_ = c;
}

/* ProcHammerObj: held in the bro's hand, thrown at state 2, then flies under gravity */
static inline __attribute__((always_inline)) void ProcHammerObj_(u8 x){
	X_ = x;
	if(!TimerControl){
		u8 a = Misc_State[x] & 0x7f;
		u8 y = HammerEnemyOffset[x];
		if(a < 2){
			M01 = 0x0f;
			Grav_(x + OFS_MISC, 0x10, 0x04);
			MoveObjectHorizontally();
			X_ = x;                             /* = ObjectOffset */
			PlayerHammerCollision();            /* RunAllH */
		}else{
			if(a == 2){                         /* SetHSpd: thrown */
				u8 *e = OPZ(&Enemy_State[y]);
				Misc_Y_Speed[x] = -2;
				e[0] &= 0xf7;
				Misc_X_Speed[x] = (AT(e, Enemy_State[y], Enemy_MovingDir[y]) & 1) ? 0x10 : (s8)0xf0;   /* HammerXSpdData */
			}
			Misc_State[x]--;                    /* SetHPos: in the bro's hand */
			Place_(x + OFS_MISC, SPos_(y + OFS_ENEMY) + 2, Enemy_Y_Position[y] - 0x0a);
			X_ = x;
		}
	}
	GetMiscOffscreenBits();                     /* RunHSubs */
	RelativeMiscPosition();
	GetMiscBoundBox();
	DrawHammer();
}

/* ---------------------------------------------------------------------------------------
** Jumping coins (6963-7067)
** ------------------------------------------------------------------------------------ */

/* FindEmptyMiscSlot: the last three misc slots, from the end; the last if all are busy */
static inline __attribute__((always_inline)) u8 FindEmptyMiscSlot_(void){
	u8 y = MISC_SLOTS - 1;
	while(Misc_State[y]){
		C_ = true;                              /* cpy #$05 with y >= 5 */
		if(--y == (u8)(MISC_SLOTS - 4)){
			y = MISC_SLOTS - 1;
			break;
		}
	}
	JumpCoinMiscOffset = y;
	return y;
}

/* JCoinC: a jumping coin at page/xpos/ypos out of block object x in the misc slot
** FindEmptyMiscSlot gives (CoinBlock: a pixel higher unless that set the carry); the coin given */
static void JCoinC_(u8 x, u8 page, u8 xpos, u8 ypos, bool coinblock){
	u8 y = FindEmptyMiscSlot_();
	if(coinblock && !C_) ypos--;
	u8 *p = OPQ(SOP(SprObject_X_Position, OFS_MISC + y));
	SprObject_PageLoc[OFS_MISC + y] = page;
	p[0] = xpos;
	p[RO(SprObject_X_Position, SprObject_Y_Position)] = ypos;
	p[RO(SprObject_X_Position, SprObject_Y_Speed)] = -5;
	p[RO(SprObject_X_Position, SprObject_Y_HighPos)] = 1;
	Misc_State[y] = 1;
	Square2SoundQueue = Sfx_CoinGrab;
	ObjectOffset = x;
	GiveOneCoin();
	CoinTallyFor1Ups++;
}

/* ProcJumpCoin: the coin jumps under gravity, then becomes a floatey number that scrolls */
static inline __attribute__((always_inline)) void ProcJumpCoin_(u8 x){
	u8 *st = &Misc_State[x];
	if(*st != 1){
		u8 s = ++*st;
		u8 *p = SOP(Misc_PageLoc, x);
		u16 t = ((u16)p[0] << 8 | p[RO(SprObject_PageLoc, SprObject_X_Position)]) + ScrollAmount;
		p[RO(SprObject_PageLoc, SprObject_X_Position)] = t;
		p[0] = t >> 8;
		if(s == 0x30){
			*st = 0;
			return;
		}
	}else{                                      /* JCoinRun */
		M01 = 0x03;
		Grav_(x + OFS_MISC, 0x50, 0x06);       /* keeps ObjectOffset = x */
		if(Misc_Y_Speed[x] == 5) (*st)++;
	}
	X_ = x;                                     /* RunJCSubs */
	RelativeMiscPosition();
	GetMiscOffscreenBits();
	GetMiscBoundBox();
	JCoinGfxHandler();
}

/* MiscObjectsCore: every misc object: hammers (d7 set) and jumping coins */
void MiscObjectsCore(void){
	for(s8 x = MISC_SLOTS - 1; x >= 0; x--){
		ObjectOffset = x;
		u8 st = Misc_State[x];
		if(!st) continue;
		if(st & 0x80) ProcHammerObj_(x);
		else ProcJumpCoin_(x);
	}
}

/* ---------------------------------------------------------------------------------------
** Coins and the score (7084-7121)
** ------------------------------------------------------------------------------------ */

/* GiveOneCoin: one coin, a 1-up at 100, then 200 points (CoinPoints) */
void GiveOneCoin(void){
	DigitModifier[5] = 1;
	Y_ = DD_COIN;
	DigitsMathRoutine();
	if(++CoinTally == 100){
		CoinTally = 0;
		OneUp_();
	}
	AddScoreCode_(0x42);                        /* CoinPoints */
}

__attribute__((noipa)) void AddToScore(void){
	Y_ = DD_SCORE;
	DigitsMathRoutine();
	TAILJMP(GetSBNybbles);
}

__attribute__((noipa)) void GetSBNybbles(void){
#if TWO_PLAYERS
	Y_ = 0;                                     /* (StatusBarNybbles' first entry: the live score, smb.h) */
#else
	Y_ = CurrentPlayer;
#endif
	A_ = SB_NYBBLES;
	TAILJMP(UpdateNumber);
}

/* UpdateNumber: prints the numbers the nybbles in A_ select. The 6502's zero suppression of
** the score's top digit in VRAM_Buffer1 is gfx17.c's: it redraws the bar from the digits */
void UpdateNumber(void){
	PrintStatusBarNumbers();
	X_ = ObjectOffset;
}

/* ---------------------------------------------------------------------------------------
** Power-ups (7125-7210)
** ------------------------------------------------------------------------------------ */

/* SetupPowerUp: power-up type t rises out of block object x, in enemy slot 5 */
static inline __attribute__((always_inline)) void SetupPowerUp_(u8 x, u8 t){
	PowerUpType = t;
	Enemy_ID[5] = PowerUpObject;
	Place_(OFS_ENEMY + 5, SPos_(x + OFS_BLOCK), Block_Y_Position[x] - 8);
	PwrUpJmp();
}

void PwrUpJmp(void){
	Enemy_State[5] = 1;
	Enemy_Flag[5] = 1;
	Enemy_BoundBoxCtrl[5] = 0x03;
	if(PowerUpType < 2){                        /* a mushroom, or a flower if fiery already */
		u8 a = PlayerStatus;
		if(a >= 2) a >>= 1;
		PowerUpType = a;
	}
	Enemy_SprAttrib[5] = 0x20;                  /* PutBehind */
	Square2SoundQueue = Sfx_GrowPowerUp;
}


/* PowerUpObjHandler: grows the power-up out of its block, then moves it */
__attribute__((noipa)) void PowerUpObjHandler(void){
	ObjectOffset = X_ = K1(5);
	u8 st = Enemy_State[5];
	if(!st) return;
	if(st & 0x80){
		if(!TimerControl){
			u8 t = PowerUpType;
			if(t == 0 || t == 3){               /* ShroomM */
				MoveNormalEnemy();
				EnemyToBGCollisionDet();
			}else if(t == 2){                   /* the star jumps */
				MoveJumpingEnemy();
				EnemyJump();
			}
		}
	}else{
		if(!(FrameCounter & 3)){                /* GrowThePowerUp */
			Enemy_Y_Position[5]--;
			u8 *e = OPQ(&Enemy_State[5]);
			e[0] = ++st;
			if(st > 0x11){
				e[0] = st = 0x80;
				AT(e, Enemy_State[5], Enemy_X_Speed[5]) = 0x10;
				AT(e, Enemy_State[5], Enemy_MovingDir[5]) = 1;   /* asl then rol of $80 */
				Enemy_SprAttrib[5] = 0;
			}
		}
		if(st < 6) return;                      /* ChkPUSte */
	}
	X_ = 5;                                     /* RunPUSubs */
	RelativeEnemyPosition();
	GetEnemyOffscreenBits();
	GetEnemyBoundBox();
	DrawPowerUp();
	PlayerEnemyCollision();
	TAILJMP(OffscreenBoundsCheck);
}

/* ---------------------------------------------------------------------------------------
** Block objects (7219-7526)
** ------------------------------------------------------------------------------------ */

/* BlockBumpedChk: is metatile a a question block or a brick that holds something: C_, Y_ */
static u8 BlockBumpedChk_(u8 a){
	s8 y = 13;
	do{
		if(a == rd(BrickQBlockMetatiles, y)) break;
	}while(--y >= 0);
	C_ = y >= 0;
	return Y_ = y;
}

/* InitBlock_XY_Pos: block object X_ at the player's column (16-pixel aligned) */
void InitBlock_XY_Pos(void){
	u8 x = X_;
	u16 t = SPos_(0) + 8;
	u8 *b = SOP(Block_PageLoc, x);
	b[RO(SprObject_PageLoc, SprObject_X_Position)] = t & 0xf0;
	b[0] = t >> 8;
	Block_PageLoc2[x] = t >> 8;
	b[RO(SprObject_PageLoc, SprObject_Y_HighPos)] = Player_Y_HighPos;
}

/* BumpBlock: bounces block object x and releases what the bumped metatile ($05) held */
static inline __attribute__((always_inline)) void BumpBlock_(u8 x){
	Square1SoundQueue = Sfx_Bump;
	u8 *p = OPQ(SOP(Block_X_Speed, x));
	p[0] = 0;
	Block_Y_MoveForce[x] = 0;
	Player_Y_Speed = 0;
	p[RO(SprObject_X_Speed, SprObject_Y_Speed)] = -2;
	u8 m = M05;
	A_ = m;
	u8 y = BlockBumpedChk_(m);
	if(!C_) return;                             /* ExitBlockChk */
	C_ = false;                                 /* JumpEngine's asl of A < $80; X_ = x */
	u8 c = rd(BlockCodeData, y);                /* BlockCode */
	if(c == BC_COIN){                           /* CoinBlock */
		/* the carry: clear from the dispatch, set by FindEmptyMiscSlot unless the first slot
		** it looked at was free; the coin starts 17 pixels up then, 16 otherwise */
		JCoinC_(x, p[RO(SprObject_X_Speed, SprObject_PageLoc)], p[RO(SprObject_X_Speed, SprObject_X_Position)] | 0x05,
		        Block_Y_Position[x] - 0x10, true);
	}else if(c == BC_VINE){                     /* VineBlock */
		X_ = 5;
		Y_ = x;
		Setup_Vine();
	}else SetupPowerUp_(x, c);                  /* MushFlower, Star, ExtraLifeMush */
}

/* BrickShatter: the big player breaks brick x: two pairs of chunks, 50 points */
static inline __attribute__((always_inline)) void BrickShatter_(u8 x, u8 *q){
	AT(q, Block_Orig_YPos[x], Block_RepFlag[x]) = Sfx_BrickShatter;
	NoiseSoundQueue = Sfx_BrickShatter;
	/* SpawnBrickChunks: block objects x and x + 2 become the two pairs of chunks */
	u8 *p = OPQ(SOP(SprObject_X_Speed, OFS_BLOCK + x));
#define BPG RO(SprObject_X_Speed, SprObject_PageLoc)
#define BXP RO(SprObject_X_Speed, SprObject_X_Position)
#define BYS RO(SprObject_X_Speed, SprObject_Y_Speed)
	u8 v = p[BXP];
	AT(q, Block_Orig_YPos[x], Block_Orig_XPos[x]) = v;
	p[BXP + 2] = v;
	p[0] = 0xf0;
	p[2] = 0xf0;
	p[BYS] = -6;
	p[BYS + 2] = -4;
	p[BPG + 2] = p[BPG];
#undef BPG
#undef BXP
#undef BYS
	Block_Y_MoveForce[x] = 0;
	Block_Y_MoveForce[x + 2] = 0;
	Block_Y_Position[x + 2] = Block_Y_Position[x] + 8;   /* the Y high byte is not copied */
	Player_Y_Speed = -2;
	AddScoreCode_(0x55);                        /* 50 points */
	X_ = x;
}

/* PlayerHeadCollision: the player bumped block A_ at M02 (row x 16) of column BBPtr/M06. Then
** CheckTopOfBlock (a coin above jumps out) and BumpBlock (small: it bounces and releases what
** it held) or BrickShatter (big: chunks, 50 points); InvOBit last */
void PlayerHeadCollision(void){
	M05 = A_;                                   /* pha .. pla: nothing here writes $05 */
	u8 x = SprDataOffset_Ctrl;
	u8 small = PlayerSize;
	u8 *q = OPQ(&Block_Orig_YPos[x]);           /* the Block_ arrays of two */
#define QB(f) AT(q, Block_Orig_YPos[x], f[x])
	X_ = x;
	DestroyBlockMetatile();
	u8 r = M02;
	q[0] = r;
	QB(Block_BBuf_Low) = M06;
	u8 m = BBPtr[r >> 4];
	BlockBumpedChk_(m);
	M00 = m;
	u8 st = small ? 0x11 : 0x12;                /* small bumps, big breaks */
	u8 a = small ? m : 0;                       /* big: tya, 0 */
	if(C_){                                     /* a question block or a coin brick */
		st = 0x11;
		a = 0xc4;
		if(m == 0x58 || m == 0x5d){             /* StartBTmr: bricks with coins */
			if(!BrickCoinTimerFlag){
				BrickCoinTimer = 0x0b;
				BrickCoinTimerFlag = 1;
			}
			if(BrickCoinTimer) a = m;           /* ContBTmr: expired, it empties */
		}
	}
	Block_State[x] = st;
	QB(Block_Metatile) = a;                      /* PutMTileB */
	X_ = x;
	InitBlock_XY_Pos();
	bb_Set(r >> 4, 0x23);
	BlockBounceTimer = 0x10;
	u8 add = (CrouchingFlag || small) ? 0x12 : 0x04;
	Block_Y_Position[x] = (u8)(Player_Y_Position + add) & 0xf0;
	/* CheckTopOfBlock (X_ = x still): a coin directly above the bumped block jumps out */
	if(r){                                      /* not the top row */
		r -= 0x10;
		M02 = r;
		if(BBPtr[r >> 4] == 0xc2){              /* a coin (not underwater) */
			bb_Set(r >> 4, 0);
			RemoveCoin_Axe();
			/* PutBlockMetatile left the name table's high address byte in $05, and BumpBlock
			** reads it: a coin above the block replaces the bumped metatile there, so the block
			** gives nothing (gfx17.c does not write the scratch bytes: it is made here). M02 is a
			** multiple of 16, so the low byte's add never carries */
			u8 m6 = M06;
			r += 0x20;
			M05 = 0x20 + ((m6 & 0x10) >> 2) + (r >> 6);
			/* SetupJumpCoin: the 4th asl of $06 left its bit 4 (the second buffer) in the carry */
			JCoinC_(x, QB(Block_PageLoc2), (u8)(m6 << 4) | 0x05, r + ((m6 >> 4) & 1), false);
		}
	}
	if(st == 0x11) BumpBlock_(x);
	else BrickShatter_(x, q);
#undef QB
	SprDataOffset_Ctrl = x ^ 1;                 /* InvOBit */
}

/* BlockObjectsCore: block object X_ (= ObjectOffset), bouncing or two pairs of chunks.
** The state's high nybble is dropped on the first update, as the 6502 did */
void BlockObjectsCore(void){
	u8 x = X_;
	u8 st = Block_State[x];
	if(!st) return;
	st &= 0x0f;
	X_ = x + OFS_BLOCK;
	ImposeGravityBlock();
	if(st != 1){
		MoveObjectHorizontally();
		X_ += 2;
		ImposeGravityBlock();
		MoveObjectHorizontally();
	}
	X_ = x;
	RelativeBlockPosition();
	GetBlockOffscreenBits();
	u8 *h = OPQ(&Block_Y_HighPos[x]);
#define BY(i) h[RO(SprObject_Y_HighPos, SprObject_Y_Position) + (i)]
	if(st == 1){                                /* BouncingBlockHandler */
		DrawBlock();
		if((BY(0) & 0x0f) < 0x05){
			Block_RepFlag[x] = 1;
			st = 0;                             /* KillBlock */
		}
	}else{
		DrawBrickChunks();
		/* above the screen: the 6502 stores the state back, it does not kill the chunks */
		if(h[0]){
			if(BY(2) > 0xf0) BY(2) = 0xf0;
			if(BY(0) >= 0xf0) st = 0;           /* ChkTop */
		}
	}
#undef BY
	Block_State[x] = st;                        /* UpdSte */
}

/* BlockObjMT_Updater: puts a finished block object's metatile back, one a frame */
void BlockObjMT_Updater(void){
	for(s8 x = 1; x >= 0; x--){
		ObjectOffset = x;
		if(VRAM_Buffer1_Pending()) continue;
		if(!Block_RepFlag[x]) continue;
		u8 c = Block_BBuf_Low[x];
		M06 = c;
		bb_Select(c);
		u8 r = Block_Orig_YPos[x];
		M02 = r;
		u8 mt = Block_Metatile[x];
		bb_Set(r >> 4, mt);
		A_ = mt;
		X_ = x;
		Y_ = r;
		ReplaceBlockMetatile();
		Block_RepFlag[x] = 0;
	}
}

/* ---------------------------------------------------------------------------------------
** Movement (7530-7759)
** ------------------------------------------------------------------------------------ */

void MoveEnemyHorizontally(void){
	X_++;
	MoveObjectHorizontally();
	X_ = ObjectOffset;
}

__attribute__((noipa)) void MovePlayerHorizontally(void){
	u8 c = JumpspringAnimCtrl;
	if(c){                                      /* returns the control in A */
		A_ = c;
		return;
	}
	X_ = 0;
	TAILJMP(MoveObjectHorizontally);
}

/* MoveObjectHorizontally: X position += X speed / 16 (signed), fraction in X_MoveForce.
** Returns in A_ the whole-pixel move plus the fraction's carry (callers scroll by it) */
void MoveObjectHorizontally(void){
	u8 x = X_;
	u8 *p = OPQ(SOP(SprObject_X_Speed, x));
#define PG RO(SprObject_X_Speed, SprObject_PageLoc)
#define XP RO(SprObject_X_Speed, SprObject_X_Position)
	u8 s = p[0];
	u8 lo = s << 4;
	M01 = lo;
	s8 a = (s8)s >> 4;
	M00 = a;
	u8 hi = a >> 7;
	M02 = hi;
	u8 *f = &SprObject_X_MoveForce[x];
	u8 m = *f + lo;
	*f = m;
	u16 pos = ((u16)p[PG] << 8 | p[XP]) + a;
	u8 r = a;
	if(m < lo){                                 /* the fraction's carry */
		pos++;
		r++;
	}
	p[XP] = pos;
	p[PG] = pos >> 8;
#undef PG
#undef XP
	A_ = r;
	Y_ = hi;
}

__attribute__((noipa)) void MovePlayerVertically(void){
	X_ = 0;
	u8 c = JumpspringAnimCtrl;
	if(!TimerControl && c){                     /* ExXMove */
		A_ = c;
		return;
	}
	M00 = VerticalForce;                        /* NoJSChk */
	A_ = 0x04;
	TAILJMP(ImposeGravitySprObj);
}

/* MoveD_EnemyVertically: $3d, or MoveFallingPlatform's $20 for the spiny's egg (state 5) */
__attribute__((noipa)) void MoveD_EnemyVertically(void){
	Y_ = (Enemy_State[X_] != 0x05) ? 0x3d : 0x20;
	TAILJMP(SetHiMax);
}

__attribute__((noipa)) void MoveFallingPlatform(void){
	Y_ = 0x20;
	TAILJMP(SetHiMax);
}

/* MoveRedPTroopa: the 6502 entered with Y 0 or 1, as A for ImposeGravity (which sets Y) */
static __attribute__((noipa)) void MoveRedPTroopa_(void){
	X_++;
	M00 = 0x03;
	M01 = 0x06;
	M02 = 0x02;
	TAILJMP(RedPTroopaGrav);
}

__attribute__((noipa)) void MoveRedPTroopaDown(void){
	A_ = 0x00;
	TAILJMP(MoveRedPTroopa_);
}

__attribute__((noipa)) void MoveRedPTroopaUp(void){
	A_ = 0x01;
	TAILJMP(MoveRedPTroopa_);
}

__attribute__((noipa)) void MoveDropPlatform(void){
	Y_ = 0x7f;
	A_ = 0x02;
	TAILJMP(SetXMoveAmt);
}

__attribute__((noipa)) void MoveEnemySlowVert(void){
	Y_ = 0x0f;
	A_ = 0x02;
	TAILJMP(SetXMoveAmt);
}

__attribute__((noipa)) void MoveJ_EnemyVertically(void){
	Y_ = 0x1c;
	TAILJMP(SetHiMax);
}

__attribute__((noipa)) void SetHiMax(void){
	A_ = 0x03;
	TAILJMP(SetXMoveAmt);
}

/* SetXMoveAmt: ImposeGravitySprObj on X_ + 1 (Y_ down, A_ maximum), then X_ = ObjectOffset */
__attribute__((noipa)) void SetXMoveAmt(void){
	M00 = Y_;
	X_++;
	M02 = A_;                                   /* ImposeGravitySprObj, into RedPTroopaGrav */
	A_ = 0;
	TAILJMP(RedPTroopaGrav);
}

__attribute__((noipa)) void ImposeGravityBlock(void){
	M00 = 0x50;                                 /* the 6502's ldy #1: ImposeGravity sets Y */
	A_ = 0x08;                                  /* MaxSpdBlockData + 1 */
	TAILJMP(ImposeGravitySprObj);
}

__attribute__((noipa)) void ImposeGravitySprObj(void){
	M02 = A_;
	A_ = 0;
	TAILJMP(ImposeGravity);
}

/* MovePlatformDown / MovePlatformUp: the 6502 entered one body with A (and Y, which
** ImposeGravity sets) 0 or 1 */
static __attribute__((noipa)) void MovePlatform_(void){
	u8 id = Enemy_ID[X_];
	X_++;
	M00 = (id == 0x29) ? 0x09 : 0x05;          /* object $29 never comes here (residual) */
	M01 = 0x0a;
	M02 = 0x03;
	TAILJMP(RedPTroopaGrav);
}

__attribute__((noipa)) void MovePlatformDown(void){
	A_ = 0;
	TAILJMP(MovePlatform_);
}

__attribute__((noipa)) void MovePlatformUp(void){
	A_ = 1;
	TAILJMP(MovePlatform_);
}

void RedPTroopaGrav(void){
	ImposeGravity();
	X_ = ObjectOffset;
}

/* ImposeGravity: moves SprObject X_ by its Y speed (with the fractional dummy), then adds the
** downward force M00 to the speed up to M02; with A_ nonzero also the upward force M01, down
** to -M02. The limits test the sign of speed - limit (bmi/bpl after cmp), as the 6502 did */
void ImposeGravity(void){
	u8 up = A_;
	u8 x = X_;
	u8 *f = OPQ(SOP(SprObject_YMF_Dummy, x));
	u8 *s = OPQ(SOP(SprObject_Y_Speed, x));
#define MF RO(SprObject_YMF_Dummy, SprObject_Y_MoveForce)
#define HP RO(SprObject_Y_Speed, SprObject_Y_HighPos)
#define YP RO(SprObject_Y_Speed, SprObject_Y_Position)
	u8 mf = f[MF];
	u8 d = f[0] + mf;
	f[0] = d;
	u8 sp = s[0];
	u8 y = (s8)sp >> 7;
	M07 = y;
	u16 pos = ((u16)s[HP] << 8 | s[YP]) + (s8)sp;
	if(d < mf) pos++;
	s[YP] = pos;
	s[HP] = pos >> 8;
	d = mf + M00;
	f[MF] = d;
	if(d < mf) sp++;
	s[0] = sp;
	u8 m = M02;
	if(!((u8)(sp - m) & 0x80) && (s8)d < 0){
		s[0] = m;
		f[MF] = 0;
	}
	if(up){                                     /* ChkUpM */
		u8 neg = -m;
		y = neg;
		M07 = neg;
		u8 fo = f[MF];
		u8 m1 = M01;
		f[MF] = fo - m1;
		sp = s[0];
		if(fo < m1) sp--;
		s[0] = sp;
		if(((u8)(sp - neg) & 0x80) && (s8)f[MF] >= 0){
			s[0] = neg;
			f[MF] = 0xff;
		}
	}
	Y_ = y;
#undef MF
#undef HP
#undef YP
}
