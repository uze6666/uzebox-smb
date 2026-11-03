/*
 * Super Uze Brothers: the one header every engine file includes. The engine is a port of
 * doppelganger's disassembly, routine for routine, under the 6502's names; the register
 * convention is below.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <avr/pgmspace.h>
#include "smb_ram.h"

/* The 6502's registers and flags: the interface of every ported routine. They
** live in CPU registers r2-r7, which every C file is compiled not to use (-ffixed-r2..r7 in the
** Makefile, the kernel's included); the kernel's interrupts save them (the video mode's call
** saves r1-r29; the sync and sound paths touch none of them) */
register u8 A_ asm("r2");
register u8 X_ asm("r3");
register u8 Y_ asm("r4");
register bool C_ asm("r5");
register bool Z_ asm("r6");
register bool N_ asm("r7");

/* Sets Z_ and N_ from a value, as a 6502 load or arithmetic would */
static inline u8 nz(u8 v){ Z_ = !v; N_ = v >> 7; return v; }

/* TWO_PLAYERS=1 (the Makefile's P2: the .uze's build has it, the standalone ROM has no room
** for it): the cartridge's game of two, Luigi the second player, at the places the port marked
** "1 player only". The waiting player's lives, place, coins and score are not in RAM beside
** the other's as the 6502's were (3 bytes of SRAM are free): each player has a place in SPI
** RAM (bb.c), where player.c's TransposePlayers leaves the one's and takes the other's. So
** every routine reads the one live score, and the 6502's tables by player (ScoreOffsets,
** CoinTallyOffsets, StatusBarNybbles) keep the first player's entry */
#ifndef TWO_PLAYERS
	#define TWO_PLAYERS 0
#endif
#include "xcalls.h"
#include "area.h"
#include "player.h"
#include "objects.h"
#include "enemies.h"
#include "bosses.h"
#include "collide.h"
#include "bgcoll.h"
#include "gfx.h"
#include "sprites.h"
#include "sound.h"
#include "game.h"
