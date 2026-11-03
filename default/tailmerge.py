"""tailmerge.py <file.s> [--min n] [--dropped <file> <object>] -- <the assembler's command>: the
same tails kept once, in the assembly of one of the game's objects (after default/tailjmp.py,
before default/shortcall.py; the Makefile's TAILMERGE=1, mode 17's builds). A tail is two
instructions or more that end in a `ret`, a `jmp x` or an `rjmp x` and that nothing leaves
before that end. Where an object has the same tail in several places, one is kept, with a
label in front of it (.Ltailmerge<n>), and each of the others becomes `rjmp .Ltailmerge<n>`:
2 bytes for the tail's, and 2 cycles more where it is reached; 2 more again for each rjmp on
the way where the tail kept was itself cut later (a tail of tails: 4 cycles then; 5 of the 52
kept in the release build are such). --min: the least a place must gain, in bytes (2).

What a tail may hold, in the place removed and in the place kept alike: instructions the
assembler knows by name, that say nothing of their own place (no `.`, no `1b` or `1f`), no
jump but the last, no skip right before the last; between them nothing but comments, the
debugger's own labels and its .loc and .cfi lines. In the place removed there is no label
(what jumped to the tail's start still does: it finds the rjmp), and the instruction before
it is no skip, nor one that the next must follow at once: `sei`, `bset 7`, a write of SREG
(the processor runs one instruction more before it takes an interrupt: gcc's own write of the
stack pointer is `out __SREG__,r0` then `out __SP_L__,r28`, and there is `sei` then `sleep`;
held(), below, has the rule), nor one this pass does not know by name. Never anything of
assembly written at file scope (apu.c's ProcessMusic). An
rjmp this pass wrote ends a tail as any other does, so a tail of tails is kept once too.

Where a tail is kept: a jump to a section keeps that section linked, and a function nothing
calls, which the linker drops (--gc-sections: five of the game's), would stay for the tail it
holds. --dropped names a file of the sections the linker drops, a line each, "<object>
<section>" (the Makefile writes it from a link of the objects as they are before this pass,
with --print-gc-sections), and this object's name in it: a tail is never kept in one of those
for another section's sake. (The places of a dropped section may still jump to a tail kept
elsewhere: nothing follows a jump out of what is not linked.) What is linked is then the same
with the pass as without, and --roms checks it: the two ROMs have the same routines.
Without --dropped the pass has only the object to go by: a tail is then kept for another
section's sake only in a section that code or a table (.progmem.*) of another section of the
object names, which is fewer tails and no proof (what a miss costs is the dropped function's
bytes, never a wrong jump).

Why a jump it writes is in reach: the file is assembled once (shortcall.py's layout()) and
the distance from each place to the tail kept is measured in the object's own .text.*
sections, back to back as the linker puts them; a place out of RJMP's reach of the tail kept
is left as it was. Whatever this pass and shortcall.py do after that only removes bytes
between two places, and so does --gc-sections: the linked distance is never more than the
one measured. And the assembler (inside a section) or the linker (between two) refuses an
RJMP out of reach: a wrong measure is a failed build, never a wrong jump.

tailmerge.py --check <before.s> <after.s>: proves by the text alone that the second is the
first with exactly that: labels of this pass put in, and tails replaced by an rjmp to a label
that is followed, in the second file, by the very instructions removed (through this pass's
own rjmps where the tail kept was itself cut later). The build runs it on every object.

tailmerge.py --roms <a.elf> <a.map> <b.elf> <b.map>: shortcall.py's proof on two linked ROMs,
with the tails allowed: each jump's target holds exactly what was removed.

tailmerge.py --drops <list> <map> <the link's output> <object>...: writes the list --dropped
reads, from a link of those objects with -Map and --print-gc-sections. It is read twice: from
the map (each object's .text.* sections, by the object's own headers, that are not in the
map's .text) and from the linker's words ("removing unused section '<s>' in file '<f>'"),
and it is written only when the two agree: a linker that says it otherwise, or a map this
tool cannot read, stops the build; it never gives a shorter list in silence.

tailmerge.py --linked <list> <the ROM's map> <object>...: the ROM's own link confirms the list
the pass went by: what its map leaves out of those objects is the list exactly (a tail kept
in a function the list did not name would have kept it linked). The build runs it at the link.
"""
import collections
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shortcall as sc      # noqa: E402  (the parsing, the object's layout, the ROMs' proof)

MNEMONICS = set('''adc add adiw and andi asr bclr bld brbc brbs brcc brcs break breq brge brhc brhs
brid brie brlo brlt brmi brne brpl brsh brtc brts brvc brvs bset bst call cbi cbr clc clh cli cln clr
cls clt clv clz com cp cpc cpi cpse dec eicall eijmp elpm eor fmul fmuls fmulsu icall ijmp in inc jmp
lds ld ldd ldi lpm lsl lsr mov movw mul muls mulsu neg nop or ori out pop push rcall ret reti rjmp rol
ror sbc sbci sbi sbic sbis sbiw sbr sbrc sbrs sec seh sei sen ser ses set sev sez sleep spm st std sts
sub subi swap tst wdr xch'''.split())
SKIPS = ('cpse', 'sbrc', 'sbrs', 'sbic', 'sbis')
SREG_IO, SREG_AT, SREG_I = 0x3f, 0x5f, 7                      # SREG as a port, as an address; its I bit
NAME = re.compile(r'(?<![\w.$])[A-Za-z_.$][\w.$]*')
PLAIN = re.compile(r'[\da-fA-FxX()+\-*/%<>&|^~\s]+')
JUMPS = ('ret', 'reti', 'jmp', 'rjmp', 'ijmp', 'eijmp')         # nothing goes on after one
NUMERIC = re.compile(r'(?<![\w.$])\d+[bf](?![\w.$])')            # 1b, 1f: a place by its distance
OURS = re.compile(r'^\.Ltailmerge(\d+)$')
# what may stand between a tail's instructions: the debugger's labels and line marks (gcc -g
# writes them inside an asm statement too: a build with -g must decide as one without)
QUIET_LABEL = re.compile(r'^\.L(VL|BB|BE|BI|FB|FE|M|CFI|DL)\d+:$')
QUIET = re.compile(r'^\.(loc|file|cfi_\w+)\b')
LONGEST = 24


def norm(t):
    return re.sub(r'\s+', ' ', t)


def classify(k, t, sec, hand, pinned):
    """'i': an instruction a tail may hold (then its text, its mnemonic); 'x': one it may not;
    '-': a line that says nothing to the code; 'l': a label; 'd': any other directive"""
    if k == 'blank':
        return ('-',)
    if k == 'label':
        return ('-',) if QUIET_LABEL.match(t) else ('l',)
    if k == 'directive':
        return ('-',) if QUIET.match(t) else ('d',)
    w = t.split(None, 1)
    rest = w[1] if len(w) > 1 else ''
    if pinned or hand == 'file' or w[0] not in MNEMONICS or not sec.startswith('.text.') \
            or sc.LONE_DOT.search(rest) or NUMERIC.search(rest):
        return ('x',)
    return ('i', norm(t), w[0])


def constants(P):
    """the file's own names for numbers (gcc's `__SREG__ = 0x3f`, a .set, an .equ): name -> the
    number, or None where the file does not say one number for it"""
    out = {}
    for k, t, sec, hand in P:
        if k != 'directive':
            continue
        m = re.match(r'^([\w.$]+)\s*=\s*(.+)$', t) or re.match(r'^\.(?:set|equ|equiv)\s+([\w.$]+)\s*,\s*(.+)$', t)
        if m:
            v = number(m.group(2), out)
            out[m.group(1)] = v if out.get(m.group(1), v) == v else None
    return out


def number(s, equ):
    """an operand's value where the file itself says it, or None"""
    try:
        s = NAME.sub(lambda m: str(int(equ[m.group(0)])), s)        # an unknown name: KeyError
        return int(eval(s, {'__builtins__': {}})) if PLAIN.fullmatch(s) else None
    except Exception:
        return None


def held(t, equ):
    """why nothing may be put right after the instruction t (so no place cut may start there),
    or None. A skip: the jump would be what is skipped. And the instructions after which the
    processor runs one more before it takes an interrupt, which that one is written to be:
    SEI, BSET 7, a write of SREG (gcc's own write of the stack pointer is `out __SREG__,r0`
    then `out __SP_L__,r28`; `sei` then `sleep`). A port or a bit this pass cannot read is
    taken as SREG's; an STS is one only to SREG's address as a number (an STS to a name is a
    variable's: the linked address is --roms' to see). An instruction this pass does not know
    by name (a macro) may end in any of those"""
    w = t.split(None, 1)
    a = [x.strip() for x in (w[1] if len(w) > 1 else '').split(',')]
    if w[0] not in MNEMONICS:
        return 'an instruction this pass does not know (%s)' % w[0]
    if w[0] in SKIPS:
        return 'a skip'
    if w[0] == 'sei' or w[0] == 'bset' and number(a[0], equ) in (SREG_I, None) \
            or w[0] == 'out' and number(a[0], equ) in (SREG_IO, None) \
            or w[0] == 'sts' and number(a[0], equ) == SREG_AT:
        return 'an instruction that the next must follow at once (%s)' % norm(t)
    return None


def is_end(c):
    """the instruction that ends a tail: ret, jmp x, rjmp x (the compiler's, or this pass's)"""
    return c[0] == 'i' and (c[1] == 'ret' or c[2] == 'jmp'
                            or c[2] == 'rjmp')


class Entry:
    __slots__ = ('line', 'orig', 'c')

    def __init__(self, line, orig, c):
        self.line, self.orig, self.c = line, orig, c


def tails(E, equ):
    """every tail of the file: its text -> [(index of its first instruction, the indices of its
    instructions, may be removed)]"""
    table = collections.defaultdict(list)
    run, labelled, prev = [], 0, None       # the instructions since the last break; how many
    #                                         of them are before the last label; the one before
    before = {}                             # an instruction -> the instruction before it
    for i, e in enumerate(E):
        c = e.c
        if c[0] == '-':
            continue
        if c[0] == 'l':
            labelled = len(run)
            continue
        if c[0] == 'd':
            run, labelled = [], 0
            if e.line.split()[0] in sc.SECTION:
                prev = None
            continue
        before[i] = prev
        prev = i
        if c[0] == 'x':
            run, labelled = [], 0
            continue
        if is_end(c):
            if run and E[run[-1]].c[2] not in SKIPS:
                for n in range(2, min(len(run) + 1, LONGEST) + 1):
                    t = run[len(run) - (n - 1):] + [i]
                    p = before[t[0]]
                    cut = len(run) - (n - 1) >= labelled \
                        and (p is None or held(sc.kind(E[p].line)[1], equ) is None)
                    table[tuple(E[k].c[1] for k in t)].append((t[0], t, cut))
            run, labelled = [], 0
        elif c[2] in JUMPS:
            run, labelled = [], 0
        else:
            run.append(i)
    return table


def merge(path, cmd, least, dropped=None):
    lines = open(path).read().split('\n')
    if any('Ltailmerge' in s for s in lines):
        sys.exit('tailmerge: %s already has names of this pass' % path)
    P = sc.parse(lines)
    pin = sc.pins(P)
    equ = constants(P)
    base, syms, where, twice = sc.layout(path, cmd)
    E = [Entry(s, i, classify(P[i][0], P[i][1], P[i][2], P[i][3], i in pin)) for i, s in enumerate(lines)]

    def addr(e):
        i = e.orig
        if i is None or i not in where or i in twice or P[i][2] not in base:
            return None
        return base[P[i][2]] + where[i][0]
    # the sections that code of another section of this object names: a tail is kept for
    # another section's sake only in one of those. A jump to a section keeps it linked, and a
    # function nothing calls (the linker drops it: --gc-sections) would stay for its tail
    home = dict((t[:-1], sec) for k, t, sec, hand in P if k == 'label' and not t[0].isdigit())
    named = set()
    for k, t, sec, hand in P:
        if k in ('insn', 'directive') and sec.startswith(('.text.', '.progmem.')) \
                and t.split()[0] not in ('.type', '.size', '.global', '.globl', '.weak', '.section'):
            named.update(home[w] for w in re.findall(r'[\w.$]+', t) if w in home and home[w] != sec)
    count = places = 0
    while True:
        best = None
        for key, occ in tails(E, equ).items():
            if len(occ) < 2:
                continue
            gain = sum(sc.size(t) for t in key) - 2
            if gain < least:
                continue
            at = [addr(E[o[0]]) for o in occ]
            for k, keep in enumerate(occ):          # the place kept: the one most others reach
                if at[k] is None:
                    continue
                sec = P[E[keep[0]].orig][2]
                cut = [o for o, a in zip(occ, at) if o is not keep and o[2] and a is not None
                       and -2048 <= (at[k] - (a + 2)) // 2 <= 2047
                       and (P[E[o[0]].orig][2] == sec
                            or (sec in named if dropped is None else sec not in dropped))]
                score = (gain * len(cut), len(key), -keep[0])
                if cut and (best is None or score > best[0]):
                    best = (score, keep, cut)
        if best is None:
            break
        _, keep, cut = best
        label = '.Ltailmerge%d' % count
        count += 1
        kept, gone = E[keep[0]], set()
        for first, t, _ in cut:
            E[first].line = '\trjmp ' + label
            E[first].c = ('i', 'rjmp ' + label, 'rjmp')
            gone.update(t[1:])
            places += 1
        E = [e for i, e in enumerate(E) if i not in gone]
        # the label goes right before the kept tail's first instruction
        E.insert(next(i for i, e in enumerate(E) if e is kept), Entry(label + ':', None, ('l',)))
    if count:
        open(path, 'w').write('\n'.join(e.line for e in E))
    return count, places


def check(a, b):
    """b must be a with labels of this pass put in and tails replaced by an rjmp to one of them,
    each label followed in b by the very instructions removed"""
    Pa, Pb = sc.parse(a), sc.parse(b)
    pin = sc.pins(Pa)
    equ = constants(Pa)
    Ca = [classify(Pa[i][0], Pa[i][1], Pa[i][2], Pa[i][3], i in pin) for i in range(len(a))]
    pinb = sc.pins(Pb)
    Cb = [classify(Pb[i][0], Pb[i][1], Pb[i][2], Pb[i][3], i in pinb) for i in range(len(b))]
    for i, s in enumerate(a):
        if Pa[i][0] == 'label' and OURS.match(Pa[i][1][:-1]) or 'Ltailmerge' in s:
            sys.exit('tailmerge --check: the first file already has this pass\'s names (line %d)' % (i + 1))
    labels, cuts = {}, []
    i = j = 0
    while i < len(a) or j < len(b):
        if i < len(a) and j < len(b) and a[i] == b[j]:
            i += 1
            j += 1
            continue
        if j < len(b) and Pb[j][0] == 'label' and OURS.match(Pb[j][1][:-1]):
            if b[j] != Pb[j][1] or Pb[j][1][:-1] in labels or not Pb[j][2].startswith('.text.') \
                    or Pb[j][3] == 'file':
                sys.exit('tailmerge --check: the label at %d of the second file is not as the pass writes it' % (j + 1))
            labels[Pb[j][1][:-1]] = j
            j += 1
            continue
        m = re.match(r'^\trjmp (\.Ltailmerge\d+)$', b[j]) if j < len(b) else None
        if not m or i >= len(a) or Ca[i][0] != 'i':
            sys.exit('tailmerge --check: unexpected difference at %d/%d: %r / %r'
                     % (i + 1, j + 1, a[i] if i < len(a) else None, b[j] if j < len(b) else None))
        # a tail removed: from a[i] to its end; the lines that are no instruction stay
        k = i - 1
        while k >= 0 and Pa[k][0] != 'insn' and not (Pa[k][0] == 'directive' and Pa[k][1].split()[0] in sc.SECTION):
            k -= 1
        why = held(Pa[k][1], equ) if k >= 0 and Pa[k][0] == 'insn' else None
        if why:
            sys.exit('tailmerge --check: the tail removed at %d follows %s' % (i + 1, why))
        if not Pa[i][2].startswith('.text.'):
            sys.exit('tailmerge --check: a tail removed at %d, outside the code' % (i + 1))
        start, was = i, [Ca[i]]
        i += 1
        j += 1
        while not is_end(was[-1]):
            if i >= len(a):
                sys.exit('tailmerge --check: the tail removed at %d has no end' % (start + 1))
            if Ca[i][0] == 'i':
                if was[-1][2] in JUMPS:
                    sys.exit('tailmerge --check: the tail removed at %d has a jump inside' % (start + 1))
                was.append(Ca[i])
                i += 1
            elif Ca[i][0] == '-' and j < len(b) and a[i] == b[j]:
                i += 1
                j += 1
            else:
                sys.exit('tailmerge --check: the tail removed at %d: line %d may not be in a tail, or '
                         'did not stay (%r)' % (start + 1, i + 1, a[i]))
        if len(was) < 2 or was[-2][2] in SKIPS:
            sys.exit('tailmerge --check: the tail removed at %d is one instruction, or ends after a skip' % (start + 1))
        cuts.append((start, m.group(1), [c[1] for c in was]))
    # what each label is followed by, in the second file
    for start, label, was in cuts:
        if label not in labels:
            sys.exit('tailmerge --check: the tail removed at %d jumps to %s, which is not there' % (start + 1, label))
        got, j, hops = [], labels[label], 0
        while len(got) < len(was):
            j += 1
            if j >= len(b) or Cb[j][0] in ('x', 'd'):
                sys.exit('tailmerge --check: after %s, line %d of the second file may not be in a tail (%r)'
                         % (label, j + 1, b[j] if j < len(b) else None))
            if Cb[j][0] != 'i':
                continue
            m = OURS.match(Cb[j][1].split(None, 1)[1]) if Cb[j][2] == 'rjmp' else None
            if m and Cb[j][1] != was[len(got)]:          # the tail kept was cut later: followed
                hops += 1
                if m.group(0) not in labels or hops > len(labels):
                    sys.exit('tailmerge --check: after %s, a jump to nowhere' % label)
                j = labels[m.group(0)]
                continue
            if Cb[j][1] != was[len(got)]:
                sys.exit('tailmerge --check: the tail removed at %d was %r where %s has %r (line %d)'
                         % (start + 1, was[len(got)], label, Cb[j][1], j + 1))
            got.append(Cb[j][1])
    used = set(c[1] for c in cuts)
    if set(labels) != used:
        sys.exit('tailmerge --check: labels no jump goes to: %s' % sorted(set(labels) - used))
    return len(labels), len(cuts)


# ---- what the linker drops: the list the pass goes by, and its two proofs
SAID = re.compile(r"removing unused section '(\.text\.[^']*)' in file '([^']*)'")


def stem(path):
    return os.path.basename(path).split('.')[0]


def code_of(objs):
    """each object's .text.* sections that hold bytes: (the object's path, the section)"""
    return [(o.replace('\\', '/'), s.name) for o in objs for s in sc.Elf(o).sections
            if s.name.startswith('.text.') and s.type == 1 and s.size]


def left_out(mapfile, objs):
    """what a link left out of those objects' code, by its map: (object, section) for each
    section that is not in the map's .text"""
    inside = set((f.replace('\\', '/'), name) for name, _, n, f in sc.map_text(mapfile) if n)
    code = code_of(objs)
    for o in set(o for o, _ in code):
        if not any((o, s) in inside for p, s in code if p == o):
            sys.exit('tailmerge: the map %s has no code of %s: it is not the map of a link of that object'
                     % (mapfile, o))
    return set((stem(o), s) for o, s in code if (o, s) not in inside)


def read_list(path):
    try:
        rows = [ln.split() for ln in open(path) if ln.strip()]
    except OSError:
        sys.exit('tailmerge: %s, the list of what the linker drops, is not there (the Makefile writes it '
                 'from a link of the objects before this pass; make again)' % path)
    if any(len(r) != 2 or not r[1].startswith('.text.') for r in rows):
        sys.exit('tailmerge: %s is not a list of what the linker drops ("<object> <section>" a line)' % path)
    return set(tuple(r) for r in rows)


def drops(out, mapfile, log, objs):
    """the list, written only when the link's map and the linker's own words say the same"""
    by_map = left_out(mapfile, objs)
    code = set((stem(o), s) for o, s in code_of(objs))
    name = dict((o.replace('\\', '/'), stem(o)) for o in objs)
    try:
        text = open(log).read().replace('\\', '/')
    except OSError:
        sys.exit('tailmerge --drops: %s, the output of the link, is not there' % log)
    by_words = set((name[f], s) for s, f in SAID.findall(text) if f in name) & code
    if by_map != by_words:
        sys.exit('tailmerge --drops: what the linker drops of the game\'s objects is not known for sure: its '
                 'map (%s) says %d sections, its own words (--print-gc-sections, in %s) say %d.\n'
                 '   only by the map: %s\n   only by its words: %s\n'
                 '   (another linker\'s message, or a map this tool does not read: the list is not written)'
                 % (mapfile, len(by_map), log, len(by_words),
                    ' '.join('%s:%s' % x for x in sorted(by_map - by_words)[:8]) or 'none',
                    ' '.join('%s:%s' % x for x in sorted(by_words - by_map)[:8]) or 'none'))
    with open(out, 'w', newline='\n') as fh:
        fh.write(''.join('%s %s\n' % x for x in sorted(by_map)))
    return len(by_map)


def linked(listfile, mapfile, objs):
    """the ROM's own link confirms the list the pass went by"""
    want, got = read_list(listfile), left_out(mapfile, objs)
    if want != got:
        sys.exit('tailmerge --linked: the ROM (%s) does not leave out of the game\'s objects what the list the '
                 'tails were kept by (%s) says.\n   in the ROM, and dropped by the list: %s\n'
                 '   left out of the ROM, and not in the list: %s\n'
                 '   (a tail kept in a function nothing calls keeps it linked: the list was wrong, or the '
                 'pass. The list is made again at the next make)'
                 % (mapfile, listfile, ' '.join('%s:%s' % x for x in sorted(want - got)[:8]) or 'none',
                    ' '.join('%s:%s' % x for x in sorted(got - want)[:8]) or 'none'))
    return len(got)


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--check':
        n, m = check(open(a[1]).read().split('\n'), open(a[2]).read().split('\n'))
        print('tailmerge --check: %s: %d tails kept once, %d places a jump to one, nothing else changed' % (a[2], n, m))
    elif a and a[0] == '--roms':
        sc.roms(a[1], a[2], a[3], a[4], True)
    elif a and a[0] == '--drops' and len(a) > 4:
        n = drops(a[1], a[2], a[3], a[4:])
        print('tailmerge --drops: %s: %d sections of the game\'s objects dropped, by the map and by the linker\'s words alike' % (a[1], n))
    elif a and a[0] == '--linked' and len(a) > 3:
        n = linked(a[1], a[2], a[3:])
        print('tailmerge --linked: %s leaves out the %d sections of %s, and no other' % (a[2], n, a[1]))
    elif '--' in a and a.index('--') >= 1:
        k = a.index('--')
        least = int(a[a.index('--min') + 1]) if '--min' in a[:k] else 2
        dropped = None
        if '--dropped' in a[:k]:
            f, o = a[a.index('--dropped') + 1:a.index('--dropped') + 3]
            dropped = set(x[1] for x in read_list(f) if x[0] == o)
        merge(a[0], a[k + 1:], least, dropped)
    else:
        sys.exit(__doc__)
