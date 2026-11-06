#!/bin/bash
# OpenTape on a floppy tape: PORTASPI.SYS /F with an emulated Colorado 250
# (86Box master, AUTOMATION=ON) holding a formatted, empty QIC-80
# cartridge from tests/qictape.py, OpenASPI's ASPITAP.SYS on it (NRMT0),
# and OpenTape's OPENTAPE.EXE backing a directory tree up to the tape,
# verifying it after a rewind and restoring it into a new directory; the
# host compares the restored files with the originals.  A QIC-80 volume
# is a stream of bytes, without the record boundaries OpenTape's variable
# blocks need, so OpenTape runs with --native --fixed-block 512, the
# block length PORTASPI starts with.
# DITTO=1: the Iomega Ditto 2GB on LPT1 instead.  Uses OpenASPI's DOS
# boot image and 86Box harness (OPENASPI=path) and an OpenTape build
# (OPENTAPE=path, its build/US/dos/OPENTAPE.EXE).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENASPI=${OPENASPI:-$ROOT/../OpenAspi}
OPENASPI=$(cd "$OPENASPI" && pwd)
OPENTAPE=${OPENTAPE:-$ROOT/../OpenTape}
OPENTAPE=$(cd "$OPENTAPE" && pwd)
BOX=${BOX:-$OPENASPI/../86Box/build-zip/src/86Box}
ROMDIR=${ROMDIR:-$HOME/.local/share/86Box/roms}
MGR=${MGR:-$ROOT/build/PORTASPI.SYS}
EXE=$OPENTAPE/build/US/dos/OPENTAPE.EXE
OUT=${OUT:-$ROOT/build/86box-opentape}
test -x "$BOX"
test -d "$ROMDIR"
test -f "$MGR"
test -f "$EXE"
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
rm -rf "$OUT/SRC" "$OUT/RESTORED" "$OUT/tape.img"
mkdir -p "$OUT/SRC/SUB/EMPTYDIR"
head -c 60000 /dev/urandom > "$OUT/SRC/A.BIN"
head -c 3000 /dev/urandom | base64 > "$OUT/SRC/B.TXT"
head -c 150000 /dev/urandom > "$OUT/SRC/SUB/C.BIN"
: > "$OUT/SRC/SUB/EMPTY.DAT"
python3 -I "$ROOT/tests/qictape.py" "$OUT/tape.img"     # no volumes
sed -e '/^cdrom_01_/d' "$OPENASPI/tests/fixtures/86box-upc.cfg" > "$OUT/86box.cfg"
if [ "${DITTO:-0}" = 1 ]; then
    sed -i 's/^lpt1_enabled = 0$/lpt1_enabled = 1/' "$OUT/86box.cfg"
    cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = 5, lpt
tape_01_medium_type = 12
tape_01_lpt_port = 0
tape_01_image_path = tape.img
CFG
    TAPEOPT=
else
    cat >> "$OUT/86box.cfg" <<CFG

[Other removable devices]
tape_01_parameters = 4, fdc
tape_01_medium_type = 12
tape_01_fdd_unit = 1
tape_01_image_path = tape.img
CFG
    TAPEOPT=/F:1
fi
printf 'DEVICE=PORTASPI.SYS /V %s\r\nDEVICE=ASPITAP.SYS\r\nSHELL=COMMAND.COM /P\r\n' \
    "$TAPEOPT" > "$OUT/CONFIG.SYS"
cat > "$OUT/AUTOEXEC.BAT" <<'BAT'
@ECHO OFF
CTTY AUX
ECHO OPENTAPE START > AUX
REM SvarCOM 2025.1 (SvarDOS/bugz#178) overwrites the first program's command
REM tail after the boot from its 64th byte on (seen with a bare OpenWatcom
REM argv dump, no drivers loaded): a short run first
OPENTAPE --help > NUL
OPENTAPE --native --fixed-block 512 --backup-tree SRC --as ROOT --device NRMT0 --overwrite --volume QIC > AUX
IF ERRORLEVEL 1 GOTO failed
OPENTAPE --native-rewind --device NRMT0 > AUX
IF ERRORLEVEL 1 GOTO failed
OPENTAPE --native --fixed-block 512 --verify --all --device NRMT0 > AUX
IF ERRORLEVEL 1 GOTO failed
OPENTAPE --native-rewind --device NRMT0 > AUX
IF ERRORLEVEL 1 GOTO failed
OPENTAPE --native --fixed-block 512 --restore --device NRMT0 --to RESTORED > AUX
IF ERRORLEVEL 1 GOTO failed
ECHO OPENTAPE QIC OK > AUX
GOTO done
:failed
ECHO OPENTAPE QIC FAILED > AUX
:done
ECHO PORTASPI OPENTAPE DONE > AUX
BAT
sed -i 's/$/\r/' "$OUT/AUTOEXEC.BAT"
mdel -i "$OUT/boot.img" ::/BIG.BIN ::/AHCIASPI.SYS 2>/dev/null || true
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/
mcopy -o -i "$OUT/boot.img" "$MGR" ::/PORTASPI.SYS
mcopy -o -i "$OUT/boot.img" "$OPENASPI/build/dos/ASPITAP.SYS" "$EXE" ::/
mcopy -o -s -i "$OUT/boot.img" "$OUT/SRC" ::/
PORTASPI=$ROOT
ROOT=$OPENASPI
source "$OPENASPI/tests/86box_run.sh"
run_86box_dos --done 'PORTASPI OPENTAPE DONE' \
    --expect 'OPENTAPE START' --count 1 \
    --read-text 'OPENTAPE QIC OK' --steps ${STEPS:-600}
mcopy -s -i "$OUT/boot.img" ::/RESTORED "$OUT/"
diff -r "$OUT/SRC" "$OUT/RESTORED/ROOT"
python3 -I "$PORTASPI/tests/qictape.py" "$OUT/tape.img" --list
echo "OpenTape on a floppy tape through PORTASPI and ASPITAP: backed up, verified, restored and compared"
