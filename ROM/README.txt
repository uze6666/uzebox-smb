Put your own Super Mario Bros. ROM here: one .nes file, any name.

It is the NES cartridge's image in iNES format, the "World" / "Japan, USA" release
(40,976 bytes with its 16-byte header). Its program must have the CRC-32 5cf548d3 and
its CHR the CRC-32 867b51ad; `python tools/asmdata.py` tells you whether yours is the one.

Then, in default/:

    make assets     the levels, the sound, the texts and the tiles, made from it (in data/)
    make            uzebros.hex
    make uze        uzebros.uze

Nothing in this folder but this file is ever committed (.gitignore), and nothing made
from the ROM is either. Dump the ROM from a cartridge you own.
