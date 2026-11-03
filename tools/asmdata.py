"""asmdata.py -- the cartridge's data tables, by the disassembly's label, read from your own ROM.

Nothing of the cartridge is in this repository. tools/romdata.map lists, for each data table the
generators use, its address in the ROM's program and the shape of its data (how many bytes or
words, and the label references between them); the bytes themselves are read from the .nes.

The ROM is the one file named by the environment's SMB_NES, or else the one .nes in ROM/ at the
repository's root. It must be Super Mario Bros. for the NES (the "World" / "Japan, USA" dump):
its program's CRC-32 is checked against the map's.

parse() gives what the disassembly's .db/.dw lines after each label would: (data, order), data a
label's list of (directive, token), a number as "$xx" (a word "$xxxx") and a reference as written.
"""
import glob
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
MAP = os.path.join(HERE, "romdata.map")
ROM_DIR = os.path.join(ROOT, "ROM")
PRG_SIZE, CHR_SIZE = 0x8000, 0x2000


def fail(msg):
    sys.exit("*** " + msg)


def rom_path():
    """the .nes: $SMB_NES, or the one .nes in ROM/"""
    p = os.environ.get("SMB_NES")
    if p:
        if not os.path.isfile(p):
            fail("SMB_NES=%s: no such file" % p)
        return p
    found = {}
    for f in glob.glob(os.path.join(ROM_DIR, "*.nes")) + glob.glob(os.path.join(ROM_DIR, "*.NES")):
        found.setdefault(os.path.normcase(f), f)        # one file, whatever its case
    found = sorted(found.values())
    if not found:
        fail("no ROM: put your own Super Mario Bros. .nes in %s (see ROM/README.txt)" % ROM_DIR)
    if len(found) > 1:
        fail("more than one .nes in %s: keep one, or name it with NES=<the .nes>" % ROM_DIR)
    return found[0]


def read_ines(path):
    """(prg, chr) of an iNES file: 32 KB of program, then 8 KB of CHR-ROM"""
    d = open(path, "rb").read()
    if d[:4] != b"NES\x1a":
        fail("%s: not an iNES (.nes) file" % path)
    at = 16 + (512 if d[6] & 4 else 0)                  # a trainer, if any, before the program
    if d[4] != 2 or d[5] != 1 or len(d) < at + PRG_SIZE + CHR_SIZE:
        fail("%s: not Super Mario Bros. (32 KB of program and 8 KB of CHR expected)" % path)
    return d[at:at + PRG_SIZE], d[at + PRG_SIZE:at + PRG_SIZE + CHR_SIZE]


def check_rom(path=None):
    """the ROM's (prg, chr), after its program is checked against the map's CRC-32"""
    path = path or rom_path()
    prg, chr_ = read_ines(path)
    want = _read_map()[2]
    got = zlib.crc32(prg)
    if got != want:
        fail("%s: program CRC-32 %08x, %08x expected: not the Super Mario Bros. ROM this port "
             "is made from (the NES's \"World\" / \"Japan, USA\" one)" % (path, got, want))
    return prg, chr_


def _read_map():
    labels, order, crc = {}, [], None
    for line in open(MAP):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        if f[0] == "prg_crc32":
            crc = int(f[1], 16)
            continue
        labels[f[0]] = (int(f[1], 16), f[2:])
        order.append(f[0])
    if crc is None:
        fail("%s: no prg_crc32 line" % MAP)
    return labels, order, crc


def parse(path=None):
    """(data, order): label -> list of (directive, token) for the data after it, from the ROM"""
    prg, _ = check_rom(path)
    labels, order, _ = _read_map()
    data = {}
    for lab in order:
        addr, items = labels[lab]
        at = addr - 0x8000
        out = []
        for it in items:
            m = re.fullmatch(r"(w?)#(\d+)", it)
            if m and m.group(1):
                for _ in range(int(m.group(2))):
                    out.append(("dw", "$%04x" % (prg[at] | prg[at + 1] << 8)))
                    at += 2
            elif m:
                for _ in range(int(m.group(2))):
                    out.append(("db", "$%02x" % prg[at]))
                    at += 1
            elif it.startswith("dw:"):
                _check_ref(prg, at, it[3:], 2, labels)
                out.append(("dw", it[3:]))
                at += 2
            else:
                _check_ref(prg, at, it, 1, labels)
                out.append(("db", it))
                at += 1
        data[lab] = out
    return data, order


def _check_ref(prg, at, tok, size, labels):
    """a reference the map knows the address of must be the ROM's byte (or word) there"""
    name = tok[1:] if tok[0] in "<>" else tok
    if name not in labels:
        return                                          # code labels, constants, expressions
    a = labels[name][0]
    want = (a & 0xff if tok[0] == "<" else a >> 8) if tok[0] in "<>" else a
    got = prg[at] if size == 1 and tok[0] in "<>" else prg[at] | prg[at + 1] << 8
    if size == 1 and tok[0] not in "<>":
        return
    if got != want:
        fail("the ROM's data does not match the map at $%04x (%s)" % (0x8000 + at, tok))


def value(t):
    t = t.strip()
    if t.startswith("$"):
        return int(t[1:], 16)
    if t.startswith("%"):
        return int(t[1:], 2)
    if re.fullmatch(r"\d+", t):
        return int(t)
    raise ValueError(t)


_cache = None


def bytes_of(label):
    global _cache
    if _cache is None:
        _cache = parse()
    return [value(t) for kind, t in _cache[0][label]]


if __name__ == "__main__":
    if sys.argv[1:] == ["--path"]:                      # for the shell scripts
        print(rom_path())
        sys.exit(0)
    p = rom_path()
    check_rom(p)
    d, o = parse(p)
    print("%s: the ROM this port is made from; %d tables, %d items" % (p, len(o), sum(len(v) for v in d.values())))
