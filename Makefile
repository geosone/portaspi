# PORTASPI - ASPI manager for parallel port devices
NASM    = nasm
NFLAGS  = -f bin -w-reloc-abs-word
OPENASPI ?= ../OpenAspi

all: build/PORTASPI.SYS

build/PORTASPI.SYS: src/portaspi.asm src/lpt.inc src/ppa.inc src/imm.inc src/epat.inc src/bpck.inc src/on26.inc src/ata.inc src/atapi.inc src/aspi.inc | build
	$(NASM) $(NFLAGS) -Isrc/ -l build/portaspi.lst -o $@ $<

build:
	mkdir -p build

# ZIP 100 parallel in 86Box (master, AUTOMATION=ON), with OpenASPI's
# ASPIFDD and its 86Box test harness; MODE=NIBBLE|PS2|EPP
test: build/PORTASPI.SYS
	MGR=$(CURDIR)/build/PORTASPI.SYS MGROPT="$(if $(MODE),/M:$(MODE))" \
	    bash $(OPENASPI)/tests/dos_86box_zip.sh

# SuperDisk LS-120 parallel (Shuttle EPAT) in 86Box, the same way
test-ls120: build/PORTASPI.SYS
	LPT=1 MGR=$(CURDIR)/build/PORTASPI.SYS MGROPT="$(if $(MODE),/M:$(MODE))" \
	    bash $(OPENASPI)/tests/dos_86box_ls120.sh

# BackPack CD-ROM (BPCK) in 86Box: ASPICDA and SHSUCDX read the CD
test-bpck: build/PORTASPI.SYS
	OPENASPI=$(OPENASPI) MGROPT="$(if $(MODE),/M:$(MODE))" bash tests/dos_86box_bpck.sh

# ATA hard disk behind an OnSpec 90c26 in 86Box (lpt-on26 branch): ASPIDSK
test-on26: build/PORTASPI.SYS
	OPENASPI=$(OPENASPI) MGROPT="$(if $(MODE),/M:$(MODE))" bash tests/dos_86box_on26.sh

clean:
	rm -rf build

.PHONY: all test test-ls120 test-bpck test-on26 clean
