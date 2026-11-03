#!/usr/bin/env python3
"""tables.py -- mode 17's pair codes: the two sequences, and the tables made from them.

    python tables.py        checks the tables, then writes ../m17_tables.inc and ../m17_tables.h

A tile byte shows 2 pixels, the renderer reading table[b] and table[b + 1]. So a byte is a
position in a cyclic sequence in which every ordered pair of values appears exactly once as two
neighbours (a de Bruijn sequence of order 2), and the table holds the sequence's colours:

- SEQ4, 16 long, over a palette's 4 values (0 the backdrop): a flash tile's low nibble. A flash
  tile's byte is palette << 4 | code, and its table T1 holds the palettes back to back,
  T1[16 p + c] = the colour of palette p's value SEQ4[c].
- SEQ15, 225 long, over the screen's 15 colour slots (0 the backdrop): a RAM tile's byte, and
  its table T2[c] = the colour of slot SEQ15[c].

Both start with 0. The second pixel of a sequence's last code is its first value, which the
renderer reads one entry further: in T1 that is the next palette's first entry and in T2 entry
225, and both hold the backdrop, so nothing wraps and no code needs a special case.

Left pixel first everywhere: a code's pixels are (SEQ[c], SEQ[c + 1]), a decoded byte is
left << 4 | right.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def debruijn(k, n):
    """The lexicographically least de Bruijn sequence B(k, n), as a list of k ** n values."""
    a = [0] * k * n
    seq = []

    def db(t, p):
        if t > n:
            if n % p == 0:
                seq.extend(a[1:p + 1])
        else:
            a[t] = a[t - p]
            db(t + 1, p)
            for j in range(a[t - p] + 1, k):
                a[t] = j
                db(t + 1, t)
    db(1, 1)
    return seq


VALUES = 4                      # a palette: the backdrop and 3 colours
SLOTS = 15                      # the screen's colours a RAM tile can show
SEQ4 = debruijn(VALUES, 2)
SEQ15 = debruijn(SLOTS, 2)
PAIRS4 = len(SEQ4)              # 16
PAIRS15 = len(SEQ15)            # 225
FLASH_PALETTES = 15             # palette 15's last code would be 0xFF, and X + 1 leave the page

# (left, right) -> code
CODE4 = {(SEQ4[c], SEQ4[(c + 1) % PAIRS4]): c for c in range(PAIRS4)}
CODE15 = {(SEQ15[c], SEQ15[(c + 1) % PAIRS15]): c for c in range(PAIRS15)}

# The tables the mode's code reads from flash
DEC4 = [(SEQ4[c] << 4) | SEQ4[(c + 1) % PAIRS4] for c in range(PAIRS4)]
DEC15 = [(SEQ15[c] << 4) | SEQ15[(c + 1) % PAIRS15] for c in range(PAIRS15)]
ENC15 = [CODE15.get((i >> 4, i & 15), 0) for i in range(256)]
# a flash tile's pair code -> which of its 2 pixels are background art (left the high nibble)
BGMASK4 = [(0xF0 if SEQ4[c] else 0) | (0x0F if SEQ4[(c + 1) % PAIRS4] else 0) for c in range(PAIRS4)]
# a sprite tile's nibble (left pixel in bits 0-1, right in bits 2-3) -> which pixels are opaque
SPRMASK = [(0xF0 if n & 3 else 0) | (0x0F if n >> 2 else 0) for n in range(16)]
# the first entry of a palette's 16 in T1 that holds value v: where a palette's colour is read back
T1_VALUE = [SEQ4.index(v) for v in range(VALUES)]
BGMASK4_AT = 0xF0               # m17_bgmask4's place in m17_dec15's page


def table1(palettes, backdrop):
    """T1 for a list of palettes (3 colours each): 16 entries a palette and the last one's spill."""
    t = []
    for pal in palettes:
        col = [backdrop] + list(pal)
        t += [col[v] for v in SEQ4]
    return t + [backdrop]


def table2(slots):
    """T2 for the screen's slot colours (slot 0 the backdrop; missing slots repeat it)."""
    col = list(slots) + [slots[0]] * (SLOTS - len(slots))
    return [col[v] for v in SEQ15] + [col[SEQ15[0]]]


def check():
    assert SEQ4[0] == 0 and SEQ15[0] == 0, "a sequence must start with the backdrop"
    assert len(CODE4) == VALUES ** 2 and len(CODE15) == SLOTS ** 2, "every pair once"
    assert 16 * (FLASH_PALETTES - 1) + PAIRS4 - 1 + 1 <= 0xFF, "T1's last read stays in its page"
    assert PAIRS15 - 1 + 1 <= 0xFF, "T2's last read stays in its page"
    # the renderer's two reads give the pair, for every code of every palette and of the screen
    pals = [(10 * p + 1, 10 * p + 2, 10 * p + 3) for p in range(FLASH_PALETTES)]
    t1 = table1(pals, 0)
    assert len(t1) == 16 * FLASH_PALETTES + 1
    for p in range(FLASH_PALETTES):
        col = [0] + list(pals[p])
        for (a, b), c in CODE4.items():
            byte = (p << 4) | c
            assert (t1[byte], t1[byte + 1]) == (col[a], col[b]), (p, a, b)
    slots = [0] + [100 + s for s in range(1, SLOTS)]
    t2 = table2(slots)
    assert len(t2) == PAIRS15 + 1
    for (a, b), c in CODE15.items():
        assert (t2[c], t2[c + 1]) == (slots[a], slots[b]), (a, b)
        assert DEC15[c] == (a << 4) | b and ENC15[(a << 4) | b] == c
    for (a, b), c in CODE4.items():
        assert DEC4[c] == (a << 4) | b
    assert T1_VALUE == [2 * v for v in range(VALUES)], "M17_T1_VALUE(v) is written as 2 * v"
    assert [CODE4[(0, v)] for v in range(1, VALUES)] == [2 * v - 1 for v in range(1, VALUES)], \
        "M17_PAIR0(v) is written as 2 * v - 1"


def emit():
    def rows(v, per=16):
        return "\n".join("\t.byte " + ", ".join("0x%02x" % x for x in v[i:i + per]) for i in range(0, len(v), per))
    tabs = (("m17_dec4", DEC4, "a flash tile's pair code -> its 2 values, left << 4 | right"),
            ("m17_bgmask4", BGMASK4, "a flash tile's pair code -> 0xF0 / 0x0F where its left / right pixel is art"),
            ("m17_sprmask", SPRMASK, "a sprite tile's nibble (left pixel in bits 0-1) -> 0xF0 / 0x0F where opaque"),
            ("m17_dec15", DEC15, "a RAM tile's code -> its 2 slots, left << 4 | right (its high nibble is SEQ15)"),
            ("m17_enc15", ENC15, "left << 4 | right -> a RAM tile's code (a slot of 15 does not exist: 0)"))
    what = {name: text for name, _, text in tabs}
    # All of it with the program's other flash data, which the linker packs and aligns as a
    # whole: a table of odd length among the code would leave what follows at an odd address.
    # The compositor's three share two pages, so it reads them with the index as the pointer's
    # low byte: enc15 a page, dec15 the next, bgmask4 at that page's 0xF0.
    assert len(ENC15) == 256 and len(DEC15) <= BGMASK4_AT and BGMASK4_AT + len(BGMASK4) == 256
    inc = ["; Mode 17's pair-code tables. Generated by generators/tables.py: do not edit.", "",
           "; The compositor's: two pages, read with the index as the pointer's low byte",
           ".section .progmem.data.m17_pairs", ".balign 256",
           "; " + what["m17_enc15"], ".global m17_enc15", "m17_enc15:", rows(ENC15),
           "; " + what["m17_dec15"], ".global m17_dec15", "m17_dec15:", rows(DEC15),
           "\t.fill %d, 1, 0" % (BGMASK4_AT - len(DEC15)),
           "; " + what["m17_bgmask4"] + ": at 0x%02x of m17_dec15's page" % BGMASK4_AT,
           ".global m17_bgmask4", "m17_bgmask4:", rows(BGMASK4), ""]
    for name, v, text in tabs:
        if name in ("m17_dec4", "m17_sprmask"):
            inc += ["; " + text, ".global " + name, ".section .progmem.data." + name, name + ":", rows(v), ""]
    with open(os.path.join(HERE, "..", "m17_tables.inc"), "w", newline="\n") as f:
        f.write("\n".join(inc))
    h = ["/* Mode 17's pair-code tables (m17_tables.inc). Generated by generators/tables.py: do not edit. */",
         "#pragma once", "",
         "#define M17_PAIRS4  %d      /* pair codes of a palette's 4 values: a flash tile's low nibble */" % PAIRS4,
         "#define M17_PAIRS15 %d     /* pair codes of the screen's 15 slots: a RAM tile's byte */" % PAIRS15,
         "#define M17_SLOTS   %d      /* the screen's colours a RAM tile can show, the backdrop included */" % SLOTS,
         "#define M17_T1_SIZE(palettes) (16 * (palettes) + 1)", "#define M17_T2_SIZE %d" % (PAIRS15 + 1),
         "#define M17_T1_VALUE(v) (2 * (v))   /* the first of a palette's 16 entries of T1 that holds its value v */",
         "#define M17_PAIR0(v) (2 * (v) - 1)  /* a palette's pair code for the backdrop then its value v (1-3) */",
         "#define M17_BGMASK4_AT 0x%02x        /* m17_bgmask4's place in m17_dec15's page */" % BGMASK4_AT,
         "",
         "#ifndef __ASSEMBLER__"]
    for name, v, what in tabs:
        h.append("extern const unsigned char %s[%d];   /* %s */" % (name, len(v), what))
    h += ["#endif", ""]
    with open(os.path.join(HERE, "..", "m17_tables.h"), "w", newline="\n") as f:
        f.write("\n".join(h))
    return sum(len(v) for _, v, _ in tabs)


if __name__ == "__main__":
    check()
    n = emit()
    print("tables.py: SEQ4 %s" % " ".join(str(v) for v in SEQ4))
    print("tables.py: checked; m17_tables.inc and m17_tables.h written, %d bytes of flash" % n)
