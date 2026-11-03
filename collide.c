/*
 * Object collisions: a port of smbdis.asm lines 11060-11892 (fireballs, hammers, power-ups and
 * enemies against the player, enemies against each other, the player on platforms).
 * Routine names are the 6502's; registers and flags are A_ X_ Y_ C_ Z_ N_ (smb.h).
 * Routines only this module and bgcoll.c call take C parameters (Label_), and set only the
 * registers a caller reads. Bounding boxes are passed as the 6502's byte offsets into
 * BoundingBox_UL_Corner: player 0, enemy x (x + 1) * 4, fireball (x + BB_FIREBALL) * 4, misc
 * (x + BB_MISC) * 4.
 */
#include <avr/pgmspace.h>
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

#define RD(t, i) pgm_read_byte(&(t)[i])

/* jmp f with its arguments in r24 (a) and r22 (b): a tail call, which -mcall-prologues keeps
** gcc from making. Only at the end of a function that saves no register, itself noipa (or gcc
** reads the __builtin_unreachable as "never returns", and misses the callee's clobbers) */
#define NOIPA __attribute__((noipa))
	#define TAILJMP(f) do{ __asm__ volatile("jmp %x0" :: "i"(f) : "memory"); __builtin_unreachable(); }while(0)
	#define TAILJMP1(f, a) do{ register u8 a_ asm("r24") = (a); 		__asm__ volatile("jmp %x0" :: "i"(f), "r"(a_) : "memory"); __builtin_unreachable(); }while(0)
	#define TAILJMP2(f, a, b) do{ register u8 a_ asm("r24") = (a); register u8 b_ asm("r22") = (b); 		__asm__ volatile("jmp %x0" :: "i"(f), "r"(a_), "r"(b_) : "memory"); __builtin_unreachable(); }while(0)

/* ---------------------------------------------------------------------------------------
** Data (ResidualXSpdData, $18 $e8, is never read)
** ------------------------------------------------------------------------------------ */
static const u8 BowserIdentities[8] PROGMEM = {
	Goomba, GreenKoopa, BuzzyBeetle, Spiny, Lakitu, Bloober, HammerBro, Bowser
};
static const u8 KickedShellPtsData[3] PROGMEM = { ROM_KickedShellPtsData };
static const u8 SetBitsMask[7] PROGMEM = { ROM_SetBitsMask };

static inline u8 enemy_bbox(u8 x){ return (u8)((x + OFS_ENEMY) * 4); }

static void EnemyTurnAround_(u8 x);

/* InitVStf: the enemy's vertical speed and force cleared */
INL void init_vstf(u8 x){
	Enemy_Y_Speed[x] = 0;
	Enemy_Y_MoveForce[x] = 0;
}

/* ---------------------------------------------------------------------------------------
** Fireballs
** ------------------------------------------------------------------------------------ */

NOIPA static void EnemySmackScore(u8 x, u8 a){
	Square1SoundQueue = Sfx_EnemySmack;
	TAILJMP2(SetupFloateyNumber_, x, a);
}

/* HandleEnemyFBallCol: enemy e (M01) hit: Bowser loses a hit point, buzzy beetles are
** fireproof, others die */
INL void HandleEnemyFBallCol_(u8 e){
	X_ = e;
	RelativeEnemyPosition();                           /* leaves M01 */
	u8 x = e;
	u8 f = Enemy_Flag[x];
	if(f & 0x80){                                      /* a duplicate: its low nybble the original */
		u8 o = f & 0x0f;
		if(Enemy_ID[o] == Bowser){
			x = o;
			goto HurtBowser;
		}
	}
	u8 id = Enemy_ID[x];                               /* ChkBuzzyBeetle */
	if(id == BuzzyBeetle) return;
	if(id != Bowser){
		if(id == BulletBill_FrenzyVar || id == Podoboo || id >= 0x15) return;   /* ChkOtherEnemies */
		ShellOrBlockDefeat_(x);
		return;
	}
HurtBowser:
	if(--BowserHitPoints) return;
	u8 y = WorldNumber, n = RD(BowserIdentities, y);   /* what Bowser really was */
	SB(x, Enemy_ID);
	Enemy_Y_MoveForce[x] = 0;                          /* InitVStf */
	S(Enemy_X_Speed) = 0;
	EnemyFrenzyBuffer = 0;
	S(Enemy_Y_Speed) = -2;
	S(Enemy_ID) = n;
	S(Enemy_State) = (y < 3) ? 0x23 : 0x20;            /* SetDBSte */
	Square2SoundQueue = Sfx_BowserFall;
	EnemySmackScore(e, 0x09);                          /* 5000 points */
}

/* FireballEnemyCDLoop: fireball box yb against enemy e (M01) */
INL void fball_enemy(u8 e, u8 yb){
	SB(e, Enemy_Flag);
	u8 s = S(Enemy_State), id = S(Enemy_ID);
	if(!(s & 0x20) && S(Enemy_Flag)
	   && (u8)(id - 0x24) >= 7                         /* platforms and the like */
	   && !(id == Goomba && s >= 2)                    /* GoombaDie */
	   && !EnemyOffscrBitsMasked[e]                    /* NotGoomba */
	   && SprObjectCollisionCore_(enemy_bbox(e), yb)){
		Fireball_State[ObjectOffset] = 0x80;
		HandleEnemyFBallCol_(e);
	}
}

/* FireballEnemyCollision: fireball X_ against enemy slots 4 to 0, every other frame */
void FireballEnemyCollision(void){
	u8 x = X_;
	u8 st = Fireball_State[x];
	if(st && !(st & 0x80) && !(FrameCounter & 1)){
		u8 yb = (u8)((x + BB_FIREBALL) * 4);
		u8 e = 5;
		do{
			M01 = --e;                                 /* nothing in the loop writes it */
			fball_enemy(e, yb);
		}while(e);                                     /* NoFToECol */
	}
	X_ = ObjectOffset;                                 /* ExitFBallEnemy */
}

/* ShellOrBlockDefeat: an enemy killed by a shell, a block from below, a fireball or a star */
void ShellOrBlockDefeat_(u8 x){
	SB(x, Enemy_ID);
	u8 a = S(Enemy_ID);
	if(a == PiranhaPlant){
		a = Enemy_Y_Position[x] + 0x18 + 1;            /* the cmp left the carry set */
		Enemy_Y_Position[x] = a;
	}
	ChkToStunEnemies_(x, a);                           /* StnE: tests A */
	S(Enemy_State) = (S(Enemy_State) & 0x1f) | 0x20;
	u8 y = S(Enemy_ID);
	a = 0x02;                                          /* 200 points */
	if(y == HammerBro) a = 0x06;                       /* 1000 */
	if(y == Goomba) a = 0x01;                          /* GoombaPoints: 100 */
	EnemySmackScore(x, a);
}

/* ---------------------------------------------------------------------------------------
** Hammers and power-ups
** ------------------------------------------------------------------------------------ */

/* PlayerHammerCollision: a hammer (misc X_) hits the player once and bounces back */
void PlayerHammerCollision(void){
	u8 x = X_;
	if(!(FrameCounter & 1)) return;
	if(TimerControl | Misc_OffscreenBits) return;
	bool c = SprObjectCollisionCore_(0, (u8)((x + BB_MISC) * 4));
	x = ObjectOffset;
	X_ = x;
	u8 f = Misc_Collision_Flag[x];
	Misc_Collision_Flag[x] = c;                        /* ClHCol, or set */
	if(!c || f) return;
	Misc_X_Speed[x] = -Misc_X_Speed[x];
	if(!StarInvincibleTimer) InjurePlayer();
}

/* SetPRout: GameEngineSubroutine a, Player_State y, timers halted */
static __attribute__((noinline)) void SetPRout_(u8 a, u8 y){
	GameEngineSubroutine = a;
	Player_State = y;
	TimerControl = 0xff;
	ScrollAmount = 0;
	Y_ = 0;
}

/* HandlePowerUpCollision: the power-up taken: mushroom, fire flower, star or 1-up */
INL void HandlePowerUpCollision_(u8 x){
	X_ = x;
	EraseEnemyObject();
	u8 t = PowerUpType;
	SetupFloateyNumber_(x, (t == 3) ? 0x0b : 0x06);    /* 1000 points, SetFor1Up: a 1-up */
	Square2SoundQueue = Sfx_PowerUpGrab;
	if(t >= 2){
		if(t != 3){
			StarInvincibleTimer = 0x23;
			AreaMusicQueue = StarPowerMusic;
		}
		return;
	}
	u8 s = PlayerStatus, a = 0x09;                     /* Shroom_Flower_PUp */
	if(s >= 2) return;                                 /* NoPUp */
	PlayerStatus = s + 1;                              /* UpToSuper, or to fiery */
	if(s){
		a = 0x0c;
		GetPlayerColors();
	}
	SetPRout_(a, 0);                                   /* UpToFiery */
}

/* ---------------------------------------------------------------------------------------
** Player against enemy
** ------------------------------------------------------------------------------------ */
INL void HandlePECollisions(u8 x, u8 id, u8 *sb_);
INL void ChkForPlayerInjury(u8 x, u8 id, u8 *sb_);
INL void EnemyStomped(u8 x, u8 id, u8 *sb_);

/* CheckPlayerVertical: true when the player is offscreen (vertically) or below row $d0 */
static bool CheckPlayerVertical_(void){
	if(Player_OffscreenBits >= 0xf0) return true;
	return Player_Y_HighPos == 1 && Player_Y_Position >= 0xd0;
}

/* PlayerEnemyCollision: enemy X_ against the player, every other frame */
void PlayerEnemyCollision(void){
	u8 x = X_;
	if(FrameCounter & 1) return;                       /* NoPUp */
	if(!CheckPlayerVertical_() && !EnemyOffscrBitsMasked[x] && GameEngineSubroutine == 0x08
	   && !(Enemy_State[x] & 0x20)){
		u8 o = ObjectOffset;
		bool c = SprObjectCollisionCore_(0, enemy_bbox(o));
		x = o;
		if(!c) Enemy_CollisionBits[x] &= 0xfe;
		else{
			SB(x, Enemy_ID);
			u8 id = S(Enemy_ID);                       /* CheckForPUpCollision */
			if(id == PowerUpObject) HandlePowerUpCollision_(x);
			else if(StarInvincibleTimer) ShellOrBlockDefeat_(x);   /* EColl */
			else HandlePECollisions(x, id, sb_);
		}
	}
	X_ = x;                                            /* NoPECol */
}

/* EnemyFacePlayer: the enemy faces the player, its speed v that way (-v to the left) */
static __attribute__((noinline)) void face_speed(u8 x, u8 v){
	u8 d = 1;
	if((s8)PlayerEnemyDiff_(x) < 0){                  /* the enemy left of the player */
		d = 2;
		v = -v;
	}
	SB(x, Enemy_MovingDir);
	S(Enemy_MovingDir) = d;
	S(Enemy_X_Speed) = v;
}

/* HandlePECollisions: hurts the player, kicks a shell, or is stomped */
INL void HandlePECollisions(u8 x, u8 id, u8 *sb_){
	const u16 so_ = RAO(Enemy_ID);
	u8 *cb = &Enemy_CollisionBits[x];
	if((*cb & 0x01) || EnemyOffscrBitsMasked[x]) return;   /* ExPEC */
	*cb |= 0x01;
	if(id == Spiny || id == BulletBill_CannonVar) goto inj;
	if(id == PiranhaPlant || id == Podoboo || id >= 0x15 || AreaType == 0){
		InjurePlayer();
		return;
	}
	u8 s = S(Enemy_State);
	if((s & 0x80) || (s & 0x07) < 0x02) goto inj;     /* moving shell, or normal or falling */
	if(id == Goomba) return;                           /* a defeated goomba */
	S(Enemy_State) = s | 0x80;                         /* kicks the shell */
	face_speed(x, 0x30);                               /* KickedShellXSpdData */
	u8 a = 3 + StompChainCounter;
	u8 y = EnemyIntervalTimer[x];
	if(y < 3) a = RD(KickedShellPtsData, y);           /* about to revive: more points */
	EnemySmackScore(x, a);                             /* KSPts, and the smack sound */
	return;
inj:
	ChkForPlayerInjury(x, id, sb_);
}

/* ChkForPlayerInjury: a stomp when falling onto it, else hurt (turning the enemy around) */
INL void ChkForPlayerInjury(u8 x, u8 id, u8 *sb_){
	const u16 so_ = RAO(Enemy_ID);
	if(Player_Y_Speed > 0
	   || (id >= Bloober && (u8)(Player_Y_Position + 0x0c) < Enemy_Y_Position[x])   /* ChkInj */
	   || StompTimer){                                 /* ChkETmrs */
		EnemyStomped(x, id, sb_);
		return;
	}
	if(InjuryTimer) return;                            /* ExInjColRoutines */
	/* ChkEnemyFaceRight / TInjE: LInj turns the enemy around first */
	u8 d = S(Enemy_MovingDir);
	if(Player_Rel_XPos < Enemy_Rel_XPos){
		if(d != 1) goto inj;
	}else if(d == 1) goto inj;
	EnemyTurnAround_(x);
inj:
	InjurePlayer();
}

/* ForceInjury: the player shrinks to status a, or dies when small */
NOIPA static void ForceInjury_(u8 a){
	if(!PlayerStatus){
		Player_X_Speed = 0;                            /* KillPlayer */
		EventMusicQueue = DeathMusic;
		Player_Y_Speed = -4;
		a = 0x0b;
	}else{
		PlayerStatus = a;
		InjuryTimer = 0x08;
		Square1SoundQueue = Sfx_PipeDown_Injury;
		GetPlayerColors();
		a = 0x0a;
	}
	TAILJMP2(SetPRout_, a, 0x01);                      /* SetKRout */
}

/* InjurePlayer: unless still invincible from the last hit (the 6502's X_ ObjectOffset: every
** caller has it there already or reloads it) */
void InjurePlayer(void){
	if(!InjuryTimer) ForceInjury_(0);
}

/* A_: the new PlayerStatus when not small (X_ ObjectOffset unread: the caller returns) */
NOIPA void ForceInjury(void){
	TAILJMP1(ForceInjury_, A_);
}

/* EnemyStomped: points, the bounce, and the enemy stunned, demoted or made a shell */
INL void EnemyStomped(u8 x, u8 id, u8 *sb_){
	const u16 so_ = RAO(Enemy_ID);
	if(id == Spiny){
		InjurePlayer();
		return;
	}
	Square1SoundQueue = Sfx_EnemyStomp;
	u8 p = 0x02, st = 0x20;                            /* StompedEnemyPtsData; the new state */
	if(id == FlyingCheepCheep || id == BulletBill_FrenzyVar || id == BulletBill_CannonVar
	   || id == Podoboo) ;
	else if(id == HammerBro || id == Bloober) p = 0x06;
	else if(id == Lakitu) p = 0x05;
	else if(id < 0x09){                                /* ChkForDemoteKoopa */
		st = 0x04;                                     /* HandleStompedShellE */
		p = ++StompChainCounter + StompTimer++;
		EnemyIntervalTimer[x] = PrimaryHardMode ? 0x0b : 0x10;   /* RevivalRateData */
	}else{
		st = 0;                                        /* a paratroopa becomes a troopa */
		p = 0x03;                                      /* 400 points */
	}
	SetupFloateyNumber_(x, p);                         /* EnemyStompedPts */
	s8 b = -4;                                         /* SBnce */
	if(st == 0x20){
		u8 dir = S(Enemy_MovingDir);
		SetStun_(x);
		S(Enemy_MovingDir) = dir;
		S(Enemy_X_Speed) = 0;
		b = -3;
	}else if(!st){
		S(Enemy_ID) = id & 0x01;
		face_speed(x, 0x08);                           /* DemotedKoopaXSpdData */
	}
	S(Enemy_State) = st;
	if(st != 0x04) init_vstf(x);
	Player_Y_Speed = b;
}

/* SetupFloateyNumber: points control a at enemy x's position */
void SetupFloateyNumber_(u8 x, u8 a){
	SB(x, Enemy_Y_Position);
	S(FloateyNum_Control) = a;
	S(FloateyNum_Timer) = 0x30;
	S(FloateyNum_Y_Pos) = S(Enemy_Y_Position);
	S(FloateyNum_X_Pos) = Enemy_Rel_XPos;
}

/* A_ the points control, X_ the enemy (A_ is left as it was: no caller reads it) */
NOIPA void SetupFloateyNumber(void){
	TAILJMP2(SetupFloateyNumber_, X_, A_);
}

/* ---------------------------------------------------------------------------------------
** Enemy against enemy
** ------------------------------------------------------------------------------------ */
INL bool no_enemy_coll(u8 x){
	u8 id = Enemy_ID[x];
	return id >= 0x15 || id == Lakitu || id == PiranhaPlant || EnemyOffscrBitsMasked[x];
}

/* The shell (or second shell) k kills v: v's points by k's chain counter, which goes up */
INL void shell_kill(u8 v, u8 k){
	ShellOrBlockDefeat_(v);
	u8 c = ShellChainCounter[k]++;
	SetupFloateyNumber_(v, c + 4);
}

/* ProcEnemyCollisions: x (ObjectOffset) and y (M01), states sx and sy, touch: shells kill,
** others turn around */
INL void ProcEnemyCollisions_(u8 x, u8 y, u8 sx, u8 sy){
	if((sy | sx) & 0x20) return;
	u8 s = x, v = y;                                   /* the shell and its victim */
	if(sx < 0x06){
		/* ProcSecondEnemyColl */
		if(sy < 0x06){
			EnemyTurnAround_(y);                       /* MoveEOfs: both turn around */
			EnemyTurnAround_(x);
			return;
		}
		s = y;                                         /* the shell y kills x */
		v = x;
		sy = 0;
	}
	if(Enemy_ID[s] == HammerBro) return;
	if(sy & 0x80){                                     /* two shells: x dies too */
		SetupFloateyNumber_(x, 0x06);
		ShellOrBlockDefeat_(x);
	}
	shell_kill(v, s);                                  /* ShellCollisions */
}

/* ECLoop: enemy o (ObjectOffset, mask m in the collision bits) against enemy e (M01) */
INL void enemy_enemy(u8 o, u8 m, u8 e){
	SB(e, Enemy_Flag);
	if(!S(Enemy_Flag)) return;
	u8 id = S(Enemy_ID);
	if(id >= 0x15 || id == Lakitu || id == PiranhaPlant || EnemyOffscrBitsMasked[e]) return;
	u8 *cb = &Enemy_CollisionBits[e];
	if(!SprObjectCollisionCore_(enemy_bbox(e), enemy_bbox(o))){   /* GetEnemyBoundBoxOfs */
		*cb &= ~m;                                     /* NoEnemyCollision: ClearBitsMask */
		return;
	}
	u8 so = Enemy_State[o], se = S(Enemy_State);
	if(!((so | se) & 0x80)){
		if(*cb & m) return;                            /* already touching */
		*cb |= m;
	}
	ProcEnemyCollisions_(o, e, so, se);                /* YesEC */
}

/* EnemiesCollision: enemy X_ against every lower slot, every other frame, not in water (X_ is
** left as it is: the only caller reloads it) */
void EnemiesCollision(void){
	u8 x = X_;
	if(!(FrameCounter & 1) || !AreaType || no_enemy_coll(x)) return;   /* ExSFN */
	u8 o = ObjectOffset, m = RD(SetBitsMask, o);       /* nothing in the loop writes them */
	while((s8)--x >= 0){
		M01 = x;
		enemy_enemy(o, m, x);
	}
}

/* EnemyTurnAround: walkers, spinies and jumping paratroopas turn around */
NOIPA static void EnemyTurnAround_(u8 x){
	u8 id = Enemy_ID[x];
	if((id < 0x07 && id != HammerBro) || id == Spiny || id == GreenParatroopaJump) TAILJMP1(RXSpd_, x);
}

/* RXSpd: reverses the enemy's speed and direction */
void RXSpd_(u8 x){
	SB(x, Enemy_MovingDir);
	S(Enemy_X_Speed) = -S(Enemy_X_Speed);
	S(Enemy_MovingDir) ^= 0x03;
}

/* ---------------------------------------------------------------------------------------
** The player on platforms
** ------------------------------------------------------------------------------------ */

/* ProcLPlatCollisions: platform x, box y: stops a jump from below, lands the player on top,
** or blocks a side */
NOIPA static void ProcLPlatCollisions_(u8 x, u8 y){
	const u8 *e = BoundingBox_UL_Corner + y, *p = BoundingBox_UL_Corner;
	s8 v = Player_Y_Speed;
	if((u8)(e[3] - p[1]) < 0x04 && v < 0)
		Player_Y_Speed = v = 1;                        /* kills the jump */
	/* ChkForTopCollision */
	if((u8)(p[3] - e[1]) < 0x06 && v >= 0){
		u8 a = M00;                                    /* small platforms: the box counter */
		u8 id = Enemy_ID[x];
		if(id != 0x2b && id != 0x2c) a = x;            /* else the platform's slot */
		PlatformCollisionFlag[ObjectOffset] = a;       /* SetCollisionFlag */
		Player_State = 0;
		return;
	}
	/* PlatformSideCollisions */
	M00 = 0x01;                                        /* left side */
	if((u8)(p[2] - e[0]) >= 0x08){
		M00 = 0x02;                                    /* right side */
		if((u8)(e[2] - p[0] - 1) >= 0x09) return;      /* NoSideC */
	}
	TAILJMP(ImpedePlayerMove);                         /* SideC */
}

static void ChkForPlayerC_LargeP_(u8 x){
	if(CheckPlayerVertical_()) return;
	u8 y = enemy_bbox(x);                              /* GetEnemyBoundBoxOfsArg */
	M00 = Enemy_Y_Position[x];
	if(SprObjectCollisionCore_(0, y)) ProcLPlatCollisions_(x, y);
}

/* LargePlatformCollision: the player against a large platform (and a balance pair's other) */
void LargePlatformCollision(void){
	u8 x = X_;
	PlatformCollisionFlag[x] = 0xff;                   /* no collision */
	SB(x, Enemy_ID);
	u8 s = S(Enemy_State);
	if(TimerControl || (s & 0x80)) return;
	if(S(Enemy_ID) == 0x24){                           /* balance platform: the other first */
		ChkForPlayerC_LargeP_(s);
		x = ObjectOffset;
	}
	ChkForPlayerC_LargeP_(x);
}

/* SmallPlatformCollision: the player against both halves of a small platform, the second
** half 128 pixels below the first */
void SmallPlatformCollision(void){
	u8 x = X_;
	if(TimerControl) return;
	PlatformCollisionFlag[x] = 0;
	if(CheckPlayerVertical_()) return;
	M00 = 0x02;
	u8 o = ObjectOffset;                               /* ChkSmallPlatLoop: the loop writes */
	u8 y = enemy_bbox(o);                              /* neither these nor the offscreen bits */
	if(Enemy_OffscreenBits & 0x02) return;
	u8 *b = BoundingBox_UL_Corner + y;
	do{
		if(b[1] >= 0x20 && SprObjectCollisionCore_(0, y)){
			ProcLPlatCollisions_(o, y);                /* ProcSPlatCollisions */
			return;
		}
		b[1] += 0x80;                                  /* MoveBoundBox */
		b[3] += 0x80;
	}while(--M00);
}

/* PositionPlayer: the player stands on platform x, d below its top */
static void PositionPlayer(u8 d){
	if(GameEngineSubroutine == 0x0b) return;           /* dying */
	SB(X_, Enemy_Y_HighPos);
	if(S(Enemy_Y_HighPos) != 0x01) return;
	u16 a = (0x100 | (u8)(S(Enemy_Y_Position) + d)) - 0x20;   /* high byte 1 less the borrow */
	Player_Y_Position = a;
	Player_Y_HighPos = a >> 8;
	Player_Y_Speed = 0;
	Player_Y_MoveForce = 0;
}

/* PositionPlayerOnS_Plat: on the small platform half A_ (1 lower, 2 upper), X_ */
NOIPA void PositionPlayerOnS_Plat(void){
	u8 d = 0x80;                                       /* PlayerPosSPlatData */
	if(A_ != 1) d = 0x00;
	TAILJMP1(PositionPlayer, d);
}

/* PositionPlayerOnVPlat: the player stands on platform X_ */
NOIPA void PositionPlayerOnVPlat(void){
	TAILJMP1(PositionPlayer, 0);
}
