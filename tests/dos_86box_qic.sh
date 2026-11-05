#!/bin/bash
# Floppy tape (QIC-117) on the floppy controller of 86Box master
# (AUTOMATION=ON): an emulated Colorado 250 on drive select line 1 with a
# QIC-80 cartridge; PORTASPI.SYS /F finds it and ASPISCAN lists it as a
# sequential device.
# Uses OpenASPI's DOS boot image and 86Box harness (OPENASPI=path).
# TAPE=path: the cartridge image, a formatted QIC-80 (DC-2120) tape, for
# example qic80_dc2120.img from https://github.com/dbrant/ftape
# (blanktapes/); default ../ahciaspi/ref/qic/qic80_dc2120.img.
# MGROPT: more PORTASPI options.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
TAPE=${TAPE:-$ROOT/../ahciaspi/ref/qic/qic80_dc2120.img}
OUT=${OUT:-$ROOT/build/86box-qic}
test -x "$BOX"
test -d "$ROMDIR"
test -f "$MGR"
test -f "$TAPE"
mkdir -p "$OUT"
if [ -e "$OUT/control.sock" ]; then
    echo "Refusing to reuse an existing control socket: $OUT/control.sock" >&2
    exit 1
fi
make -C "$OPENASPI" all >/dev/null
if [ ! -f "$OPENASPI/build/dos-cda/boot.img" ]; then
    bash "$OPENASPI/tests/dos_cda.sh" >/dev/null
fi
cp "$OPENASPI/build/dos-cda/boot.img" "$OUT/"
cp "$TAPE" "$OUT/tape.img"
sed -e '/^cdrom_01_/d' -e "s/^fdd_02_type = none$/fdd_02_type = ${FDD2:-none}/" \
    "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = 4, fdc
tape_01_medium_type = 12
tape_01_fdd_unit = ${UNIT:-1}
tape_01_image_path = tape.img
CFG
printf 'DEVICE=PORTASPI.SYS /V /F:%s %s\r\nSHELL=COMMAND.COM /P\r\n' "${UNIT:-1}" \
    "$MGROPT" > "$OUT/CONFIG.SYS"
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
ASPISCAN > AUX
ECHO PORTASPI QIC DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" ::/
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI QIC DONE' \
    --expect 'type=01 TAPE' --count 1 \
    --read-text 'PORTASPI QIC DONE'
echo "86Box floppy tape through PORTASPI: found"
