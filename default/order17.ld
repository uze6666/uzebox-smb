/* order17.ld -- the start of the mode 17 ROM's flash (make VMODE=17; the Makefile gives it to
** the linker as --section-ordering-file: binutils 2.43 or later).
**
** Three tables need a page boundary: the mode's pair tables (2 pages; their index is a
** pointer's low byte), the mixer's waves (4 pages), the background's tiles (the mode reads a
** tile from the table's high byte). Linked where their objects fall, each pays its own gap:
** 308 bytes in all. Here they are back to back from the first boundary after the vectors,
** 0x0100, and what is in front of them is small tables that use most of the gap that is left.
**
** The linker puts these statements at the start of .text, before its own: so the vectors are
** named first. A pattern that matches nothing is no error; a table that outgrows the gap
** moves the pair tables a page up, which default/ramcheck17.py refuses at the link. */
.text : {
  *(.vectors)
  *(.progmem.data.m17_dec4)
  *(.progmem.data.m17_sprmask)
  *(.progmem.data.io_table)
  *(.progmem.data.FontTiles17)
  *(.progmem.data.MTilesTab)
  *(.progmem.data.m17_pairs)
  *(.text.waves)
  *(.text.gfx17_bg_tiles)
}
