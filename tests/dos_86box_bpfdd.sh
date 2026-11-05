#!/bin/bash
# BackPack floppy drive (Micro Solutions BPCK bridge with its own 765) on
# LPT1 of 86Box (AUTOMATION=ON), through Micro Solutions' BACKPACK.SYS 2.13
# (ORIG=path, not shipped) against the 86Box model.
# The pod's drive is floppy drive 5, a 1.44 MB image. DOS reads a file
# from it and writes one to it; the host compares both and runs fsck.
# LETTER: the drive letter BACKPACK.SYS gives the pod (default C).
# Uses OpenASPI's boot floppy and 86Box harness (OPENASPI=path).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
ORIG=${ORIG:-}
LETTER=${LETTER:-C}
OUT=${OUT:-$ROOT/build/86box-bpfdd}
test -x "$BOX"
test -d "$ROMDIR"
test -f "$ORIG"
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
sed -e 's/^lpt1_enabled = 0$/lpt1_enabled = 1\nlpt1_device = bpfdd/' \
    -e 's/^fdd_02_type = none$/&\nfdd_05_type = 35_2hd\nfdd_05_fn = pod.img/' \
    -e '/^cdrom_01_/d' \
    "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
grep -q '^lpt1_device = bpfdd$' "$OUT/86box.cfg"
grep -q '^fdd_05_fn = pod.img$' "$OUT/86box.cfg"

# The pod's disk: a 1.44 MB FAT12 floppy with a file to read back.
rm -f "$OUT/pod.img"
mkfs.fat -C -F 12 -n BPFDD "$OUT/pod.img" 1440 > "$OUT/mkfs.log"
head -c 60000 /dev/urandom > "$OUT/POD.BIN"
mcopy -i "$OUT/pod.img" "$OUT/POD.BIN" ::/POD.BIN
head -c 50000 /dev/urandom > "$OUT/HOST.BIN"

DRV=$(basename "$ORIG" | tr a-z A-Z)
printf 'DEVICE=%s\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' "$DRV" > "$OUT/CONFIG.SYS"
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
DIR $LETTER: > AUX
COPY $LETTER:\\POD.BIN A:\\POD.BIN > AUX
IF NOT EXIST A:\\POD.BIN ECHO BPFDD read FAILED > AUX
COPY A:\\HOST.BIN $LETTER:\\HOST.BIN > AUX
IF NOT EXIST $LETTER:\\HOST.BIN ECHO BPFDD write FAILED > AUX
ECHO BPFDD copy passed > AUX
ECHO BPFDD DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" "$OUT/HOST.BIN" ::/
mcopy -o -i "$OUT/boot.img" "$ORIG" ::/"$DRV"
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'BPFDD DONE' --expect 'BPFDD copy passed' --count 1 \
    --read-text '1 file(s) copied' --steps ${STEPS:-300}
mcopy -o -i "$OUT/boot.img" ::/POD.BIN "$OUT/POD.BACK"
cmp "$OUT/POD.BACK" "$OUT/POD.BIN"
mcopy -o -i "$OUT/pod.img" ::/HOST.BIN "$OUT/HOST.BACK"
cmp "$OUT/HOST.BACK" "$OUT/HOST.BIN"
fsck.fat -n "$OUT/pod.img" > "$OUT/fsck.log"
echo "86Box BackPack floppy drive through $DRV: read, write and compare passed"
