"""shortcall.py <file.s> -- <the assembler's command>: the game's own calls made short, in the
assembly of one of the game's objects (after default/tailjmp.py; the Makefile's SHORTCALL=1, mode
17's builds). `call f` (4 bytes, 4 cycles) becomes `rcall f` (2 bytes, 3 cycles) and `jmp f`
becomes `rjmp f` (2 bytes, 2 cycles for 3) where f is a FUNCTION OF THIS OBJECT that the
compiler wrote (a label in a .text.* section with .type @function, not weak, outside any hand
assembly) and the two ends are within RCALL's reach in the object's own layout. The engine's
own tail jumps are among them: an asm("jmp f") statement inside a C function (30 of them).

Never a call that leaves the object: so none to the kernel, the mode, libgcc or the C library
(tailjmp.py's DENY is kept besides), whose callers' 4-cycle CALL can be counted. Never an
instruction of assembly written at file scope (gcc's #APP to #NOAPP outside any function:
apu.c's ProcessMusic, which the kernel calls), never a call to a routine written there, never
an instruction that a `.+n` jumps over. This is not the linker's relaxation (-mrelax stays
forbidden): the vectors, every assembly file and every call between two objects are the bytes
they were.

How the reach is known: the file is assembled by the command given (with a listing), which
gives each call's place in its section and each function's; an object's .text.* sections are
linked back to back in the object's order (avr5's script: *(.text.*), and -Wa,-no-pad-sections),
less what --gc-sections drops, so the linked distance is never more than the object's. A call
made short brings others into reach: assembled again until nothing more changes. And the
linker refuses an RCALL out of reach ("relocation truncated to fit: R_AVR_13_PCREL"): a wrong
guess is a failed build, never a wrong jump.

shortcall.py --check <before.s> <after.s>: proves the second is the first with exactly those
changes and no other, by the text alone (the build runs it on every object).

shortcall.py --roms <a.elf> <a.map> <b.elf> <b.map> [--tails]: the proof on two linked ROMs,
both linked with -Wl,-q (the relocations kept: the .hex is the same). Routine by routine, b is
a with nothing but: a CALL made RCALL or a JMP made RJMP, to the start of a function of the
same object, one of the game's; and, with --tails (default/tailmerge.py's proof), instructions up
to a RET or a jump replaced by an RJMP to a place of the same object that holds those very
instructions (met from there to the first RET or jump; an RJMP met where the instructions
removed have none is a later cut of the kept tail itself, and is followed), and that do not
come right after a skip, nor after SEI or a write of SREG (the next instruction must follow
those at once). Every other byte is the same, every relocation names the same place (an
instruction by its number in its routine, not by its address). It reads the ELF files itself.
"""
import bisect
import collections
import os
import re
import struct
import subprocess
import sys

# tailjmp.py's list: the kernel's and the C library's names
DENY = re.compile(r'^(SpiRam|m16_|mem|str|__|Uart|ReadJoypad|WaitVsync|GetVsyncCounter|SetUserP)')
FOUR = ('call', 'jmp', 'lds', 'sts')                 # the instructions of 4 bytes
LONE_DOT = re.compile(r'(?<![\w.$])\.(?![\w.$])')    # the place itself, in an operand
SECTION = ('.section', '.text', '.data', '.bss')
ENGINE = ('smb_ram', 'bb', 'area', 'player', 'objects', 'enemies', 'bosses', 'collide', 'bgcoll',
          'sprites', 'gfx17', 'text', 'sound', 'apu', 'game', 'main17')


def kind(s):
    t = s.split(';')[0].split('/*')[0].strip()
    if not t:
        return 'blank', None
    if t.endswith(':'):
        return 'label', t
    if t.startswith('.') or re.match(r'^[\w.$]+\s*=', t):
        return 'directive', t
    return 'insn', t


def parse(lines):
    """a line each: (kind, text, the section it is in, hand assembly). Hand assembly, between
    gcc's #APP and #NOAPP, is 'stmt' (an asm statement inside a function the compiler wrote,
    between its label and its .size) or 'file' (written at file scope), False otherwise"""
    sec, hand, out, typed, inside = '.text', False, [], set(), None
    for s in lines:
        mark = s.strip()
        if mark == '/* #APP */':
            hand = 'stmt' if inside else 'file'
        elif mark == '/* #NOAPP */':
            hand = False
        k, t = kind(s)
        if k == 'label' and not hand and t[:-1] in typed:
            inside = t[:-1]
        if k == 'directive' and not hand:
            m = re.match(r'\.type\s+([\w.$]+)\s*,\s*@function$', t)
            if m:
                typed.add(m.group(1))
            m = re.match(r'\.size\s+([\w.$]+)\s*,', t)
            if m and m.group(1) == inside:
                inside = None
        if k == 'directive':
            d = t.split()[0]
            if d == '.section':
                sec = re.match(r'\.section\s+([^,\s]+)', t).group(1)
            elif d in ('.text', '.data', '.bss'):
                sec = d
            elif d in ('.pushsection', '.popsection', '.previous', '.subsection'):
                sys.exit('shortcall: %s: a directive this pass does not follow' % t)
        out.append((k, t, sec, hand))
    return out


def functions(P):
    """the functions the compiler wrote in this object"""
    typed, weak, here = set(), set(), set()
    for k, t, sec, hand in P:
        if k == 'directive':
            m = re.match(r'\.type\s+([\w.$]+)\s*,\s*@function$', t)
            if m and not hand:
                typed.add(m.group(1))
            m = re.match(r'\.weak\s+([\w.$]+)', t)
            if m:
                weak.add(m.group(1))
        elif k == 'label' and not hand and sec.startswith('.text.'):
            here.add(t[:-1])
    return set(f for f in typed & here if f not in weak and not DENY.match(f))


def size(t):
    return 4 if t.split()[0] in FOUR else 2


def pins(P):
    """the instructions no pass may change or remove: assembly written at file scope, and what
    is relative to its place: `.+n` with the n bytes it jumps over; any other use of the place
    pins its whole stretch of the section"""
    pin = set()
    for i, (k, t, sec, hand) in enumerate(P):
        if k != 'insn':
            continue
        if hand == 'file':
            pin.add(i)
            continue
        w = t.split(None, 1)
        if len(w) < 2 or not LONE_DOT.search(w[1]):
            continue
        pin.add(i)
        m = re.fullmatch(r'\.(?:\s*\+\s*(\d+))?', w[1].strip())
        if m:
            n, j = int(m.group(1) or 0), i + 1
            while n > 0 and j < len(P):
                if P[j][0] == 'insn':
                    pin.add(j)
                    n -= size(P[j][1])
                j += 1
        else:
            lo = hi = i
            while lo > 0 and not (P[lo][0] == 'directive' and P[lo][1].split()[0] in SECTION):
                lo -= 1
            while hi + 1 < len(P) and not (P[hi + 1][0] == 'directive' and P[hi + 1][1].split()[0] in SECTION):
                hi += 1
            pin.update(j for j in range(lo, hi + 1) if P[j][0] == 'insn')
    return pin


# ---- the ELF files, read here (an object's layout, a ROM's routines and relocations)
Sec = collections.namedtuple('Sec', 'name type addr off size link info')
Sym = collections.namedtuple('Sym', 'name value size type shndx')


class Elf:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        if d[:6] != b'\x7fELF\x01\x01':
            sys.exit('shortcall: %s is not a 32-bit little-endian ELF file' % path)
        shoff, = struct.unpack_from('<I', d, 0x20)
        entsize, num, strndx = struct.unpack_from('<HHH', d, 0x2e)
        raw = [struct.unpack_from('<10I', d, shoff + i * entsize) for i in range(num)]

        def cstr(tab, o):
            return d[tab + o:d.index(b'\0', tab + o)].decode()
        self.sections = [Sec(cstr(raw[strndx][4], r[0]), r[1], r[3], r[4], r[5], r[6], r[7]) for r in raw]
        self.symbols, self.relocs = [], {}
        for s in self.sections:
            if s.type == 2:                                   # SHT_SYMTAB
                tab = self.sections[s.link].off
                for o in range(s.off, s.off + s.size, 16):
                    name, value, sz, info, _, shndx = struct.unpack_from('<IIIBBH', d, o)
                    self.symbols.append(Sym(cstr(tab, name), value, sz, info & 15, shndx))
        for s in self.sections:
            if s.type == 4:                                   # SHT_RELA: (offset, type, symbol, addend)
                self.relocs[s.info] = [(o, i & 255, i >> 8, a) for o, i, a in
                                       struct.iter_unpack('<IIi', d[s.off:s.off + s.size])]

    def index(self, name):
        for i, s in enumerate(self.sections):
            if s.name == name:
                return i
        return None


def layout(src, cmd):
    """the object's .text.* sections back to back: each one's base, each symbol's (section,
    offset), and each source line's (offset in its section, bytes) from the assembler's listing"""
    obj, lst = src + '.lay.o', src + '.lay.lst'
    r = subprocess.run(cmd + ['-Wa,-aln=' + lst, src, '-o', obj], capture_output=True, text=True)
    if r.returncode:
        sys.exit('shortcall: the assembler failed on %s:\n%s' % (src, r.stderr))
    e = Elf(obj)
    base, at = {}, 0
    for s in e.sections:
        if s.name.startswith('.text.'):
            if s.name in base:
                sys.exit('shortcall: %s: two sections named %s' % (src, s.name))
            base[s.name] = at
            at += s.size
    syms = {}
    for y in e.symbols:
        if y.name and y.shndx < len(e.sections) and e.sections[y.shndx].name in base:
            syms[y.name] = (e.sections[y.shndx].name, y.value)
    where, twice = {}, set()
    for line in open(lst):
        m = re.match(r'\s*(\d+) ([0-9a-f]{4}) ((?:[0-9A-F]{2})+(?: (?:[0-9A-F]{2})+)*)', line)
        if m:
            i = int(m.group(1)) - 1
            if i in where:
                twice.add(i)
            else:
                where[i] = (int(m.group(2), 16), len(m.group(3).replace(' ', '')) // 2)
    os.remove(obj)
    os.remove(lst)
    return base, syms, where, twice


def reach(base, syms, where, twice, P, i, name):
    """the distance in words an RJMP or RCALL at line i would have to `name`, or None"""
    if i not in where or i in twice or name not in syms or P[i][2] not in base:
        return None
    tsec, toff = syms[name]
    return (base[tsec] + toff - (base[P[i][2]] + where[i][0] + 2)) // 2


def candidates(P):
    funcs, pin = functions(P), pins(P)
    out = []
    for i, (k, t, sec, hand) in enumerate(P):
        if k != 'insn' or i in pin or not sec.startswith('.text.'):
            continue
        w = t.split(None, 1)
        if len(w) == 2 and w[0] in ('call', 'jmp') and w[1].strip() in funcs:
            out.append(i)
    return out


def shorten(path, cmd):
    lines = open(path).read().split('\n')
    P = parse(lines)
    left, n = candidates(P), 0
    while left:
        base, syms, where, twice = layout(path, cmd)
        still = []
        for i in left:
            op, f = P[i][1].split(None, 1)
            if i in where and where[i][1] != 4:
                sys.exit('shortcall: %s line %d: %s is not 4 bytes in the listing' % (path, i + 1, P[i][1]))
            d = reach(base, syms, where, twice, P, i, f.strip())
            if d is not None and -2048 <= d <= 2047:
                lines[i] = lines[i].replace(op, 'r' + op, 1)
                n += 1
            else:
                still.append(i)
        if len(still) == len(left):
            break
        left = still
        open(path, 'w').write('\n'.join(lines))
    return n


def check(a, b):
    """b must be a with some `call f` made `rcall f`, some `jmp f` made `rjmp f`, f one of the
    functions the pass may reach"""
    if len(a) != len(b):
        sys.exit('shortcall --check: %d lines became %d' % (len(a), len(b)))
    P = parse(a)
    ok = set(candidates(P))
    n = 0
    for i, (x, y) in enumerate(zip(a, b)):
        if x == y:
            continue
        w = (P[i][1] or '').split(None, 1)
        if P[i][0] != 'insn' or len(w) != 2 or w[0] not in ('call', 'jmp'):
            sys.exit('shortcall --check: unexpected difference at %d: %r / %r' % (i + 1, x, y))
        if not x.lstrip().startswith(w[0]) or y != x.replace(w[0], 'r' + w[0], 1):
            sys.exit('shortcall --check: not the same call at %d: %r / %r' % (i + 1, x, y))
        if i not in ok:
            sys.exit('shortcall --check: line %d: %s is made short, and it is not a call to a function '
                     'of this object that the pass may reach' % (i + 1, P[i][1]))
        n += 1
    return n


# ---- two ROMs compared, routine by routine
R_7, R_13, R_16, R_16PM, R_CALL = 2, 3, 4, 5, 18
R_LDI = (6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 24, 25)
MASK = {R_7: 0xFC07, R_13: 0xF000, R_16: 0, R_16PM: 0, R_CALL: 0xFE0E}
MASK.update((t, 0xF0F0) for t in R_LDI)
Insn = collections.namedtuple('Insn', 'addr n sig cls tgt')


def map_text(mapfile):
    """from a link's map, what the output section .text is made of: (input section, address,
    bytes, the file it is from) each"""
    try:
        m = open(mapfile).read()
    except OSError as x:
        sys.exit('shortcall: the map %s cannot be read (%s)' % (mapfile, x.strerror))
    k = m.find('Linker script and memory map')
    a = re.search(r'^\.text ', m[k:], re.M) if k >= 0 else None      # the output section .text, to the next one
    if not a:
        sys.exit('shortcall: %s is not a link\'s map as this tool reads one (no memory map, or no .text in it)' % mapfile)
    m = m[k + a.start():]
    e = re.search(r'^\.(?!text )\w', m, re.M)
    m = m[:e.start()] if e else m
    return [(x.group(1), int(x.group(2), 16), int(x.group(3), 16), x.group(4))
            for x in re.finditer(r'^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$', m, re.M)]


class Rom:
    def __init__(self, elf, mapfile):
        e = self.e = Elf(elf)
        ti = e.index('.text')
        self.text = e.sections[ti]
        t = self.text
        self.code = e.d[t.off:t.off + t.size]
        if t.addr != 0:
            sys.exit('shortcall --roms: .text is not at 0')
        self.end = t.size
        self.rel = self.relocs(ti)
        if not self.rel:
            sys.exit('shortcall --roms: %s has no relocations: link it with -Wl,-q' % elf)
        # the routines: the symbols of .text that are not the compiler's local labels
        at = collections.defaultdict(list)
        for y in e.symbols:
            if y.shndx == ti and y.name and not y.name.startswith('.L') and y.type in (0, 1, 2) \
                    and y.value < self.end and '\1' not in y.name:      # L0^A: the assembler's own, for `.`
                at[y.value].append(y)
        at.setdefault(0, [])
        starts = sorted(at)
        self.routines = []                        # (name, start, size, is a function)
        for k, a in enumerate(starts):
            nxt = starts[k + 1] if k + 1 < len(starts) else self.end
            names = sorted(set(y.name for y in at[a])) or ['(start)']
            self.routines.append(('/'.join(names), a, nxt - a, any(y.type == 2 for y in at[a])))
        self.funcs = set(a for _, a, _, f in self.routines if f)
        # the objects, from the map
        spans = [(a, n, f) for _, a, n, f in map_text(mapfile) if n and a + n <= self.end]
        spans.sort()
        self.spans, self.span_at = spans, [s[0] for s in spans]

    def relocs(self, index):
        """offset -> (type, the address it names)"""
        out = {}
        for o, typ, sym, add in self.e.relocs.get(index, []):
            if typ:
                out[o] = (typ, self.e.symbols[sym].value + add)
        return out

    def obj(self, addr):
        k = bisect.bisect_right(self.span_at, addr) - 1
        if k < 0 or addr >= self.spans[k][0] + self.spans[k][1]:
            return None
        return self.spans[k][2]

    def word(self, a):
        return self.code[a] | self.code[a + 1] << 8

    def insn(self, a):
        w = self.word(a)
        call, jmp = (w & 0xFE0E) == 0x940E, (w & 0xFE0E) == 0x940C
        n = 4 if call or jmp or (w & 0xFC0F) == 0x9000 else 2
        sig, cls, tgt = [w] + ([self.word(a + 2)] if n == 4 else []), None, None
        for k in range(0, n, 2):
            if a + k in self.rel:
                typ, to = self.rel[a + k]
                if typ not in MASK:
                    sys.exit('shortcall --roms: relocation type %d at %#x: not one this proof knows' % (typ, a + k))
                sig[k // 2] &= MASK[typ]
                if typ == R_CALL:
                    sig[1] = 0
                if typ in R_LDI or typ in (R_16, R_16PM):
                    cls, tgt = 'r%d' % typ, to
        if call or jmp:
            cls, tgt = ('call' if call else 'jmp'), ((w >> 3 & 0x3E | w & 1) << 16 | self.word(a + 2)) * 2
            sig = [w & 0xFE0E, 0]
        elif (w & 0xE000) == 0xC000:
            k = w & 0xFFF
            cls, tgt, sig = ('rcall' if w & 0x1000 else 'rjmp'), a + 2 + 2 * (k - 4096 if k & 2048 else k), [w & 0xF000]
        elif (w & 0xF800) == 0xF000:
            k = w >> 3 & 0x7F
            cls, tgt, sig = 'br', a + 2 + 2 * (k - 128 if k & 64 else k), [w & 0xFC07]
        if cls in ('call', 'jmp', 'rcall', 'rjmp', 'br') and a in self.rel and self.rel[a][1] != tgt:
            sys.exit('shortcall --roms: at %#x the instruction goes to %#x, its relocation to %#x'
                     % (a, tgt, self.rel[a][1]))
        return Insn(a, n, tuple(sig), cls, tgt)

    def insns(self, start, n):
        out, a = [], start
        while a < start + n:
            out.append(self.insn(a))
            a += out[-1].n
        if a != start + n:
            sys.exit('shortcall --roms: the routine at %#x does not end on an instruction' % start)
        return out

    def run(self, a):
        """the instructions from a to the first that never goes on (RET, RETI, a jump)"""
        out = []
        while a < self.end:
            out.append(self.insn(a))
            if ends(self, out[-1]):
                return out
            a += out[-1].n
        sys.exit('shortcall --roms: no end to the instructions at %#x' % a)

    def masked(self, start, n):
        """a routine that did not change size: its bytes with the relocated fields out, and
        the relocations (offset in it, type, the address named)"""
        b, rl = bytearray(self.code[start:start + n]), []
        for o in range(start, start + n):
            if o in self.rel:
                typ, to = self.rel[o]
                if typ not in MASK:
                    sys.exit('shortcall --roms: relocation type %d at %#x: not one this proof knows' % (typ, o))
                w = (b[o - start] | b[o - start + 1] << 8) & MASK[typ]
                b[o - start], b[o - start + 1] = w & 255, w >> 8
                if typ == R_CALL:
                    b[o - start + 2] = b[o - start + 3] = 0
                rl.append((o - start, typ, to))
        return bytes(b), rl


def ends(rom, x):
    w = rom.word(x.addr)
    return x.cls in ('jmp', 'rjmp') or w in (0x9508, 0x9518, 0x9409, 0x9419)


def skips(rom, x):
    w = rom.word(x.addr)
    return (w & 0xFC00) == 0x1000 or (w & 0xFC08) == 0xFC00 or (w & 0xFD00) == 0x9900


def at_once(rom, x):
    """SEI (BSET 7), an OUT to SREG, an STS to SREG's address: the processor runs the next
    instruction before it takes an interrupt, so nothing may be put between the two"""
    w = rom.word(x.addr)
    return w == 0x9478 or (w & 0xFE0F) == 0xBE0F or (w & 0xFE0F) == 0x9200 and rom.word(x.addr + 2) == 0x5F


def follow(rom, a, was):
    """the instructions met from a to the first that never goes on. An RJMP met where `was`,
    the instructions removed, has one that is no jump is a later cut of the kept tail itself:
    it is followed"""
    out = []
    for _ in range(64):
        out += rom.run(a)
        k = len(out) - 1
        if out[k].cls == 'rjmp' and k < len(was) and was[k].cls not in ('rjmp', 'jmp'):
            a = out.pop().tgt
            continue
        return out
    sys.exit('shortcall --roms: the jumps from %#x never end' % a)


def engine(path):
    return path is not None and '(' not in path and os.path.basename(path)[:-2] in ENGINE


def roms(ea, ma, eb, mb, tails):
    A, B = Rom(ea, ma), Rom(eb, mb)
    bad = []

    def fail(s):
        bad.append(s)
        if len(bad) >= 20:
            sys.exit('shortcall --roms:\n  ' + '\n  '.join(bad))
    if [r[0] for r in A.routines] != [r[0] for r in B.routines]:
        sa, sb = [r[0] for r in A.routines], [r[0] for r in B.routines]
        sys.exit('shortcall --roms: not the same routines in the same order; only in the first: %s; '
                 'only in the second: %s' % (sorted(set(sa) - set(sb))[:12], sorted(set(sb) - set(sa))[:12]))
    # pass 1: each routine's instructions paired, and where b's addresses are in a
    same, pairs, short, merges, back = [], [], [], [], {}
    for (name, sa, na, fa), (_, sb, nb, fb) in zip(A.routines, B.routines):
        if na == nb:
            same.append((sb, nb, sa, name))
            continue
        if not fa or not engine(A.obj(sa)) or not engine(B.obj(sb)):
            sys.exit('shortcall --roms: %s changed size (%d > %d) and is not a function of the game\'s objects'
                     % (name, na, nb))
        xa, xb = A.insns(sa, na), B.insns(sb, nb)
        i = j = 0
        while i < len(xa) and j < len(xb):
            p, q = xa[i], xb[j]
            back[q.addr] = p.addr
            if p.sig == q.sig and p.cls == q.cls:
                pairs.append((name, p, q))
                i += 1
            elif (p.cls, q.cls) in (('call', 'rcall'), ('jmp', 'rjmp')):
                pairs.append((name, p, q))
                short.append((name, p, q))
                i += 1
            elif tails and q.cls == 'rjmp':
                run = follow(B, q.tgt, xa[i:])
                merges.append((name, xa[i:i + len(run)], q, run, xa[i - 1] if i else None))
                i += len(run)
            else:
                sys.exit('shortcall --roms: %s: at %#x / %#x the instructions differ' % (name, p.addr, q.addr))
            j += 1
        if i != len(xa) or j != len(xb):
            sys.exit('shortcall --roms: %s: one has instructions the other has not, from %#x / %#x'
                     % (name, xa[min(i, len(xa) - 1)].addr, xb[min(j, len(xb) - 1)].addr))
    same.append((B.end, 0x10000, A.end, '(after .text)'))      # .data's image follows .text
    same.sort()
    same_at = [s[0] for s in same]

    def canon(addr):
        """an address of b, as a's"""
        if addr >= 0x800000:
            return addr
        if addr in back:
            return back[addr]
        k = bisect.bisect_right(same_at, addr) - 1
        if k >= 0 and addr < same[k][0] + max(same[k][1], 1):
            return addr - same[k][0] + same[k][2]
        return None

    def alike(name, p, q, where):
        """q does what p does: the same instruction, or the same call made short"""
        if not (p.sig == q.sig and p.cls == q.cls) and (p.cls, q.cls) not in (('call', 'rcall'), ('jmp', 'rjmp')):
            fail('%s: %s at %#x / %#x: not the same instruction' % (name, where, p.addr, q.addr))
        elif p.tgt is not None and canon(q.tgt) != p.tgt:
            fail('%s: %s at %#x / %#x: one names %#x, the other %#x (%s of the first)'
                 % (name, where, p.addr, q.addr, p.tgt, q.tgt, canon(q.tgt) and hex(canon(q.tgt))))

    # pass 2: what each pair names
    for name, p, q in pairs:
        alike(name, p, q, 'an instruction')
    for name, p, q in short:
        oa, ot = A.obj(p.addr), A.obj(p.tgt)
        if p.tgt not in A.funcs or oa != ot or not engine(oa) or B.obj(q.addr) != B.obj(q.tgt):
            fail('%s: the %s at %#x was made short and goes to %#x: not the start of a function of the '
                 'same object of the game (%s to %s; in the second ROM %s to %s)'
                 % (name, p.cls, p.addr, p.tgt, oa, ot, B.obj(q.addr), B.obj(q.tgt)))
    gone = 0
    for name, was, q, run, before in merges:
        if len(was) != len(run) or len(run) < 2:
            fail('%s: the jump at %#x replaces %d instructions and its target has %d' % (name, q.addr, len(was), len(run)))
            continue
        if before is not None and skips(A, before):
            fail('%s: the tail removed at %#x follows a skip' % (name, was[0].addr))
        if before is not None and at_once(A, before):
            fail('%s: the tail removed at %#x follows SEI or a write of SREG, which the next instruction '
                 'must follow at once' % (name, was[0].addr))
        if skips(A, was[-2]):
            fail('%s: the tail removed at %#x ends after a skip' % (name, was[0].addr))
        if any(ends(A, p) for p in was[:-1]):
            fail('%s: the tail removed at %#x has a jump inside' % (name, was[0].addr))
        if B.obj(q.addr) != B.obj(q.tgt) or not engine(B.obj(q.addr)):
            fail('%s: the jump at %#x leaves its object' % (name, q.addr))
        for p, r in zip(was, run):
            alike(name, p, r, 'a tail kept at %#x, an instruction' % q.tgt)
        if not ends(A, was[-1]):
            fail('%s: the tail removed at %#x does not end in a RET or a jump' % (name, was[0].addr))
        gone += sum(p.n for p in was) - 2
    # the routines that kept their size, .data, and the sum
    for sb, nb, sa, name in same[:-1]:
        ba, ra = A.masked(sa, nb)
        bb, rb = B.masked(sb, nb)
        if ba != bb:
            fail('%s: its bytes differ' % name)
        elif [(o, t, canon(to)) for o, t, to in rb] != ra:
            fail('%s: a relocation names another place' % name)
    da, db = A.e.index('.data'), B.e.index('.data')
    sa, sb = A.e.sections[da], B.e.sections[db]
    ra, rb = A.relocs(da), B.relocs(db)
    xa, xb = bytearray(A.e.d[sa.off:sa.off + sa.size]), bytearray(B.e.d[sb.off:sb.off + sb.size])
    for o in set(ra) | set(rb):
        if ra.get(o, (0, 0))[0] not in (R_16, R_16PM) or o not in rb or rb[o][0] != ra[o][0] \
                or canon(rb[o][1]) != ra[o][1]:
            fail('.data: the relocation at %#x differs' % o)
        else:
            xa[o - sa.addr:o - sa.addr + 2] = xb[o - sb.addr:o - sb.addr + 2] = b'\0\0'
    if xa != xb or sa.addr != sb.addr:
        fail('.data differs')
    if A.end - B.end != 2 * len(short) + gone:
        fail('.text is %d bytes shorter, and the changes found account for %d' % (A.end - B.end, 2 * len(short) + gone))
    if bad:
        sys.exit('shortcall --roms:\n  ' + '\n  '.join(bad))
    by = collections.Counter(os.path.basename(A.obj(p.addr))[:-2] for _, p, _ in short)
    print('shortcall --roms: %d routines, %d bytes > %d; %d calls and jumps made short (%d bytes), '
          'each to a function of its own object; %d tails made a jump to the one kept (%d bytes); every other instruction, '
          'every relocation and .data the same' % (len(A.routines), A.end + sa.size, B.end + sb.size,
                                                   len(short), 2 * len(short), len(merges), gone))
    print('   by object: ' + ', '.join('%s %d' % kv for kv in by.most_common()))
    return len(short), len(merges)


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--check':
        n = check(open(a[1]).read().split('\n'), open(a[2]).read().split('\n'))
        print('shortcall --check: %s: %d calls and jumps made short, nothing else changed' % (a[2], n))
    elif a and a[0] == '--roms':
        roms(a[1], a[2], a[3], a[4], '--tails' in a)
    elif len(a) >= 3 and a[1] == '--':
        shorten(a[0], a[2:])
    else:
        sys.exit(__doc__)
