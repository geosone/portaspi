#!/bin/bash
# BackPack CD-ROM (Micro Solutions BPCK bridge) on LPT1 of 86Box master
# (AUTOMATION=ON): PORTASPI.SYS, then OpenASPI's ASPICDA.SYS and SHSUCDX.
# DOS reads the CD's README and copies a 100000 byte file from it to the
# floppy; the copy is compared with the original on the host.
# Uses OpenASPI's CD test image and 86Box harness (OPENASPI=path).
# MGROPT: more PORTASPI options, for example /M:PS2.
# ORIG=path: Micro Solutions' own BPCDDRV.SYS (not shipped) instead of
# PORTASPI and ASPICDA, run as a black box against the 86Box model;
# BPOPT: its options, for example MODE=0.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
ORIG=${ORIG:-}
BPOPT=${BPOPT:-}
OUT=${OUT:-$ROOT/build/86box-bpck}
test -x "$BOX"
test -d "$ROMDIR"
if [ -n "$ORIG" ]; then test -f "$ORIG"; else test -f "$MGR"; fi
mkdir -p "$OUT"
if [ -e "$OUT/control.sock" ]; then
    echo "Refusing to reuse an existing control socket: $OUT/control.sock" >&2
    exit 1
fi
make -C "$OPENASPI" all >/dev/null
if [ ! -f "$OPENASPI/build/dos-cda/cd.iso" ]; then
    bash "$OPENASPI/tests/dos_cda.sh" >/dev/null
fi
cp "$OPENASPI/build/dos-cda/boot.img" "$OPENASPI/build/dos-cda/cd.iso" "$OUT/"
sed -e 's/^lpt1_enabled = 0$/lpt1_enabled = 1/' \
    -e 's/^cdrom_01_parameters = 1, atapi$/cdrom_01_parameters = 1, lpt/' \
    -e 's/^cdrom_01_ide_channel = 1:0$/cdrom_01_lpt_port = 0/' \
    -e 's/^cdrom_01_image_path = cd.cue$/cdrom_01_image_path = cd.iso/' \
    "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
grep -q '^cdrom_01_parameters = 1, lpt$' "$OUT/86box.cfg"
CDDEV=ASPICDA0
if [ -n "$ORIG" ]; then
    CDDEV='BPCDDRV$'
    printf 'DEVICE=BPCDDRV.SYS %s\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$BPOPT" > "$OUT/CONFIG.SYS"
else
    printf 'DEVICE=PORTASPI.SYS /V %s\r\nDEVICE=ASPICDA.SYS /D:ASPICDA0 /V\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$MGROPT" > "$OUT/CONFIG.SYS"
fi
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
ASPISCAN > AUX
SHSUCDX /D:$CDDEV,R > AUX
TYPE R:\README.TXT > AUX
COPY R:\SUB\BIG.BIN A:\CDBIG.BIN > AUX
IF NOT EXIST A:\CDBIG.BIN ECHO PORTASPI BPCK copy FAILED > AUX
ECHO PORTASPI BPCK copy passed > AUX
ECHO PORTASPI BPCK DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
if [ -n "$ORIG" ]; then
    mcopy -o -i "$OUT/boot.img" "$ORIG" ::/BPCDDRV.SYS
else
    mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
fi
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" ::/
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI BPCK DONE' \
    --expect 'PORTASPI BPCK copy passed' --count 1 \
    --read-text 'OpenASPI ASPICDA read test'
mcopy -o -i "$OUT/boot.img" ::/CDBIG.BIN "$OUT/CDBIG.BIN"
cmp "$OUT/CDBIG.BIN" "$OPENASPI/build/dos-cda/cdroot/SUB/BIG.BIN"
echo "86Box BackPack CD-ROM through PORTASPI: read and compare passed"
