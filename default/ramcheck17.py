#!/usr/bin/env python3
"""ramcheck17.py -- the SRAM map of a mode 17 build of the game, from its ELF, and a check that
it is what the build's constants say and leaves the stack its room. The Makefile runs it at
every link (make VMODE=17).

    python default/ramcheck17.py uzebros.elf [stack bytes wanted, 190]

The ATmega644's SRAM is 0x0100-0x10FF. Nothing in the link says a build overran it (the stack
would just start inside .bss), nor that the engine's OAM, an address its files were compiled
with, is the page the linker gave the mode's sprite list. This says so, and fails the build.

The flash too, for one thing: the three tables that need a page boundary are back to back at
its start (order17.ld), and what the linker filled in front of them is small. A table that
outgrew the gap would move them a page up, 256 bytes, without a word.
"""
import os
import re
import subprocess
import sys

RAM_START, RAM_END = 0x0100, 0x1100
STACK_WANTED = 190      # the plan's budget: the engine's frame, a picture that cuts it, a margin
TABLES_AT = 0x0100      # the first page boundary after the vectors
FILL_MAX = 32           # bytes the linker may fill in the whole flash for alignment


def symbols(elf):
    out = subprocess.run(["avr-nm", "-n", elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16) & 0xFFFF
    return syms


def flash_fill(mapfile):
    """The bytes the linker filled in the flash (fills of 2 bytes and more), from the map; None
    when there is no map beside the ELF."""
    if not os.path.exists(mapfile):
        return None
    text = open(mapfile, errors="replace").read()
    text = text[text.find("Linker script and memory map"):]
    n = 0
    for m in re.finditer(r"\*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", text):
        if int(m.group(1), 16) < 0x800000 and int(m.group(2), 16) >= 2:
            n += int(m.group(2), 16)
    return n


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.split("\n\n")[1])
    want = int(sys.argv[2]) if len(sys.argv) > 2 else STACK_WANTED
    s = symbols(sys.argv[1])
    run, run_end = s["m17_run"], s["m17_run_end"]
    data, bss, end = s["__data_start"], s["__bss_start"], s["__bss_end"]
    game = s["uzebros_game_at"]
    n = (s["m17_restore"] - s["m17_ram_tiles"]) // 32
    vram = s["m17_ram_tiles"] - s["vram"]
    state = run_end - s["m17_restore"] - 3 * n
    rows = [
        ("the mode's run: T2 and the bar's row, T1", 512),
        ("    the sprite list, which is the engine's OAM", s["vram"] - s["m17_sprites"]),
        ("    VRAM: the playfield's rows", vram),
        ("    %d RAM tiles and their restore entries" % n, 35 * n),
        ("    the mode's state", state),
        ("the game's block of fixed addresses (tails17.h)", data - game),
        (".data", bss - data),
        (".bss: the engine's ra, the kernel, sound, the platform", end - bss),
    ]
    used = end - RAM_START
    stack = RAM_END - end
    print("SRAM, %s:" % sys.argv[1])
    for name, v in rows:
        print("  %5d  %s" % (v, name))
    print("  %5d  in all, of %d; the stack has %d" % (used, RAM_END - RAM_START, stack))
    bad = []
    if sum(v for _, v in rows) != used or run != RAM_START or game != run_end:
        bad.append("the parts do not follow each other from %#x: the run %#x-%#x, the game's block %#x, .data %#x"
                   % (RAM_START, run, run_end, game, data))
    if s["m17_sprites"] != s["uzebros_oam_at"] or s["m17_sprites"] & 255:
        bad.append("the mode's sprite list is at %#x, and the engine was compiled for an OAM at %#x"
                   % (s["m17_sprites"], s["uzebros_oam_at"]))
    if stack < want:
        bad.append("the stack has %d bytes, %d wanted: %d too many (a RAM tile is 35)" % (stack, want, want - stack))
    # the flash: the pair tables at the first boundary, the waves on their heels, then the tiles
    pairs, waves, tiles = s["m17_enc15"], s["waves"], s["gfx17_bg_tiles"]
    fill = flash_fill(os.path.splitext(sys.argv[1])[0] + ".map")
    print("flash: the pair tables at %#06x, the waves at %#06x, the tiles at %#06x; %s bytes filled for alignment"
          % (pairs, waves, tiles, "?" if fill is None else fill))
    if pairs != TABLES_AT or waves != pairs + 512 or tiles <= waves or (tiles - waves) & 255:
        bad.append("the page-aligned tables are not back to back from %#x (order17.ld): pairs %#x, waves %#x, tiles %#x"
                   % (TABLES_AT, pairs, waves, tiles))
    if fill is not None and fill > FILL_MAX:
        bad.append("%d bytes of flash are alignment fill, %d allowed: a table in front of the pair tables grew, "
                   "or one of the three left its place (order17.ld)" % (fill, FILL_MAX))
    for b in bad:
        print("*** %s: %s" % (sys.argv[1], b), file=sys.stderr)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
