/*
 * The frame and the modes: a port of smbdis.asm's NMI body (lines 739-848, without the PPU),
 * OperModeExecutionTree and the title, demo and victory modes (929-1258), the screen routines
 * (1361-1634), the area parser's task handler (3035-3058), PauseRoutine, ReadJoypads and
 * UpdateTopScore. Where the 6502 wrote the PPU, the platform layer (gfx17.c, text.c) draws.
 */
#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include "smb.h"
#include "apu.h"
#include "romtables.h"   /* the cartridge's tables, from your ROM (tools/gen_tables.py) */

/* ---------------------------------------------------------------------------------------
** Controllers: the Uzebox pad (SNES layout) as the NES's bits, by name: A jumps and B runs
** and throws fireballs, as on the NES's pad (Y and X do nothing)
** ------------------------------------------------------------------------------------ */
static u8 JoypadBitMask_;

_Static_assert(offsetof(struct RamPersist, CoinDisplay_) == offsetof(struct RamPersist, PlayerScoreDisplay_) + 6,
	"game_menu_routine: the coins follow the score");

_Static_assert(BTN_B == 1 && BTN_Y == 2 && BTN_SELECT == 4 && BTN_START == 8 && BTN_UP == 16 &&
	BTN_DOWN == 32 && BTN_LEFT == 64 && BTN_RIGHT == 128 && BTN_A == 256, "read_joypads: the SNES layout");

#if TWO_PLAYERS
/* A pad's buttons as the NES's bits: A, B, then Select to Right (Y dropped): bits 7-0 */
static __attribute__((noinline)) u8 pad_bits(u8 n){
	u16 b = ReadJoypad(n);
	u8 lo = (u8)b, a = (u8)(b >> 8);
	/* what is in the second port is a pad only if the last four of its 16 bits are clear, as
	** a pad's are (the kernel takes the last one for a mouse's): a keyboard's dongle there
	** holds bits down, and the title, which wants Start alone of both pads' bits, was dead
	** to the first pad with one (the user, 2026-10-13) */
	if(n && (a & 0xf0)) return 0;
	a = (u8)(a << 1) | (lo & 1);
	lo >>= 2;
	for(u8 i = 6; i; i--){
		a = (u8)(a << 1) | (lo & 1);
		lo >>= 1;
	}
	return a;
}

/* ... with select and start reported once a press (the 6502's JoypadBitMask,x) */
static u8 JoypadBitMask2_;
static __attribute__((noinline)) u8 pad_once(u8 n, u8 *mask){
	u8 a = pad_bits(n);
	if((a & 0x30) & *mask) a &= 0xcf;
	else *mask = a;
	return a;
}

/* ReadJoypads: the first pad. Returns the bits */
static u8 read_joypads(void){
	u8 a = pad_once(0, &JoypadBitMask_);
	SavedJoypadBits = a;
	return a;
}

/* The menus take either pad: the title's (GameMenuRoutine) and the wait for B after the last
** castle read SavedJoypad1Bits ORed with SavedJoypad2Bits. The second pad is read here, where
** a menu asks, a frame of it each */
static u8 either_pad(void){
	return SavedJoypadBits | pad_once(1, &JoypadBitMask2_);
}

/* ... and the second pad, the second player's while his turn lasts (player.c's
** GameCoreRoutine: the 6502's SavedJoypadBits,x). Its select and start are not passed on: the
** pause and the menus read the first pad's, as the cartridge's do, and nothing of a turn
** reads them */
u8 game_Pad2(void){
	return pad_bits(1) & 0xcf;
}
#else
/* ReadJoypads: player 1 only; select and start are reported once a press. Returns the bits */
static u8 read_joypads(void){
	u16 b = ReadJoypad(0);
	u8 lo = (u8)b, a = (u8)(b >> 8);          /* A, B, then Select to Right (Y dropped): bits 7-0 */
	a = (u8)(a << 1) | (lo & 1);
	lo >>= 2;
	for(u8 i = 6; i; i--){
		a = (u8)(a << 1) | (lo & 1);
		lo >>= 1;
	}
	if((a & 0x30) & JoypadBitMask_) a &= 0xcf;
	else JoypadBitMask_ = a;
	SavedJoypadBits = a;
	return a;
}
#endif

/* ---------------------------------------------------------------------------------------
** PauseRoutine, UpdateTopScore
** ------------------------------------------------------------------------------------ */
/* PauseRoutine: joy is SavedJoypadBits */
static void pause_routine(u8 joy){
	if(OperMode != VictoryModeValue){
		if(OperMode != GameModeValue || OperMode_Task != 3) return;
	}
	if(GamePauseTimer){
		GamePauseTimer--;
		return;
	}
	if(joy & Start_Button){
		if(GamePauseStatus & 0x80) return;
		GamePauseTimer = 0x2b;
		PauseSoundQueue = GamePauseStatus + 1;
		GamePauseStatus = (GamePauseStatus ^ 1) | 0x80;
	}else{
		GamePauseStatus &= 0x7f;
	}
}

/* ---------------------------------------------------------------------------------------
** The VRAM buffer address control: the 6502's NMI uploaded one of these each frame
** ------------------------------------------------------------------------------------ */
/* UpdateScreen */
static void update_screen(void){
	gfx_FrameStart();
	u8 c = VRAM_Buffer_AddrCtrl;
	VRAM_Buffer_AddrCtrl = 0;
	switch(c){
	case 1: case 2: case 3: case 4:
		gfx_LoadPalette(c - 1);               /* water, ground, underground, castle */
		break;
	case 5:
		gfx_DrawTitleScreen();
		break;
	case 8: BowserPaletteRequest(); break;
	case 9: case 10: case 11:
		gfx_LoadAltPalette(c);                /* day snow, night snow, mushroom */
		break;
	case 12: case 13: case 14: case 15: case 16: case 17: case 18:
		gfx_Message(c);                       /* the victory messages, the world select */
		break;
	}
}

/* ---------------------------------------------------------------------------------------
** The title screen and the demo
** ------------------------------------------------------------------------------------ */
/* ATTRACT=0: the title is static: no demo (the 6502's DemoEngine and its data are not
** built), and the game's core does not run under it; the area is still set up, as the 6502
** did, and shown when the game starts */
static const u8 DemoActionData[] PROGMEM = { ROM_DemoActionData };
static const u8 DemoTimingData[] PROGMEM = { ROM_DemoTimingData };

/* DemoEngine: the demo's controller; true when it is over */
static bool demo_engine(void){
	u8 x = DemoAction;
	if(!DemoActionTimer){
		x++;
		DemoAction++;
		DemoActionTimer = pgm_read_byte(&DemoTimingData[x - 1]);
		if(!DemoActionTimer) return true;
	}
	SavedJoypadBits = pgm_read_byte(&DemoActionData[x - 1]);
	DemoActionTimer--;
	return false;
}

/* ResetTitle: the title again, from its first task */
static void reset_title(void){
	OperMode = 0;
	OperMode_Task = 0;
	Sprite0HitDetectFlag = 0;
	DisableScreenFlag++;
}

static void go_continue(u8 w){
	WorldNumber = w;
	AreaNumber = 0;
}

/* the game mode from its first task, in the area of WorldNumber and AreaNumber (LoadAreaPointer
** reads only those two, so the stores around it may come in any order) */
static void start_area(void){
	LoadAreaPointer();
	FetchNewGameTimerFlag++;
	OperMode = GameModeValue;
	OperMode_Task = 0;
}

/* The world select: B on the title selects the world, as after the game is beaten,
** without the second quest's hard mode that WorldSelectEnableFlag also starts; and A selects
** the level of that world (the bar shows both), which the cartridge has no way to */
	/* Neither is built in: a code of
	** buttons on the title gives the select, its level key and 99 lives. Its state is bit 1 of
	** WorldSelectEnableFlag (bit 0 is the 6502's: the game beaten, which also starts the hard
	** mode); the code stores 2 and the game's end 1, the last one wins. The level's key works
	** with either */
	#define CODE_ON      (WorldSelectEnableFlag & 2)
	#define LEVEL_KEY(a) (WorldSelectEnableFlag && (a) == A_Button)
	/* (the 0 after it: no button is the code's eleventh) */
	static const u8 Code[] PROGMEM = {
		Up_Dir, Up_Dir, Down_Dir, Down_Dir, Left_Dir, Right_Dir, Left_Dir, Right_Dir, B_Button, A_Button, 0
	};
	#define CODE_N (sizeof Code - 1)
	static u8 code_at, code_last;
	/* a frame's buttons: a press that is the code's next goes on, any other starts it again;
	** the code is in when its last button is let go (pressed, it is not yet the level's key) */
	static void code(u8 a){
		u8 last = code_last;
		code_last = a;
		if(a == last) return;
		u8 n = code_at;
		if(a){
			u8 want = pgm_read_byte(&Code[n]);
			n++;
			if(a != want) n = 0;
		}else{
			if(n != CODE_N) return;
			n = 0;
			WorldSelectEnableFlag = 2;
			/* the power-up's sound: queued, and the effect's buffer marked, which is what makes
			** the sound engine run on the title, silent otherwise (sound.c's SoundEngine). Not
			** the coin's: the title's set-up leaves a silence queued for the music, which zeroes
			** the pulse's volume on the engine's first frame, and of square 2's effects only
			** those that write their volume again as they go are heard after it */
			apu.s.Square2SoundBuffer = Square2SoundQueue = Sfx_PowerUpGrab;
		}
		code_at = n;
	}

/* GameMenuRoutine */
static void game_menu_routine(void){
#if TWO_PLAYERS
	u8 a = either_pad();
#else
	u8 a = SavedJoypadBits;
#endif
	bool b_sel = false;
	code(a);
	if(a == Start_Button || a == (A_Button | Start_Button)){
		/* ChkContinue */
		if(!DemoTimer){                       /* the demo is running: start ends it */
			reset_title();
			return;
		}
		if(a & A_Button) go_continue(ContinueWorld);
		Hidden1UpFlag++;
		PrimaryHardMode = WorldSelectEnableFlag & 1;
		if(CODE_ON) NumberofLives = 98;       /* (the second player's too: StartPlayers, below) */
		DemoTimer = 0;
		memset((u8 *)&rp + offsetof(struct RamPersist, PlayerScoreDisplay_), 0, 8);   /* the score, the coins */
#if TWO_PLAYERS
		StartPlayers();                       /* the second player's lives, place and score: the first's */
#endif
		start_area();                         /* OperMode++: the title's is 0 */
		return;
	}
	if(a == Select_Button){
		b_sel = true;
	}else if(DemoTimer){
		if((WorldSelectEnableFlag && a == B_Button) || LEVEL_KEY(a)) b_sel = true;
	}else{
		SelectTimer = a;
		if(demo_engine()){
			reset_title();
			return;
		}
		goto RunDemo;
	}
	if(b_sel){
		/* SelectBLogic: 1 player only, so select only resets the demo timer */
		if(!DemoTimer){
			reset_title();
			return;
		}
		DemoTimer = 0x18;
		if(!SelectTimer){
			SelectTimer = 0x10;
			if(a == B_Button){
				WorldSelectNumber = (WorldSelectNumber + 1) & 7;
				go_continue(WorldSelectNumber);
				gfx_WorldSelect(WorldNumber + 1);
				LevelNumber = gfx_bar_level = 0;
			}else if(a == A_Button){
				/* the level's area: one more from the third level on in the worlds whose second
				** level comes after the scene of the pipe (1, 2, 4 and 7), which is an area */
				u8 l = (LevelNumber + 1) & 3;
				LevelNumber = gfx_bar_level = l;
				AreaNumber = l + (l >= 2 && ((0x4b >> WorldNumber) & 1));
			}else{
#if TWO_PLAYERS
				NumberOfPlayers ^= 1;            /* select: a game of one, of two */
#endif
				gfx_TitleCursor();               /* DrawMushroomIcon */
			}
		}
	}
	SavedJoypadBits = 0;                      /* NullJoypad */
RunDemo:
	GameCoreRoutine();
	if(GameEngineSubroutine == 6) reset_title();
}

/* TitleScreenMode */
static void title_screen_mode(void){
	switch(OperMode_Task){
	case 0: InitializeGame(); break;
	case 1: ScreenRoutines(); break;
	case 2: PrimaryGameSetup(); break;
	default: game_menu_routine(); break;
	}
}

/* ---------------------------------------------------------------------------------------
** Victory mode
** ------------------------------------------------------------------------------------ */
/* SetupVictoryMode */
static void setup_victory_mode(void){
	DestinationPageLoc = ScreenRight_PageLoc + 1;
	EventMusicQueue = EndOfCastleMusic;
	OperMode_Task++;                          /* IncModeTask_B */
}

/* PlayerVictoryWalk */
static void player_victory_walk(void){
	u8 y = 0;
	if(Player_PageLoc != DestinationPageLoc || Player_X_Position < 0x60) y = 1;
	VictoryWalkControl = y;                   /* only this routine writes it */
	A_ = y;
	AutoControlPlayer();
	if(ScreenLeft_PageLoc != DestinationPageLoc){
		u8 f = ScrollFractional;
		ScrollFractional = f ^ 0x80;          /* + $80, the carry out d7 */
		Y_ = (f >> 7) + 1;
		ScrollScreen();
		UpdScrollVar();
		VictoryWalkControl = ++y;
	}
	if(!y) OperMode_Task++;
}

/* PrintVictoryMessages */
static void print_victory_messages(void){
	u8 y = PrimaryMsgCounter;
	if(!SecondaryMsgCounter && y < 9){       /* a message every 256 frames: ThankPlayer, PrintMsg */
		if(WorldNumber == World8){
			if(y == 3) EventMusicQueue = VictoryMusic;
			if(y && y < 3) goto IncMsgCounter;
		}else{
			if(y >= 4) goto EndWorld;
			if(y & 1) goto IncMsgCounter;     /* 1 and 3: nothing */
		}
		VRAM_Buffer_AddrCtrl = y + 0x0c;      /* $0c first, then world 8's $0f-$14, $0e world 1-7's */
	}
IncMsgCounter:
	{
		u8 s = SecondaryMsgCounter + 4;
		SecondaryMsgCounter = s;
		if(s < 4) PrimaryMsgCounter = ++y;
	}
	if(y >= 7){
	EndWorld:
		WorldEndTimer = 6;
		OperMode_Task++;
	}
}

/* PlayerEndWorld */
static void player_end_world(void){
	if(WorldEndTimer) return;
	if(WorldNumber >= World8){
#if TWO_PLAYERS
		if(either_pad() & B_Button){              /* (EndChkBButton: B on either pad) */
#else
		if(SavedJoypadBits & B_Button){
#endif
			WorldSelectEnableFlag = 1;
			NumberofLives = 0xff;
			TerminateGame();
		}
		return;
	}
	AreaNumber = 0;
	LevelNumber = 0;
	WorldNumber++;
	start_area();
}

/* VictoryMode */
static void victory_mode(void){
	switch(OperMode_Task){
	case 0: BridgeCollapse(); break;
	case 1: setup_victory_mode(); break;
	case 2: player_victory_walk(); break;
	case 3: print_victory_messages(); break;
	default: player_end_world(); break;
	}
	if(OperMode_Task){
		ObjectOffset = 0;
		X_ = 0;
		EnemiesAndLoopsCore();
	}
	RelativePlayerPosition();
	PlayerGfxHandler();
}

/* ---------------------------------------------------------------------------------------
** The screen routines
** ------------------------------------------------------------------------------------ */
static const u8 AreaPalette[4] PROGMEM = { ROM_AreaPalette };
static const u8 BGColorCtrl_Addr_[4] PROGMEM = { ROM_BGColorCtrl_Addr_ };

/* SetupIntermediate: the player's colours for the intermediate screen */
static __attribute__((noinline)) void setup_intermediate(void){
	u8 bg = BackgroundColorCtrl, st = PlayerStatus;
	PlayerStatus = 0;
	BackgroundColorCtrl = 2;
	GetPlayerColors();
	PlayerStatus = st;
	BackgroundColorCtrl = bg;
}

#if TOPSAVE
/* The top score, kept in the console's EEPROM (the Makefile's TOPSAVE: the .uze's build has
** it): a block of the kernel's, number TOPSAVE_ID (the wiki's "EEPROM Block ID Reservation
** List"), its first six bytes the score's digits as the game holds them. Read at power-on.
** Written at a screen's set-up where the block's digits are not the score's: the screen is
** off there (after a life lost, a level's end, a pipe, a game's end), so the score is kept
** before any player can switch the console off between two levels, and nothing is written
** while a level plays (a byte takes the EEPROM 3.4 ms and the main program waits for it:
** the digits that changed, and 8 bytes when the block is made, at the first title).
** By the kernel's EepromBlockExists, which finds the block or a free one's place, and its
** ReadEeprom and WriteEeprom: its EepromReadBlock and EepromWriteBlock would be a block of
** 32 bytes on the stack, all 32 written each time and about 150 bytes of flash more, which
** the ROM has not.
** A block whose six bytes are not digits is not this game's score (the number given to
** another program): the top score then starts at 0, and the first set-up writes it there */
#ifndef TOPSAVE_ID
	#define TOPSAVE_ID 0x5342
#endif
static void top_score(bool save){
	u16 at;
	u8 free = 0;
	u8 none = EepromBlockExists(TOPSAVE_ID, &at, &free);   /* (0: it is there, at) */
	u16 a = at;                                        /* (in registers: at is the call's, in the frame) */
	if(none){
		if(!save || !free) return;                         /* none to read; none free to make */
		a = (u16)free * EEPROM_BLOCK_SIZE;
		WriteEeprom(a, (u8)TOPSAVE_ID);
		WriteEeprom(a + 1, TOPSAVE_ID >> 8);
	}
	a += 2;                                            /* (the digits follow the block's number) */
	u8 *p = TopScoreDisplay;
	for(u8 i = 6; i; i--, a++, p++){
		u8 v = ReadEeprom(a);
		if(save){
			if(v != *p) WriteEeprom(a, *p);
		}else{
			if(v > 9){                                     /* not digits: not this game's */
				memset(TopScoreDisplay, 0, 6);
				return;
			}
			*p = v;
		}
	}
}
#else
	#define top_score(save) ((void)0)
#endif

/* ScreenRoutines: task ScreenRoutineTask of the screen set-up, each the 6502 routine named */
void ScreenRoutines(void){
	u8 n;
	switch(ScreenRoutineTask){
	case 0:                                   /* InitScreen */
		top_score(true);                      /* (TOPSAVE: the top score to the EEPROM, the screen off) */
		MoveAllSpritesOffscreen();
		gfx_ClearScreen();                    /* InitializeNameTables */
		if(OperMode) VRAM_Buffer_AddrCtrl = 3;    /* the black-backdrop palette for the screens */
		break;
	case 1:                                   /* SetupIntermediate */
		setup_intermediate();
		break;
	case 3:                                   /* WriteBottomStatusLine */
		GetSBNybbles();
		/* fall through */
	case 2: {                                 /* WriteTopStatusLine */
		u8 a = A_;                            /* (the bar's line, with this screen's world and level) */
		A_ = 0;
		WriteGameText();
		A_ = a;
		break;
	}
	case 4:                                   /* DisplayTimeUp */
		if(!GameTimerExpiredFlag){
			ScreenRoutineTask = 6;
			return;
		}
		GameTimerExpiredFlag = 0;
		n = 2;
		goto output_inter;
	case 5: case 7:                           /* ResetSpritesAndScreenTimer */
		if(ScreenTimer) return;
		MoveAllSpritesOffscreen();
	reset_timer:
		ScreenTimer = 7;                      /* ResetScreenTimer */
		break;
	case 6:                                   /* DisplayIntermediate */
		if(OperMode){
			if(OperMode == GameOverModeValue){
				ScreenTimer = 0x12;
				A_ = 3;
				WriteGameText();
				goto inc_mode;
			}
			if(!AltEntranceControl && (AreaType == 3 || !DisableIntermediate)){
				DrawPlayer_Intermediate();
				n = 1;
			output_inter:
				A_ = n;
				WriteGameText();
				DisableScreenFlag = 0;
				goto reset_timer;
			}
		}
		ScreenRoutineTask = 8;                /* NoInter */
		return;
	case 8:                                   /* AreaParserTaskControl */
		DisableScreenFlag++;
		do{
			AreaParserTaskHandler();
		}while(AreaParserTaskNum);
		if((s8)--ColumnSets < 0){
			ScreenRoutineTask++;
			gfx_ResetView();                  /* the area is parsed: show it */
		}
		return;
	case 9:                                   /* GetAreaPalette */
		VRAM_Buffer_AddrCtrl = pgm_read_byte(&AreaPalette[AreaType]);
		break;
	case 10:                                  /* GetBackgroundColor */
		if(BackgroundColorCtrl) VRAM_Buffer_AddrCtrl = pgm_read_byte(&BGColorCtrl_Addr_[BackgroundColorCtrl - 4]);
		ScreenRoutineTask++;
		GetPlayerColors();
		return;
	case 11:                                  /* GetAlternatePalette1 */
		if(AreaStyle == 1) VRAM_Buffer_AddrCtrl = 0x0b;
		break;
	case 12:                                  /* DrawTitleScreen */
		if(OperMode) goto inc_mode;
		VRAM_Buffer_AddrCtrl = 5;
		break;
	case 13:                                  /* ClearBuffersDrawIcon */
		if(OperMode) goto inc_mode;
		gfx_TitleCursor();                    /* DrawMushroomIcon */
		break;
	default:                                  /* WriteTopScore */
		gfx_TopScore();
	inc_mode:
		OperMode_Task++;                      /* IncModeTask_B */
		return;
	}
	ScreenRoutineTask++;                      /* IncSubtask */
}

/* ---------------------------------------------------------------------------------------
** The area parser's task handler: 8 tasks, 2 columns (RenderAreaGraphics is the PPU's: gfx17.c
** draws VRAM from the block buffer as the camera moves, so its tasks do nothing here)
** ------------------------------------------------------------------------------------ */
void AreaParserTaskHandler(void){
	u8 n = AreaParserTaskNum;                 /* 0-8: only this routine writes it */
	if(!n) AreaParserTaskNum = n = 8;
	n = (n - 1) & 3;                          /* the task, 7 down to 0, of a column's four */
	if(!n) IncrementColumnPos();              /* tasks 0 and 4 */
	else if(n == 3) AreaParserCore();         /* 3 and 7; the others RenderAreaGraphics */
	if(!--AreaParserTaskNum) RenderAttributeTables();
}

/* ---------------------------------------------------------------------------------------
** The frame: the 6502's NMI after its PPU work
** ------------------------------------------------------------------------------------ */
/* OperModeExecutionTree */
static void oper_mode_execution_tree(void){
	switch(OperMode){
	case TitleScreenModeValue: title_screen_mode(); break;
	case GameModeValue: GameMode(); break;
	case VictoryModeValue: victory_mode(); break;
	default: GameOverMode(); break;
	}
}

static void ColdBoot(void){
	RA_CLEAR();
	memset(&rg, 0, sizeof rg);
	memset(&rp, 0, sizeof rp);
	OperMode = 0;
	PseudoRandomBitReg[0] = 0xa5;
	top_score(false);                         /* (TOPSAVE: the top score the EEPROM has) */
	MoveAllSpritesOffscreen();
	gfx_ClearScreen();
	DisableScreenFlag++;
}

void game_Init(void){
	ColdBoot();
}

void game_Upload(void){
	update_screen();
}

void game_Frame(void){
	game_Upload();
	game_Engine();
}

void game_Engine(void){
	SoundEngine();
	pause_routine(read_joypads());
	UpdateTopScore();
	/* the pseudorandom bit register: d1 of the first byte EOR d1 of the second, rotated in */
	u8 *p = PseudoRandomBitReg;
	asm("" : "+r"(p));                        /* p[0], p[1] through the pointer: shorter */
	u16 w = (p[0] ^ p[1]) >> 1;               /* the bit to rotate in at d0, then each byte's d0 */
	u8 i = 7;
	do{
		w = (w << 8) | *p;
		*p++ = (u8)(w >> 1);
		asm("" : "+r"(i));                    /* a counter, not a pointer compare */
	}while(--i);
	if(!(GamePauseStatus & 1)){
		if(!TimerControl || !--TimerControl){
			u8 n = 0x15;                      /* the frame timers, every 21st frame all of them */
			if((s8)--IntervalTimerControl < 0){
				IntervalTimerControl = 0x14;
				n = 0x24;
			}
			p = Timers;
			do{
				u8 v = *p;
				if(v) v--;
				*p++ = v;
			}while(--n);
		}
		FrameCounter++;
		/* The OAM back from SPI RAM, whole unless this is a frame of play (gfx_OamPlay): the
		** 6502 then moved every sprite but sprite 0 off screen and each object drew its own
		** again; sprite 0, never shown here, goes off with the rest (its bytes were the colour
		** table's). Paused: no OAM, the sprites stay (gfx_Frame) */
		if(Sprite0HitDetectFlag){
			gfx_OamPlay();
			MoveAllSpritesOffscreen();
			SpriteShuffler();
		}else{
			gfx_OamBegin();
		}
		oper_mode_execution_tree();
	}
	gfx_Frame();
}
