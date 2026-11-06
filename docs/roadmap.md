# Roadmap

1. **ASPI layer and the first paride protocols, EPAT and BPCK**. **EPAT is
   done in nibble mode (PORTASPI 0.3)**, with ASPIFDD on the LS-120 tested
   in 86Box; byte mode and EPP wait for an emulator or hardware. **BPCK is
   done in nibble and PS/2 mode (0.4)**, with ASPICDA on the BackPack
   CD-ROM tested in 86Box. **ON26 with SCSI-to-ATA translation is done
   (0.5)**: an ATA disk (or ATAPI drive) behind an OnSpec 90c26, tested
   with ASPIDSK on an 86Box model of the chip (lpt-on26 branch). **ATA
   disks behind the EPAT (nibble) and the EPIA (nibble and PS/2) are
   done**, on 86Box models checked with Shuttle's EPATHD.SYS and
   EPIAHD.SYS (lpt-epat-hdd branch), and so is the BackPack hard drive
   (checked with Micro Solutions' BPHDDRV.SYS). **FRPW and FRIQ are done
   (nibble and PS/2)**: CD-ROM and hard disk behind Freecom's Power
   adapter and IQ cable, on an 86Box model checked with Freecom's
   G735.SYS and POWER-CD.SYS. **The BackPack floppy drive is done**
   (a 765 behind the pod, a removable disk for `ASPIFDD.SYS`, checked
   with BACKPACK.SYS), and **so are the first BackPack CD-ROMs** (160550,
   162550: a Mitsumi FX001 behind the pod, a CD-ROM for `ASPICDA.SYS`,
   checked with BPCDDRV.SYS 1.06). Host
   adapter inquiry, execute SCSI I/O, chaining; SPP, PS/2 and EPP modes;
   ATAPI packets pass through. Goal: `ASPIFDD.SYS` and `EXFAT.SYS` on the
   SuperDisk LS-120 parallel, `ASPICDA.SYS` on the BackPack CD-ROM. Both
   are emulated by 86Box, so this step can be tested automatically.
2. **The ZIP drives** (PPA, IMM): SCSI over the port. Both are emulated
   by 86Box master: the ZIP 100 (PPA) and the ZIP 250 (IMM). **Done: the ZIP 100
   (PPA, PORTASPI 0.1) and the ZIP 250 (IMM, 0.2)**, both tested in 86Box in
   all three port modes; they came first, because the drives need no
   translation. Then the remaining paride
   protocols (FRPW/FRIQ and ON26 are done, the rest are rare) and ATA
   disks with the
   SCSI-to-ATA translation.
3. **Floppy tape, reading**: QIC-117 on the floppy controller and on
   parallel port FDC adapters; reading QIC-80/3010/3020 tapes into
   `ASPITAP.SYS` and OpenTape. 86Box has both: the floppy tape drive and
   the Ditto parallel port drive. **QIC-80 on the floppy controller is
   read** (`/F`, tested with 86Box's Colorado 250): the volumes of the
   volume table as tape files, bad sectors skipped, a damaged sector a
   segment corrected from the parity; **and so is the Ditto on the
   parallel port** (86Box's Ditto 2GB with a QIC-80 cartridge), and
   QIC-3010/3020 cartridges in format code 6 on 86Box's QIC-3020 drive.
   Still to do: erasures (up to three sectors with CRC errors; 86Box's
   tape has no such errors to test with), the 1 and 2 Mbit/s rates (the
   controller stays at 500 kbit/s; 86Box does not check the rate), the
   Ditto 2GB's own cartridges and tapes over 65535 segments.
4. **Floppy tape, writing and formatting**: segments with ECC, bad sector
   map, volume table, streaming.
5. **Optional**: the floppy drive as a SCSI disk; proprietary CD-ROM
   interfaces; further buses from [devices](devices.md#further-ideas-of-the-same-kind).

## Testing

- **86Box** (current master): floppy tape on the FDC, Ditto parallel,
  BackPack CD-ROM and EPAT with an LS-120 (see the README); the ZIP 100
  (PPA) and ZIP 250 (IMM) parallel. `make test` runs PORTASPI on the ZIP
  100 (see the README). OpenASPI's `tests/dos_86box_zip.sh` (`ZIP250=1` for the
  IMM drive) already runs Iomega's `ASPIPPM1.SYS`/`ASPIPPM2.SYS` and
  `ASPIFDD.SYS` on them. The IMM model also records where Iomega's driver
  differs from Linux `imm.c` (see `lpt_zip.c`). OpenASPI
  already drives 86Box from its tests (`tests/dos_86box_upc.sh`); the same
  way the PORTASPI tests can boot DOS with these devices.
- **Real hardware**, with logs of `DEVICE=PORTASPI.SYS /V` and the
  OpenASPI tools (`ASPISCAN`, `ASPITEST`).
- **A device model of our own** for what no emulator has:
  QEMU's parallel port can be connected to a chardev, and a small program
  on the host could play a PPA or IMM ZIP drive behind it, so the OpenASPI
  QEMU test suites run against PORTASPI too.

## Toolchain

As the other ASPI managers: JWasm or NASM for the driver, Open Watcom for C
parts, built on Linux; the GeosOne DOS Installer for the release.
