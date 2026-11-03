"""tailjmp.py <file.s>: the tail calls avr-gcc does not make under -mcall-prologues (it makes none
at all with that option), made in its assembly output: `call f` followed by `ret`, nothing between
them but comments and directives, becomes `jmp f`, and f returns straight to our caller. The ret
stays where a label could still reach it (no change then) or a skip instruction before the call
could jump to it. Never for f in the kernel or the C library: spiram.s's sequential reads count
cycles from their caller's 4-cycle call (DENY).

tailjmp.py --check <before.s> <after.s>: proves the second is the first with exactly those
changes and no other (the build runs it on every object)."""
import re
import sys

SKIPS = ('cpse', 'sbrc', 'sbrs', 'sbic', 'sbis')
# the labels gcc -g puts between a call and its ret (location lists, blocks, a function's end):
# as labels they hid the pair, and a ROM built with debug information was 180 bytes more
DEBUG_LABEL = re.compile(r'^\.L(VL|BB|BE|FB|FE|M)\d+:$')
DENY = re.compile(r'^(SpiRam|m16_|mem|str|__|Uart|ReadJoypad|WaitVsync|GetVsyncCounter|SetUserP)')


def kind(s):
    t = s.split(';')[0].split('/*')[0].strip()
    if not t:
        return 'blank', None
    if DEBUG_LABEL.match(t):
        return 'directive', t        # -g's marks: no jump goes to one
    if t.endswith(':'):
        return 'label', t
    if t.startswith('.'):
        return 'directive', t
    return 'insn', t


def transform(lines):
    out, n, i, prev = [], 0, 0, None
    while i < len(lines):
        k, t = kind(lines[i])
        if k == 'insn':
            op = t.split()[0]
            if op == 'call' and not DENY.match(t.split(None, 1)[1]):
                j = i + 1
                while j < len(lines) and kind(lines[j])[0] in ('blank', 'directive'):
                    j += 1
                if j < len(lines) and kind(lines[j]) == ('insn', 'ret'):
                    out.append(lines[i].replace('call', 'jmp', 1))
                    out.extend(lines[i + 1:j])
                    if prev in SKIPS:
                        out.append(lines[j])         # a skip may jump over the jmp, to the ret
                    else:
                        n += 1
                    prev = 'ret'
                    i = j + 1
                    continue
            prev = op
        elif k == 'label':
            prev = None
        out.append(lines[i])
        i += 1
    return out, n


def check(a, b):
    """b must be a with some `call f` (f not denied) + `ret` pairs turned into `jmp f`"""
    i = j = 0
    n = 0
    while i < len(a) or j < len(b):
        if i < len(a) and j < len(b) and a[i] == b[j]:
            i += 1
            j += 1
            continue
        ka, ta = kind(a[i])
        kb, tb = kind(b[j])
        if not (ka == kb == 'insn' and ta.split()[0] == 'call' and tb.split()[0] == 'jmp'
                and ta.split(None, 1)[1] == tb.split(None, 1)[1] and not DENY.match(ta.split(None, 1)[1])):
            sys.exit(f'tailjmp --check: unexpected difference at {i + 1}/{j + 1}: {a[i]!r} / {b[j]!r}')
        i += 1
        j += 1
        while i < len(a) and j < len(b) and a[i] == b[j] and kind(a[i])[0] in ('blank', 'directive'):
            i += 1
            j += 1
        if kind(a[i]) != ('insn', 'ret'):
            sys.exit(f'tailjmp --check: no ret after the call at {i + 1}')
        if j < len(b) and kind(b[j]) == ('insn', 'ret'):
            i += 1                               # the ret kept (after a skip)
            j += 1
        else:
            k = i - 1                            # the ret dropped: nothing may reach it
            while k >= 0 and kind(a[k])[0] in ('blank', 'directive'):
                k -= 1
            if kind(a[k])[0] != 'insn' or kind(a[k])[1].split()[0] != 'call':
                sys.exit(f'tailjmp --check: a label or another instruction before the ret at {i + 1}')
            k -= 1
            while k >= 0 and kind(a[k])[0] in ('blank', 'directive'):
                k -= 1
            if k >= 0 and kind(a[k])[0] == 'insn' and kind(a[k])[1].split()[0] in SKIPS:
                sys.exit(f'tailjmp --check: a skip before the call at {k + 1}, its ret dropped')
            i += 1
            n += 1
    return n


if sys.argv[1] == '--check':
    a = open(sys.argv[2]).read().split('\n')
    b = open(sys.argv[3]).read().split('\n')
    n = check(a, b)
    print(f'tailjmp --check: {sys.argv[3]}: {n} tail jumps, nothing else changed')
else:
    path = sys.argv[1]
    lines = open(path).read().split('\n')
    out, n = transform(lines)
    open(path, 'w').write('\n'.join(out))
