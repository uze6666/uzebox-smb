"""The ROM's linker script: the toolchain's own, with order17.ld's statements at the start of
its .text (2026-11-01).

    python default/ldorder.py <objdir> <cc> -mmcu=<mcu> order17.ld <out>

order17.ld was given to the linker as --section-ordering-file, which puts its statements at the
start of .text, before the script's own. binutils 2.47 refuses any such file ("linker script
file 'order17.ld (.../ldscripts/avr5.xn)' appears multiple times": a two-line file and an empty
main do it). This makes the same script and the Makefile gives it with -T, which every
binutils takes: under avr-gcc 14.2.0 and binutils 2.45 the ROMs are the bytes they were.

The statements go between two comments, which default/bootjmp.py reads (the tables it must not
move) when it makes its own script from this one.
"""
import os
import re
import subprocess
import sys

BEGIN = "    /* order17.ld */\n"
END = "    /* end of order17.ld */\n"


def main():
    objdir, cc, mmcu, order, out = sys.argv[1:6]
    empty = os.path.join(objdir, "ldorder-empty.c")
    open(empty, "w").write("")
    p = subprocess.run([cc, mmcu, "-Wl,--verbose", "-nostdlib", empty, "-o", empty + ".elf"],
                       capture_output=True, text=True)
    m = re.search(r"^=+\n(.*?)^=+\n", p.stdout + p.stderr, re.S | re.M)
    if not m:
        sys.exit("ldorder: the toolchain's linker script was not found in its --verbose output")
    ld = m.group(1)

    text = re.sub(r"/\*.*?\*/", "", open(order).read(), flags=re.S)
    o = re.fullmatch(r"\s*\.text\s*:\s*\{(.*?)\}\s*", text, re.S)
    if not o:
        sys.exit("ldorder: %s must be one '.text : { ... }' and nothing else" % order)
    body = "".join("    %s\n" % s.strip() for s in o.group(1).splitlines() if s.strip())

    head = re.findall(r"^  \.text   :\n  \{\n", ld, re.M)
    if len(head) != 1:
        sys.exit("ldorder: the toolchain's linker script has no '  .text   :' and '{' lines: this tool is for the one it knows")
    ld = re.sub(r"^  \.text   :\n  \{\n", lambda h: h.group(0) + BEGIN + body + END, ld, flags=re.M)
    open(out, "w", newline="\n").write(ld)


if __name__ == "__main__":
    main()
