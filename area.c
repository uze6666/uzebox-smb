/*
 * The area parser: a port of smbdis.asm's AreaParserCore and its object routines (lines
 * 3062-4455). It renders the level one block column at a time into the block buffer, from
 * the original area data. Routine names are the 6502's; comments say what is not obvious.
 * The PPU half (RenderAreaGraphics, attribute tables) is the platform's: see gfx17.c.
 * An object routine gets o, its area object buffer: o[0] AreaObjOffsetBuffer, o[3]
 * AreaObjectLength, o[9] MushroomLedgeHalfLen (the three arrays are laid out that way).
 */
#include <stddef.h>
#include <avr/pgmspace.h>
#include "smb.h"
/* The level data: in the flash, or with SD=1 (the .uze's build, sd17.c) in SPI RAM, where
** AreaData and EnemyData then point. LVB: a byte of it; LVW: two, the low one first */
#if SD
#define LVDATA SDDATA
#define LVB(p) sd_byte(p)
#define LVW(p) (sd_byte(p) | (u16)sd_byte((p) + 1) << 8)
#else
#define LVB(p) pgm_read_byte(p)
#define LVW(p) pgm_read_word(p)
#endif
#include "levels.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

/* the distance from field b to field a of struct s (ra, rab...) */
#define FD(s, a, b) (offsetof(__typeof__(s), a) - offsetof(__typeof__(s), b))
#define RA_D(a, b) FD(ra, a, b)
#define OFS_LEN  3
#define OFS_HALF 9
_Static_assert(offsetof(struct RamArea, AreaObjectLength_) - offsetof(struct RamArea, AreaObjOffsetBuffer_) == OFS_LEN, "");
_Static_assert(offsetof(struct RamArea, MushroomLedgeHalfLen_) - offsetof(struct RamArea, AreaObjOffsetBuffer_) == OFS_HALF, "");

/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
static const u8 BackSceneryData[144] PROGMEM = { ROM_BackSceneryData };

static const u8 BackSceneryMetatiles[36] PROGMEM = { ROM_BackSceneryMetatiles };   /* cloud left, middle, right; bush left, middle, right; mountain left, middle, right; fence, tall tree, short tree */

static const u8 ForeSceneryData[39] PROGMEM = { ROM_ForeSceneryData };   /* in water; wall; over water */

static const u8 TerrainMetatiles[4] PROGMEM = { ROM_TerrainMetatiles };

static const u8 TerrainRenderBits[32] PROGMEM = { ROM_TerrainRenderBits };

/* numbers lower than these with the same attribute bits are not stored in the block buffer */
static const u8 BlockBuffLowBounds[4] PROGMEM = { ROM_BlockBuffLowBounds };

const u8 Bitmasks[8] PROGMEM = { ROM_Bitmasks };

static const u8 CastleMetatiles[55] PROGMEM = { ROM_CastleMetatiles };

/* SidePipeShaftData, SidePipeTopPart, SidePipeBottomPart, four each */
static const u8 SidePipeData[12] PROGMEM = { ROM_SidePipeData };
static const u8 VerticalPipeData[8] PROGMEM = { ROM_VerticalPipeData };
/* SolidBlockMetatiles (4), BrickMetatiles (5, the last for cloud levels), CoinMetatileData (4) */
static const u8 RowColMetatiles[13] PROGMEM = { ROM_RowColMetatiles };
/* where each of row of bricks, of solid blocks, of coins, column of bricks, of solid blocks
** starts in RowColMetatiles */
static const u8 RowColBase[5] PROGMEM = { 4, 0, 9, 4, 0 };
static const u8 StaircaseHeightData[9] PROGMEM = { ROM_StaircaseHeightData };
static const u8 StaircaseRowData[9] PROGMEM = { ROM_StaircaseRowData };
static const u8 FrenzyIDData[3] PROGMEM = { FlyCheepCheepFrenzy, BBill_CCheep_Frenzy, Stop_Frenzy };
/* Row12Object's: the row, its metatile, the height under it, the metatile under it (0 none) */
static const u8 Row12Data[24] PROGMEM = {
	0x06, 0x0b, 0x00, 0x63,  0x07, 0x0b, 0x00, 0x63,  0x09, 0x0b, 0x00, 0x63,   /* bridges */
	0x0a, 0x86, 0x01, 0x87,                                                    /* water hole */
	0x03, 0xc0, 0x00, 0x00,  0x07, 0xc0, 0x00, 0x00                            /* ? block rows */
};

/* used by question blocks, then bricks of ground level types, then of other level types */
const u8 BrickQBlockMetatiles[14] PROGMEM = { ROM_BrickQBlockMetatiles };

#define RD(t, i) pgm_read_byte(&(t)[i])
/* inlined or kept out of line as measured smallest (a call boundary costs reloads and saves) */
#define INLINE static inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))

/* an object routine: o its area object buffer, m the object's number (M00); AOBJI ones are
** inlined into RunAObj */
#define AOBJ_ARGS (u8 *o __attribute__((unused)), u8 m __attribute__((unused)))
#define AOBJ(name) static void name AOBJ_ARGS
#define AOBJI(name) INLINE void name AOBJ_ARGS

/* ---------------------------------------------------------------------------------------
** Helpers shared by the objects
** ------------------------------------------------------------------------------------ */

/* the area data bytes i (low) and i + 1 (high) */
static u16 NOINLINE ad2(u8 i){
	return LVW(AreaData + i);
}

/* GetBlockBufferAddr: the column (BlockBufferColumnPos, 0-31) as BBPtr; M06 keeps the column,
** whose low nybble is its x / 16 as the 6502's low byte was */
void GetBlockBufferAddr(void){
	u8 c = A_;
	M06 = c;
	bb_Select(c);
}

/* GetLrgObjAttrib: the row (also in M07) in the low byte, the length or height in the high,
** of the object in buffer ObjectOffset (the 6502's X; every caller's o) */
static u16 NOINLINE GetLrgObjAttrib(void){
	u16 w = ad2(AreaObjOffsetBuffer[ObjectOffset]) & 0x0f0f;
	u8 *m7 = &M07;
	asm("" : "+r"(m7));
	*m7 = w;
	return w;
}

/* ChkLrgObjFixedLength: loads the length counter with len if not loaded; true if just
** starting (the 6502's carry) */
INLINE bool ChkLrgObjFixedLength(u8 *o, u8 len){
	if((s8)o[OFS_LEN] >= 0) return false;
	o[OFS_LEN] = len;
	return true;
}

/* ChkLrgObjLength: GetLrgObjAttrib's two values, d15 set if just starting */
static u16 ChkLrgObjLength(u8 *o){
	u16 r = GetLrgObjAttrib();
	if(ChkLrgObjFixedLength(o, r >> 8)) r |= 0x8000;
	return r;
}

/* the object's column position plus d, the page in the high byte */
INLINE u16 obj_pos(s8 d){
	return (((u16)CurrentPageLoc << 8) | (u8)(CurrentColumnPos << 4)) + d;
}

/* RenderUnderPart: metatile a from row x downwards for y + 1 rows, keeping what is there
** where the 6502 kept it */
static void NOINLINE RenderUnderPart(u8 x, u8 y, u8 a){
	u8 *p = &MetatileBuffer[x];
	for(;;){
		AreaObjectHeight = y;
		u8 cur = *p;
		bool draw;
		if(cur == 0) draw = true;
		else if(cur == 0x17 || cur == 0x1a) draw = false;          /* ledge middles: wait a row */
		else if(cur == 0xc0) draw = true;                         /* question block with coin */
		else if(cur >= 0xc0) draw = false;                        /* any other palette 3 metatile */
		else if(cur != 0x54) draw = true;                         /* not cracked rock: overwrite */
		else draw = (a != 0x50);                                  /* stem top of mushroom: wait */
		if(draw) *p = a;
		p++;
		if(++x >= 0x0d) return;
		if((s8)--y < 0) return;
	}
}

/* metatile top at row x, then RenderUnderPart from the row under it */
static void NOINLINE RenderTop(u8 x, u8 y, u8 a, u8 top){
	MetatileBuffer[x] = top;
	RenderUnderPart(x + 1, y, a);
}

/* MetatileBuffer rows 0 to n - 1 blank */
static void NOINLINE mb_clear(u8 n){
	u8 *p = MetatileBuffer;
	do *p++ = 0; while(--n);
}

/* MetatileBuffer[r] = a, MetatileBuffer[r + 1] = b */
static void NOINLINE mb_put2(u8 r, u8 a, u8 b){
	u8 *p = &MetatileBuffer[r];
	p[0] = a;
	p[1] = b;
}

/* the first empty enemy slot 0-4, or 5 (the 6502's X when none: carry set) */
static u8 FindEmptyEnemySlot_(void){
	u8 x = 0;
	while(x < 5 && Enemy_Flag[x]) x++;
	return x;
}

/* an object's enemy in the first empty slot (5 if none, written all the same; the flagpole's
** flag always 5): x (the column's plus d), page, y (row x 16 + 32), identifier, Y_HighPos 1
** (not the flag's), the flag incremented (1 in an empty slot; the star flag's set to 1), a
** jumpspring's fixed y; returns the slot */
static u8 NOINLINE spawn(s8 d, u8 row, u8 id){
	u8 y = (row << 4) + 32, e = 5;
	if(id != FlagpoleFlagObject){
		e = FindEmptyEnemySlot_();
		if(e == 5 && id == PiranhaPlant) return e;  /* VerticalPipe's: only in an empty slot */
	}
	u16 pos = obj_pos(d);
	u8 *p = &Enemy_Flag[e];
	asm("" : "+r"(p));                          /* through p: std, not sts */
	p[0] = (id == StarFlagObject) ? 1 : p[0] + 1;
	p[RA_D(Enemy_ID_, Enemy_Flag_)] = id;
	if(id == JumpspringObject) p[RA_D(SprObject_X_Speed_, Enemy_Flag_) + OFS_ENEMY] = y;   /* Jumpspring_FixedYPos */
	p += RA_D(SprObject_PageLoc_, Enemy_Flag_) + OFS_ENEMY;
	p[0] = pos >> 8;
	if(id != FlagpoleFlagObject) p[RA_D(SprObject_Y_HighPos_, SprObject_PageLoc_)] = 1;
	p += RA_D(SprObject_X_Position_, SprObject_PageLoc_);
	p[0] = pos;
	p[RA_D(SprObject_Y_Position_, SprObject_X_Position_)] = y;
	return e;
}

/* the cannon (and whirlpool) slot at Cannon_Offset: y (row x 16 + 32; the whirlpool's length),
** page, x; then the next of n */
static void NOINLINE put_cannon(u8 row, s8 d, u8 n){
	u8 y = (row << 4) + 32;
	u16 pos = obj_pos(d);
	u8 c = Cannon_Offset;
	u8 *p = &Cannon_PageLoc[c];
	asm("" : "+r"(p));                          /* through p: std, not sts */
	p[0] = pos >> 8;
	p[FD(rab, Cannon_X_Position_, Cannon_PageLoc_)] = pos;
	p[FD(rab, Cannon_Y_Position_, Cannon_PageLoc_)] = y;
	if(++c >= n) c = 0;
	Cannon_Offset = c;
}

static void NOINLINE set_fore(u8 b){              /* the header's foreground scenery or background colour */
	b &= 0x07;
	if(b >= 4){
		BackgroundColorCtrl = b;
		b = 0;
	}
	ForegroundScenery = b;
}

static void NOINLINE set_terrain(u8 b){           /* the header's terrain and background scenery */
	TerrainControl = b & 0x0f;
	BackgroundScenery = (b & 0x30) >> 4;
}

/* ---------------------------------------------------------------------------------------
** Area objects
** ------------------------------------------------------------------------------------ */

AOBJI(AlterAreaAttributes){
	u8 b = ad2(o[0]) >> 8;
	if(!(b & 0x40)) set_terrain(b);
	else set_fore(b);
}

AOBJI(ScrollLockObject){
	ScrollLock ^= 1;
}

static void kill_enemies(u8 id){
	M00 = id;
	for(s8 x = 4; x >= 0; x--){
		if(Enemy_ID[x] == id) Enemy_Flag[x] = 0;
	}
}

/* KillEnemies: A_ the identifier (no caller reads the registers after) */
void KillEnemies(void){
	kill_enemies(A_);
}

AOBJI(ScrollLockObject_Warp){
	u8 x = 4;
	if(WorldNumber){
		x++;
		if(AreaType == 1) x++;
	}
	WarpZoneControl = x;
	A_ = x;
	WriteGameText();
	kill_enemies(PiranhaPlant);
	ScrollLockObject(o, m);
}

AOBJI(AreaFrenzy){
	u8 a = RD(FrenzyIDData, m - 8);
	for(u8 y = 0; y < 5; y++){
		if(Enemy_ID[y] == a){
			a = 0;
			break;
		}
	}
	EnemyFrenzyQueue = a;
}

AOBJI(TreeLedge){
	u16 r = GetLrgObjAttrib();
	u8 row = r;
	u8 l = o[OFS_LEN];
	if(l == 0){
		RenderUnderPart(row, 0, 0x18);          /* end of tree ledge */
		return;
	}
	if((s8)l < 0){
		o[OFS_LEN] = r >> 8;
		if(CurrentPageLoc | CurrentColumnPos){
			RenderUnderPart(row, 0, 0x16);      /* start of tree ledge */
			return;
		}
	}
	RenderTop(row, 0x0f, 0x4c, 0x17);           /* middle (also at the level's start), the trunk */
}

AOBJI(MushroomLedge){
	u16 r = ChkLrgObjLength(o);
	u8 row = r;
	if((s16)r < 0){
		o[OFS_HALF] = o[OFS_LEN] >> 1;
		RenderUnderPart(row, 0, 0x19);          /* start of mushroom */
		return;
	}
	u8 y = o[OFS_LEN];
	if(y == 0){
		RenderUnderPart(row, 0, 0x1b);          /* end of mushroom */
		return;
	}
	MetatileBuffer[row] = 0x1a;                 /* middle */
	if(y != o[OFS_HALF]) return;
	RenderTop(row + 1, 0x0f, 0x50, 0x4f);       /* stem top under the centre, the stem */
}

AOBJI(BulletBillCannon){
	u16 r = GetLrgObjAttrib();
	u8 row = r, y = r >> 8;
	u8 *p = &MetatileBuffer[row];
	p[0] = 0x64;
	if(y){
		p[1] = 0x65;
		if(--y) RenderUnderPart(row + 2, y - 1, 0x66);
	}
	put_cannon(row, 0, 6);
}

AOBJI(AreaStyleObject){
	u8 s = AreaStyle;
	if(s == 0) TreeLedge(o, m);
	else if(s == 1) MushroomLedge(o, m);
	else BulletBillCannon(o, m);
}

AOBJI(PulleyRopeObject){
	u8 a = 0x42;                                /* PulleyRopeMetatiles: left */
	if((s16)ChkLrgObjLength(o) >= 0){
		a = 0x41;                               /* middle */
		if(!o[OFS_LEN]) a = 0x43;               /* right */
	}
	MetatileBuffer[0] = a;
}

AOBJI(CastleObject){
	u8 top = GetLrgObjAttrib() >> 8;
	M07 = top;
	ChkLrgObjFixedLength(o, 4);
	u8 l = o[OFS_LEN];
	const u8 *s = &CastleMetatiles[l];          /* top is 0 or 6 in every level: rows top to 10 */
	u8 *d = &MetatileBuffer[top];
	do{
		*d++ = pgm_read_byte(s);
		s += 5;
	}while(d != &MetatileBuffer[0x0b]);
	if(!CurrentPageLoc) return;
	if(l == 1 || (top == 0 && l == 3)){
		MetatileBuffer[10] = 0x52;              /* PlayerStop: a brick at the floor */
		return;
	}
	if(l != 2) return;
	spawn(0, 7, StarFlagObject);
}

AOBJI(WaterPipe){
	u8 row = GetLrgObjAttrib();
	mb_put2(row, 0x6b, 0x6c);
}

/* IntroPipe (exit false) and ExitPipe: RenderSidewaysPipe, then IntroPipe's vertical part
** where the shaft is drawn */
static void SidewaysPipe(u8 *o, bool exit){
	ChkLrgObjFixedLength(o, 3);
	u8 y = exit ? GetLrgObjAttrib() >> 8 : 0x0a;
	u8 m5 = y - 2, m6 = o[OFS_LEN];
	M05 = m5;
	u8 a = RD(SidePipeData, m6);
	if(a) RenderUnderPart(0, m5, a);            /* the shaft, rows 0 to M05 */
	mb_put2(m5 + 1, RD(SidePipeData, m6 + 4), RD(SidePipeData, m6 + 8));
	if(!a || exit) return;
	mb_clear(7);
	MetatileBuffer[7] = RD(VerticalPipeData, m6);
}

AOBJI(IntroPipe){
	SidewaysPipe(o, false);
}

AOBJI(ExitPipe){
	SidewaysPipe(o, true);
}

AOBJI(VerticalPipe){
	ChkLrgObjFixedLength(o, 1);                 /* GetPipeHeight */
	u16 r = GetLrgObjAttrib();
	u8 h = (r >> 8) & 0x07, row = r;
	u8 y = o[OFS_LEN];
	if(m) y += 4;                             /* a decoration pipe: usage control bit clear */
	if((AreaNumber | WorldNumber) && o[OFS_LEN]){
		u8 e = spawn(8, row, PiranhaPlant);
		if(e != 5){
			X_ = e;
			InitPiranhaPlant();
		}
	}
	RenderTop(row, h - 1, RD(VerticalPipeData, y + 2), RD(VerticalPipeData, y));
}

/* Bridge_High, _Middle, _Low (M00 2-4), Hole_Water (5), QuestionBlockRow_High, _Low (6-7):
** a metatile at a fixed row, then some rows of another under it */
AOBJI(Row12Object){
	ChkLrgObjLength(o);
	const u8 *d = &Row12Data[(u8)((m - 2) << 2)];
	u8 row = pgm_read_byte(d), a = pgm_read_byte(d + 3);
	u8 top = pgm_read_byte(d + 1);
	if(a) RenderTop(row, pgm_read_byte(d + 2), a, top);
	else MetatileBuffer[row] = top;
}

AOBJI(FlagBalls_Residual){
	RenderUnderPart(2, GetLrgObjAttrib() >> 8, 0x6d);
}

AOBJI(FlagpoleObject){
	RenderTop(0, 8, 0x25, 0x24);
	MetatileBuffer[10] = 0x61;
	spawn(-8, 1, FlagpoleFlagObject);
	FlagpoleFNum_Y_Pos = 0xb0;
}

AOBJI(EndlessRope){
	RenderUnderPart(0, 0x0f, 0x40);
}

AOBJI(BalancePlatRope){
	RenderUnderPart(1, 0x0f, 0x44);
	RenderUnderPart(1, GetLrgObjAttrib() >> 8, 0x40);
}

/* RowOfBricks, RowOfSolidBlocks, RowOfCoins (M00 2-4), ColumnOfBricks, ColumnOfSolidBlocks
** (M00 5-6) */
AOBJ(RowColObject){
	u8 n = m - 2, t = AreaType;
	if(n == 0 && CloudTypeOverride) t = 4;
	u8 a = RD(RowColMetatiles, (u8)(RD(RowColBase, n) + t));
	u16 r = (n < 3) ? (u8)ChkLrgObjLength(o) : GetLrgObjAttrib();
	RenderUnderPart(r, r >> 8, a);
}

/* ChainObj (M00 3), AxeObj (2), CastleBridgeObj (4) */
AOBJI(ChainObj){
	u8 y = m - 2, a = 0x0c;                     /* C_ObjectRow y + 6: y is 0-2 in every level */
	if(y == 0){
		BowserPaletteRequest();                 /* the 6502 loaded Bowser's palette (VRAM_Buffer_AddrCtrl 8) */
		a = 0xc5;
	}
	if(y == 2){
		ChkLrgObjFixedLength(o, 0x0c);
		a = 0x89;
	}
	RenderUnderPart(y + 6, 0, a);
}

AOBJI(StaircaseObject){
	if((s16)ChkLrgObjLength(o) < 0) StaircaseControl = 9;
	u8 y = --StaircaseControl;
	RenderUnderPart(RD(StaircaseRowData, y), RD(StaircaseHeightData, y), 0x61);
}

AOBJI(Jumpspring){
	u8 row = GetLrgObjAttrib();
	spawn(0, row, JumpspringObject);
	mb_put2(row, 0x67, 0x68);
}

/* QuestionBlock (M00 0-2), Hidden1UpBlock (3), BrickWithItem (4-6, 8), BrickWithCoins (7),
** EmptyBlock (10) */
AOBJI(BlockObject){
	u8 id = m, a = 0xc4;
	if(id != 10){
		if(id == 3){
			if(!Hidden1UpFlag) return;
			Hidden1UpFlag = 0;
		}
		if(id == 7) BrickCoinTimerFlag = 0;
		u8 i = id;
		if(id >= 3 && AreaType != 1) i += 5;
		a = RD(BrickQBlockMetatiles, i);
	}
	RenderUnderPart(GetLrgObjAttrib(), 0, a);
}

AOBJ(Hole_Empty){
	u16 r = ChkLrgObjLength(o);
	u8 t = AreaType;
	if((s16)r < 0 && t == 0) put_cannon(r >> 8, -0x10, 5);   /* d15 shifts out */
	RenderUnderPart(8, 0x0f, t ? 0x00 : 0x87);   /* HoleMetatiles (AreaType is 0-3) */
}

/* RunAObj: DecodeAreaData's jump table, by M00 + M07 (n), m = M00 */
static void NOINLINE RunAObj(u8 *o, u8 n, u8 m){
	switch(n){
	/* large objects (rows 0-11, d6-d4 set) */
	case 0x00: case 0x07: VerticalPipe(o, m); break;   /* warp pipes, decoration pipes */
	case 0x01: AreaStyleObject(o, m); break;
	case 0x02 ... 0x06: RowColObject(o, m); break;
	/* special row 12 */
	case 0x08: Hole_Empty(o, m); break;
	case 0x09: PulleyRopeObject(o, m); break;
	case 0x0a ... 0x0f: Row12Object(o, m); break;
	/* special row 15 */
	case 0x10: EndlessRope(o, m); break;
	case 0x11: BalancePlatRope(o, m); break;
	case 0x12: CastleObject(o, m); break;
	case 0x13: StaircaseObject(o, m); break;
	case 0x14: ExitPipe(o, m); break;
	case 0x15: FlagBalls_Residual(o, m); break;
	/* small objects (rows 0-11, d6-d4 clear) */
	case 0x16 ... 0x1e: case 0x20: BlockObject(o, m); break;
	case 0x1f: WaterPipe(o, m); break;
	case 0x21: Jumpspring(o, m); break;
	/* special row 13 (d6 set) */
	case 0x22: IntroPipe(o, m); break;
	case 0x23: FlagpoleObject(o, m); break;
	case 0x24 ... 0x26: ChainObj(o, m); break;
	case 0x27: ScrollLockObject_Warp(o, m); break;
	case 0x28: case 0x29: ScrollLockObject(o, m); break;
	case 0x2a ... 0x2c: AreaFrenzy(o, m); break;
	/* 0x2d LoopCmdE: nothing; special row 14 */
	case 0x2e: AlterAreaAttributes(o, m); break;
	}
}

/* ---------------------------------------------------------------------------------------
** The parser
** ------------------------------------------------------------------------------------ */

/* a parser variable at or after CurrentPageLoc, through the base pointer P (ldd/std) */
#define PV(f) P[offsetof(struct RamArea, f##_) - offsetof(struct RamArea, CurrentPageLoc_)]

/* ProcessAreaData, with DecodeAreaData: each of the three object buffers x (2 to 0) decodes
** the object at the area data offset (o[3] negative: fresh) or continues the one in it. Both
** read the same two bytes when fresh (the 6502 read them twice); x stays ObjectOffset */
static void ProcessAreaData(void){
	u8 *P = &CurrentPageLoc;
	asm("" : "+r"(P));                       /* GCC must not fold it back into addresses */
	u8 *const ob = &PV(AreaObjOffsetBuffer);
	do{
		for(u8 x = 2;; x--){
			ObjectOffset = x;
			PV(BehindAreaParserFlag) = 0;
			u8 *o = ob + x;
			u8 ofs = PV(AreaDataOffset);
			bool fresh = (s8)o[OFS_LEN] < 0;
			u16 w = ad2(fresh ? ofs : o[0]);
			u8 b = w, b2 = w >> 8, r = b & 0x0f;
			if(b == 0xfd) goto ChkLength;               /* end of level */
			if(fresh){
				if((b2 & 0x80) && !PV(AreaObjectPageSel)){
					PV(AreaObjectPageSel)++;
					PV(AreaObjectPageLoc)++;
				}
				if(r == 0x0d){
					if(!(b2 & 0x40) && !PV(AreaObjectPageSel)){
						PV(AreaObjectPageLoc) = b2 & 0x1f;   /* page control */
						goto Next;
					}
				}else if(r == 0x0e && PV(BackloadingFlag)) goto Decode;
				if(PV(AreaObjectPageLoc) < PV(CurrentPageLoc)){
					PV(BehindAreaParserFlag) = 1;
					goto Next;
				}
			}
Decode:		;
			u8 m7 = 0, a = (b2 >> 4) & 0x07;            /* d6-d4 */
			if(r == 0x0e){
				a = 0x2e;
			}else if(r == 0x0d){
				m7 = 0x22;
				if(!(b2 & 0x40)){                         /* page control, handled above */
					M07 = m7;
					goto ChkLength;
				}
				b2 &= 0x7f;
				if(b2 == 0x4b) PV(LoopCommand)++;
				a = b2 & 0x3f;
			}else if(r < 0x0c){
				if(!a){
					m7 = 0x16;                            /* small object */
					a = b2 & 0x0f;
				}else if(a == 0x07 && (b2 & 0x08)) a = 0; /* warp pipe */
			}else{
				m7 = (r == 0x0f) ? 0x10 : 0x08;          /* SpecObj: rows 12 and 15 */
			}
			M07 = m7;
			M00 = a;
			if(fresh){
				if(PV(AreaObjectPageLoc) != PV(CurrentPageLoc)){
					if(r != 0x0e || !PV(BackloadingFlag)) goto ChkLength;
				}else{
					if(PV(BackloadingFlag)){
						PV(BackloadingFlag) = 0;
						PV(BehindAreaParserFlag) = 0;
						ObjectOffset = x = 0;
						o = ob;
						goto ChkLength;
					}
					if((b >> 4) != PV(CurrentColumnPos)) goto ChkLength;
				}
				o[0] = ofs;                               /* StrAObj */
				PV(AreaDataOffset) = ofs + 2;                 /* IncAreaObjOffset */
				PV(AreaObjectPageSel) = 0;
			}
			RunAObj(o, a + m7, a);
			goto ChkLength;
Next:
			PV(AreaDataOffset) = ofs + 2;                     /* IncAreaObjOffset */
			PV(AreaObjectPageSel) = 0;
ChkLength:
			if((s8)o[OFS_LEN] >= 0) o[OFS_LEN]--;
			if(!x) break;
		}
	}while(PV(BehindAreaParserFlag) || PV(BackloadingFlag));
}

/* AreaParserCore: the column at CurrentPageLoc/CurrentColumnPos, rendered into the metatile
** buffer and stored in the block buffer at BlockBufferColumnPos */
void AreaParserCore(void){
	if(BackloadingFlag) ProcessAreaData();

	u8 *mb = MetatileBuffer;
	mb_clear(13);

	/* background scenery */
	u8 y = BackgroundScenery;
	if(y){
		u8 a = CurrentPageLoc;
		while(a >= 3) a -= 3;
		while(--y) a += 3;                      /* BSceneDataOffsets: 0x30 a scenery type */
		a = RD(BackSceneryData, (u8)((a << 4) + CurrentColumnPos));
		if(a){
			const u8 *s = &BackSceneryMetatiles[(u8)(((a & 0x0f) - 1) * 3)];
			y = a >> 4;
			for(u8 n = 3; n; n--){
				mb[y] = pgm_read_byte(s++);
				if(++y == 0x0b) break;
			}
		}
	}
	/* foreground scenery */
	y = ForegroundScenery;
	if(y){
		const u8 *s = ForeSceneryData;
		while(--y) s += 13;                     /* FSceneDataOffsets */
		u8 *d = mb, n = 13;
		do{
			asm("" : "+r"(s));
			u8 a = pgm_read_byte(s++);
			if(a) *d = a;
			d++;
		}while(--n);
	}
	/* terrain: two bytes of bits, one a row from the top */
	u8 t, at = AreaType;
	if(at == 0 && WorldNumber == World8) t = 0x62;
	else{
		t = RD(TerrainMetatiles, at);
		if(CloudTypeOverride) t = 0x88;
	}
	y = TerrainControl << 1;
	u16 bits = pgm_read_word(&TerrainRenderBits[y]);
	if(CloudTypeOverride) bits &= 0x08ff;
	u8 b1 = bits >> 8;
	for(u8 x = 0; x < 13; x++){
		if(bits & 1) mb[x] = t;
		bits >>= 1;
		if(at == 2 && x == 10) t = 0x54;
	}
	M00 = b1;
	M01 = y + 2;
	M07 = t;

	ProcessAreaData();
	u8 col[13];
	u8 c = BlockBufferColumnPos;
	M06 = c;                                    /* so the objects' M06 (the 6502's) are not kept */
	for(u8 x = 0; x < 13; x++){
		u8 a = mb[x];
		if(a < RD(BlockBuffLowBounds, a >> 6)) a = 0;
		col[x] = a;
	}
	bb_PutColumn(c, col);
	bb_PutView(c, mb);
}

void IncrementColumnPos(void){
	u8 c = (CurrentColumnPos + 1) & 0x0f;
	CurrentColumnPos = c;
	if(!c) CurrentPageLoc++;
	BlockBufferColumnPos = (BlockBufferColumnPos + 1) & 0x1f;
}

/* ---------------------------------------------------------------------------------------
** Area pointers and headers
** ------------------------------------------------------------------------------------ */

INLINE u8 get_area_type(u8 a){
	return AreaType = (a & 0x60) >> 5;
}

void LoadAreaPointer(void){
	u8 p = pgm_read_byte(&AreaAddrOffsets[pgm_read_byte(&WorldAddrOffsets[WorldNumber]) + AreaNumber]);
	AreaPointer = p;
	get_area_type(p);
}

/* the address of area (or enemy) data l of type t */
static const u8 *data_addr(const u8 *const *tab, const u8 *hofs, u8 t, u8 l){
	return (const u8 *)pgm_read_ptr(&tab[(u8)(pgm_read_byte(&hofs[t]) + l)]);
}

void GetAreaDataAddrs(void){
	u8 p = AreaPointer;
	u8 t = get_area_type(p);
	u8 l = p & 0x1f;
	AreaAddrsLOffset = l;
	EnemyData = data_addr(EnemyDataAddr, EnemyAddrHOffsets, t, l);
	const u8 *d = data_addr(AreaDataAddr, AreaDataHOffsets, t, l);
	u8 h = LVB(d);
	set_fore(h);
	PlayerEntranceCtrl = (h & 0x38) >> 3;
	GameTimerSetting = h >> 6;
	h = LVB(d + 1);
	set_terrain(h);
	h >>= 6;
	if(h == 3){
		CloudTypeOverride = h;
		h = 0;
	}
	AreaStyle = h;
	AreaData = d + 2;
}
