/*
 *  Uzebox Kernel - Video Mode 17
 *  Copyright (C) 2026 Alec Bourque
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Uzebox is a reserved trade mark
*/

/**
 * ===========================================================================
 *
 * Global defines for video mode 17: the kernel's names for the mode's own
 * (m17.def.h).
 *
 * ===========================================================================
 */

#pragma once

#define VMODE_ASM_SOURCE QUOTE(VIDEO_MODE_PATH/videoModeCore.s)
#define VMODE_C_SOURCE QUOTE(VIDEO_MODE_PATH/videoMode.c)
#define VMODE_C_PROTOTYPES QUOTE(VIDEO_MODE_PATH/videoMode.h)
#ifndef VMODE_FUNC
	#define VMODE_FUNC sub_video_mode17
#endif

#include "m17.def.h"

#define TILE_WIDTH        M17_TILE_W
#define TILE_HEIGHT       M17_TILE_H
#define SCREEN_TILES_H    M17_COLS
#define SCREEN_TILES_V    M17_ROWS
#define VRAM_TILES_H      M17_VRAM_H
#define VRAM_TILES_V      M17_VRAM_V
#define VRAM_SIZE         M17_VRAM_SIZE
#define VRAM_ADDR_SIZE    1
#define VRAM_PTR_TYPE     char
#define FRAME_LINES       M17_FRAME_LINES
#define FIRST_RENDER_LINE M17_FIRST_LINE

/* Maximum free cycles usable by the hsync and audio: the renderer pads the mixer's own to
** this, so the picture stays put whatever the audio build */
#define HSYNC_USABLE_CYCLES 232
