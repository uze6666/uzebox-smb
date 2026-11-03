/*
 * tails17.h -- the game's RAM at fixed addresses on video mode 17 (TAIL_RAM, VMODE=17): what
 * tails.h is on mode 16, where these objects live in the tile planes' tails. Mode 17 has no
 * such tails: they are a block of SRAM of their own, at M17_GAME_AT (the Makefile's, which
 * starts .data after it). Constant addresses, so the engine reads them as it does on mode 16:
 * as members of one struct in .bss (the PC's arrangement) its code is 138 bytes longer.
 *
 * Nothing clears the block at boot: the owners initialise their own (game_Init the RAM
 * structs, bb_Init its cache).
 */
#pragma once

#ifndef M17_GAME_AT
	#error "tails17.h: M17_GAME_AT, the block's address"
#endif

#define TAIL_RP        (M17_GAME_AT + 0)      /* struct RamPersist (smb_ram.h), 80 bytes kept */
#define TAIL_RG        (M17_GAME_AT + 80)     /* struct RamGame, 24 */
#define TAIL_RAB       (M17_GAME_AT + 104)    /* struct RamAreaB, ra's $0400 run, 86 */
#define TAIL_RAC       (M17_GAME_AT + 190)    /* struct RamAreaC, ra's $0483 run, 86 */
#define TAIL_BB_CACHE  (M17_GAME_AT + 276)    /* bb.c's column cache, BB_CACHE x 13 bytes */
#define TAIL17_SIZE    328                    /* the block: the Makefile reads this */

/* an object of n bytes at address a ends before address b */
#define TAIL17_FITS(a, n, b) _Static_assert((a) + (n) <= (b), "tails17.h: an object outgrew its place")
