/*
 * The player and the game modes: a port of smbdis.asm's game setup (lines 2649-3035), the game
 * core and every player routine (5290-6268), the player's animation logic (14585-14760) and the
 * relative-position and offscreen-bit routines (14761-15000). Routine names are the 6502's;
 * smb.h has the conventions. The drawing (PlayerGfxHandler, RenderPlayerSub...) is gfx17.c's.
 */
#include <string.h>
#include <stddef.h>
#include <avr/pgmspace.h>
#include "smb.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

#define RD(t, i) pgm_read_byte(&(t)[i])

/* the bmi/bpl after a cmp: d7 of the 8-bit difference, not a signed comparison */
static inline bool cmp_neg(u8 a, u8 b){ return (u8)(a - b) & 0x80; }

/* a byte of ra by its offset: two arrays read through one pointer */
#define RA_OFS(f) offsetof(struct RamArea, f)

/* gcc's -Os choices, measured: some helpers are smaller inlined, some kept out of line */
#define INLINE static inline __attribute__((always_inline))
#define NOINLINE static __attribute__((noinline))

/* a pointer gcc must keep in a register (Y or Z): the fields up to 63 bytes after it are
** then reached by displacement (ldd/std, 2 bytes) instead of their address (lds/sts, 4) */
	#define OPQ(p) ({ __typeof__(p) p_ = (p); __asm__("" : "+b"(p_)); p_; })
	#define OPZ(p) ({ __typeof__(p) p_ = (p); __asm__("" : "+z"(p_)); p_; })   /* Z: no call */
/* field f, of its own type, through pointer b to field v (f 0-63 bytes after v) */
#define AT(b, v, f) (*(__typeof__(&(f)))((u8 *)(b) + ((u8 *)&(f) - (u8 *)&(v))))
/* the $0700 fields (SavedJoypadBits to ScrollLock and on) through q */
#define Q(f) AT(q, SavedJoypadBits, f)

/* ---------------------------------------------------------------------------------------
** Data
** ------------------------------------------------------------------------------------ */
static const u8 MusicSelectData[6] PROGMEM = {
	WaterMusic, GroundMusic, UndergroundMusic, CastleMusic, CloudMusic, PipeIntroMusic
};

static const u8 PlayerStarting_X_Pos[4] PROGMEM = { ROM_PlayerStarting_X_Pos };
static const u8 PlayerStarting_Y_Pos[9] PROGMEM = { ROM_PlayerStarting_Y_Pos };

/* page numbers are in order from -1 to -4 */
static const u8 HalfwayPageNybbles[16] PROGMEM = { ROM_HalfwayPageNybbles };

static const u8 Hidden1UpCoinAmts[8] PROGMEM = { ROM_Hidden1UpCoinAmts };

static const u8 ClimbAdderLow[4] PROGMEM = { ROM_ClimbAdderLow };  /* ClimbAdderHigh: its sign */

/* by jump speed (0-4) or swimming (5, 6 in a whirlpool), one row each: JumpMForceData,
** FallMForceData, PlayerYSpdData (InitMForceData is $80 for 5, else 0) */
static const u8 JumpPhysData[3 * 7] PROGMEM = { ROM_JumpPhysData };
/* MaxLeftXSpdData is its negative ($d8 $e8 $f0); $0c, pipe intros, in code */
static const u8 MaxRightXSpdData[3] PROGMEM = { ROM_MaxRightXSpdData };
static const u8 FrictionData[3] PROGMEM = { ROM_FrictionData };

const u8 PlayerGfxTblOffsets[16] PROGMEM = { ROM_PlayerGfxTblOffsets };

/* the growing half; the shrinking half ($02 on even frames, $00 on odd) is tested in code */
static const u8 ChangeSizeOffsetAdder[10] PROGMEM = { ROM_ChangeSizeOffsetAdder };

/* the offscreen-bit routines' data by axis: DividePDiff's $06 and $05, DefaultX/YOnscreenOfs,
** then X/YOffscreenBitsData */
static const u8 OffscrXData[2 + 3 + 16] PROGMEM = { 0x38, 0x08, ROM_OffscrXData };
static const u8 OffscrYData[2 + 3 + 9] PROGMEM = { 0x20, 0x04, ROM_OffscrYData };

typedef void (*fn_t)(void);

static void PlayerCtrlRoutine_(void);
static void lr_air(void);
static void chk_p_offscr(void);
INLINE void x_physics(u8 *q);
static void impose_friction(u8 a);
static void next_area(void);
static void get_area_music(void);
static u8 offscr(u8 x, bool vert);

/* ChgAreaMode: screen off, the game mode restarts at InitializeArea */
NOINLINE void chg_area_mode(void){
	DisableScreenFlag++;
	OperMode_Task = 0;
	Sprite0HitDetectFlag = 0;
}

/* JumpEngine: runs entry i of a PROGMEM table of n routines */
NOINLINE void jump_engine(const fn_t *t, u8 i, u8 n){
	if(i < n) ((fn_t)pgm_read_ptr(&t[i]))();
}

/* ---------------------------------------------------------------------------------------
** Game setup (2649-3035)
** ------------------------------------------------------------------------------------ */

/* InitializeGame: clears memory to $076f, sets the demo timer, then InitializeArea (which
** clears $0000-$074b again: only the $074c-$076f half is needed here) */
void InitializeGame(void){
	memset(&rg, 0, sizeof rg);
	/* ClrSndLoop: the sound engine's memory ($07b0-$07cf), sound.c's business */
	DemoTimer = 0x18;
	LoadAreaPointer();
	InitializeArea();
}

/* GetScreenPosition: the right edge, 255 pixels right of the left one */
INLINE void get_screen_position(void){
	u16 s = (u16)ScreenLeft_X_Pos + 0xff;
	ScreenRight_X_Pos = (u8)s;
	ScreenRight_PageLoc = ScreenLeft_PageLoc + (s >> 8);
}

/* InitializeArea: clears memory to $074b and the timers, sets the start page and the hard modes */
void InitializeArea(void){
	/* InitializeMemory $4b: the 6502 cleared $0000-$074b (skipping $0160-$01ff) */
	RA_CLEAR();
	bb_Clear();                         /* the block buffer (SPI RAM) was part of it */
	/* the sound queues and AreaMusicBuffer live in the 6502's zero page: cleared too */
	u8 *g = OPQ(&PauseSoundQueue);
#define G(f) AT(g, PauseSoundQueue, f)
	memset(g, 0, 6);                    /* Pause .. Square1SoundQueue */
	G(AreaMusicBuffer) = 0;
	memset(Timers, 0, 0x22);                      /* $0780-$07a1 */
	u8 a = G(HalfwayPage);
	if(G(AltEntranceControl)) a = G(EntrancePage);
	u8 *z = OPZ(&ScreenLeft_PageLoc);
#define Z(f) AT(z, ScreenLeft_PageLoc, f)
	Z(ScreenLeft_PageLoc) = a;
	Z(CurrentPageLoc) = a;
	Z(BackloadingFlag) = a;
	Z(ScreenRight_X_Pos) = 0xff;                  /* GetScreenPosition: ScreenLeft_X_Pos is 0 */
	Z(ScreenRight_PageLoc) = a;
	/* CurrentNTAddr_High/Low: the name table to render from ($2080/$2480), the PPU's */
	BlockBufferColumnPos = (a & 0x01) << 4;
	Z(AreaObjectLength[0]) = 0xff;                /* cleared, then decremented */
	Z(AreaObjectLength[1]) = 0xff;
	Z(AreaObjectLength[2]) = 0xff;
	Z(ColumnSets) = 0x0b;
#undef Z
	GetAreaDataAddrs();
	u8 w = G(WorldNumber);
	if(G(PrimaryHardMode) || w > World5 || (w == World5 && G(LevelNumber) >= Level3))
		SecondaryHardMode = 1;                    /* cleared above, incremented */
	if(G(HalfwayPage)) PlayerEntranceCtrl = 0x02;
	G(AreaMusicQueue) = Silence;
#undef G
	DisableScreenFlag = 0x01;
	OperMode_Task++;
}

/* PrimaryGameSetup: a new game: timer from the header, small player, three lives */
void PrimaryGameSetup(void){
	FetchNewGameTimerFlag = 0x01;
	PlayerSize = 0x01;
	NumberofLives = 3 - 1;
	/* OffScr_NumberofLives: the second player's, 1 player only */
	SecondaryGameSetup();
}

/* SecondaryGameSetup: the per-area state cleared before the game core runs */
void SecondaryGameSetup(void){
	DisableScreenFlag = 0;
	/* ClearVRLoop: clears the VRAM buffer $0300-$03ff, the platform's */
	GameTimerExpiredFlag = 0;
	DisableIntermediate = 0;
	BackloadingFlag = 0;
	BalPlatformAlignment = 0xff;
	/* Mirror_PPU_CTRL_REG1 d0 = ScreenLeft_PageLoc d0: the name table shown, the platform's */
	get_area_music();
	InitSprOffsets();                   /* SprShuffleAmt, SprDataOffset (sprites.c); no sprite 0 */
	/* DoNothing2/DoNothing1: residual, not called */
	Sprite0HitDetectFlag++;
	OperMode_Task++;
}

/* GetAreaMusic: queues the area's music (pipe intro, cloud, or by area type) */
static void get_area_music(void){
	if(!OperMode) return;
	u8 y = 0x05;
	if(AltEntranceControl == 0x02 || (PlayerEntranceCtrl & 0xfe) != 0x06){
		y = AreaType;
		if(CloudTypeOverride) y = 0x04;
	}
	AreaMusicQueue = RD(MusicSelectData, y);
}

/* Entrance_GameTimerSetup: places the player for the entrance, loads the game timer */
static void Entrance_GameTimerSetup_(void){
	Player_PageLoc = ScreenLeft_PageLoc;
	VerticalForceDown = 0x28;
	PlayerFacingDir = 0x01;
	Player_Y_HighPos = 0x01;
	Player_State = 0;
	Player_CollisionBits--;
	HalfwayPage = 0;
	SwimmingFlag = !AreaType;
	u8 x = PlayerEntranceCtrl;
	u8 y = AltEntranceControl;
	if(y >= 2) x = (y & 0x01) ? 0x00 : 0x08;      /* AltYPosOffset: $08, $00 */
	Player_X_Position = RD(PlayerStarting_X_Pos, y);
	Player_Y_Position = RD(PlayerStarting_Y_Pos, x);
	/* PlayerBGPriorityData: $20 for 1 and 8 (GameTimerData's dummy), else 0 */
	Player_SprAttrib = (x == 0x01 || x == 0x08) ? 0x20 : 0;
	GetPlayerColors();
	u8 *z = OPZ(&StarInvincibleTimer);
#define Z(f) AT(z, StarInvincibleTimer, f)
	y = GameTimerSetting;
	if(y && Z(FetchNewGameTimerFlag)){
		Z(GameTimerDisplay[0]) = 0x05 - y;       /* GameTimerData: 4, 3, 2 for 1-3 */
		Z(GameTimerDisplay[2]) = 0x01;
		Z(GameTimerDisplay[1]) = 0;
		Z(FetchNewGameTimerFlag) = 0;
		Z(StarInvincibleTimer) = 0;
	}
	if(Z(JoypadOverride)){
#undef Z
		Player_State = 0x03;
		X_ = 0;
		InitBlock_XY_Pos();
		Block_Y_Position[0] = 0xf0;
		X_ = 5;
		Y_ = 0;
		Setup_Vine();
	}
	if(!AreaType){
		/* the 6502's X here: GetPlayerColors' VRAM_Buffer1_Offset, 0 on an area's first frame */
		X_ = 0;
		SetupBubble();
	}
	GameEngineSubroutine = 0x07;
}

/* LoadAreaPointer, with a new game timer */
static void load_area(void){
	LoadAreaPointer();
	FetchNewGameTimerFlag++;
}

#if TWO_PLAYERS
/* A player's own: the 6502's OnscreenPlayerInfo (lives, halfway page, level, hidden 1-up flag,
** coin tally, world, area: $075a to $0760), then his score and his coins' count, which the NES
** kept for both players in RAM. Here the live ones are the player's whose turn it is, and
** each player has a place in SPI RAM (bb.c), 16 bytes on from the other's: player p's own
** to his place (store), or from it */
#define OWN_INFO  7
#define OWN_SCORE 8
_Static_assert(offsetof(__typeof__(rg), AreaNumber_) == offsetof(__typeof__(rg), NumberofLives_) + OWN_INFO - 1
               && offsetof(__typeof__(rp), CoinDisplay_) == offsetof(__typeof__(rp), PlayerScoreDisplay_) + 6,
               "player.c: a player's own bytes");
static __attribute__((noinline)) void own(u8 p, bool store){
	p <<= 4;
	bb_Park(p, &NumberofLives, OWN_INFO, store);
	bb_Park(p + OWN_INFO, PlayerScoreDisplay, OWN_SCORE, store);
}

/* A game starts: the second player as the first. (The 6502 wrote each field for both:
** PrimaryGameSetup's lives, StartWorld1's hidden 1-up flag, GoContinue's world and area,
** InitScores) */
void StartPlayers(void){
	own(1, true);
}

/* TransposePlayers: in a game of two the turn passes to the other player, if he has a life
** left. True, the 6502's carry set, when it does not: one player, or the other's game is
** over (OffScr_NumberofLives negative: his own are taken to see, and given back) */
static bool TransposePlayers_(void){
	if(!NumberOfPlayers) return true;
	u8 p = CurrentPlayer;
	own(p, true);
	own(p ^ 1, false);
	if((s8)NumberofLives < 0){
		own(p, false);
		return true;
	}
	CurrentPlayer = p ^ 1;
	return false;
}

/* ContinueGame: the area again, with a small player */
static void ContinueGame_(void){
	LoadAreaPointer();
	PlayerSize = 0x01;
	FetchNewGameTimerFlag++;
	TimerControl = 0;
	PlayerStatus = 0;
	GameEngineSubroutine = 0;
	OperMode_Task = 0;
	OperMode = 0x01;
}
#endif

/* PlayerLoseLife: takes a life; game over, or back to the halfway page if passed */
static void PlayerLoseLife_(void){
	u8 *g = OPQ(&EventMusicQueue);
#define G(f) AT(g, EventMusicQueue, f)
	G(EventMusicQueue) = Silence;
	chg_area_mode();
	if((s8)--G(NumberofLives) < 0){
		OperMode = GameOverModeValue;
		return;
	}
	u8 l = G(LevelNumber);
	u8 x = G(WorldNumber) << 1;
	if(l & 0x02) x++;
	u8 a = RD(HalfwayPageNybbles, x);
	if(!(l & 0x01)) a >>= 4;                     /* areas -1 and -3: the high nybble */
	a &= 0x0f;
	if(a > ScreenLeft_PageLoc) a = 0;            /* must have reached the halfway page */
	G(HalfwayPage) = a;
	/* TransposePlayers: 1 player, carry set; ContinueGame: reloads the area, small player */
#if TWO_PLAYERS
	TransposePlayers_();                         /* (in a game of two: the other's turn, if he has one) */
	ContinueGame_();
#else
	LoadAreaPointer();
	G(FetchNewGameTimerFlag)++;
	G(PlayerSize) = 0x01;
	TimerControl = 0;
	G(PlayerStatus) = 0;
	GameEngineSubroutine = 0;
	OperMode = 0x01;
#endif
#undef G
}

/* GameOverMode: OperMode 3: SetupGameOver, the screen routines, RunGameOver */
void GameOverMode(void){
	u8 t = OperMode_Task;
	if(!t){                                      /* SetupGameOver */
		ScreenRoutineTask = 0;
		EventMusicQueue = GameOverMusic;
		chg_area_mode();
		OperMode_Task = 1;                       /* after chg_area_mode's 0 */
	}else if(t == 1){
		ScreenRoutines();
	}else if(t == 2){                            /* RunGameOver: start or the screen timer */
		DisableScreenFlag = 0;
		if(!(SavedJoypad1Bits & Start_Button) && ScreenTimer) return;
		TerminateGame();
	}
}

/* TerminateGame: no other player (TransposePlayers' carry is always set): back to the title
** screen, saving the continue world */
void TerminateGame(void){
	EventMusicQueue = Silence;
#if TWO_PLAYERS
	if(!TransposePlayers_()){                    /* the other player's game goes on */
		ContinueGame_();
		return;
	}
#endif
	ContinueWorld = WorldNumber;
	OperMode_Task = 0;
	ScreenTimer = 0;
	OperMode = 0;
}

/* ---------------------------------------------------------------------------------------
** Game mode and the game core (5290-5474)
** ------------------------------------------------------------------------------------ */

static const fn_t GameModeTbl[4] PROGMEM = {
	InitializeArea, ScreenRoutines, SecondaryGameSetup, GameCoreRoutine
};

/* GameMode: OperMode 1 */
void GameMode(void){
	jump_engine(GameModeTbl, OperMode_Task, 4);
}

/* CyclePlayerPalette: a's low two bits as the player's palette */
static void cycle_pal(u8 a){
	M00 = a &= 0x03;
	Player_SprAttrib = (Player_SprAttrib & 0xfc) | a;
}

/* ResetPalStar: the player's palette back to 0 */
static void reset_pal_star(void){
	Player_SprAttrib &= 0xfc;
}

/* GameEngine: every object of the frame, in the original order */
INLINE void GameEngine(void){
	ProcFireball_Bubble();
	for(u8 x = 0; x < 6; x++){
		ObjectOffset = x;
		X_ = x;
		EnemiesAndLoopsCore();
		X_ = x;
		FloateyNumbersRoutine();
	}
	GetPlayerOffscreenBits();
	RelativePlayerPosition();
	PlayerGfxHandler();
	BlockObjMT_Updater();
	for(s8 x = 1; x >= 0; x--){
		ObjectOffset = x;
		X_ = x;
		BlockObjectsCore();
	}
	MiscObjectsCore();
	ProcessCannons();
	ProcessWhirlpools();
	FlagpoleRoutine();
	RunGameTimer();
	ColorRotation();
	u8 st = StarInvincibleTimer;
	if(cmp_neg(Player_Y_HighPos, 0x02)){         /* not below the screen: the music */
		if(!st){
			reset_pal_star();                    /* ClrPlrPal */
			goto SaveAB;
		}
		if(st == 0x04 && !IntervalTimerControl) get_area_music();
	}
	{                                            /* NoChgMus: below the screen too */
		u8 a = FrameCounter;
		if(st < 0x08) a >>= 2;
		cycle_pal(a >> 1);
	}
SaveAB:
	PreviousA_B_Buttons = A_B_Buttons;
	Left_Right_Buttons = 0;
	UpdScrollVar();
}

static const fn_t GameRoutinesTbl[13] PROGMEM;

/* GameCoreRoutine: the player's routine (GameRoutines), then the game engine once the area
** is set up */
void GameCoreRoutine(void){
	/* SavedJoypadBits,x with CurrentPlayer: player 1's bits, 1 player only */
#if TWO_PLAYERS
	if(CurrentPlayer) SavedJoypadBits = game_Pad2();   /* (the second player's turn: the second pad) */
#endif
	jump_engine(GameRoutinesTbl, GameEngineSubroutine, 13);
	if(OperMode_Task >= 0x03) GameEngine();
}

/* UpdScrollVar: runs the area parser when it has work or the scroll passed 32 pixels */
void UpdScrollVar(void){
	if(VRAM_Buffer_AddrCtrl == 0x06) return;
	if(!AreaParserTaskNum){
		u8 s = ScrollThirtyTwo;
		if(cmp_neg(s, 0x20)) return;
		ScrollThirtyTwo = s - 0x20;              /* carry set by the cmp */
		/* VRAM_Buffer2_Offset = 0: the level graphics buffer, the platform's */
	}
	AreaParserTaskHandler();
}

/* ScrollHandler: scrolls by the player's movement once past the screen's middle */
static void ScrollHandler_(void){
	u8 y = Player_X_Scroll += Platform_X_Scroll;
	u8 p = Player_Pos_ForScroll;
	if(!ScrollLock && p >= 0x50 && !SideCollisionTimer && !cmp_neg(y, 1)){
		/* not 0 nor "negative" ($81-$ff) */
		if(p < 0x70 && y >= 0x02) y--;           /* ChkNearMid */
		Y_ = y;
		ScrollScreen();
		return;
	}
	ScrollAmount = 0;                            /* InitScrlAmt */
	chk_p_offscr();
}

/* ChkPOffscr: keeps the player inside the screen's edges, stopping him against them */
static void chk_p_offscr(void){
	u8 a = offscr(0, false);                     /* the player */
	M00 = a;
	u8 y = 0;
	if(!(a & 0x80)){
		y = 1;
		if(!(a & 0x20)) goto InitPlatScrl;
	}
	{                                            /* KeepOnscr */
		u8 *ed = (u8 *)&ra + RA_OFS(ScreenLeft_PageLoc__) + y;
		u8 e = ed[RA_OFS(ScreenLeft_X_Pos__) - RA_OFS(ScreenLeft_PageLoc__)];
		u8 s = y << 4;                           /* X_SubtracterData: $00, $10 */
		Player_X_Position = e - s;
		Player_PageLoc = ed[0] - (e < s);
		if(Left_Right_Buttons != y + 1) Player_X_Speed = 0;   /* OffscrJoypadBitsData */
	}
InitPlatScrl:
	Platform_X_Scroll = 0;
}

/* ScrollScreen: scrolls right by Y_ pixels (VictoryMode calls it too) */
void ScrollScreen(void){
	u8 y = Y_;
	ScrollAmount = y;
	ScrollThirtyTwo += y;
	u16 s = (u16)ScreenLeft_X_Pos + y;
	ScreenLeft_X_Pos = (u8)s;
	HorizontalScroll = (u8)s;
	ScreenLeft_PageLoc += s >> 8;
	/* Mirror_PPU_CTRL_REG1 d0 = ScreenLeft_PageLoc d0: the name table shown, the platform's */
	get_screen_position();
	ScrollIntervalTimer = 0x08;                  /* residual */
	chk_p_offscr();
}

/* ---------------------------------------------------------------------------------------
** Player routines (5494-5874)
** ------------------------------------------------------------------------------------ */

/* AutoControlPlayer: the controller bits forced to a, then PlayerCtrlRoutine */
NOINLINE void auto_ctrl(u8 a){
	SavedJoypadBits = a;
	PlayerCtrlRoutine_();
}

void AutoControlPlayer(void){
	auto_ctrl(A_);
}

/* MovePlayerYAxis: adds a to the player's vertical position */
INLINE void move_y(u8 a){
	Player_Y_Position += a;
}

/* EnterSidePipe: walks right at speed 8, stopping on a 16-pixel boundary */
static void enter_side_pipe(void){
	u8 y = (Player_X_Position & 0x0f) ? 0x01 : 0;
	Player_X_Speed = y << 3;
	auto_ctrl(y);
}

/* PlayerEntrance: walks in, rises out of a pipe or climbs a vine, then hands over control */
static void PlayerEntrance_(void){
	if(AltEntranceControl == 0x02){              /* EntrMode2 */
		if(!JoypadOverride){
			move_y(0xff);                        /* up one pixel */
			if(Player_Y_Position >= 0x91) return;
		}else{                                   /* VineEntr */
			if(VineHeight != 0x60) return;
			u8 y = 0, a = 0x01;                  /* walk right, off the vine */
			if(Player_Y_Position >= 0x99){
				Player_State = 0x03;
				y = 1;
				a = 0x08;                        /* climb up */
				bb_Put(4, 0xb4 >> 4, 0x08);      /* Block_Buffer_1+$b4: covers the hole */
			}
			DisableCollisionDet = y;
			auto_ctrl(a);
			if(Player_X_Position < 0x48) return;
		}
	}else{
		u8 a = 0;
		if(Player_Y_Position < 0x30) goto Auto;
		if((PlayerEntranceCtrl & 0xfe) == 0x06){ /* ChkBehPipe */
			a = 0x01;                            /* walk right */
			if(!Player_SprAttrib) goto Auto;
			enter_side_pipe();                   /* IntroEntr */
			if(--ChangeAreaTimer) return;
			DisableIntermediate++;
			next_area();
			return;
		}
		goto PlayerRdy;
	Auto:
		auto_ctrl(a);
		return;
	}
PlayerRdy:
	GameEngineSubroutine = 0x08;
	PlayerFacingDir = 0x01;
	AltEntranceControl = 0;
	DisableCollisionDet = 0;
	JoypadOverride = 0;
}

/* SetEntr: enters the next area by entry a (2: from below) */
NOINLINE void set_entr(u8 a){
	AltEntranceControl = a;
	chg_area_mode();
}

/* Vine_AutoClimb: climbs up the vine until above the status bar, then changes area */
static void Vine_AutoClimb_(void){
	if(!Player_Y_HighPos && Player_Y_Position < 0xe4){
		set_entr(0x02);
		return;
	}
	JoypadOverride = Up_Dir;                     /* AutoClimb */
	Player_State = 0x03;
	auto_ctrl(Up_Dir);
}

/* ChgAreaPipe: when the change-area timer expires, sets the entry mode and changes area */
static void chg_area_pipe(u8 y){
	if(--ChangeAreaTimer) return;
	set_entr(y);
}

/* VerticalPipeEntry: sinks into a pipe; entry mode 0 warp zone, 1, or 2 into a castle */
static void VerticalPipeEntry_(void){
	move_y(0x01);
	ScrollHandler_();
	u8 y = 0;
	if(!WarpZoneControl){
		y = 1;
		if(AreaType == 0x03) y = 2;
	}
	chg_area_pipe(y);
}

/* SideExitPipeEntry: walks into a side pipe, entry mode 2 */
static void SideExitPipeEntry_(void){
	enter_side_pipe();
	chg_area_pipe(0x02);
}

/* InitChangeSize: starts growing or shrinking, inverting the player's size */
static void init_change_size(void){
	if(PlayerChangeSizeFlag) return;
	PlayerAnimCtrl = 0;
	PlayerChangeSizeFlag++;
	PlayerSize ^= 0x01;
}

/* DonePlayerTask: timers running again, back to the player control routine */
static void done_player_task(void){
	TimerControl = 0;
	GameEngineSubroutine = 0x08;
}

/* PlayerChangeSize: starts the size change at $f8, ends the task at $c4 */
static void PlayerChangeSize_(void){
	u8 t = TimerControl;
	if(t == 0xf8) init_change_size();
	else if(t == 0xc4) done_player_task();
}

/* PlayerInjuryBlink: shrinks at $f0, then runs the player until $c8 */
static void PlayerInjuryBlink_(void){
	u8 a = TimerControl;
	if(a >= 0xf0){
		if(a == 0xf0) init_change_size();        /* ExitBlink's bne falls through at $f0 */
		return;
	}
	if(a == 0xc8){
		done_player_task();
		return;
	}
	PlayerCtrlRoutine_();
}

/* PlayerDeath: frozen until the timer control drops under $f0, then falls */
static void PlayerDeath_(void){
	if(TimerControl >= 0xf0) return;
	PlayerCtrlRoutine_();
}

/* PlayerFireFlower: cycles the palette every 4 frames until $c0 */
static void PlayerFireFlower_(void){
	if(TimerControl == 0xc0){                    /* ResetPalFireFlower */
		done_player_task();
		reset_pal_star();
		return;
	}
	cycle_pal(FrameCounter >> 2);
}

/* FlagpoleSlide: slides down the pole while the flag object exists */
static void FlagpoleSlide_(void){
	if(Enemy_ID[5] != FlagpoleFlagObject){
		GameEngineSubroutine++;                  /* NoFPObj */
		return;
	}
	Square1SoundQueue = FlagpoleSoundQueue;
	FlagpoleSoundQueue = 0;
	auto_ctrl((Player_Y_Position < 0x9e) ? Down_Dir : 0);   /* climb down until the bottom */
}

/* PlayerEndLevel: walks to the castle; when the star flag is done, the next area */
static void PlayerEndLevel_(void){
	auto_ctrl(Right_Dir);
	if(Player_Y_Position >= 0xae && ScrollLock){
		EventMusicQueue = EndOfLevelMusic;
		ScrollLock = 0;
	}
	if(!(Player_CollisionBits & 0x01)){          /* ChkStop */
		if(!StarFlagTaskControl) StarFlagTaskControl = 1;
		Player_SprAttrib = 0x20;                 /* InCastle: behind the background */
	}
	if(StarFlagTaskControl != 0x05) return;      /* RdyNextA */
	if(++LevelNumber == 0x03 && CoinTallyFor1Ups >= RD(Hidden1UpCoinAmts, WorldNumber))
		Hidden1UpFlag++;
	next_area();
}

/* NextArea: the next area, new game timer, back to the start of the area */
static void next_area(void){
	AreaNumber++;
	load_area();
	HalfwayPage = 0;
	EventMusicQueue = Silence;
	chg_area_mode();
}

/* GameRoutines' table, by GameEngineSubroutine */
static const fn_t GameRoutinesTbl[13] PROGMEM = {
	Entrance_GameTimerSetup_, Vine_AutoClimb_, SideExitPipeEntry_, VerticalPipeEntry_,
	FlagpoleSlide_, PlayerEndLevel_, PlayerLoseLife_, PlayerEntrance_, PlayerCtrlRoutine_,
	PlayerChangeSize_, PlayerInjuryBlink_, PlayerDeath_, PlayerFireFlower_
};

/* ---------------------------------------------------------------------------------------
** Movement (5874-6268)
** ------------------------------------------------------------------------------------ */

/* GetPlayerAnimSpeed: the animation timer setting by speed; skidding stops a slow player */
static void get_player_anim_speed(void){
	u8 *q = OPZ(&SavedJoypadBits);
	u8 t = 0x02;                                 /* PlayerAnimTmrData: 2 running, 4, 7 slow */
	u8 a = Q(Player_XSpeedAbsolute);
	if(a >= 0x1c){
		Q(RunningSpeed) = a;                     /* SetRunSpd */
	}else{
		t = 0x04;
		if(a < 0x0e) t = 0x07;
		u8 j = Q(SavedJoypadBits) & 0x7f;        /* ChkSkid */
		if(j){
			if((j & 0x03) == Player_MovingDir){
				Q(RunningSpeed) = 0;
			}else if(a < 0x0b){                  /* ProcSkid */
				Player_MovingDir = PlayerFacingDir;
				Player_X_Speed = 0;
				Q(Player_X_MoveForce) = 0;
			}
		}
	}
	Q(PlayerAnimTimerSet) = t;                   /* SetAnimSpd */
}

/* sets the facing direction from the left/right bits, if any; returns them */
static u8 face_lr(void){
	u8 lr = Left_Right_Buttons;
	if(lr) PlayerFacingDir = lr;
	return lr;
}

/* MovePlayerHorizontally, the move kept for the scroll */
INLINE void move_h(void){
	MovePlayerHorizontally();
	Player_X_Scroll = A_;
}

/* OnGroundStateSub: walking and running */
static void OnGroundStateSub_(void){
	get_player_anim_speed();
	impose_friction(face_lr());                  /* GndMove */
	move_h();
}

/* FallingSub: the falling force, then the air movement */
static void FallingSub_(void){
	VerticalForce = VerticalForceDown;
	lr_air();
}

/* JumpSwimSub: rising (the jump is cut when A is let go) or swimming */
static void JumpSwimSub_(void){
	if(Player_Y_Speed >= 0
	   || (!(A_B_Buttons & A_Button & PreviousA_B_Buttons)
	       && (u8)(JumpOrigin_Y_Position - Player_Y_Position) >= DiffToHaltJump))
		VerticalForce = VerticalForceDown;       /* DumpFall */
	if(SwimmingFlag){                            /* ProcSwim */
		get_player_anim_speed();
		if(Player_Y_Position < 0x14) VerticalForce = 0x18;
		face_lr();                               /* LRWater */
	}
	lr_air();
}

/* LRAir: horizontal movement in the air, then vertical */
static void lr_air(void){
	u8 lr = Left_Right_Buttons;
	if(lr) impose_friction(lr);
	move_h();                                    /* JSMove */
	if(GameEngineSubroutine == 0x0b) VerticalForce = 0x28;
	MovePlayerVertically();                      /* ExitMov1 */
}

/* ClimbingSub: moves on the vine; left/right against it pushes off to the other side */
static void ClimbingSub_(void){
	u16 s = (u16)Player_YMF_Dummy + Player_Y_MoveForce;
	Player_YMF_Dummy = (u8)s;
	s8 v = Player_Y_Speed;
	M00 = (u8)(v >> 7);                          /* the high byte's adder */
	u16 p = (((u16)Player_Y_HighPos << 8) | Player_Y_Position) + v + (u8)(s >> 8);   /* MoveOnVine */
	Player_Y_Position = (u8)p;
	Player_Y_HighPos = p >> 8;
	u8 lr = Left_Right_Buttons;
	u8 a = lr & Player_CollisionBits;
	if(!a){
		ClimbSideTimer = 0;                      /* InitCSTimer */
		return;
	}
	if(ClimbSideTimer) return;
	ClimbSideTimer = 0x18;
	u8 x = 0;
	if(!(a & Right_Dir)) x += 2;
	if(PlayerFacingDir != 1) x++;                /* ClimbFD */
	p = (((u16)Player_PageLoc << 8) | Player_X_Position) + (s8)RD(ClimbAdderLow, x);   /* CSetFDir */
	Player_X_Position = (u8)p;
	Player_PageLoc = p >> 8;
	PlayerFacingDir = lr ^ 0x03;
}

static const fn_t MoveSubsTbl[4] PROGMEM = {
	OnGroundStateSub_, JumpSwimSub_, FallingSub_, ClimbingSub_
};

/* PlayerPhysicsSub: climbing speeds, or the jump/swim start, then X_Physics */
INLINE void player_physics_sub(u8 *q){
	u8 st = Player_State;
	if(st == 0x03){
		/* Climb_Y_MForceData, Climb_Y_SpeedData: none 0 0, up $20 -1, down $ff 1 */
		u8 mf = 0, sp = 0, t = 0x04;
		u8 a = Up_Down_Buttons & Player_CollisionBits;
		if(a){
			mf = 0xff;
			sp = 0x01;
			if(a & Up_Dir){
				mf = 0x20;
				sp = 0xff;
				t = 0x08;
			}
		}
		Player_Y_MoveForce = mf;                 /* ProcClimb */
		Player_Y_Speed = (s8)sp;
		Q(PlayerAnimTimerSet) = t;                  /* SetCAnim: 8 climbing up, else 4 */
		return;
	}
	/* CheckForJumping: A pressed this frame and not the last */
	if(Q(JumpspringAnimCtrl) || !(A_B_Buttons & A_Button) || (A_Button & PreviousA_B_Buttons))
		goto X_Phys;
	u8 sw = Q(SwimmingFlag);
	if(st){                                      /* ProcJumping */
		if(!sw || (!JumpSwimTimer && Player_Y_Speed < 0))
			goto X_Phys;                         /* no midair jump; swim only when sinking */
	}
	JumpSwimTimer = 0x20;                        /* InitJS */
	Player_YMF_Dummy = 0;
	Q(JumpOrigin_Y_HighPos) = Player_Y_HighPos;
	Q(JumpOrigin_Y_Position) = Player_Y_Position;
	Player_State = 0x01;
	u8 y = 0;
	u8 a = Q(Player_XSpeedAbsolute);
	if(a >= 0x09){
		y++;
		if(a >= 0x10){
			y++;
			if(a >= 0x19){
				y++;
				if(a >= 0x1c) y++;
			}
		}
	}
	Q(DiffToHaltJump) = 0x01;                       /* ChkWtr */
	u8 snd = Sfx_EnemyStomp;
	if(sw){
		y = 0x05;
		if(Whirlpool_Flag) y++;
	}else{
		snd = PlayerSize ? Sfx_SmallJump : Sfx_BigJump;   /* PJumpSnd */
	}
	const u8 *t = JumpPhysData + y;              /* GetYPhy */
	Q(VerticalForce) = RD(t, 0);
	Q(VerticalForceDown) = RD(t, 7);
	Player_Y_MoveForce = (y == 0x05) ? 0x80 : 0;
	Player_Y_Speed = (s8)RD(t, 14);
	Square1SoundQueue = snd;
	if(sw && Player_Y_Position < 0x14) Player_Y_Speed = 0;    /* not above the water */
X_Phys:
	x_physics(q);
}

/* X_Physics: the maximum speeds and the friction for the player's state and speed */
INLINE void x_physics(u8 *q){
	u8 y = 0, m = 0;
	u8 abs = Q(Player_XSpeedAbsolute);
	if(Player_State){
		if(abs >= 0x19) goto GetXPhy;
	}else{                                       /* ProcPRun */
		y = 1;
		if(AreaType){
			y = 0;
			if(Left_Right_Buttons == Player_MovingDir){
				if(A_B_Buttons & B_Button){
					RunningTimer = 0x0a;         /* SetRTmr */
					goto GetXPhy;
				}
				if(RunningTimer) goto GetXPhy;
			}
		}
	}
	y++;                                         /* ChkRFast */
	m++;
	if(Q(RunningSpeed) || abs >= 0x21) m++;         /* FastXSp */
GetXPhy:
	M00 = m;
	u8 r = RD(MaxRightXSpdData, y);
	MaximumLeftSpeed = -(s8)r;
	if(GameEngineSubroutine == 0x07) r = 0x0c;
	MaximumRightSpeed = (s8)r;
	u8 lo = RD(FrictionData, m), hi = 0;
	if(PlayerFacingDir != Player_MovingDir){     /* turning: double friction */
		hi = lo >> 7;
		lo <<= 1;
	}
	Q(FrictionAdderLow) = lo;
	Q(FrictionAdderHigh) = hi;
}

/* ImposeFriction: a the left/right bits; accelerates or slows the player to the max speeds */
static void impose_friction(u8 a){
	u8 *q = OPZ(&SavedJoypadBits);
	a &= Player_CollisionBits;
	u8 sp = (u8)Player_X_Speed;
	bool right;                                  /* true: RghtFrict (subtracts) */
	if(a){
		right = !(a & 0x01);                     /* JoypFrict: left pressed */
	}else{
		if(!sp){
			Q(Player_XSpeedAbsolute) = 0;        /* SetAbsSpd */
			return;
		}
		right = !(sp & 0x80);
	}
	/* the speed and its fraction as one 16-bit value: the friction added or subtracted */
	u16 v = ((u16)sp << 8) | Q(Player_X_MoveForce);
	u16 f = ((u16)Q(FrictionAdderHigh) << 8) | Q(FrictionAdderLow);
	u8 m;
	if(!right){                                  /* LeftFrict */
		v += f;
		m = (u8)MaximumRightSpeed;               /* straight to SetAbsSpd: m is positive */
	}else{                                       /* RghtFrict */
		v -= f;
		m = (u8)MaximumLeftSpeed;
	}
	sp = v >> 8;
	if(cmp_neg(sp, m) == right) sp = m;
	Q(Player_X_MoveForce) = (u8)v;
	Player_X_Speed = (s8)sp;
	if(sp & 0x80) sp = -sp;                      /* XSpdSign */
	Q(Player_XSpeedAbsolute) = sp;               /* SetAbsSpd */
}

/* PlayerCtrlRoutine: the controls, movement, scrolling, collisions, and falling into a hole */
static void PlayerCtrlRoutine_(void){
	u8 *q = OPQ(&SavedJoypadBits);
	if(GameEngineSubroutine != 0x0b){
		u8 j = Q(SavedJoypadBits);
		if(!AreaType && (Player_Y_HighPos != 1 || Player_Y_Position >= 0xd0))
			Q(SavedJoypadBits) = j = 0;             /* water: no control out of the playfield */
		A_B_Buttons = j & 0xc0;                  /* SaveJoyp */
		u8 lr = j & 0x03, ud = j & 0x0c;
		if((ud & Down_Dir) && !Player_State && lr) lr = ud = 0;   /* down on the ground */
		Left_Right_Buttons = lr;
		Up_Down_Buttons = ud;
	}
	/* PlayerMovementSubs: crouching, physics, then the routine of the player's state */
	if(PlayerSize){
		Q(CrouchingFlag) = 0;
	}else if(!Player_State){
		Q(CrouchingFlag) = Up_Down_Buttons & Down_Dir;
	}
	player_physics_sub(q);                        /* ProcMove */
	if(!Q(PlayerChangeSizeFlag)){
		u8 s = Player_State;
		if(s != 0x03) ClimbSideTimer = 0x18;
		jump_engine(MoveSubsTbl, s, 4);          /* MoveSubs */
	}
	/* SizeChk: 1 big, else 2 crouching (CrouchingFlag is 0 or 4), 0 */
	Player_BoundBoxCtrl = PlayerSize ? 0x01 : Q(CrouchingFlag) >> 1;   /* ChkMoveDir */
	s8 sp = Player_X_Speed;
	if(sp) Player_MovingDir = (sp < 0) ? 0x02 : 0x01;
	ScrollHandler_();                            /* PlayerSubs */
	GetPlayerOffscreenBits();
	RelativePlayerPosition();
	X_ = 0;                                      /* Y_ 0: left by RelativePlayerPosition */
	BoundingBoxCore();
	PlayerBGCollision();
	if(Player_Y_Position >= 0x40){
		u8 g = GameEngineSubroutine;
		if(g != 0x05 && g != 0x07 && g >= 0x04) Player_SprAttrib &= 0xdf;   /* in front of bg */
	}
	u8 hp = Player_Y_HighPos;                    /* PlayerHole */
	if(cmp_neg(hp, 0x02)) return;
	Q(ScrollLock) = 0x01;
	u8 m7 = 0x04;
	bool die = GameTimerExpiredFlag || !CloudTypeOverride;   /* cloud level: exit */
	if(die && GameEngineSubroutine != 0x0b){     /* HoleDie */
		if(!Q(DeathMusicLoaded)){
			EventMusicQueue = DeathMusic;
			Q(DeathMusicLoaded) = 0x01;
		}
		m7 = 0x06;                               /* HoleBottom */
	}
	M07 = m7;
	if(cmp_neg(hp, m7)) return;                  /* ChkHoleX */
	if(!die){                                    /* CloudExit: SetEntr, then entry 3 */
		JoypadOverride = 0;
		set_entr(0x03);
		return;
	}
	if(EventMusicBuffer) return;
	GameEngineSubroutine = 0x06;
}

/* ---------------------------------------------------------------------------------------
** The player's graphics offset (14585-14760)
** ------------------------------------------------------------------------------------ */

/* GetOffsetFromAnimCtrl: frame a x 8 plus the action's offset (the third asl's carry is 0:
** a is under 10) */
NOINLINE u8 GetOffsetFromAnimCtrl_(u8 a, u8 y){
	return (u8)(a << 3) + RD(PlayerGfxTblOffsets, y);
}

/* The offset of action y (GetGfxOffsetAdder: the small player's entries are eight further):
** e 0 NonAnimatedActs, one frame, the animation reset; 1 GetCurrentAnimOffset, the current
** frame; 2-3 AnimationControl, the current frame, advanced when the timer expires (e frames) */
INLINE u8 player_action(u8 *q, u8 y, u8 e){
	if(!e) Q(PlayerAnimCtrl) = 0;
	u8 c = Q(PlayerAnimCtrl);
	if(PlayerSize) y += 0x08;
	if(e >= 0x02){
		M00 = e;
		if(!PlayerAnimTimer){
			PlayerAnimTimer = Q(PlayerAnimTimerSet);
			u8 n = c + 1;
			if(n >= e) n = 0;
			Q(PlayerAnimCtrl) = n;               /* SetAnimC */
		}
	}
	return GetOffsetFromAnimCtrl_(c, y);         /* the frame before the advance; ExAnimC */
}

/* ProcessPlayerAction: the graphics table offset for the player's action, in A_ */
void ProcessPlayerAction(void){
	u8 *q = OPQ(&SavedJoypadBits);
	u8 y, e = 0;
	switch(Player_State){
	case 0x03:                                   /* ActionClimbing */
		y = 0x05;
		if(Player_Y_Speed) e = 0x02;             /* ThreeFrameExtent */
		break;
	case 0x02:                                   /* ActionFalling */
		y = 0x04;
		e = 0x01;
		break;
	case 0x01:
		if(Q(SwimmingFlag)){                     /* ActionSwimming */
			y = 0x01;
			e = 0x01;
			if((JumpSwimTimer | Q(PlayerAnimCtrl)) || (A_B_Buttons & A_Button)) e = 0x03;   /* FourFrameExtent */
		}else{
			y = Q(CrouchingFlag) ? 0x06 : 0x00;  /* crouching or jumping */
		}
		break;
	default:                                     /* ProcOnGroundActs */
		if(Q(CrouchingFlag)){
			y = 0x06;
		}else if(!((u8)Player_X_Speed | Left_Right_Buttons)){
			y = 0x02;                            /* standing */
		}else if(Q(Player_XSpeedAbsolute) < 0x09 || (Player_MovingDir & PlayerFacingDir)){
			y = 0x04;                            /* ActionWalkRun */
			e = 0x03;
		}else{
			y = 0x03;                            /* skidding */
		}
		break;
	}
	A_ = player_action(q, y, e);
}

/* HandleChangeSize: the graphics table offset while growing or shrinking, in A_; the frame
** advances every fourth frame, and the size change ends after ten */
void HandleChangeSize(void){
	u8 y = PlayerAnimCtrl;
	if(!(FrameCounter & 0x03)){
		if(++y >= 0x0a){
			y = 0;
			PlayerChangeSizeFlag = 0;
		}
		PlayerAnimCtrl = y;                      /* CSzNext */
	}
	if(PlayerSize){                              /* ShrinkPlayer: two of the swimming frames */
		/* ShrPlF: PlayerGfxTblOffsets[9] on even frames, [1] on odd (PlayerAnimCtrl < 10) */
		A_ = (y & 0x01) ? 0x28 : 0x88;
		return;
	}
	A_ = GetOffsetFromAnimCtrl_(RD(ChangeSizeOffsetAdder, y), 0x0f);   /* growing */
}

/* ChkForPlayerAttrib: which of the player's bottom sprite rows must be drawn unflipped with
** their right sprite mirrored (the 6502 rewrote those OAM attributes). A_: 0 none, 1 the
** fourth row (crouching, small standing, growing), 2 the third and the fourth (killed, and
** big standing: the 6502's cmp #$c8 falls through into KilledAtt) */
void ChkForPlayerAttrib(void){
	u8 a = 0;
	if(GameEngineSubroutine == 0x0b){
		a = 2;                                   /* KilledAtt */
	}else{
		u8 o = PlayerGfxOffset;
		if(o == 0xc8) a = 2;                     /* big, standing: falls into KilledAtt */
		else if(o == 0x50 || o == 0xb8 || o == 0xc0) a = 1;   /* C_S_IGAtt */
	}
	A_ = a;
}

/* ---------------------------------------------------------------------------------------
** Positions relative to the screen (14761-14820)
** ------------------------------------------------------------------------------------ */

/* GetProperObjOffset's ObjOffsetData is OFS_FIREBALL, OFS_BUBBLE, OFS_MISC here */

/* GetObjRelativePosition + RelWOfs: the SprObject x's coordinates relative to the screen, in
** slot y; X_ back to ObjectOffset */
static void RelWOfs_(u8 x, u8 y){
	u8 *o = (u8 *)&ra + RA_OFS(SprObject_X_Position_) + x;
	u8 *r = (u8 *)&ra + RA_OFS(SprObject_Rel_XPos_) + y;
	r[RA_OFS(SprObject_Rel_YPos_) - RA_OFS(SprObject_Rel_XPos_)] =
		o[RA_OFS(SprObject_Y_Position_) - RA_OFS(SprObject_X_Position_)];
	A_ = r[0] = o[0] - ScreenLeft_X_Pos;
	X_ = ObjectOffset;
	Y_ = y;
}

/* X_ + ofs the SprObject */
static void rel_x(u8 ofs, u8 y){
	RelWOfs_(X_ + ofs, y);
}

void RelativePlayerPosition(void){
	RelWOfs_(0, REL_PLAYER);
}

/* X_: the bubble */
void RelativeBubblePosition(void){
	rel_x(OFS_BUBBLE, REL_BUBBLE);
}

/* X_: the fireball; both fireballs share one relative slot */
void RelativeFireballPosition(void){
	rel_x(OFS_FIREBALL, REL_FBALL);
}

/* X_: the misc object; all share one relative slot */
void RelativeMiscPosition(void){
	rel_x(OFS_MISC, REL_MISC);
}

/* X_: the enemy (VariableObjOfsRelPos keeps X_ in $00) */
void RelativeEnemyPosition(void){
	M00 = X_;
	rel_x(OFS_ENEMY, REL_ENEMY);
}

/* X_: the block object; its second half (the brick chunks') is two SprObject slots on */
void RelativeBlockPosition(void){
	M00 = X_;
	rel_x(OFS_BLOCK, REL_BLOCK);
	M00 = X_ += 2;                               /* X_ is ObjectOffset again */
	rel_x(OFS_BLOCK, REL_BLOCK + 1);
}

/* ---------------------------------------------------------------------------------------
** Offscreen bits (14821-15000)
** ------------------------------------------------------------------------------------ */

/* GetXOffscreenBits / GetYOffscreenBits: x the SprObject; the bits: d7-d0 how much of the
** object (in eighths) is past the right edge (bottom), or the left edge (top) when nothing is
** past it. DividePDiff: within $06 pixels of the edge, the offset from the distance / 8 (plus
** $05 on the left side or the top). Leaves A_ X_ Y_ as the 6502 */
static u8 offscr(u8 x, bool vert){
	M04 = x;
	_Static_assert(RA_OFS(SprObject_Y_Position_) - RA_OFS(SprObject_Y_HighPos_)
	               == RA_OFS(SprObject_X_Position_) - RA_OFS(SprObject_PageLoc_), "layout");
	const u8 *t = OffscrXData;
	u8 *o = (u8 *)&ra + RA_OFS(SprObject_PageLoc_) + x;   /* the page, then the position */
	if(vert){
		t = OffscrYData;
		o = (u8 *)&ra + RA_OFS(SprObject_Y_HighPos_) + x;
	}
	u8 ph = o[0], p = o[RA_OFS(SprObject_X_Position_) - RA_OFS(SprObject_PageLoc_)];
	u8 y = 1, a;
	do{
		u8 e = y - 1, eh = 0x01;                 /* HighPosUnitData: $ff, $00 */
		if(!vert){
			u8 *ed = (u8 *)&ra + RA_OFS(ScreenLeft_PageLoc__) + y;
			eh = ed[0];
			e = ed[RA_OFS(ScreenLeft_X_Pos__) - RA_OFS(ScreenLeft_PageLoc__)];
		}
		u8 d = e - p;
		M07 = d;
		u8 page = eh - ph - (e < p);
		u8 xi = RD(t + y, (page & 0x80) ? 2 : 3);
		if(!page){                               /* same page: within the edge's range */
			u8 m6 = RD(t, 0), m5 = RD(t, 1);
			M06 = m6;
			M05 = m5;
			if(d < m6){
				xi = d >> 3;
				if(!y) xi += m5;
			}
		}
		a = RD(t + 5, xi);                       /* X/YLdBData */
	}while(!a && (s8)--y >= 0);
	A_ = a;
	X_ = x;
	Y_ = y;
	return a;
}

/* GetXOffscreenBits: X_ the SprObject */
void GetXOffscreenBits(void){
	offscr(X_, false);
}

/* GetOffScreenBitsSet: x the SprObject, y the offscreen-bits slot: vertical bits in the
** high nybble, horizontal in the low */
static void GetOffScreenBitsSet_(u8 x, u8 y){
	u8 h = offscr(x, false) >> 4;                /* RunOffscrBitsSubs */
	M00 = h;
	h |= (u8)(offscr(x, true) << 4);
	M00 = h;
	SprObject_OffscrBits[y] = h;
	A_ = h;
	Y_ = y;
	X_ = ObjectOffset;
}

static void offscr_x(u8 ofs, u8 y){
	GetOffScreenBitsSet_(X_ + ofs, y);
}

void GetPlayerOffscreenBits(void){
	GetOffScreenBitsSet_(0, REL_PLAYER);
}

/* X_: the fireball */
void GetFireballOffscreenBits(void){
	offscr_x(OFS_FIREBALL, REL_FBALL);
}

/* X_: the bubble */
void GetBubbleOffscreenBits(void){
	offscr_x(OFS_BUBBLE, REL_BUBBLE);
}

/* X_: the misc object */
void GetMiscOffscreenBits(void){
	offscr_x(OFS_MISC, REL_MISC);
}

/* X_: the enemy (SetOffscrBitsOffset's $00 is overwritten) */
void GetEnemyOffscreenBits(void){
	offscr_x(OFS_ENEMY, REL_ENEMY);
}

/* X_: the block object */
void GetBlockOffscreenBits(void){
	offscr_x(OFS_BLOCK, REL_BLOCK);
}
