# Roadmap

1. **ASPI layer and the first paride protocols, EPAT and BPCK**: host
   adapter inquiry, execute SCSI I/O, chaining; SPP, PS/2 and EPP modes;
   ATAPI packets pass through. Goal: `ASPIFDD.SYS` and `EXFAT.SYS` on the
   SuperDisk LS-120 parallel, `ASPICDA.SYS` on the BackPack CD-ROM. Both
   are emulated by 86Box, so this step can be tested automatically.
2. **The ZIP drives** (PPA, IMM): SCSI over the port. Both are emulated
   by 86Box (`lpt-zip` branch): the ZIP 100 (PPA) and the ZIP 250 (IMM). Then the remaining paride
   protocols (FRPW/FRIQ, ON20/26 and the rest) and ATA disks with the
   SCSI-to-ATA translation.
3. **Floppy tape, reading**: QIC-117 on the floppy controller and on
   parallel port FDC adapters; reading QIC-80/3010/3020 tapes into
   `ASPITAP.SYS` and OpenTape. 86Box has both: the floppy tape drive and
   the Ditto parallel port drive.
4. **Floppy tape, writing and formatting**: segments with ECC, bad sector
   map, volume table, streaming.
5. **Optional**: the floppy drive as a SCSI disk; proprietary CD-ROM
   interfaces; further buses from [devices](devices.md#further-ideas-of-the-same-kind).

## Testing

- **86Box** (current master): floppy tape on the FDC, Ditto parallel,
  BackPack CD-ROM and EPAT with an LS-120 (see the README); the ZIP 100
  (PPA) and ZIP 250 (IMM) parallel on the `lpt-zip` branch of
  geosone/86Box. OpenASPI's `tests/dos_86box_zip.sh` (`ZIP250=1` for the
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
