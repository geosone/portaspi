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
# FORMAT=ditto2gb (with DITTO=1): the Ditto 2GB with its own cartridge
# (format code 4, the scrambled header), laid out the same way.
# FORMAT=ditto5gb or dittomax7 (with DITTO=1): the Ditto Max with a
# cartridge of over 65535 segments, the third volume past segment 65535,
# again across the end of a track.
# WRITE=W: QICREAD first writes two volumes at the end of data (70000
# bytes in blocks of 10000, 300000 in blocks of 32768, from a generator
# both sides know), WRITE=T one after the first volume, the cartridge
# ending there; then reads all back, and the host checks the image: the
# volume table, sizes, CRCs and the parity of what was written.  WRITE=F
# (QIC-80 only): the header says the cartridge ends at segment 170, and
# QICREAD fills it with one volume past the third: VOLUME OVERFLOW, EOM
# and the residue, at the filemark and once more.
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
    qic80) MEDIUM=12 SPT=150 ;;     # 86Box tape_types[] (past its #if 0)
    qic3010) MEDIUM=15 SPT=215 ;;
    qic3020) MEDIUM=16 SPT=422 ;;
    ditto2gb) MEDIUM=7 SPT=502 ;;
    ditto5gb) MEDIUM=9 SPT=1256 ;;
    dittomax7) MEDIUM=10 SPT=1758 ;;
    *) echo "FORMAT: qic80, qic3010, qic3020, ditto2gb, ditto5gb or" \
            "dittomax7" >&2; exit 1 ;;
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
        END=()
        [ "${WRITE:-}" != F ] || END=(--end 170)
        python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img" "${END[@]}" --bad 4:5 \
            --damage 3:10 --damage 4:30 --damage 20:0 --damage 151:31 \
            "${VOLS[@]}"
    else
        L=$((SPT - 8))
        case $FORMAT in ditto5gb|dittomax7) L=$(((65536 / SPT + 2) * SPT - 8)) ;; esac
        python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img" \
            --format "$FORMAT" --bad 4:5 --bad 6:'*' --last-at $L \
            --damage 3:10 --damage 4:30 --damage 20:0 \
            --damage $((L + 9)):31 "${VOLS[@]}"
    fi
fi
WRITE=${WRITE:-}
case $WRITE in ''|W|T|F) ;; *) echo "WRITE: W, T or F" >&2; exit 1 ;; esac
if [ "$WRITE" = F ] && [ "$FORMAT" != qic80 ]; then
    echo "WRITE=F needs FORMAT=qic80" >&2
    exit 1
fi
python3 -I - "$OUT" "$WRITE" "${VOLS[@]}" > "$OUT/expect.txt" <<'PY'
import sys, zlib
out, write, names = sys.argv[1], sys.argv[2], sys.argv[3:]
vols = [open(n, "rb").read() for n in names]
def gen(seed, size):
    b = bytearray(size)
    for i in range(size):
        seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
        b[i] = (seed >> 16) & 0xFF
    return bytes(b)
first = len(vols)
if write == "T":
    vols, first = vols[:1] + [gen(0x1234, 70000)], 1
elif write == "W":
    vols += [gen(0x1234, 70000), gen(0x1235, 300000)]
elif write == "F":                      # segments 163 to 170, none bad
    full = 8 * 29 * 1024
    vols += [gen(0x1234, full)]
    print("FULL %d\nFULL 0" % full)
for i, d in enumerate(vols):
    print("VOL %d %d %08X" % (i, len(d), zlib.crc32(d)))
print("EOD %d" % len(vols))
if vols:
    print("BACK ok")
if len(vols) > 1:
    print("SPACED %d %08X" % (len(vols[1]), zlib.crc32(vols[1])))
with open(out + "/expect-img.txt", "w") as f:
    for i, d in enumerate(vols):
        if i < first:
            f.write("TBL %d %d\n" % (i, len(d)))
        else:
            f.write("IMG %d %d %08X\nPARITY %d 0\n" % (i, len(d),
                                                       zlib.crc32(d), i))
print(first, file=open(out + "/first.txt", "w"))
PY
sed -e '/^cdrom_01_/d' -e "s/^fdd_02_type = none$/fdd_02_type = ${FDD2:-none}/" \
    "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
if [ "${DITTO:-0}" = 1 ]; then
    sed -i 's/^lpt1_enabled = 0$/lpt1_enabled = 1/' "$OUT/86box.cfg"
    cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = $(case $FORMAT in qic80|ditto2gb) echo 5 ;; ditto5gb|dittomax7) echo 6 ;; *) echo 7 ;; esac), lpt
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
QICREAD $WRITE > AUX
ECHO PORTASPI QIC DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" "$OUT/QICREAD.COM" ::/
PORTASPI=$ROOT
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI QIC DONE' \
    --expect 'type=01 TAPE' --count 1 \
    --read-text 'QICREAD DONE' --steps ${STEPS:-300}
tr -d '\r' < "$OUT/serial.log" | grep -E '^(FULL|VOL|EOD|BACK|SPACED) ' > "$OUT/got.txt" || true
diff -u "$OUT/expect.txt" "$OUT/got.txt"
if [ -n "$WRITE" ]; then
    python3 -I "$PORTASPI/tests/qictape.py" "$OUT/tape.img" \
        --check "$(cat "$OUT/first.txt")" > "$OUT/got-img.txt"
    diff -u "$OUT/expect-img.txt" "$OUT/got-img.txt"
    echo "86Box floppy tape through PORTASPI: written ($WRITE), the image checked"
fi
echo "86Box floppy tape through PORTASPI: $(grep -c '^VOL' "$OUT/got.txt") volumes read and compared, filemarks spaced"
