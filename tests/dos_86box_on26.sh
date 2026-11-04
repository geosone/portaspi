#!/bin/bash
# ATA hard disk behind an OnSpec 90c26 on LPT1 of 86Box (lpt_on26.c of the
# lpt-on26 branch; AUTOMATION=ON): an ordinary 86Box hard disk on bus LPT,
# port LPT1, which brings the bridge with it. 86Box gives it LBA from 1024
# cylinders on: the default 20 MB disk is addressed in CHS, MB=1000 in LBA. PORTASPI.SYS
# translates SCSI to ATA, OpenASPI's ASPIDSK.SYS gives the FAT16 partition a
# drive letter. DOS reads a file, copies a 100000 byte file to the floppy and
# writes it back twice (plain and with VERIFY ON); the host compares the
# copies and checks the file system. Writes only go to this image.
# MGROPT: more PORTASPI options, for example /M:PS2. MB: the disk size.
# ORIG=path: a vendor block driver (for example ValuStor's VSMOBILE.SYS, not
# shipped) instead of PORTASPI and ASPIDSK, run as a black box against the
# 86Box model; ORIGOPT: its options. When ORIG is an ASPI manager (Shuttle's
# EPATHD.SYS), ORIGDSK names the block driver after it: ASPIDSK for
# OpenASPI's, or a path (Shuttle's ASPIHDRM.SYS); ORIGDSKOPT: its options.
# BRIDGE: the bridge of the disk on LPT1, on26, epat (Shuttle EPAT), epia
# (Shuttle EPIA) or bpck (Micro Solutions BackPack; ORIG= its BPHDDRV.SYS).
# The FAT16 file system is laid out as DOS FORMAT does (1 reserved sector,
# 63 hidden, 16 heads of 63 sectors): Shuttle's ASPIHDRM.SYS counts on it.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
MGROPT=${MGROPT:-}
ORIG=${ORIG:-}
ORIGOPT=${ORIGOPT:-}
ORIGDSK=${ORIGDSK:-}
ORIGDSKOPT=${ORIGDSKOPT:-}
MB=${MB:-20}
BRIDGE=${BRIDGE:-on26}
OUT=${OUT:-$ROOT/build/86box-on26}
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
sed -e '/^\[Other removable devices\]$/,$d' \
    "$OPENASPI/tests/fixtures/86box-zip.cfg" > "$OUT/86box.cfg"
CYL=$((MB*2048 / (16*63)))
printf '\n[Hard disks]\nhdd_01_parameters = 63, 16, %s, 0, lpt\nhdd_01_fn = %s\nhdd_01_lpt_port = 0\nhdd_01_lpt_bridge = %s\n' \
    "$CYL" "$OUT/hd.img" "$BRIDGE" >> "$OUT/86box.cfg"
grep -q '^lpt1_enabled = 1$' "$OUT/86box.cfg"
if [ -n "$ORIG" ]; then
    printf 'DEVICE=%s %s\r\n' "$(basename "$ORIG" | tr a-z A-Z)" "$ORIGOPT" \
        > "$OUT/CONFIG.SYS"
    case "$ORIGDSK" in
    '') ;;
    ASPIDSK) printf 'DEVICE=ASPIDSK.SYS %s\r\n' "$ORIGDSKOPT" >> "$OUT/CONFIG.SYS" ;;
    *) test -f "$ORIGDSK"
       printf 'DEVICE=%s %s\r\n' "$(basename "$ORIGDSK" | tr a-z A-Z)" \
           "$ORIGDSKOPT" >> "$OUT/CONFIG.SYS" ;;
    esac
    printf 'LASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' >> "$OUT/CONFIG.SYS"
else
    printf 'DEVICE=PORTASPI.SYS /V %s\r\nDEVICE=ASPIDSK.SYS\r\nLASTDRIVE=Z\r\nSHELL=COMMAND.COM /P\r\n' \
        "$MGROPT" > "$OUT/CONFIG.SYS"
fi
cat > "$OUT/AUTOEXEC.BAT" <<'BAT'
@ECHO OFF
ASPISCAN > AUX
TYPE C:\HD.TXT > AUX
COPY C:\BIG.BIN A:\BIG.BIN > NUL
COPY A:\BIG.BIN C:\PLAIN.BIN > NUL
VERIFY ON
COPY A:\BIG.BIN C:\VERIFY.BIN > NUL
VERIFY OFF
IF NOT EXIST A:\BIG.BIN ECHO PORTASPI ON26 read FAILED > AUX
IF NOT EXIST C:\PLAIN.BIN ECHO PORTASPI ON26 plain write FAILED > AUX
IF NOT EXIST C:\VERIFY.BIN ECHO PORTASPI ON26 verified write FAILED > AUX
ECHO PORTASPI ON26 read/write passed > AUX
ECHO PORTASPI ON26 DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS ::/ASPICDA.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
if [ -n "$ORIG" ]; then
    mcopy -o -i "$OUT/boot.img" "$ORIG" ::/"$(basename "$ORIG" | tr a-z A-Z)"
    case "$ORIGDSK" in
    ''|ASPIDSK) ;;
    *) mcopy -o -i "$OUT/boot.img" "$ORIGDSK" ::/"$(basename "$ORIGDSK" | tr a-z A-Z)" ;;
    esac
else
    mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
fi
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPISCAN.COM" "$OPENASPI/build/dos/ASPIDSK.SYS" ::/
rm -f "$OUT/hd.img"
truncate -s $((CYL*16*63*512)) "$OUT/hd.img"
echo 'start=63, type=6' | sfdisk -q "$OUT/hd.img"
PART=$((CYL*16*63 - 63))
mkfs.fat -F16 -a -R 1 -h 63 -g 16/63 -i 26262626 -n PARDISK --offset 63 "$OUT/hd.img" $((PART / 2)) > "$OUT/mkfs.log"
printf 'PORTASPI ON26 file read passed\r\n' > "$OUT/HD.TXT"
head -c 100000 /dev/urandom > "$OUT/BIG.BIN"
mcopy -i "$OUT/hd.img@@32256" "$OUT/HD.TXT" "$OUT/BIG.BIN" ::/
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI ON26 DONE' \
    --expect 'PORTASPI ON26 read/write passed' --count 1 \
    --read-text 'PORTASPI ON26 file read passed'
mcopy -o -i "$OUT/hd.img@@32256" ::/PLAIN.BIN "$OUT/PLAIN.BIN"
mcopy -o -i "$OUT/hd.img@@32256" ::/VERIFY.BIN "$OUT/VERIFY.BIN"
cmp "$OUT/BIG.BIN" "$OUT/PLAIN.BIN"
cmp "$OUT/BIG.BIN" "$OUT/VERIFY.BIN"
dd if="$OUT/hd.img" of="$OUT/part.img" bs=512 skip=63 status=none
fsck.fat -vn "$OUT/part.img" > "$OUT/fsck.log"
case "$BRIDGE" in
    epat) NAME="Shuttle EPAT" ;;
    epia) NAME="Shuttle EPIA" ;;
    bpck) NAME="MicroSolutions BackPack" ;;
    *) NAME="OnSpec 90c26" ;;
esac
DRV=PORTASPI
[ -z "$ORIG" ] || DRV=$(basename "$ORIG")
echo "86Box $NAME ATA disk through $DRV: read/write and host-image validation passed"
