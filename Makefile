# NAVAL HUNTER (GBDK-2020)
GBDK_HOME ?= /opt/gbdk
LCC        = $(GBDK_HOME)/bin/lcc

# -Wm-yn sets the ROM header title. "Battleship" is Hasbro's trademark, so the
# header carries the game's own name instead; the header field is 11-15 chars and
# NAVAL HUNTER is 12, so nothing is truncated.
# DO NOT add -Wm-yc (CGB-enhanced) unless you also load CGB palettes:
# setting the CGB flag makes the emulator use the CGB palette registers, and
# GBDK's CRT does not initialise them -> the screen renders uniformly white.
CFLAGS = -Wm-yn"NAVAL HUNTER"

# -zap suffix: this repo's builds, distinct from Coder2's navalhunter.gb
TARGET = navalhunter-zap.gb
SRC    = main.c

all: $(TARGET)

$(TARGET): $(SRC) gfx.h
	$(LCC) $(CFLAGS) -o $(TARGET) $(SRC)

# symbols for emulator debuggers
sym: $(SRC) gfx.h
	$(LCC) $(CFLAGS) -debug -o $(TARGET) $(SRC)

# gfx.h is generated OUTSIDE this image (the slim image has no python3);
# run `python3 mkgfx.py` on the host, then sync gfx.h in.
gfx:
	@echo "gfx.h is generated host-side: python3 mkgfx.py && scp gfx.h ..."

# Fleet-placement RULES, unit-tested on the host with plain gcc.
# place.h has no GB dependencies, so this needs no emulator and runs in ~1 s.
# Only the RENDERING of placement needs the emulator; the rules do not.
test:
	gcc -O1 -Wall -Wextra -I. -o $(TMPDIR)/test_place tests/test_place.c
	$(TMPDIR)/test_place

usage: $(TARGET)
	@$(GBDK_HOME)/bin/romusage $(TARGET) -g

clean:
	rm -f *.gb *.o *.lst *.map *.sym *.ihx *.asm *.cdb *.noi *.rel

.PHONY: all clean sym usage test
