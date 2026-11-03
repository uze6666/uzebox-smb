/*
 * The sprite drawing: a port of smbdis.asm's Draw* and *GfxHandler routines (lines 887-953,
 * 2640-2757, 10443-10554, 13101-14584, 15000-15044, and the drawing tails of
 * FloateyNumbersRoutine and ProcBowserFlame). They write the virtual OAM (gfx.h) exactly as
 * the 6502 wrote the NES's: NES tile numbers, NES attribute bytes, NES screen coordinates.
 * gfx17.c converts it afterwards. Routine names are the 6502's.
 *
 * An OAM offset is a multiple of 4, so a sprite's four bytes never wrap; from one sprite to
 * the next the offset is a u8 and wraps at 256, as the 6502's y did. A routine that reworks
 * its sprites after drawing them composes them in a local buffer, then copies it (flush).
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

#define PB(t, i) pgm_read_byte(&(t)[i])
#define NOINLINE __attribute__((noinline))
#define RO(a, b) ((int)((const u8 *)(b) - (const u8 *)(a)))   /* array a to array b, in bytes */
/* jmp f, a tail call (-mcall-prologues makes none): only at the end of a function that saves
** no register, and that function noipa (see objects.c) */
	#define TAILJMP(f) do{ __asm__ volatile("jmp %x0" :: "i"(f) : "memory"); __builtin_unreachable(); }while(0)
	#define LPM_INC(p) __extension__({ u8 r_; __asm__("lpm %0, Z+" : "=r"(r_), "+z"(p)); r_; })
	#define IN_REG(p, r) __asm__("" : "+" r(p))
	#define OAM(o) oam_at(o)
	_Static_assert(((u16)OAM_BASE & 0xff) == 0, "the OAM on a page");
	/* the OAM's page and o as the two bytes of the address (mov, ldi) */
	static inline u8 *oam_at(u8 o){
		union { u16 w; struct { u8 lo, hi; } b; } u;
		u.b.lo = o;
		u.b.hi = (u16)OAM_BASE >> 8;
		return (u8 *)u.w;
	}
	#define OAMZ(o) oam_z(o)
	#define OAM_NEXT(s) __asm__("subi %A0, lo8(-4)" : "+z"(s))
	static inline u8 *oam_z(u8 o){
		u8 *p;
		__asm__("mov %A0, %1\n\tldi %B0, %2" : "=z"(p) : "r"(o), "M"((u16)OAM_BASE >> 8));
		return p;
	}

/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
/* FloateyNumTileData: objects.c (objects.h); StarFlag*: bosses.c (bosses.h);
** PlayerGfxTblOffsets: player.c (player.h) */

/* SprShuffleAmt (as stored, by index), then DefaultSprOffsets */
static const u8 DefaultSprOffsets[18] PROGMEM = { 0x58,0x48,0x38, ROM_DefaultSprOffsets };

/* the third entry is what the C read past the 6502's two (FirstSprXPos[0]) */
static const u8 VineYPosAdder[3] PROGMEM = { 0x00,0x30,0x04 };

/* by hammer pose, its two sprites as a template (tmpl): FirstSprYPos, FirstSprTilenum,
** HammerSprAttrib, FirstSprXPos, then the same plus SecondSprYPos/XPos, SecondSprTilenum */
static const u8 HammerSprData[4][8] PROGMEM = {
	{ 0x00,0x80,0x03,0x04, 0x08,0x81,0x03,0x04 },
	{ 0x04,0x82,0x03,0x00, 0x04,0x83,0x03,0x08 },
	{ 0x00,0x81,0xc3,0x04, 0x08,0x80,0xc3,0x04 },
	{ 0x04,0x83,0xc3,0x00, 0x04,0x82,0xc3,0x08 },
};

static const u8 FlagpoleScoreNumTiles[10] PROGMEM = { ROM_FlagpoleScoreNumTiles };

/* a row of sprites 8 pixels apart, palette 2, tile 0 (moved on by TMPL) */
static const u8 RowSprData[24] PROGMEM = {
	0x00,0x00,0x02,0x00, 0x00,0x00,0x02,0x08, 0x00,0x00,0x02,0x10,
	0x00,0x00,0x02,0x18, 0x00,0x00,0x02,0x20, 0x00,0x00,0x02,0x28
};

/* top left, right, bottom left, right */
static const u8 PowerUpGfxTable[16] PROGMEM = { ROM_PowerUpGfxTable };   /* regular mushroom; fire flower; star; 1-up mushroom */

/* PowerUpAttributes: 2, 1, 2, 1 (computed) */

/* top left, right, middle left, right, bottom left, right; read past offset $ff (jumpspring
** frame 3) as the 6502's absolute,x did */
static const u8 EnemyGraphicsTable[258] PROGMEM = { ROM_EnemyGraphicsTable };   /* buzzy beetle frame 1; frame 2; koopa troopa frame 1; frame 2; koopa paratroopa frame 1; frame 2; spiny frame 1; frame 2; spiny's egg frame 1; frame 2; bloober frame 1; frame 2; cheep-cheep frame 1; frame 2; goomba; koopa shell frame 1 (upside-down); frame 2; koopa shell frame 1 (rightside up); frame 2; buzzy beetle shell frame 1 (rightside up); frame 2; buzzy beetle shell frame 1 (upside-down); frame 2; defeated goomba; lakitu frame 1; frame 2; princess; mushroom retainer; hammer bro frame 1; frame 2; frame 3; frame 4; piranha plant frame 1; frame 2; podoboo; bowser front frame 1; bowser rear frame 1; front frame 2; rear frame 2; bullet bill; jumpspring frame 1; frame 2; frame 3 */

static const u8 EnemyGfxTableOffsets[27] PROGMEM = { ROM_EnemyGfxTableOffsets };

static const u8 EnemyAttributeData[27] PROGMEM = { ROM_EnemyAttributeData };

/* EnemyAnimTimingBMask: only its first entry, $08, is ever read */
static const u8 JumpspringFrameOffsets[5] PROGMEM = { ROM_JumpspringFrameOffsets };

static const u8 DefaultBlockObjTiles[4] PROGMEM = { ROM_DefaultBlockObjTiles };   /* brick w/ line */

static const u8 ExplosionTiles[3] PROGMEM = { ROM_ExplosionTiles };

/* tiles 2 a row, top to bottom */
static const u8 PlayerGraphicsTable[216] PROGMEM = { ROM_PlayerGraphicsTable };   /* big player; walking frame 1; frame 2; frame 3; skidding; jumping; swimming frame 1; frame 2; frame 3; climbing frame 1; frame 2; crouching; fireball throwing; small player; walking frame 1; frame 2; frame 3; skidding; jumping; swimming frame 1; frame 2; frame 3; climbing frame 1; frame 2; killed; both sizes; small player standing; intermediate grow frame; big player standing */

#define SwimTileRepOffset 0x48                  /* the asm's PlayerGraphicsTable + $9e */
/* SwimKickTileNum: $31, $46 */

static const u8 IntermediatePlayerData[5] PROGMEM = { 0x5b,0x01,0x00,0x50,0xff };   /* M07: 4; y and x are this
** screen's (text.c): beside the lives, under WORLD (the 6502's: $58, $60) */

/* ---------------------------------------------------------------------------------------
** Helpers
** ------------------------------------------------------------------------------------ */

/* sprite k of those from o offscreen for each bit k of m */
static NOINLINE void hide_mask(u8 o, u8 m){
	for(; m; m >>= 1, o += 4){
		if(m & 0x01) *OAMZ(o) = 0xf8;
	}
}

/* TMPL: n sprites of a template d (PROGMEM) into the OAM from o, the Y bytes moved down by
** y, the tiles on by t, the X bytes right by x (packed in two register pairs) */
#define TMPL(o, d, n, y, t, x) tmpl(o, d, (u8)(n) | (u16)(u8)(t) << 8, (u8)(y) | (u16)(u8)(x) << 8)
static NOINLINE void tmpl(u8 o, const u8 *d, u16 nt, u16 yx){
	u8 n = nt;
	do{
		u8 *s = OAM(o);
		s[0] = LPM_INC(d) + (u8)yx;
		s[1] = LPM_INC(d) + (nt >> 8);
		s[2] = LPM_INC(d);
		s[3] = LPM_INC(d) + (yx >> 8);
		o += 4;
	}while(--n);
}

/* n bytes composed at b into the OAM from o */
static NOINLINE void flush(u8 o, const u8 *b, u8 n){
	do{
		*OAM(o) = *b++;
		o++;
	}while(--n);
}

/* DrawSpriteObject, n rows: tiles M00/M01 (from t, 2 a row) side by side at M05,M02,
** swapped and flipped by M03 d1, attributes M04, into p (8 bytes a row); M02 moves down 8 */
static NOINLINE void rows(u8 *p, const u8 *t, u8 n){
	u8 *m = ra.M_;
	IN_REG(m, "z");
	u8 a = m[4], x = m[5], y = m[2], f = m[3], t0, t1;
	if(f & 0x02) a |= 0x40;
	do{
		t0 = LPM_INC(t);
		t1 = LPM_INC(t);
		IN_REG(p, "y");
		if(f & 0x02){ p[1] = t1; p[5] = t0; }
		else{ p[1] = t0; p[5] = t1; }
		p[0] = y; p[2] = a; p[3] = x;
		p[4] = y; p[6] = a; p[7] = x + 8;
		p += 8;
		y += 8;
		IN_REG(y, "r");
	}while(--n);
	m = ra.M_;
	IN_REG(m, "z");
	m[0] = t0;
	m[1] = t1;
	m[2] = y;
}

/* attributes l and r into the left and right sprites of n rows at p (n constant: unrolled) */
static inline __attribute__((always_inline)) void cols(u8 *p, u8 l, u8 r, u8 n){
	p[2] = l;
	p[6] = r;
	if(n > 1){
		p[10] = l;
		p[14] = r;
	}
	if(n > 2){
		p[18] = l;
		p[22] = r;
	}
}

/* ---------------------------------------------------------------------------------------
** OAM offsets: initial values and the flicker shuffle
** ------------------------------------------------------------------------------------ */

_Static_assert(offsetof(struct RamArea, SprDataOffset_) == offsetof(struct RamArea, SprShuffleAmt_) + 3,
	"SprShuffleAmt and SprDataOffset are contiguous");

/* SecondaryGameSetup's ShufAmtLoop: the shuffle amounts and the default OAM offsets.
** Sprite0Data is not loaded: there is no sprite 0 hit here */
void InitSprOffsets(void){
	u8 *d = (u8 *)&ra + offsetof(struct RamArea, SprShuffleAmt_);
	for(u8 i = 0; i < 18; i++) d[i] = PB(DefaultSprOffsets, i);
}

/* SpriteShuffler: rotates the OAM offsets from sprite 10 up, for flicker, and derives the
** misc objects' offsets from three of them */
void SpriteShuffler(void){
	M00 = 0x28;                                   /* the preset: sprite 10 */
	u8 amt = SprShuffleAmt[SprShuffleAmtOffset];
	u8 *p = SprDataOffset;
	for(u8 i = 15; i; i--){
		u8 a = *p;
		if(a >= 0x28){
			u16 s = a + amt;
			a = s;
			if(s >> 8) a += 0x28;                 /* past $ff: add the preset (carry was clear) */
			*p = a;
		}
		p++;
	}
	u8 x = SprShuffleAmtOffset + 1;
	if(x == 3) x = 0;
	SprShuffleAmtOffset = x;
	u8 *m = Misc_SprDataOffset;
	for(u8 i = 5; i < 8; i++){
		u8 a = SprDataOffset[i];
		*m++ = a;
		*m++ = a + 8;
		*m++ = a + 16;
	}
	X_ = 0xff; Y_ = 0xff;
}

/* MoveSpritesOffscreen: all but sprite 0 */
void MoveSpritesOffscreen(void){
	u8 o = 4;
	do{
		*OAM(o) = 0xf8;
		o += 4;
	}while(o);
	A_ = 0xf8; Y_ = 0;
}

/* MoveAllSpritesOffscreen: every sprite's Y to $f8 (MoveSpritesOffscreen from sprite 0) */
void MoveAllSpritesOffscreen(void){
	u8 o = 0;
	do{
		*OAM(o) = 0xf8;
		o += 4;
	}while(o);
	A_ = 0xf8; Y_ = 0;
}

/* SprObjectOffscrChk: moves the enemy's columns and rows offscreen from its offscreen bits,
** and erases it when wholly offscreen below the screen (not a podoboo) */
static void spr_object_offscr_chk(void){
	u8 x = X_ = ObjectOffset;
	u8 b = Enemy_OffscreenBits, m = 0;
	if(b & 0x04) m |= 0x2a;                       /* right column */
	if(b & 0x08) m |= 0x15;                       /* left column */
	if(b & 0x20) m |= 0x30;                       /* third row */
	if(b & 0x40) m |= 0x0c;                       /* second and third rows */
	if(b & 0x80) m |= 0x03;                       /* all of them (A was 0 by then) */
	hide_mask(Enemy_SprDataOffset[x], m);
	x = X_;                                       /* hide_mask keeps X_ and the RAM */
	if((Enemy_OffscreenBits & 0x80) && Enemy_ID[x] != Podoboo && Enemy_Y_HighPos[x] == 0x02)
		EraseEnemyObject();
}

/* ---------------------------------------------------------------------------------------
** Vine, hammer, flagpole, platforms, coins, power-up
** ------------------------------------------------------------------------------------ */

/* DrawVine: six staggered vine sprites for vine slot Y_ (returned in Y_), hidden above the
** vine's start */
void DrawVine(void){
	u8 v = Y_;
	M00 = v;
	u8 a = Enemy_Rel_YPos + PB(VineYPosAdder, v);
	u8 y = Enemy_SprDataOffset[VineObjOffset[v]];
	M02 = y;
	u8 rx = Enemy_Rel_XPos, vs = VineStart_Y_Position, at = 0x21;
	u8 k = rx ^ (u8)(rx + 6);                     /* x toggles between rx and rx + 6 */
	for(u8 i = 6; i; i--){
		u8 *s = OAMZ(y);
		u8 sy = a;
		if((u8)(vs - a) >= 0x64) sy = 0xf8;       /* ChkFTop */
		s[0] = sy;
		s[1] = 0xe1;
		s[2] = at;
		s[3] = rx;
		y += 4;
		a += 8;
		at ^= 0x40;                               /* horizontal flip, */
		rx ^= k;                                  /* the staggered shape */
	}
	if(!Y_) OAM(M02)[1] = 0xe0;                   /* top of the vine (Y_ is still v) */
	A_ = a; X_ = 6;
}

/* DrawHammer: X_ the misc object */
void DrawHammer(void){
	u8 x = X_;
	/* the last three slots are the 6502's 6-8 (SpawnHammerObj): their OAM offsets */
	u8 y = Misc_SprDataOffset[(x >= MISC_SLOTS - 3) ? x + (9 - MISC_SLOTS) : x];
	u8 f = 0;                                                           /* ForceHPose */
	if(!TimerControl && (Misc_State[x] & 0x7f) == 0x01) f = (FrameCounter >> 2) & 0x03;   /* GetHPose */
	TMPL(y, HammerSprData[f], 2, Misc_Rel_YPos, 0, Misc_Rel_XPos);      /* RenderH */
	x = ObjectOffset;
	if(Misc_OffscreenBits & 0xfc){
		Misc_State[x] = 0;
		hide_mask(y, 0x03);
	}
	X_ = x; Y_ = y;
}

/* the flag: triangles, skull, triangles */
static const u8 FlagSprData[12] PROGMEM = {
	0x00,0x7e,0x01,0x00, 0x00,0x7f,0x01,0x08, 0x08,0x7e,0x01,0x08
};

/* FlagpoleGfxHandler: the flag (three sprites) and the flagpole's floatey number */
void FlagpoleGfxHandler(void){
	u8 x = X_;
	u8 y = Enemy_SprDataOffset[x];
	u8 rx = Enemy_Rel_XPos, r8 = rx + 8;
	M05 = r8 + 0x0c;                              /* the floatey number's X */
	TMPL(y, FlagSprData, 3, Enemy_Y_Position[x], 0, rx);
	if(r8 >= 0x100 - 0x0c) OAM(y + 8)[0]++;       /* the carry of the add above */
	M02 = FlagpoleFNum_Y_Pos;
	M03 = 0x01;                                   /* flip, not used */
	M04 = 0x01;
	if(FlagpoleCollisionYPos){
		u8 b[8];
		rows(b, &FlagpoleScoreNumTiles[FlagpoleScore << 1], 1);
		flush(y + 0x0c, b, 8);
	}
	/* ChkFlagOffscreen: X_ (kept) is ObjectOffset, as RelativeEnemyPosition, FlagpoleRoutine's
	** last call, left it; so y is still its offset */
	if(Enemy_OffscreenBits & 0x0e) hide_mask(y, 0x3f);
	Y_ = y;
}

/* DrawLargePlatform: six sprites in a row, the last two dropped in castles and hard mode */
void DrawLargePlatform(void){
	u8 x = ObjectOffset;                          /* X_ is ObjectOffset here */
	u8 y = Enemy_SprDataOffset[x];
	M02 = y;
	TMPL(y, RowSprData, 6, Enemy_Y_Position[x], CloudTypeOverride ? 0x75 : 0x5b, Enemy_Rel_XPos);   /* puff or girder */
	if(AreaType == 0x03 || SecondaryHardMode) hide_mask(y + 16, 0x03);   /* ShrinkPlatform */
	X_ = x + OFS_ENEMY;
	GetXOffscreenBits();
	u8 a = A_ & 0xfc;                             /* d7 to d2: sprites 1 to 6 */
	if(Enemy_OffscreenBits & 0x80) a = 0xfc;      /* all six */
	for(u8 o = y; a; a <<= 1, o += 4){
		if(a & 0x80) *OAMZ(o) = 0xf8;
	}
	X_ = x; Y_ = y;
}

/* the jumping coin's two sprites (JumpingCoinTiles, $60-$63, by the tile offset), then the
** floatey number's "200" (DrawFloateyNumber_Coin) */
static const u8 JCoinSprData[2][8] PROGMEM = {
	{ 0x00,0x60,0x02,0x00, 0x08,0x60,0x82,0x00 },   /* the bottom half flipped */
	{ 0x00,0xf7,0x02,0x00, 0x00,0xfb,0x02,0x08 },
};

/* JCoinGfxHandler: the jumping coin, or its floatey number rising every other frame */
void JCoinGfxHandler(void){
	u8 x = X_;
	/* a coin's slot is one of the last three, the 6502's 6-8: their OAM offsets, not those of the
	** port's slot number (with 6 slots, 3-5's are the power-up's, whose bottom row a coin covered) */
	u8 y = Misc_SprDataOffset[x + (9 - MISC_SLOTS)];
	u8 st = Misc_State[x], t = 0;
	const u8 *d = JCoinSprData[1];
	if(st >= 0x02){
		if(!(FrameCounter & 0x01)) Misc_Y_Position[x]--;
	}else{
		d = JCoinSprData[0];
		t = (FrameCounter >> 1) & 0x03;
	}
	TMPL(y, d, 2, Misc_Y_Position[x], t, Misc_Rel_XPos);
	if(st < 0x02) X_ = ObjectOffset;
	Y_ = y;
}

/* DrawPowerUp: the power-up in enemy slot 5; flower and star flash and mirror */
void DrawPowerUp(void){
	u8 y = Enemy_SprDataOffset[5];
	M02 = Enemy_Rel_YPos + 8;
	M05 = Enemy_Rel_XPos;
	u8 type = PowerUpType, sa = Enemy_SprAttrib[5];
	M04 = ((type & 0x01) ? 0x01 : 0x02) | sa;     /* PowerUpAttributes */
	M07 = 0xff;                                   /* PUpDrawLoop's count, run out */
	M03 = 0x01;
	u8 b[16];
	rows(b, &PowerUpGfxTable[type << 2], 2);
	if(type != 0 && type != 3){                   /* not the mushrooms */
		M00 = type;
		u8 a = ((FrameCounter >> 1) & 0x03) | sa;
		if(type != 1) cols(b, a, a | 0x40, 2);    /* the star: the bottom row too */
		else{
			cols(b, a, a | 0x40, 1);
			b[14] |= 0x40;                        /* FlipPUpRightSide */
		}
	}
	flush(y, b, 16);
	Y_ = y;
	spr_object_offscr_chk();
}

/* ---------------------------------------------------------------------------------------
** Enemies
** ------------------------------------------------------------------------------------ */

/* EnemyGfxHandler from DrawEnemyObject: the six sprites of table offset g at M_EB, reworked
** for ef, ec and vf ($ef, $ec, VerticalFlipFlag); then the offscreen check */
static NOINLINE void enemy_draw(u8 g, u8 ef, u8 ec, u8 vf){
	u8 b[24], a;
	rows(b, &EnemyGraphicsTable[g], 3);
	if(ef == 0x08) goto SprObjectOffscrChk;       /* bullet bill: SkipToOffScrChk */
/* CheckForVerticalFlip: */
	if(vf){
		a = b[2] | 0x80;
		cols(b, a, a, 3);
		u8 *r = b;
		if(!(ef == HammerBro || ef == Lakitu || ef >= 0x15)) r = b + 8;
		/* FlipEnemyVertically: the third row's tiles swapped with the first or second's */
		u8 t0 = r[1], t1 = r[5];
		r[1] = b[17];
		r[5] = b[21];
		b[21] = t1;
		b[17] = t0;
	}
/* CheckForESymmetry: */
	if(BowserGfxFlag) goto SprObjectOffscrChk;
	if(ef == 0x05) goto SprObjectOffscrChk;       /* hammer bro */
	if(ef == Bloober || ef == PiranhaPlant || ef == Podoboo) goto MirrorEnemyGfx;
	if(ef == Spiny && ec != 0x05) goto CheckToMirrorLakitu;
/* ESRtnr: */
	if(ef == 0x15) b[22] = 0x42;                  /* retainer: bottom right flipped */
/* SpnySC: */
	if(ec < 0x02) goto CheckToMirrorLakitu;
MirrorEnemyGfx:
	a = b[2] & 0xa3;                              /* vertical flip, priority, palette */
	cols(b, a, (ec == 0x05) ? (a | 0xc0) : (a | 0x40), 3);   /* EggExc: the spiny's egg */
	if(ec == 0x04){
		a = b[10] | 0x80;
		cols(b + 8, a, a | 0x40, 2);
	}
CheckToMirrorLakitu:
	if(ef == Lakitu){
		if(!vf){
			b[18] &= 0x81;
			a = b[22] | 0x41;
			b[22] = a;
			if(FrenzyEnemyTimer >= 0x10) goto SprObjectOffscrChk;
			b[14] = a;
			b[10] = a & 0x81;
			goto SprObjectOffscrChk;
		}
		b[2] &= 0x81;                             /* NVFLak */
		b[6] |= 0x41;
	}
/* CheckToMirrorJSpring: */
	if(ef >= 0x18) cols(b + 8, 0x82, 0xc2, 2);
SprObjectOffscrChk:;
	u8 y = Y_ = M_EB;
	flush(y, b, 24);
	spr_object_offscr_chk();
}

/* EnemyGfxHandler: picks the enemy's frame, attributes and flips (the 6502's branch maze,
** kept as gotos), draws its six sprites and handles offscreen. X_ the enemy: every caller
** has it equal to ObjectOffset, which the 6502 reloaded halfway */
void EnemyGfxHandler(void){
	u8 x = X_, y, a, g, ec, ef, bg;
	/* the enemy's bytes, read up front through three pointers */
	const u8 *p = &Enemy_ID[x];
	IN_REG(p, "z");
	a = p[0];
	u8 ed = p[RO(Enemy_ID, Enemy_State)];
	u8 m03 = p[RO(Enemy_ID, Enemy_MovingDir)];
	u8 xs = p[RO(Enemy_ID, PiranhaPlant_Y_Speed)];
	p = (const u8 *)&Enemy_Y_Speed[x];
	IN_REG(p, "z");
	u8 ys = p[0], m02 = p[RO(Enemy_Y_Speed, Enemy_Y_Position)];
	p = &EnemyFrameTimer[x];
	IN_REG(p, "z");
	u8 ft = p[0], it = p[RO(EnemyFrameTimer, EnemyIntervalTimer)];
	u8 m04 = Enemy_SprAttrib[x];
	u8 vf = 0;
	M05 = Enemy_Rel_XPos;
	M_EB = Enemy_SprDataOffset[x];
	bool hide = a == PiranhaPlant && (s8)xs >= 0 && ft;   /* a piranha plant waiting, hidden */
	if(hide) goto Store;
/* CheckForRetainerObj: */
	y = ed & 0x1f;
	if(a == RetainerObject){
		y = 0;
		m03 = 0x01;                               /* not used */
		a = 0x15;                                 /* the code for retainer/princess */
	}
/* CheckForBulletBillCV: */
	if(a == BulletBill_CannonVar){
		m02--;
		m04 = ft ? 0x23 : 0x03;                   /* SBBAt */
		y = 0;
		ed = 0;
		a = 0x08;
	}
/* CheckForJumpspring: */
	if(a == JumpspringObject){
		y = 0x03;
		a = PB(JumpspringFrameOffsets, JumpspringAnimCtrl);
	}
/* CheckForPodoboo: */
	ef = a; ec = y;
	if(a == 0x0c && (s8)ys >= 0) vf = 1;
/* CheckBowserGfxFlag: */
	bg = BowserGfxFlag;
	if(bg) ef = (bg != 0x01) ? 0x17 : 0x16;       /* front or rear */
/* CheckForGoomba: */
	if(ef == Goomba){                             /* ed is still the goomba's state */
		if(ed >= 0x02) ec = 0x04;                 /* defeated (the 6502 also set X 4, unused) */
		if(!((ed & 0x20) | TimerControl) && !(FrameCounter & 0x08)) m03 ^= 0x03;   /* GmbaAnim */
	}
/* CheckBowserFront: */
	m04 |= PB(EnemyAttributeData, ef);
	g = PB(EnemyGfxTableOffsets, ef);
	y = ec;
	if(bg){
		if(bg == 0x01){
			if(BowserBodyControls & 0x80) g = 0xde;           /* mouth: second frame */
		}else{
			if(BowserBodyControls & 0x01) g = 0xe4;           /* CheckBowserRear: feet */
			if(ed & 0x20) m02 -= 0x10;
		}
		if(ed & 0x20) vf = g;                     /* FlipBowserOver */
		goto DrawEnemyObject;
	}
/* CheckForSpiny: */
	if(g == 0x24){
		if(y == 0x05){                            /* the egg */
			g = 0x30;
			m03 = 0x02;
			ec = 0x05;
		}
		goto CheckForHammerBro;                   /* NotEgg */
	}
/* CheckForLakitu: */
	if(g == 0x90){
		if(!(ed & 0x20) && FrenzyEnemyTimer < 0x10) g = 0x96;
		goto CheckDefeatedState;                  /* NoLAFr */
	}
/* CheckUpsideDownShell: */
	if(ef < 0x04 && y >= 0x02){
		g = 0x5a;
		if(ef == BuzzyBeetle){
			g = 0x7e;
			m02++;
		}
	}
/* CheckRightSideUpShell: */
	if(ec != 0x04) goto CheckForHammerBro;
	g = 0x72;
	m02++;
	if(ef != BuzzyBeetle){
		g = 0x66;
		m02++;
	}
/* CheckForDefdGoomba: */
	if(ef != Goomba) goto CheckForHammerBro;
	g = 0x54;
	if(!(ed & 0x20)){
		g = 0x8a;
		m02--;
	}
CheckForHammerBro:
	if(ef == HammerBro){
		if(ed == 0) goto CheckToAnimateEnemy;
		if(!(ed & 0x08)) goto CheckDefeatedState;
		g = 0xb4;
		goto CheckToAnimateEnemy;
	}
/* CheckForBloober: */
	if(g == 0x48) goto CheckToAnimateEnemy;       /* cheep-cheep */
	if(it >= 0x05) goto CheckDefeatedState;
	if(g != 0x3c) goto CheckToAnimateEnemy;       /* not the bloober */
	if(it == 0x01) goto CheckDefeatedState;
	m02 += 3;
	goto CheckAnimationStop;
CheckToAnimateEnemy:
	if(ef == Goomba || ef == 0x08 || ef == Podoboo || ef >= 0x18) goto CheckDefeatedState;
	if(ef == 0x15){                               /* retainer/princess */
		if(WorldNumber >= World8) goto CheckDefeatedState;
		g = 0xa2;                                 /* the mushroom retainer */
		ec = 0x03;
		goto CheckDefeatedState;
	}
/* CheckForSecondFrame: */
	if(FrameCounter & 0x08) goto CheckDefeatedState;   /* EnemyAnimTimingBMask[0] */
CheckAnimationStop:
	if((ed & 0xa0) | TimerControl) goto CheckDefeatedState;
	g += 6;                                       /* the second frame */
CheckDefeatedState:
	if((ed & 0x20) && ef >= 0x04){
		vf = 0x01;
		ec = 0x00;
	}
DrawEnemyObject:
Store:
	M02 = m02;
	M03 = m03;
	M04 = m04;
	VerticalFlipFlag = vf;
	if(hide) return;
	M_ED = ed;
	M_EC = ec;
	M_EF = ef;
	enemy_draw(g, ef, ec, vf);
}

/* ---------------------------------------------------------------------------------------
** Blocks, fireballs, explosions, small platform, bubble
** ------------------------------------------------------------------------------------ */

/* DrawBlock: the bumped block, a used block if its replacement is $c4. X_ is ObjectOffset */
void DrawBlock(void){
	M02 = Block_Rel_YPos[0];
	M05 = Block_Rel_XPos[0];
	M04 = 0x03;
	M03 = 0x01;                                   /* flip, not used */
	u8 b[16];
	rows(b, DefaultBlockObjTiles, 2);             /* DBlkLoop */
	u8 x = ObjectOffset;
	u8 y = Block_SprDataOffset[x];
	bool ground = AreaType == 0x01;
	if(!ground) b[1] = b[5] = 0x86;               /* lineless brick outside ground areas */
	if(Block_Metatile[x] == 0xc4){                /* ChkRep: the used block */
		b[1] = b[5] = b[9] = b[13] = 0x87;
		u8 a = ground ? 0x03 : 0x01;              /* SetBFlip */
		b[2] = a;
		b[6] = a | 0x40;
		b[14] = a | 0xc0;
		b[10] = a | 0x80;
	}
	u8 o = Block_OffscreenBits[0];                /* BlkOffscr */
	if(o & 0x04) b[4] = b[12] = 0xf8;
	if(o & 0x08) b[0] = b[8] = 0xf8;              /* ChkLeftCo */
	flush(y, b, 16);
	X_ = x; Y_ = y;
}

/* the original's relative X m doubled less the current r, plus 6, the 6502's carries kept */
static inline __attribute__((always_inline)) u8 chunk_x(u8 m, u8 r){
	u16 s = (u16)(u8)(m - r) + m;
	if(m >= r) s++;
	return (u8)s + 6 + (u8)(s >> 8);
}

/* DrawBrickChunks: the four chunks of a shattered brick (two block objects) */
void DrawBrickChunks(void){
	u8 m = M00 = Block_Orig_XPos[X_] - ScreenLeft_X_Pos;   /* the original's relative X */
	u8 x0 = Block_Rel_XPos[0], x2 = Block_Rel_XPos[1];
	u8 c0 = chunk_x(m, x0), c2 = chunk_x(m, x2);
	u8 t = 0x75, f = 0x02;                        /* ball (residual) */
	if(GameEngineSubroutine != 0x05){
		f = 0x03;
		t = 0x84;                                 /* brick chunk */
	}
	u8 a = ((FrameCounter << 4) & 0xc0) | f;      /* d3-d2 of the counter as flips */
	u8 y0 = Block_Rel_YPos[0], y1 = y0, y2 = Block_Rel_YPos[1], y3 = y2;
	u8 ob = Block_OffscreenBits[0];
	if(ob & 0x08) y0 = y2 = 0xf8;                 /* ChkLeftCo */
	if(ob & 0x80) y0 = y1 = 0xf8;
	if((s8)m < 0 && x0 >= c0) y1 = y3 = 0xf8;     /* ChnkOfs */
	u8 *s = OAMZ(Y_ = Block_SprDataOffset[X_]);   /* X_ is kept */
	s[0] = y0; s[1] = t; s[2] = a; s[3] = x0;
	OAM_NEXT(s);
	s[0] = y1; s[1] = t; s[2] = a; s[3] = c0;
	OAM_NEXT(s);
	s[0] = y2; s[1] = t; s[2] = a; s[3] = x2;
	OAM_NEXT(s);
	s[0] = y3; s[1] = t; s[2] = a; s[3] = c2;
}

/* DrawFireball: X_ the fireball */
__attribute__((noipa)) void DrawFireball(void){
	u8 *s = OAM(Y_ = FBall_SprDataOffset[X_]);
	s[0] = Fireball_Rel_YPos[0];
	s[3] = Fireball_Rel_XPos[0];
	TAILJMP(DrawFirebar);
}

/* DrawFirebar: the fireball tile at Y_, spinning every four frames, flipped every eight */
void DrawFirebar(void){
	u8 *s = OAM(Y_);
	u8 a = FrameCounter >> 2;
	s[1] = (a & 0x01) ^ 0x64;
	A_ = s[2] = (a & 0x02) ? 0xc2 : 0x02;
}

/* DrawExplosion_Fireball: X_ the fireball; the state counts the explosion's frames */
__attribute__((noipa)) void DrawExplosion_Fireball(void){
	u8 x = X_;
	u8 a = (Fireball_State[x]++ >> 1) & 0x07;
	if(a >= 0x03){
		Fireball_State[x] = 0;                    /* KillFireBall */
		A_ = 0;
		return;
	}
	A_ = a; Y_ = Alt_SprDataOffset[x];
	TAILJMP(DrawExplosion_Fireworks);
}

/* the explosion's four sprites, around the fireball's relative position less 4 */
static const u8 ExplosionSprData[16] PROGMEM = {
	0x00,0x00,0x02,0x00, 0x08,0x00,0x82,0x00, 0x00,0x00,0x42,0x08, 0x08,0x00,0xc2,0x08
};

/* DrawExplosion_Fireworks: frame A_ of the explosion, four sprites at Y_ around the
** fireball's relative position */
void DrawExplosion_Fireworks(void){
	u8 y = Y_;
	TMPL(y, ExplosionSprData, 4, Fireball_Rel_YPos[0] - 4, PB(ExplosionTiles, A_), Fireball_Rel_XPos[0] - 4);
	X_ = ObjectOffset; Y_ = y; A_ = 0xc2;
}

/* DrawSmallPlatform: two rows of three, 128 pixels apart, hidden behind the status bar */
void DrawSmallPlatform(void){
	u8 x = X_;
	u8 y = Enemy_SprDataOffset[x];
	u8 sx = Enemy_Rel_XPos, sy = Enemy_Y_Position[x];
	u8 top = (sy >= 0x20) ? sy : 0xf8;            /* TopSP */
	u8 bot = sy + 0x80;
	if(bot < 0x20) bot = 0xf8;                    /* BotSP */
	TMPL(y, RowSprData, 3, top, 0x5b, sx);
	TMPL(y + 12, RowSprData, 3, bot, 0x5b, sx);
	u8 b = Enemy_OffscreenBits, m = 0;
	if(b & 0x08) m |= 0x09;                       /* the columns: d3, */
	if(b & 0x04) m |= 0x12;                       /* SOfs, */
	if(b & 0x02) m |= 0x24;                       /* SOfs2 */
	hide_mask(y, m);
	X_ = ObjectOffset; Y_ = y;
}

/* DrawBubble: X_ the air bubble; only while the player is on screen vertically */
void DrawBubble(void){
	if(Player_Y_HighPos != 0x01) return;
	if(Bubble_OffscreenBits & 0x08) return;
	u8 y = Y_ = Bubble_SprDataOffset[X_];
	u8 *s = OAMZ(y);
	s[0] = Bubble_Rel_YPos;
	s[1] = 0x74;
	s[2] = 0x02;
	s[3] = Bubble_Rel_XPos;
}

/* ---------------------------------------------------------------------------------------
** The star flag, the floatey numbers, Bowser's flame
** ------------------------------------------------------------------------------------ */

/* the star flag's four sprites (DSFLoop from index 3 down: StarFlagYPosAdder,
** StarFlagTileData, palette 2 behind the background, StarFlagXPosAdder) */
static const u8 StarFlagSprData[16] PROGMEM = {
	0x08,0x57,0x22,0x08, 0x08,0x56,0x22,0x00, 0x00,0x55,0x22,0x08, 0x00,0x54,0x22,0x00
};

/* DrawStarFlag: the castle's star flag, four sprites; X_ the object */
void DrawStarFlag(void){
	RelativeEnemyPosition();
	u8 y = Enemy_SprDataOffset[X_];
	TMPL(y, StarFlagSprData, 4, Enemy_Rel_YPos, 0, Enemy_Rel_XPos);
	y += 16;
	X_ = ObjectOffset; Y_ = y;
}

/* DrawFloateyNumber: FloateyNumbersRoutine's drawing, ChkTallEnemy/GetAltOffset (the OAM
** offset) and SetupNumSpr to the end; objects.c has done the rest, FloateyPart's raise
** included, and passes its carry in C_ (clear: the sbc #$08 takes 9). X_ the slot */
void DrawFloateyNumber(void){
	u8 x = X_;
	u8 y = Enemy_SprDataOffset[x];
	const u8 *e = &Enemy_ID[x];
	IN_REG(e, "z");
	u8 a = e[0];
	if(a == Spiny || a == PiranhaPlant) goto SetupNumSpr;
	if(a == HammerBro) goto GetAltOffset;
	if(a == GreyCheepCheep || a == RedCheepCheep) goto SetupNumSpr;
	if(a >= TallEnemy) goto GetAltOffset;
	if(e[RO(Enemy_ID, Enemy_State)] >= 0x02) goto SetupNumSpr;
GetAltOffset:
	y = Alt_SprDataOffset[SprDataOffset_Ctrl];
	x = ObjectOffset;
SetupNumSpr:;
	const u8 *p = &FloateyNum_Control[x];
	IN_REG(p, "z");
	u8 sy = p[RO(FloateyNum_Control, FloateyNum_Y_Pos)] - 9 + C_;   /* sbc #$08 */
	u8 sx = p[RO(FloateyNum_Control, FloateyNum_X_Pos)];
	const u8 *t = &FloateyNumTileData[p[0] << 1];
	u8 t0 = LPM_INC(t), t1 = LPM_INC(t);
	u8 *s = OAMZ(y);
	s[0] = sy;
	s[1] = t0;
	s[2] = 0x02;
	s[3] = sx;
	OAM_NEXT(s);
	s[0] = sy;
	s[1] = t1;
	s[2] = 0x02;
	s[3] = sx + 8;
	A_ = t1;
	X_ = ObjectOffset; Y_ = y;
}

/* DrawBowserFlame: ProcBowserFlame from FlmeAt's OAM offset (after SetGfxF's relative
** position, state test and M00 = $51, M01 = the attributes) to ExFlmeD: three flame
** sprites, then the offscreen bits. X_ ObjectOffset */
void DrawBowserFlame(void){
	u8 x = X_;
	u8 y = Enemy_SprDataOffset[x];
	u8 t = M00, sx = Enemy_Rel_XPos, sy = Enemy_Rel_YPos, at = M01;
	for(u8 i = 3; i; i--){                        /* DrawFlameLoop */
		u8 *s = OAMZ(y);
		s[0] = sy;
		s[1] = t++;
		s[2] = at;
		s[3] = sx;
		y += 4;
		sx += 8;
	}
	M00 = t;
	Enemy_Rel_XPos = sx;
	X_ = ObjectOffset;
	GetEnemyOffscreenBits();
	x = X_;
	y = Enemy_SprDataOffset[x];
	u8 b = Enemy_OffscreenBits, o = y + 12;
	for(u8 i = 4; i; i--){                        /* d0: residual, the flame is three sprites */
		if(b & 0x01) *OAMZ(o) = 0xf8;              /* M3FOfs, M2FOfs, M1FOfs */
		b >>= 1;
		o -= 4;
	}
	X_ = x; Y_ = y;
}

/* ---------------------------------------------------------------------------------------
** The player (his OAM offset is 0 or 4: only InitSprOffsets sets it, and the shuffle
** leaves offsets under $28 alone, so his eight sprites never wrap)
** ------------------------------------------------------------------------------------ */

/* RenderPlayerSub: n rows of the player from PlayerGfxOffset (DrawPlayerLoop: M07 counts
** them down to 0) */
static void render_player_sub(u8 n){
	u8 a = Player_Rel_XPos;
	Player_Pos_ForScroll = a;
	M05 = a;
	M02 = Player_Rel_YPos;
	M03 = PlayerFacingDir;
	M04 = Player_SprAttrib;
	rows(OAM(Player_SprDataOffset), &PlayerGraphicsTable[PlayerGfxOffset], n);
	M07 = 0;
}

/* a row's flips cleared and its right sprite mirrored */
static void unflip_row(u8 *s){
	s[0] &= 0x3f;
	s[4] = (s[4] & 0x3f) | 0x40;
}

/* PlayerGfxProcessing: draws frame a, then the fireball throwing frame if its timer allows.
** ChkForPlayerAttrib (player.c) only sets A_, which PlayerOffscreenChk overwrites: its OAM
** half (KilledAtt, C_S_IGAtt) is here: killed, the third and fourth rows; crouching,
** standing or growing, the fourth; those rows lose their flips, the right sprite mirrored */
static void player_gfx_processing(u8 a){
	PlayerGfxOffset = a;
	render_player_sub(4);
	u8 *s = OAM(Player_SprDataOffset);
	if(GameEngineSubroutine == 0x0b || a == 0xc8){   /* KilledAtt; big and standing ($c8)
		                                             ** falls into it in the 6502 */
		unflip_row(s + 18);
		goto C_S_IGAtt;
	}
	if(a == 0x50 || a == 0xb8 || a == 0xc0){
	C_S_IGAtt:
		unflip_row(s + 26);
	}
	u8 f = FireballThrowingTimer;
	if(f){
		u8 t = PlayerAnimTimer;
		FireballThrowingTimer = 0;
		if(t < f){
			FireballThrowingTimer = t;
			PlayerGfxOffset = PB(PlayerGfxTblOffsets, 0x07);
			render_player_sub((Player_X_Speed || Left_Right_Buttons) ? 3 : 4);   /* SUpdR */
		}
	}
	/* PlayerOffscreenChk: each row offscreen from the vertical offscreen bits */
	u8 b = Player_OffscreenBits >> 4;
	u8 y = Player_SprDataOffset + 0x18;           /* the bottom row */
	for(u8 i = 4; i; i--){
		if(b & 0x01){
			u8 *r = OAMZ(y);
			r[0] = 0xf8;
			r[4] = 0xf8;
		}
		b >>= 1;
		y -= 8;
	}
	M00 = 0;
	X_ = 0xff; Y_ = A_ = y;
}

/* PlayerGfxHandler: draws the player (every other frame while injured), and animates the
** feet when swimming */
void PlayerGfxHandler(void){
	if(InjuryTimer && (FrameCounter & 0x01)) return;
	if(GameEngineSubroutine == 0x0b){             /* CntPl: PlayerKilled */
		player_gfx_processing(PB(PlayerGfxTblOffsets, 0x0e));
		return;
	}
	if(PlayerChangeSizeFlag){                     /* DoChangeSize */
		HandleChangeSize();
		player_gfx_processing(A_);
		return;
	}
	bool swim = SwimmingFlag && Player_State;
	ProcessPlayerAction();                        /* FindPlayerAction */
	player_gfx_processing(A_);
	if(!swim || (FrameCounter & 0x04)) return;
	u8 *s = OAM(Player_SprDataOffset + 25);
	if(!(PlayerFacingDir & 0x01)) s += 4;         /* facing left: the next sprite */
	u8 t = 0x31;                                  /* SwimKickTileNum */
	if(PlayerSize){                               /* SwimKT */
		if(*s == SwimTileRepOffset) return;
		t = 0x46;
	}
	*s = t;                                       /* BigKTS */
}

/* DrawPlayer_Intermediate: the small player standing on the world/lives screen. Its bottom
** right sprite: the 6502 ORs the flip into the attribute of the sprite after the player's
** ("empty sprite attributes", Sprite_Attributes+36), which is 0 there: InitializeArea has just
** cleared the memory, the OAM's page with it. Nothing clears the mode's sprite list, and that
** entry is the first air bubble's, palette 2: after a water level the tile was drawn in that
** palette (the user's run 4, 2026-10-11). So the 6502's value itself */
void DrawPlayer_Intermediate(void){
	for(u8 i = 0; i < 5; i++) ra.M_[2 + i] = PB(IntermediatePlayerData, i);   /* PIntLoop: $02-$06 */
	rows(OAM(0x04), &PlayerGraphicsTable[0xb8], 4);   /* DrawPlayerLoop, M07 rows */
	M07 = 0;
	OAM_BASE[34] = 0x40;
}
