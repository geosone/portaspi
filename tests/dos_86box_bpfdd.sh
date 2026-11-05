#!/bin/bash
# BackPack floppy drive (Micro Solutions BPCK bridge with its own 765) on
# LPT1 of 86Box (AUTOMATION=ON), through PORTASPI.SYS and OpenASPI's
# ASPIFDD.SYS, or with ORIG=path through Micro Solutions' BACKPACK.SYS 2.13
# (not shipped). The pod's drive is floppy drive 5, a 1.44 MB image. DOS
# reads a file from it and writes one to it; the host compares both and
# runs fsck.
# SWAP=1 (PORTASPI only): then a second disk goes in, a write protected
# 720 KB one; DOS must see the change and read it, and a write must fail.
# MGROPT: more PORTASPI options, for example /M:PS2.
# LETTER: the drive letter the pod gets (default C).
# Uses OpenASPI's boot floppy and 86Box harness (OPENASPI=path).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
ORIG=${ORIG:-}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
LETTER=${LETTER:-C}
SWAP=${SWAP:-}
OUT=${OUT:-$ROOT/build/86box-bpfdd}
test -x "$BOX"
test -d "$ROMDIR"
if [ -n "$ORIG" ]; then test -f "$ORIG"; else test -f "$MGR"; fi
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
ON=()
if [ -n "$SWAP" ]; then
    test -z "$ORIG"
    rm -f "$OUT/pod2.img"
    mkfs.fat -C -F 12 -n BPFDD2 "$OUT/pod2.img" 720 >> "$OUT/mkfs.log"
    head -c 30000 /dev/urandom > "$OUT/POD2.BIN"
    mcopy -i "$OUT/pod2.img" "$OUT/POD2.BIN" ::/POD2.BIN
    cp "$OUT/pod2.img" "$OUT/pod2.orig"
    chmod 444 "$OUT/pod2.img"           # 86Box takes it read only
    ON=(--on 'BPFDD SWAP' "fdd load 4 $OUT/pod2.img; step 3000; key down 0x1c; key up 0x1c")
    # absolute and DOS writes must fail with DOS error 00h
    "$OPENASPI/build/tools/jwasm/jwasm" -q -bin -0 -W4 -WX \
        -DPROTECT_DRIVE=$(( $(printf '%d' "'$LETTER") - 65 )) \
        -Fo="$OUT/PROTECT.COM" "$OPENASPI/tests/disk_protect.asm"
fi

if [ -n "$ORIG" ]; then
    DRV=$(basename "$ORIG" | tr a-z A-Z)
    printf 'DEVICE=%s\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' "$DRV" > "$OUT/CONFIG.SYS"
    SCAN=REM
else
    DRV=PORTASPI.SYS
    printf 'DEVICE=PORTASPI.SYS /V %s\r\nDEVICE=ASPIFDD.SYS\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$MGROPT" > "$OUT/CONFIG.SYS"
    SCAN=ASPISCAN
fi
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
$SCAN > AUX
DIR $LETTER: > AUX
COPY $LETTER:\\POD.BIN A:\\POD.BIN > AUX
IF NOT EXIST A:\\POD.BIN ECHO BPFDD read FAILED > AUX
COPY A:\\HOST.BIN $LETTER:\\HOST.BIN > AUX
IF NOT EXIST $LETTER:\\HOST.BIN ECHO BPFDD write FAILED > AUX
ECHO BPFDD copy passed > AUX
BAT
if [ -n "$SWAP" ]; then
    cat >> "$OUT/AUTOEXEC.BAT" <<BAT
ECHO BPFDD SWAP > AUX
PAUSE
DIR $LETTER: > AUX
COPY $LETTER:\\POD2.BIN A:\\POD2.BIN > AUX
IF NOT EXIST A:\\POD2.BIN ECHO BPFDD swap read FAILED > AUX
PROTECT > AUX
ECHO BPFDD swap passed > AUX
BAT
fi
echo 'ECHO BPFDD DONE > AUX' >> "$OUT/AUTOEXEC.BAT"
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" "$OUT/HOST.BIN" ::/
if [ -n "$ORIG" ]; then
    mcopy -o -i "$OUT/boot.img" "$ORIG" ::/"$DRV"
else
    mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
    if [ -n "$SWAP" ]; then mcopy -o -i "$OUT/boot.img" "$OUT/PROTECT.COM" ::/; fi
    mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPIFDD.SYS" \
        "$OPENASPI/build/dos/ASPISCAN.COM" ::/
fi
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'BPFDD DONE' --expect 'BPFDD copy passed' --count 1 \
    --read-text '1 file(s) copied' --steps ${STEPS:-300} "${ON[@]}"
mcopy -o -i "$OUT/boot.img" ::/POD.BIN "$OUT/POD.BACK"
cmp "$OUT/POD.BACK" "$OUT/POD.BIN"
mcopy -o -i "$OUT/pod.img" ::/HOST.BIN "$OUT/HOST.BACK"
cmp "$OUT/HOST.BACK" "$OUT/HOST.BIN"
fsck.fat -n "$OUT/pod.img" > "$OUT/fsck.log"
if [ -n "$SWAP" ]; then
    grep -q 'BPFDD swap passed' "$OUT/serial.log"
    grep -q 'Live block write protection passed' "$OUT/serial.log"
    grep -q 'POD2' "$OUT/serial.log"
    mcopy -o -i "$OUT/boot.img" ::/POD2.BIN "$OUT/POD2.BACK"
    cmp "$OUT/POD2.BACK" "$OUT/POD2.BIN"
    cmp "$OUT/pod2.img" "$OUT/pod2.orig"    # nothing written
    echo "86Box BackPack floppy drive: disk change, 720 KB and write protection passed"
fi
echo "86Box BackPack floppy drive through $DRV: read, write and compare passed"
