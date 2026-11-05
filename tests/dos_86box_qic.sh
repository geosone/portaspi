#!/bin/bash
# Floppy tape (QIC-117) on the floppy controller of 86Box master
# (AUTOMATION=ON): an emulated Colorado 250 on drive select line 1 with a
# QIC-80 cartridge; PORTASPI.SYS /F finds it, ASPISCAN lists it as a
# sequential device and QICREAD reads every volume of the cartridge as a
# tape file (size and CRC-32), spaces over filemarks and reads again.
# The cartridge comes from tests/qictape.py: three volumes, the first
# across a bad sector, the third across the end of track 0 into track 1.
# Uses OpenASPI's DOS boot image and 86Box harness (OPENASPI=path).
# TAPE=path: another cartridge image instead, for example the formatted
# blank qic80_dc2120.img of https://github.com/dbrant/ftape (blanktapes/):
# no volumes.
# UNIT: the drive select line, default 1.  MGROPT: more PORTASPI options.
# 86Box master since the floppy refactor of 2026-09-26 crashes when the
# tape starts to stream (its command and motion timers get no drive); the
# fix is the fdd-tape-timer-priv branch of github.com/geosone/86Box.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
TAPE=${TAPE:-}
OUT=${OUT:-$ROOT/build/86box-qic}
test -x "$BOX"
test -d "$ROMDIR"
test -f "$MGR"
[ -z "$TAPE" ] || test -f "$TAPE"
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
nasm -f bin -o "$OUT/QICREAD.COM" "$ROOT/tests/qicread.asm"
rm -f "$OUT/tape.img" "$OUT/expect.txt"
if [ -n "$TAPE" ]; then
    cp --sparse=always "$TAPE" "$OUT/tape.img"
    VOLS=()
else
    head -c 100000 /dev/urandom > "$OUT/vol0.bin"
    head -c 5000 /dev/urandom > "$OUT/vol1.bin"
    head -c 4600000 /dev/urandom > "$OUT/vol2.bin"
    VOLS=("$OUT/vol0.bin" "$OUT/vol1.bin" "$OUT/vol2.bin")
    python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img" --bad 4:5 "${VOLS[@]}"
fi
python3 -I - "${VOLS[@]}" > "$OUT/expect.txt" <<'PY'
import sys, zlib
names = sys.argv[1:]
for i, n in enumerate(names):
    d = open(n, "rb").read()
    print("VOL %d %d %08X" % (i, len(d), zlib.crc32(d)))
print("EOD %d" % len(names))
if names:
    print("BACK ok")
if len(names) > 1:
    d = open(names[1], "rb").read()
    print("SPACED %d %08X" % (len(d), zlib.crc32(d)))
PY
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
QICREAD > AUX
ECHO PORTASPI QIC DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" "$OUT/QICREAD.COM" ::/
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI QIC DONE' \
    --expect 'type=01 TAPE' --count 1 \
    --read-text 'QICREAD DONE' --steps ${STEPS:-300}
tr -d '\r' < "$OUT/serial.log" | grep -E '^(VOL|EOD|BACK|SPACED) ' > "$OUT/got.txt" || true
diff -u "$OUT/expect.txt" "$OUT/got.txt"
echo "86Box floppy tape through PORTASPI: $(grep -c '^VOL' "$OUT/got.txt") volumes read and compared, filemarks spaced"
