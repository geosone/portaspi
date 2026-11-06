# PORTASPI - ASPI manager for parallel port devices
NASM    = nasm
NFLAGS  = -f bin -w-reloc-abs-word
OPENASPI ?= ../OpenAspi

all: build/PORTASPI.SYS

build/PORTASPI.SYS: src/portaspi.asm src/lpt.inc src/ppa.inc src/imm.inc src/epat.inc src/bpck.inc src/on26.inc src/epia.inc src/fc.inc src/ata.inc src/fdc.inc src/mcd.inc src/qic.inc src/atapi.inc src/aspi.inc | build
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

# Floppy tape (QIC-117) in 86Box: the Colorado 250 on the floppy
# controller, or with DITTO=1 the Iomega Ditto; WRITE=W|T|F|X|V writes,
# fills or formats first (see tests/dos_86box_qic.sh)
test-qic: build/PORTASPI.SYS
	OPENASPI=$(OPENASPI) bash tests/dos_86box_qic.sh

# OpenTape backs a tree up to the floppy tape, verifies and restores it
test-opentape: build/PORTASPI.SYS
	OPENASPI=$(OPENASPI) bash tests/dos_86box_opentape.sh

# distribution: INSTALL.EXE comes from the GeosOne DOS Installer
# (https://github.com/geosone/gdi), the OpenASPI drivers from
# https://github.com/geosone/OpenAspi (make), SHSUCDX and CDAUDIO from
# https://github.com/geosone/shsucd (install/mkpkg.sh)
GDI ?= ../gdi/build/INSTALL.EXE
OPENASPI_DOS ?= $(OPENASPI)/build/dos
SHSUCD ?= ../shsucd/build/pkg
PKG = build/pkg
package: build/PORTASPI.SYS
	rm -rf $(PKG) && mkdir -p $(PKG)
	cp build/PORTASPI.SYS $(PKG)/
	cd $(OPENASPI_DOS) && cp ASPICDA.SYS ASPIFDD.SYS ASPIDSK.SYS ASPITAP.SYS ASPISCAN.COM TAPECTL.COM $(abspath $(PKG))/
	cd $(SHSUCD) && cp SHSUCDX.COM CDAUDIO.COM SHSUCD.LIC SHSUCD.TXT $(abspath $(PKG))/
	sed 's/$$/\r/' install/INSTALL.INI > $(PKG)/INSTALL.INI
	sed 's/$$/\r/' install/README.TXT > $(PKG)/README.TXT
	sed 's/$$/\r/' LICENSE > $(PKG)/LICENSE.TXT
	cp $(GDI) $(PKG)/INSTALL.EXE
	sed 's/$$/\r/' $(dir $(GDI))../LICENSE > $(PKG)/GPL3.TXT
	cd $(PKG) && rm -f ../portaspi.zip && zip -q -9 ../portaspi.zip *
	# a 1.44 MB disk (mtools)
	cd $(PKG) && rm -f ../PORTASPI.IMG && mformat -C -f 1440 -v PORTASPI -i ../PORTASPI.IMG :: && mcopy -i ../PORTASPI.IMG * ::

# the setup package in QEMU: INSTALL /S and INSTALL /U (tests/install.sh)
test-install: package
	bash tests/install.sh

clean:
	rm -rf build

.PHONY: all test test-ls120 test-bpck test-on26 test-qic test-opentape package test-install clean
