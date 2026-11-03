/* smb_ram.c -- the game state (smb_ram.h) and the 6502 register globals (smb.h) */
#include "smb.h"

struct RamArea ra;
TAIL17_FITS(TAIL_RP, sizeof(struct RamPersist), TAIL_RG);
TAIL17_FITS(TAIL_RG, sizeof(struct RamGame), TAIL_RAB);
TAIL17_FITS(TAIL_RAB, sizeof(struct RamAreaB), TAIL_RAC);
TAIL17_FITS(TAIL_RAC, sizeof(struct RamAreaC), TAIL_BB_CACHE);

