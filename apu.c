/*
 * apu.c -- the NES APU as far as SMB's sound engine uses it, on the Uzebox mixer.
 *
 * sound.c (the 6502's SoundEngine) writes the registers in apu; ProcessMusic(), which the
 * kernel calls every vsync after the game's frame, runs one NES frame of the APU's clocks
 * and sets the mixer's four channels once: square 1 and 2 on channels 0 and 1, the triangle
 * on 2, the noise on 3. What SMB never does is left out: a looped envelope, the 75% duty, the
 * noise's decay envelope and short mode, and the pulses' and triangle's length counters
 * running out (they are loaded with 127 frames; only $4015 stops a channel).
 *
 * The frame, as the NES ran it between two of the engine's passes: quarter clock, quarter +
 * half, quarter, quarter + half. The mixer is set after the first. tools/sndmodel.py is the
 * reference: the same registers on the whole APU.
 *
 * The C below is the host's (sndsim); the AVR runs the same in assembly, further down.
 */
#include <stddef.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include "apu.h"

#define SND_APU_TABLES
#include "snddata.inc"

struct Apu apu;

extern const u8 waves[] PROGMEM;                    /* the kernel's: a page a wave */
#define WAVES_PAGE ((u8)((u16)waves >> 8))
#define WAVE_PAGE(m) (((u8 *)&(m)->position)[1])    /* the mixer's wave: its position's high byte */


/* The assembly reads these places by number */
_Static_assert(offsetof(ApuChan, ctl) == 0 && offsetof(ApuChan, sweep) == 1 && offsetof(ApuChan, period) == 2 &&
               offsetof(ApuChan, decay) == 4 && offsetof(ApuChan, ediv) == 5 && offsetof(ApuChan, sdiv) == 6 &&
               offsetof(ApuChan, flags) == 7 && sizeof(ApuChan) == 8, "ApuChan");
_Static_assert(offsetof(struct Apu, n_ctl) == 24 && offsetof(struct Apu, n_period) == 25 &&
               offsetof(struct Apu, n_len) == 26 && offsetof(struct Apu, dmc) == 27, "struct Apu");
_Static_assert(APU_ON == 1 && APU_TRIG == 2 && APU_RELOAD == 4, "APU_*");
_Static_assert(offsetof(struct MixerStruct, channels.type.wave[1]) == 6 &&
               offsetof(struct MixerWaveChannelStruct, step) == 1 &&
               offsetof(struct MixerWaveChannelStruct, position) == 4 &&
               offsetof(struct MixerStruct, channels.type.noise.volume) == 18 &&
               offsetof(struct MixerStruct, channels.type.noise.params) == 19, "mixer");

#define STR_(x) #x
#define STR(x) STR_(x)

/*
 * ProcessMusic: the C above, the same clocks in the same order. Y is the channel (apu.c[ch]),
 * T the sweep's mute. The quarter frames of a pulse: envelope, target, mixer; envelope, target,
 * sweep; envelope; envelope, target, sweep (its period moves only in a sweep, so the target
 * is computed again only there). A step is STEP_K / (period + 1): every divisor here is above
 * 8 (a pulse is muted under 8, the triangle's periods are the note table's), so STEP_K's top 3
 * bits are a remainder already and 16 steps of division remain.
 */
__asm__(
	".if (" STR(STEP_K) " >> 16) > 8\n"
	".error \"STEP_K: its top bits are no longer under every divisor\"\n"
	".endif\n"
	".if " STR(SOFT_PERIOD) " > 255 || " STR(SNAP_MUL) " > 255 || " STR(TRI_VOL) " > 255\n"
	".error \"SOFT_PERIOD, SNAP_MUL, TRI_VOL: a byte each\"\n"
	".endif\n"
	".section .text.ProcessMusic,\"ax\",@progbits\n"
	".global ProcessMusic\n"
	".type ProcessMusic, @function\n"
"ProcessMusic:\n"
	"\tpush r28\n"
	"\tpush r29\n"
	"\tldi  r28, lo8(apu)\n"
	"\tldi  r29, hi8(apu)\n"
	"\tldi  r19, 1\n"                           /* square 1: the sweep's one's complement */
	"\trcall pm_pulse\n"
	"\tadiw r28, 8\n"
	"\tclr  r19\n"
	"\trcall pm_pulse\n"
	"\tadiw r28, 8\n"                           /* the triangle: its linear counter in r23 */
	"\tldd  r20, Y+0\n"
	"\tldd  r22, Y+7\n"
	"\tldd  r23, Y+4\n"
	"\trcall pm_lin\n"
	"\tclr  r24\n"                              /* silent: step 0, the wave stopped where it is */
	"\tclr  r25\n"
	"\tsbrs r22, 0\n"
	"\trjmp 1f\n"
	"\ttst  r23\n"
	"\tbreq 1f\n"
	"\tldd  r24, Y+2\n"
	"\tldd  r25, Y+3\n"
	"\trcall pm_stepof\n"
	"\tlsr  r25\n"
	"\tror  r24\n"
"1:\tsts  mixer+13, r24\n"
	"\tsts  mixer+14, r25\n"
	"\tldi  r24, " STR(TRI_VOL) "\n"
	"\trcall pm_att\n"
	"\tsts  mixer+12, r24\n"
	"\trcall pm_lin\n"
	"\trcall pm_lin\n"
	"\trcall pm_lin\n"
	"\tstd  Y+4, r23\n"
	"\tstd  Y+7, r22\n"
	"\tclr  r24\n"                              /* the noise */
	"\tldd  r18, Y+10\n"
	"\ttst  r18\n"
	"\tbreq 2f\n"
	"\tldd  r30, Y+8\n"
	"\tandi r30, 15\n"
	"\tldi  r31, 0\n"
	"\tsubi r30, lo8(-(NoiseVol))\n"
	"\tsbci r31, hi8(-(NoiseVol))\n"
	"\tlpm  r24, Z\n"
	"\trcall pm_att\n"
	"\tldd  r30, Y+9\n"
	"\tandi r30, 15\n"
	"\tldi  r31, 0\n"
	"\tsubi r30, lo8(-(NoiseParam))\n"
	"\tsbci r31, hi8(-(NoiseParam))\n"
	"\tlpm  r30, Z\n"
	"\tsts  mixer+19, r30\n"
	"\tdec  r18\n"                              /* its length: two half frames */
	"\tbreq 1f\n"
	"\tdec  r18\n"
"1:\tstd  Y+10, r18\n"
"2:\tsts  mixer+18, r24\n"
	"\tpop  r29\n"
	"\tpop  r28\n"
	"\tret\n"

/* A frame of a pulse (Y, r19 1 for square 1); r22 its flags */
"pm_pulse:\n"
	"\tldd  r22, Y+7\n"
	"\trcall pm_env\n"
	"\trcall pm_target\n"
	"\tclr  r23\n"                              /* the volume */
	"\tbrts 3f\n"                               /* muted: the volume 0 alone */
	"\tsbrc r22, 0\n"
	"\trjmp 1f\n"
	"\tset\n"                                   /* off: the same */
	"\trjmp 3f\n"
"1:\tldd  r20, Y+0\n"
	"\tldi  r21, hi8(waves+768)\n"              /* the wave: the triangle's above 1.5 kHz */
	"\tcpi  r24, " STR(SOFT_PERIOD) "\n"
	"\tcpc  r25, r1\n"
	"\tbrcs 2f\n"
	"\tldi  r21, hi8(waves+512)\n"              /* else 12.5, 25 or 50% by the duty */
	"\tsbrc r20, 6\n"
	"\tdec  r21\n"
	"\tsbrc r20, 7\n"
	"\tsubi r21, 2\n"
"2:\tmov  r30, r20\n"                           /* the volume: constant, or the envelope's */
	"\tandi r30, 15\n"
	"\tsbrs r20, 4\n"
	"\tldd  r30, Y+4\n"
	"\tldi  r31, 0\n"
	"\tsubi r30, lo8(-(PulseVol))\n"
	"\tsbci r31, hi8(-(PulseVol))\n"
	"\tlpm  r23, Z\n"
	"\trcall pm_pstep\n"
"3:\tldi  r30, lo8(mixer+6)\n"                  /* the channel's mixer */
	"\tldi  r31, hi8(mixer+6)\n"
	"\tsbrc r19, 0\n"
	"\tsbiw r30, 6\n"
	"\tbrts 4f\n"
	"\tstd  Z+1, r24\n"
	"\tstd  Z+2, r25\n"
	"\tstd  Z+5, r21\n"
"4:\tst   Z, r23\n"
	"\trcall pm_es\n"
	"\trcall pm_env\n"
	"\trcall pm_es\n"
	"\tstd  Y+7, r22\n"
	"\tret\n"

/* A quarter frame of the envelope */
"pm_env:\n"
	"\tsbrs r22, 1\n"
	"\trjmp 1f\n"
	"\tandi r22, 0xfd\n"                        /* started: its level 15 */
	"\tldi  r18, 15\n"
	"\tstd  Y+4, r18\n"
	"\trjmp 2f\n"
"1:\tldd  r18, Y+5\n"
	"\tsubi r18, 1\n"
	"\tbrcc 3f\n"
	"\tldd  r18, Y+4\n"                         /* its divider at 0: the level a step down, to 0 */
	"\tcpse r18, r1\n"
	"\tdec  r18\n"
	"\tstd  Y+4, r18\n"
"2:\tldd  r18, Y+0\n"
	"\tandi r18, 15\n"
"3:\tstd  Y+5, r18\n"
	"\tret\n"

/* A quarter and a half frame: the envelope, then the sweep */
"pm_es:\n"
	"\trcall pm_env\n"
	"\trcall pm_target\n"
	"\tldd  r18, Y+6\n"
	"\ttst  r18\n"
	"\tbrne 3f\n"
	"\tbrts 1f\n"                               /* its divider at 0: the period moves, unless muted, */
	"\tsbrs r21, 7\n"                           /* disabled */
	"\trjmp 1f\n"
	"\tmov  r18, r21\n"                         /* or of shift 0 */
	"\tandi r18, 7\n"
	"\tbreq 1f\n"
	"\tstd  Y+2, r30\n"
	"\tstd  Y+3, r31\n"
"1:\tmov  r18, r21\n"                           /* the divider reloaded */
	"\tswap r18\n"
	"\tandi r18, 7\n"
	"\tandi r22, 0xfb\n"
	"\trjmp 4f\n"
"3:\tsbrc r22, 2\n"
	"\trjmp 1b\n"
	"\tdec  r18\n"
"4:\tstd  Y+6, r18\n"
	"\tret\n"

/* The sweep's target in r31:r30, its mute in T; the period in r25:r24, the sweep in r21 */
"pm_target:\n"
	"\tldd  r24, Y+2\n"
	"\tldd  r25, Y+3\n"
	"\tldd  r21, Y+1\n"
	"\tmovw r30, r24\n"
	"\tmov  r18, r21\n"
	"\tandi r18, 7\n"
	"\trjmp 2f\n"
"1:\tlsr  r31\n"
	"\tror  r30\n"
"2:\tdec  r18\n"
	"\tbrpl 1b\n"
	"\tclt\n"
	"\tcpi  r24, 8\n"
	"\tcpc  r25, r1\n"
	"\tbrcc 3f\n"
	"\tset\n"
"3:\tsbrs r21, 3\n"
	"\trjmp 4f\n"
	"\tsub  r30, r24\n"                         /* negated: period - t - 1 (square 1), period - t */
	"\tsbc  r31, r25\n"
	"\tcom  r30\n"
	"\tcom  r31\n"
	"\tsbrs r19, 0\n"
	"\tadiw r30, 1\n"
	"\tret\n"
"4:\tadd  r30, r24\n"
	"\tadc  r31, r25\n"
	"\tcpi  r31, 8\n"
	"\tbrcs 5f\n"
	"\tset\n"
"5:\tret\n"

/* pulse_step: r25:r24 the period, then its step */
"pm_pstep:\n"
	"\tadiw r24, 1\n"
	"\ttst  r25\n"
	"\tbrne pm_divk\n"
	"\tldi  r18, " STR(SNAP_MUL) "\n"
	"\tmul  r24, r18\n"                         /* samples a cycle, 8.8 */
	"\tmov  r20, r0\n"
	"\tmov  r18, r1\n"
	"\tclr  r1\n"
	"\tsbrs r20, 7\n"
	"\trjmp 1f\n"
	"\tneg  r20\n"
	"\tinc  r18\n"
"1:\tmov  r0, r18\n"                            /* within n + n / 4 of n samples: 65536 / n */
	"\tlsr  r0\n"
	"\tlsr  r0\n"
	"\tadd  r0, r18\n"
	"\tcp   r0, r20\n"
	"\tbrcs pm_divk\n"
	"\tmov  r26, r18\n"
	"\tclr  r27\n"
	"\tclr  r30\n"
	"\tclr  r31\n"
	"\tmov  r24, r18\n"
	"\tlsr  r24\n"
	"\tsub  r24, r18\n"
	"\tldi  r25, 0xff\n"
	"\trcall pm_div\n"
	"\tadiw r24, 1\n"
	"\tret\n"

/* step_of: r25:r24 the period, then STEP_K / (period + 1) */
"pm_stepof:\n"
	"\tadiw r24, 1\n"
"pm_divk:\n"
	"\tmovw r26, r24\n"
	"\tldi  r24, lo8(" STR(STEP_K) ")\n"
	"\tldi  r25, hi8(" STR(STEP_K) ")\n"
	"\tldi  r30, hlo8(" STR(STEP_K) ")\n"
	"\tclr  r31\n"
/* r25:r24 / r27:r26, r31:r30 the remainder so far: the quotient in r25:r24 */
"pm_div:\n"
	"\tldi  r18, 16\n"
"1:\tlsl  r24\n"
	"\trol  r25\n"
	"\trol  r30\n"
	"\trol  r31\n"
	"\tcp   r30, r26\n"
	"\tcpc  r31, r27\n"
	"\tbrcs 2f\n"
	"\tsub  r30, r26\n"
	"\tsbc  r31, r27\n"
	"\tinc  r24\n"
"2:\tdec  r18\n"
	"\tbrne 1b\n"
	"\tret\n"

/* A quarter frame of the triangle's linear counter (r23; r20 its register, r22 its flags) */
"pm_lin:\n"
	"\tsbrs r22, 1\n"
	"\trjmp 1f\n"
	"\tmov  r23, r20\n"
	"\tandi r23, 0x7f\n"
	"\trjmp 2f\n"
"1:\tcpse r23, r1\n"
	"\tdec  r23\n"
"2:\tsbrs r20, 7\n"
	"\tandi r22, 0xfd\n"
	"\tret\n"

/* att: r24 lowered by the DMC's level (Y the triangle's, its level at Y+11) */
"pm_att:\n"
	"\tldd  r25, Y+11\n"
	"\tmov  r0, r25\n"
	"\tlsr  r0\n"
	"\tadd  r25, r0\n"
	"\tmul  r24, r25\n"
	"\tsub  r24, r1\n"
	"\tclr  r1\n"
	"\tret\n"
	".size ProcessMusic, .-ProcessMusic\n"
	".text\n");


void apu_Init(void){
	WAVE_PAGE(&mixer.channels.type.wave[2]) = WAVES_PAGE + 3;
}
