# PORTASPI - ASPI manager for parallel port devices
NASM    = nasm
NFLAGS  = -f bin -w-reloc-abs-word
OPENASPI ?= ../OpenAspi

all: build/PORTASPI.SYS

build/PORTASPI.SYS: src/portaspi.asm src/ppa.inc src/aspi.inc | build
	$(NASM) $(NFLAGS) -Isrc/ -l build/portaspi.lst -o $@ $<

build:
	mkdir -p build

# ZIP 100 parallel in 86Box (master, AUTOMATION=ON), with OpenASPI's
# ASPIFDD and its 86Box test harness; MODE=NIBBLE|PS2|EPP
test: build/PORTASPI.SYS
	MGR=$(CURDIR)/build/PORTASPI.SYS MGROPT="$(if $(MODE),/M:$(MODE))" \
	    bash $(OPENASPI)/tests/dos_86box_zip.sh

clean:
	rm -rf build

.PHONY: all test clean
