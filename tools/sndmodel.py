#!/usr/bin/env python3
"""sndmodel.py -- the reference for the sound engine (../sound.c, ../apu.c): the NES's.

Three things, all from your ROM's tables (tools/asmdata.py, by the disassembly's labels):
1. The tables the driver reads (music headers and data, periods, lengths, envelopes), with
   the 6502's layout kept: the driver reads across table ends on purpose.
2. The APU (gen_sfx.py's pulses and noise, a quarter frame at a time, plus the triangle and
   the DMC's level) and the 6502's whole SoundEngine: gen_sfx.py's port of the effects, and
   the music handler ported here line for line.
3. What the Uzebox mixer is set to each frame: the one definition of the volume tables, the
   step and the wave, which gen_snd.py writes for the C side and sndcheck compares with.

    run(script, frames) -> per frame, the four mixer channels
        script: {frame: [(name, value), ...]}, written before that frame's SoundEngine();
        names are the queues (Square1SoundQueue, ..., AreaMusicQueue, EventMusicQueue) and
        OperMode

A frame, as the NES ran it (SMB writes $4017 every NMI): quarter + half clock, the driver's
writes, then quarter, quarter + half, quarter. The mixer is set once a frame, from the APU's
state after the writes and the first quarter clock (apu.c's order: quarter, set the mixer,
quarter + half, quarter, quarter + half).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import asmdata  # noqa: E402
import gen_sfx  # noqa: E402
from gen_sfx import CPU, MIX_RATE, LENGTH_TABLE, NOISE_PERIODS  # noqa: E402

# ---------------------------------------------------------------------------------------
# The tables
# ---------------------------------------------------------------------------------------

DATA, ORDER = asmdata.parse()


def _span(first, last):
    """the labels from first up to (not including) last, in ROM order"""
    return ORDER[ORDER.index(first):ORDER.index(last)]


def music_tables():
    """(header region, music data, {label: offset in the music data}): MusicHeaderData (the
    offset table and the headers, as the ROM's bytes) with each header's data address replaced
    by the data's offset in the music array"""
    blob, at = [], {}
    for lab in _span("Star_CloudMData", "FreqRegLookupTbl"):
        at[lab] = len(blob)
        blob += [asmdata.value(t) for _, t in DATA[lab]]
    while blob and blob[-1] == 0xff:                    # the ROM's unused space after the songs
        blob.pop()
    hlabels = _span("MusicHeaderData", "Star_CloudMData")
    hat, n = {}, 0
    for lab in hlabels:
        hat[lab] = n
        n += len(DATA[lab])
    hdr = []
    for lab in hlabels:
        for _, t in DATA[lab]:
            if t.endswith("-MHD"):
                hdr.append(hat[t[:-4]])
            elif t[0] in "<>":
                hdr.append(at[t[1:]] & 0xff if t[0] == "<" else at[t[1:]] >> 8)
            else:
                hdr.append(asmdata.value(t))
    return hdr, blob, at


MusicHeaderData, MusicBlob, MusicAt = music_tables()
FreqRegLookupTbl = asmdata.bytes_of("FreqRegLookupTbl")
MusicLengthLookupTbl = asmdata.bytes_of("MusicLengthLookupTbl")
# the envelopes, one array in ROM order: the driver indexes past each table's end
ENV_LABELS = ["EndOfCastleMusicEnvData", "AreaMusicEnvData", "WaterEventMusEnvData",
              "BowserFlameEnvData"]
EnvData, EnvAt = [], {}
for _lab in ENV_LABELS:
    EnvAt[_lab] = len(EnvData)
    EnvData += asmdata.bytes_of(_lab)
BrickShatterEnvData = gen_sfx.BrickShatterEnvData       # 16 bytes (the vectors follow)
EnvAt["BrickShatterEnvData"] = len(EnvData)
EnvData += BrickShatterEnvData

DeathMusic, GameOverMusic, VictoryMusic, EndOfCastleMusic = 0x01, 0x02, 0x04, 0x08
TimeRunningOutMusic, Silence = 0x40, 0x80

# ---------------------------------------------------------------------------------------
# The mixer: what apu.c sets each frame
# ---------------------------------------------------------------------------------------

P15 = 90                                    # the mixer volume of a pulse at NES volume 15
STEP_K = round(CPU * 65536 / (16 * MIX_RATE))   # pulse step = STEP_K / (period + 1)


def _pulse_pp(v):
    return 95.88 / (8128.0 / v + 100) if v else 0.0


def _tnd_pp(v, weight):
    return 159.79 / (weight / v + 100) if v else 0.0


PULSE_VOL = [round(P15 * _pulse_pp(v) / _pulse_pp(15)) for v in range(16)]
NOISE_VOL = [round(P15 * _tnd_pp(v, 12241.0) / _pulse_pp(15)) for v in range(16)]
TRI_VOL = round(P15 * _tnd_pp(15, 8227.0) / _pulse_pp(15))
NOISE_PARAM = [(gen_sfx.noise_divider(i) << 1) | 1 for i in range(16)]
WAVE_OF_DUTY = {0: 2, 1: 1, 2: 0}           # waves.inc: 0 = 50%, 1 = 25%, 2 = 12.5%, 3 triangle
SNAP_MUL = round(256 * 65536 / STEP_K)      # samples a cycle, 8.8, of a period + 1
# Under 10.5 mixer samples a cycle (above 1.5 kHz) a pulse is played with the triangle's wave:
# a square's fifth harmonic no longer fits under the mixer's 7.9 kHz there, so what the NES
# played is close to a fundamental and a weak third, and the mixer's square is a tenth folded
# harmonics, which are not in tune (the 1-up's notes but the first; the power-up grab's top two)
SOFT_PERIOD = int(10.5 * STEP_K / 65536)    # periods under it


def pulse_step(period):
    """the mixer's step of a pulse. A pitch a hair off a whole number of mixer samples a cycle
    is played on it: the harmonics that fold back at 15.7 kHz then land on the real ones,
    where a hair off they sit a few tens of Hz beside them and beat (the coin's two notes, 16.04
    and 11.96 samples: 35 and 57 Hz). Within 0.5% (8 cents), above 440 Hz only."""
    d = period + 1
    if d < 256:
        x = d * SNAP_MUL
        n, dist = (x + 128) >> 8, x & 0xff
        if dist & 0x80:
            dist = 256 - dist
        if dist <= n + (n >> 2):
            return ((65536 - n + (n >> 1)) & 0xffff) // n + 1
    return (STEP_K // d) & 0xffff


def att(v, dmc):
    """the DMC's level lowers the triangle and the noise (about x0.72 at $30)"""
    return v - ((v * (dmc + (dmc >> 1))) >> 8)


def triangle_wave():
    """256 signed samples: the NES's 32 steps of 16 levels, started at mid level"""
    seq = list(range(15, -1, -1)) + list(range(16))
    seq = seq[8:] + seq[:8]                             # 7, 6, ... : a start next to zero
    out = []
    for v in seq:
        out += [round((v - 7.5) * 127 / 7.5)] * 8
    return out


# ---------------------------------------------------------------------------------------
# The APU: gen_sfx.py's, with the triangle and the DMC's level
# ---------------------------------------------------------------------------------------

class Triangle:
    def __init__(self):
        self.ctrl = self.reload_val = self.lin = self.period = self.length = 0
        self.reload = False
        self.enabled = False

    def write(self, reg, v):
        if reg == 0:
            self.ctrl, self.reload_val = v >> 7, v & 0x7f
        elif reg == 2:
            self.period = (self.period & 0x700) | v
        elif reg == 3:
            self.period = (self.period & 0xff) | ((v & 7) << 8)
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            self.reload = True

    def quarter(self):
        if self.reload:
            self.lin = self.reload_val
        elif self.lin:
            self.lin -= 1
        if not self.ctrl:
            self.reload = False

    def half(self):
        if self.length and not self.ctrl:
            self.length -= 1

    def sounding(self):
        return self.enabled and self.length > 0 and self.lin > 0


class APU(gen_sfx.APU):
    def __init__(self):
        super().__init__()
        self.tri = Triangle()
        self.dmc = 0

    def write(self, addr, v):
        if addr == 0x4015:
            for c, bit in ((self.sq1, 1), (self.sq2, 2), (self.tri, 4), (self.noise, 8)):
                c.enabled = bool(v & bit)
                if not c.enabled:
                    c.length = 0
        elif 0x4008 <= addr <= 0x400b:
            self.tri.write(addr - 0x4008, v)
        elif addr == 0x4011:
            self.dmc = v & 0x7f
        else:
            if addr in (0x4000, 0x4004):                # what apu.c leaves out
                assert not v & 0x20, "a pulse's envelope looped / length halted"
                assert v >> 6 != 3, "duty 75%"
            super().write(addr, v)

    def quarter(self, half):
        super().quarter(half)
        self.tri.quarter()
        if half:
            self.tri.half()

    def mixer(self):
        """the four mixer channels: pulses (volume, step, wave), the triangle (volume, step),
        the noise (volume, params); step and wave None where the volume is 0"""
        out = []
        for p in (self.sq1, self.sq2):
            if p.enabled and p.length and not p.muted():
                vol = PULSE_VOL[p.env.output()]
                out.append((vol, pulse_step(p.period),
                            3 if p.period < SOFT_PERIOD else WAVE_OF_DUTY[p.duty]))
            else:
                out.append((0, None, None))
        t = self.tri
        step = ((STEP_K // (t.period + 1)) & 0xffff) >> 1 if t.sounding() else 0
        out.append((att(TRI_VOL, self.dmc), step))
        n = self.noise
        if n.enabled and n.length:
            assert n.env.const, "the noise's decay envelope"
            out.append((att(NOISE_VOL[n.env.vol], self.dmc), NOISE_PARAM[n.pidx]))
        else:
            out.append((0, None))
        return out


# ---------------------------------------------------------------------------------------
# The 6502's SoundEngine: gen_sfx.py's effects, and the music (labels as in the disassembly)
# ---------------------------------------------------------------------------------------

class Driver(gen_sfx.Engine):
    def __init__(self, apu):
        super().__init__(apu)
        self.OperMode = 1                               # GameModeValue
        self.AreaMusicQueue = self.EventMusicQueue = 0
        self.AreaMusicBuffer = self.EventMusicBuffer = self.AreaMusicBuffer_Alt = 0
        self.MusicData = 0
        self.MusicOffset_Square2 = self.MusicOffset_Square1 = 0
        self.MusicOffset_Triangle = self.MusicOffset_Noise = self.NoiseDataLoopbackOfs = 0
        self.Squ2_NoteLenBuffer = self.Squ2_NoteLenCounter = self.Squ2_EnvelopeDataCtrl = 0
        self.Squ1_NoteLenCounter = self.Squ1_EnvelopeDataCtrl = 0
        self.Tri_NoteLenBuffer = self.Tri_NoteLenCounter = self.Noise_BeatLenCounter = 0
        self.NoteLenLookupTblOfs = self.NoteLengthTblAdder = 0
        self.GroundMusicHeaderOfs = self.AltRegContentFlag = self.DAC_Counter = 0

    def md(self, y):
        return MusicBlob[self.MusicData + y]

    # -- SoundEngine ---------------------------------------------------------------------
    def SoundEngine(self):
        if self.OperMode == 0:                          # the title screen: silence, queues kept
            self.w(0x4015, 0)
            return
        self.w(0x4015, 0x0f)                            # SndOn
        if self.PauseModeFlag or self.PauseSoundQueue == 1:
            self.InPause()
        else:                                           # RunSoundSubroutines
            self.Square1SfxHandler()
            self.Square2SfxHandler()
            self.NoiseSfxHandler()
            self.MusicHandler()
            self.AreaMusicQueue = self.EventMusicQueue = 0
        self.Square1SoundQueue = self.Square2SoundQueue = 0   # SkipSoundSubroutines
        self.NoiseSoundQueue = self.PauseSoundQueue = 0
        y = self.DAC_Counter
        if self.AreaMusicBuffer & 3:
            self.DAC_Counter += 1
            if y >= 0x30:
                self.DAC_Counter -= 1                   # NoIncDAC: y is not 0 here
        elif y:
            self.DAC_Counter -= 1
        self.w(0x4011, y)                               # StrWave

    # gen_sfx.py's InPause returns before DecPauC when nothing is queued, as the 6502 does
    # (beq SkipSoundSubroutines), and its SoundEngine is replaced by the one above.

    # -- the frequency registers, with the 6502's result: tone or not ----------------------
    def SetFreq(self, a, base):
        lo = FreqRegLookupTbl[a + 1]
        if lo == 0:
            return False                                # NoTone
        self.w(base + 2, lo)
        self.w(base + 3, FreqRegLookupTbl[a] | 0x08)
        return True

    # -- MusicHandler ----------------------------------------------------------------------
    def MusicHandler(self):
        if self.EventMusicQueue:
            return self.LoadEventMusic(self.EventMusicQueue)
        if self.AreaMusicQueue:
            return self.LoadAreaMusic(self.AreaMusicQueue)
        if self.EventMusicBuffer | self.AreaMusicBuffer:
            return self.HandleSquare2Music()            # ContinueMusic

    def LoadEventMusic(self, a):
        self.EventMusicBuffer = a
        if a == DeathMusic:
            self.StopSquare1Sfx()
            self.StopSquare2Sfx()
        self.AreaMusicBuffer_Alt = self.AreaMusicBuffer  # NoStopSfx
        self.NoteLengthTblAdder = 0
        self.AreaMusicBuffer = 0
        if a == TimeRunningOutMusic:
            self.NoteLengthTblAdder = 8
        self.FindHeader(a, 0)                           # FindEventMusicHeader

    def LoadAreaMusic(self, a):
        if a == 0x04:
            self.StopSquare1Sfx()
        self.GroundMusicHeaderOfs = 0x10                # NoStop1 / GMLoopB
        self.HandleAreaMusicLoopB(a)

    def HandleAreaMusicLoopB(self, a):
        while True:
            self.EventMusicBuffer = 0
            self.AreaMusicBuffer = a
            if a != 0x01:
                self.MusicOffset_Square2 = 8            # FindAreaMusicHeader (residual)
                return self.FindHeader(a, 8)
            self.GroundMusicHeaderOfs += 1
            y = self.GroundMusicHeaderOfs
            if y != 0x32:
                return self.LoadHeader(y)
            self.GroundMusicHeaderOfs = 0x11            # GMLoopB again

    def FindHeader(self, a, y):
        while True:                                     # FindEventMusicHeader
            y += 1
            c = a & 1
            a >>= 1
            if c:
                break
        self.LoadHeader(y)

    def LoadHeader(self, y):
        y = MusicHeaderData[y - 1]                      # MusicHeaderOffsetData,y
        h = MusicHeaderData
        self.NoteLenLookupTblOfs = h[y]
        self.MusicData = h[y + 1] | h[y + 2] << 8
        self.MusicOffset_Triangle = h[y + 3]
        self.MusicOffset_Square1 = h[y + 4]
        self.MusicOffset_Noise = self.NoiseDataLoopbackOfs = h[y + 5]
        self.Squ2_NoteLenCounter = self.Squ1_NoteLenCounter = 1
        self.Tri_NoteLenCounter = self.Noise_BeatLenCounter = 1
        self.MusicOffset_Square2 = 0
        self.AltRegContentFlag = 0
        self.w(0x4015, 0x0b)
        self.w(0x4015, 0x0f)
        self.HandleSquare2Music()

    def HandleSquare2Music(self):
        self.Squ2_NoteLenCounter = (self.Squ2_NoteLenCounter - 1) & 0xff
        if self.Squ2_NoteLenCounter == 0:
            a = self.md(self.MusicOffset_Square2)
            self.MusicOffset_Square2 = (self.MusicOffset_Square2 + 1) & 0xff
            if a == 0:                                  # EndOfMusicData
                a = self.EventMusicBuffer
                if a == TimeRunningOutMusic:
                    a = self.AreaMusicBuffer_Alt
                    if a:
                        return self.HandleAreaMusicLoopB(a)   # MusicLoopBack
                a &= VictoryMusic                       # NotTRO
                if a:
                    return self.LoadEventMusic(a)       # VictoryMLoopBack
                a = self.AreaMusicBuffer & 0x5f
                if a:
                    return self.HandleAreaMusicLoopB(a)
                self.AreaMusicBuffer = self.EventMusicBuffer = 0
                self.w(0x4008, 0)
                self.w(0x4000, 0x90)
                self.w(0x4004, 0x90)
                return
            if a & 0x80:                                # Squ2LengthHandler
                self.Squ2_NoteLenBuffer = self.ProcessLengthData(a)
                a = self.md(self.MusicOffset_Square2)
                self.MusicOffset_Square2 = (self.MusicOffset_Square2 + 1) & 0xff
            if not self.Square2SoundBuffer:             # Squ2NoteHandler
                if self.SetFreq(a, 0x4004):
                    a, x, y = self.LoadControlRegs()
                else:
                    a, x, y = 0, 0x04, a                # Rest: X from SetFreq_Squ2, Y the note
                self.Squ2_EnvelopeDataCtrl = a
                self.Dump_Sq2_Regs(x, y)
            self.Squ2_NoteLenCounter = self.Squ2_NoteLenBuffer   # SkipFqL1
        # MiscSqu2MusicTasks
        if not self.Square2SoundBuffer and not self.EventMusicBuffer & 0x91:
            y = self.Squ2_EnvelopeDataCtrl
            if y:
                self.Squ2_EnvelopeDataCtrl -= 1
            self.w(0x4004, self.LoadEnvelopeData(y))
            self.w(0x4005, 0x7f)
        self.HandleSquare1Music()

    def HandleSquare1Music(self):
        if self.MusicOffset_Square1:
            self.Squ1_NoteLenCounter = (self.Squ1_NoteLenCounter - 1) & 0xff
            misc = True
            if self.Squ1_NoteLenCounter == 0:
                while True:                             # FetchSqu1MusicData
                    a = self.md(self.MusicOffset_Square1)
                    self.MusicOffset_Square1 = (self.MusicOffset_Square1 + 1) & 0xff
                    if a:
                        break
                    self.w(0x4000, 0x83)
                    self.w(0x4001, 0x94)
                    self.AltRegContentFlag = 0x94
                x = a                                   # Squ1NoteHandler
                self.Squ1_NoteLenCounter = self.AlternateLengthHandler(a)
                if self.Square1SoundBuffer:
                    misc = False
                else:
                    note = x & 0x3e
                    if self.SetFreq(note, 0x4000):
                        a, x, y = self.LoadControlRegs()
                    else:
                        a, x, y = 0, 0x00, note         # X from SetFreq_Squ1
                    self.Squ1_EnvelopeDataCtrl = a      # SkipCtrlL
                    self.Dump_Squ1_Regs(x, y)
            if misc and not self.Square1SoundBuffer:    # MiscSqu1MusicTasks
                if not self.EventMusicBuffer & 0x91:
                    y = self.Squ1_EnvelopeDataCtrl
                    if y:
                        self.Squ1_EnvelopeDataCtrl -= 1
                    self.w(0x4000, self.LoadEnvelopeData(y))
                self.w(0x4001, self.AltRegContentFlag or 0x7f)   # DeathMAltReg
        self.HandleTriangleMusic()

    def HandleTriangleMusic(self):
        self.Tri_NoteLenCounter = (self.Tri_NoteLenCounter - 1) & 0xff
        if self.Tri_NoteLenCounter == 0:
            a = self.md(self.MusicOffset_Triangle)
            self.MusicOffset_Triangle = (self.MusicOffset_Triangle + 1) & 0xff
            note = True
            if a == 0:
                note = False                            # LoadTriCtrlReg with 0
            elif a & 0x80:
                self.Tri_NoteLenBuffer = self.ProcessLengthData(a)
                self.w(0x4008, 0x1f)
                a = self.md(self.MusicOffset_Triangle)
                self.MusicOffset_Triangle = (self.MusicOffset_Triangle + 1) & 0xff
                if a == 0:
                    note = False
            if not note:
                self.w(0x4008, 0)
            else:                                       # TriNoteHandler
                self.SetFreq(a, 0x4008)
                x = self.Tri_NoteLenBuffer
                self.Tri_NoteLenCounter = x
                if self.EventMusicBuffer & 0x6e or self.AreaMusicBuffer & 0x0a:
                    if x >= 0x12:                       # NotDOrD4
                        a = 0xff                        # LongN
                    elif self.EventMusicBuffer & EndOfCastleMusic:
                        a = 0x0f
                    else:
                        a = 0x1f                        # MediN
                    self.w(0x4008, a)                   # LoadTriCtrlReg
        self.HandleNoiseMusic()

    def HandleNoiseMusic(self):
        if not self.AreaMusicBuffer & 0xf3:
            return
        self.Noise_BeatLenCounter = (self.Noise_BeatLenCounter - 1) & 0xff
        if self.Noise_BeatLenCounter:
            return
        while True:                                     # FetchNoiseBeatData
            a = self.md(self.MusicOffset_Noise)
            self.MusicOffset_Noise = (self.MusicOffset_Noise + 1) & 0xff
            if a:
                break
            a = self.MusicOffset_Noise = self.NoiseDataLoopbackOfs
            if not a:
                break                                   # bne not taken: on with A = 0
        x = a                                           # NoiseBeatHandler
        self.Noise_BeatLenCounter = self.AlternateLengthHandler(a)
        y = self.last_len_index
        a = x & 0x3e
        if a == 0x30:
            a, x, y = 0x1c, 0x03, 0x58                  # LongBeat
        elif a == 0x20:
            a, x, y = 0x1c, 0x0c, 0x18                  # StrongBeat
        elif a and a & 0x10:
            a, x, y = 0x1c, 0x03, 0x18                  # short beat
        else:
            a = 0x10                                    # SilentBeat: X and Y as they were
        self.w(0x400c, a)                               # PlayBeat
        self.w(0x400e, x)
        self.w(0x400f, y)

    def AlternateLengthHandler(self, a):
        return self.ProcessLengthData((a & 1) << 2 | (a >> 7) << 1 | (a >> 6 & 1))

    def ProcessLengthData(self, a):
        y = (a & 7) + self.NoteLenLookupTblOfs + self.NoteLengthTblAdder
        self.last_len_index = y                         # the 6502's Y afterwards
        return MusicLengthLookupTbl[y]

    def LoadControlRegs(self):
        if self.EventMusicBuffer & EndOfCastleMusic:
            a = 0x04
        elif self.AreaMusicBuffer & 0x7d:
            a = 0x08
        else:
            a = 0x28                                    # WaterMus
        return a, 0x82, 0x7f

    def LoadEnvelopeData(self, y):
        if self.EventMusicBuffer & EndOfCastleMusic:
            return EnvData[EnvAt["EndOfCastleMusicEnvData"] + y]
        if self.AreaMusicBuffer & 0x7d:
            return EnvData[EnvAt["AreaMusicEnvData"] + y]
        return EnvData[EnvAt["WaterEventMusEnvData"] + y]


QUEUES = ("Square1SoundQueue", "Square2SoundQueue", "NoiseSoundQueue", "PauseSoundQueue",
          "AreaMusicQueue", "EventMusicQueue", "OperMode")


def run(script, frames):
    """[(mixer channels, driver state)] per frame; state: the two music buffers, for the caller"""
    apu = APU()
    drv = Driver(apu)
    out = []
    for f in range(frames):
        apu.quarter(True)                               # $4017 = $ff at the NMI
        for name, v in script.get(f, ()):
            assert name in QUEUES, name
            setattr(drv, name, v)
        drv.SoundEngine()
        apu.quarter(False)
        out.append((apu.mixer(), (drv.AreaMusicBuffer, drv.EventMusicBuffer)))
        apu.quarter(True)
        apu.quarter(False)
    return out


if __name__ == "__main__":
    print(f"music headers {len(MusicHeaderData)}, music data {len(MusicBlob)}, "
          f"envelopes {len(EnvData)}; STEP_K {STEP_K}")
    print("pulse volumes", PULSE_VOL)
    print("noise volumes", NOISE_VOL, "triangle", TRI_VOL)
    print("noise params ", NOISE_PARAM)
    import math
    print("pulse notes played on a whole number of samples (period: step, cents):")
    tbl = FreqRegLookupTbl
    for p in sorted({tbl[i] << 8 | tbl[i + 1] for i in range(0, len(tbl), 2) if tbl[i + 1]} | {0x54}):
        a, b = (STEP_K // (p + 1)) & 0xffff, pulse_step(p)
        if a != b:
            print(f"  {p:4d}: {a} -> {b} ({65536 / b:.2f} samples, {1200 * math.log2(b / a):+.1f} cents)")
