/*
 * Super Uze Brothers on video mode 17 (customVideoMode17): the main loop.
 *
 * The frame is the main loop's, on the main stack: the picture is one long interrupt, so what
 * it cuts is suspended and goes on after it, and nothing of the mode's memory is borrowed (on
 * mode 16 the frame runs in the vsync callback, on a stack in a line buffer).
 *
 * A frame is shown by show(): what the engine's frame asked of the screen is uploaded (the
 * palettes, a text screen) and committed (gfx17.c: the view, the changed cells, the scroll),
 * and its sprites are composed. A picture that starts inside it would show a new background
 * under the last frame's sprites, or a frame without its first sprites, the player's. So
 * show() starts only where it will end before the next picture: else the loop waits for the
 * next blank's start, and the picture between shows the last frame again, whole. What the
 * engine does while it runs no picture sees.
 */
#include <string.h>
#include <avr/io.h>
#include <uzebox.h>
#include "smb.h"

/* What a frame's show() can take over the last one's, in lines: on 427,033 frames in a row
** 25.4 at most, and 13.9 once the last one took SHOW_LONG or more (the sprites drawn a
** composition are capped, so a long one has little left to grow by) */
#define SHOW_MARGIN      26
#define SHOW_LONG        30
#define SHOW_LONG_MARGIN 14
#define MARK(name) __asm__ volatile(".global " #name "\n" #name ":")

extern volatile u8 sync_flags;

static u8 show_took = 255;             /* the lines the last show() took (255: none to go by) */



/* BANDS=1 (the Makefile; customVideoMode17's M17_BANDS): the picture in bands. While the mode
** has the program's leave, a picture leaves its rows of black to the kernel's own lines, and
** what of the engine's frame did not fit the blank goes on in their time. So the leave is
** given only while nothing a picture reads is written: from show()'s end to the next one's
** start, and only while a level is shown (gfx17.c's gfx_level: there the engine's changes to
** the screen wait for the commit; a text screen and the title are written at once). A
** picture that starts inside show() has no leave and is whole, as it ever was. And show()
** never starts inside a picture: an engine's frame that ends in a picture's rows of black
** has no room() there, whatever the flag says (a picture's end set it while the frame ran:
** that was the picture before), and the loop waits for that picture's end */
extern bool gfx_level;
	#define LEVEL_SHOWN() (gfx_level && !gfx_title)   /* (the title is in the level, its cursor written at once) */

static void show(void){
	u8 l = gfx_LinesLeft();
	m17_TakeLeave();
	game_Upload();
	gfx_Commit();
	gfx_LiftsOff();                    /* (gfx17.c: a lift's row is composed once) */
	m17_ComposeSprites();
	gfx_LiftsOn();
	show_took = l - gfx_LinesLeft();   /* (more than a blank when a picture came meanwhile) */
	if(LEVEL_SHOWN()) m17_GiveLeave();
}

/* Whether show() will end before the next picture: always when that picture is not drawn.
** The lines left are show_took and its margin or more (in 8 bits: the difference, when there
** is one, against the margin; show_took is 255 when no show() was timed).
** In bands the main program runs in a picture's rows of black too: there is no room there
** (gfx_LinesLeft() would count to the picture after, and that picture reads what show()
** writes to its last line): the loop's wait is then for that picture's end, where the blank
** starts. Asked before the hold, which is the next picture's */
static bool room(void){
	if(m17_InPicture()) return false;
	if(m17_hold) return true;
	u8 took = show_took, left = gfx_LinesLeft();
	u8 margin = took >= SHOW_LONG ? SHOW_LONG_MARGIN : SHOW_MARGIN;
	return left >= took && (u8)(left - took) >= margin;
}

int main(void){
	bb_Init();
	gfx_Init();
	sound_Init();
#if SD
	sd_Load();                         /* the .uze's build: the levels and the music from the card (sd17.c) */
#endif
	game_Init();

	while(1){
		if(sync_flags & SYNC_FLAG_VSYNC){  /* a picture ended while the engine ran: the blank is under way
		                                   ** (or, in bands, the next picture's rows of black: no room) */
			ClearVsyncFlag();
			if(!room()){
				MARK(uzebros_frame_wait);
				WaitVsync(1);
			}
		}else{
			WaitVsync(1);                  /* a picture's end, drawn or held (the screen off) */
		}
		show();
		ReadControllers();                 /* this frame's buttons (CONTROLLERS_VSYNC_READ=0) */
		game_Engine();
		m17_hold = gfx_ScreenOff();        /* an area being set up: no picture, as the NES's screen off */
	}
}
