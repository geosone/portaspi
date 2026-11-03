# Design

## One manager, several back ends

`PORTASPI.SYS` is one DOS device driver (`SCSIMGR$`) with back ends that
are chosen on the command line or found by probing:

```dos
DEVICE=C:\OPENASPI\PORTASPI.SYS [/P:378] [/M:SPP|PS2|EPP] [/F:3F0] [/V]
```

- `/P:` parallel port address (default: the ports in the BIOS data area)
- `/M:` the port mode to use (default: the best one that works)
- `/F:` floppy controller for tape (default: none; the primary FDC
  belongs to DOS)
- `/V` show what was found

Each back end gives the same small interface to the ASPI layer: "execute
this CDB on target t with this data buffer" and "reset". The ASPI layer
(host adapter inquiry, device type, execute SCSI I/O, abort, reset,
chaining to an earlier manager) is the same as in AHCIASPI and EUSBASPI; its
behaviour is the reference, but the code has to be written again or
licensed for GPLv2 by its authors (see the README).

| Back end | Translation |
|---|---|
| PPA, IMM | none: the CDB goes to the SCSI bus behind the port |
| paride protocols, ATAPI device | none: the CDB is the ATAPI packet |
| paride protocols, ATA disk | SCSI to ATA for the commands DOS drivers use |
| floppy tape | SCSI tape commands to the QIC-80 volume format (below) |
| floppy drive | SCSI block commands to FDC read/write of sectors |

## What the OpenASPI drivers need

The drivers expect the same things from PORTASPI as from AHCIASPI and
EUSBASPI.

**ASPI functions.** PORTASPI needs:
- 00h HA INQUIRY
- 01h GET DEVICE TYPE
- 02h EXECUTE SCSI I/O
- 03h ABORT
- 04h RESET DEVICE
- 06h GET DISK INFO. It returns DriveFlags 00h for a drive that neither the
  BIOS nor DOS uses. Every parallel port drive is such a drive, unless a
  vendor driver loaded before PORTASPI has taken it.

05h SET HOST ADAPTER PARAMETERS is not used.

**Sense.** After each CHECK CONDITION, PORTASPI must fill the SRB with
automatic sense data. A cartridge change must show up as UNIT ATTENTION on
the next command. PORTASPI must not swallow the change, and it must not
repeat the command for the driver. No retries, and no replays of writes.

**Direct access devices** (`ASPIFDD`, `ASPIDSK`, `EXFAT.SYS`). These commands
are needed:
- TEST UNIT READY
- INQUIRY: removable bit set, with the real vendor and product. ASPIFDD
  chooses its devices by model name (ZIP, LS-120, SyQuest, ...).
- READ CAPACITY
- READ(10), WRITE(10), WRITE AND VERIFY(10), VERIFY(10)
- PREVENT ALLOW MEDIUM REMOVAL
- START STOP UNIT (eject)
- REQUEST SENSE
- MODE SENSE(6), header only (DBD, page 3Fh, 4 bytes): the WP bit in the
  device-specific byte. Each volume load asks for it, and a protected
  medium then gets no writes. For ATAPI devices behind a protocol chip,
  MODE SENSE(6) is translated to MODE SENSE(10) and the answer converted
  back, keeping the WP bit.

**ATA disks behind a protocol chip** (for example the parallel EZ135 /
EZFlyer). The SCSI to ATA layer works like AHCIASPI's `src/ata.inc`:
- CHS addressing with the geometry from IDENTIFY, because these drives have
  no LBA
- the sense data per device, mapped from the ATA error register (NM, MC, WP)
- the WP bit for MODE SENSE from GET MEDIA STATUS, where the drive has it.
  GET MEDIA STATUS also reports a media change, and the drive clears it by
  reporting it. MODE SENSE must then return UNIT ATTENTION, so the change
  is not lost.

**CD-ROM** (`ASPICDA`). PORTASPI passes these through: READ(10), READ CD
(BEh), READ TOC (43h), READ SUB-CHANNEL (42h), PLAY AUDIO MSF (47h),
PAUSE/RESUME (4Bh), SEEK(10), START STOP UNIT, PREVENT ALLOW, MODE SENSE(10)
page 2Ah, and MODE SELECT(10).

**Tape** (`ASPITAP`, `TAPECTL`). These commands are used: REWIND (01h),
READ BLOCK LIMITS (05h), READ(6) / WRITE(6) (08h / 0Ah, with transfer
length and fixed bit), WRITE FILEMARKS (10h), SPACE (11h), LOAD UNLOAD
(1Bh), PREVENT ALLOW (1Eh), LOCATE(10) and LOCATE(16) (2Bh / 92h), READ
POSITION (34h), LOG SENSE (4Dh), and MODE SENSE(6) / MODE SELECT(6).

The 6 byte block translations (READ(6) to READ(10), REZERO UNIT to
SEEK(10)) apply to block devices only: disk, WORM, CD and MO. On tape, 01h
is REWIND, and 08h / 0Ah carry no LBA. AHCIASPI and EUSBASPI make the same
distinction. The floppy tape back end produces exactly this tape command
set.

## The parallel port

- Port modes: SPP (nibble reads), PS/2 (byte reads), EPP; ECP only where a
  protocol needs it. The Linux `parport` layer and the drivers' own
  `ppa_*`/`imm_*` port routines show the exact sequences and timing.
- No interrupts: the transfers are polled, as in the Linux drivers. A
  timeout per transfer, and no automatic retries of writes.
- Sharing: a printer on the same port is not possible while a drive is
  attached (as with Iomega's drivers).
- Timing: ISA I/O delays matter on fast machines; the Linux drivers' delays
  are the starting point, measured with a timer, not with loops.

## The floppy controller

- Floppy tape drives sit on the FDC as drive 1 (sometimes 2 or 3) and are
  selected with the QIC-117 "pulse" commands (step pulses on the step
  line); status comes back through the track 0 line.
- DMA: data moves through ISA DMA channel 2, so the buffers must be below
  16 MB and must not cross a 64 KB page; a bounce buffer in conventional
  memory.
- The primary FDC is shared with DOS's floppy drives: PORTASPI must take it
  only while no BIOS floppy access is running (hook INT 13h for drives A:/B:
  and serialise), or use a second controller (Ditto Dash, FC-20) on its own
  ports.
- Under Windows 3.x the FDC and DMA are virtualised: a VxD part may be
  needed, as `AHCIVXD.386` is for AHCIASPI.

## Floppy tape

QIC-40/80/3010/3020 and Travan drives store fixed size segments (32
sectors of 1 KB, 3 of them Reed-Solomon ECC) on tracks; there are no
blocks, no filemarks and no SCSI style status. To appear as a SCSI tape
drive PORTASPI keeps the QIC-80 volume format itself, as Linux
`ftape`/`zftape` does:

- the header segments, the bad sector map and the volume table;
- READ(6)/WRITE(6) of fixed or variable blocks on top of the segments, with
  the ECC encoded on write and corrected on read;
- WRITE FILEMARKS, SPACE and READ POSITION through the volume table;
- REWIND, LOAD/UNLOAD, ERASE and FORMAT as tape operations (formatting a
  QIC-80 cartridge is its own long job);
- streaming: the drive needs data in time, or it stops and repositions.
  The buffer must be large enough (several segments, so EMS or XMS), and the
  DOS side must not block too long.

This is the largest part of the project and comes last; a usable first
step is reading existing QIC-80 tapes made by the DOS and Windows backup
programs of the time.

## Memory

The manager stays resident. Each back end is optional (`/NOTAPE`, ...), so
that a system with only a ZIP drive does not pay for the tape code. The
tape buffers go to XMS or EMS when present.
