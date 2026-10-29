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
