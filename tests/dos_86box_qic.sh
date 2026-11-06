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
# DITTO=1: an Iomega Ditto 2GB on LPT1 instead (86Box's lpt_ditto.c, the
# drive and its 765 behind a BackPack) with the same cartridge; PORTASPI
# finds it without /F.
# FORMAT=qic3010 or qic3020 (with DITTO=1): an Iomega QIC-3020 drive on
# LPT1 with a QIC-3010 or QIC-3020 cartridge in format code 6; the third
# volume starts near the end of track 0, a whole segment is bad.
# Needs 86Box master of 2026-11-05 or later (86Box/86Box#8748: before it
# the emulator crashed when the tape started to stream).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
TAPE=${TAPE:-}
FORMAT=${FORMAT:-qic80}
case $FORMAT in
    qic80) MEDIUM=14 SPT=150 ;;     # 86Box tape_types[]: QIC-80, DC-2120
    qic3010) MEDIUM=17 SPT=215 ;;
    qic3020) MEDIUM=18 SPT=422 ;;
    *) echo "FORMAT: qic80, qic3010 or qic3020" >&2; exit 1 ;;
esac
if [ "$FORMAT" != qic80 ] && [ "${DITTO:-0}" != 1 ]; then
    echo "FORMAT=$FORMAT needs DITTO=1 (fdd_tape.c is a QIC-80 drive)" >&2
    exit 1
fi
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
    if [ "$FORMAT" = qic80 ]; then
        python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img" --bad 4:5 \
            --damage 3:10 --damage 4:30 --damage 20:0 --damage 151:31 \
            "${VOLS[@]}"
    else
        L=$((SPT - 8))
        python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img" \
            --format "$FORMAT" --bad 4:5 --bad 6:'*' --last-at $L \
            --damage 3:10 --damage 4:30 --damage 20:0 \
            --damage $((SPT + 1)):31 "${VOLS[@]}"
    fi
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
if [ "${DITTO:-0}" = 1 ]; then
    sed -i 's/^lpt1_enabled = 0$/lpt1_enabled = 1/' "$OUT/86box.cfg"
    cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = $([ "$FORMAT" = qic80 ] && echo 5 || echo 7), lpt
tape_01_medium_type = $MEDIUM
tape_01_lpt_port = 0
tape_01_image_path = tape.img
CFG
    TAPEOPT=
else
    cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = 4, fdc
tape_01_medium_type = $MEDIUM
tape_01_fdd_unit = ${UNIT:-1}
tape_01_image_path = tape.img
CFG
    TAPEOPT=/F:${UNIT:-1}
fi
printf 'DEVICE=PORTASPI.SYS /V %s %s\r\nSHELL=COMMAND.COM /P\r\n' "$TAPEOPT" \
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
