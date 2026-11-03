/*
 * Super Uze Brothers as a .uze (SD=1, make uze): the level data and the music are not in the
 * flash. A .uze is 61,440 bytes at most, the bootloader keeping the chip's last 4 KB, and the
 * full version is 65,482. So those 5,981 bytes follow the program inside the game's own file
 * on the card, and are read here into SPI RAM when the console is switched on, where the
 * engine reads them (area.c, enemies.c, sound.c: sd_byte()).
 *
 * The card is read with the bootloader's own SD and FAT code: a version 5.0.06 or later
 * bootloader has a table of entry points at 0xF000 (kernel/bootlib.h names them;
 * the official Uzebox repository's demos/Bootloader5 is its source), and nothing of an SD library is linked here. A console
 * with an older bootloader, or with none, cannot run this build: it says so. The standalone
 * .hex (make) has the data in its flash and reads no card.
 *
 * The data is a section of the game's own link, .sddata, at 0x8000 of the data space where
 * there is no memory: so a pointer to a level block or to the music is its address in SPI
 * RAM bank 1 with no table made by hand, and default/mkuze.py takes the bytes out of the ELF.
 * In the file, at the first sector after the program: the data's CRC-32 (4 bytes), which the
 * link gives the program (sd_id_lo, sd_id_hi), then the data. The file is found by that: a
 * .UZE of the root directory that is a whole number of sectors (a .uze of the usual kind is
 * its header and its program, to the byte) with sectors after its program is looked at. So
 * the file may have any name, its header anything, and two versions on a card find each its
 * own.
 *
 * The SPI bus is the card's and the SPI RAM's (rules found on a Uzebox): the bootloader's code wants the card's clock and waits on SPIF, which the SPI
 * RAM's routines leave set, counting cycles; the SPI RAM's want the full clock, and the card
 * off the bus.
 */
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include <spiram.h>
#include <bootlib.h>                     /* sdc_struct_t */
#include "smb.h"

#if SD

#define SD_BANK 1                        /* SPI RAM bank 1: the block store has bank 0 (bb.c) */
#define SD_BASE 0x8000                   /* .sddata's address, its low 16 bits (the Makefile's SDDATA_AT) */

/* A byte of the level data or of the music */
u8 sd_byte(const u8 *p){
	return SpiRamReadU8(SD_BANK, (u16)p);
}

/* The bootloader's entry points, byte addresses; called as C functions (what they clobber is
** what a C function may) */
#define BL_SIGNATURE    0xF002           /* B0 07 10 AD, then the version: 0x50, 0x06 and later */
#define BL_FAT_INIT     0xF01C           /* the card and its file system: 0, or what failed (6: a read, 7: no FAT16 or FAT32) */
#define BL_FAT_READ     0xF020           /* the selected file's current sector into the buffer: 0, or an error */
#define BL_FAT_NEXT     0xF024           /* the next sector of the file: 0, or 1 at its end */
#define BL_FAT_ROOT     0xF028           /* the root directory selected, at its start */
#define BL_FAT_CLUSTER  0xF02A           /* a file selected by its first cluster, at its start */
#define BL_FAT_FILE     0xF02C           /* a directory entry's first cluster */
typedef u8 (*bl_io)(sdc_struct_t *);
#define BL_ROOT(d)       ((void (*)(sdc_struct_t *))(BL_FAT_ROOT / 2))(d)
#define BL_CLUSTER(d, c) ((void (*)(sdc_struct_t *, u32))(BL_FAT_CLUSTER / 2))(d, c)
#define BL_FILE(d, e)    ((u32 (*)(sdc_struct_t *, u8 *))(BL_FAT_FILE / 2))(d, e)

/* The sector's buffer: the mode's RAM tiles, which nothing uses before the first frame */
extern u8 m17_ram_tiles[];
#define BUF m17_ram_tiles

/* From the link (the Makefile, default/mkuze.py --id): the data's CRC-32 in two halves and its
** sectors in the file, as the addresses of three symbols. Weak: the links that only measure
** (the passes', default/bootjmp.py's first) have none, and mkuze.py checks the ROM's */
extern const u8 sd_id_lo[] __attribute__((weak)), sd_id_hi[] __attribute__((weak)), sd_secs[] __attribute__((weak));

static void spi_clock(u8 cr){
	SPCR = cr;
	SPSR = 1 << SPI2X;
}

/* A call of the bootloader's that uses the card: at the card's clock (28.6 MHz / 8), SPIF
** cleared first; then the card off the bus whatever the call left, and the full clock back
** for the SPI RAM */
static u8 card(u16 at, sdc_struct_t *d){
	spi_clock((1 << MSTR) | (1 << SPE) | (1 << SPR0));
	(void)SPSR;
	(void)SPDR;
	u8 r = ((bl_io)(at / 2))(d);
	PORTD |= 1 << PD6;
	spi_clock((1 << MSTR) | (1 << SPE));
	return r;
}

enum { NO_LOADER, NO_CARD, NO_FAT, NO_FILE, READ_ERROR, LOADED };

/* The data, from the file's sector that is in the buffer on: sd_secs sectors into SPI RAM,
** the first four bytes (the CRC) under SD_BASE. (mkuze.py refuses data that would pass the
** bank's end) */
static u8 copy(sdc_struct_t *f){
	u16 at = SD_BASE - 4;
	for(u8 n = (u8)(u16)sd_secs; ; ){
		SpiRamWriteFrom(SD_BANK, at, BUF, 512);
		if(!--n) return LOADED;
		at += 512;
		if(card(BL_FAT_NEXT, f) || card(BL_FAT_READ, f)) return READ_ERROR;
	}
}

static u8 load(void){
	sdc_struct_t d, f;
	u8 none = NO_FILE;                   /* what is said when no file is the game's: a read failed, or none did */
	if(pgm_read_dword(BL_SIGNATURE) != 0xAD1007B0UL) return NO_LOADER;
	if(((u16)pgm_read_byte(BL_SIGNATURE + 4) << 8 | pgm_read_byte(BL_SIGNATURE + 5)) < 0x5006) return NO_LOADER;
	d.bufp = BUF;
	u8 r = card(BL_FAT_INIT, &d);
	if(r) return r == 7 ? NO_FAT : r == 6 ? READ_ERROR : NO_CARD;
	BL_ROOT(&d);
	for(u16 n = 0; ; n++){
		u8 *e = BUF + (u8)(n & 15) * 32;
		/* a sector of the directory is 16 entries; its end, or an entry never used: no file */
		if(!(n & 15)){
			if(n && card(BL_FAT_NEXT, &d)) return none;
			if(card(BL_FAT_READ, &d)) return READ_ERROR;
		}
		if(!e[0]) return none;
		/* a file (no directory, no volume's or long name's entry), not erased, a .UZE */
		if(e[0] == 0xe5 || (e[11] & 0x18) || e[8] != 'U' || e[9] != 'Z' || e[10] != 'E') continue;
		/* of whole sectors, three at least (a header, a program, data): a .uze of the usual
		** kind is its header and its program to the byte, and is passed here unread */
		if(e[28] || (e[29] & 1)) continue;
		u16 secs = *(u32 *)(e + 28) >> 9;
		if(secs < 3) continue;
		f = d;
		BL_CLUSTER(&f, BL_FILE(&d, e));
		if(card(BL_FAT_READ, &f)) none = READ_ERROR;
		else{
			/* its header: the program's size is bytes 8 to 11 (61,440 at most): the data is
			** in the sector after the program's last */
			u16 k = (*(u16 *)(BUF + 8) + 511) >> 9;
			if(secs > (u16)(k + 1)){
				do{
					if(card(BL_FAT_NEXT, &f)) break;
				}while(k--);
				if(k != 0xffff || card(BL_FAT_READ, &f)) none = READ_ERROR;
				else if(*(u16 *)BUF == (u16)sd_id_lo && *(u16 *)(BUF + 2) == (u16)sd_id_hi) return copy(&f);
			}
		}
		if(card(BL_FAT_READ, &d)) return READ_ERROR;      /* the directory's sector back in the buffer */
	}
}

/* What could not be loaded, in the game's font (NES tile codes: a digit is its value, A is
** 0x0a, the blank 0x24, the period 0xaf): a length, then the text */
static const u8 Faults[] PROGMEM = {
	24, 0x0b, 0x18, 0x18, 0x1d, 0x15, 0x18, 0x0a, 0x0d, 0x0e, 0x1b, 0x24,                   /* BOOTLOADER 5.0.06 NEEDED */
	    0x05, 0xaf, 0x00, 0xaf, 0x00, 0x06, 0x24, 0x17, 0x0e, 0x0e, 0x0d, 0x0e, 0x0d,
	10, 0x17, 0x18, 0x24, 0x1c, 0x0d, 0x24, 0x0c, 0x0a, 0x1b, 0x0d,                         /* NO SD CARD */
	26, 0x0c, 0x0a, 0x1b, 0x0d, 0x24, 0x12, 0x1c, 0x24, 0x17, 0x18, 0x1d, 0x24,             /* CARD IS NOT FAT16 OR FAT32 */
	    0x0f, 0x0a, 0x1d, 0x01, 0x06, 0x24, 0x18, 0x1b, 0x24, 0x0f, 0x0a, 0x1d, 0x03, 0x02,
	25, 0x10, 0x0a, 0x16, 0x0e, 0x24, 0x0f, 0x12, 0x15, 0x0e, 0x24, 0x17, 0x18, 0x1d, 0x24, /* GAME FILE NOT ON THE CARD */
	    0x18, 0x17, 0x24, 0x1d, 0x11, 0x0e, 0x24, 0x0c, 0x0a, 0x1b, 0x0d,
	15, 0x0c, 0x0a, 0x1b, 0x0d, 0x24, 0x1b, 0x0e, 0x0a, 0x0d, 0x24, 0x0e, 0x1b, 0x1b, 0x18, 0x1b, /* CARD READ ERROR */
};

/* At power-on, after the mode's set-up and before the game's: the data into SPI RAM. Without
** it there is no game: the fault is written in the middle of a black screen, and the load is
** tried again twice a second, so a card put in then starts the game (and a card that a reset
** cut in the middle of a sector gets the clocks to end it) */
void sd_Load(void){
	u8 r, shown = LOADED;
	while((r = load()) != LOADED){
		if(r != shown){
			const u8 *p = Faults;
			u8 len;
			for(shown = r; len = pgm_read_byte(p++), r; r--) p += len;
			u8 text[28];
			memcpy_P(text, p, len);
			gfx_ClearScreen();
			gfx_LoadPalette(1);          /* the text's white is the ground's palette 2 */
			m17_SetBackdrop(0);
			gfx_PutText((u8)(28 - len) / 2, 11, text, len);
		}
		WaitVsync(30);
	}
	if(shown != LOADED) gfx_ClearScreen();
}

#endif
