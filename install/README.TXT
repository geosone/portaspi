PORTASPI 0.6 - ASPI manager for parallel port drives and floppy tape
====================================================================

PORTASPI.SYS is an ASPI manager (device SCSIMGR$) for DOS.  It makes
parallel port drives and floppy tape drives usable for DOS ASPI drivers:
the OpenASPI drivers that come with it, or others.

Parallel port drives (nibble and PS/2 mode, the ZIP drives also EPP):
  - Iomega ZIP 100 (PPA), ZIP Plus and ZIP 250 (IMM)
  - Imation SuperDisk LS-120 (Shuttle EPAT)
  - Micro Solutions BackPack: CD-ROM, floppy drive, hard drive, and the
    Mitsumi BackPack CD-ROMs
  - ATA hard disks and ATAPI drives behind an OnSpec 90c26, a Shuttle
    EPAT or EPIA, or Freecom's Power adapter and IQ cable
  - the Iomega Ditto tape drive (Ditto 2GB, Ditto Max)

Floppy tape drives (QIC-117) on the floppy controller, with /F: QIC-80,
QIC-3010 and QIC-3020 cartridges (and the Ditto's own, up to 7 GB) are
read, written and formatted as SCSI tape, in the QIC-80 volume format.
OpenASPI's ASPITAP.SYS makes them tape devices for DOS backup programs.

License: GNU General Public License version 2 only (see LICENSE.TXT); the
OpenASPI drivers version 2 or later, the installer (GDI) version 3 (see
GPL3.TXT), SHSUCDX and CDAUDIO see SHSUCD.LIC.
Source:  https://github.com/geosone/portaspi


Installation
------------

Run INSTALL.EXE from the distribution disk or directory.  It copies the
files to C:\OPENASPI (shared with AHCIASPI, EUSBASPI and OpenASPI) and
adds PORTASPI.SYS to CONFIG.SYS.  INSTALL /U in C:\OPENASPI\PORTASPI
removes it again.

The OpenASPI drivers (https://github.com/geosone/OpenAspi) come with it,
each one an option; INSTALL loads the ones chosen after PORTASPI.SYS:

  ASPICDA.SYS  CD-ROM drives (on by default), with SHSUCDX for the
               drive letter
  ASPIFDD.SYS  ZIP, LS-120, the BackPack floppy drive (on by default)
  ASPIDSK.SYS  hard disks
  ASPITAP.SYS  tape drives (TAPECTL.COM)

With PORTASPI already loaded, ASPISCAN.COM finds the disks and tape
drives, and their options are preselected.

By hand: copy PORTASPI.SYS to the hard disk and add it to CONFIG.SYS
before the ASPI drivers that use it, for example:

    DEVICE=C:\OPENASPI\PORTASPI.SYS
    DEVICE=C:\OPENASPI\ASPIFDD.SYS
    DEVICE=C:\OPENASPI\ASPICDA.SYS /D:ASPICDA0

and in AUTOEXEC.BAT for the CD-ROM:

    C:\OPENASPI\SHSUCDX.COM /D:ASPICDA0


Options
-------

  DEVICE=PORTASPI.SYS [/P:addr] [/M:NIBBLE|PS2|EPP] [/F[:unit]] [/T:s]
                      [/ID:name] [/Q] [/V]

  /P:addr   parallel port base address in hex (default: the LPT ports in
            the BIOS data area, the first one with a drive)
  /M:mode   port mode: NIBBLE (any port), PS2 (bidirectional), EPP
            (default: PS2 where the port can read bytes, else NIBBLE)
  /F:unit   also a floppy tape drive on drive select line unit (0-3,
            default 1, the B: line) of the floppy controller at 3F0h;
            PORTASPI then installs without a parallel port drive too
  /T:s      command timeout in seconds (default 15)
  /ID:name  ASPI manager id (default PORTASPI)
  /Q        quiet, /V verbose (shows the chip, port, mode and devices)

PORTASPI finds the chip on its own.  The ASPI adapter name shows chip,
port and mode, for example "PPA 0378 PS/2".


Floppy tape
-----------

The tape is read in the QIC-80 volume format (format codes 2, 4 and 6):
volumes are tape files, a filemark between them.  A damaged sector is put
right from the segment's Reed-Solomon parity, and so are up to three a
segment that cannot be read at all.  WRITE appends a volume at the end of
data (or at a volume start, where the cartridge then ends); a filemark,
REWIND, SPACE or READ closes it and enters it in the volume table.
FORMAT MEDIUM formats a cartridge, with VERIFY also the bad sector map.

READ and WRITE take variable blocks (a volume is a stream of bytes) or
fixed ones of the MODE SELECT block length (512 to start with).  Backup
programs that need record boundaries must use fixed blocks; OpenTape
works with --native --fixed-block 512 through ASPITAP.SYS.

The tape runs at 1 Mbit/s where cartridge, drive and controller (82077
or later) have it, otherwise at 500 kbit/s; QIC-3020 needs 1 Mbit/s.
2 Mbit/s is not done.  While the tape runs PORTASPI uses IRQ 6 and DMA
channel 2 (the Ditto: its own buffer instead), and the resident part
grows by a 32 KB segment buffer.


Tested
------

PORTASPI is tested in 86Box, which emulates the ZIP drives, the
SuperDisk, the BackPack drives, the OnSpec, Shuttle and Freecom bridges,
QIC-117 floppy tape drives and the Ditto.  Reports from real hardware
are welcome: https://github.com/geosone/portaspi/issues
