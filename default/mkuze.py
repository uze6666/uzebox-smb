#!/usr/bin/env python3
"""mkuze.py -- Super Uze Brothers as a .uze, its level data and music inside the file.

A .uze is 61,440 bytes of program at most (the bootloader keeps the chip's last 4 KB) and the
full version is more: so the SD=1 build has its level data and its music in a section of the
link that is not in the flash (.sddata), and the game reads them at power-on from its own file
on the card, after the program, with the version 5 bootloader's SD code (sd17.c). This makes
that file, and what the build and the emulator's checks need round it.

    python default/mkuze.py --id <objects...>
        the link's options for the data: its CRC-32 in two halves and its sectors, as symbols
        (-Wl,--defsym=sd_id_lo=... sd_id_hi=... sd_secs=...). The data is the objects' .sddata
        sections in the link's order, which is what the linked section holds: the pack checks it.

    python default/mkuze.py <the ROM's .elf> <its .hex> <out.uze> [gameinfo.properties]
        the file: the header as tools/packrom writes it (name=, author=, year=, desc= from the
        properties; no icon), the program (the .hex below 0xF000), filled to a sector's end
        with 0xFF, then the data's CRC-32 (4 bytes, the low one first) and the data, filled the
        same. The bootloader programs what the header's size says and no more; the game finds
        its file by the CRC (the file's name and its header's texts are free).

    python default/mkuze.py --hex <the ROM's .hex> <a bootloader's .hex> <out.hex>
        for the emulator, or a programmer: the game and a version 5 bootloader's image at
        0xF000 in one .hex, as a console that has loaded the .uze holds them.

    python default/mkuze.py --check <file.uze> <the ROM's .elf>
        the file read back: its header, its program's CRC, and its data against the ELF's.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

SECTION = ".sddata"
MAX_PROG = 61440          # 65,536 less the bootloader's 4,096
SECTOR = 512
MAX_SECS = 64             # the data's sectors: SPI RAM bank 1 from 0x7FFC to its end (sd17.c)


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        sys.exit("mkuze: %s failed:\n%s" % (cmd[0], r.stderr.decode(errors="replace")))
    return r.stdout.decode(errors="replace")


def section_bytes(path):
    """The section's bytes in an object or an ELF; nothing when it has none."""
    if not re.search(r"^\s*\d+\s+%s\s" % re.escape(SECTION), run(["avr-objdump", "-h", path]), re.M):
        return b""
    fd, tmp = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    try:
        run(["avr-objcopy", "-O", "binary", "-j", SECTION, path, tmp])
        return open(tmp, "rb").read()
    finally:
        os.remove(tmp)


def sectors(n):
    return (n + SECTOR - 1) // SECTOR


def data_figures(data):
    """(the CRC-32, the sectors the CRC and the data take in the file). The loader copies
    whole sectors into SPI RAM bank 1 from 0x7FFC: 64 of them end at the bank's last byte."""
    secs = sectors(4 + len(data))
    if secs > MAX_SECS:
        sys.exit("mkuze: *** the data is %d bytes, %d sectors with its CRC: %d at most fit SPI RAM's bank 1 from 0x7FFC"
                 % (len(data), secs, MAX_SECS))
    return zlib.crc32(data) & 0xffffffff, secs


def read_hex(path):
    mem, base = {}, 0
    for ln in open(path):
        ln = ln.strip()
        if not ln.startswith(":"):
            continue
        n, addr, kind = int(ln[1:3], 16), int(ln[3:7], 16), int(ln[7:9], 16)
        body = bytes.fromhex(ln[9:9 + 2 * n])
        if kind == 0:
            for i, b in enumerate(body):
                mem[base + addr + i] = b
        elif kind == 2:
            base = int.from_bytes(body, "big") << 4
        elif kind == 4:
            base = int.from_bytes(body, "big") << 16
        elif kind == 1:
            break
    return mem


def write_hex(path, mem):
    out = []
    addrs = sorted(mem)
    i = 0
    while i < len(addrs):
        a = addrs[i]
        row = [mem[a]]
        while len(row) < 16 and i + 1 < len(addrs) and addrs[i + 1] == a + len(row) and (a + len(row)) & 15:
            i += 1
            row.append(mem[addrs[i]])
        i += 1
        rec = bytes([len(row), a >> 8 & 255, a & 255, 0]) + bytes(row)
        out.append(":%s%02X" % (rec.hex().upper(), -sum(rec) & 255))
    out.append(":00000001FF")
    open(path, "w", newline="\n").write("\n".join(out) + "\n")


def symbols(elf):
    s = {}
    for ln in run(["avr-nm", elf]).splitlines():
        p = ln.split()
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def properties(path):
    d = {}
    if path:
        for ln in open(path, encoding="utf-8"):
            if "=" in ln and not ln.startswith("#"):
                k, v = ln.rstrip("\r\n").split("=", 1)
                d[k.strip()] = v
    return d


def header(prog, prop):
    def text(v, n):
        b = v.encode("latin-1")[:n - 1]
        return b + bytes(n - len(b))
    h = b"UZEBOX" + bytes([1, 0]) + struct.pack("<IH", len(prog), int(prop.get("year", "0") or 0))
    h += text(prop.get("name", ""), 32) + text(prop.get("author", ""), 32) + bytes(256)
    h += struct.pack("<I", zlib.crc32(prog) & 0xffffffff) + bytes([0]) + text(prop.get("desc", ""), 64) + bytes([0, 0])
    return h + bytes(SECTOR - len(h))


def program_of(hexfile):
    mem = read_hex(hexfile)
    low = {a: b for a, b in mem.items() if a < 0x10000}
    if not low:
        sys.exit("mkuze: %s has no program" % hexfile)
    size = max(low) + 1
    if size > MAX_PROG:
        sys.exit("mkuze: *** the program is %d bytes, %d over a .uze's %d: the bootloader would refuse the file"
                 % (size, size - MAX_PROG, MAX_PROG))
    return bytes(low.get(a, 0xff) for a in range(size))


def fill(b):
    return b + b"\xff" * (-len(b) % SECTOR)


def pack(elf, hexfile, out, prop_path):
    data = section_bytes(elf)
    if not data:
        sys.exit("mkuze: %s has no %s: is it an SD=1 build?" % (elf, SECTION))
    crc, secs = data_figures(data)
    s = symbols(elf)
    have = (s.get("sd_id_lo"), s.get("sd_id_hi"), s.get("sd_secs"))
    want = (crc & 0xffff, crc >> 16, secs)
    if have != want:
        sys.exit("mkuze: *** the program was linked for data %s and the ELF's data is %s (CRC low, high, sectors): "
                 "the link's options are not this data's (mkuze.py --id)" % (have, want))
    prog = program_of(hexfile)
    body = fill(prog) + fill(struct.pack("<I", crc) + data)
    open(out, "wb").write(header(prog, properties(prop_path)) + body)
    print("mkuze: %s: the program %d bytes (%d under a .uze's %d), the data %d bytes in %d sectors after it (CRC %08x); the file %d bytes"
          % (os.path.basename(out), len(prog), MAX_PROG - len(prog), MAX_PROG, len(data), secs, crc, SECTOR + len(body)))


def check(uze, elf):
    f = open(uze, "rb").read()
    if f[:6] != b"UZEBOX" or len(f) % SECTOR:
        sys.exit("mkuze: %s is not a .uze of whole sectors" % uze)
    size = struct.unpack("<I", f[8:12])[0]
    prog = f[SECTOR:SECTOR + size]
    bad = []
    if struct.unpack("<I", f[0x14e:0x152])[0] != zlib.crc32(prog) & 0xffffffff:
        bad.append("the header's CRC is not the program's")
    at = SECTOR * (1 + sectors(size))
    data = section_bytes(elf)
    crc, secs = data_figures(data)
    if struct.unpack("<I", f[at:at + 4])[0] != crc:
        bad.append("the data's CRC in the file is not the ELF's data's")
    if f[at + 4:at + 4 + len(data)] != data:
        bad.append("the data in the file is not the ELF's")
    if len(f) != at + SECTOR * secs:
        bad.append("the file is %d bytes for %d expected" % (len(f), at + SECTOR * secs))
    if bad:
        sys.exit("mkuze: *** %s: %s" % (uze, "; ".join(bad)))
    print("mkuze: %s is its ELF's: %d bytes of program, %d of data at sector %d" % (os.path.basename(uze), size, len(data), at // SECTOR))


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "--id":
        crc, secs = data_figures(b"".join(section_bytes(o) for o in a[1:]))
        print("-Wl,--defsym=sd_id_lo=0x%04x -Wl,--defsym=sd_id_hi=0x%04x -Wl,--defsym=sd_secs=%d" % (crc & 0xffff, crc >> 16, secs))
    elif len(a) == 4 and a[0] == "--hex":
        game, boot = read_hex(a[1]), read_hex(a[2])
        both = sorted(set(game) & set(boot))
        if both:
            sys.exit("mkuze: *** the two .hex files both have bytes from 0x%04x: the game must end under the bootloader" % both[0])
        if not boot or min(boot) < 0xF000:
            sys.exit("mkuze: %s is not a bootloader's image (it starts under 0xF000)" % a[2])
        game.update(boot)
        write_hex(a[3], game)
        print("mkuze: %s: the game to 0x%04x and the bootloader's image from 0x%04x" % (os.path.basename(a[3]), max(k for k in game if k < 0xF000), min(boot)))
    elif len(a) == 3 and a[0] == "--check":
        check(a[1], a[2])
    elif len(a) in (3, 4) and not a[0].startswith("--"):
        pack(a[0], a[1], a[2], a[3] if len(a) == 4 else None)
    else:
        sys.exit(__doc__.split("\n\n", 1)[1])


if __name__ == "__main__":
    main()
