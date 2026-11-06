# PORTASPI - ASPI manager for parallel port and floppy controller devices

**Status: ZIP drives, LS-120, BackPack CD-ROM and floppy drive, OnSpec, EPAT, EPIA, BackPack and Freecom; floppy tape reading, writing and formatting.**
`PORTASPI.SYS` 0.5
drives Iomega's parallel port ZIP drives in nibble, PS/2 and EPP mode: the
ZIP 100 (PPA chip) and the ZIP Plus and ZIP 250 (IMM chip). It also drives
ATAPI drives behind two bridges: the Shuttle EPAT, as in the parallel port
SuperDisk LS-120 (nibble mode so far), and the Micro Solutions BackPack,
as in the BackPack CD-ROM (nibble and PS/2 mode). Behind an OnSpec 90c26
(nibble and 8-bit mode), a Shuttle EPAT (nibble mode), a Shuttle EPIA
(nibble and PS/2 mode), the BackPack (the BackPack hard drive) and
Freecom's two bridges, the Power adapter and the IQ cable (nibble and PS/2
mode), it drives an ATAPI drive or a plain ATA hard disk:
SCSI commands are translated to ATA (READ/WRITE/VERIFY SECTORS in LBA or
CHS, INQUIRY and READ CAPACITY from IDENTIFY DEVICE, sense data from the
error register). The BackPack floppy drive, a 765 floppy controller behind
the BackPack (nibble and PS/2 mode), becomes a removable SCSI disk for
OpenASPI's `ASPIFDD.SYS`: the disk's format is found with READ ID, sectors
go a track at a time through the pod's buffer. The first BackPack CD-ROMs
(160550, 162550), a Mitsumi FX001 or FX001D behind the BackPack (nibble
and PS/2 mode), become a SCSI CD-ROM drive: READ (10) a sector at a time,
READ CAPACITY and READ TOC from the drive's disc information (the first
track and the lead-out only). With `/F` a QIC-117 floppy tape drive on the
floppy controller becomes a SCSI tape drive: each volume of a
QIC-80 cartridge (the volume table of QIC-80/QIC-113) is a tape file,
read with READ (6) and written with WRITE (6) of variable blocks, with
filemarks between them;
QIC-3010 and QIC-3020 cartridges too (format code 6), and the Ditto's
own of 2 GB (format code 4, its scrambled header) to 7 GB (over 65535
segments). The
Iomega Ditto on the parallel port, the same drive and a 765 behind a
BackPack, is read the same way; PORTASPI finds it without `/F`.
Everything else below is still the plan.

`PORTASPI.SYS` is an ASPI manager (device `SCSIMGR$`) for DOS. It is meant
to make old devices on the parallel port and the floppy controller
usable for DOS ASPI drivers: Iomega ZIP drives on the parallel port,
parallel port CD-ROM, disk and tape drives, and floppy tape drives (QIC-80,
Travan, Ditto). Each device appears as an ordinary SCSI target, so the
[OpenASPI](https://github.com/geosone/OpenASPI) drivers work on it
unchanged:

| Device | becomes | used by |
|---|---|---|
| ZIP 100/250 parallel, SuperDisk parallel, EZ-135/EZ-Flyer parallel | removable direct access device | `ASPIFDD.SYS`, `EXFAT.SYS`, `ASPIFMT`, `ASPIDD` |
| Parallel port hard disks (Backpack, Freecom, ...) | direct access device | `ASPIDSK.SYS` |
| BackPack floppy drive | removable direct access device | `ASPIFDD.SYS` |
| Parallel port CD-ROM | CD-ROM device | `ASPICDA.SYS`, SHSUCDX, CD audio |
| Parallel port and floppy tape drives | sequential access device | `ASPITAP.SYS`, [OpenTape](https://github.com/geosone/OpenTape) |
| The floppy drive itself (optional) | removable direct access device | `ASPIDD` for disk images of odd formats |

Like [AHCIASPI](https://github.com/geosone/ahciaspi) and
[EUSBASPI](https://github.com/geosone/eusbaspi), PORTASPI would chain to an
ASPI manager loaded before it, so all of them can be used together.

- [Devices](docs/devices.md): the device families, their protocols and the
  Linux drivers to start from
- [Design](docs/design.md): how the manager is built, and what is hard
- [Roadmap](docs/roadmap.md): the order of the work and how to test it

## Use

```dos
DEVICE=C:\OPENASPI\PORTASPI.SYS [/P:378] [/M:NIBBLE|PS2|EPP] [/F[:1]] [/T:15] [/ID:name] [/Q] [/V]
DEVICE=C:\OPENASPI\ASPIFDD.SYS
```

- `/P:` the port address in hex. By default PORTASPI tries the LPT ports
  in the BIOS data area and takes the first one with a drive.
- `/M:` the port mode. By default it uses PS/2 when the port can read bytes
  (or has an ECR), else nibble mode. EPP is only used with `/M:EPP`.
- `/T:` the command timeout in seconds (default 15).
- `/F:` also a floppy tape drive (QIC-117) on drive select line 0 to 3
  (default 1, the B: line) of the floppy controller at 3F0h. It gets the
  first free SCSI ID; PORTASPI then installs even without a parallel port
  drive. The tape is read in the QIC-80 volume format (format codes 2 to
  6, that is QIC-80 and its long tapes, QIC-3010, QIC-3020 and the
  Ditto's; tested are 2, 4 and 6): the header segment with the bad sector
  map, the volume table, the volumes' segments with
  their three ECC sectors left out. A sector that reads back wrong is put
  right from the segment's Reed-Solomon parity (one a segment), and so are
  up to three a segment that the controller cannot read at all (CRC
  errors, erasures): the read goes on past them while the tape streams,
  and a retry only fetches what is still missing.
  REWIND, SPACE over filemarks and to the end of data, READ BLOCK LIMITS,
  MODE SENSE and LOAD/UNLOAD are there. WRITE (6) writes a new volume at
  the end of data, or at the start of a volume, where the cartridge then
  ends (the volumes from there on are dropped, as on any tape written
  from a point); the segments get their parity, bad sectors are skipped,
  and any filemark (WRITE FILEMARKS), REWIND, SPACE or READ ends the
  volume and puts it into the volume table (QIC-113 entries in format
  code 6), dated from the BIOS clock. A full cartridge is VOLUME
  OVERFLOW with EOM and the residue. FORMAT MEDIUM formats the cartridge
  (blank or not): the reference burst if it has none, every track while
  it streams (the 765's FORMAT, 128-sector cylinders ended by the DMA
  count as in ftape, gap 233, or 248 for the flex lengths), then the
  header twice and an empty volume table. The tracks are those the drive
  seeks to, the segments a track what it calibrates to (QIC-40/80 drives
  that cannot: from the cartridge's length); the format code is 2 up to
  6912 segments of QIC-40/80, 4 up to 65535 and 6 above and for
  QIC-3010/3020. With VERIFY every segment is read after: sectors with
  CRC errors go into the bad sector map, a segment with more than three
  or not found as a whole. The tape runs at 1 Mbit/s where the cartridge (not QIC-40), the
  drive (SELECT RATE, and its configuration then says so) and the
  controller (an 82077 or later: VERSION 90h) all have it, otherwise at
  500 kbit/s; it drops back to 500 kbit/s if the header cannot be read.
  2 Mbit/s (an 82078-1) is not done; QIC-3020 needs 1 Mbit/s or more. While it reads PORTASPI takes IRQ 6 and DMA channel 2, and keeps the
  BIOS from turning the floppy motors off under it; the resident part grows
  by the 32 KB segment buffer.
- An Iomega Ditto on the port is found with the BackPack units: a pod
  whose 765 (at registers 40h-47h) shows its interrupt in register 00h.
  Its tape is SCSI ID 0 and is read like the one on `/F`: the segments go
  through the pod's 128 KB buffer instead of DMA channel 2. Nibble and
  PS/2 mode.
- PORTASPI finds the chip on its own. It tries the IMM first (it answers
  IEEE 1284.3 command packets), then the EPAT (the drive's task file must
  keep what is written to it), then the BackPack (units 0 to 7, which
  answer with their address and its complement), then the OnSpec 90c26
  (its command packet answers B, 5, F on the status lines), then the
  Shuttle EPIA (the link test of Shuttle's EPIAHD.SYS: command 80h hands
  the data lines back), then Freecom's IQ cable and Power adapter (the
  test pattern of register 10h and a task file register that keeps what
  is written to it; the IQ cable's drive is powered on first), then the
  PPA.
- The EPAT is driven in nibble mode whatever `/M:` says. 86Box does not
  emulate its byte mode block reads or EPP, so they are not in yet. The
  BackPack runs in nibble or PS/2 mode; `/M:EPP` falls back to nibble,
  because Linux's EPP register access is not what 86Box's model (written
  from the vendor's driver) takes. The ATAPI drive behind a bridge is
  target 0. MODE SENSE(6)/SELECT(6), READ(6),
  WRITE(6), SEEK(6) and REZERO UNIT are rewritten into the 10-byte commands
  for it, as AHCIASPI and EUSBASPI do.
- The ASPI adapter name shows the chip, port and mode, for example
  `PPA 0378 PS/2` or `IMM 0378 EPP`. The drive keeps its SCSI ID (6 for
  the ZIP 100).

Building needs NASM: `make` gives `build/PORTASPI.SYS`.

## Testing

`make test OPENASPI=../OpenAspi [MODE=NIBBLE|PS2|EPP]` boots MS-DOS in
86Box master (built with `AUTOMATION=ON`) with a ZIP 100 on LPT1. It loads
PORTASPI and OpenASPI's `ASPIFDD.SYS`, then copies files on the cartridge,
also with `VERIFY ON`. The image is then checked on the host. `ZIP250=1`
takes the ZIP 250 (IMM) instead. `make test-ls120` does the same with an
LS-120 behind the EPAT (OpenASPI's `tests/dos_86box_ls120.sh LPT=1 MGR=`).
`make test-bpck` boots with a BackPack CD-ROM: OpenASPI's `ASPICDA.SYS` and
SHSUCDX read the CD and copy a file from it, which the host compares
(`tests/dos_86box_bpck.sh`; `ORIG=path/BPCDDRV.SYS` runs Micro Solutions'
own driver instead, as a black box against the 86Box model: version 4.02
of 2002 passes, 2.13 of 1998 hangs at init). `BRIDGE=bpmcd` takes the
Mitsumi BackPack CD-ROM instead (86Box with the `lpt_bpmcd` device), also
with `ORIG=path/BPCDDRV.SYS` version 1.06 of 1994 and `MSCDEX=`.
`tests/dos_86box_bpfdd.sh` takes the BackPack floppy drive with a 1.44 MB
disk through `ASPIFDD.SYS` (`MGROPT=/M:NIBBLE` or `/M:PS2`), or with
`ORIG=path/BACKPACK.SYS` through Micro Solutions' own driver; DOS reads a
file from it and writes one, the host compares both and runs `fsck.fat`.
`SWAP=1` then changes the disk to a write protected 720 KB one: DOS must
see the change and read it, and writes must fail with DOS error 00h.
`make test-on26` boots with an ATA hard disk behind an OnSpec 90c26:
OpenASPI's `ASPIDSK.SYS` mounts its FAT16 partition, DOS reads and writes
it and the host compares the files and checks the file system
(`tests/dos_86box_on26.sh`, `LBA=0` for a CHS-only drive, `MB=` the size).
86Box has no such device upstream; the model, `src/device/lpt_on26.c`, is
on the `lpt-on26` branch of github.com/geosone/86Box, written from Linux's
on26.c and the ATA standard. `BRIDGE=epat`, `BRIDGE=epia` and
`BRIDGE=bpck` put the disk behind a Shuttle EPAT or EPIA or a BackPack
instead (`lpt-epat-hdd` branch; the EPIA model is `src/device/lpt_epia.c`,
from Linux's epia.c). `BRIDGE=frpw` and `BRIDGE=friq` take Freecom's
Power adapter or IQ cable (`src/device/lpt_freecom.c`, from Linux's frpw.c
and friq.c); the CD-ROM test takes the same `BRIDGE=` (and `on26`,
`epia`), with Freecom's POWER-CD.SYS (IQ) or G735.SYS (Power) as `ORIG=`.
`ORIG=` runs a
vendor driver against the model, `ORIGDSK=` the block driver after it:
ValuStor's VSMOBILE.SYS (V2.00 and V3.26) and OnSpec's EGIDE100.SYS (90c26), Shuttle's
EPATHD.SYS and EPIAHD.SYS with ASPIDSK or Shuttle's ASPIHDRM.SYS, and
Micro Solutions' BPHDDRV.SYS (2002) on the BackPack.
`tests/dos_86box_qic.sh` puts a Colorado 250 floppy tape drive on drive
select line 1 with a QIC-80 cartridge that `tests/qictape.py` writes:
three volumes, one across a bad sector and one from track 0 into the
reversed track 1, and four sectors damaged after their parity was written
(data and parity sectors), which the reader must correct. `QICREAD.COM` (`tests/qicread.asm`) reads every volume
through ASPI and prints its size and CRC-32, spaces over filemarks and
reads again; the host compares. `TAPE=` takes another cartridge image.
`DITTO=1` puts the cartridge into an Iomega Ditto 2GB on LPT1 instead
(86Box's `lpt_ditto.c`; its log of every port access is on in master,
which makes the run take hours, so the test is meant for a build with it
off). `FORMAT=qic3010` or `qic3020` with `DITTO=1` takes 86Box's Iomega
QIC-3020 drive and a cartridge of that standard in format code 6, with a
whole bad segment and the third volume from near the end of track 0 into
track 1. `FORMAT=ditto2gb` is the Ditto 2GB with its own cartridge (format
code 4, the header scrambled as the Ditto writes it), `ditto5gb` and
`dittomax7` the Ditto Max with cartridges of over 65535 segments, the
third volume past segment 65535.
`WRITE=W` makes `QICREAD` first write two volumes at the end of data
(70000 bytes in blocks of 10000 and 300000 in blocks of 32768, from a
generator the host knows too), `WRITE=T` one after the first volume,
which ends the cartridge there; it then reads all back, and
`tests/qictape.py --check` looks at the image: the volume table, and the
written volumes' sizes, CRCs and parity. Every drive and format above
writes. `WRITE=F` (QIC-80) has the header end the cartridge at segment
170 and fills it: VOLUME OVERFLOW with EOM and the residue, at once again
after the filemark. `WRITE=X` starts from a blank cartridge (an empty
image) that QICREAD formats with FORMAT MEDIUM and then writes as `W`,
`WRITE=V` with VERIFY; with `DITTO=1` `MEDIUM_X=` takes another 86Box
medium for it.
`tests/dos_86box_qicecc.sh` runs the tape's Reed-Solomon code
(`src/qicecc.inc`) on its own in DOS: `QICECC.COM` (`tests/qicecc.asm`)
mends the segments `tests/qictape.py --ecc-vectors` writes, with one to
three erasures (also in the parity sectors and in short segments), a
damaged sector, and damage beyond the parity, which it must find, and
computes the parity of segments to write.
`PROT=1` adds Iomega's write protection (ASPICTL PROTECT/UNPROTEC), which
checks CHECK CONDITION and the sense data. This uses OpenASPI's
`tests/dos_86box_zip.sh` with `MGR=`.

## Why GPL version 2 only

Most of the knowledge about these devices is in Linux drivers: `ppa` and
`imm` for the ZIP drives, `pata_parport` (formerly `paride`) for the
parallel port protocol chips, `ftape`/`zftape` for floppy tape, and the
old CD-ROM drivers. They are licensed GPL-2.0-only. So that their code can
be taken over directly, PORTASPI is **GPL-2.0-only** as well (see
[LICENSE](LICENSE)).

OpenASPI, AHCIASPI and EUSBASPI are GPL version 3. That is no problem:
PORTASPI is a separate program, and the drivers talk to it only through
the ASPI interface, as with any third-party ASPI manager. Code is not moved
between the GPLv2 and the GPLv3 projects, unless its authors license it for
both. The ASPI layer of PORTASPI comes from EUSBASPI; its author licenses
it for PORTASPI under the GPL version 2 as well.

## Testing and hardware wanted

[86Box](https://github.com/86Box/86Box) (current master, autumn 2026)
emulates several of these devices, so much can be tested without hardware:

- a QIC-117 floppy tape drive on the floppy controller (QIC-80, QIC-80XL;
  `src/floppy/fdd_tape.c`),
- the Iomega Ditto parallel port tape drive (Ditto 2GB, Ditto Max,
  QIC-3020; cartridges QIC-80, QIC-3010/3020, Travan TR-1 to TR-3, Ditto
  2 to 7 GB; `src/device/lpt_ditto.c`),
- the Micro Solutions BackPack CD-ROM (BPCK protocol;
  `src/device/lpt_bpck.c`) and, on the `lpt-bpck-link` branch of
  github.com/geosone/86Box (86Box PR #8733), the BackPack floppy drive
  (`src/device/lpt_bpfdd.c`),
- the Shuttle EPAT bridge with an Imation SuperDisk LS-120 (Imation's own
  DOS driver works on it; `src/device/lpt_epat.c`),
- the Iomega ZIP 100 (PPA) and ZIP 250 / ZIP Plus (IMM) parallel port
  drives (Iomega's `ASPIPPM1.SYS` and `ASPIPPM2.SYS` work on them;
  `src/device/lpt_zip.c`, in master since 2026-11-04).

For everything else real hardware is needed. If you have any of
these and can test, please open an issue: ZIP parallel, Backpack, Freecom,
SyQuest parallel, Colorado/Conner floppy or parallel tape drives, Ditto
Dash or FC-20 controller cards.

License: GNU General Public License version 2 only.
