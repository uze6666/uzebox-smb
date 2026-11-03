/*
 * The enemies: a port of smbdis.asm lines 7763-10066 -- the loop commands, loading enemies
 * from the area's enemy data, the InitXxx routines, the per-frame run and movement code, and
 * the firebar's position and collision. Routine names are the 6502's; a typed helper of a
 * label is the label with a trailing underscore (x: the enemy slot 0-5; the run and movement
 * routines reached through the tables take it in X_). See smb.h.
 */
#include <avr/pgmspace.h>
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

/* a byte of the enemy data: in the flash, or with SD=1 in SPI RAM (area.c, sd17.c) */
#if SD
#define ED(i) sd_byte(EnemyData + (u8)(i))
#else
#define ED(i) pgm_read_byte(EnemyData + (u8)(i))
#endif
#define RD(t, i) pgm_read_byte(&(t)[i])
/* call a routine of another module on enemy slot x */
#define CX(f) do{ X_ = x; f(); }while(0)

/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
static const u8 LoopCmdWorldNumber[11] PROGMEM = { ROM_LoopCmdWorldNumber };
static const u8 LoopCmdPageNumber[11] PROGMEM = { ROM_LoopCmdPageNumber };
static const u8 LoopCmdYPosition[11] PROGMEM = { ROM_LoopCmdYPosition };
/* the area parser's table (line 4351), only read here */
static const u8 AreaDataOfsLoopback[11] PROGMEM = { ROM_AreaDataOfsLoopback };

static const u8 PRDiffAdjustData[12] PROGMEM = { ROM_PRDiffAdjustData };
static const u8 FlyCCXPositionData[16] PROGMEM = { ROM_FlyCCXPositionData };
static const u8 FlyCCXSpeedData[12] PROGMEM = { ROM_FlyCCXSpeedData };
static const u8 FlyCCTimerData[4] PROGMEM = { ROM_FlyCCTimerData };
const u8 FlameYPosData[4] PROGMEM = { ROM_FlameYPosData };
/* FireworksXPosData then FireworksYPosData, contiguous as in the 6502 */
static const u8 FireworksXPosData[12] PROGMEM = { ROM_FireworksXPosData };
#define FireworksYPosData (FireworksXPosData + 6)
static const u8 Enemy17YPosData[8] PROGMEM = { ROM_Enemy17YPosData };

/* horizontal adder at the part's offset + the spin state, vertical at + 8, oscillated */
static const u8 FirebarPosLookupTbl[99] PROGMEM = { ROM_FirebarPosLookupTbl };
static const u8 FirebarMirrorData[4] PROGMEM = { ROM_FirebarMirrorData };
static const u8 FirebarTblOffsets[12] PROGMEM = { ROM_FirebarTblOffsets };

/* PRandomSubtracter is read with offsets 0-15: past its 5 bytes the 6502 read FlyCCBPriority
** and then MoveFlyingCheepCheep's own code (lda $1e,x / and #$20 / beq +8 / lda #0 /
** sta $03c5,x / jmp...), which are reproduced here; FlyCCBPriority is read the same way */
static const u8 PRandomSubtracter[21] PROGMEM = { ROM_PRandomSubtracter };   /* PRandomSubtracter; FlyCCBPriority */
#define FlyCCBPriority (PRandomSubtracter + 5)

/* ---------------------------------------------------------------------------------------
** Helpers
** ------------------------------------------------------------------------------------ */
/* An enemy's fields (or a struct's scalars) through one base pointer, so gcc can use ldd/std
** with a displacement: EBASE(x, B) puts it at B[x], f[x] is then f[x]; GBASE(V) puts it at
** V, w is then w. B and f (V and w) must be in one RAM struct (ra, rab, rac, rg, rp): a
** static assert checks the distance is a constant. The asm hides the pointer's origin, or gcc
** folds it back into each address. Only an addressing mode: it computes the same address */
static inline u8 *ebase_(u8 *p){
	__asm__("" : "+b"(p));
	return p;
}

/* the same in Z (no push of Y; for a stretch with no call) */
static inline u8 *zbase_(u8 *p){
	__asm__("" : "+z"(p));
	return p;
}
#define EOFS_(f, b) ((__UINTPTR_TYPE__)((f) + 0) - (__UINTPTR_TYPE__)((b) + 0))
#define EOK_(f, b) _Static_assert(__builtin_constant_p(EOFS_(f, b)), "EP/G: one RAM struct")
#define EBASE(x, B) u8 *const eb_ = ebase_((u8 *)(B) + (x))
#define EP(B, f) (*({ EOK_(f, B); (__typeof__(&(f)[0]))(eb_ + EOFS_(f, B)); }))
#define EBASE2(x, B) u8 *const eb2_ = ebase_((u8 *)(B) + (x))
#define EP2(B, f) (*({ EOK_(f, B); (__typeof__(&(f)[0]))(eb2_ + EOFS_(f, B)); }))
/* slot x's fields through two bases: EF(f) for Enemy_Flag to Enemy_X_Speed ($0f-$58 and the
** arrays between), EX(f) for Enemy_X_Position to Enemy_Y_Position ($87-$d3) */
#define EFB(x) u8 *const ef_ = ebase_((u8 *)Enemy_Flag + (x))
#define EF(f) (*({ EOK_(f, Enemy_Flag); (__typeof__(&(f)[0]))(ef_ + EOFS_(f, Enemy_Flag)); }))
#define EXB(x) u8 *const ex_ = ebase_((u8 *)Enemy_X_Position + (x))
#define EX(f) (*({ EOK_(f, Enemy_X_Position); (__typeof__(&(f)[0]))(ex_ + EOFS_(f, Enemy_X_Position)); }))
#define GBASE(V) u8 *const gb_ = ebase_((u8 *)&(V))
#define GBASEZ(V) u8 *const gb_ = zbase_((u8 *)&(V))
#define G(V, w) (*({ EOK_(&(w), &(V)); (__typeof__(&(w)))(gb_ + EOFS_(&(w), &(V))); }))

static void InitEnemyObject_(u8 x);
static void RunEnemyObjectsCore_(void);
static void CheckpointEnemyID_(u8 x);
static void EraseEnemyObject_(u8 x);
static void PutAtRightExtent_(u8 x, u8 a);
static u8 PlayerLakituDiff_(u8 x);

/* the enemy's 16-bit horizontal position (page, x) */
static u16 EnemyX(u8 x){
	EBASE(x, Enemy_PageLoc);
	return (EP(Enemy_PageLoc, Enemy_PageLoc) << 8) | EP(Enemy_PageLoc, Enemy_X_Position);
}

static void SetEnemyX(u8 x, u16 p){
	EBASE(x, Enemy_PageLoc);
	EP(Enemy_PageLoc, Enemy_X_Position) = p;
	EP(Enemy_PageLoc, Enemy_PageLoc) = p >> 8;
}

static void AddEnemyX(u8 x, u16 d){
	SetEnemyX(x, EnemyX(x) + d);
}

/* ---------------------------------------------------------------------------------------
** EnemiesAndLoopsCore, the loop commands and the enemy data
** ------------------------------------------------------------------------------------ */

/* the screen's right edge (page, x) */
static u16 ScreenRightX(void){
	return (ScreenRight_PageLoc << 8) | ScreenRight_X_Pos;
}

/* Inc2B / Inc3B: past the enemy object (2 bytes, 3 for an area pointer) */
static inline __attribute__((always_inline)) void IncB_(u8 n){
	GBASE(EnemyDataOffset);
	G(EnemyDataOffset, EnemyDataOffset) += n;
	G(EnemyDataOffset, EnemyObjectPageSel) = 0;
}

/* ParseRow0e: an area pointer (row $0e): the pipe's destination when for this world */
static inline __attribute__((always_inline)) void ParseRow0e_(void){
	u8 y = EnemyDataOffset;
	u8 b = ED(y + 2);
	if((b >> 5) == WorldNumber){
		AreaPointer = ED(y + 1);
		EntrancePage = b & 0x1f;
	}
	IncB_(3);
}

/* CheckFrenzyBuffer: loads the frenzy enemy, or the vine's second object, into slot x */
static inline __attribute__((always_inline)) void CheckFrenzyBuffer_(u8 x){
	u8 a = EnemyFrenzyBuffer;
	if(!a){
		if(VineFlagOffset != 1) return;
		a = VineObject;
	}
	Enemy_ID[x] = a;
	InitEnemyObject_(x);
}

/* HandleGroupEnemies: a = $37-$3e, two or three goombas or koopas at the screen's right edge
** (the scratch bytes M00-M03 and the count are written back at the end: nothing between
** reads them) */
static void HandleGroupEnemies_(u8 a){
	a -= 0x37;
	u8 id = GreenKoopa;
	if(a < 4) id = PrimaryHardMode ? BuzzyBeetle : Goomba;
	u8 yp = (a & 0x02) ? 0x70 : 0xb0;
	u16 p = ScreenRightX();
	u8 n = (a & 0x01) ? 3 : 2;
	do{
		u8 x = 0;
		while(Enemy_Flag[x]){
			if(++x >= 5) goto done;
		}
		Enemy_ID[x] = id;
		SetEnemyX(x, p);
		p += 0x18;
		Enemy_Y_Position[x] = yp;
		Enemy_Y_HighPos[x] = 1;
		Enemy_Flag[x] = 1;
		CheckpointEnemyID_(x);
	}while(--n);
done:
	NumberofGroupEnemies = n;
	M00 = yp;
	M01 = id;
	M02 = p >> 8;
	M03 = p;
	IncB_(2);
}

/* PositionEnemyObj: object b, b1 at its page and column, skipped left of the screen, frenzy
** past r (M06/M07: the right edge + 48), else placed and initialised */
static inline __attribute__((always_inline)) void PositionEnemyObj_(u8 x, u8 b, u8 b1, u16 r){
	u16 pos = (EnemyObjectPageLoc << 8) | (b & 0xf0);
	SetEnemyX(x, pos);
	bool row0e = (b & 0x0f) == 0x0e;
	if(pos < ScreenRightX()){                   /* left of the right edge: skipped */
		if(row0e) goto row;
		IncB_(2);
		return;
	}
	/* CheckRightExtBounds */
	if(r < pos){
		CheckFrenzyBuffer_(x);
		return;
	}
	Enemy_Y_HighPos[x] = 1;
	Enemy_Y_Position[x] = b << 4;
	if(row0e) goto row;
	if((b1 & 0x40) && !SecondaryHardMode){      /* a hard mode object */
		IncB_(2);
		return;
	}
	/* CheckForEnemyGroup */
	u8 id = b1 & 0x3f;
	if((u8)(id - 0x37) < 8){
		HandleGroupEnemies_(id);
		return;
	}
	/* BuzzyBeetleMutate */
	if(id == Goomba && PrimaryHardMode) id = BuzzyBeetle;
	Enemy_ID[x] = id;
	Enemy_Flag[x] = 1;
	InitEnemyObject_(x);
	if(Enemy_Flag[x]) IncB_(2);
	return;
row:
	ParseRow0e_();
}

/* ProcessEnemyData: the next object of the enemy data, into slot x when it is in range; true
** where the 6502 jumped back to ProcLoopCommand (a page control was read) */
static inline __attribute__((always_inline)) bool ProcessEnemyData_(u8 x){
	u8 y = EnemyDataOffset;
	u8 b = ED(y);
	if(b == 0xff){                              /* end of the data */
		CheckFrenzyBuffer_(x);
		return false;
	}
	u8 b1 = ED(y + 1);
	u8 lo = b & 0x0f;
	/* CheckEndofBuffer: the sixth slot only takes area pointers and object $2e */
	if(lo != 0x0e && x >= 5 && (b1 & 0x3f) != 0x2e) return false;
	/* CheckRightBounds: the right edge + 48 pixels, column and page, in M07/M06 */
	u16 r = (ScreenRightX() + 0x30) & 0xfff0;
	M07 = r;
	M06 = r >> 8;
	if(!EnemyObjectPageSel){
		if(b1 & 0x80){
			EnemyObjectPageSel = 1;
			EnemyObjectPageLoc++;
		}else if(lo == 0x0f){                   /* CheckPageCtrlRow */
			EnemyObjectPageLoc = b1 & 0x3f;
			EnemyDataOffset = y + 2;
			EnemyObjectPageSel = 1;
			return true;
		}
	}
	PositionEnemyObj_(x, b, b1, r);
	return false;
}

/* ExecGameLoopback: everything four pages back, the area data from loop command y's offset */
static void ExecGameLoopback_(u8 y){
	u8 a = RD(AreaDataOfsLoopback, y);
	GBASEZ(ScreenLeft_PageLoc);
#define AP(w) G(ScreenLeft_PageLoc, w)
	Player_PageLoc -= 4;
	AP(CurrentPageLoc) -= 4;
	AP(ScreenLeft_PageLoc) -= 4;
	AP(ScreenRight_PageLoc) -= 4;
	AP(AreaObjectPageLoc) -= 4;
	AP(EnemyObjectPageSel) = 0;
	AP(AreaObjectPageSel) = 0;
	AP(EnemyDataOffset) = 0;
	AP(EnemyObjectPageLoc) = 0;
	AP(AreaDataOffset) = a;
#undef AP
}

/* ProcLoopCommand: the loop commands (the castle mazes), then the frenzy queue or the enemy
** data, for empty slot x */
static inline __attribute__((always_inline)) void ProcLoopCommand_(u8 x){
	do{
		if(LoopCommand && !CurrentColumnPos){
			for(u8 y = 10; (s8)y >= 0; y--){
				if(WorldNumber != RD(LoopCmdWorldNumber, y) || CurrentPageLoc != RD(LoopCmdPageNumber, y)) continue;
				bool right = Player_Y_Position == RD(LoopCmdYPosition, y) && Player_State == 0;
				u8 act;                         /* 0 InitLCmd, 1 InitMLp, 2 DoLpBack */
				if(WorldNumber == World7){
					if(right) MultiLoopCorrectCntr++;
					if(++MultiLoopPassCntr != 3) act = 0;
					else act = (MultiLoopCorrectCntr == 3) ? 1 : 2;
				}else{
					act = right ? 1 : 2;
				}
				if(act == 2){
					ExecGameLoopback_(y);
					X_ = x;
					KillAllEnemies();
				}
				if(act){
					MultiLoopPassCntr = 0;
					MultiLoopCorrectCntr = 0;
				}
				LoopCommand = 0;
				break;
			}
		}
		/* ChkEnemyFrenzy */
		u8 q = EnemyFrenzyQueue;
		if(q){
			Enemy_ID[x] = q;
			Enemy_Flag[x] = 1;
			Enemy_State[x] = 0;
			EnemyFrenzyQueue = 0;
			InitEnemyObject_(x);
			return;
		}
	}while(ProcessEnemyData_(x));
}

/* EnemiesAndLoopsCore: runs the enemy in slot X_, or loads one into it when empty */
void EnemiesAndLoopsCore(void){
	u8 x = X_;
	EBASE(x, Enemy_Flag);
	u8 f = EP(Enemy_Flag, Enemy_Flag);
	if(f & 0x80){                               /* ChkBowserF: a duplicate dies with its original */
		if(!Enemy_Flag[f & 0x0f]) EP(Enemy_Flag, Enemy_Flag) = 0;
	}else if(f){
		RunEnemyObjectsCore_();
	}else if((AreaParserTaskNum & 0x07) != 0x07){
		ProcLoopCommand_(x);
	}
}

/* ---------------------------------------------------------------------------------------
** Enemy initialisation
** ------------------------------------------------------------------------------------ */

static void InitEnemyObject_(u8 x){
	Enemy_State[x] = 0;
	CheckpointEnemyID_(x);
}

/* InitVStf: no vertical speed or force */
static void InitVStf_(u8 x){
	Enemy_Y_Speed[x] = 0;
	Enemy_Y_MoveForce[x] = 0;
}

void InitVStf(void){
	InitVStf_(X_);
	A_ = nz(0);
}

/* SetBBox: the bounding box control, moving left, no vertical speed (TallBBox 3, SmallBBox 9) */
static void SetBBox_(u8 x, u8 a){
	Enemy_BoundBoxCtrl[x] = a;
	Enemy_MovingDir[x] = 2;
	InitVStf_(x);
}

/* SetESpd: a horizontal speed then a SetBBox */
static void SetESpd_(u8 x, s8 s, u8 box){
	Enemy_X_Speed[x] = s;
	SetBBox_(x, box);
}

static void InitNormalEnemy_(u8 x){
	u8 id = Enemy_ID[x];
	SetESpd_(x, PrimaryHardMode ? (s8)0xf4 : (s8)0xf8, (id == Goomba) ? 0x09 : 0x03);   /* NormalXSpdData */
	if(id == RedKoopa) Enemy_State[x] = 1;      /* InitRedKoopa */
}

static void InitPodoboo_(u8 x){
	EBASE(x, Enemy_Y_HighPos);
	EP(Enemy_Y_HighPos, Enemy_Y_HighPos) = 2;                     /* below the screen */
	EP(Enemy_Y_HighPos, Enemy_Y_Position) = 2;
	EnemyIntervalTimer[x] = 1;
	Enemy_State[x] = 0;
	SetBBox_(x, 0x09);
}

static void InitRetainerObj_(u8 x){
	Enemy_Y_Position[x] = 0xb8;
}

static void InitHammerBro_(u8 x){
	HammerThrowingTimer[x] = 0;
	EnemyIntervalTimer[x] = SecondaryHardMode ? 0x50 : 0x80;   /* HBroWalkingTimerData */
	SetESpd_(x, 0, 0x0b);
}

static void InitHorizFlySwimEnemy_(u8 x){
	SetESpd_(x, 0, 0x03);
}

static void InitBloober_(u8 x){
	SetESpd_(x, 0, 0x09);                       /* BlooperMoveSpeed 0, SmallBBox */
}

/* InitRedPTroopa: centre 48 pixels down, or 32 up when low (the jump engine left carry 0) */
static void InitRedPTroopa_(u8 x){
	u8 p = Enemy_Y_Position[x];
	RedPTroopaOrigXPos[x] = p;
	SetESpd_(x, ((p & 0x80) ? 0xe0 : 0x30) + p, 0x03);   /* RedPTroopaCenterYPos */
}

static void InitBulletBill_(u8 x){
	Enemy_MovingDir[x] = 2;
	Enemy_BoundBoxCtrl[x] = 0x09;
}

static void InitCheepCheep_(u8 x){
	SetESpd_(x, PseudoRandomBitReg[x] & 0x10, 0x09);   /* CheepCheepMoveMFlag, SmallBBox */
	CheepCheepOrigYPos[x] = Enemy_Y_Position[x];
}

static void SetupLakitu_(u8 x){
	LakituReappearTimer = 0;
	InitHorizFlySwimEnemy_(x);                  /* and TallBBox2 */
}

static void InitLakitu_(u8 x){
	if(EnemyFrenzyBuffer) EraseEnemyObject_(x);     /* KillLakitu */
	else SetupLakitu_(x);
}

/* CreateSpiny: a spiny egg in slot x, thrown up from the lakitu in slot y */
static inline __attribute__((always_inline)) void CreateSpiny_(u8 x, u8 y){
	if(Player_Y_Position < 0x2c || Enemy_State[y]) return;
	SetEnemyX(x, EnemyX(y));
	Enemy_Y_HighPos[x] = 1;
	Enemy_Y_Position[x] = Enemy_Y_Position[y] - 8;
	const u8 *p = PRDiffAdjustData + (PseudoRandomBitReg[x] & 0x03);
	for(u8 i = 3; i; i--, p += 4) ra.M_[i] = pgm_read_byte(p);   /* M03, M02, M01 */
	x = ObjectOffset;
	PlayerLakituDiff_(x);
	/* the 6502 then chose a speed from the difference, but SmallBBox loses it in A: the egg's
	** horizontal speed is 0 and it moves right */
	EFB(x);
	Enemy_BoundBoxCtrl[x] = 0x09;
	Enemy_Y_MoveForce[x] = 0;
	EF(Enemy_X_Speed) = 0;
	EF(Enemy_MovingDir) = 1;
	Enemy_Y_Speed[x] = (s8)0xfd;
	EF(Enemy_Flag) = 1;
	EF(Enemy_State) = 5;                        /* the egg state */
}

/* InitEnemyFrenzy ($12): LakituAndSpinyHandler: an egg from the lakitu, or a lakitu back */
static void InitFrenzyLakitu_(u8 x){
	if(x >= 5) return;
	FrenzyEnemyTimer = 0x80;
	for(u8 y = 4; (s8)y >= 0; y--){
		if(Enemy_ID[y] == Lakitu){
			CreateSpiny_(x, y);
			return;
		}
	}
	if(++LakituReappearTimer < 7) return;
	for(u8 s = 4; (s8)s >= 0; s--){
		if(!Enemy_Flag[s]){                     /* CreateL */
			Enemy_State[s] = 0;
			Enemy_ID[s] = Lakitu;
			SetupLakitu_(s);
			PutAtRightExtent_(s, 0x20);
			return;
		}
	}
}

/* DuplicateEnemyObj: a second object for the one in slot x, flagged with x | $80 */
static void DuplicateEnemyObj_(u8 x){
	u8 y = 0;
	while(y < ENEMY_SLOTS && Enemy_Flag[y]) y++;   /* the 6502 searched with no bound */
	DuplicateObj_Offset = y;
	Enemy_Flag[y] = x | 0x80;
	SetEnemyX(y, EnemyX(x));
	Enemy_Flag[x] = 1;
	Enemy_Y_HighPos[y] = 1;
	Enemy_Y_Position[y] = Enemy_Y_Position[x];
}

static inline __attribute__((always_inline)) void InitShortFirebar_(u8 x){
	EFB(x);
	EF(FirebarSpinState_Low) = 0;
	u8 y = EF(Enemy_ID) - 0x1b;
	FirebarSpinSpeed[x] = (y & 0x01) ? 0x38 : 0x28;              /* FirebarSpinSpdData */
	EF(FirebarSpinDirection) = ((y >> 1) == 1) ? 0x10 : 0x00;    /* FirebarSpinDirData */
	Enemy_Y_Position[x] += 4;
	AddEnemyX(x, 4);
	Enemy_BoundBoxCtrl[x] = 0x03;               /* TallBBox2 */
}

static void InitLongFirebar_(u8 x){
	DuplicateEnemyObj_(x);
	InitShortFirebar_(x);
}

/* InitEnemyFrenzy ($14): InitFlyingCheepCheep: a cheep-cheep jumping from below, placed and
** aimed around the player (M00, M01 written back as the 6502 left them) */
static void InitFrenzyFlyCC_(u8 x){
	EBASE2(x, Enemy_Flag);
	EBASE(x, Enemy_Y_Speed);
	SetBBox_(x, 0x09);
	u8 r1 = PseudoRandomBitReg[x + 1] & 0x03;
	FrenzyEnemyTimer = RD(FlyCCTimerData, r1);
	u8 y = SecondaryHardMode ? 4 : 3;
	if(x >= y){
		M00 = y;
		return;
	}
	u8 m1 = PseudoRandomBitReg[x] & 0x03;
	EP(Enemy_Y_Speed, Enemy_Y_Speed) = (s8)0xfb;
	u8 a = 0;
	u8 ps = Player_X_Speed;
	if(ps){
		a = 4;
		if(ps >= 0x19) a = 8;
	}
	u8 m0 = m1 + a;
	if(r1) m0 = PseudoRandomBitReg[x + 2] & 0x0f;
	M00 = m0;
	M01 = m1;
	y = a + m1;
	s8 s = RD(FlyCCXSpeedData, y);
	u8 d = 1;
	if(!ps){
		y = m0;
		if(y & 0x02){
			s = -s;
			d = 2;
		}
	}
	EP2(Enemy_Flag, Enemy_X_Speed) = s;
	EP2(Enemy_Flag, Enemy_MovingDir) = d;
	u16 p = (Player_PageLoc << 8) | Player_X_Position;
	u8 o = RD(FlyCCXPositionData, y);
	SetEnemyX(x, (y & 0x02) ? p + o : p - o);  /* D2XPos1 / D2XPos2 */
	EP2(Enemy_Flag, Enemy_Flag) = 1;                          /* FinCCSt: below the screen */
	EP(Enemy_Y_Speed, Enemy_Y_HighPos) = 1;
	EP(Enemy_Y_Speed, Enemy_Y_Position) = 0xf8;
}

static void InitBowser_(u8 x){
	DuplicateEnemyObj_(x);
	BowserFront_Offset = x;
	BowserBodyControls = 0;
	BridgeCollapseOffset = 0;
	BowserOrigXPos = Enemy_X_Position[x];
	BowserFireBreathTimer = 0xdf;
	Enemy_MovingDir[x] = 0xdf;
	BowserFeetCounter = 0x20;
	EnemyFrameTimer[x] = 0x20;
	BowserHitPoints = 5;
	BowserMovementSpeed = 5 >> 1;
}

/* FinishFlame: the flame's (and the frenzy enemies') common values; the 6502 left A 0 */
static void FinishFlame_(u8 x){
	EBASE(x, Enemy_Flag);
	Enemy_BoundBoxCtrl[x] = 0x08;
	Enemy_Y_HighPos[x] = 1;
	EP(Enemy_Flag, Enemy_Flag) = 1;
	Enemy_X_MoveForce[x] = 0;
	EP(Enemy_Flag, Enemy_State) = 0;
}

/* PutAtRightExtent: at height a, 32 pixels past the right edge of the screen */
static void PutAtRightExtent_(u8 x, u8 a){
	Enemy_Y_Position[x] = a;
	SetEnemyX(x, ScreenRightX() + 0x20);
	FinishFlame_(x);
}

/* InitEnemyFrenzy ($15): InitBowserFlame: from Bowser's mouth, or from the right edge when
** there is no Bowser */
static void InitFrenzyFlame_(u8 x){
	EBASE(x, Enemy_X_Position);
	Enemy_Y_MoveForce[x] = 0;
	NoiseSoundQueue |= Sfx_BowserFlame;
	u8 r = PseudoRandomBitReg[x] & 0x03;
	Enemy_YMF_Dummy[x] = r;                     /* BowserFlamePRandomOfs */
	u8 h = RD(FlameYPosData, r);
	u8 y = BowserFront_Offset;
	if(Enemy_ID[y] != Bowser){
		SetFlameTimer();
		u8 a = A_ + 0x20;
		if(SecondaryHardMode) a -= 0x10;
		FrenzyEnemyTimer = a;
		PutAtRightExtent_(x, h);
		return;
	}
	/* SpawnFromMouth */
	EP(Enemy_X_Position, Enemy_X_Position) = Enemy_X_Position[y] - 0x0e;
	Enemy_PageLoc[x] = Enemy_PageLoc[y];
	u8 yp = Enemy_Y_Position[y] + 8;
	EP(Enemy_X_Position, Enemy_Y_Position) = yp;
	Enemy_Y_MoveForce[x] = (h >= yp) ? 0x01 : 0xff;   /* FlameYMFAdderData */
	EnemyFrenzyBuffer = 0;
	FinishFlame_(x);
}

/* InitEnemyFrenzy ($16): InitFireworks: an explosion around the star flag */
static void InitFrenzyFireworks_(u8 x){
	EFB(x);
	EXB(x);
	FrenzyEnemyTimer = 0x20;
	FireworksCounter--;
	u8 y = 5;
	while(Enemy_ID[y] != StarFlagObject){
		if(!y--) return;        /* the 6502 searched on through memory (a crash): none here */
	}
	u16 p = EnemyX(y) - 0x30;
	M00 = p >> 8;
	u8 o = FireworksCounter + Enemy_State[y];
	SetEnemyX(x, p + RD(FireworksXPosData, o));
	EX(Enemy_Y_Position) = RD(FireworksYPosData, o);
	EX(Enemy_Y_HighPos) = 1;
	EF(Enemy_Flag) = 1;
	EF(ExplosionGfxCounter) = 0;
	EX(ExplosionTimerCounter) = 0x08;
}

/* InitEnemyFrenzy ($17): BulletBillCheepCheep: swimming cheep-cheeps (water) or bullet bills,
** at a height not used yet (BitMFilter) at the right edge */
static void InitFrenzyBBCC_(u8 x){
	EBASE(x, PseudoRandomBitReg);
	u8 id;
	if(!AreaType){
		if(x >= 3) return;
		u8 y = EP(PseudoRandomBitReg, PseudoRandomBitReg) >= 0xaa;
		if(WorldNumber != World2) y++;
		id = GreyCheepCheep + (y & 0x01);       /* SwimCC_IDData */
	}else{
		for(u8 y = 0; y < 5; y++){              /* DoBulletBills */
			if(Enemy_Flag[y] && Enemy_ID[y] == BulletBill_FrenzyVar) return;
		}
		Square2SoundQueue |= Sfx_Blast;         /* FireBulletBill */
		id = BulletBill_FrenzyVar;
	}
	/* Set17ID */
	Enemy_ID[x] = id;
	u8 f = BitMFilter;
	if(f == 0xff) f = 0;
	u8 y = EP(PseudoRandomBitReg, PseudoRandomBitReg) & 0x07;
	u8 m;
	while((m = 1 << y) & f) y = (y + 1) & 0x07;   /* ChkRBit (Bitmasks) */
	BitMFilter = f | m;
	PutAtRightExtent_(x, RD(Enemy17YPosData, y));
	Enemy_YMF_Dummy[x] = 0;                     /* A after PutAtRightExtent */
	FrenzyEnemyTimer = 0x20;
	CheckpointEnemyID_(x);
}

static void InitPiranhaPlant_(u8 x){
	EFB(x);
	EF(PiranhaPlant_Y_Speed) = 1;
	EF(Enemy_State) = 0;
	PiranhaPlant_MoveFlag[x] = 0;
	u8 p = Enemy_Y_Position[x];
	PiranhaPlantDownYPos[x] = p;
	PiranhaPlantUpYPos[x] = p - 0x18;
	Enemy_BoundBoxCtrl[x] = 0x09;
}

void InitPiranhaPlant(void){
	InitPiranhaPlant_(X_);
}

/* EndFrenzy: lakitus leave, the frenzy ends */
static void EndFrenzy_(u8 x){
	for(u8 y = 5; (s8)y >= 0; y--){
		if(Enemy_ID[y] == Lakitu) Enemy_State[y] = 1;
	}
	EnemyFrenzyBuffer = 0;
	Enemy_Flag[x] = 0;
}

static void InitJumpGPTroopa_(u8 x){
	EBASE(x, Enemy_MovingDir);
	EP(Enemy_MovingDir, Enemy_MovingDir) = 2;
	EP(Enemy_MovingDir, Enemy_X_Speed) = (s8)0xf8;
	Enemy_BoundBoxCtrl[x] = 0x03;               /* TallBBox2 */
}

/* SPBBox: the platform's bounding box: small in castles and secondary hard mode */
static void SPBBox_(u8 x){
	u8 a = 0x05;
	if(AreaType != 3 && !SecondaryHardMode) a = 0x06;
	Enemy_BoundBoxCtrl[x] = a;
}

static inline __attribute__((always_inline)) void CommonPlatCode_(u8 x){
	InitVStf_(x);
	SPBBox_(x);
}

static void InitDropPlatform_(u8 x){
	PlatformCollisionFlag[x] = 0xff;
	CommonPlatCode_(x);
}

/* InitBalPlatform: one of a pair; BalPlatformAlignment pairs it with the previous one */
static void InitBalPlatform_(u8 x){
	EBASE(x, Enemy_State);
	Enemy_Y_Position[x] -= 2;
	u8 a = BalPlatformAlignment;
	EP(Enemy_State, Enemy_State) = a;
	BalPlatformAlignment = (a & 0x80) ? x : 0xff;
	EP(Enemy_State, Enemy_MovingDir) = 0;
	/* PosPlatform 2 (-8) unless in secondary hard mode, then PosPlatform 0 (+8) */
	if(SecondaryHardMode) AddEnemyX(x, 8);
	InitDropPlatform_(x);
}

static void InitHoriPlatform_(u8 x){
	XMoveSecondaryCounter[x] = 0;
	CommonPlatCode_(x);
}

static void InitVertPlatform_(u8 x){
	u8 y = 0x40, a = Enemy_Y_Position[x];
	if(a & 0x80){
		a = 0 - a;
		y = 0xc0;
	}
	YPlatformTopYPos[x] = a;
	YPlatformCenterYPos[x] = y + Enemy_Y_Position[x];
	CommonPlatCode_(x);
}

/* PosPlatform 1: how far right of its column a lift that goes up or down is. The cartridge's
** 12 is 4 pixels off the 8-pixel grid of mode 17's cells, where a lift's row then is seven
** columns of cells and three pictures. On mode 17 it is 16, on the grid: six columns and one
** picture, which gfx17.c composes once (the user, 2026-10-08: "4 pixels won't make a big
** difference in gameplay", "Do it if it's doable"). 16 and not 8: 1-2's lifts are then in
** the middle of their shafts (32 and 32 pixels to the walls, the cartridge's 28 and 36) and
** 4-2's 16 or 32 from the left wall and 16 from the right (12 or 28, and 20); at 8 one gap
** of 4-2 is 8 pixels and 1-2's right one 40 */
#define LIFT_GRID 16
#ifndef LIFT_X
	#define LIFT_X LIFT_GRID
#endif

/* PlatLiftUp / PlatLiftDown, then CommonSmallLift (PosPlatform 1, box 4) */
static inline __attribute__((always_inline)) void SmallLift_(u8 x, u8 f, s8 s, u8 lx){
	Enemy_Y_MoveForce[x] = f;
	Enemy_Y_Speed[x] = s;
	AddEnemyX(x, lx);
	Enemy_BoundBoxCtrl[x] = 0x04;
}

static void InitLift_(u8 x){
	u8 id = Enemy_ID[x];
	bool up = (id == 0x26 || id == 0x2b);       /* PlatLiftUp, else PlatLiftDown */
	/* the small lifts ride a rope, which is the background's: they stay the cartridge's 12
	** pixels from their column, in its middle (the user, 2026-10-09: 2-4's) */
	SmallLift_(x, up ? 0x10 : 0xf0, up ? (s8)0xff : 0, id < 0x2b ? LIFT_X : 12);
	if(id < 0x2b) SPBBox_(x);                   /* LargeLiftBBox */
}

/* the jump engine left Y = id * 2 + 2 and carry clear: Setup_Vine reads Y (residual, the
** vine's second object takes its position from unrelated memory, never used) */
static void InitPowerUp_(u8 x){
	X_ = x; Y_ = 0x5e; C_ = false; PwrUpJmp();
}

static void InitVine_(u8 x){
	X_ = x; Y_ = 0x60; C_ = false; Setup_Vine();
}

/* CheckpointEnemyID: an enemy below $15 is 8 pixels lower and offscreen-masked, then the
** init routine of its identifier (InitEnemyRoutines; the frenzies through InitEnemyFrenzy) */
static void CheckpointEnemyID_(u8 x){
	u8 id = Enemy_ID[x];
	if(id < 0x15){
		Enemy_Y_Position[x] += 8;               /* the cmp #$15 left carry clear */
		EnemyOffscrBitsMasked[x] = 1;
	}
	if(id == Spiny || (u8)(id - FlyCheepCheepFrenzy) < 4){   /* InitEnemyFrenzy */
		EnemyFrenzyBuffer = id;
		if(FrenzyEnemyTimer) return;            /* each frenzy's own first test */
	}
	switch(id){                                 /* InitEnemyRoutines */
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x06: InitNormalEnemy_(x); break;
	case 0x05: InitHammerBro_(x); break;
	case 0x07: InitBloober_(x); break;
	case 0x08: InitBulletBill_(x); break;
	case 0x0a: case 0x0b: InitCheepCheep_(x); break;
	case 0x0c: InitPodoboo_(x); break;
	case 0x0d: InitPiranhaPlant_(x); break;
	case 0x0e: InitJumpGPTroopa_(x); break;
	case 0x0f: InitRedPTroopa_(x); break;
	case 0x10: InitHorizFlySwimEnemy_(x); break;
	case 0x11: InitLakitu_(x); break;
	case 0x12: InitFrenzyLakitu_(x); break;
	case 0x14: InitFrenzyFlyCC_(x); break;
	case 0x15: InitFrenzyFlame_(x); break;
	case 0x16: InitFrenzyFireworks_(x); break;
	case 0x17: InitFrenzyBBCC_(x); break;
	case 0x18: EndFrenzy_(x); break;
	case 0x1b: case 0x1c: case 0x1d: case 0x1e: InitShortFirebar_(x); break;
	case 0x1f: InitLongFirebar_(x); break;
	case 0x24: InitBalPlatform_(x); break;
	case 0x25: InitVertPlatform_(x); break;
	case 0x26: case 0x27: case 0x2b: case 0x2c: InitLift_(x); break;
	case 0x28: case 0x2a: InitHoriPlatform_(x); break;
	case 0x29: InitDropPlatform_(x); break;
	case 0x2d: InitBowser_(x); break;
	case 0x2e: InitPowerUp_(x); break;
	case 0x2f: InitVine_(x); break;
	case 0x35: InitRetainerObj_(x); break;
	default: break;                             /* NoInitCode */
	}
}

/* ---------------------------------------------------------------------------------------
** Running the enemies (the movement routines take the slot in X_)
** ------------------------------------------------------------------------------------ */

/* EraseEnemyObject: clears slot x */
static void EraseEnemyObject_(u8 x){
	EBASE(x, Enemy_Flag);
	EP(Enemy_Flag, Enemy_Flag) = 0;
	EP(Enemy_Flag, Enemy_ID) = 0;
	EP(Enemy_Flag, Enemy_State) = 0;
	FloateyNum_Control[x] = 0;
	EnemyIntervalTimer[x] = 0;
	ShellChainCounter[x] = 0;
	Enemy_SprAttrib[x] = 0;
	EnemyFrameTimer[x] = 0;
}

void EraseEnemyObject(void){
	EraseEnemyObject_(X_);
	A_ = nz(0);
}

/* RunRetainerObj: the retainer (and a Bowser half) is only drawn */
void RunRetainerObj(void){
	u8 x = X_;
	CX(GetEnemyOffscreenBits);
	CX(RelativeEnemyPosition);
	CX(EnemyGfxHandler);
	X_ = x;
}

/* MovePodoboo: rises again from below when its timer runs out */
static void MovePodoboo_(void){
	u8 x = X_;
	if(!EnemyIntervalTimer[x]){
		InitPodoboo_(x);
		u8 a = PseudoRandomBitReg[x + 1] | 0x80;
		Enemy_Y_MoveForce[x] = a;
		EnemyIntervalTimer[x] = (a & 0x0f) | 0x06;
		Enemy_Y_Speed[x] = (s8)0xf9;
	}
	CX(MoveJ_EnemyVertically);
}

/* MoveHammerBroXDir: shimmies left and right, faces the player, walks left on a timer */
static void MoveHammerBroXDir_(u8 x){
	EBASE(x, Enemy_MovingDir);
	EP(Enemy_MovingDir, Enemy_X_Speed) = (FrameCounter & 0x40) ? (s8)0xfc : 0x04;
	u8 y = 1;
	CX(PlayerEnemyDiff);
	if(!(A_ & 0x80)){                           /* the enemy right of the player */
		y = 2;
		if(!EnemyIntervalTimer[x]) EP(Enemy_MovingDir, Enemy_X_Speed) = (s8)0xf8;
	}
	EP(Enemy_MovingDir, Enemy_MovingDir) = y;
	CX(MoveNormalEnemy);
}

/* SetHJ: the jump of vertical speed y; M00 (0 or 1) masks the jump length bit */
static void SetHJ_(u8 x, u8 y){
	Enemy_Y_Speed[x] = (s8)y;
	Enemy_State[x] |= 0x01;
	bool l = SecondaryHardMode && (M00 & PseudoRandomBitReg[x + 2]);
	EnemyFrameTimer[x] = l ? 0x37 : 0x20;       /* HammerBroJumpLData */
	HammerBroJumpTimer[x] = PseudoRandomBitReg[x + 1] | 0xc0;
	MoveHammerBroXDir_(x);
}

/* SetHJ: X_ kept (MoveNormalEnemy, last, leaves it) */
void SetHJ(void){
	SetHJ_(X_, Y_);
}

static inline __attribute__((always_inline)) void MoveDefeatedEnemy_(u8 x){
	CX(MoveD_EnemyVertically);
	CX(MoveEnemyHorizontally);
}

/* ProcHammerBro: throws hammers between jumps; HammerBroJumpCode: up from the bottom half,
** up or down from the top */
static void ProcHammerBro_(void){
	u8 x = X_;
	EBASE2(x, HammerThrowingTimer);
	EBASE(x, Enemy_State);
	u8 s = EP(Enemy_State, Enemy_State);
	if(s & 0x20){
		MoveDefeatedEnemy_(x);
		return;
	}
	if(!EP(Enemy_State, HammerBroJumpTimer)){
		if((s & 0x07) != 0x01){                 /* not already jumping */
			u8 m = 0, y = 0xfa;
			u8 p = Enemy_Y_Position[x];
			if(!(p & 0x80)){
				y = 0xfd;
				if(p < 0x70) m = 1;
				else if(!(PseudoRandomBitReg[x + 1] & 0x01)) y = 0xfa;
			}
			M00 = m;
			SetHJ_(x, y);
			return;
		}
	}else{
		EP(Enemy_State, HammerBroJumpTimer)--;
		if(!(Enemy_OffscreenBits & 0x0c)){
			if(!EP2(HammerThrowingTimer, HammerThrowingTimer)){
				EP2(HammerThrowingTimer, HammerThrowingTimer) = SecondaryHardMode ? 0x1c : 0x30;   /* HammerThrowTmrData */
				CX(SpawnHammerObj);
				if(C_){
					EP(Enemy_State, Enemy_State) |= 0x08;     /* throwing */
					goto move;
				}
			}
			EP2(HammerThrowingTimer, HammerThrowingTimer)--;           /* DecHT */
		}
	}
move:
	MoveHammerBroXDir_(x);
}

/* MoveNormalEnemy: walking, falling, stunned and defeated enemies (ReviveStunned: a stunned
** enemy gets up when its timer runs out, a stomped goomba goes); X_ kept */
void MoveNormalEnemy(void){
	u8 x = X_;
	EBASE(x, Enemy_ID);
	bool slow = false, def = false;
	u8 s = EP(Enemy_ID, Enemy_State);
	if(!(s & 0x40)){
		if(s & 0x80) goto SteadM;
		if(s & 0x20) def = true;                /* MoveDefeatedEnemy: as MEHor */
		else{
			s &= 0x07;
			if(!s) goto SteadM;
			if(s != 0x05 && s >= 0x03){         /* ReviveStunned */
				u8 t = EnemyIntervalTimer[x];
				if(t){                          /* ChkKillGoomba */
					if(t == 0x0e && EP(Enemy_ID, Enemy_ID) == Goomba) EraseEnemyObject_(x);
				}else{
					EP(Enemy_ID, Enemy_State) = 0;
					u8 f = FrameCounter & 0x01;
					EP(Enemy_ID, Enemy_MovingDir) = f + 1;
					s8 v = PrimaryHardMode ? 0x0c : 0x08;   /* RevivedXSpeed */
					EP(Enemy_ID, Enemy_X_Speed) = f ? -v : v;
				}
				goto out;
			}
		}
	}
	/* FallE */
	CX(MoveD_EnemyVertically);
	s = EP(Enemy_ID, Enemy_State);
	if(def || s == 0x02){                       /* MEHor */
		CX(MoveEnemyHorizontally);
		goto out;
	}
	slow = (s & 0x40) && EP(Enemy_ID, Enemy_ID) != PowerUpObject;   /* SlowM */
SteadM:;
	s8 sp = EP(Enemy_ID, Enemy_X_Speed);
	s8 n = sp;
	if(slow) n += (sp < 0) ? 0x18 : (s8)0xe8;   /* XSpeedAdderData */
	EP(Enemy_ID, Enemy_X_Speed) = n;
	CX(MoveEnemyHorizontally);
	EP(Enemy_ID, Enemy_X_Speed) = sp;
	A_ = (u8)sp;
out:
	X_ = x;
}

/* MoveJumpingEnemy: X_ kept */
void MoveJumpingEnemy(void){
	u8 x = X_;
	CX(MoveJ_EnemyVertically);
	CX(MoveEnemyHorizontally);
	X_ = x;
}

/* ProcMoveRedPTroopa: back down to its origin, then up and down around its centre */
static void ProcMoveRedPTroopa_(void){
	u8 x = X_;
	EBASE(x, Enemy_Y_Speed);
	u8 p = EP(Enemy_Y_Speed, Enemy_Y_Position);
	if(!((u8)EP(Enemy_Y_Speed, Enemy_Y_Speed) | Enemy_Y_MoveForce[x])){
		Enemy_YMF_Dummy[x] = 0;
		if(p < RedPTroopaOrigXPos[x]){
			if(!(FrameCounter & 0x07)) EP(Enemy_Y_Speed, Enemy_Y_Position) = p + 1;
			return;
		}
	}
	/* MoveRedPTUpOrDown */
	X_ = x;
	if(p < RedPTroopaCenterYPos[x]) MoveRedPTroopaDown();
	else MoveRedPTroopaUp();
}

/* XMoveCntr_Platform: every fourth frame the secondary counter runs up to max and back down;
** the primary counter counts the turns */
static void XMoveCntr_Platform_(u8 x, u8 max){
	M01 = max;
	if(FrameCounter & 0x03) return;
	u8 y = XMoveSecondaryCounter[x];
	if(XMovePrimaryCounter[x] & 0x01){
		if(!y) XMovePrimaryCounter[x]++;        /* IncPXM */
		else XMoveSecondaryCounter[x]--;        /* DecSeXM */
	}else{
		if(y == max) XMovePrimaryCounter[x]++;
		else XMoveSecondaryCounter[x]++;
	}
}

void XMoveCntr_Platform(void){
	XMoveCntr_Platform_(X_, A_);
}

/* MoveWithXMCntrs: moves right or left by the secondary counter; M00 the amount moved */
static u8 MoveWithXMCntrs_(u8 x){
	EFB(x);
	EXB(x);
	u8 saved = EF(XMoveSecondaryCounter);
	u8 y = 1;
	if(!(EX(XMovePrimaryCounter) & 0x02)){
		EF(XMoveSecondaryCounter) = 0 - saved;
		y = 2;
	}
	EF(Enemy_MovingDir) = y;
	CX(MoveEnemyHorizontally);
	M00 = A_;
	EF(XMoveSecondaryCounter) = saved;
	return saved;
}

void MoveWithXMCntrs(void){
	u8 x = X_;
	A_ = MoveWithXMCntrs_(x);
	X_ = x;
}

/* MoveFlyGreenPTroopa: back and forth, waving up and down */
static void MoveFlyGreenPTroopa_(void){
	u8 x = X_;
	XMoveCntr_Platform_(x, 0x13);               /* XMoveCntr_GreenPTroopa */
	MoveWithXMCntrs_(x);
	u8 f = FrameCounter;
	if(f & 0x03) return;
	u8 m = (f & 0x40) ? 0x01 : 0xff;            /* YSway */
	M00 = m;
	Enemy_Y_Position[x] += m;
}

/* MoveBloober: swims toward the player, never above the status bar */
static void MoveBloober_(void){
	u8 x = X_;
	EFB(x);
	if(EF(Enemy_State) & 0x20){                  /* MoveDefeatedBloober */
		CX(MoveEnemySlowVert);
		return;
	}
	bool c = false;                             /* the jump engine left carry clear */
	u8 mk = SecondaryHardMode ? 0x03 : 0x3f;   /* BlooberBitmasks */
	if(!(u8)(PseudoRandomBitReg[x + 1] & mk)){
		u8 y;
		if(x & 0x01){                           /* second or fourth slot: the player's direction */
			y = Player_MovingDir;
			c = true;
		}else{                                  /* FBLeft */
			y = 2;
			CX(PlayerEnemyDiff);
			c = C_;                             /* the enemy not left of the player */
			if(A_ & 0x80) y--;
		}
		EF(Enemy_MovingDir) = y;                 /* SBMDir */
	}
	/* BlooberSwim: ProcSwimmingB, the strokes (counter d1 clear) and the float down; c the
	** carry the 6502 had, which ChkNearPlayer adds */
	u8 cnt = BlooperMoveCounter[x];
	if(cnt & 0x02){                             /* ChkForFloatdown */
		if(!EnemyIntervalTimer[x]                /* ChkNearPlayer */
		   && (u8)(Enemy_Y_Position[x] + 0x10 + c) >= Player_Y_Position){
			BlooperMoveCounter[x] = 0;
		}else if(!(FrameCounter & 0x01)){
			Enemy_Y_Position[x]++;              /* Floatdown */
		}
	}else if(!(FrameCounter & 0x07)){
		u8 m = Enemy_Y_MoveForce[x];
		if(!(cnt & 0x01)){
			m++;
			if(m == 0x02) cnt++;
		}else{                                  /* SlowSwim */
			m--;
			if(!m){
				cnt++;
				EnemyIntervalTimer[x] = 2;
			}
		}
		Enemy_Y_MoveForce[x] = m;
		EF(BlooperMoveSpeed) = m;
		BlooperMoveCounter[x] = cnt;
	}
	u8 p = Enemy_Y_Position[x] - Enemy_Y_MoveForce[x];
	if(p >= 0x20) Enemy_Y_Position[x] = p;
	/* SwimX / LeftSwim */
	u8 s = EF(BlooperMoveSpeed);
	AddEnemyX(x, (EF(Enemy_MovingDir) == 1) ? s : -(u16)s);
}

/* MoveBulletBill: the frenzy's bullet bill flies left */
static void MoveBulletBill_(void){
	u8 x = X_;
	EBASE(x, Enemy_State);
	if(EP(Enemy_State, Enemy_State) & 0x20){
		CX(MoveJ_EnemyVertically);
		return;
	}
	EP(Enemy_State, Enemy_X_Speed) = (s8)0xe8;
	CX(MoveEnemyHorizontally);
}

/* MoveSwimmingCheepCheep: slowly left, and from the third slot up and down by $20 a frame of
** subpixel; ChkSwimYPos turns it round 15 pixels from its origin */
static void MoveSwimmingCheepCheep_(void){
	u8 x = X_;
	EBASE(x, Enemy_Y_HighPos);
	if(Enemy_State[x] & 0x20){
		CX(MoveEnemySlowVert);
		return;
	}
	M03 = 0;                                    /* the state's d5, clear here */
	M02 = 0x20;
	u8 sub = (Enemy_ID[x] == GreyCheepCheep) ? 0x40 : 0x80;   /* SwimCCXMoveData */
	u8 f = Enemy_X_MoveForce[x];
	Enemy_X_MoveForce[x] = f - sub;
	if(f < sub) AddEnemyX(x, 0xffff);
	if(x < 2) return;
	u16 yy = (EP(Enemy_Y_HighPos, Enemy_Y_HighPos) << 8) | EP(Enemy_Y_HighPos, Enemy_Y_Position);
	u8 d = Enemy_YMF_Dummy[x];
	if(CheepCheepMoveMFlag[x] >= 0x10){
		yy += (d >= 0xe0);
		d += 0x20;
	}else{                                      /* CCSwimUpwards */
		yy -= (d < 0x20);
		d -= 0x20;
	}
	Enemy_YMF_Dummy[x] = d;
	EP(Enemy_Y_HighPos, Enemy_Y_Position) = yy;
	EP(Enemy_Y_HighPos, Enemy_Y_HighPos) = yy >> 8;               /* ChkSwimYPos */
	u8 y = 0;
	u8 v = (u8)yy - CheepCheepOrigYPos[x];
	if(v & 0x80){
		y = 0x10;
		v = 0 - v;
	}
	if(v >= 0x0f) CheepCheepMoveMFlag[x] = y;
}

/* ---------------------------------------------------------------------------------------
** Firebars
** ------------------------------------------------------------------------------------ */

/* a firebar part's adder: row o of FirebarPosLookupTbl at spin state a, oscillating */
static __attribute__((noinline)) u8 FirebarAdder_(u8 o, u8 a){
	a &= 0x0f;
	if(a >= 0x09) a = (a ^ 0x0f) + 1;
	return RD(FirebarPosLookupTbl, o + a);
}

/* FirebarCollision: draws the part at OAM offset y (DrawFirebar), positioned at M06/M07, and
** hurts the player when it touches him; M06 then the next part's OAM offset */
static void FirebarCollision_(u8 y){
	GBASE(M00);
	Y_ = y;
	DrawFirebar();
	if(!(StarInvincibleTimer | TimerControl)){
		G(M00, M05) = 0;
		if(Player_Y_HighPos == 1){
			u8 a = Player_Y_Position;
			if(PlayerSize || CrouchingFlag){    /* AdjSm: small, or big and crouching */
				G(M00, M05) = 2;
				a += 0x18;
			}
			for(;;){                            /* FBCLoop */
				u8 d = a - G(M00, M07);
				if(d & 0x80) d = 0 - d;
				if(d < 0x08 && G(M00, M06) < 0xf0){
					u8 m4 = Sprite_X_Position(4) + 4;   /* the player's sprite 1 */
					G(M00, M04) = m4;
					u8 e = m4 - G(M00, M06);
					if(e & 0x80) e = 0 - e;
					if(e < 0x08){               /* ChgSDir */
						Enemy_MovingDir[0] = (m4 >= G(M00, M06)) ? 1 : 2;   /* not indexed: slot 0 */
						u8 s = G(M00, M00);
						X_ = 0;
						InjurePlayer();
						G(M00, M00) = s;
						break;
					}
				}
				if(G(M00, M05) == 0x02) break;           /* Chk2Ofs */
				a = Player_Y_Position + (G(M00, M05) ? 0x18 : 0x0c);
				G(M00, M05)++;
			}
		}
	}
	G(M00, M06) = y + 4;                                /* NoColFB */
	X_ = G(M00, ObjectOffset);
}

/* GetFirebarPosition, then DrawFirebar_Collision: part M00 at spin state M_EF: the adders
** (M01, M02) mirrored by M03, around the firebar's centre; out of range or with the centre
** offscreen it is hidden (y $f8); M05 the mirroring bits left */
static inline __attribute__((always_inline)) void DrawFirebarPart_(void){
	GBASE(M00);
	u8 a = M_EF;
	u8 o = RD(FirebarTblOffsets, G(M00, M00));
	u8 h = FirebarAdder_(o, a);
	G(M00, M01) = h;
	u8 v = FirebarAdder_(o, a + 0x08);
	G(M00, M02) = v;
	u8 m = RD(FirebarMirrorData, a >> 3);
	G(M00, M03) = m;
	u8 y = G(M00, M06);
	u8 rx = Enemy_Rel_XPos;
	if(!(m & 0x01)) h = 0 - h;
	m >>= 1;
	a = h + rx;                                 /* AddHA */
	Sprite_X_Position(y) = a;
	G(M00, M06) = a;
	u8 d = (a >= rx) ? (u8)(a - rx) : (u8)(rx - a);
	if(d >= 0x59) a = 0xf8;                     /* ChkFOfs */
	else{                                       /* VAHandl */
		a = Enemy_Rel_YPos;
		if(a != 0xf8){
			if(!(m & 0x01)) v = 0 - v;
			m >>= 1;
			a += v;                             /* AddVA */
		}
	}
	G(M00, M05) = m;
	Sprite_Y_Position(y) = a;                   /* SetVFbr */
	G(M00, M07) = a;
	FirebarCollision_(y);
}

/* ProcFirebar: spins the firebar and draws its parts, each also checked against the player */
static inline __attribute__((always_inline)) void ProcFirebar_(u8 x){
	CX(GetEnemyOffscreenBits);
	if(Enemy_OffscreenBits & 0x08) return;
	if(!TimerControl){
		X_ = x;
		A_ = FirebarSpinSpeed[x];
		FirebarSpin();
		FirebarSpinState_High[x] = A_ & 0x1f;
	}
	/* SusFbar */
	u8 a = FirebarSpinState_High[x];
	if(Enemy_ID[x] >= 0x1f && (a == 0x08 || a == 0x18)){   /* long: never horizontal */
		a++;
		FirebarSpinState_High[x] = a;
	}
	M_EF = a;                                   /* SetupGFB */
	CX(RelativeEnemyPosition);
	/* the 6502 called GetFirebarPosition here (residual: its results are overwritten) */
	u8 y = Enemy_SprDataOffset[x];
	M07 = Sprite_Y_Position(y) = Enemy_Rel_YPos;
	M06 = Sprite_X_Position(y) = Enemy_Rel_XPos;
	M00 = 1;
	FirebarCollision_(y);                       /* the centre */
	M_ED = (Enemy_ID[x] >= 0x1f) ? 0x0b : 0x05;
	M00 = 0;
	do{                                         /* DrawFbar */
		DrawFirebarPart_();
		if(M00 == 4) M06 = Enemy_SprDataOffset[DuplicateObj_Offset];   /* the long firebar's */
	}while(++M00 < M_ED);
}

/* ---------------------------------------------------------------------------------------
** Flying cheep-cheeps and lakitu
** ------------------------------------------------------------------------------------ */

static void MoveFlyingCheepCheep_(void){
	u8 x = X_;
	if(Enemy_State[x] & 0x20){
		Enemy_SprAttrib[x] = 0;
		CX(MoveJ_EnemyVertically);
		return;
	}
	CX(MoveEnemyHorizontally);                  /* FlyCC */
	X_ = x;
	Y_ = 0x0d;
	A_ = 0x05;
	SetXMoveAmt();
	u8 f = Enemy_Y_MoveForce[x];
	u8 y = f >> 4;
	u8 d = Enemy_Y_Position[x] - RD(PRandomSubtracter, y);
	if(d & 0x80) d = 0 - d;
	if(d < 0x08){                               /* AddCCF */
		f += 0x10;
		Enemy_Y_MoveForce[x] = f;
		y = f >> 4;
	}
	Enemy_SprAttrib[x] = RD(FlyCCBPriority, y);  /* BPGet (residual, overwritten) */
}

/* PlayerLakituDiff: steers the lakitu toward the player; returns its speed (or the spiny's,
** unused) from the adjusters M01-M03 less the distance */
static u8 PlayerLakituDiff_(u8 x){
	EFB(x);
	u8 y = 0;
	CX(PlayerEnemyDiff);
	u8 m = M00;
	if(A_ & 0x80){                              /* the enemy left of the player */
		y = 1;
		m = 0 - m;
	}
	/* ChkLakDif */
	if(m >= 0x3c){
		m = 0x3c;
		if(EF(Enemy_ID) == Lakitu && y != LakituMoveDirection[x]){
			if(LakituMoveDirection[x]){
				u8 s = --EF(LakituMoveSpeed);
				if(s){                          /* ExMoveLak */
					M00 = m;
					return s;
				}
			}
			LakituMoveDirection[x] = y;         /* SetLMovD */
		}
	}
	/* ChkPSpeed */
	m = (m & 0x3c) >> 2;
	M00 = m;
	y = 0;
	u8 ps = Player_X_Speed;
	if(ps && ScrollAmount){
		y = 1;
		if(ps >= 0x19 && ScrollAmount >= 0x02) y = 2;
		/* ChkSpinyO / ChkEmySpd: a spiny with the player moving keeps it */
		if(EF(Enemy_ID) != Spiny && !Enemy_Y_Speed[x]) y = 0;
	}
	/* SubDifAdj / SPixelLak: one less per pixel of difference, plus one */
	return ra.M_[1 + y] - (m + 1);
}

static void MoveLakitu_(void){
	u8 x = X_;
	GBASE(M01);
	u8 s = Enemy_State[x];
	if(s & 0x20){
		CX(MoveD_EnemyVertically);
		return;
	}
	u8 a;
	if(s){                                      /* leaving: left at speed $10 */
		LakituMoveDirection[x] = 0;
		EnemyFrenzyBuffer = 0;
		a = 0x10;
	}else{                                      /* Fr12S */
		EnemyFrenzyBuffer = Spiny;
		G(M01, M01) = 0x15;                             /* LakituDiffAdj */
		G(M01, M02) = 0x30;
		G(M01, M03) = 0x40;
		a = PlayerLakituDiff_(x);
	}
	u8 y = 1;
	if(!(LakituMoveDirection[x] & 0x01)){
		a = 0 - a;
		y = 2;
	}
	LakituMoveSpeed[x] = a;                    /* SetLSpd */
	Enemy_MovingDir[x] = y;                    /* SetLMov */
	CX(MoveEnemyHorizontally);
}

/* ---------------------------------------------------------------------------------------
** The run routines
** ------------------------------------------------------------------------------------ */

/* RunNormalEnemies: objects $00-$14, moved by EnemyMovementSubs */
static void RunNormalEnemies_(void){
	u8 x = X_;
	Enemy_SprAttrib[x] = 0;
	CX(GetEnemyOffscreenBits);
	CX(RelativeEnemyPosition);
	CX(EnemyGfxHandler);
	CX(GetEnemyBoundBox);
	CX(EnemyToBGCollisionDet);
	CX(EnemiesCollision);
	CX(PlayerEnemyCollision);
	if(!TimerControl){
		u8 id = Enemy_ID[x];
		X_ = x;
		C_ = false;                             /* the jump engine left carry clear */
		switch(id){                             /* EnemyMovementSubs */
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x06: case 0x12: MoveNormalEnemy(); break;
		case 0x05: ProcHammerBro_(); break;
		case 0x07: MoveBloober_(); break;
		case 0x08: MoveBulletBill_(); break;
		case 0x0a: case 0x0b: MoveSwimmingCheepCheep_(); break;
		case 0x0c: MovePodoboo_(); break;
		case 0x0d: MovePiranhaPlant(); break;
		case 0x0e: MoveJumpingEnemy(); break;
		case 0x0f: ProcMoveRedPTroopa_(); break;
		case 0x10: MoveFlyGreenPTroopa_(); break;
		case 0x11: MoveLakitu_(); break;
		case 0x14: MoveFlyingCheepCheep_(); break;
		default: break;                         /* NoMoveCode */
		}
	}
	CX(OffscreenBoundsCheck);                   /* SkipMove */
}

static void RunBowserFlame_(void){
	u8 x = X_;
	CX(ProcBowserFlame);
	CX(GetEnemyOffscreenBits);
	CX(RelativeEnemyPosition);
	CX(GetEnemyBoundBox);
	CX(PlayerEnemyCollision);
	CX(OffscreenBoundsCheck);
}

static void RunFirebarObj_(void){
	u8 x = X_;
	ProcFirebar_(x);
	CX(OffscreenBoundsCheck);
}

static void RunSmallPlatform_(void){
	u8 x = X_;
	CX(GetEnemyOffscreenBits);
	CX(RelativeEnemyPosition);
	CX(SmallPlatformBoundBox);
	CX(SmallPlatformCollision);
	CX(RelativeEnemyPosition);
	CX(DrawSmallPlatform);
	CX(MoveSmallPlatform);
	CX(OffscreenBoundsCheck);
}

/* RunLargePlatform: platforms $24-$2a, moved by LargePlatformSubroutines */
static void RunLargePlatform_(void){
	u8 x = X_;
	CX(GetEnemyOffscreenBits);
	CX(RelativeEnemyPosition);
	CX(LargePlatformBoundBox);
	CX(LargePlatformCollision);
	if(!TimerControl){
		u8 n = Enemy_ID[x] - 0x24;
		X_ = x;
		Y_ = (n << 1) + 2;                      /* as the jump engine left them */
		C_ = false;
		switch(n){                              /* LargePlatformSubroutines */
		case 0x00: BalancePlatform(); break;
		case 0x01: YMovingPlatform(); break;
		case 0x02: case 0x03: MoveLargeLiftPlat(); break;
		case 0x04: XMovingPlatform(); break;
		case 0x05: DropPlatform(); break;
		case 0x06: RightPlatform(); break;
		}
	}
	CX(RelativeEnemyPosition);                  /* SkipPT */
	CX(DrawLargePlatform);
	CX(OffscreenBoundsCheck);
}

/* RunEnemyObjectsCore: the run routine of the enemy at ObjectOffset, by identifier */
static void RunEnemyObjectsCore_(void){
	u8 x = ObjectOffset;
	u8 id = Enemy_ID[x];
	u8 n = (id < 0x15) ? 0 : id - 0x14;
	X_ = x;
	Y_ = (n << 1) + 2;                          /* as the jump engine left them */
	C_ = false;
	switch(id){
	case 0x00 ... 0x14: RunNormalEnemies_(); break;
	case 0x15: RunBowserFlame_(); break;
	case 0x16: RunFireworks(); break;
	case 0x1b ... 0x22: RunFirebarObj_(); break;
	case 0x24 ... 0x2a: RunLargePlatform_(); break;
	case 0x2b: case 0x2c: RunSmallPlatform_(); break;
	case 0x2d: RunBowser(); break;
	case 0x2e: PowerUpObjHandler(); break;
	case 0x2f: VineObjectHandler(); break;
	case 0x31: RunStarFlagObj(); break;
	case 0x32: JumpspringHandler(); break;
	case 0x34: WarpZoneObject(); break;
	case 0x35: RunRetainerObj(); break;
	default: break;                             /* NoRunCode */
	}
}
