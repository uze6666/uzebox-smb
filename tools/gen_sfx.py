#!/usr/bin/env python3
"""gen_sfx.py -- the sound effects, as Uzebox patches: ../data/sfx.inc (for ../sound_patch.c, the
build before the music). Its APU and its port of the 6502's effect handlers are also the base
of the sound engine's reference, sndmodel.py.

    python gen_sfx.py            writes ../data/sfx.inc and prints a summary
    python gen_sfx.py -v         also prints every effect frame by frame (NES, then the patch)
    python gen_sfx.py --json F   also writes the per-frame model to F (for the emulator check)

How:
1. The 6502's sound effect handlers (doppelganger's disassembly, SoundEngine and below) are ported here
   line for line, writing the APU's registers; an APU model (pulse: envelope, sweep, length
   counter, the sweep's muting; noise: envelope, period, length) turns the writes into what is
   heard, a quarter frame at a time (SMB writes $4017 = $ff every NMI: the frame counter is
   reset and clocks the envelopes and the sweeps/length counters at once, then at 1/4, 1/2 and
   3/4 of the frame: 4 quarter clocks and 2 half clocks a frame).
2. Each effect is run alone from a silent APU, one frame per NMI, until its buffer empties: a
   per-frame list of (frequency, volume 0-15, duty or noise period).
3. Each list becomes a patch: a stream of the kernel's patch commands (kernel/defines.h PC_*),
   one frame per delta tick, found by a greedy search (the command set that keeps the most
   frames within tolerance per byte). Volume: PC_ENV_VOL (the NES DAC's level for that
   volume, 0-255), or PC_ENV_SPEED for a run that falls or rises linearly; within 0.6 of a
   NES step, silence exact. Pitch: PC_PITCH (or NOTE_UP/DOWN) to the nearest MIDI note, or
   PC_SLIDE for a sweep; within 0.52 semitone, and a held NES pitch is held, a rising one does
   not fall. Each frame's NES values are the mean of its four quarter frames.
   Waves: 3 = square 25% (NES duty 12.5% and 25% and 75%), 4 = square 50%. Noise: PC_NOISE_PARAMS
   = (divider << 1) | 1, the 15-bit LFSR shifted every divider+1 mixer samples (15,734 Hz).
   The encoding is checked against a model of the kernel's ProcessMusic(): what it would play
   each frame is compared with what the NES played.

The C side (../sound_patch.c) ports the handlers' control flow (queues, buffers, length counters,
which effect may interrupt which) and triggers a patch on the frame the 6502 made its first
register write, on the NES's channel: square 1 -> 0, square 2 -> 1, noise -> 3.
"""
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import asmdata  # noqa: E402

OUT = os.path.join(HERE, "..", "data", "sfx.inc")

CPU = 1789773.0                     # NTSC 2A03
MIX_RATE = 15734.263736263736       # Uzebox mixer: one sample a scanline

# ---------------------------------------------------------------------------------------
# The APU
# ---------------------------------------------------------------------------------------

LENGTH_TABLE = [10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30]
NOISE_PERIODS = [4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068]


class Envelope:
    def __init__(self):
        self.start = False
        self.divider = 0
        self.decay = 0
        self.const = 0
        self.vol = 0
        self.loop = 0

    def quarter(self):
        if self.start:
            self.start = False
            self.decay = 15
            self.divider = self.vol
        elif self.divider == 0:
            self.divider = self.vol
            if self.decay:
                self.decay -= 1
            elif self.loop:
                self.decay = 15
        else:
            self.divider -= 1

    def output(self):
        return self.vol if self.const else self.decay


class Pulse:
    def __init__(self, ones_complement):
        self.ones = ones_complement     # pulse 1 negates with one's complement
        self.env = Envelope()
        self.duty = 0
        self.period = 0
        self.length = 0
        self.enabled = False
        self.sw_en = self.sw_p = self.sw_neg = self.sw_shift = 0
        self.sw_div = 0
        self.sw_reload = False

    def write(self, reg, v):
        if reg == 0:
            self.duty = v >> 6
            self.env.loop = (v >> 5) & 1
            self.env.const = (v >> 4) & 1
            self.env.vol = v & 15
        elif reg == 1:
            self.sw_en, self.sw_p = v >> 7, (v >> 4) & 7
            self.sw_neg, self.sw_shift = (v >> 3) & 1, v & 7
            self.sw_reload = True
        elif reg == 2:
            self.period = (self.period & 0x700) | v
        else:
            self.period = (self.period & 0xff) | ((v & 7) << 8)
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            self.env.start = True

    def target(self):
        change = self.period >> self.sw_shift
        if self.sw_neg:
            return self.period - change - (1 if self.ones else 0)
        return self.period + change

    def muted(self):
        return self.period < 8 or (not self.sw_neg and self.target() > 0x7ff)

    def half(self):
        if self.length and not self.env.loop:
            self.length -= 1
        if self.sw_div == 0 and self.sw_en and self.sw_shift and not self.muted():
            self.period = max(0, self.target())
        if self.sw_div == 0 or self.sw_reload:
            self.sw_div = self.sw_p
            self.sw_reload = False
        else:
            self.sw_div -= 1

    def sample(self):
        """(frequency Hz, volume 0-15, duty) or volume 0"""
        if not self.enabled or self.length == 0 or self.muted():
            return (0.0, 0, self.duty)
        return (CPU / (16.0 * (self.period + 1)), self.env.output(), self.duty)


class Noise:
    def __init__(self):
        self.env = Envelope()
        self.mode = 0
        self.pidx = 0
        self.length = 0
        self.enabled = False

    def write(self, reg, v):
        if reg == 0:
            self.env.loop = (v >> 5) & 1
            self.env.const = (v >> 4) & 1
            self.env.vol = v & 15
        elif reg == 2:
            self.mode, self.pidx = v >> 7, v & 15
        elif reg == 3:
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            self.env.start = True

    def half(self):
        if self.length and not self.env.loop:
            self.length -= 1

    def sample(self):
        if not self.enabled or self.length == 0:
            return (0.0, 0, self.pidx)
        return (CPU / NOISE_PERIODS[self.pidx], self.env.output(), self.pidx)


class APU:
    def __init__(self):
        self.sq1, self.sq2, self.noise = Pulse(True), Pulse(False), Noise()

    def chans(self):
        return (self.sq1, self.sq2, self.noise)

    def write(self, addr, v):
        if addr == 0x4015:
            for i, c in enumerate(self.chans()):
                c.enabled = bool(v >> (i if i < 2 else 3) & 1)
                if not c.enabled:
                    c.length = 0
        elif 0x4000 <= addr <= 0x4003:
            self.sq1.write(addr - 0x4000, v)
        elif 0x4004 <= addr <= 0x4007:
            self.sq2.write(addr - 0x4004, v)
        elif 0x400c <= addr <= 0x400f:
            self.noise.write(addr - 0x400c, v)

    def quarter(self, half):
        for c in self.chans():
            c.env.quarter()
            if half:
                c.half()

# ---------------------------------------------------------------------------------------
# The 6502's sound effect handlers (labels as in the disassembly)
# ---------------------------------------------------------------------------------------

FreqRegLookupTbl = asmdata.bytes_of("FreqRegLookupTbl")
SwimStompEnvelopeData = asmdata.bytes_of("SwimStompEnvelopeData")
ExtraLifeFreqData = asmdata.bytes_of("ExtraLifeFreqData")
PowerUpGrabFreqData = asmdata.bytes_of("PowerUpGrabFreqData")
PUp_VGrow_FreqData = asmdata.bytes_of("PUp_VGrow_FreqData")
BrickShatterFreqData = asmdata.bytes_of("BrickShatterFreqData")
BrickShatterEnvData = [asmdata.value(t) for k, t in asmdata.parse()[0]["BrickShatterEnvData"]
                       if k == "db"]
BowserFlameEnvData = asmdata.bytes_of("BowserFlameEnvData")
# BowserFlameEnvData-1,y with y = 0 on the last frame reads the byte before the table
BEFORE_FLAME = asmdata.bytes_of("WaterEventMusEnvData")[-1]


class Engine:
    """RAM of the sound engine and the routines, writing an APU"""

    def __init__(self, apu):
        self.apu = apu
        self.Square1SoundQueue = self.Square2SoundQueue = self.NoiseSoundQueue = 0
        self.PauseSoundQueue = 0
        self.Square1SoundBuffer = self.Square2SoundBuffer = self.NoiseSoundBuffer = 0
        self.PauseSoundBuffer = self.PauseModeFlag = 0
        self.Squ1_SfxLenCounter = self.Squ2_SfxLenCounter = self.Noise_SfxLenCounter = 0
        self.Sfx_SecondaryCounter = 0
        self.writes = 0                 # register writes this frame (other than $4015)

    def w(self, addr, v):
        if addr != 0x4015:
            self.writes += 1
        self.apu.write(addr, v & 0xff)

    # -- SoundEngine (the part for effects; OperMode is not the title screen) ------------
    def SoundEngine(self):
        self.w(0x4015, 0x0f)
        if self.PauseModeFlag or self.PauseSoundQueue == 1:
            self.InPause()
        else:
            self.Square1SfxHandler()
            self.Square2SfxHandler()
            self.NoiseSfxHandler()
        self.Square1SoundQueue = self.Square2SoundQueue = 0
        self.NoiseSoundQueue = self.PauseSoundQueue = 0

    def InPause(self):
        if not self.PauseSoundBuffer:
            if not self.PauseSoundQueue:
                return
            self.PauseSoundBuffer = self.PauseModeFlag = self.PauseSoundQueue
            self.w(0x4015, 0)
            self.Square1SoundBuffer = self.Square2SoundBuffer = self.NoiseSoundBuffer = 0
            self.w(0x4015, 0x0f)
            self.Squ1_SfxLenCounter = 0x2a
            self.PlaySqu1Sfx(0x44, 0x84, 0x7f)                 # PTone1F
        else:
            c = self.Squ1_SfxLenCounter                        # ContPau
            if c in (0x24, 0x18):
                self.PlaySqu1Sfx(0x64, 0x84, 0x7f)             # PTone2F
            elif c == 0x1e:
                self.PlaySqu1Sfx(0x44, 0x84, 0x7f)             # PTone1F
        self.Squ1_SfxLenCounter -= 1                           # DecPauC
        if self.Squ1_SfxLenCounter == 0:
            self.w(0x4015, 0)
            if self.PauseSoundBuffer == 2:
                self.PauseModeFlag = 0
            self.PauseSoundBuffer = 0

    # -- register dumps --------------------------------------------------------------------
    def Dump_Freq_Regs(self, a, base):
        lo = FreqRegLookupTbl[a + 1]
        if lo == 0:                                            # NoTone
            return
        self.w(base + 2, lo)
        self.w(base + 3, FreqRegLookupTbl[a] | 0x08)

    def Dump_Squ1_Regs(self, x, y):
        self.w(0x4001, y)
        self.w(0x4000, x)

    def PlaySqu1Sfx(self, a, x, y):
        self.Dump_Squ1_Regs(x, y)
        self.Dump_Freq_Regs(a, 0x4000)                         # SetFreq_Squ1

    def Dump_Sq2_Regs(self, x, y):
        self.w(0x4004, x)
        self.w(0x4005, y)

    def PlaySqu2Sfx(self, a, x, y):
        self.Dump_Sq2_Regs(x, y)
        self.Dump_Freq_Regs(a, 0x4004)                         # SetFreq_Squ2

    # -- square 1 ----------------------------------------------------------------------------
    def Square1SfxHandler(self):
        q = self.Square1SoundQueue
        if q:
            self.Square1SoundBuffer = q
            if q & 0x80:
                return self.PlaySmallJump()
            for bit, f in ((0x01, self.PlayBigJump), (0x02, self.PlayBump),
                           (0x04, self.PlaySwimStomp), (0x08, self.PlaySmackEnemy),
                           (0x10, self.PlayPipeDownInj), (0x20, self.PlayFireballThrow),
                           (0x40, self.PlayFlagpoleSlide)):
                if q & bit:
                    return f()
        b = self.Square1SoundBuffer                            # CheckSfx1Buffer
        if not b:
            return
        if b & 0x80:
            return self.ContinueSndJump()
        for bit, f in ((0x01, self.ContinueSndJump), (0x02, self.ContinueBumpThrow),
                       (0x04, self.ContinueSwimStomp), (0x08, self.ContinueSmackEnemy),
                       (0x10, self.ContinuePipeDownInj), (0x20, self.ContinueBumpThrow),
                       (0x40, self.DecrementSfx1Length)):
            if b & bit:
                return f()

    def PlayFlagpoleSlide(self):
        self.Squ1_SfxLenCounter = 0x40
        self.Dump_Freq_Regs(0x62, 0x4000)                      # SetFreq_Squ1
        self.Dump_Squ1_Regs(0x99, 0xbc)                        # FPS2nd / DmpJpFPS
        self.DecrementSfx1Length()

    def PlaySmallJump(self):
        self.JumpRegContents(0x26)

    def PlayBigJump(self):
        self.JumpRegContents(0x18)

    def JumpRegContents(self, a):
        self.PlaySqu1Sfx(a, 0x82, 0xa7)
        self.Squ1_SfxLenCounter = 0x28
        self.ContinueSndJump()

    def ContinueSndJump(self):
        c = self.Squ1_SfxLenCounter
        if c == 0x25:
            self.Dump_Squ1_Regs(0x5f, 0xf6)
        elif c == 0x20:
            self.Dump_Squ1_Regs(0x48, 0xbc)
        self.DecrementSfx1Length()

    def PlayFireballThrow(self):
        self.Fthrow(0x05, 0x99)

    def PlayBump(self):
        self.Fthrow(0x0a, 0x93)

    def Fthrow(self, length, y):
        self.Squ1_SfxLenCounter = length
        self.PlaySqu1Sfx(0x0c, 0x9e, y)
        self.ContinueBumpThrow()

    def ContinueBumpThrow(self):
        if self.Squ1_SfxLenCounter == 0x06:
            self.w(0x4001, 0xbb)
        self.DecrementSfx1Length()

    def PlaySwimStomp(self):
        self.Squ1_SfxLenCounter = 0x0e
        self.PlaySqu1Sfx(0x26, 0x9e, 0x9c)
        self.ContinueSwimStomp()

    def ContinueSwimStomp(self):
        y = self.Squ1_SfxLenCounter
        self.w(0x4000, SwimStompEnvelopeData[y - 1])
        if y == 0x06:
            self.w(0x4002, 0x9e)
        self.DecrementSfx1Length()

    def PlaySmackEnemy(self):
        self.Squ1_SfxLenCounter = 0x0e
        self.PlaySqu1Sfx(0x28, 0x9f, 0xcb)
        self.DecrementSfx1Length()

    def ContinueSmackEnemy(self):
        if self.Squ1_SfxLenCounter == 0x08:
            self.w(0x4002, 0xa0)
            self.w(0x4000, 0x9f)
        else:
            self.w(0x4000, 0x90)                               # SmSpc
        self.DecrementSfx1Length()

    def DecrementSfx1Length(self):
        self.Squ1_SfxLenCounter = (self.Squ1_SfxLenCounter - 1) & 0xff
        if self.Squ1_SfxLenCounter == 0:
            self.StopSquare1Sfx()

    def StopSquare1Sfx(self):
        self.Square1SoundBuffer = 0
        self.w(0x4015, 0x0e)
        self.w(0x4015, 0x0f)

    def PlayPipeDownInj(self):
        self.Squ1_SfxLenCounter = 0x2f
        self.ContinuePipeDownInj()

    def ContinuePipeDownInj(self):
        c = self.Squ1_SfxLenCounter
        if not (c & 1) and not (c & 2) and (c & 8):
            self.PlaySqu1Sfx(0x44, 0x9a, 0x91)
        self.DecrementSfx1Length()                             # NoPDwnL

    # -- square 2 ----------------------------------------------------------------------------
    def Square2SfxHandler(self):
        if self.Square2SoundBuffer & 0x40:                     # Sfx_ExtraLife
            return self.ContinueExtraLife()
        q = self.Square2SoundQueue
        if q:
            self.Square2SoundBuffer = q
            if q & 0x80:
                return self.PlayBowserFall()
            for bit, f in ((0x01, self.PlayCoinGrab), (0x02, self.PlayGrowPowerUp),
                           (0x04, self.PlayGrowVine), (0x08, self.PlayBlast),
                           (0x10, self.PlayTimerTick), (0x20, self.PlayPowerUpGrab),
                           (0x40, self.PlayExtraLife)):
                if q & bit:
                    return f()
        b = self.Square2SoundBuffer                            # CheckSfx2Buffer
        if not b:
            return
        if b & 0x80:
            return self.ContinueBowserFall()
        for bit, f in ((0x01, self.ContinueCGrabTTick), (0x02, self.ContinueGrowItems),
                       (0x04, self.ContinueGrowItems), (0x08, self.ContinueBlast),
                       (0x10, self.ContinueCGrabTTick), (0x20, self.ContinuePowerUpGrab),
                       (0x40, self.ContinueExtraLife)):
            if b & bit:
                return f()

    def PlayCoinGrab(self):
        self.CGrab_TTickRegL(0x35, 0x8d)

    def PlayTimerTick(self):
        self.CGrab_TTickRegL(0x06, 0x98)

    def CGrab_TTickRegL(self, length, x):
        self.Squ2_SfxLenCounter = length
        self.PlaySqu2Sfx(0x42, x, 0x7f)
        self.ContinueCGrabTTick()

    def ContinueCGrabTTick(self):
        if self.Squ2_SfxLenCounter == 0x30:
            self.w(0x4006, 0x54)
        self.DecrementSfx2Length()

    def PlayBlast(self):
        self.Squ2_SfxLenCounter = 0x20
        self.PlaySqu2Sfx(0x5e, 0x9f, 0x94)                     # SBlasJ / PBFRegs
        self.DecrementSfx2Length()

    def ContinueBlast(self):
        if self.Squ2_SfxLenCounter == 0x18:
            self.PlaySqu2Sfx(0x18, 0x9f, 0x93)
        self.DecrementSfx2Length()

    def PlayPowerUpGrab(self):
        self.Squ2_SfxLenCounter = 0x36
        self.ContinuePowerUpGrab()

    def ContinuePowerUpGrab(self):
        c = self.Squ2_SfxLenCounter
        if not (c & 1):
            # PowerUpGrabFreqData-1,y: y up to $1b, inside the 27-byte table
            self.PlaySqu2Sfx(PowerUpGrabFreqData[(c >> 1) - 1], 0x5d, 0x7f)   # LoadSqu2Regs
        self.DecrementSfx2Length()

    def DecrementSfx2Length(self):
        self.Squ2_SfxLenCounter = (self.Squ2_SfxLenCounter - 1) & 0xff
        if self.Squ2_SfxLenCounter == 0:
            self.EmptySfx2Buffer()

    def EmptySfx2Buffer(self):
        self.Square2SoundBuffer = 0
        self.StopSquare2Sfx()

    def StopSquare2Sfx(self):
        self.w(0x4015, 0x0d)
        self.w(0x4015, 0x0f)

    def PlayBowserFall(self):
        self.Squ2_SfxLenCounter = 0x38
        self.PlaySqu2Sfx(0x18, 0x9f, 0xc4)                     # PBFRegs / LoadSqu2Regs
        self.DecrementSfx2Length()

    def ContinueBowserFall(self):
        if self.Squ2_SfxLenCounter == 0x08:
            self.PlaySqu2Sfx(0x5a, 0x9f, 0xa4)
        self.DecrementSfx2Length()

    def PlayExtraLife(self):
        self.Squ2_SfxLenCounter = 0x30
        self.ContinueExtraLife()

    def ContinueExtraLife(self):
        c = self.Squ2_SfxLenCounter
        if c & 7 == 0:                                         # DivLLoop: every 8 frames
            self.PlaySqu2Sfx(ExtraLifeFreqData[(c >> 3) - 1], 0x82, 0x7f)
        self.DecrementSfx2Length()

    def PlayGrowPowerUp(self):
        self.GrowItemRegs(0x10)

    def PlayGrowVine(self):
        self.GrowItemRegs(0x20)

    def GrowItemRegs(self, length):
        self.Squ2_SfxLenCounter = length
        self.w(0x4005, 0x7f)
        self.Sfx_SecondaryCounter = 0
        self.ContinueGrowItems()

    def ContinueGrowItems(self):
        self.Sfx_SecondaryCounter = (self.Sfx_SecondaryCounter + 1) & 0xff
        y = self.Sfx_SecondaryCounter >> 1
        if y == self.Squ2_SfxLenCounter:
            return self.EmptySfx2Buffer()                      # StopGrowItems
        self.w(0x4004, 0x9d)
        self.Dump_Freq_Regs(PUp_VGrow_FreqData[y], 0x4004)     # SetFreq_Squ2

    # -- noise -------------------------------------------------------------------------------
    def NoiseSfxHandler(self):
        q = self.NoiseSoundQueue
        if q:
            self.NoiseSoundBuffer = q
            if q & 1:
                return self.PlayBrickShatter()
            if q & 2:
                return self.PlayBowserFlame()
        b = self.NoiseSoundBuffer                              # CheckNoiseBuffer
        if b & 1:
            return self.ContinueBrickShatter()
        if b & 2:
            return self.ContinueBowserFlame()

    def PlayBrickShatter(self):
        self.Noise_SfxLenCounter = 0x20
        self.ContinueBrickShatter()

    def ContinueBrickShatter(self):
        c = self.Noise_SfxLenCounter
        if c & 1:
            y = c >> 1
            self.PlayNoiseSfx(BrickShatterEnvData[y], BrickShatterFreqData[y])
        self.DecrementSfx3Length()

    def PlayNoiseSfx(self, a, x):
        self.w(0x400c, a)
        self.w(0x400e, x)
        self.w(0x400f, 0x18)

    def DecrementSfx3Length(self):
        self.Noise_SfxLenCounter = (self.Noise_SfxLenCounter - 1) & 0xff
        if self.Noise_SfxLenCounter == 0:
            self.w(0x400c, 0xf0)
            self.NoiseSoundBuffer = 0

    def PlayBowserFlame(self):
        self.Noise_SfxLenCounter = 0x40
        self.ContinueBowserFlame()

    def ContinueBowserFlame(self):
        y = self.Noise_SfxLenCounter >> 1
        self.PlayNoiseSfx(BowserFlameEnvData[y - 1] if y else BEFORE_FLAME, 0x0f)
        self.DecrementSfx3Length()


# ---------------------------------------------------------------------------------------
# Running one effect
# ---------------------------------------------------------------------------------------

# name, NES channel (0 square 1, 1 square 2, 3 noise), queue, bit, options:
#   relative: pitch commands relative to the trigger note only (the patch transposes)
#   share:    no stream of its own: that effect's (relative) patch, triggered on its own note
#   cycle:    the patch is the first n frames (from the first write) only; the effect repeats
#             them and sound.c triggers the patch again each time (the 6502 restarts the
#             channel there too)
EFFECTS = [
    ("SmallJump",    0, "Square1SoundQueue", 0x80, {"relative": True}),
    ("BigJump",      0, "Square1SoundQueue", 0x01, {"share": "SmallJump"}),
    ("Bump",         0, "Square1SoundQueue", 0x02),
    ("EnemyStomp",   0, "Square1SoundQueue", 0x04),
    ("EnemySmack",   0, "Square1SoundQueue", 0x08),
    ("PipeDown",     0, "Square1SoundQueue", 0x10, {"cycle": 16}),
    ("Fireball",     0, "Square1SoundQueue", 0x20),
    ("Flagpole",     0, "Square1SoundQueue", 0x40),
    ("CoinGrab",     1, "Square2SoundQueue", 0x01),
    ("GrowPowerUp",  1, "Square2SoundQueue", 0x02),
    ("GrowVine",     1, "Square2SoundQueue", 0x04),
    ("Blast",        1, "Square2SoundQueue", 0x08),
    ("TimerTick",    1, "Square2SoundQueue", 0x10),
    ("PowerUpGrab",  1, "Square2SoundQueue", 0x20),
    ("ExtraLife",    1, "Square2SoundQueue", 0x40),
    ("BowserFall",   1, "Square2SoundQueue", 0x80),
    ("BrickShatter", 3, "NoiseSoundQueue",   0x01),
    ("BowserFlame",  3, "NoiseSoundQueue",   0x02),
    ("Pause",        0, "PauseSoundQueue",   0x01),
]


def run_effect(queue, bit, ch):
    """frames: per frame (freq, vol, duty/period) averaged over its 4 quarters; and the
    number of frames before the 6502's first register write (when the patch starts)"""
    apu = APU()
    eng = Engine(apu)
    setattr(eng, queue, bit)
    chan = apu.chans()[2 if ch == 3 else ch]
    frames, first_write = [], None
    for f in range(400):
        apu.quarter(True)                      # $4017 = $ff: quarter + half clock
        eng.writes = 0
        eng.SoundEngine()
        if eng.writes and first_write is None:
            first_write = f
        quarters = []
        for q in range(4):
            quarters.append(chan.sample())
            if q < 3:
                apu.quarter(q == 1)            # 1/4: quarter, 1/2: quarter + half, 3/4: quarter
        frames.append(quarters)
        busy = (eng.PauseSoundBuffer if queue == "PauseSoundQueue" else
                {0: eng.Square1SoundBuffer, 1: eng.Square2SoundBuffer,
                 3: eng.NoiseSoundBuffer}[ch])
        if not busy:
            break
    return frames, first_write or 0


def summarize(quarters, noise):
    """one frame: (freq, vol) with vol the mean of the 4 quarters (rounded), freq the mean of
    the audible quarters' (noise: the period index), duty of the first audible quarter"""
    aud = [q for q in quarters if q[1] > 0]
    vol = sum(q[1] for q in quarters) / 4.0
    if not aud:
        return (0.0, 0.0, quarters[0][2])
    if noise:
        return (aud[0][0], vol, aud[0][2])
    f = math.exp(sum(math.log(q[0]) for q in aud) / len(aud))
    return (f, vol, aud[0][2])


def midi(f):
    return 69 + 12 * math.log2(f / 440.0)


# ---------------------------------------------------------------------------------------
# The kernel side: what the patch commands do (kernel/uzeboxSoundEngine.c)
# ---------------------------------------------------------------------------------------

KERNEL = os.path.join(HERE, "..", "kernel")
STEPS = [int(w, 16) for w in re.findall(r"\.word\s+0x([0-9a-fA-F]+)",
                                        open(os.path.join(KERNEL, "data", "steptable.inc")).read())]
# order of the commands inside one frame: the pitch before a slide, the speed before a slide
ORDER = ["WAVE", "NOISE_PARAMS", "PITCH", "NOTE_UP", "NOTE_DOWN", "SLIDE_SPEED", "SLIDE", "ENV_VOL", "ENV_SPEED",
         "NOTE_CUT"]
WAVE_OF_DUTY = {0: 2, 1: 1, 2: 0, 3: 1}     # waves.inc: 0 square 50%, 1 25% (75% sounds the same), 2 12.5%
NOTE_VOL = 255                               # the TriggerNote volume sound.c passes
MASTER = 0x6f                                # DEFAULT_MASTER_VOL


def step_of(f):
    return f * 65536.0 / MIX_RATE


def midi_of_step(s):
    return 69 + 12 * math.log2(s * MIX_RATE / 65536.0 / 440.0)


def pulse_level(v):
    """the NES pulse DAC (one channel), 0-1 of volume 15"""
    return 0.0 if v <= 0 else (95.88 / (8128.0 / v + 100)) / (95.88 / (8128.0 / 15 + 100))


def noise_level(v):
    """the NES triangle/noise/DMC DAC with the noise alone, 0-1 of volume 15"""
    return 0.0 if v <= 0 else (159.79 / (12241.0 / v + 100)) / (159.79 / (12241.0 / 15 + 100))


def noise_divider(pidx):
    """Uzebox divider whose shift rate (15,734 / (d+1)) is nearest the NES period's"""
    rate = CPU / NOISE_PERIODS[pidx]
    return max(0, min(127, round(MIX_RATE / rate) - 1))


class KTrack:
    """a track of ProcessMusic(): envelope, slide, then the commands, then the volume"""

    def __init__(self, note):
        self.note = note
        self.step = STEPS[note]
        self.sliding = False
        self.slide_step = 0
        self.slide_note = 0
        self.slide_speed = None             # not reset by TriggerNote(): a patch sets its own
        self.env = 255
        self.env_step = 0
        self.wave = 0
        self.cut = False

    def copy(self):
        k = KTrack.__new__(KTrack)
        k.__dict__.update(self.__dict__)
        return k

    def frame(self, cmds):
        if self.env_step:
            self.env = max(0, min(255, self.env + self.env_step))
        if self.sliding:
            self.step = (self.step + self.slide_step) & 0xffff
            t = STEPS[self.slide_note]
            if (self.slide_step > 0 and self.step >= t) or (self.slide_step < 0 and self.step <= t):
                self.step = t
                self.sliding = False
        for c, p in cmds:
            self.command(c, p)
        return self.output()

    def command(self, c, p):
        if c == "ENV_SPEED":
            self.env_step = p
        elif c == "ENV_VOL":
            self.env = p
        elif c in ("WAVE", "NOISE_PARAMS"):
            self.wave = p
        elif c == "PITCH":
            self.note, self.step, self.sliding = p, STEPS[p], False
        elif c in ("NOTE_UP", "NOTE_DOWN"):                     # a slide, if any, runs on
            self.note += p if c == "NOTE_UP" else -p
            self.step = STEPS[self.note]
        elif c == "SLIDE_SPEED":
            self.slide_speed = p
        elif c == "SLIDE":
            cur, tgt = STEPS[self.note], STEPS[self.note + p]
            d = int((tgt - cur) / self.slide_speed)           # C division: towards zero
            if d == 0:
                d = 1
            self.step = (self.step + d) & 0xffff
            self.slide_step, self.slide_note, self.sliding = d, self.note + p, True
        elif c == "NOTE_CUT":
            self.cut = True
        else:
            raise ValueError(c)

    def output(self):
        """(step, mixer volume, wave or noise params) as the mixer gets them"""
        if self.cut or self.env == 0:
            return (self.step, 0, self.wave)
        u = (NOTE_VOL * 255 + 0x100) >> 8                       # note x track volume (an fx)
        u = (u * self.env + 0x100) >> 8                         # envelope
        u = (u * 255 + 0x100) >> 8                              # expression
        u = (u * MASTER + 0x100) >> 8                           # master
        return (self.step, u, self.wave)


# ---------------------------------------------------------------------------------------
# Encoding one effect
# ---------------------------------------------------------------------------------------

PITCH_TOL = 0.52            # semitones: the nearest note is never further than 0.5
VOL_TOL = 0.6               # NES volume steps
CMD_COST = 1.5              # frames of coverage a command must buy (the greedy's exchange rate)
SLIDE_SPEEDS = (1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64)


def targets(frames, noise):
    """per frame: (step or None, envelope 0-255 float, wave or noise params, NES volume)"""
    out = []
    for qs in frames:
        f, v, d = summarize(qs, noise)
        if noise:
            env = 255.0 * noise_level(v) if v else 0.0
            out.append((None, env, (noise_divider(d) << 1) | 1, v))
        else:
            env = 255.0 * pulse_level(v) if v else 0.0
            out.append((step_of(f) if f else None, env, WAVE_OF_DUTY[d], v))
    return out


def vol_ok(env_sim, tgt):
    if tgt == 0:
        return env_sim == 0
    return env_sim != 0 and abs(env_sim - tgt) <= VOL_TOL * 17


def pitch_ok(step_sim, tgt):
    return tgt is None or (step_sim > 0 and abs(12 * math.log2(step_sim / tgt)) <= PITCH_TOL)


def coverage(state, cmds, tg, f, check):
    """frames from f the state follows the targets, with cmds applied at frame f"""
    st = state.copy()
    n = 0
    for i in range(f, len(tg)):
        before = st.step
        st.frame(cmds if i == f else [])
        if not check(st, tg[i], before, tg[i - 1] if i else None):
            break
        n += 1
    return n


def pitch_shape_ok(st, t, before, tprev):
    """within the tolerance, and the same shape: a held NES pitch is held, a rising one does
    not fall, a falling one does not rise (else a slide passes for a run of notes)"""
    if t[1] == 0:
        return True
    if not pitch_ok(st.step, t[0]):
        return False
    if tprev is None or tprev[1] == 0 or tprev[0] is None:
        return True
    d = t[0] - tprev[0]
    if abs(d) < 1e-6 * t[0]:
        return st.step == before
    return (st.step - before) * d >= 0


def greedy(tg, state, candidates, check):
    """{frame: [commands]}: at each uncovered frame the candidate (a command list) with the
    best coverage less its cost"""
    out = {}
    f = 0
    while f < len(tg):
        best = None
        for cmds in candidates(state, f):
            n = coverage(state, cmds, tg, f, check)
            if n == 0:
                continue
            score = n - CMD_COST * len(cmds)
            if best is None or score > best[0]:
                best = (score, n, cmds)
        if best is None:
            raise RuntimeError(f"no command covers frame {f}")
        _, n, cmds = best
        if cmds:
            out[f] = list(cmds)
        for i in range(f, f + n):
            state.frame(cmds if i == f else [])
        f += n
    return out


def set_note(state, n, relative):
    """the command that sets note n: PITCH, or NOTE_UP/DOWN (relative to the trigger note,
    so the patch transposes; only when no slide runs, which they do not stop)"""
    if not relative:
        return [("PITCH", n)]
    if state.sliding or n == state.note:
        return None
    return [("NOTE_UP", n - state.note)] if n > state.note else [("NOTE_DOWN", state.note - n)]


def pitch_candidates(tg, relative):
    def cands(state, f):
        yield []
        t = tg[f][0]
        if t is None or tg[f][1] == 0:
            return
        m = midi_of_step(t)
        n0s = [n for n in range(int(m) - 1, int(m) + 3) if 0 <= n < len(STEPS)]
        sets = [(n, set_note(state, n, relative)) for n in n0s]
        sets = [(n, c) for n, c in sets if c]
        for n, c in sets:
            yield c
        # a slide from n0 (the current note, or one set now) to n1
        ahead = [midi_of_step(x[0]) for x in tg[f:f + 64] if x[0] is not None and x[1] > 0]
        if len(ahead) < 3:
            return
        lo, hi = int(min(ahead)) - 1, int(max(ahead)) + 2
        for n0, pre in [(state.note, [])] + sets:
            for n1 in range(max(0, lo), min(len(STEPS) - 1, hi) + 1):
                if n1 == n0 or abs(n1 - n0) > 48:
                    continue
                for s in SLIDE_SPEEDS:
                    sp = [] if s == state.slide_speed else [("SLIDE_SPEED", s)]
                    yield pre + sp + [("SLIDE", n1 - n0)]
    return cands


def vol_candidates(tg):
    def cands(state, f):
        yield []
        v = int(round(tg[f][1]))
        for s in [0] + [x for k in range(1, 80) for x in (k, -k)]:
            if s == state.env_step:
                yield [("ENV_VOL", v)]
            else:
                yield [("ENV_VOL", v), ("ENV_SPEED", s)]
                yield [("ENV_SPEED", s)]
    return cands


def start_state(tg, noise):
    """(trigger note, the track as TriggerNote() leaves it)"""
    if noise:
        init = KTrack(0)
        init.wave = 1                               # TriggerCommon: 15-bit LFSR, divider 0
        return None, init
    note = int(round(midi_of_step(next(x for x in tg if x[1] > 0)[0])))
    return note, KTrack(note)


def audible_targets(frames, noise):
    tg = targets(frames, noise)
    audible = [i for i, x in enumerate(tg) if x[1] > 0]
    return tg[:audible[-1] + 1]                     # cut after the last audible frame


def simulate(cmds, init, n):
    st = init.copy()
    return [st.frame(cmds.get(i, [])) for i in range(n)]


def encode(frames, ch, relative=False):
    """(commands by frame, trigger note, audible frames, the model's per-frame output, targets)"""
    noise = ch == 3
    tg = audible_targets(frames, noise)
    note, init = start_state(tg, noise)
    cmds = {}
    cur = init.wave                                 # waves / noise params where they change
    for i, x in enumerate(tg):
        if x[1] > 0 and x[2] != cur:
            cmds.setdefault(i, []).append(("NOISE_PARAMS" if noise else "WAVE", x[2]))
            cur = x[2]
    if not noise:
        pc = greedy(tg, init.copy(), pitch_candidates(tg, relative), pitch_shape_ok)
        for f, c in pc.items():
            cmds.setdefault(f, []).extend(c)
    vc = greedy(tg, init.copy(), vol_candidates(tg), lambda st, t, b, p: vol_ok(st.env, t[1]))
    for f, c in vc.items():
        cmds.setdefault(f, []).extend(c)
    cmds.setdefault(len(tg), []).append(("NOTE_CUT", 0))
    for f in cmds:
        cmds[f].sort(key=lambda c: ORDER.index(c[0]))
    return cmds, note, len(tg), simulate(cmds, init, len(tg) + 1), tg


def stream(cmds):
    """(delta, command, parameter) triples; the patch ends with 0, PATCH_END"""
    out, last = [], 0
    for f in sorted(cmds):
        for i, (c, p) in enumerate(cmds[f]):
            out.append((f - last if i == 0 else 0, "PC_" + c, p))
            last = f
    return out


def check(model, tg, noise):
    """worst pitch error (semitones) and volume error (NES steps at full scale) of the model"""
    pe, ve = 0.0, 0.0
    full = KTrack(60).output()[1]
    for (step, vol, w), t in zip(model, tg):
        ve = max(ve, abs(vol * 255.0 / full - t[1]) / 17.0)
        if t[1] > 0 and not noise and t[0]:
            pe = max(pe, abs(12 * math.log2(step / t[0])))
    return pe, ve


# ---------------------------------------------------------------------------------------

def main():
    verbose = "-v" in sys.argv
    jpath = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    lines = ["/* sfx.inc -- generated by tools/gen_sfx.py from your ROM: do not edit.",
             " * The sound effects of SMB as Uzebox patches, one delta tick a frame (sound.c",
             " * includes it). Above each: the NES effect, its length, the encoding's worst error. */",
             "#include <stddef.h>", ""]
    table, notes, defines, total, report, jdata = [], [], [], 0, [], {}
    encoded = {}
    for idx, (name, ch, queue, bit, *opt) in enumerate(EFFECTS):
        opt = opt[0] if opt else {}
        frames, start = run_effect(queue, bit, ch)
        played = frames[start:]
        if "cycle" in opt:                          # the rest repeats the first cycle
            n = opt["cycle"]
            assert all(played[i] == played[i % n] for i in range(len(played))), name
            played = played[:n]
        if "share" in opt:
            cmds = encoded[opt["share"]]
            tg = audible_targets(played, ch == 3)
            note, init = start_state(tg, ch == 3)
            length = max(cmds)
            assert len(tg) == length, name
            model = simulate(cmds, init, length + 1)
            st, size, stream_name = [], 0, opt["share"]
        else:
            cmds, note, length, model, tg = encode(played, ch, opt.get("relative", False))
            encoded[name] = cmds
            st = stream(cmds)
            size = 3 * len(st) + 2
            stream_name = name
        total += size
        pe, ve = check(model, tg, ch == 3)
        waves = sorted({c[1] for f in cmds.values() for c in f if c[0] == "WAVE"})
        report.append((name, ch, len(frames), start, length, size, pe, ve, note, waves,
                       sum(1 for f in cmds.values() for c in f if c[0] == "SLIDE")))
        what = f", its first {opt['cycle']} frames, repeated" if "cycle" in opt else ""
        shared = f"; the patch of {opt['share']}, from note {note}" if "share" in opt else ""
        lines.append(f"/* {name}: channel {ch}, {len(frames)} NES frames (first write at frame "
                     f"{start}){what}, {length} audible; worst error {pe:.2f} semitone, "
                     f"{ve:.2f} volume step{shared} */")
        if st:
            lines.append(f"static const char sfx_{name}[] PROGMEM = {{")
            row = "\t"
            for item in [f"{d},{c},{p}" for d, c, p in st] + ["0,PATCH_END"]:
                if len(row) + len(item) > 96:
                    lines.append(row.rstrip())
                    row = "\t"
                row += item + ", "
            lines.append(row.rstrip(", "))
            lines.append("};")
        table.append(f"\t{{{1 if ch == 3 else 0}, NULL, sfx_{stream_name}, 0, 0}},")
        notes.append(note if note is not None else idx)
        defines.append(f"#define SFX_{name.upper():14s}{idx}")
        jdata[name] = {"patch": idx, "channel": ch, "note": notes[-1], "nes_frames": len(frames),
                       "first_write": start, "cycle": opt.get("cycle", 0),
                       "model": [list(m) for m in model]}
        if verbose:
            print(f"{name}:")
            for i, m in enumerate(model):
                t = tg[i] if i < len(tg) else (None, 0, 0, 0)
                c = " ".join(f"{a}={b}" for a, b in cmds.get(i, []))
                tm = f"{midi_of_step(t[0]):6.2f}" if t[0] and t[1] else "   -  "
                mm = f"{midi_of_step(m[0]):6.2f}" if m[0] and m[1] and ch != 3 else "   -  "
                print(f"  {i:3d} nes {tm} v{t[3]:5.2f} | uze {mm} vol {m[1]:3d} w{m[2]:3d}  {c}")
    lines.append("")
    lines += defines
    lines.append(f"#define SFX_COUNT           {len(EFFECTS)}")
    lines.append("")
    lines.append("/* the patch table InitMusicPlayer() takes: type 1 for the noise channel */")
    lines.append("const struct PatchStruct sfx_patches[] PROGMEM = {")
    lines += table
    lines.append("};")
    lines.append("")
    lines.append("/* the note each patch is triggered with: its first pitch (noise: its own index,")
    lines.append(" * which TriggerNote() takes as the patch on channel 3) */")
    lines.append("static const u8 sfx_note[] PROGMEM = {" + ", ".join(map(str, notes)) + "};")
    open(OUT, "w", newline="\n").write("\n".join(lines) + "\n")
    print(f"{'effect':13s} ch  NES fr  start  audible  bytes  pitch err  vol err  note  waves  slides")
    for r in report:
        print(f"{r[0]:13s} {r[1]:2d}  {r[2]:6d}  {r[3]:5d}  {r[4]:7d}  {r[5]:5d}  {r[6]:9.2f}  "
              f"{r[7]:7.2f}  {str(r[8]):>4s}  {str(r[9]):6s} {r[10]}")
    print(f"patch data: {total} bytes; table {len(EFFECTS) * 9} bytes, notes {len(EFFECTS)} bytes")
    if jpath:
        json.dump(jdata, open(jpath, "w"))


if __name__ == "__main__":
    main()
