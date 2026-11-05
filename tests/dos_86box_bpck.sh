#!/bin/bash
# BackPack CD-ROM (Micro Solutions BPCK bridge) on LPT1 of 86Box master
# (AUTOMATION=ON): PORTASPI.SYS, then OpenASPI's ASPICDA.SYS and SHSUCDX.
# DOS reads the CD's README and copies a 100000 byte file from it to the
# floppy; the copy is compared with the original on the host.
# Uses OpenASPI's CD test image and 86Box harness (OPENASPI=path).
# MGROPT: more PORTASPI options, for example /M:PS2.
# ORIG=path: Micro Solutions' own BPCDDRV.SYS (not shipped) instead of
# PORTASPI and ASPICDA, run as a black box against the 86Box model;
# BPOPT: its options, for example MODE=0. ORIGDEV: the device name the
# driver gives SHSUCDX (BPCDDRV$, or what BPOPT names with /D:).
# BRIDGE: the bridge of the drive on LPT1, bpck (the BackPack), on26,
# epia, frpw (Freecom Power) or friq (Freecom IQ); ORIG= Freecom's
# POWER-CD.SYS (IQ) or its older G735.SYS (Power) with BPOPT=/D:FCCD001
# and ORIGDEV=FCCD001 for those, OnSpec's CDBLAZER.SYS (CD-Blazer) with
# BRIDGE=on26 BPOPT=/D:ONIDE001 ORIGDEV=ONIDE001 for on26.
# MSCDEX=path: Microsoft's MSCDEX.EXE (not shipped) as the redirector
# instead of SHSUCDX. BPCDDRV.SYS 2.13 needs it: with SHSUCDX it reads
# sector 80000058h for 58h and fails.
# BRIDGE=bpmcd: the first BackPack CD-ROMs (160550, 162550), a Mitsumi
# FX001 drive behind the BackPack (86Box's lpt_bpmcd), through PORTASPI or
# with ORIG= Micro Solutions' BPCDDRV.SYS 1.06 and MSCDEX=.
# BPUNIT: the BackPack's chain address (unit), default 7.
# SWAP=1: then a second CD goes in; the redirector must see the change and
# read a file only that CD has.
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
ORIGDEV=${ORIGDEV:-BPCDDRV\$}
MSCDEX=${MSCDEX:-}
BRIDGE=${BRIDGE:-bpck}
BPUNIT=${BPUNIT:-}
SWAP=${SWAP:-}
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
ON=()
if [ -n "$SWAP" ]; then
    rm -rf "$OUT/cd2root" "$OUT/cd2.iso"
    mkdir -p "$OUT/cd2root"
    head -c 70000 /dev/urandom > "$OUT/cd2root/SWAP.BIN"
    xorriso -outdev "$OUT/cd2.iso" -volid SECOND \
        -map "$OUT/cd2root" / -padding 1m -end 2>/dev/null
    ON=(--on 'BPCK SWAP' "cd load 0 $OUT/cd2.iso; step 3000; key down 0x1c; key up 0x1c")
fi
sed -e 's/^lpt1_enabled = 0$/lpt1_enabled = 1/' \
    -e 's/^cdrom_01_parameters = 1, atapi$/cdrom_01_parameters = 1, lpt/' \
    -e 's/^cdrom_01_ide_channel = 1:0$/cdrom_01_lpt_port = 0/' \
    -e 's/^cdrom_01_image_path = cd.cue$/cdrom_01_image_path = cd.iso/' \
    "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
grep -q '^cdrom_01_parameters = 1, lpt$' "$OUT/86box.cfg"
if [ "$BRIDGE" = bpmcd ]; then
    sed -i -e 's/^cdrom_01_parameters = 1, lpt$/cdrom_01_parameters = 1, mitsumi/' \
        -e '/^cdrom_01_lpt_port = 0$/d' \
        -e 's/^lpt1_enabled = 1$/&\nlpt1_device = bpmcd/' "$OUT/86box.cfg"
    grep -q '^lpt1_device = bpmcd$' "$OUT/86box.cfg"
elif [ "$BRIDGE" != bpck ]; then
    sed -i "s/^cdrom_01_lpt_port = 0$/&\ncdrom_01_lpt_bridge = $BRIDGE/" "$OUT/86box.cfg"
    grep -q "^cdrom_01_lpt_bridge = $BRIDGE$" "$OUT/86box.cfg"
fi
if [ -n "$BPUNIT" ]; then
    SECTION="Micro Solutions BackPack Bantam CD-ROM #1"
    [ "$BRIDGE" != bpmcd ] || SECTION="Micro Solutions BackPack CD-ROM (Mitsumi, 160550/162550) #1"
    printf '\n[%s]\nunit = %s\n' "$SECTION" "$BPUNIT" >> "$OUT/86box.cfg"
fi
case "$BRIDGE" in
    on26) NAME="OnSpec 90c26" ;;
    epia) NAME="Shuttle EPIA" ;;
    frpw) NAME="Freecom Power" ;;
    friq) NAME="Freecom IQ" ;;
    bpmcd) NAME="BackPack (Mitsumi)" ;;
    *) NAME="BackPack" ;;
esac
DRV=PORTASPI
CDDEV=ASPICDA0
if [ -n "$ORIG" ]; then
    CDDEV=$ORIGDEV
    DRV=$(basename "$ORIG" | tr a-z A-Z)
    printf 'DEVICE=%s %s\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$DRV" "$BPOPT" > "$OUT/CONFIG.SYS"
else
    printf 'DEVICE=PORTASPI.SYS /V %s\r\nDEVICE=ASPICDA.SYS /D:ASPICDA0 /V\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$MGROPT" > "$OUT/CONFIG.SYS"
fi
REDIR="SHSUCDX /D:$CDDEV,R > AUX"
[ -z "$MSCDEX" ] || REDIR="MSCDEX /D:$CDDEV /L:R > AUX"
cat > "$OUT/AUTOEXEC.BAT" <<BAT
@ECHO OFF
ASPISCAN > AUX
$REDIR
TYPE R:\README.TXT > AUX
COPY R:\SUB\BIG.BIN A:\CDBIG.BIN > AUX
IF NOT EXIST A:\CDBIG.BIN ECHO PORTASPI BPCK copy FAILED > AUX
ECHO PORTASPI BPCK copy passed > AUX
BAT
if [ -n "$SWAP" ]; then
    cat >> "$OUT/AUTOEXEC.BAT" <<BAT
ECHO BPCK SWAP > AUX
PAUSE
DIR R: > AUX
COPY R:\SWAP.BIN A:\SWAP.BIN > AUX
IF NOT EXIST A:\SWAP.BIN ECHO PORTASPI BPCK swap FAILED > AUX
ECHO PORTASPI BPCK swap passed > AUX
BAT
fi
echo 'ECHO PORTASPI BPCK DONE > AUX' >> "$OUT/AUTOEXEC.BAT"
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
if [ -n "$ORIG" ]; then
    mcopy -o -i "$OUT/boot.img" "$ORIG" ::/"$DRV"
else
    mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
fi
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" ::/
[ -z "$MSCDEX" ] || mcopy -o -i "$OUT/boot.img" "$MSCDEX" ::/MSCDEX.EXE
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI BPCK DONE' \
    --expect 'PORTASPI BPCK copy passed' --count 1 \
    --read-text 'OpenASPI ASPICDA read test' "${ON[@]}"
mcopy -o -i "$OUT/boot.img" ::/CDBIG.BIN "$OUT/CDBIG.BIN"
cmp "$OUT/CDBIG.BIN" "$OPENASPI/build/dos-cda/cdroot/SUB/BIG.BIN"
if [ -n "$SWAP" ]; then
    grep -q 'PORTASPI BPCK swap passed' "$OUT/serial.log"
    if grep -q 'swap FAILED' "$OUT/serial.log"; then exit 1; fi
    mcopy -o -i "$OUT/boot.img" ::/SWAP.BIN "$OUT/SWAP.BACK"
    cmp "$OUT/SWAP.BACK" "$OUT/cd2root/SWAP.BIN"
    echo "86Box $NAME CD-ROM through $DRV: disc change passed"
fi
echo "86Box $NAME CD-ROM through $DRV: read and compare passed"
