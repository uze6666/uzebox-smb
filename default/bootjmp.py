#!/usr/bin/env python3
"""A jump to 0 at 0xF000: the ROM starts with either reset fuse.

Most Uzeboxes have the bootloader's fuse (high fuse 0xD2): the chip resets at 0xF000, not at 0.
A ROM over 61,440 bytes flashed with a programmer has its own code there, and starts in the
middle of it. This links the ROM with `jmp 0` at 0xF000.

The linker cannot put code round a fixed address without cutting .text in two, so only data
goes above the jump: PROGMEM tables, read by address wherever they are. No code changes but by
the bytes taken out in front of it.

    python default/bootjmp.py <objdir> <out.elf> <the link command, with its -Wl,-Map= and no -o>

  1. links as ever (<objdir>/plain.elf). A ROM that ends at 0xF000 or under needs nothing:
     its link is the one of before, byte for byte.
  2. from that link's map: the tables that were kept (.progmem.data.<name>, a section each),
     less the ones order17.ld places and the ones with an alignment; the set of them that
     fills 0xF004 to the chip's end best.
  3. the toolchain's own linker script with one output section more, .bootjmp at 0xF000 (the
     jump, then those tables), in front of .text so that its patterns are the first to match.
  4. checks what came out: the jump's four bytes, nothing else from 0xF000 on, the top within
     the chip.
"""
import os
import re
import subprocess
import sys

BOOT = 0xF000          # where a chip with the bootloader's fuse starts (BOOTSZ = 2048 words)
TOP = 0x10000          # the ATmega644's flash
JMP0 = bytes([0x0c, 0x94, 0x00, 0x00])


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    return r.returncode, r.stdout.decode(errors="replace").replace("\r\n", "\n")


def sections(elf):
    """the loaded sections of the flash: name -> (LMA, size)"""
    rc, out = run(["avr-objdump", "-h", elf])
    if rc:
        sys.exit("bootjmp: avr-objdump failed on %s\n%s" % (elf, out))
    d = {}
    lines = out.splitlines()
    for i, l in enumerate(lines):
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s", l)
        if m and i + 1 < len(lines) and "LOAD" in lines[i + 1]:
            size, lma = int(m.group(2), 16), int(m.group(4), 16)
            if size and lma < 0x800000:
                d[m.group(1)] = (lma, size)
    return d


def kept_tables(mapfile, ordered):
    """(name, size) of each PROGMEM table of its own section that the link kept, in the map's
    order; without the ones the ordering file names and the ones aligned"""
    text = open(mapfile, encoding="utf-8", errors="replace").read()
    a = text.index("Linker script and memory map")
    out, seen = [], set()
    for m in re.finditer(r"^ (\.progmem\.data\.[A-Za-z0-9_.$]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$", text[a:], re.M):
        name, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if not size or name in ordered or name in seen or not os.path.isfile(obj):
            continue
        rc, h = run(["avr-objdump", "-h", obj])
        al = re.search(r"\s%s\s+[0-9a-f]{8}\s+[0-9a-f]{8}\s+[0-9a-f]{8}\s+[0-9a-f]{8}\s+2\*\*(\d+)" % re.escape(name), h)
        if rc or not al or al.group(1) != "0":
            continue
        # a name in two objects (two statics of the same name) would move both: left alone
        if len(re.findall(r"^ %s\s" % re.escape(name), text[a:], re.M)) != 1:
            continue
        seen.add(name)
        out.append((name, size))
    return out


def choose(tables, room):
    """the set of tables whose bytes come nearest to `room` from under"""
    best = {0: []}
    for name, size in tables:
        for total, names in sorted(best.items(), reverse=True):
            t = total + size
            if t <= room and t not in best:
                best[t] = names + [name]
    t = max(best)
    return t, best[t]


def main():
    objdir, out, cmd = sys.argv[1], sys.argv[2], sys.argv[3:]
    mapopt = [c for c in cmd if c.startswith("-Wl,-Map=")]
    if len(mapopt) != 1:
        sys.exit("bootjmp: the link command must have one -Wl,-Map=")
    outmap = mapopt[0][len("-Wl,-Map="):]
    plain, plainmap = os.path.join(objdir, "plain.elf"), os.path.join(objdir, "plain.map")
    script = os.path.join(objdir, "bootjmp.ld")
    jobj = os.path.join(objdir, "bootjmp.o")
    base = [c for c in cmd if c != mapopt[0]]

    rc, log = run(base + ["-Wl,-Map=" + plainmap, "-o", plain])
    sys.stdout.write(log)
    if rc:
        sys.exit(rc)
    s = sections(plain)
    end = max(l + n for l, n in s.values())
    if end <= BOOT:
        rc, log = run(cmd + ["-o", out])
        sys.stdout.write(log)
        if rc:
            sys.exit(rc)
        print("bootjmp: the ROM ends at 0x%04x, under 0x%04x: no jump needed" % (end, BOOT))
        return

    # the link's own script (default/ldorder.py's: the toolchain's with order17.ld's statements
    # in it) is the base of this one, and it is given no other -T
    given = [c for c in cmd if c.startswith("-Wl,-T,")]
    if len(given) > 1:
        sys.exit("bootjmp: the link command has more than one -Wl,-T,")
    base_ld = open(given[0][len("-Wl,-T,"):]).read() if given else None
    cmd = [c for c in cmd if c not in given]
    ordered = set()
    if base_ld is not None:
        o = re.search(r"/\* order17\.ld \*/\n(.*?)/\* end of order17\.ld \*/", base_ld, re.S)
        if o:
            ordered = set(re.findall(r"\*\((\.progmem\.data\.[^)\s]+)\)", o.group(1)))
    tables = kept_tables(plainmap, ordered)
    room = TOP - (BOOT + len(JMP0))
    need = end - BOOT                       # what must leave the flash under the jump
    moved, names = choose(tables, room)
    if moved < need:
        sys.exit("bootjmp: *** the ROM is %d bytes: %d must go above 0x%04x and at most %d fit there with the jump "
                 "(%d of tables found to move). It does not fit with BOOTJMP=1" % (end, need, BOOT, room, moved))

    empty = os.path.join(objdir, "bootjmp-empty.c")
    open(empty, "w").write("")
    if base_ld is not None:
        ld = base_ld
    else:
        rc, log = run([cmd[0]] + [c for c in cmd if c.startswith("-mmcu=")] + ["-Wl,--verbose", "-nostdlib", empty, "-o", empty + ".elf"])
        m = re.search(r"^=+\n(.*?)^=+\n", log, re.S | re.M)
        if not m:
            sys.exit("bootjmp: the toolchain's linker script was not found in its --verbose output")
        ld = m.group(1)
    if len(re.findall(r"^  \.text   :\n", ld, re.M)) != 1:
        sys.exit("bootjmp: the toolchain's linker script has no '  .text   :' line: this tool is for the one it knows")
    block = ("  /* default/bootjmp.py: the bootloader's reset address. The jump, then tables that are\n"
             "  ** read by address; in front of .text so that these patterns match first */\n"
             "  .bootjmp 0x%x :\n  {\n    KEEP(*(.bootjmp))\n%s  }  > text\n"
             "  .text 0x0 :\n" % (BOOT, "".join("    *(%s)\n" % n for n in names)))
    ld = re.sub(r"^  \.text   :\n", lambda _: block, ld, flags=re.M)
    open(script, "w", newline="\n").write(ld)

    src = os.path.join(objdir, "bootjmp.S")
    open(src, "w", newline="\n").write(
        "; default/bootjmp.py: jmp 0, as its two words (no relocation to resolve)\n"
        "\t.section .bootjmp,\"ax\",@progbits\n\t.global bootjmp\nbootjmp:\n\t.word 0x940c, 0x0000\n")
    rc, log = run([cmd[0]] + [c for c in cmd if c.startswith("-mmcu=")] + ["-c", src, "-o", jobj])
    if rc:
        sys.exit("bootjmp: the jump did not assemble\n" + log)

    rc, log = run(cmd + [jobj, "-Wl,-T," + script, "-o", out])
    sys.stdout.write(log)
    if rc:
        sys.exit(rc)

    s = sections(out)
    bad = []
    if ".bootjmp" not in s or s[".bootjmp"][0] != BOOT:
        bad.append(".bootjmp is not at 0x%04x" % BOOT)
    else:
        rc, _ = run(["avr-objcopy", "-O", "binary", "-j", ".bootjmp", out, jobj + ".bin"])
        if rc or open(jobj + ".bin", "rb").read(4) != JMP0:
            bad.append("the four bytes at 0x%04x are not jmp 0" % BOOT)
        if s[".bootjmp"][1] != len(JMP0) + moved:
            bad.append(".bootjmp is %d bytes for %d expected" % (s[".bootjmp"][1], len(JMP0) + moved))
    for name, (lma, size) in sorted(s.items()):
        if name != ".bootjmp" and lma + size > BOOT:
            bad.append("%s ends at 0x%04x, over 0x%04x" % (name, lma + size, BOOT))
        if lma + size > TOP:
            bad.append("%s ends at 0x%05x, over the chip's flash" % (name, lma + size))
    if sorted(n for n in s if n != ".bootjmp") != sorted(sections(plain)):
        bad.append("the sections are not the plain link's and .bootjmp")
    if bad:
        os.remove(out)
        sys.exit("bootjmp: *** " + "; ".join(bad))
    low = max(l + n for k, (l, n) in s.items() if k != ".bootjmp")
    print("bootjmp: jmp 0 at 0x%04x and %d tables above it (%d bytes, %d free there); the rest ends at 0x%04x (%d free under the jump)"
          % (BOOT, len(names), moved, room - moved, low, BOOT - low))


if __name__ == "__main__":
    main()
