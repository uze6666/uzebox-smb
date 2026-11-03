/* apu.h -- the NES sound registers SMB's sound engine writes, and no more (apu.c) */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* A pulse, or the triangle (channel 2), which uses ctl (its linear counter's register),
** period, flags, and decay as the linear counter */
typedef struct {
	uint8_t  ctl;       /* $4000/$4004: duty, constant volume flag, volume or envelope period */
	uint8_t  sweep;     /* $4001/$4005: write it with apu_Sweep() */
	uint16_t period;    /* $4002-$4003: 11 bits */
	uint8_t  decay;     /* the envelope's level (triangle: the linear counter) */
	uint8_t  ediv;      /* the envelope's divider */
	uint8_t  sdiv;      /* the sweep's divider */
	uint8_t  flags;     /* APU_* */
} ApuChan;

#define APU_ON     1    /* the length counter is not 0: set by a frequency write, cleared by $4015 */
#define APU_TRIG   2    /* a frequency write: the envelope starts (triangle: the counter reloads) */
#define APU_RELOAD 4    /* a sweep write: its divider reloads */

/* SND_STRUCT (mode 17's ROMs, for the flash): the sound engine's own RAM (sound.c: nothing
** else reads it) is one struct behind the registers, 58 bytes in all, so that one pointer
** with a displacement reaches every byte of both: sound.c keeps it in Y. Without it the
** engine's RAM is sound.c's statics and the registers are reached by their addresses.
** Two bytes are sound.c's statics either way (the note lengths' table: NoteLenLookupTblOfs,
** NoteLengthTblAdder): the routine that reads them has no pointer. */
struct Snd {
	uint8_t Square1SoundBuffer, Square2SoundBuffer, NoiseSoundBuffer;
	uint8_t PauseSoundBuffer, PauseModeFlag;
	uint8_t Squ1_SfxLenCounter, Squ2_SfxLenCounter, Noise_SfxLenCounter, Sfx_SecondaryCounter;
	const uint8_t *MusicData;
	uint8_t MusicOffset_Square2, MusicOffset_Square1, MusicOffset_Triangle, MusicOffset_Noise;
	uint8_t NoiseDataLoopbackOfs;
	uint8_t Squ2_NoteLenBuffer, Squ2_NoteLenCounter, Squ2_EnvelopeDataCtrl;
	uint8_t Squ1_NoteLenCounter, Squ1_EnvelopeDataCtrl;
	uint8_t Tri_NoteLenBuffer, Tri_NoteLenCounter, Noise_BeatLenCounter;
	uint8_t GroundMusicHeaderOfs, AltRegContentFlag;
	uint8_t AreaMusicBuffer_Alt, DAC_Counter;
};

struct Apu {
	ApuChan c[3];       /* square 1, square 2, triangle */
	uint8_t n_ctl;      /* $400c: the noise's volume (always constant in SMB) */
	uint8_t n_period;   /* $400e */
	uint8_t n_len;      /* its length counter, in half frames */
	uint8_t dmc;        /* $4011: the DMC's level, which lowers the triangle and the noise */
	struct Snd s;       /* the sound engine's own RAM, behind the registers */
};
extern struct Apu apu;

void apu_Init(void);                /* the triangle's wave */
void ProcessMusic(void);            /* the kernel's vsync call: a frame of the APU, into the mixer */

/* A sweep register write */
static inline void apu_Sweep(uint8_t ch, uint8_t v){
	apu.c[ch].sweep = v;
	apu.c[ch].flags |= APU_RELOAD;
}
