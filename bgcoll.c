/*
 * bgcoll.c -- background collision and bounding boxes: a port of smbdis.asm lines 11899-13090
 * (PlayerBGCollision to BlockBufferCollision), plus WarpZoneNumbers (line 1682). Routine names
 * are the 6502's; the register convention is smb.h's. Routines only this module and
 * collide.c call take C parameters (Label_), and set only the registers a caller reads.
 */
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

#define INL static inline __attribute__((always_inline))

/* One slot's fields through one base pointer, so gcc can use ldd/std with a displacement:
** SB(x, arr) puts it at arr[x], S(f) is then f[x] (f within 63 bytes of arr in ra). The asm
** hides the pointer's origin, or gcc would fold it back into each address */
#define RAO(p) ((u16)((const u8 *)(p) - (const u8 *)&ra))
static inline u8 *ra_at(u16 o){
	u8 *p = (u8 *)&ra + o;
	__asm__("" : "+b"(p));
	return p;
}
#define SB(x, arr) u8 *const sb_ = ra_at(RAO(arr) + (x)); const u16 so_ = RAO(arr)
#define S(f) (*(__typeof__(&(f)[0]))(sb_ + (RAO(f) - so_)))
/* slot field f[x] through E = ra_at(RAO(Enemy_ID) + x): Enemy_ID, _State, _MovingDir, _X_Speed */
#define EF(f) (*(__typeof__(&(f)[0]))(E + (RAO(f) - RAO(Enemy_ID))))
/* a field of ra's first 64 bytes through R = ra_at(0) */
#define RF(f) (*(__typeof__(&(f)))(R + RAO(&(f))))
/* field f of ra through p = ra_at(RAO(&b)): f within 63 bytes after b */
#define AT(p, b, f) (*(__typeof__(&(f)))((p) + (RAO(&(f)) - RAO(&(b)))))


/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */

/* the warp pipes' worlds, left to right, by WarpZoneControl & 3; $24 is a space */
const u8 WarpZoneNumbers[12] PROGMEM = { ROM_WarpZoneNumbers };

/* the third byte is the opcode after the table ($ad), read for a crouching small player */
static const u8 PlayerBGUpperExtent[3] PROGMEM = { 0x20, 0x10, 0xad };
/* unread (the timer is computed), kept so the tables after it stay where they were */
static const u8 AreaChangeTimerData[2] PROGMEM __attribute__((used)) = { 0xa0, 0x34 };
/* ClimbXPosAdder, ClimbPLocAdder, FlagpoleYPosData, contiguous as on the 6502: a player facing
** both ways (3) reads the next table's first byte (facing 0 reads 255 on, as the port did) */
static const u8 ClimbXPosAdder[9] PROGMEM = { ROM_ClimbXPosAdder };
#define ClimbPLocAdder (ClimbXPosAdder + 2)
#define FlagpoleYPosData (ClimbXPosAdder + 4)
static const u8 SolidMTileUpperExt[4] PROGMEM = { ROM_SolidMTileUpperExt };
static const u8 ClimbMTileUpperExt[4] PROGMEM = { ROM_ClimbMTileUpperExt };

/* EnemyBGCStateData then EnemyBGCXSpdData, one table: states 6-7 read the speeds as on the 6502 */
static const u8 EnemyBGCStateData[8] PROGMEM = { ROM_EnemyBGCStateData };

/* added to relative coordinates: left edge, top edge, right edge, bottom edge */
static const u8 BoundBoxCtrlData[48] PROGMEM = { ROM_BoundBoxCtrlData };

/* unread (0, 7, $0e are computed), kept for the layout states past 7 read EnemyBGCStateData in */
static const u8 BlockBufferAdderData[3] PROGMEM __attribute__((used)) = { 0x00, 0x07, 0x0e };

/* BlockBuffer_X_Adder then BlockBuffer_Y_Adder (BB_YADD on), one table as on the 6502 */

#define BB_YADD 28
static const u8 BlockBuffer_X_Adder[2 * BB_YADD] PROGMEM = { ROM_BlockBuffer_X_Adder };   /* BlockBuffer_Y_Adder */

#define PB(t, i) pgm_read_byte(&(t)[i])
/* a flash byte at p, p moved on */
	#define LPM_INC(p) __extension__({ u8 r_; __asm__("lpm %0, Z+" : "=r"(r_), "+z"(p)); r_; })

/* ---------------------------------------------------------------------------------------
** The block buffer
** ------------------------------------------------------------------------------------ */

#define invisible_mtile(a) ((u8)((a) - 0x5f) < 2)

/* ChkForNonSolids: the vine blank, coins and hidden blocks */
INL bool nonsolid_mtile(u8 a){
	return a == 0x26 || (u8)(a - 0xc2) < 2 || invisible_mtile(a);
}

/* BlockBufferCollision: metatile at object x + adder y; M02 row x 16, M03 the metatile, M04 the
** low nybble of x (flag d0 set) or y, M05 the x, M06/BBPtr the column; flag d1: a non-solid
** metatile (ChkForNonSolids) returned as 0. ($06),y with y = M02 past row 12 ran on the 6502
** from buffer 1 into rows 0-2 of buffer 2's same column; from buffer 2 into other RAM (read as
** blank here) */
static u8 bb_coll(u8 flag, u8 x, u8 y){
	/* the 6502 kept y in M04 first: overwritten below, nothing between reads it */
	u8 *sb_ = &SprObject_X_Position[x];
	__asm__("" : "+z"(sb_));
	const u16 so_ = RAO(SprObject_X_Position);
	u8 xp = S(SprObject_X_Position), yp = S(ra.SprObject_Y_Position_), pg = S(ra.SprObject_PageLoc_);
	const u8 *t = BlockBuffer_X_Adder + y;
	u8 ax = pgm_read_byte(t);
	u8 m05 = xp + ax;
	if(m05 < ax) pg++;
	u8 m02 = ((yp + pgm_read_byte(t + BB_YADD)) & 0xf0) - 0x20;
	u8 *m = ra_at(RAO(&M02));
	m[0] = m02;
	m[2] = (flag & 1 ? xp : yp) & 0x0f;
	m[3] = m05;
	A_ = (u8)(pg << 4 & 0x10) | (m05 >> 4);
	GetBlockBufferAddr();                           /* M06 = A_, which it leaves as it was */
	u8 row = m02 >> 4, v = 0;
	if(row < 13) v = BBPtr[row];
	else if(A_ < 16) v = bb_Get(A_ + 16, row - 13);
	M03 = v;
	if((flag & 2) && nonsolid_mtile(v)) v = 0;
	return v;
}

/* A_ 0 or 1 */
void BlockBufferCollision(void){
	bb_coll(A_, X_, Y_);
}

/* ErACM: blanks the block buffer at M02/M06 (rows past 12 as bb_coll reads them) */
static void bb_clear_m02(void){
	u8 row = M02 >> 4;
	if(row < 13) bb_Set(row, 0);
	else if(M06 < 16) bb_Put(M06 + 16, row - 13, 0);
}

/* BlockBufferChk_Enemy/FBall then ChkForNonSolids: SprObject sx, adder y, horizontal flag 0 */
INL u8 bbchk(u8 sx, u8 y){
	return bb_coll(2, sx, y);
}

/* ---------------------------------------------------------------------------------------
** Metatile classes
** ------------------------------------------------------------------------------------ */

/* CheckForSolidMTiles / CheckForClimbMTiles: at or above the class's bound for its 2 MSB */
static bool __attribute__((noinline)) mtile_ge(const u8 *t, u8 a){
	return a >= pgm_read_byte(t + (a >> 6));
}
#define solid_mtile(a) mtile_ge(SolidMTileUpperExt, a)
#define climb_mtile(a) mtile_ge(ClimbMTileUpperExt, a)
#define jumpspring_mtile(a) ((u8)((a) - 0x67) < 2)

/* CheckForCoinMTiles + HandleCoinMetatile: a coin is taken (sound, erased, counted) */
INL bool take_coin(u8 a){
	if((u8)(a - 0xc2) >= 2) return false;
	Square2SoundQueue = Sfx_CoinGrab;
	bb_clear_m02();
	RemoveCoin_Axe();
	CoinTallyFor1Ups++;
	GiveOneCoin();
	return true;
}

/* ---------------------------------------------------------------------------------------
** The player against the background
** ------------------------------------------------------------------------------------ */

INL void DoFootCheck_(u8 *R);

void PlayerBGCollision(void){
	u8 *const R = ra_at(0);
	if(DisableCollisionDet) return;
	u8 g = RF(GameEngineSubroutine);
	if(g == 0x0b || g < 0x04) return;
	if(SwimmingFlag) RF(Player_State) = 1;
	else if(RF(Player_State) == 0 || RF(Player_State) == 3) RF(Player_State) = 2;
	if(Player_Y_HighPos != 1) return;
	Player_CollisionBits = 0xff;
	u8 py = Player_Y_Position;
	if(py >= 0xcf) return;
	/* ChkCollSize: BlockBufferAdderData 0, 7, $0e */
	u8 y = 0x0e, x = PlayerSize;
	if(CrouchingFlag) x++;
	else if(!x) y = SwimmingFlag ? 0x07 : 0x00;
	M_EB = y;
	/* HeadChk */
	if(py >= PB(PlayerBGUpperExtent, x)){
		u8 a = bb_coll(0, 0, y);
		if(take_coin(a)) return;
		if(a && Player_Y_Speed < 0 && RF(M04) >= 4){
			if(solid_mtile(a)){
				if(a != 0x26) Square1SoundQueue = Sfx_Bump;   /* SolidOrClimb */
			}else if(AreaType && !BlockBounceTimer){
				A_ = a;
				PlayerHeadCollision();
				goto foot;
			}
			Player_Y_Speed = 1;                         /* NYSpd */
		}
	}
foot:
	DoFootCheck_(R);
}

/* ImpedePlayerMove: M00 1 stops the player moving left (pushing it 1 pixel right otherwise
** stops moving right); clears the side's collision bit */
void ImpedePlayerMove(void){
	u8 *const P = ra_at(RAO(&Player_X_Speed));
	u8 x = 1;
	s8 d = -1, sp = AT(P, Player_X_Speed, Player_X_Speed);
	if(M00 == 1){
		if(sp < 0) goto ExIPM;
	}else{
		x = 2;
		if((s8)(sp - 1) >= 0) goto ExIPM;               /* cpy #$01 / bpl: $80 counts as right */
		d = 1;
	}
	/* NXSpd */
	SideCollisionTimer = 0x10;
	AT(P, Player_X_Speed, Player_X_Speed) = 0;
	M00 = d >> 7;
	u16 p = ((AT(P, Player_X_Speed, Player_PageLoc) << 8) | AT(P, Player_X_Speed, Player_X_Position)) + d;
	AT(P, Player_X_Speed, Player_X_Position) = p;
	AT(P, Player_X_Speed, Player_PageLoc) = p >> 8;
ExIPM:
	Player_CollisionBits &= ~x;
}

/* FlagpoleCollision: the flagpole slide, the score by the height the pole was caught at */
INL void FlagpoleCollision(u8 *R){
	u8 g = RF(GameEngineSubroutine);
	if(g == 0x05) return;
	RF(PlayerFacingDir) = 1;
	ScrollLock++;
	if(g != 0x04){
		A_ = BulletBill_CannonVar;
		KillEnemies();
		EventMusicQueue = Silence;
		FlagpoleSoundQueue = Silence >> 1;
		u8 py = Player_Y_Position;
		FlagpoleCollisionYPos = py;
		u8 x = 4;
		while(py < PB(FlagpoleYPosData, x) && --x);   /* ChkFlagpoleYPosLoop: 0 if none */
		FlagpoleScore = x;
	}
	RF(GameEngineSubroutine) = 4;                   /* RunFR */
}

/* HandleClimbing: a the climbable metatile; only its middle (x low nybble 6-9) is caught; then
** PutPlayerOnVine: climbing state, the player against the column at M06 */
INL void HandleClimbing_(u8 *R, u8 a){
	if((u8)(RF(M04) - 0x06) >= 4) return;
	/* ChkForFlagpole */
	if((u8)(a - 0x24) < 2) FlagpoleCollision(R);
	else if(a == 0x26 && Player_Y_Position < 0x20) RF(GameEngineSubroutine) = 1;   /* VineCollision */
	RF(Player_State) = 3;
	Player_X_Speed = 0;
	Player_X_MoveForce = 0;
	if((u8)(Player_X_Position - ScreenLeft_X_Pos) < 0x10) RF(PlayerFacingDir) = 2;
	const u8 *t = ClimbXPosAdder + (u8)(RF(PlayerFacingDir) - 1);
	u8 c = RF(M06);
	Player_X_Position = (c << 4) + pgm_read_byte(t);
	if(!c) Player_PageLoc = ScreenRight_PageLoc + pgm_read_byte(t + 2);   /* ClimbPLocAdder */
}

/* HandlePipeEntry: down on a warp pipe (M00 right foot, M01 left foot) */
INL void HandlePipeEntry_(u8 *R){
	if(!(RF(Up_Down_Buttons) & Down_Dir) || RF(M00) != 0x11 || RF(M01) != 0x10) return;
	ChangeAreaTimer = 0x30;
	RF(GameEngineSubroutine) = 3;
	Square1SoundQueue = Sfx_PipeDown_Injury;
	Player_SprAttrib = 0x20;
	if(!WarpZoneControl) return;
	u8 x = (WarpZoneControl & 0x03) << 2;
	u8 px = Player_X_Position;
	if(px >= 0x60){
		x++;
		if(px >= 0xa0) x++;
	}
	/* GetWNum; AreaNumber cleared first so LoadAreaPointer reads the world's first area */
	WorldNumber = PB(WarpZoneNumbers, x) - 1;
	AreaNumber = 0;
	u8 t = AreaType;
	LoadAreaPointer();
	AreaType = t;                                   /* the 6502 set only AreaPointer here */
	EventMusicQueue = Silence;
	EntrancePage = 0;
	LevelNumber = 0;
	AltEntranceControl = 0;
	Hidden1UpFlag++;
	FetchNewGameTimerFlag++;
}

/* ChkFootMTile: false where the 6502 jumped away instead of going on to the side check */
INL bool ChkFootMTile(u8 *R, u8 a){
	if(climb_mtile(a) || Player_Y_Speed < 0) return true;
	if(a == 0xc5){                                  /* HandleAxeMetatile */
		OperMode_Task = 0;
		OperMode = 2;
		Player_X_Speed = 0x18;
		bb_clear_m02();                             /* ErACM */
		RemoveCoin_Axe();
		return false;
	}
	if(invisible_mtile(a)) return true;
	if(!JumpspringAnimCtrl){
		if(RF(M04) >= 5){
			RF(M00) = Player_MovingDir;
			ImpedePlayerMove();
			return false;
		}
		/* LandPlyr: ChkForLandJumpSpring */
		if(jumpspring_mtile(a)){
			VerticalForce = 0x70;
			JumpspringForce = 0xf9;
			JumpspringTimer = 0x03;
			JumpspringAnimCtrl = 0x01;
		}
		Player_Y_Position &= 0xf0;
		HandlePipeEntry_(R);
		Player_Y_Speed = 0;
		Player_Y_MoveForce = 0;
		StompChainCounter = 0;
	}
	RF(Player_State) = 0;                           /* InitSteP */
	return true;
}

/* CheckSideMTiles: what the player's side touched */
INL void CheckSideMTiles(u8 *R, u8 a){
	if(invisible_mtile(a)) return;
	if(climb_mtile(a)){
		HandleClimbing_(R, a);
		return;
	}
	if(take_coin(a)) return;
	if(jumpspring_mtile(a)){
		if(JumpspringAnimCtrl) return;
	}else if(!RF(Player_State) && RF(PlayerFacingDir) == 1 && (a == 0x6c || a == 0x1f)){
		/* ChkPBtm / PipeDwnS: a sideways pipe entered facing right, standing */
		u8 s = Player_SprAttrib;
		if(!s) Square1SoundQueue = Sfx_PipeDown_Injury;
		Player_SprAttrib = s | 0x20;
		if(Player_X_Position & 0x0f) ChangeAreaTimer = ScreenLeft_PageLoc ? 0x34 : 0xa0;
		if(RF(GameEngineSubroutine) == 0x08) RF(GameEngineSubroutine) = 2;   /* ChkGERtn: not from 7 */
		return;
	}
	ImpedePlayerMove();                             /* StopPlayerMove */
}

/* DoFootCheck: the blocks under the player's feet (M01 left, M00 right), then DoPlayerSideCheck
** / SideCheckLoop: both halves of each side; M00 the side counter (2 then 1) */
INL void DoFootCheck_(u8 *R){
	u8 y = M_EB;
	if(Player_Y_Position < 0xcf){
		u8 l = bb_coll(0, 0, ++y);
		if(take_coin(l)) return;
		u8 r = bb_coll(0, 0, ++y);
		RF(M00) = r;
		RF(M01) = l;
		u8 a = l ? l : r;
		if(a && (take_coin(a) || !ChkFootMTile(R, a))) return;
	}
	y = M_EB + 2;
	RF(M00) = 2;
	do{
		M_EB = ++y;
		u8 py = Player_Y_Position, a;
		if(py >= 0x20){
			if(py >= 0xe4) return;
			a = bb_coll(1, 0, y);
			if(a && a != 0x1c && a != 0x6b && !climb_mtile(a)) goto side;
		}
		/* BHalf */
		y++;                                        /* M_EB + 1: bb_coll does not write M_EB */
		if(py < 0x08 || py >= 0xd0) return;
		a = bb_coll(1, 0, y);
		if(a){
	side:
			CheckSideMTiles(R, a);
			return;
		}
	}while(--RF(M00));
}

/* ---------------------------------------------------------------------------------------
** Enemies against the background
** ------------------------------------------------------------------------------------ */

/* SubtEnemyYPos: false if the enemy is too high (y + 62 < 68) */
INL bool subt_y(u8 x){
	return (u8)(Enemy_Y_Position[x] + 0x3e) >= 0x44;
}

/* PlayerEnemyDiff: M00 the x difference, the page difference returned (negative: enemy
** left), C_ set when the enemy is not left of the player */
u8 PlayerEnemyDiff_(u8 x){
	SB(x, Enemy_PageLoc);
	u16 e = (S(Enemy_PageLoc) << 8) | S(Enemy_X_Position);
	u16 p = (Player_PageLoc << 8) | Player_X_Position;
	u16 d = e - p;
	M00 = d;
	C_ = e >= p;
	return d >> 8;
}

/* -> M00 x difference, A_/N_ page difference, C_ */
void PlayerEnemyDiff(void){
	u8 a = PlayerEnemyDiff_(X_);
	A_ = a;
	N_ = a >> 7;
}

/* EnemyLanding: stops the fall, the enemy on the block's top (InitVStf, then the y); returns
** &Enemy_Y_Speed[x] */
static s8 *EnemyLanding_(u8 x){
	SB(x, Enemy_Y_Speed);
	S(Enemy_Y_Speed) = 0;
	Enemy_Y_MoveForce[x] = 0;
	S(Enemy_Y_Position) = (S(Enemy_Y_Position) & 0xf0) | 0x08;
	return &S(Enemy_Y_Speed);
}

/* ChkForBump_HammerBroJ: the enemy turns around, or a hammer bro jumps */
static void ChkForBump_HammerBroJ_(u8 x){
	SB(x, Enemy_ID);
	if(x != 5 && (S(Enemy_State) & 0x80)) Square1SoundQueue = Sfx_Bump;   /* not the special slot */
	X_ = x;
	if(S(Enemy_ID) == HammerBro){
		M00 = 0;
		Y_ = 0xfa;
		SetHJ();
		return;
	}
	RXSpd_(x);                                      /* InvEnemyDir */
}

/* DoEnemySideCheck: a solid block on the side the enemy moves to turns it ($eb the side) */
static void DoEnemySideCheck_(u8 x){
	if(Enemy_Y_Position[x] < 0x20) return;
	u8 m = 2;                                       /* the side; its adder $18 - m */
	do{
		M_EB = m;
		if(m == Enemy_MovingDir[x]){
			u8 a = bb_coll(3, x + OFS_ENEMY, 0x18 - m);
			x = ObjectOffset;
			if(a){
				ChkForBump_HammerBroJ_(x);
				return;
			}
		}
	}while(--m);
	M_EB = 0;
}

/* ChkForRedKoopa: a red koopa walking turns at a ledge; otherwise the falling state is set */
INL void ChkForRedKoopa_(u8 x, u8 *E){
	u8 s = EF(Enemy_State);
	if(EF(Enemy_ID) == RedKoopa && !s){
		ChkForBump_HammerBroJ_(x);
		return;
	}
	EF(Enemy_State) = (s & 0x80) ? (s | 0x40) : PB(EnemyBGCStateData, s);   /* SetD6Ste */
	DoEnemySideCheck_(x);
}

/* LandEnemyProperly: an enemy that fell onto a block (M04 the low nybble of its y) */
INL void LandEnemyProperly(u8 x, u8 *E){
	if((u8)(M04 - 8) >= 5){
		ChkForRedKoopa_(x, E);
		return;
	}
	u8 s = EF(Enemy_State), id = EF(Enemy_ID);
	if(s & 0x40) goto init;
	if(!s || (s & 0x80)) goto side;
	/* ChkLandedEnemyState */
	if(s != 5){
		if(s >= 3) return;
		if(s == 2){
			EnemyIntervalTimer[x] = (id == Spiny) ? 0x00 : 0x10;
			EF(Enemy_State) = 3;
			EnemyLanding_(x);
			return;
		}
	}
	/* ProcEnemyDirection */
	if(id == Goomba) goto init;
	if(id == Spiny){
		EF(Enemy_MovingDir) = 1;
		EF(Enemy_X_Speed) = 8;
		if(!(FrameCounter & 0x07)) goto init;
	}
	/* InvtD: facing the player already (inverted here) means turning around */
	if((u8)((PlayerEnemyDiff_(x) >> 7) + 1) == EF(Enemy_MovingDir)) ChkForBump_HammerBroJ_(x);
init:
	/* LandEnemyInitState: the normal state, or a shell with d6 cleared */
	EnemyLanding_(x);
	s = EF(Enemy_State);
	EF(Enemy_State) = s & (u8)((s8)s >> 7) & 0xbf;  /* d7 set: NMovShellFallBit, else 0 */
	return;
side:
	DoEnemySideCheck_(x);                           /* SChkA */
}

/* SetStun: knocked over: stunned state, a small hop away from the player */
void SetStun_(u8 x){
	SB(x, Enemy_ID);
	S(Enemy_State) = (S(Enemy_State) & 0xf0) | 0x02;
	u8 id = S(Enemy_ID);
	u8 *v = (u8 *)&Enemy_Y_Speed[x];
	v[RAO(Enemy_Y_Position) - RAO(Enemy_Y_Speed)] -= 2;
	v[0] = (id == Bloober || !AreaType) ? 0xff : 0xfd;   /* SetWYSpd / SetNotW */
	bool l = PlayerEnemyDiff_(x) & 0x80;            /* the enemy left of the player */
	if(id != BulletBill_CannonVar && id != BulletBill_FrenzyVar) S(Enemy_MovingDir) = l ? 2 : 1;
	S(Enemy_X_Speed) = l ? (s8)0xf0 : 0x10;         /* EnemyBGCXSpdData */
}

/* ChkToStunEnemies: a the enemy identifier (or what the caller left in A); $09 and $0d-$10
** are demoted to koopas, then stunned */
void ChkToStunEnemies_(u8 x, u8 a){
	if(a == 0x09 || (u8)(a - PiranhaPlant) < 4) Enemy_ID[x] = a & 0x01;   /* Demote */
	SetStun_(x);
}


/* EnemyJump: the jumping paratroopa (and the star) lands and jumps again */
void EnemyJump(void){
	u8 x = X_;
	SB(x, Enemy_Y_Speed);
	if((u8)(S(Enemy_Y_Position) + 0x3e) >= 0x44 && (u8)(S(Enemy_Y_Speed) + 2) >= 0x03){   /* SubtEnemyYPos */
		u8 a = bbchk(x + OFS_ENEMY, 0x15);
		x = ObjectOffset;
		if(a) *EnemyLanding_(x) = -3;
	}
	DoEnemySideCheck_(x);                           /* DoSide */
}

void EnemyToBGCollisionDet(void){
	u8 x = X_;
	SB(x, Enemy_ID);
	if(S(Enemy_State) & 0x20) return;
	if(!subt_y(x)) return;
	u8 id = S(Enemy_ID);
	if(id == Spiny && Enemy_Y_Position[x] < 0x25) return;
	/* DoIDCheckBGColl */
	if(id == GreenParatroopaJump){
		EnemyJump();
		return;
	}
	bool hb = id == HammerBro;
	if(!hb && id != Spiny && id != PowerUpObject && id >= 0x07) return;
	/* YesIn, or HammerBroBGColl's: the block under the enemy (non-solids 0), r as it is */
	u8 a = bbchk(x + OFS_ENEMY, 0x15);
	x = ObjectOffset;
	u8 *const E = ra_at(RAO(Enemy_ID) + x);
	u8 r = M03;
	if(r == 0x23){
		/* a block just hit under it: the hammer bro killed; else the block blanked so this
		** runs once (HandleEToBGCollision), a goomba killed, the others stunned */
		if(!hb){
			bb_clear_m02();
			a = EF(Enemy_ID);
			if(a >= 0x15) goto stun;
			if(a != Goomba) goto points;
		}
		ShellOrBlockDefeat_(x);                     /* KillEnemyAboveBlock */
		Enemy_Y_Speed[x] = -4;
		if(hb) return;
	points:
		/* GiveOEPoints: SetupFloateyNumber leaves its own A for ChkToStunEnemies */
		SetupFloateyNumber_(x, 0x01);
		a = Enemy_Rel_XPos;
	stun:
		ChkToStunEnemies_(x, a);
		return;
	}
	if(hb){
		/* HammerBroBGColl: UnderHammerBro, or NoUnderHammerBro */
		if(r && !EnemyFrameTimer[x]){
			EF(Enemy_State) &= 0x88;
			EnemyLanding_(x);
			DoEnemySideCheck_(x);
			return;
		}
		EF(Enemy_State) |= 0x01;
		return;
	}
	if(a) LandEnemyProperly(x, E);
	else ChkForRedKoopa_(x, E);                     /* NoEToBGCollision */
}

/* ---------------------------------------------------------------------------------------
** Fireballs against the background
** ------------------------------------------------------------------------------------ */

/* FireballBGCollision: X_ the fireball, left as it is */
void FireballBGCollision(void){
	u8 x = X_, a = 0;
	if(Fireball_Y_Position[x] >= 0x18){
		a = bbchk(x + OFS_FIREBALL, 0x1a);
		X_ = x = ObjectOffset;
	}
	SB(x, FireballBouncingFlag);
	if(a){
		u8 *v = (u8 *)&Fireball_Y_Speed[x];
		if((s8)*v < 0 || S(FireballBouncingFlag)){
			S(Fireball_State) = 0x80;                   /* InitFireballExplode */
			Square1SoundQueue = Sfx_Bump;
			return;
		}
		*v = 0xfd;
		S(FireballBouncingFlag) = 1;
		v[RAO(Fireball_Y_Position) - RAO(Fireball_Y_Speed)] &= 0xf8;
		return;
	}
	S(FireballBouncingFlag) = 0;                    /* ClearBounceFlag */
}

/* ---------------------------------------------------------------------------------------
** Bounding boxes. Their index is the BoundBoxCtrl slot: 0 player, 1-6 enemies, then
** BB_FIREBALL, BB_MISC; the box is at that index x 4
** ------------------------------------------------------------------------------------ */

/* BoundingBoxCore: the box of slot x from the relative position y; returns the box */
static u8 *bbox_core(u8 x, u8 y){
	SB(y, SprObject_Rel_XPos);
	u8 rx = S(SprObject_Rel_XPos), ry = S(ra.SprObject_Rel_YPos_);
	u8 *m = &M00;
	__asm__("" : "+z"(m));
	m[0] = x;
	m[1] = rx;
	m[2] = ry;
	const u8 *t = &BoundBoxCtrlData[(u8)(SprObj_BoundBoxCtrl[x] << 2)];
	u8 t0 = LPM_INC(t), t1 = LPM_INC(t), t2 = LPM_INC(t), t3 = LPM_INC(t);
	u8 *b = &BoundingBox_UL_Corner[(u8)(x << 2)];
	__asm__("" : "+z"(b));
	b[0] = rx + t0;
	b[1] = ry + t1;
	b[2] = rx + t2;
	b[3] = ry + t3;
	return b;
}

/* X_ the box slot, Y_ the relative position */
void BoundingBoxCore(void){
	bbox_core(X_, Y_);
}

/* BoundingBoxCore then CheckRightScreenBBox: x the slot, also read as a SprObject index (for a
** misc object that is misc + 9, block objects for misc 0-3: the 6502's). A box past the
** screen edge on the object's side is clamped to $ff (right) or $00 (left); X_ ObjectOffset.
** off: MoveBoundBoxOffscreen instead, the box all $ff (X_ left as it is) */
static void __attribute__((noinline, noclone)) box_and_check(u8 x, u8 rel, bool off){
	if(off){
		u8 *b = &BoundingBox_UL_Corner[(u8)(x << 2)];
		b[0] = b[1] = b[2] = b[3] = 0xff;
		return;
	}
	u8 *b = bbox_core(x, rel);
	u16 m = (((u16)ScreenLeft_PageLoc << 8) | ScreenLeft_X_Pos) + 0x80;
	M02 = m;
	M01 = m >> 8;
	SB(x, ra.SprObject_PageLoc_);
	bool r = (u16)(((u16)S(ra.SprObject_PageLoc_) << 8) | S(SprObject_X_Position)) >= m;
	__asm__("" : "+z"(b));
	if(r){
		if(!(b[2] & 0x80)){
			if(!(b[0] & 0x80)) b[0] = 0xff;
			b[2] = 0xff;                            /* SORte */
		}
	}else if(b[0] >= 0xa0){                         /* CheckLeftScreenBBox */
		if(b[2] & 0x80) b[2] = 0;
		b[0] = 0;                                   /* SOLft */
	}
	X_ = ObjectOffset;
}

void GetFireballBoundBox(void){
	box_and_check(X_ + BB_FIREBALL, REL_FBALL, false);
}

void GetMiscBoundBox(void){
	box_and_check(X_ + BB_MISC, REL_MISC, false);
}

/* GetMaskedOffScrBits: l the bitmask at or left of the left edge, r (M00) the one right of it */
static void GetMaskedOffScrBits_(u8 l, u8 r){
	M00 = r;
	u8 x = X_;
	u16 d = ((Enemy_PageLoc[x] << 8) | Enemy_X_Position[x])
	      - ((ScreenLeft_PageLoc << 8) | ScreenLeft_X_Pos);
	M01 = d;
	if((s16)d > 0) l = r;
	u8 a = l & Enemy_OffscreenBits;                 /* CMBits */
	EnemyOffscrBitsMasked[x] = a;
	box_and_check(x + OFS_ENEMY, REL_ENEMY, a);     /* SetupEOffsetFBBox, or offscreen */
}

void GetEnemyBoundBox(void){
	GetMaskedOffScrBits_(0x44, 0x48);
}

void SmallPlatformBoundBox(void){
	GetMaskedOffScrBits_(0x04, 0x08);
}

void LargePlatformBoundBox(void){
	u8 x = X_;
	X_ = x + OFS_ENEMY;
	GetXOffscreenBits();
	box_and_check(x + OFS_ENEMY, REL_ENEMY, A_ >= 0xfe);
}

/* SprObjectCollisionCore: boxes at offsets x and y (slot x 4); true if they overlap, wrapped
** boxes included; M06 the y, M07 the counter as the 6502 left it */
bool SprObjectCollisionCore_(u8 x, u8 y){
	const u8 *p = BoundingBox_UL_Corner + x, *q = BoundingBox_UL_Corner + y;
	M06 = y;
	bool c = false;
	s8 i = 1;
	do{
		u8 a = q[0], px = p[0], p2 = p[2], q2 = q[2];
		if(a >= px){
			/* FirstBoxGreater */
			if(a != px && a > p2 && (a <= q2 || q2 < px)) goto out;
		}else if(a < p2){
			/* SecondBoxVerticalChk */
			if(p2 >= px && q2 < px) goto out;
		}else if(a != p2){
			if(q2 >= a && q2 < px) goto out;
		}
		/* CollisionFound */
		p++;
		q++;
	}while(--i >= 0);
	c = true;                                       /* the counter ran out: $ff */
out:
	M07 = i;
	return c;
}
