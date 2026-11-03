; The game's tiles on video mode 17: the cartridge's own, as tools/gen_gfx17.py wrote them
; (gfxtiles17.inc; make gen17 NES=<the .nes>). gfx17.c names the three tables.

; (NESTITLE=1: its second set, the title as the cartridge has it)
#if NESTITLE
#include "gfxtiles17n.inc"
#else
#include "gfxtiles17.inc"
#endif
