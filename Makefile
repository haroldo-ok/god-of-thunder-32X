# God of Thunder 32X - build
#
#   make            build the ROM (build/got32x.32x)
#   make assets     regenerate the embedded resource archive
#   make clean

TARGET   := got32x
BUILD    := obj
OUTDIR   := rom
SRC      := src
SRCMD    := src-md

GOTRES   ?= /home/user/dl/gotdata/GOTRES.DAT

SHPREFIX := sh-elf-
CC       := $(SHPREFIX)gcc
AS       := $(SHPREFIX)as
LD       := $(SHPREFIX)gcc
OBJC     := $(SHPREFIX)objcopy
NM       := $(SHPREFIX)nm

MDAS     := m68k-linux-gnu-as
MDLD     := m68k-linux-gnu-ld
MDOBJC   := m68k-linux-gnu-objcopy

# Optimisation flags follow d32xr's release profile. -O2 rather than -Os:
# the inner blit loops benefit from unrolling more than the ROM benefits
# from being a few KB smaller, and we have 2 MB of cartridge to spare.
CCFLAGS  := -c -std=c11 $(EXTRA_CFLAGS) -m2 -mb -Wall -Wextra -Wno-unused-parameter \
            -ffreestanding -fomit-frame-pointer -fno-builtin \
            -ffunction-sections -fdata-sections -fno-common \
            -O2 -funroll-loops \
            -fno-align-loops -fno-align-jumps -fno-align-labels \
            -I$(SRC)
ASFLAGS  := --big -I$(SRCMD)
LDFLAGS  := -T $(SRC)/mars.ld -Wl,-Map=$(BUILD)/$(TARGET).map -nostdlib \
            -Wl,--gc-sections -m2 -mb
# libgcc supplies the SH2 integer division / shift helpers.
LIBS     := -lgcc

COBJS := \
	$(BUILD)/main.o \
	$(BUILD)/game.o \
	$(BUILD)/level.o \
	$(BUILD)/actors.o \
	$(BUILD)/gfx.o \
	$(BUILD)/panel.o \
	$(BUILD)/input.o \
	$(BUILD)/sound.o \
	$(BUILD)/res.o \
	$(BUILD)/object.o \
	$(BUILD)/sptile.o \
	$(BUILD)/move.o \
	$(BUILD)/script.o \
	$(BUILD)/mars_hw.o \
	$(BUILD)/util.o

OBJS := $(BUILD)/crt0.o $(COBJS) $(BUILD)/assets.o

.PHONY: all clean assets dirs

all: $(OUTDIR)/$(TARGET).32x

dirs:
	@mkdir -p $(BUILD) $(OUTDIR)

# ---- embedded resource archive ----------------------------------------
assets: $(BUILD)/gotres.bin

$(BUILD)/gotres.bin: tools/mkassets.py | dirs
	python3 tools/mkassets.py --res $(GOTRES) \
		--out $@ --header $(SRC)/got_assets.h --manifest $(BUILD)/manifest.csv

$(BUILD)/assets.s: $(BUILD)/gotres.bin
	@printf '\t.section .rodata\n\t.balign 4\n\t.global _gotres_start\n_gotres_start:\n\t.incbin "%s"\n\t.balign 4\n\t.global _gotres_end\n_gotres_end:\n' "$(abspath $(BUILD)/gotres.bin)" > $@

$(BUILD)/assets.o: $(BUILD)/assets.s
	$(AS) $(ASFLAGS) $< -o $@

# ---- 68000 side --------------------------------------------------------
$(SRCMD)/m68k.bin: $(SRCMD)/md_main.s $(SRCMD)/md.ld
	$(MDAS) -m68000 --register-prefix-optional $(SRCMD)/md_main.s -o $(SRCMD)/md_main.o
	$(MDLD) -T $(SRCMD)/md.ld $(SRCMD)/md_main.o -o $(SRCMD)/md.elf
	$(MDOBJC) -O binary $(SRCMD)/md.elf $@

# ---- SH2 side ----------------------------------------------------------
$(BUILD)/crt0.o: $(SRC)/crt0.s $(SRCMD)/m68k.bin | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: $(SRC)/%.c $(SRC)/got.h $(SRC)/mars.h | dirs
	$(CC) $(CCFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(SRC)/mars.ld
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(OUTDIR)/$(TARGET).32x: $(BUILD)/$(TARGET).elf | dirs
	$(OBJC) -O binary $< $(BUILD)/rom_raw.bin
	python3 tools/mkrom.py $(BUILD)/rom_raw.bin $@
	@echo "ROM: $@ ($$(stat -c %s $@) bytes)"

clean:
	rm -f $(OUTDIR)/*.32x $(BUILD)/*.o $(BUILD)/*.elf $(BUILD)/*.bin $(BUILD)/*.32x \
	      $(BUILD)/*.map $(BUILD)/assets.s \
	      $(SRCMD)/*.o $(SRCMD)/*.elf $(SRCMD)/m68k.bin

# ---- tests -------------------------------------------------------------
CORE ?= /home/user/pico/picodrive_libretro.so

tests/runner: tests/runner.c
	cc -O2 -std=gnu11 -o $@ $< -ldl

.PHONY: test
test: $(OUTDIR)/$(TARGET).32x tests/runner
	python3 tests/run_tests.py --rom $(OUTDIR)/$(TARGET).32x --core $(CORE) -v
