#!/usr/bin/env python3
"""gen_gfx17.py -- the graphics data of the port to video mode 17: ../data/gfxdata17.h and
../data/gfxtiles17.inc; and the same two for a ROM built with NESTITLE=1 (the .uze),
../data/gfxdata17n.h and ../data/gfxtiles17n.inc.

    python gen_gfx17.py <Super Mario Bros. .nes> [--all]

      --all    every glyph of the font and every metatile's tiles are stored, drawn or not
               (384 bytes more): what the generator did before it read the game's texts.
               The title's board then has no place: none is written

The pixels are the cartridge's CHR-ROM, read from the file named on the command line: **the
.nes is never copied into the repository**, and nothing here runs without it. What is drawn in
which palette comes from the game's own tables (the disassembly's metatiles, the font)
and from area_mtiles.txt, every metatile the area parser makes, area by area.

What it writes:
- gfxtiles17.inc (assembly): the background's flash tiles, 32 bytes each, a (tile, palette) the
  game draws once, the palette being in a flash tile's bytes; the 256 sprite tiles, 16 bytes
  each, and their opaque boxes (../customVideoMode17/generators/m17tiles.py);
- gfxdata17.h: the metatile tables as flash tile numbers, the font's, the two windows, the
  title's board (its cells as flash tiles), and the colour data.

**The title's board** (an ATTRACT=1 ROM alone has it): the cartridge's own cells and tiles,
read from the data its DrawTitleScreen uploads (read_board). Its tiles are the table's last,
after the font's own, inside the text window: no other
tile's number moves. Two of
its tiles are drawn as their nearest (BOARD_MERGE): the window has no place for more.

**The title as the cartridge has it** (NESTITLE=1: the .uze's build, which has the room): a
second set of files, the first left as it is. No tile of the board is drawn as another, and
the line under it is there, the copyright line: the name table's row 15, which the cartridge
draws in the board's palette (its attribute bytes give that row the palette of the board's
last), so each of its glyphs is a tile of its own, the font's being of another palette. 13
tiles more, and the table is then past the 256 that a text window ends at: the title gets a
window of its own further up (GFX17_BANK_TITLE; the mode's bank is any number: a flash
tile's address is 256 bytes on for each), the tiles its scene draws are ordered inside it,
and the board's cells are kept as the bytes VRAM holds through that window.

**What is not stored.** A glyph no text of the game draws: the texts are read from the sources
(every byte of text.c's tables, bgcoll.c's WarpZoneNumbers, the digits, and the symbols the
code names); its place in the font's table is the blank. And the tiles of a metatile that
neither a parser nor the game makes (area_mtiles.txt, GAME_WRITES): its table entry is the
blank.

**The windows.** A VRAM cell reaches 192 flash tiles from 8 x its section's bank, and the game
has more. The tiles are ordered so that two banks serve every scene:
- the level window, bank 0: every tile a metatile draws, and the blank;
- the text window: the font, the blank, and every tile of the areas that show text over the
  level (a castle's messages, a warp zone's welcome) or share their type (the underground's).
  The status bar's row and the text screens use it too.
The tiles only the other areas draw come first, outside the text window; the font's own come
last, outside the level window. Every area is checked against its window here, from
area_mtiles.txt: a scene that does not fit stops the generator with the tiles in excess.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
M17 = os.path.join(HERE, "..", "customVideoMode17", "generators")
sys.path.insert(0, HERE)
sys.path.insert(0, M17)
sys.dont_write_bytecode = True
import asmdata      # noqa: E402
import nespal       # noqa: E402
import m17tiles     # noqa: E402

OUT_H = os.path.join(HERE, "..", "data", "gfxdata17.h")
OUT_INC = os.path.join(HERE, "..", "data", "gfxtiles17.inc")
OUT_H_N = os.path.join(HERE, "..", "data", "gfxdata17n.h")       # ... for NESTITLE=1
OUT_INC_N = os.path.join(HERE, "..", "data", "gfxtiles17n.inc")
AREAS = os.path.join(HERE, "area_mtiles.txt")

WINDOW = 192                    # flash tiles a VRAM cell reaches (m17.def.h: indexes 64-255)
AREA_TYPES = ("water", "ground", "underground", "castle")
FONT_PAL = 2                    # the 6502 drew its text and the status bar in palette 2
COIN = (0x2e, 3)                # the bar's coin, in the palette that cycles
WATER_BLANK = (0x26, 2)         # a water area's blank cell (gfx.c's level_cell)
# The title's cursor, tile $ce beside the first player line (and beside the second): the
# cartridge's title data gives its cells the board's palette, not the text's (the name table's
# columns 8 and 9 there: build() reads it from the cartridge and stops if it is another)
CURSOR, CURSOR_PAL = 0xce, 1
CURSOR_CELLS = ((18, 9), (20, 9))       # the name table's (row, column)


def font_pair(t):
    """The (NES tile, palette) a glyph of the font is drawn as."""
    return (t, CURSOR_PAL if t == CURSOR else FONT_PAL)

# The metatiles the platform writes into the view as the game runs, which no parser makes:
# where the 6502 wrote the name table. A blank (a coin taken, a block destroyed, the bridge),
# the used block that replaces a bumped one ($c4), the pulleys' rope ($40, $44). Taken as
# possible in every area. (The block buffer also gets a bumped block's stand-in, $23, a grown
# vine's $26 and the cover at a vine's top, $08: the 6502 drew none of them, and on mode 17
# the view does not get them: bb.c, gfx17.c)
GAME_WRITES = (0x00, 0x40, 0x44, 0xc4)

# The font: NES tiles $00-$2f, and three symbols outside them. What the code names itself:
# the digits (the bar, the lives, the scores, the warp zone's numbers); the blank, the dash and
# the cross of the lives screen; the crown, the period, the title's cursor. And every letter:
# the lives screen prints the lives less 10 as a tile, as the 6502 did, so 20 lives are the
# crown and an A and 45 the crown and a Z. (Past that the 6502 printed the font's symbols and
# then pieces of scenery, tiles $25 and up: from 49 lives on mode 17 shows a blank there)
FONT_TABLE = set(range(0x30)) | {0x9f, 0xaf, 0xce}
FONT_CODE = set(range(0x24)) | {0x24, 0x28, 0x29, 0x9f, 0xaf, 0xce}

# 1-1's area (AreaAddrOffsets[0] = $25): the title of an ATTRACT=1 build is text over it
TITLE_AREA = (1, 5)

# The title's board (ATTRACT=1 alone). The 6502's DrawTitleScreen copies $13a bytes from CHR
# $1ec0 into its VRAM buffer: records of a name table address (high, low), a control byte (bit
# 7: down a column; bit 6: one byte repeated; bits 0-5: the count) and the data, a 0 ending
# them. The board is the name table's rows 4 to 14, columns 5 to 26, every cell of it written
# there, its palette the one the same data's attribute bytes give. Read from the cartridge,
# never from a table typed in.
TITLE_AT, TITLE_LEN = 0x1ec0, 0x13a
BOARD_ROW, BOARD_ROWS = 4, 11           # the name table's row of its first, and how many
BOARD_COL, BOARD_COLS = 5, 22
NT_PF_ROW = 4                           # the playfield's row 0 is the name table's row 4
# The board's tiles that are drawn as another, 2 pixels from it: the text window has no place
# for them (the user's word, 2026-10-08). The
# generator stops if either pair is further apart than MERGE_PIXELS, and if the board then
# needs more tiles than the window has left.
BOARD_MERGE = {0xd5: 0xd9, 0xe6: 0xdb}
MERGE_PIXELS = 2
# NESTITLE=1: the rows under the board that the title's data writes in the board's palette, and
# that are drawn with it: the copyright line, the name table's row 15 from column 13 on. A cell
# of such a row that the data does not write is not the title's (None: the level shows)
TITLE_MORE_ROWS = 1


def read_areas():
    """[(type, index, has a warp zone, metatiles)] from area_mtiles.txt."""
    if not os.path.exists(AREAS):
        sys.exit("gen_gfx17: %s is missing: it is part of the repository" % AREAS)
    out = []
    for ln in open(AREAS):
        p = ln.split()
        if p and p[0] == "A":
            out.append((int(p[1]), int(p[2]), p[3] == "1", {int(x, 16) for x in p[4:]} | set(GAME_WRITES)))
    assert len(out) == 34, "area_mtiles.txt: 34 areas expected"
    return out


def c_macros(path):
    """A header's `#define NAME a, b, c` lines: name -> [a, b, c] (textdata.h's messages)."""
    out = {}
    for m in re.finditer(r"^#define (\w+) (0x[0-9a-fA-F]+(?:, 0x[0-9a-fA-F]+)*)$", open(path).read(), re.M):
        out[m.group(1)] = [int(v, 0) for v in m.group(2).split(",")]
    return out


def c_array_bytes(path, only=None, macros=None):
    """The bytes of a C file's `u8 name[...] PROGMEM = { ... }` tables (only: one name); a
    table's macro is expanded from `macros`."""
    src = open(path).read()
    out = set()
    found = 0
    for m in re.finditer(r"const u8 (\w+)\[\d*\]\s*PROGMEM\s*=\s*\{([^}]*)\}", src):
        if only and m.group(1) != only:
            continue
        found += 1
        body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)
        for v in body.replace("\n", " ").split(","):
            v = v.strip()
            if v in (macros or {}):
                out.update(macros[v])
            elif v:
                out.add(int(v, 0))
    assert found, "%s: no table%s" % (path, " named " + only if only else "")
    return out


def font_used():
    """The NES tiles the game's texts draw: text.c's tables, the warp zone's numbers, and what
    the code names."""
    texts = c_macros(os.path.join(HERE, "..", "data", "textdata.h"))     # gen_text.py's, from the ROM
    used = c_array_bytes(os.path.join(HERE, "..", "text.c"), macros=texts) | FONT_CODE
    used |= c_array_bytes(os.path.join(HERE, "..", "bgcoll.c"), "WarpZoneNumbers",
                          c_macros(os.path.join(HERE, "..", "data", "romtables.h")))     # gen_tables.py's
    stray = sorted(used - FONT_TABLE)
    assert not stray, "a text names tiles outside the font: %s" % " ".join("%02x" % t for t in stray)
    return used


def metatile_pairs(mts, mt, water=False):
    """The (NES tile, palette) pairs the metatiles mts draw."""
    out = set()
    for m in mts:
        p, row = m >> 6, m & 0x3f
        assert 4 * row + 3 < len(mt[p]), "metatile %02x is past its table" % m
        for q in range(4):
            out.add((mt[p][4 * row + q], p))
    if water:
        out.add(WATER_BLANK)
    return out


def title_data(nes):
    """What the data DrawTitleScreen uploads writes: ({(row, column): tile}, {attribute byte's
    number: its value})."""
    d = open(nes, "rb").read()
    at = 16 + (512 if d[6] & 4 else 0) + d[4] * 16384 + TITLE_AT
    buf = d[at:at + TITLE_LEN]
    cells, attr = {}, {}
    i = 0
    while buf[i]:
        addr, ctl = buf[i] << 8 | buf[i + 1], buf[i + 2]
        n, step = ctl & 0x3f, 32 if ctl & 0x80 else 1
        i += 3
        for k in range(n):
            a = addr + k * step
            v = buf[i] if ctl & 0x40 else buf[i + k]
            assert 0x2000 <= a < 0x2400, "the title's data writes outside name table 0: %04x" % a
            if a < 0x23c0:
                cells[divmod(a - 0x2000, 32)] = v
            else:
                attr[a - 0x23c0] = v
        i += 1 if ctl & 0x40 else n
        assert i < TITLE_LEN, "the title's data has no end in its $%x bytes" % TITLE_LEN
    return cells, attr


def title_palette(attr, r, c):
    """The palette the title's attribute bytes give the name table's cell (r, c), None when the
    data does not write its byte."""
    a = 8 * (r // 4) + c // 4                       # the attribute byte of the cell's 32 x 32 pixels
    return (attr[a] >> (4 * ((r >> 1) & 1) + 2 * ((c >> 1) & 1))) & 3 if a in attr else None


def read_board(nes, more=0):
    """The title's board as the cartridge has it: BOARD_ROWS rows of BOARD_COLS (NES tile,
    palette), from the data DrawTitleScreen uploads; and `more` rows under it, a cell the data
    does not write being None there."""
    cells, attr = title_data(nes)
    rows = []
    for r in range(BOARD_ROW, BOARD_ROW + BOARD_ROWS + more):
        row = []
        for c in range(BOARD_COL, BOARD_COL + BOARD_COLS):
            if r >= BOARD_ROW + BOARD_ROWS and (r, c) not in cells:
                row.append(None)
                continue
            if (r, c) not in cells or title_palette(attr, r, c) is None:
                sys.exit("gen_gfx17: the title's data does not write the board's cell at row %d, column %d" % (r, c))
            row.append((cells[(r, c)], title_palette(attr, r, c)))
        rows.append(row)
    return rows


def board_layout(board, bg_chr, bytes_of, number, not_stored, window_end, merge):
    """(the board's new tiles in their order, its cells as flash tile numbers, the pixels the
    merges change): the new tiles are numbered from the table's end, which window_end bounds.
    merge: the tiles drawn as another (BOARD_MERGE, or none)."""
    used = {k[0] for row in board for k in row if k}

    def apart(a, b):
        return sum(1 for y in range(8) for x in range(8) if bg_chr[a][y][x] != bg_chr[b][y][x])
    for src, dst in sorted(merge.items()):
        if src not in used or dst not in used or dst in merge:
            sys.exit("gen_gfx17: BOARD_MERGE names tile %02x as %02x: both must be the board's, and the second drawn" % (src, dst))
        if apart(src, dst) > MERGE_PIXELS:
            sys.exit("gen_gfx17: the board's tiles %02x and %02x are %d pixels apart: no more than %d are merged"
                     % (src, dst, apart(src, dst), MERGE_PIXELS))
    n = len(number)
    number = dict(number)                                           # with the board's own
    tiles = []
    for k in sorted({(merge.get(k[0], k[0]), k[1]) for row in board for k in row if k}, key=lambda k: (k[1], k[0])):
        if k in not_stored:
            sys.exit("gen_gfx17: the board's tile %02x/%d is a pair the tables hold as not stored" % k)
        if bytes_of(k) not in number:
            number[bytes_of(k)] = n + len(tiles)
            tiles.append(bytes_of(k))
    if n + len(tiles) > window_end:
        sys.exit("gen_gfx17: the title's board needs %d new tiles, %d more than the text window has after the font (%d)"
                 % (len(tiles), n + len(tiles) - window_end, window_end - n))
    cells = [[number[bytes_of((merge.get(k[0], k[0]), k[1]))] if k else None for k in row] for row in board]
    pixels = sum(apart(k[0], merge[k[0]]) for row in board for k in row if k and k[0] in merge)
    return tiles, cells, pixels


# The sprites that go without first when a frame is short of RAM tiles (the mode's last class):
# the brick's chunks (sprites.c's DrawBrickChunks) and the floating scores (objects.c's
# FloateyNumTileData)
LAST_SPRITE_TILES = {0x84, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0x50, 0xfd, 0xfe}

# The sprite tile the Makefile names to the mode as empty (M17_SPR_NONE): build() holds it so
SPR_NONE = 0xfc


def build(nes, keep_all=False, nestitle=False):
    """Everything the writers need. nestitle: the files of a NESTITLE=1 ROM (the head's "The
    title as the cartridge has it")."""
    spr_chr, bg_chr = m17tiles.read_ines_chr(nes)
    mt = [asmdata.bytes_of("Palette%d_MTiles" % p) for p in range(4)]
    areas = read_areas()

    font_drawn = FONT_TABLE if keep_all else font_used()            # the glyphs that are stored
    font_nes = sorted(font_drawn)
    font_pairs = [font_pair(t) for t in font_nes] + [COIN]
    got = [title_palette(title_data(nes)[1], r, c) for (r, c) in CURSOR_CELLS]
    if got != [CURSOR_PAL] * len(CURSOR_CELLS):
        sys.exit("gen_gfx17: the title's data gives the cursor's cells palettes %s, not CURSOR_PAL (%d)" % (got, CURSOR_PAL))

    def bytes_of(pair):
        return tuple(m17tiles.flash_tile(bg_chr[pair[0]], pair[1]))
    blank_bytes = tuple([0] * 32)

    # every pair a table can name, and the ones something makes
    table_pairs = set()
    for p in range(4):
        table_pairs |= metatile_pairs({(p << 6) | r for r in range(len(mt[p]) // 4)}, mt)
    made = set().union(*[a[3] for a in areas])
    made_pairs = set()
    for (t, i, w, mts) in areas:
        made_pairs |= metatile_pairs(mts, mt, t == 0)
    never = sorted(m for p in range(4) for m in [(p << 6) | r for r in range(len(mt[p]) // 4)] if m not in made)
    stored_pairs = table_pairs if keep_all else made_pairs          # the metatiles' pairs that get a tile

    # the scenes of the text window: every area that shows text over the level, and its type's
    # others where the type decides (the port picks the bank from the area's pointer alone)
    text_types = {3}                                                # the castles' messages
    for (t, i, w, mts) in areas:
        if w and t != 1:
            text_types.add(t)                                       # the underground's warp zones
    text_areas = [a for a in areas if a[0] in text_types or a[2]]
    ground_text = sorted(a[1] for a in text_areas if a[0] == 1)

    def tiles_of(pairs):
        return {bytes_of(k) for k in pairs} - {blank_bytes}

    all_mt = tiles_of(stored_pairs)
    font = tiles_of(font_pairs)
    text_scene = set()
    for (t, i, w, mts) in text_areas:
        text_scene |= tiles_of(metatile_pairs(mts, mt, t == 0))
    title_scene = tiles_of(metatile_pairs([a for a in areas if (a[0], a[1]) == TITLE_AREA][0][3], mt))

    def layout(extra_text):
        """(order, level bank, text bank) or the reason there is none."""
        b = (text_scene | extra_text | (font & all_mt)) & (all_mt | font)
        b_mt = b & all_mt
        a = all_mt - b_mt
        f_only = font - all_mt
        n = len(a) + len(b_mt) + 1 + len(f_only)
        if len(a) + len(b_mt) + 1 > WINDOW:
            return None, "the level window: %d tiles and the blank" % (len(a) + len(b_mt))
        need = 8 * -(-(max(0, n - WINDOW)) // 8)
        if need > len(a):
            return None, ("the text window: %d tiles of %d must be outside it, %d are (the scenes' and the font's are %d)"
                          % (need, n, len(a), n - len(a)))
        # the window as far up as it goes: what is left of its 192 is after the font, for more
        return (a, b_mt, f_only, len(a) // 8), None

    # one order for both builds if the title's scene fits the text window too
    lay, why_title = layout(title_scene)
    title_fits = lay is not None
    if lay is None:
        lay, why = layout(set())
        if lay is None:
            sys.exit("gen_gfx17: the tiles do not fit two windows: %s" % why)
    a, b_mt, f_only, bank_text = lay

    # the order: by (palette, NES tile) of a tile's first pair, group by group
    first_pair = {}
    for k in sorted(stored_pairs | set(font_pairs) | {WATER_BLANK}, key=lambda k: (k[1], k[0])):
        first_pair.setdefault(bytes_of(k), k)
    key = lambda b: (first_pair[b][1], first_pair[b][0])
    # ... and the tiles with art in every pixel in one run, for the compositor (a sprite behind
    # the background shows nothing over one: m17_solid_lo, m17_solid_hi): the last of the level
    # window's own, the first of the two windows'
    solid = m17tiles.flash_tile_solid
    # NESTITLE=1: the board with every tile its own, and the rows under it. Its tiles are the
    # table's last and the table is then longer than a window is from the text window's start:
    # the title's window starts as far up as that takes (bank_title), and the tiles of the two
    # windows' that come to be under it must be ones the title's scene does not draw: solid
    # ones, the run of them being those tiles' first (b_first)
    board = read_board(nes, TITLE_MORE_ROWS if nestitle else 0)
    merge = {} if nestitle else BOARD_MERGE
    bank_title, b_first = bank_text, []
    if nestitle:
        board_new = {bytes_of(k) for row in board for k in row if k} - all_mt - font - {blank_bytes}
        n_all = len(a) + len(b_mt) + 1 + len(f_only) + len(board_new)
        bank_title = max(bank_text, -(-(n_all - WINDOW) // 8))
        under = max(0, 8 * bank_title - len(a))
        b_first = [b for b in sorted(b_mt, key=key) if solid(b) and b not in title_scene and b not in font][:under]
        if len(b_first) < under:
            sys.exit("gen_gfx17: the title's window (bank %d) leaves %d tiles of the two windows' under it, and only %d "
                     "solid ones are not the title's scene's" % (bank_title, under, len(b_first)))
    order = (sorted(a, key=lambda b: (solid(b), key(b))) +
             sorted(b_mt, key=lambda b: (not solid(b), b not in b_first, key(b))) +
             [blank_bytes] + sorted(f_only, key=key))
    solid_run = (len(a) - sum(1 for b in a if solid(b)), len(a) + sum(1 for b in b_mt if solid(b)))
    number = {b: n for n, b in enumerate(order)}
    blank = number[blank_bytes]
    # a pair that is not stored (a glyph no text draws, a metatile nothing makes) is the blank,
    # unless its tile is there for another's sake
    every = table_pairs | {font_pair(t) for t in FONT_TABLE} | {COIN, WATER_BLANK}
    index = {k: number.get(bytes_of(k), blank) for k in every}
    not_stored = sorted(k for k in every if bytes_of(k) not in number)
    n = len(order)
    assert n <= 256

    # every area against its window, every text scene and the bar against theirs
    lvl = range(0, WINDOW)
    txt = range(8 * bank_text, 8 * bank_text + WINDOW)
    assert blank in lvl and blank in txt
    for (t, i, w, mts) in areas:
        is_text = t in text_types or w
        win = txt if is_text else lvl
        for k in metatile_pairs(mts, mt, t == 0):
            assert index[k] in win, "area %s %d: tile %02x/%d is flash tile %d, outside its window" % (AREA_TYPES[t], i, k[0], k[1], index[k])
        if is_text:
            for k in font_pairs:
                assert index[k] in txt
    for k in font_pairs:
        assert index[k] in txt, "the font's tile %02x is outside the text window" % k[0]

    # The title's board (ATTRACT=1): its tiles after every other, so that no tile's number is
    # another with it than without, and the ATTRACT=0 ROM is the bytes it was. The compositor's
    # run of solid tiles ends before the blank: a board tile with art in every pixel is outside
    # it, and is taken as not solid (m17_pass1.s compares a tile's address with the run's two
    # labels; m17model.py's compose() the same): a sprite behind it is composed for nothing
    # shown, which is right and only slower.
    # (--all stores every glyph, and the window then has no place for the board: none is made,
    # and an ATTRACT=1 build of such files stops at gfxdata17.h)
    board_tiles, board_cells, merged_pixels = [], None, 0
    if not keep_all:
        # (the title's window: the text window, or with NESTITLE=1 its own)
        ttl = range(8 * bank_title, 8 * bank_title + WINDOW)
        board_tiles, board_cells, merged_pixels = board_layout(
            board, bg_chr, bytes_of, number, not_stored, ttl.stop, merge)
        # the title's own scene: 1-1, the font and the board, every tile of them in the text window
        if not title_fits:
            sys.exit("gen_gfx17: the title's scene does not fit the text window: %s" % why_title)
        for k in metatile_pairs([a for a in areas if (a[0], a[1]) == TITLE_AREA][0][3], mt):
            assert index[k] in ttl, "the title's scene: tile %02x/%d is flash tile %d, outside the text window" % (k[0], k[1], index[k])
        assert all(c in ttl for row in board_cells for c in row if c is not None)
        assert n + len(board_tiles) <= (ttl.stop if nestitle else 256)
        if nestitle:
            assert blank in ttl and all(index[k] in ttl for k in font_pairs), "the font is outside the title's window"

    per_type = []
    for t in range(4):
        s = set()
        for (tt, i, w, mts) in areas:
            if tt == t:
                s |= tiles_of(metatile_pairs(mts, mt, t == 0))
        per_type.append(s)
    dead = tiles_of(table_pairs) - tiles_of(made_pairs) - font     # tiles only a never-made metatile names

    # The engine's blank sprite tile: the Makefile names it to the mode as a tile with no pixel
    # (M17_SPR_NONE), and the mode's second pass then never looks at its entries
    assert m17tiles.sprite_box(spr_chr[SPR_NONE]) == m17tiles.EMPTY_BOX, \
        "sprite tile %02x has a pixel: the Makefile's M17_SPR_NONE must not name it" % SPR_NONE

    pal_labels = ["WaterPaletteData", "GroundPaletteData", "UndergroundPaletteData", "CastlePaletteData"]
    return {
        "bg_chr": bg_chr, "spr_chr": spr_chr, "mt": mt, "order": order, "index": index, "blank": blank,
        "solid_run": solid_run,
        "board": board, "board_tiles": board_tiles, "board_cells": board_cells, "merged_pixels": merged_pixels,
        "bank_text": bank_text, "text_types": sorted(text_types), "ground_text": ground_text,
        "nestitle": nestitle, "bank_title": bank_title, "merge": merge, "under_title": len(b_first),
        "font_nes": font_nes, "font_pairs": font_pairs, "made": made, "not_stored": not_stored,
        "glyphs_out": sorted(t for (t, p) in not_stored if (t, p) == font_pair(t) and t in FONT_TABLE),
        "sprites": [m17tiles.sprite_tile(t) for t in spr_chr],
        "boxes": [m17tiles.sprite_box(t, i in LAST_SPRITE_TILES) for i, t in enumerate(spr_chr)],
        "area_pals": [asmdata.bytes_of(l)[3:35] for l in pal_labels],
        "stats": {
            "table_pairs": len(table_pairs), "font_pairs": len(font_pairs),
            "all_mt": len(all_mt), "font": len(font), "f_only": len(f_only), "a": len(a), "b": len(b_mt),
            "made": len(made), "never": never, "dead": len(dead), "stored_dead": len(dead & all_mt),
            "per_type": [len(s) for s in per_type],
            "text_scene": len(text_scene | (font & all_mt)), "title": len(title_scene),
            "title_fits": title_fits, "why_title": why_title,
            "shapes": len({tuple(tuple(r) for r in bg_chr[k[0]]) for k in table_pairs | set(font_pairs)}),
        },
    }


def c_bytes(v, per=16):
    return ",\n".join("\t" + ", ".join("0x%02x" % x for x in v[i:i + per]) for i in range(0, len(v), per))


def write_inc(d, path=OUT_INC):
    with open(path, "w", newline="\n") as f:
        f.write("; %s -- mode 17's tiles, GENERATED by tools/gen_gfx17.py from the cartridge; do not edit.\n"
                % os.path.basename(path))
        if d["nestitle"]:
            f.write("; For a ROM built with NESTITLE=1: the title as the cartridge has it (gfxtiles17.inc is the others').\n")
        f.write("\n")
        f.write("; %d background tiles of 32 bytes; the blank is tile %d\n" % (len(d["order"]), d["blank"]))
        f.write(".global gfx17_bg_tiles\n.section .text.gfx17_bg_tiles\n.balign 256\ngfx17_bg_tiles:\n")
        f.write(m17tiles.solid_run_asm(d["order"], *d["solid_run"]) + "\n")
        if d["board_tiles"]:
            n = len(d["order"])
            f.write("; the title's board: %d tiles, %d to %d, the last of the table\n"
                    % (len(d["board_tiles"]), n, n + len(d["board_tiles"]) - 1))
            f.write(m17tiles.asm_bytes([b for t in d["board_tiles"] for b in t]) + "\n")
        f.write("\n")
        f.write("; 256 sprite tiles of 16 bytes\n.global gfx17_spr_tiles\n.section .text.gfx17_spr_tiles\ngfx17_spr_tiles:\n")
        f.write(m17tiles.asm_bytes([b for t in d["sprites"] for b in t]) + "\n\n")
        f.write("; their opaque boxes, 2 bytes each\n.global gfx17_spr_boxes\n.section .text.gfx17_spr_boxes\ngfx17_spr_boxes:\n")
        f.write(m17tiles.asm_bytes([b for t in d["boxes"] for b in t]) + "\n")


def write_h(d, path=OUT_H):
    mt, index = d["mt"], d["index"]
    counts = [len(t) // 4 for t in mt]
    L = ["/* %s -- the graphics data of the port to mode 17, GENERATED by tools/gen_gfx17.py; do not edit. */"
         % os.path.basename(path), "#pragma once", ""]
    if d["nestitle"]:
        L[1:1] = ["/* For a ROM built with NESTITLE=1: the title as the cartridge has it (gfxdata17.h is the others'). */"]
    L.append("/* the background's flash tiles (gfxtiles17.inc), and the two windows a VRAM cell reaches them")
    L.append("** through: a cell holds 64 + a flash tile - 8 x its section's bank */")
    L.append("#define GFX17_BG_TILES %d" % len(d["order"]))
    L.append("#define GFX17_BLANK %d              /* the blank, in both windows */" % d["blank"])
    L.append("#define GFX17_BANK_LEVEL 0           /* every tile a metatile draws */")
    L.append("#define GFX17_BANK_TEXT %d            /* the font, and the tiles of the areas that show text */" % d["bank_text"])
    if d["nestitle"]:
        L.append("#define GFX17_BANK_TITLE %d           /* the title's own: 1-1, the font, the board and the line under it */" % d["bank_title"])
    # (the pointer's bits 5 and 6 are the area's type and its low 5 the area: bit 7 is not the
    # area's, and the cartridge's table has it set for one, 1-2's $c0)
    cond = " || ".join(["(((p) >> 5) & 3) == %d" % t for t in d["text_types"]] +
                       ["((p) & 0x7f) == 0x%02x" % ((1 << 5) | i) for i in d["ground_text"]])
    L.append("/* the areas drawn through the text window, by AreaPointer: the castles (their messages), the")
    L.append("** underground (its warp zones), and the ground's own warp zone. (Bit 7 is not the area's:")
    L.append("** 1-2's pointer is $c0) */")
    L.append("#define GFX17_TEXT_AREA(p) (%s)" % cond)
    L.append("")
    L.append("/* metatiles: [palette][row] -> 4 flash tiles, TL BL TR BR */")
    for p in range(4):
        L.append("static const u8 MTiles17_%d[%d][4] PROGMEM = {" % (p, counts[p]))
        L.append(",\n".join("\t{%s}" % ", ".join("%3d" % index[(mt[p][4 * i + q], p)] for q in range(4)) for i in range(counts[p])))
        L.append("};")
    L.append("#define MTILES_COUNTS %s" % ", ".join(map(str, counts)))
    L.append("")
    L.append("/* the font: NES tiles $00-$2f in palette %d -> flash tile; and the symbols outside them */" % FONT_PAL)
    L.append("/* (a glyph no text draws is not stored: the blank is in its place) */")
    L.append("static const u8 FontTiles17[48] PROGMEM = {\n%s\n};" % c_bytes([index[(t, FONT_PAL)] for t in range(0x30)], 12))
    for t in sorted(FONT_TABLE):
        if t >= 0x30:
            L.append("#define GFX17_FONT_%02X %d" % (t, index[font_pair(t)]))
    L.append("#define GFX17_COIN %d               /* the bar's coin, in palette %d */" % (index[COIN], COIN[1]))
    L.append("#define GFX17_WATER %d              /* a water area's blank cell */" % index[WATER_BLANK])
    L.append("")
    rows = len(d["board"])
    if d["nestitle"]:
        L.append("/* the title's board and the line under it (ATTRACT=1), as the cartridge has them: a column of")
        L.append("** %d rows after the other, from the name table's column %d and the playfield's row %d. A cell"
                 % (rows, BOARD_COL, BOARD_ROW - NT_PF_ROW))
        L.append("** is the byte VRAM holds for it through the title's window (64 + its flash tile - 8 x")
        L.append("** GFX17_BANK_TITLE: the board's %d tiles are the table's last, %d to %d, past what a byte"
                 % (len(d["board_tiles"]), len(d["order"]), len(d["order"]) + len(d["board_tiles"]) - 1))
        L.append("** numbers), and 0 where the cartridge's data writes nothing: the level shows there.")
        L.append("** BOARD17_AT: where the table is kept (gfx17.c: the flash, or the card with the levels) */")
    else:
        L.append("/* the title's board: the cartridge's own, its cells as flash tiles, a column of")
        L.append("** %d rows after the other, from the name table's column %d and the playfield's row %d. Its %d"
                 % (BOARD_ROWS, BOARD_COL, BOARD_ROW - NT_PF_ROW, len(d["board_tiles"])))
        L.append("** tiles are the table's last, %d to %d */"
                 % (len(d["order"]), len(d["order"]) + len(d["board_tiles"]) - 1))
    if d["board_cells"] and d["nestitle"]:
        def cell(v):
            return 0 if v is None else 64 + v - 8 * d["bank_title"]
        assert all(v is None or 64 <= cell(v) <= 255 for row in d["board_cells"] for v in row)
        L.append("#define GFX17_BOARD_COL %d" % BOARD_COL)
        L.append("#define GFX17_BOARD_COLS %d" % BOARD_COLS)
        L.append("#define GFX17_BOARD_ROW %d" % (BOARD_ROW - NT_PF_ROW))
        L.append("#define GFX17_BOARD_ROWS %d" % rows)
        L.append("static const u8 Board17[%d] BOARD17_AT = {\n%s\n};" % (rows * BOARD_COLS, ",\n".join(
            "\t" + ", ".join("%3d" % cell(d["board_cells"][r][c]) for r in range(rows)) for c in range(BOARD_COLS))))
    elif d["board_cells"]:
        L.append("#define GFX17_BOARD_COL %d" % BOARD_COL)
        L.append("#define GFX17_BOARD_COLS %d" % BOARD_COLS)
        L.append("#define GFX17_BOARD_ROW %d" % (BOARD_ROW - NT_PF_ROW))
        L.append("#define GFX17_BOARD_ROWS %d" % BOARD_ROWS)
        L.append("static const u8 Board17[%d] PROGMEM = {\n%s\n};" % (BOARD_ROWS * BOARD_COLS, ",\n".join(
            "\t" + ", ".join("%3d" % d["board_cells"][r][c] for r in range(BOARD_ROWS)) for c in range(BOARD_COLS))))
    else:
        L.append("#error \"gen_gfx17.py --all stores no title board: the game needs the files without it\"")
    L.append("")
    L.append("/* the 8 palettes of each area type (water, ground, underground, castle): NES colours */")
    # The game's colours as the Uzebox's BBGGGRRR: the ROM converts nothing (the table of 64 and
    # its look-ups were 110 bytes)
    def col(v):
        return "0x%02x" % nespal.NES_TO_UZE[v & 63]
    L.append("static const u8 AreaPalettes[4][32] PROGMEM = {")
    L.append(",\n".join("\t{" + ", ".join(col(v) for v in pal) + "}" for pal in d["area_pals"]))
    L.append("};")
    for lab in ("DaySnowPaletteData", "NightSnowPaletteData", "MushroomPaletteData", "BowserPaletteData"):
        b = asmdata.bytes_of(lab)
        L.append("static const u8 %s[4] PROGMEM = {%s};  /* PPU $%02x%02x */" % (lab, ", ".join(col(v) for v in b[3:7]), b[0], b[1]))
    for lab in ("PlayerColors", "BackgroundColors", "ColorRotatePalette", "Palette3Data"):
        b = asmdata.bytes_of(lab)
        L.append("static const u8 %s[%d] PROGMEM = {%s};" % (lab, len(b), ", ".join(col(v) for v in b)))
    b = asmdata.bytes_of("BGColorCtrl_Addr")
    L.append("static const u8 BGColorCtrl_Addr[%d] PROGMEM = {%s};" % (len(b), ", ".join("0x%02x" % v for v in b)))
    L.append("")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L))


def main():
    args = sys.argv[1:]
    keep_all = "--all" in args
    if keep_all:
        args.remove("--all")
    if len(args) > 1:
        sys.exit(__doc__.split("\n\n")[1])
    m17tiles.check()
    nes = args[0] if args else asmdata.rom_path()       # by default the .nes in ROM/
    asmdata.check_rom(nes)
    d = build(nes, keep_all)
    write_inc(d)
    write_h(d)
    s, n = d["stats"], len(d["order"])
    print("gen_gfx17: %d (tile, palette) pairs in the metatile tables, %d of the font; %d shapes"
          % (s["table_pairs"], s["font_pairs"], s["shapes"]))
    print("gen_gfx17: %d flash tiles, %d bytes: %d the metatiles draw, the blank, %d the font's own (%d of its %d are a metatile's too)"
          % (n, 32 * n, s["all_mt"], s["f_only"], s["font"] - s["f_only"], s["font"]))
    print("gen_gfx17: tiles by area type, every area parsed whole: %s"
          % ", ".join("%s %d" % (AREA_TYPES[t], c) for t, c in enumerate(s["per_type"])))
    print("gen_gfx17: the level window, bank 0: tiles 0-%d of its 192; the text window, bank %d: tiles %d-%d (%d of 192)"
          % (s["a"] + s["b"], d["bank_text"], 8 * d["bank_text"], n - 1, n - 8 * d["bank_text"]))
    print("gen_gfx17: %d tiles are the other areas' alone, %d of them before the text window; %d of its 192 are free after the font"
          % (s["a"], 8 * d["bank_text"], 8 * d["bank_text"] + WINDOW - n))
    print("gen_gfx17: the title over 1-1 (ATTRACT=1, %d tiles): %s"
          % (s["title"], "fits the text window in this order too" if s["title_fits"] else "does NOT fit: " + s["why_title"]))
    if d["board_cells"]:
        nb = len(d["board_tiles"])
        print("gen_gfx17: the title's board (ATTRACT=1 alone): %d cells of %d NES tiles, %d new flash tiles (%d to %d, %d bytes); "
              "drawn as another: %s (%d pixels of %d); %d of the text window's 192 are free with it"
              % (BOARD_ROWS * BOARD_COLS, len({t for row in d["board"] for (t, p) in row}), nb, n, n + nb - 1, 32 * nb,
                 ", ".join("%02x as %02x" % m for m in sorted(BOARD_MERGE.items())) or "none", d["merged_pixels"],
                 64 * BOARD_ROWS * BOARD_COLS, 8 * d["bank_text"] + WINDOW - n - nb))
        # the same for a ROM built with NESTITLE=1: its own three files
        dn = build(nes, False, True)
        write_inc(dn, OUT_INC_N)
        write_h(dn, OUT_H_N)
        nn, nbn = len(dn["order"]), len(dn["board_tiles"])
        wrote = sum(1 for row in dn["board"][BOARD_ROWS:] for k in row if k)
        print("gen_gfx17: NESTITLE=1 (gfxdata17n.h, gfxtiles17n.inc): the board with every tile its own and "
              "%d cells under it in its palette: %d new flash tiles (%d to %d, %d bytes, %d more than without); "
              "the title's window is bank %d, tiles %d to %d, and %d of the two windows' tiles are under it"
              % (wrote, nbn, nn, nn + nbn - 1, 32 * nbn, 32 * (nbn - nb), dn["bank_title"], 8 * dn["bank_title"],
                 8 * dn["bank_title"] + WINDOW - 1, dn["under_title"]))
    else:
        print("gen_gfx17: no title board with --all: these files build no ATTRACT=1 ROM")
    print("gen_gfx17: metatiles: %d in the tables, %d made by a parser or the game; never made: %s; %d tiles are theirs alone (%d stored)"
          % (sum(len(t) // 4 for t in d["mt"]), s["made"], " ".join("%02x" % m for m in s["never"]) or "none", s["dead"], s["stored_dead"]))
    print("gen_gfx17: glyphs no text draws, not stored: %s"
          % (" ".join("%02x" % t for t in d["glyphs_out"]) or "none"))
    empty = sum(1 for b in d["boxes"] if tuple(b) == m17tiles.EMPTY_BOX)
    print("gen_gfx17: sprite tiles 256 (%d bytes with their boxes), %d of them empty" % (16 * 256 + 512, empty))


if __name__ == "__main__":
    main()
