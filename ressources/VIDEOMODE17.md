# Video mode 17

Video mode 17 is the custom Uzebox video mode written for *Super Uze Brothers*, the port of
Super Mario Bros. to the Uzebox. It draws the NES's own 8×8 tiles, with NES-style palettes and
sprites, on an ATmega644 at 28.636 MHz with 4 KB of SRAM. This document covers how it works,
why it is built the way it is, and where it falls short of the NES. For the project as a whole
(the engine port, the memory and flash budgets, the SD card), see [MAKING-OF.md](MAKING-OF.md).

The mode lives in [`customVideoMode17/`](../customVideoMode17/). The game's side of it is
`gfx17.c` (the platform layer), `main17.c` (the frame loop), `tiles17.s` and
`tools/gen_gfx17.py` (the tile generator). It is built through the kernel's `VIDEO_MODE=0` and
`VIDEO_MODE_PATH` mechanism, like any custom mode.

---

## 1. Why a new mode

### What Super Mario Bros. asks of the NES PPU

| NES feature | How SMB uses it |
|---|---|
| A background of 8×8 tiles, 2 bits a pixel | Every level, all text, the title board |
| 4 background palettes of 3 colours plus a shared backdrop | Ground, bricks, pipes and so on, each in its own palette. The palette is chosen per 16×16 area by the attribute table |
| 4 sprite palettes of 3 colours, colour 0 transparent | Mario, enemies, items, fireballs |
| 64 sprites of 8×8, with x/y flip and a "behind background" priority bit | Mario is 8 sprites; piranha plants rise from behind pipes; a power-up rises out of a block |
| Horizontal scrolling over a 2-screen name table | The level scrolls right one column at a time |
| A fixed status bar over the scrolling playfield | Done with a mid-frame scroll split timed by the sprite-0 hit |
| Palette writes at any frame | The "?" blocks and coins cycle a colour every 8 frames |

### What existing modes offered

The game was first written on **mode 16**, an earlier custom mode (described in UzeBasic's
`docs/video-modes.md` §4). Mode 16 draws a 160×100 picture from 6×8 tiles through a line buffer.
Each pixel is 8 cycles wide and spans two scanlines, and sprites are taken from a sorted list
by a fixed number of slots per row. On mode 16 the game had to:

- use art **redrawn by hand** at 6×8, because the NES's 8-pixel tiles don't fit a 6-pixel
  cell,
- show the level at **half vertical resolution**, with a vertical camera that panned to keep the
  player on screen,
- convert the engine's OAM into the mode's own sprite format every frame.

That worked, but it wasn't the cartridge's picture. The other candidates were measured and
dropped before mode 17 was designed:

| Option | Why it was dropped |
|---|---|
| Kernel mode 13 as it is | 15 colours for the whole screen in fixed slots. Flash tiles carry slot numbers, so the game would need 18 to 19 slots. Its sprite blitter is in C and has no flips. |
| 2 bits a pixel, one palette a cell (16-byte tiles) | A cell where a sprite overlaps background art needs two palettes. That gave wrong colours in 43.8% of frames, and 8 palettes cost 7 cycles a pixel (24 columns). |
| Mode 16's format (6×8 tiles, 3 pixels and a palette per byte) | Wrong pixels wherever a 3-pixel group straddles a sprite's edge: 6.6 to 12 a frame |
| 17-byte flash tiles with a palette nibble per cell, 4-bit RAM tiles | Exact, but 61 to 78 cycles a tile, which is 8 to 10 cycles a pixel and only 16 to 21 columns |
| Pair codes with a palette nibble in RAM tiles too | Wrong pixels where a pair straddles a sprite's edge: 7.4 a frame |

The figures above were measured by running these formats against 1.7 million frames of the
game's PC build. The requirement was **no wrong pixel**, whatever the sprites do. Slowdown in
the busiest scenes was acceptable, and so was having sprites take turns when resources run out.

Mode 17 is the design that met that requirement. It took over as the game's only mode, and
mode 16's build of the game was removed.

---

## 2. Screen geometry and timing

```
              28 columns × 8 px = 224 px  (6 cycles a pixel)
        ┌──────────────────────────────────────────────────┐
row  0  │ 00000 ◎00   1-1  000      HUD row: never scrolls │  NES lines 24–31
        ├──────────────────────────────────────────────────┤
row  1  │                                                  │  NES line 32
  ...   │   playfield: 24 rows, VRAM 32 columns wide,      │
        │   scrolls horizontally (Screen.scrollX 0–255)    │
row 24  │                                                  │  NES line 223
        └──────────────────────────────────────────────────┘
          200 lines shown = 25 rows × 8 (M17_ROWS = 25)
```

| | Value | Where it is set |
|---|---|---|
| Pixels a line | 224 (28 tiles × 8) | `M17_COLS` |
| Cycles a pixel | 6 (48 a tile) | `videoModeCore.s` |
| First pixel / black after the last | cycle 346 / 1,691 after the sync edge | `M17_FIRST_PIXEL` |
| Rows shown | 25: 1 HUD row and 24 playfield rows | `M17_ROWS=25`, `M17_HUD=1` |
| Lines drawn | 200, starting at kernel line 32 | `M17_FRAME_LINES`, `M17_FIRST_LINE` |
| Line / field | 1,820 cycles / 262 lines = 476,840 cycles | the kernel |
| VRAM | 32 columns × 24 rows, 1 byte a cell | `M17_VRAM_H`, `M17_PF_ROWS` |
| Vertical scroll | none | — |

The pixel clock is 28.636 MHz / 6 = 4.77 MHz. The NES's is about 5.37 MHz, so a mode 17 pixel is
about 12% wider than an NES pixel. The art itself is 1:1.

### Horizontal scrolling: fine scroll and the VRAM ring

The renderer is derived from kernel mode 13's scrolling renderer:

- **The VRAM layout is interleaved.** Cell (x, y) is at
  `vram[(y >> 3) * 256 + 8 * x + (y & 7)]`. A page holds 8 rows, and stepping one column adds 8
  to the pointer's low byte. Only the low byte ever changes, so a row wraps round its 32 columns
  by itself. The renderer's column step is `add YL, r9`, with r9 = 8.
- **The fine scroll** (`scrollX & 7`) is handled by drawing the first, partial tile through a
  jump table entered with `push`/`push`/`ret`. Every `out` in both loops and both jump tables is
  on the 6-cycle grid, so a line ends on the same cycle at all 8 fine-scroll offsets. Timer1's
  overflow ends each line.
- **The coarse scroll** (`scrollX >> 3`) picks the starting column in the 32-column ring. The
  game keeps level tile column *c* in VRAM column `c & 31`. When the view moves, `gfx_Commit()`
  writes the one column that enters on either side. Nothing is ever moved.

The view is 224 of the NES screen's 256 pixels. Its left edge stays at the screen's left until
the player passes x = 96 in the view (`VIEW_FOLLOW`), and then follows him, up to 32 pixels
(`VIEW_SLACK`).

### The HUD row

The HUD row isn't part of VRAM. Its 28 cells are bytes 226 to 255 of the SRAM page that holds
table T2 (`m17_hud[]`, one byte a column, `M17_HUD_OFS = 226`). It goes through the same line
code as the playfield, but with a column step of 1 in place of 8, no scroll, and its own tile
bank (`Screen.hudTileBank`).

Keeping the HUD out of VRAM saves a quarter of VRAM. One HUD row would otherwise cost a whole
fourth page: 256 bytes, or 7 to 8 RAM tiles. The NES produces its status bar with a mid-frame
scroll split; here the split simply comes from the renderer switching sections.

---

## 3. Tiles and colour

### Three tile formats

| Tile | Bytes | One byte holds | Read by |
|---|---|---|---|
| Flash (background) tile | 32: 8 lines × 4 bytes | `pppp cccc`: a palette (0–7) and a **pair code** over that palette's 4 values: 2 pixels | the renderer, through table T1 |
| RAM tile (a cell with a sprite in it) | 32 | a **pair code** over the screen's 15 colour slots: 2 pixels | the renderer, through table T2 |
| Sprite tile | 16: 8 lines × 2 bytes, plus 2 bytes of opaque box | 4 pixels of 2 bits, left pixel in the low bits, 0 transparent | the compositor only |

### Pair codes: two pixels per byte from one lookup

The renderer reads a tile byte `b` and then outputs `table[b]` and `table[b + 1]`. That is all
it does per pixel pair. For this to give any pair of colours, the table must be a sequence in
which **every ordered pair of values occurs exactly once as two neighbours**. That is a de Bruijn
sequence of order 2, and a pixel pair's code is its position in the sequence.
`generators/tables.py` builds the sequences and the tables from them:

- **SEQ4**, 16 long, over a palette's 4 values (0 is the backdrop):

  ```
  code c :  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 | (16)
  SEQ4[c]:  0  0  1  0  2  0  3  1  1  2  1  3  2  2  3  3 | (0)
  pair   : 00 01 10 02 20 03 31 11 12 21 13 32 22 23 33 30
  ```

  All 16 pairs of 4 values come out of 16 entries. A plain two-entry-per-code table would need 32.

- **SEQ15**, 225 long, over the screen's 15 colour slots (slot 0 is the backdrop). This gives
  all 15 × 15 = 225 pairs from 225 entries.

Both sequences start with 0, the backdrop. The last code's second pixel is read one entry past
the sequence's end, so that entry must be the backdrop too. With that, nothing ever wraps:

- **T1** holds the 8 palettes back to back: `T1[16·p + c]` = palette p's colour for value
  `SEQ4[c]`. Palette p's last entry is followed by palette p+1's first, which is the backdrop.
  An extra entry after palette 7 completes it: 8 × 16 + 1 = 129 bytes.
- **T2** holds the 15 slots' colours in SEQ15 order, plus the extra backdrop entry: 226 bytes.

**Example (flash tile).** Byte `0x5A` is palette 5, code 10. The renderer outputs `T1[0x5A]`,
which is palette 5's colour for value `SEQ4[10] = 1`, and then `T1[0x5B]`, its colour for
`SEQ4[11] = 3`. The two pixels are (value 1, value 3) in palette 5.

**Example (RAM tile).** To store slots (2, 5), the compositor looks up `m17_enc15[0x25]` and gets
code 61 (`0x3D`). The renderer outputs `T2[61]` and `T2[62]`, which are the colours of slots
`SEQ15[61] = 2` and `SEQ15[62] = 5`.

```
 flash byte  0x5A ──► XL                          RAM byte 0x3D ──► XL
                      XH = page of T1                               XH = page of T2
   ld r16, X+   →  T1[0x5A] = pal5 value1           ld r16, X+  →  T2[61] = slot 2
   ld r8,  X    →  T1[0x5B] = pal5 value3           ld r8,  X   →  T2[62] = slot 5
   out ..., r16 / 6 cycles later out ..., r8
```

### Why this is fast enough

T1 and T2 are in SRAM, each in its own 256-byte page. The tile byte goes straight into `XL` with
`XH` constant, so two pixels cost one tile read (`lpm` for flash, `ld` for RAM), two `ld`, and
two `out`: no shifts and no masks. That fits a 6-cycle pixel with room for the next tile's VRAM
read, its `mul` by 32 and its address.

The flash loop (`m17_flash`) has no spare cycle at all. Tiles of each kind have their own loop,
and each of the four combinations (this tile flash or RAM, the next one flash or RAM) has its
own tail. The kind of the next tile comes from `cpi index, 64` placed after the `mul` and read
by a branch 4 cycles later. `XH` is only rewritten when the kind changes.

The palette sits in the high nibble of each flash byte, so selecting a palette costs nothing
per pixel. Palettes behave as on the NES: a palette is a role, one tile set serves every area's
colours, and changing a palette is a write to T1.

The pair tables also explain why the RAM tiles use **screen slots** and not palette values. A
cell with a sprite over background art can need any two colours side by side, from different
palettes. A RAM tile byte names two of the screen's colours, so it is exact as long as the
screen has at most 15 distinct colours. In SMB the palettes in force hold 11 to 15 by area. Only
underground, with the "?" cycle on its darkest step, reaches 15.

### Palettes and the screen's colours

- There are **8 palettes, as on the NES**: 0–3 for the background and 4–7 for sprites
  (`M17_SPRITE_PAL + attr & 3`). Each has 3 colours over the shared backdrop. T1 is where the
  palettes are stored (`m17_SetPalette`, `m17_SetBackdrop`).
- **Rebuilding the colour slots.** After a palette write, the next composition assigns slots to
  the distinct colours of the 8 palettes (backdrop first) and fills T2. It also builds two
  translation tables in the 127 bytes left in T1's page:
  - flash bytes of palettes 0–3 → RAM tile bytes (64 entries). This is why a background tile
    that a sprite can cover must use palettes 0–3, the same rule as on the NES.
  - a sprite tile's nibble (2 pixels) in each sprite palette → two slots (64 entries).

  A full rebuild takes about 13,400 cycles. A 16th colour is mapped to the nearest slot and
  counted in `m17_colors_over`; the game never has one.
- **`m17_SetPaletteColor(p, v, colour)`** changes one colour without a rebuild, for the "?" block
  cycle. Depending on whether the colour is already on screen and whether its slot is shared,
  it either reuses a slot, recolours a slot in place (15 bytes of T2: RAM tiles change at once),
  or takes a free slot. This costs 2,600 to 3,500 cycles, about a fifth of a rebuild.
- **NES to Uzebox colours.** `tools/nespal.py` holds the NES's 64 colours (FCEUX's default
  palette). For each one it picks the nearest Uzebox colour (`BBGGGRRR`: 3 bits red, 3 green,
  2 blue), by squared distance with green weighted most (weights 3, 4, 2 for R, G, B). The
  generator converts the game's palette tables at build time, so the ROM stores Uzebox colours
  and has no lookup table.

---

## 4. VRAM, banks and windows

A VRAM cell is one byte:

```
 index   0 ..  63   RAM tile  (only 38 exist in this build: M17_RAM_TILES)
 index  64 .. 255   flash tile  (index − 64) + 8 × bank      → 192 tiles reachable
```

The renderer computes the address as index × 32 plus a base high byte. Moving the bank by 1
moves the base by 256 bytes, which is 8 tiles. A section (playfield or HUD) can therefore see
any window of 192 consecutive flash tiles that starts at a multiple of 8. The bank is any
number, so the window can be anywhere in the tile table.

The game has more background tiles than one window holds. A tile is stored once for each
palette it's drawn in, because the palette is part of a flash tile's bytes. The standalone ROM's
table holds 248 tiles. `tools/gen_gfx17.py` orders them so that **two windows** cover every
scene:

```
 flash tile table (gfx17_bg_tiles, page aligned)
 ┌────────────────┬─────────────────────────────────────┬─────────────┐
 │ tiles only the │ tiles the level and text windows    │ font tiles  │
 │ other areas    │ share (incl. the blank, the solid   │ (+ title    │
 │ draw           │ run)                                │  board)     │
 └────────────────┴─────────────────────────────────────┴─────────────┘
 ◄──────────── level window, bank 0 (192) ───────────►
                  ◄──────── text window (192), bank GFX17_BANK_TEXT ───►
```

- **The level window** (bank 0) holds every tile a metatile draws, and the blank.
- **The text window** holds the font, the blank, and every tile used by areas that show text over
  the level (castle messages, the warp zone's welcome) or that share those areas' type
  (underground). The HUD row and the text screens use it too.
- **The title's window.** The `.uze` build (`NESTITLE=1`) draws the title exactly as the
  cartridge does, including the copyright line, which uses the board's palette. Each of that
  line's glyphs therefore needs its own tile. That pushes the table past the 256 tiles a text
  window can reach, so the title gets a window of its own further up the table (`GFX17_BANK_TITLE`).
  The generator orders the tiles the title scene draws inside that window.

The generator checks every area against its window, using `tools/area_mtiles.txt` (every
metatile each area's parser produces). If a scene doesn't fit, it stops and reports the excess
tiles. Glyphs that no text draws and metatile tiles that nothing produces aren't stored; their
entries point at the blank.

The generator also places the tiles that have art in every pixel ("solid" tiles) in one run,
between the labels `m17_solid_lo` and `m17_solid_hi`. The compositor uses that run (§5).

---

## 5. Sprites

### The sprite list is the engine's OAM

The mode reads a 64-entry, 4-byte list in the NES's own format: y, tile, attributes, x. The
attribute bits are palette (bits 0–1), behind background (`0x20`), flip x (`0x40`) and flip y
(`0x80`). Entry 0 is on top.

In the game this list **is the engine's OAM**: the same 256 bytes, at a fixed address
(`M17_OAM_AT`), with no copy or conversion. The mode applies an origin (`Screen.spriteX`,
`Screen.spriteY`) to place the NES's 256-pixel screen in the 224-pixel view. An entry is in play
when −7 ≤ x − spriteX ≤ 223 and y − spriteY ≥ −7, computed as whole numbers. Both passes test
this first, so an entry out of view costs almost nothing.

Sprites are drawn at their OAM y, one line above where the NES draws them (y + 1). A sprite
standing on the ground then lies on the cells' own rows and needs half as many RAM tiles.

### RAM tiles

A cell with a sprite pixel in it is shown as a **RAM tile**. The compositor copies the cell's
flash tile into a RAM tile through the palette translation and then draws the sprites into it.
This build has **38 RAM tiles** (`M17_RAM_TILES=38`), the most the SRAM allows (§7). Each one
costs 32 bytes, plus 3 bytes in the restore list (the cell's address and the flash tile it
held).

VRAM normally holds flash tile indexes. At each picture's start, the frame writes each RAM tile's
index into its cell (19 cycles a tile), and at the end it puts the flash index back. A program
never sees RAM tile indexes in VRAM.

### Opaque boxes

Each sprite tile has a 2-byte **opaque box**: the first and last line, and the first and last
column, that contain any pixel. `m17tiles.sprite_box` computes it. Pass 1 gives a cell a RAM tile
only if the box actually reaches that cell. Mario's empty columns, a lift's empty lines and fully
empty tiles cost no RAM tile.

Bit 7 of the box's second byte marks the tile's sprites as **last class** (below).

### Pass 1: which cells get RAM tiles (`m17_pass1.s`)

Pass 1 walks the list starting at `m17_rotate`, in three trips, one per class:

1. **First class**: `M17_FIRST_N = 8` entries from `M17_FIRST_AT = 1`, the player's 8 sprites.
   (Entry 0 is the NES's sprite 0.)
2. **Middle class**: every other sprite.
3. **Last class**: sprites whose tile is marked in its box. The generator marks the brick chunks
   and the floating score numbers (`LAST_SPRITE_TILES` in `gen_gfx17.py`).

For each sprite in turn, every cell its box reaches gets a RAM tile while any are left. The
cell's flash tile is copied through the translation, or filled directly if it's the blank. Two
rules save tiles:

- **Behind-background sprites over solid tiles.** A sprite behind the background gets no RAM tile
  for a cell that shows a solid flash tile, because nothing of it could show there. Such a sprite
  counts against the cap only if one of its cells gets a tile. Piranha plants down in their pipes
  therefore cost nothing. In a level of pipes this took a composition from 41 lines to 30.
- **The draw cap**: a composition gives turns to at most `M17_DRAW_MAX = 24` sprites with a cell
  in the picture. In the public build this is a constant. (An earlier variable cap was folded
  out.)

**When RAM tiles or turns run out**, the first entry that went without becomes `m17_rotate`, and
the next composition starts from there. What is missing changes every frame, so a sprite that
doesn't fit flickers rather than vanishing. The classes decide who loses first: the player last,
brick chunks and scores first.

### Pass 2: drawing the pixels (`m17_pass2.s`)

Pass 2 goes from the last entry to entry 0, so entry 0 ends up on top.

- A sprite line is 2 bytes (8 pixels). At an odd x the 16 bits are shifted by 2 and become 5
  pairs, so each sprite pair always lines up with a RAM tile pair.
- **Front sprites.** At the start, the pass builds the sprite palette's 16 pair codes, plain and
  reversed for flip x, into two 16-byte tables in a 47-byte frame on the stack. A pair with both
  pixels opaque is then one table read and one store. A pair with one pixel opaque is decoded
  (`m17_dec15`), merged and re-encoded (`m17_enc15`). An empty pair is skipped. These tables
  share two flash pages, so each lookup is `mov ZL, value` with `ZH` constant.
- **Flips.** Flip x takes the pairs from the end and swaps each pair's two pixels. Flip y reads
  the tile's lines in reverse.
- **Behind the background.** The pixel is written only where the background pixel is the
  backdrop. The pass finds the cell's flash tile from the restore list and tests it with
  `m17_bgmask4`. If every cell in the row shows the blank tile, there is no background to keep,
  and the sprite takes the faster front path. Piranha plants over the sky cost about 1,800
  cycles instead of about 3,000.
- **NES priority semantics** are kept: every cell is composed by the same rules as the NES. That
  includes a behind-background sprite with a lower index masking a front sprite. The Python model
  checks this pixel for pixel against the NES's rules.
- **`M17_SPR_NONE = 0xFC`**: the engine's empty sprite tile. Pass 2 treats an entry with this
  tile as if it were off. The engine leaves 2 to 12 such entries switched on in a frame. The
  generator asserts that the tile's box is empty.
- **Sprites over the HUD row** are composed as on the NES. For example, Mario's feet show over
  the bar on 1-2's ceiling. Both passes have a HUD case for the row's differences: no scroll, one
  byte per column, and the HUD's bank.

### Lifts: one segment composed, the rest copied

A large lift is six identical sprites in a row. In `gfx17.c`, `gfx_LiftsOff()` and
`gfx_LiftsOn()` handle a lift row that sits on the cell grid over blank cells. Only one segment
is composed; the other five cells are given that segment's RAM tiles. A lift then needs 2 RAM
tiles instead of 12 to 14, and 1 sprite turn instead of 6.

For this to work, the up/down lifts are placed 16 pixels right of their column (`LIFT_GRID 16`
in `enemies.c`) instead of the cartridge's 12, which is 4 pixels off the grid. On 1-2 this took
the frames short of a RAM tile from 83 to 5.

---

## 6. The frame

### One picture is one long interrupt

The mode draws its 200 lines inside the kernel's line interrupt, and the main program is
suspended for that time. The main program, including the compositor, runs in the vertical blank.

```
 one field = 262 lines × 1,820 cycles = 476,840 cycles
 ├── picture: 200 lines ─────────────────────────────┤├── blank: 62 lines ───┤
 │ HUD │ playfield rows 1–24 (main program stopped)  ││ show(): upload,      │
 │     │   …except rows of pure black (bands) ───────►││ gfx_Commit(),        │
 │     │   which the kernel draws while the main     ││ m17_ComposeSprites() │
 │     │   program runs                              ││ then pad + engine    │
 └─────┴─────────────────────────────────────────────┘└──────────────────────┘
                                                        112,840 cycles,
                                                        ≈ 93,000 for the program
```

The blank is 62 lines, 112,840 cycles. The kernel's sync interrupt, mostly the sound mixer,
takes 266 to 275 cycles of each line, which leaves the main program **about 93,000 cycles (82%)**.
The tools count work in "lines of blank" of 1,820 wall-clock cycles each; one such line is about
1,550 cycles of main program.

A picture that starts in the middle of a composition is still drawn correctly. The background is
complete, and the sprites drawn up to that point show. The mode guarantees this as follows:

- While the compositor runs, `m17_ram_used` is 0, so the frame leaves VRAM as the compositor has
  it.
- Pass 1 only points a cell at its RAM tile once the tile is complete.
- Pass 2 writes one byte (one pair) at a time.

Pass 2 draws from the last entry to the first, so a cut picture is missing the first entries:
in the game, the player. The game's loop avoids cuts altogether (below).

`m17_hold` is the program's "screen off": a picture that starts while it is set isn't drawn. The
mode still counts its lines and sets the vsync flag at its end, so `WaitVsync()` keeps working.

### The game's loop (`main17.c`)

The loop follows the cartridge's order. At the start of the blank, it shows the frame the engine
finished, then runs the engine for the next one:

```
 loop:
   wait for a picture's end (or carry on if the engine already overran into the blank)
   show():   m17_TakeLeave()
             game_Upload()          palettes, text screens
             gfx_Commit()           changed cells, new column, bar, colour step, scroll, origin
             gfx_LiftsOff(); m17_ComposeSprites(); gfx_LiftsOn()
             m17_GiveLeave()        (only while a level is shown)
   ReadControllers()
   game_Engine()                    the 6502's frame; may run past the blank
   m17_hold = gfx_ScreenOff()
```

**Nothing the engine does is visible until `show()`.** The engine writes its own RAM, the OAM
(read only by the compositor) and a queue of changed cells. `show()` commits the upload, the
cells, the scroll and the composition together, and **only starts if it will finish before the
next picture**. That check is `room()`: the lines left (`gfx_LinesLeft()`) must cover the last
`show()`'s length plus a margin. The margin is 26 lines, or 14 after a `show()` of 30 lines or
more. These figures come from 427,033 consecutive frames, where a `show()` exceeded the previous
one by at most 25.4 lines. If there isn't room, the loop waits one field, and the picture in
between repeats the last frame in full. The result is that a picture never shows a new background
under old sprites, or a frame without its player.

### Bands: black rows given back to the CPU

A playfield row in which every visible cell holds the blank tile, over a black backdrop, is just
8 black lines. The kernel's sync interrupt produces exactly those lines on its own during the
blank. When the program has given its **leave** (`m17_GiveLeave()`), the mode hands such rows to
the kernel, and the main program runs during them:

- Each such row is worth **about 12,400 cycles**, 8 lines of blank. A field gives the program
  95,300 cycles with no black row and 199,400 with nine. This helps underground, in castles and
  at night.
- The frame looks one row ahead using cycles that were waits (4 cells after each of a row's first
  7 lines), so rows with art cost nothing extra.
- A band ends like a frame: Timer1 is reset, and the kernel's render window
  (`first_render_line`, `render_lines_count`) is moved to the row's last line, where the mode's
  entry takes over again.
- The state lives in GPIOR0 to GPIOR2, so it takes no SRAM. GPIOR0 bit 7 is the leave and bit 0
  says "inside a picture" (`m17_InPicture()`).
- **The rule for the program**: while the leave is given, it writes nothing a picture reads and
  doesn't compose. `main17.c` gives the leave after `show()`, only while a level is shown. It
  takes the leave back before the next `show()`, and `room()` answers no inside a picture.

The picture is identical pixel for pixel, and every sync edge stays on its cycle. In the public
repository bands are always on.

### CPU budget, as measured

The worst cases come from the bench's recorded scenes, in lines of the blank's 62, before and
after the speed work on the compositor:

| Scene | Before: average (worst) | After: average (worst) |
|---|---|---|
| 1-1, big Mario, Goombas, brick chunks | 45.1 (48.6) | 32.6 (35.3) |
| 1-1, 24 sprites in front | 56.4 (60.4) | 40.7 (44.4) |
| 1-1, 28 sprites asked of 24 | 55.1 (59.0) | 41.5 (44.4) |
| 6-2, out of a pipe, plants down | 30.3 (41.0) | 10.2 (14.4) |
| 6-2, three plants moving | 39.5 (62.1) | 17.6 (31.4) |

After the speed work, pass 1 costs 2.6 to 5.3 lines a frame and pass 2 5.8 to 20.8. The engine
costs 8 to 14 lines, and the commit about 1.3. In the 6-2 rows the bands carry part of the work,
so the "after" column there is what remains to fit in the blank, not the total time.

Other numbers:

- A full field holds 62 lines of blank. The worst frame of the bench uses 44.4 of them.
- From the frame's entry to its first sync edge takes 1,356 cycles. This includes the 19 cycles
  per RAM tile used to place them in their cells.
- An empty composition costs about 1,600 cycles.

---

## 7. Memory map

### SRAM

All of the mode's SRAM is one linker section, the **run** (`.m17run`), placed by the Makefile
at `0x0100`. The C start-up doesn't clear it; the mode does when it initialises. Its layout comes
from `m17.def.h`, and the addresses below are those of the shipped ELF:

```
 0x0100 ┌─────────────────────────────┐ M17_RUN_T2
        │ T2 (226) │ HUD row (30)     │ 1 page
 0x0200 ├─────────────────────────────┤ M17_RUN_T1
        │ T1 (129) │ translations(127)│ 1 page
 0x0300 ├─────────────────────────────┤ M17_RUN_SPRITES = M17_OAM_AT
        │ sprite list = engine's OAM  │ 1 page (64 × 4)
 0x0400 ├─────────────────────────────┤ M17_RUN_VRAM
        │ VRAM: 24 rows × 32 columns  │ 3 pages (768)
 0x0700 ├─────────────────────────────┤ M17_RUN_TILES
        │ 38 RAM tiles × 32           │ 1,216
 0x0BC0 ├─────────────────────────────┤ M17_RUN_RESTORE
        │ restore list, 38 × 3        │ 114
 0x0C32 ├─────────────────────────────┤ M17_RUN_STATE
        │ frame state, Screen, comp.  │ 26
 0x0C4C ├═════════════════════════════┤ end of the run: 2,892 bytes (M17_RUN_SIZE)
        │ game's fixed block (tails17)│ 328   (M17_GAME_AT)
 0x0D94 ├─────────────────────────────┤
        │ .data / .bss                │
 0x1041 ├─────────────────────────────┤
        │ stack (≈190 bytes)          │
 0x10FF └─────────────────────────────┘
```

- **Page alignment.** T1, T2 and the sprite list each need their own page, because the renderer
  and the compositor index them with a pointer's low byte. Placing the run on a page boundary,
  with `.data` after it, means no bytes are lost to alignment. It also turns every part's address
  into a compile-time constant. The engine is compiled with `M17_OAM_AT = 0x0300`.
- **The budget is tight.** The run takes 2,892 of the 4,096 bytes. The engine, sound, platform
  layer and kernel take the rest, except for a stack of about 190 bytes. Every RAM tile costs 35
  bytes, and 38 is what fits. `default/ramcheck17.py` runs at every link and fails the build if
  the stack has less than 190 bytes, or if the OAM isn't the mode's sprite list.

### Flash: tables that must be page-aligned

Three flash tables need a page boundary:

| Table | Size | Why |
|---|---|---|
| `m17_pairs`: `m17_enc15`, `m17_dec15`, `m17_bgmask4` | 2 pages | the compositor uses the value as `ZL` |
| the mixer's `waves` | 4 pages | the kernel's mixer |
| `gfx17_bg_tiles` | the background tiles | the renderer builds a tile's address from the table's high byte |

`default/order17.ld`, passed as `--section-ordering-file`, puts them back to back from `0x0100`,
the first boundary after the vectors (pairs at `0x0100`, waves at `0x0300`, tiles at `0x0700`).
Small tables such as `m17_dec4`, `m17_sprmask` and the font map fill most of the gap in front.
Linked wherever their objects happened to fall, the three would have cost 308 bytes of padding.
`ramcheck17.py` also checks that the linker inserted no more than 32 bytes of fill.

Flash tiles can't be compressed, because the renderer reads them during the scanline and the
compositor reads them in the blank. A background tile costs 32 bytes and a sprite tile 18 (16
plus its box).

---

## 8. How the mode made the port possible

- **The cartridge's own 8×8 art at 1:1**, with no redrawing and no vertical camera.
- **Pair codes**: one byte, `table[b]` and `table[b+1]`, gives two pixels in 6 cycles each, so
  the screen gets 28 columns.
- **Two kinds of tile.** Flash tiles carry NES-style palettes. RAM tiles over 15 screen slots make
  every cell with a sprite exact.
- **The NES's own structures**: the OAM is the sprite list, T1 holds the palettes, and the HUD
  row replaces the sprite-0 split. No conversion pass is needed.
- **A compositor built for the worst frames**: opaque boxes, hidden sprites skipped, a fast
  behind-background path, classes, a cap, rotation, and lifts composed once.
- **A frame protocol that never shows a broken picture**, and bands that give the dark levels
  time back.
- **SRAM laid out by hand**, which leaves room for 38 RAM tiles next to the full engine in 4 KB.

---

## 9. Limitations

- **Sprite flicker when RAM tiles run out.** An 8×8 sprite off the cell grid covers 4 cells, so
  the real cost is cells, not sprites. With 38 RAM tiles, busy scenes run out. Examples are big
  Mario with enemies, a brick's chunks and a score on 1-1 and 1-2 (the chunks and score lose
  first), pieces of firebars on 5-4 around the small lifts, and an occasional Bullet Bill on 8-3.
  Sprites then take turns, so the missing pieces flicker. There is no SRAM left for more tiles.
- **The draw cap.** At most 24 sprites get a turn in each composition. Past that, sprites take
  turns as well. With the game's lists this happened on 0.73% of frames when the cap was
  introduced.
- **15 colours where sprites are.** RAM tiles can show 15 distinct colours, backdrop included,
  across the whole screen. Flash tiles can show anything in the 8 palettes. SMB's palettes stay
  within 15 (underground reaches exactly 15), and a 16th colour would be shown as its nearest
  slot.
- **Palette granularity is finer than the NES's, at a cost in flash.** The NES picks a palette per
  16×16 area. Here every flash tile carries its palette, so each (tile, palette) combination the
  game draws is stored separately: some shapes are stored two or three times. Background tiles
  that sprites can cover must use palettes 0–3, as on the NES.
- **No vertical scroll.** SMB doesn't need it. Every level row is always in the same VRAM row.
- **25 rows instead of the NES's 30.** The picture is NES lines 24 to 223:
  - The top 24 lines are cut, including the status bar's row of names ("MARIO", "WORLD",
    "TIME"). The HUD row shows only the numbers.
  - The bottom 16 lines are cut: the lower ground row. Anything that moves through lines 224 to
    239, such as a firebar on 5-4 swinging low, isn't shown. This isn't flicker.
  - Horizontally, 224 of 256 pixels are shown, and the view follows the player.

  A 28-row build would show all 13 block rows, but would leave only about 69,000 cycles of blank,
  less than a heavy composition needs.
- **Sprites are one line higher** than on the NES, because they're drawn at their OAM y rather
  than y + 1.
- **Slowdown is accepted in the busiest places.** If a `show()` doesn't fit before the next
  picture, the last frame is shown again and the game runs one picture late. After the speed
  work, neither the bench's scenes nor its plays had a frame that took two fields, but the rule
  is still there for frames that would.
- **Lifts on a grid.** The large up/down lifts sit 16 pixels right of their column instead of 12
  (`LIFT_GRID` in `enemies.c`), so that a lift row can be composed once. The small lifts on ropes
  keep the cartridge's 12.
- **The standalone title board** (`make`) draws two of its tiles as their nearest neighbours
  (`BOARD_MERGE` in `gen_gfx17.py`: `$d5` as `$d9` and `$e6` as `$db`, at most 2 pixels apart
  each), because the text window is full. It also leaves out the copyright line. The `.uze` build
  has its own title window and draws the board exactly as the cartridge does.
- **Bands only help black rows.** A blue-sky row isn't black, and the kernel's lines are black,
  so blue-sky levels get no time back. Bands for a coloured backdrop were designed but not built.

---

## 10. How it was verified

The mode was tested during development with tools that are not part of this repository:

- **A test ROM** (`customVideoMode17/test/`) driven in the emulator through its GDB stub by
  `m17check.py`. It checks:
  - every `out` of every line, for cycle and value, at all 8 fine scrolls;
  - every field's 262 sync edges and 476,840 cycles, at 25 and 28 rows, with and without the HUD,
    with 0 to 44 RAM tiles;
  - held pictures, the origin's edges and the run's layout;
  - about 600 cut pictures per configuration, with the compositor stopped at every instruction of
    its start and end and at random points;
  - single colour writes;
  - 89 band cases against the whole picture, with planted faults (`--fault`) to show the check
    fails.
- **A Python model** (`generators/m17model.py`) composes and draws a frame, and also draws it
  directly from the NES's rules. Compositions matched it byte for byte on 1,729 made-up lists,
  480 recorded frames of 1-1 and 2,176 frames from 32 levels. `m17pass1.py`, `m17front.py` and
  `m17behind.py` hold each pass against the model.
- **The game**: the ROM in the emulator against the engine's PC build, frame by frame (the `.uze`:
  105 scenarios, 446,000 frames, all equal). On the console, plays driven over the UART matched
  the PC's state on every report (13 plays, 50,400 frames).
