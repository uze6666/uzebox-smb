###############################################################################
# Super Uze Brothers: a port of Super Mario Bros. to the Uzebox, on video mode 17
# (../customVideoMode17: the NES's own 8x8 tiles and palettes).
#
#   make assets   first, once: everything that is the cartridge's, from your own ROM (put the
#                 .nes in ../ROM, or NES=<the .nes>), written to ../data
#   make          uzebros.hex: the game, alone on the ATmega644's 64 KB (an ISP programmer)
#   make uze      uzebros.uze: the game for the bootloader (version 5.0.06 or later), with what
#                 the .hex has no room for: the game for two players, the title as the
#                 cartridge has it, the top score kept in the EEPROM. Its level data and music
#                 follow the program in the file and are read from the card at power-on
#                 (SD=1: uzebros-sd.hex is its program)
#   make clean
#
# Never -mrelax: the kernel and the video mode are cycle counted.
###############################################################################

MCU  = atmega644
CC   = avr-gcc
GAME = uzebros

ROOT       = ..
KERNEL_DIR = $(ROOT)/kernel
M17_DIR    = $(ROOT)/customVideoMode17
DATA_DIR   = $(ROOT)/data
TOOLS_DIR  = $(ROOT)/tools

## SD=1 (make uze): the .uze's build
SD ?= 0
ifeq ($(SD),1)
  OUT    = $(GAME)-sd
  OBJDIR = obj/sd
else
  OUT    = $(GAME)
  OBJDIR = obj/release
endif

## Video mode 17: M17_ROWS rows shown, the first the status bar's, M17_RAM_TILES RAM tiles for
## the sprites, M17_DRAW_MAX sprites a composition at most (the others take turns), the
## player's 8 sprites drawn first. The mode's memory (its run) is at M17_RUN_AT; the engine's
## OAM is the mode's sprite list (M17_OAM_AT); the game's block of fixed addresses (tails17.h)
## follows the run at M17_GAME_AT, and .data follows that
M17_ROWS       = 25
M17_RAM_TILES  = 38
M17_DRAW_MAX   = 24
M17_RUN_AT     = 0x0100
KERNEL_OPTIONS = -DVIDEO_MODE=0 -DVIDEO_MODE_PATH=$(realpath $(M17_DIR))
KERNEL_OPTIONS += -DM17_ROWS=$(M17_ROWS) -DM17_RAM_TILES=$(M17_RAM_TILES) -DM17_HUD=1 -DM17_DRAW_MAX=$(M17_DRAW_MAX) -DM17_FIRST_AT=1 -DM17_FIRST_N=8
## the run's size and its sprite list's offset are m17.def.h's, for these options
M17_DEF       = echo "$(1)" | $(CC) -E -P -x c -include $(M17_DIR)/m17.def.h $(KERNEL_OPTIONS) - | tail -n 1
M17_RUN_SIZE := $(shell echo $$(( $$($(call M17_DEF,M17_RUN_SIZE)) )))
M17_OAM_AT   := $(shell printf '0x%04x' $$(( $(M17_RUN_AT) + $$($(call M17_DEF,M17_RUN_SPRITES)) )))
M17_GAME_AT  := $(shell printf '0x%04x' $$(( $(M17_RUN_AT) + $(M17_RUN_SIZE) )))
KERNEL_OPTIONS += -DNO_EEPROM_FORMAT=1 -DTRUE_RANDOM_GEN=0 -DKERNEL_LEAN=1 -DUART=0
## the kernel's inline mixer, channel 5 off; what sets it is the game's own sound engine
## (../sound.c, ../apu.c), not the kernel's player
KERNEL_OPTIONS += -DENABLE_MIXER=1 -DSOUND_MIXER=1 -DSOUND_CHANNEL_5_ENABLE=0
## the pad is read just before the game's frame, as the NES did, not by the kernel after it
KERNEL_OPTIONS += -DCONTROLLERS_VSYNC_READ=0
## the mixer's waves: the NES pulse duties and its triangle (../data/waves.inc)
KERNEL_OPTIONS += -DMIXER_WAVES=\"waves.inc\"
GAME_OPTIONS    = -DM17_RUN_AT=$(M17_RUN_AT) -DM17_OAM_AT=$(M17_OAM_AT) -DM17_GAME_AT=$(M17_GAME_AT)
## the 6502's registers in r2-r7 (../smb.h): no C file may use them, the kernel's included
GAME_OPTIONS += -ffixed-r2 -ffixed-r3 -ffixed-r4 -ffixed-r5 -ffixed-r6 -ffixed-r7
## the engine's blank sprite tile, named to the mode as a tile with no pixel
KERNEL_OPTIONS += -DM17_SPR_NONE=0xFC

## SD=1: a .uze is 61,440 bytes at most, so the level data and the music are not in the flash:
## a section of their own (.sddata, at SDDATA_AT where there is no memory: a pointer to them is
## their SPI RAM address), put in the .uze after the program by mkuze.py and read from the card
## at power-on with the bootloader's own SD code (../sd17.c). With it, what the .hex has no
## room for: the game for two (TWO_PLAYERS), the title as the cartridge draws it (NESTITLE),
## the top score in the EEPROM (TOPSAVE, block TOPSAVE_ID)
SDDATA_AT  = 0x808000
TOPSAVE_ID = 0x5342
ifeq ($(SD),1)
  GAME_OPTIONS += -DSD=1
  SD_O          = sd17.o
  GAME_OPTIONS += -DTWO_PLAYERS=1 -DNESTITLE=1 -DTOPSAVE=1 -DTOPSAVE_ID=$(TOPSAVE_ID)
  GFX17_INC     = gfxtiles17n.inc
  GFX17_H       = gfxdata17n.h
else
  GFX17_INC     = gfxtiles17.inc
  GFX17_H       = gfxdata17.h
endif

ENGINE  = smb_ram.o bb.o area.o player.o objects.o enemies.o bosses.o collide.o bgcoll.o \
          sprites.o gfx17.o text.o sound.o apu.o game.o main17.o $(SD_O)
SRC     = uzeboxVideoEngineCore.o uzeboxCore.o uzeboxSoundEngineCore.o spiram.o \
          uzeboxVideoEngine.o tiles17.o $(ENGINE)
OBJECTS = $(addprefix $(OBJDIR)/,$(SRC))
## the game's own objects: -ftoplevel-reorder (without it GCC never inlines a function called
## once) and -fno-split-wide-types; the kernel and the mode keep -fno-toplevel-reorder
$(addprefix $(OBJDIR)/,$(ENGINE) $(ENGINE:.o=.pre.o)): CFLAGS += -ftoplevel-reorder -fno-split-wide-types

CFLAGS  = -mmcu=$(MCU) -mcall-prologues -Os -Wall -Wextra -std=gnu99 -DF_CPU=28636360UL
CFLAGS += -fsigned-char -ffunction-sections -fdata-sections -fno-toplevel-reorder -Wa,-no-pad-sections
CFLAGS += -MD -MP -MT $@ -MF $@.d
CFLAGS += $(KERNEL_OPTIONS) $(GAME_OPTIONS)
ASMFLAGS  = $(CFLAGS) -Wa,-mno-link-relax -x assembler-with-cpp -Wa,-gdwarf2
LDFLAGS   = -mmcu=$(MCU) -mcall-prologues -Os -Wl,-Map=$(OUT).map -Wl,-gc-sections
INCLUDES  = -I"$(KERNEL_DIR)" -I$(ROOT) -I$(DATA_DIR)

## the mode's run at M17_RUN_AT, the game's block after it (tails17.h's TAIL17_SIZE bytes),
## .data after that
TAIL17_SIZE := $(shell echo TAIL17_SIZE | $(CC) -E -P -x c -include $(ROOT)/tails17.h -DM17_GAME_AT=0 - | tail -n 1)
M17_RUN_START := $(shell printf '0x%x' $$(( 0x800000 + $(M17_RUN_AT) )))
M17_DATA_START := $(shell printf '0x%x' $$(( 0x800000 + $(M17_GAME_AT) + $(TAIL17_SIZE) )))
LDFLAGS  += -Wl,--section-start=.m17run=$(M17_RUN_START) -Wl,--section-start=.data=$(M17_DATA_START)
LDFLAGS  += -Wl,--defsym=uzebros_oam_at=$(M17_OAM_AT) -Wl,--defsym=uzebros_game_at=$(M17_GAME_AT)
## the three tables that need a page boundary (the mode's pairs, the waves, the tiles) back to
## back at the start of the flash (order17.ld), inside the toolchain's own script (ldorder.py)
LDSCRIPT17 = $(OBJDIR)/order17.x
LDFLAGS  += -Wl,-T,$(LDSCRIPT17)
LINK_DEPS = $(LDSCRIPT17)
ifeq ($(SD),1)
## the data's section where no memory is; its CRC-32 and sectors, which sd17.c checks, are the
## link's own options, from the objects
LDFLAGS  += -Wl,--section-start=.sddata=$(SDDATA_AT)
LINK_DEPS += mkuze.py
SD_ID     = $$(python mkuze.py --id $(OBJECTS))
endif
## the SRAM map at every link: the build fails when the stack is left too little
RAMCHECK  = python ramcheck17.py

FIX_DEPS = sed -i 's|\([A-Za-z]\):/|/\L\1/|g' $@.d

vpath %.c $(ROOT) $(KERNEL_DIR)
vpath %.s $(ROOT) $(KERNEL_DIR)

.PHONY: all clean size assets uze uzefile FORCE
## a link that fails the RAM check leaves no .elf behind
.DELETE_ON_ERROR:
all: $(OUT).hex size

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LDSCRIPT17): order17.ld ldorder.py $(STAMP) | $(OBJDIR)
	@python ldorder.py $(OBJDIR) $(CC) -mmcu=$(MCU) order17.ld $@

## The stamp: what an object is compiled and linked with, rewritten only when it differs: a
## changed option rebuilds
STAMP      = $(OBJDIR)/flags.stamp
STAMP_TEXT = $(CC) $(INCLUDES) $(CFLAGS) / $(LDFLAGS)
FORCE:
$(STAMP): FORCE | $(OBJDIR)
	@printf '%s\n' '$(STAMP_TEXT)' | cmp -s - $@ || printf '%s\n' '$(STAMP_TEXT)' > $@

$(OBJDIR)/%.o: %.c $(STAMP) Makefile | $(OBJDIR)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@
	@$(FIX_DEPS)

## The game's own objects, from the compiler's assembly through three passes, each with its
## own --check that nothing else changed:
## - tailjmp.py: call f; ret -> jmp f (never to the kernel or the C library);
## - tailmerge.py: the same instruction tails kept once, the others an RJMP to them;
## - shortcall.py: a CALL or JMP to a function of the same object in reach made short.
## tailmerge.py must know which functions the linker drops (--gc-sections): each object is
## first made as before the passes (<object>.pre.o), those are linked once for the linker's
## list (dropped.txt), then the passes run, and the ROM's own link confirms the list.
## That first link is of the code before the passes: it is given 128 KB of flash
ENGINE_AS    = $(CC) -mmcu=$(MCU) $(filter -Wa%,$(CFLAGS)) -c
ENGINE_TOOLS = tailjmp.py shortcall.py tailmerge.py
ENGINE_OBJS  = $(addprefix $(OBJDIR)/,$(ENGINE))
comma       := ,
PRE_ROOM     = -Wl,--defsym=__TEXT_REGION_LENGTH__=128K
ENGINE_PRE   = $(ENGINE_OBJS:.o=.pre.o)
DROPPED      = $(OBJDIR)/dropped.txt
$(ENGINE_PRE): $(OBJDIR)/%.pre.o: %.c $(STAMP) Makefile tailjmp.py | $(OBJDIR)
	$(CC) $(INCLUDES) $(CFLAGS) -S $< -o $(OBJDIR)/$*.o.0.s
	@$(FIX_DEPS)
	@cp $(OBJDIR)/$*.o.0.s $(OBJDIR)/$*.o.1.s && python tailjmp.py $(OBJDIR)/$*.o.1.s && python tailjmp.py --check $(OBJDIR)/$*.o.0.s $(OBJDIR)/$*.o.1.s >/dev/null
	$(ENGINE_AS) $(OBJDIR)/$*.o.1.s -o $@
DROPS        = python tailmerge.py --drops $(OBJDIR)/dropped.done.new $(OBJDIR)/pre.map $(OBJDIR)/dropped.done.log $(ENGINE_PRE) >/dev/null
$(OBJDIR)/dropped.done: $(ENGINE_PRE) $(filter-out $(ENGINE_OBJS),$(OBJECTS)) $(STAMP) Makefile $(LINK_DEPS) tailmerge.py shortcall.py
	@LC_ALL=C $(CC) $(filter-out -Wl$(comma)-Map=%,$(LDFLAGS)) $(PRE_ROOM) -Wl,-Map=$(OBJDIR)/pre.map -Wl,--print-gc-sections $(foreach o,$(OBJECTS),$(if $(filter $(o),$(ENGINE_OBJS)),$(o:.o=.pre.o),$(o))) -o $(OBJDIR)/pre.elf > $@.log 2>&1 || { cat $@.log >&2; exit 1; }
	@rm -f $@.new && $(DROPS)
	@touch $@
$(DROPPED): $(OBJDIR)/dropped.done FORCE
	@test -f $<.new || $(DROPS) || { rm -f $<; exit 1; }
	@cmp -s $<.new $@ || cp $<.new $@
DROPPED_CHECK = python tailmerge.py --linked $(DROPPED) $(OUT).map $(ENGINE_OBJS) >/dev/null || { rm -f $(OBJDIR)/dropped.done; exit 1; }
$(ENGINE_OBJS): $(OBJDIR)/%.o: $(OBJDIR)/%.pre.o $(DROPPED) $(STAMP) Makefile $(ENGINE_TOOLS) | $(OBJDIR)
	@cp $@.1.s $@.s && python tailmerge.py $@.s --min 2 --dropped $(DROPPED) $* -- $(ENGINE_AS) && python tailmerge.py --check $@.1.s $@.s >/dev/null
	@cp $@.s $@.2.s && python shortcall.py $@.s -- $(ENGINE_AS) && python shortcall.py --check $@.2.s $@.s >/dev/null
	$(ENGINE_AS) $@.s -o $@

$(OBJDIR)/%.o: %.s $(STAMP) Makefile | $(OBJDIR)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@
	@$(FIX_DEPS)
	@if avr-readelf -h $@ | grep -q link-relax; then \
		echo "error: $< was assembled for linker relaxation" >&2; rm -f $@; exit 1; \
	fi

$(OBJDIR)/tiles17.o: $(DATA_DIR)/$(GFX17_INC)
$(OBJDIR)/gfx17.o $(OBJDIR)/gfx17.pre.o: $(DATA_DIR)/$(GFX17_H)

## flags17.mk: the compiler flags of each C object that make the linked ROM smaller (found an
## object at a time), after the Makefile's own. Each object has a stamp of its own flags
include flags17.mk
F17_OBJS  = $(basename $(ENGINE)) uzeboxCore uzeboxVideoEngine
define F17_RULE
$(OBJDIR)/$(1).o: private CFLAGS += $$(FLAGS17_$(1))
$(OBJDIR)/$(1).o: $(OBJDIR)/$(1).flags17
$(OBJDIR)/$(1).pre.o: private CFLAGS += $$(FLAGS17_$(1))
$(OBJDIR)/$(1).pre.o: $(OBJDIR)/$(1).flags17
endef
$(foreach o,$(F17_OBJS),$(eval $(call F17_RULE,$(o))))
$(OBJDIR)/%.flags17: FORCE | $(OBJDIR)
	@printf '%s\n' '$(FLAGS17_$*)' | cmp -s - $@ || printf '%s\n' '$(FLAGS17_$*)' > $@

## the link, with a jmp 0 at 0xF000 for a chip whose reset is the bootloader's (bootjmp.py)
$(OUT).elf: $(OBJECTS) $(STAMP) Makefile $(LINK_DEPS) bootjmp.py
	python bootjmp.py $(OBJDIR) $@ $(CC) $(LDFLAGS) $(SD_ID) $(OBJECTS)
	@$(RAMCHECK) $@
	@$(DROPPED_CHECK)

$(OUT).hex: $(OUT).elf
	avr-objcopy -O ihex -R .eeprom -R .sddata $< $@

## the flash it takes (.text, .data's image and bootjmp.py's section): the chip's 65,536
## bytes, or a .uze's 61,440 (the bootloader has the chip's last 4,096)
ifeq ($(SD),1)
FLASH_MAX = 61440
else
FLASH_MAX = 65536
endif
size: $(OUT).elf
	@avr-size -C --mcu=$(MCU) $(OUT).elf
	@n=$$(avr-size -A $(OUT).elf | awk '$$1==".text"||$$1==".data"||$$1==".bootjmp"{n+=$$2} END{print n}'); if [ $$n -gt $(FLASH_MAX) ]; then echo "*** $(OUT): $$n bytes, $$(( n - $(FLASH_MAX) )) over $(FLASH_MAX): it does not fit"; rm -f $(OUT).hex $(UZE); exit 1; fi

## make uze: uzebros.uze
UZE = $(GAME).uze
uze:
	@$(MAKE) --no-print-directory SD=1 uzefile
uzefile: $(UZE)
$(UZE): $(OUT).elf $(OUT).hex size $(ROOT)/gameinfo.properties mkuze.py
	@python mkuze.py $(OUT).elf $(OUT).hex $@ $(ROOT)/gameinfo.properties
	@python mkuze.py --check $@ $(OUT).elf

## make assets: everything that is the cartridge's, made from your own Super Mario Bros. .nes
## (the one in ../ROM, or NES=<the .nes>), read where it is and never copied, and written to
## ../data: the levels, the sound, the texts and the engine's tables are its program's bytes at
## the addresses ../tools/romdata.map gives; the tiles and the title's board its CHR-ROM
ifneq ($(NES),)
  export SMB_NES := $(if $(filter /% %:%,$(firstword $(NES))),$(NES),$(CURDIR)/$(NES))
endif
assets:
	@mkdir -p $(DATA_DIR)
	cd $(TOOLS_DIR) && python asmdata.py && python gen_levels.py && python gen_snd.py && python gen_text.py && python gen_tables.py && python gen_gfx17.py
ASSETS_NEEDED = $(addprefix $(DATA_DIR)/,levels.h snddata.inc waves.inc textdata.h romtables.h $(GFX17_H) $(GFX17_INC))
ifneq ($(filter-out $(wildcard $(ASSETS_NEEDED)),$(ASSETS_NEEDED)),)
ifeq ($(filter assets clean,$(MAKECMDGOALS)),)
$(error $(notdir $(filter-out $(wildcard $(ASSETS_NEEDED)),$(ASSETS_NEEDED))): not in ../data. They are made from your own Super Mario Bros. ROM, which is not in the repository: put the .nes in ../ROM, then make assets)
endif
endif
$(OBJDIR)/area.o $(OBJDIR)/area.pre.o: $(DATA_DIR)/levels.h
$(OBJDIR)/text.o $(OBJDIR)/text.pre.o: $(DATA_DIR)/textdata.h
$(foreach o,area bgcoll bosses collide enemies game objects player sprites,$(OBJDIR)/$(o).o $(OBJDIR)/$(o).pre.o): $(DATA_DIR)/romtables.h

clean:
	-rm -rf obj $(GAME).hex $(GAME).elf $(GAME).map $(GAME)-sd.hex $(GAME)-sd.elf $(GAME)-sd.map $(GAME).uze

-include $(wildcard $(OBJDIR)/*.d)
