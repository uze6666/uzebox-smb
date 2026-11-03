"""m17tiles.py -- mode 17's tiles from 8 x 8 pixels of 2 bits, and an iNES file's CHR-ROM.

A tile here is 8 rows of 8 values 0-3, the left pixel first (0 the backdrop, or transparent in
a sprite). What the mode stores:

- a flash (background) tile, 32 bytes: 8 lines of 4 bytes, a byte palette << 4 | pair code
  (tables.py): flash_tile();
- a sprite tile, 16 bytes: 8 lines of 2 bytes, a byte 4 pixels with the left one in bits 0-1:
  sprite_tile(); and its opaque box, 2 bytes: sprite_box().
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from tables import BGMASK4, CODE4, DEC4      # noqa: E402

EMPTY_BOX = (0xFF, 0xFF)


def read_ines_chr(path):
    """The two pattern tables of an iNES file's first 8 KB of CHR-ROM, each 256 tiles."""
    d = open(path, "rb").read()
    if d[:4] != b"NES\x1a":
        raise ValueError("%s is not an iNES file" % path)
    if d[5] < 1:
        raise ValueError("%s has no CHR-ROM (its tiles are in CHR-RAM)" % path)
    at = 16 + (512 if d[6] & 4 else 0) + d[4] * 16384
    tables = []
    for base in (0, 0x1000):
        tiles = []
        for t in range(256):
            o = at + base + 16 * t
            tiles.append([[((d[o + r] >> (7 - x)) & 1) | (((d[o + 8 + r] >> (7 - x)) & 1) << 1)
                           for x in range(8)] for r in range(8)])
        tables.append(tiles)
    return tables


def flash_tile(pix, palette):
    """32 bytes: the tile in a palette (0-14). A blank tile is all zeros whatever the palette."""
    assert 0 <= palette <= 14
    out = []
    for row in pix:
        for x in range(0, 8, 2):
            c = CODE4[(row[x], row[x + 1])]
            out.append(0 if (row[x], row[x + 1]) == (0, 0) else (palette << 4) | c)
    return out


def flash_tile_solid(b):
    """Whether a flash tile's 32 bytes have art in every pixel: a sprite behind the background
    shows nowhere in a cell of it (the compositor's m17_solid_lo, m17_solid_hi)."""
    return all(BGMASK4[x & 15] == 0xFF for x in b)


def solid_run_asm(tiles, lo, hi, per=16):
    """A tile table's bytes with the two labels round its solid run, tiles lo to hi - 1."""
    assert all(flash_tile_solid(t) for t in tiles[lo:hi]), "a tile of the run is not solid"
    part = lambda ts: asm_bytes([b for t in ts for b in t], per)    # noqa: E731
    out = []
    if lo:
        out.append(part(tiles[:lo]))
    out.append("; the tiles with art in every pixel that the compositor knows of: %d to %d (%d)" % (lo, hi - 1, hi - lo))
    out.append(".global m17_solid_lo\nm17_solid_lo:")
    if hi > lo:
        out.append(part(tiles[lo:hi]))
    out.append(".global m17_solid_hi\nm17_solid_hi:")
    if hi < len(tiles):
        out.append(part(tiles[hi:]))
    return "\n".join(out)


def longest_solid_run(tiles, first=0, end=None):
    """(lo, hi) of the longest run of solid tiles in a table as it stands, among the tiles
    first to end - 1 (a window's)."""
    best, at = (0, 0), None
    end = len(tiles) if end is None else min(end, len(tiles))
    for i, t in enumerate(list(tiles[:end]) + [None]):
        if t is not None and i >= first and flash_tile_solid(t):
            at = i if at is None else at
        else:
            if at is not None and i - at > best[1] - best[0]:
                best = (at, i)
            at = None
    return best


def flash_tile_pixels(b):
    """(pixels, palettes seen) of a flash tile's 32 bytes: flash_tile()'s inverse."""
    pix, pals = [], set()
    for r in range(8):
        row = []
        for i in range(4):
            v = b[4 * r + i]
            d = DEC4[v & 15]
            row += [d >> 4, d & 15]
            if d:
                pals.add(v >> 4)
        pix.append(row)
    return pix, pals


def sprite_tile(pix):
    """16 bytes: 8 lines of 2 bytes, a byte's pixels left to right in bits 0-1, 2-3, 4-5, 6-7."""
    out = []
    for row in pix:
        for x in (0, 4):
            out.append(row[x] | (row[x + 1] << 2) | (row[x + 2] << 4) | (row[x + 3] << 6))
    return out


def sprite_tile_pixels(b):
    return [[(b[2 * r + (x >> 2)] >> (2 * (x & 3))) & 3 for x in range(8)] for r in range(8)]


BOX_LAST = 0x80     # in a box's second byte: the tile's sprites are the compositor's last class


def sprite_box(pix, last=False):
    """2 bytes: first line | last line << 4, first column | last column << 4 of the opaque
    pixels; EMPTY_BOX for a tile with none. last: the sprites of this tile get their RAM tiles
    and their turn after the others (BOX_LAST)."""
    lines = [r for r in range(8) if any(pix[r])]
    cols = [x for x in range(8) if any(pix[r][x] for r in range(8))]
    if not lines:
        return EMPTY_BOX
    return (lines[0] | (lines[-1] << 4), cols[0] | (cols[-1] << 4) | (BOX_LAST if last else 0))


def asm_bytes(v, per=16):
    return "\n".join("\t.byte " + ", ".join("0x%02x" % x for x in v[i:i + per]) for i in range(0, len(v), per))


def check():
    import random
    rnd = random.Random(17)
    for _ in range(200):
        pix = [[rnd.randrange(4) for _ in range(8)] for _ in range(8)]
        p = rnd.randrange(15)
        back, pals = flash_tile_pixels(flash_tile(pix, p))
        assert back == pix and pals <= {p}
        assert sprite_tile_pixels(sprite_tile(pix)) == pix
    blank = [[0] * 8 for _ in range(8)]
    assert flash_tile(blank, 9) == [0] * 32 and sprite_box(blank) == EMPTY_BOX
    one = [[0] * 8 for _ in range(8)]
    one[2][5] = 3
    one[6][1] = 1
    assert sprite_box(one) == (2 | (6 << 4), 1 | (5 << 4))


if __name__ == "__main__":
    check()
    print("m17tiles.py: checked")
