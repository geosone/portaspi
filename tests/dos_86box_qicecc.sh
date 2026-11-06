#!/bin/bash
# PORTASPI's floppy tape Reed-Solomon code (src/qicecc.inc) on its own:
# QICECC.COM (tests/qicecc.asm) puts right the segments tests/qictape.py
# --ecc-vectors writes - erasures (sectors with CRC errors, which 86Box's
# tape drives never have) up to three, a damaged sector, and damage beyond
# what the parity can mend, which must be found - in DOS under 86Box, and
# the host compares. Uses OpenASPI's DOS boot image and 86Box harness
# (OPENASPI=path).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
OUT=${OUT:-$ROOT/build/86box-qicecc}
test -x "$BOX"
test -d "$ROMDIR"
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
nasm -f bin -I"$ROOT/src/" -o "$OUT/QICECC.COM" "$ROOT/tests/qicecc.asm"
python3 -I "$ROOT/tests/qictape.py" --ecc-vectors "$OUT/VEC.BIN" \
    "$OUT/expect.txt"
sed -e '/^cdrom_01_/d' "$OPENASPI/tests/fixtures/86box-upc.cfg" \
    > "$OUT/86box.cfg"
printf 'SHELL=COMMAND.COM /P\r\n' > "$OUT/CONFIG.SYS"
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
ECHO QICECC START > AUX
QICECC > AUX
ECHO PORTASPI QICECC DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
mcopy -o -i "$OUT/boot.img" "$OUT/QICECC.COM" "$OUT/VEC.BIN" ::/
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI QICECC DONE' \
    --expect 'QICECC START' --count 1 \
    --read-text 'QICECC DONE' --steps ${STEPS:-200}
tr -d '\r' < "$OUT/serial.log" | grep -E '^(ECC |QICECC DONE)' > "$OUT/got.txt" || true
python3 -I - "$OUT/expect.txt" "$OUT/got.txt" <<'PY'
import sys
exp = open(sys.argv[1]).read().split("\n")
got = open(sys.argv[2]).read().split("\n")
bad = 0
for e, g in zip(exp, got):
    # a segment found damaged beyond repair: its CRC does not matter
    if e != g and not (e.endswith(" 1") and g.startswith(e + " ")):
        print("expected %r, got %r" % (e, g))
        bad += 1
if len(exp) != len(got):
    print("expected %d lines, got %d" % (len(exp), len(got)))
    bad += 1
sys.exit(1 if bad else 0)
PY
echo "PORTASPI tape ECC: $(grep -c '^ECC' "$OUT/got.txt") segments checked"
