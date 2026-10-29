# Roadmap

1. **ASPI layer and the ZIP drives** (PPA, IMM): host adapter inquiry,
   execute SCSI I/O, chaining; SPP, PS/2 and EPP modes. Goal: `ASPIFDD.SYS`
   and `EXFAT.SYS` on a parallel ZIP 100 and ZIP 250.
2. **The paride protocol chips** (EPAT first, then BPCK, FRPW/FRIQ, ON20/26
   and the rest): ATAPI devices (CD-ROM, LS-120) pass through; then ATA
   disks with the SCSI-to-ATA translation.
3. **Floppy tape, reading**: QIC-117 on the floppy controller and on
   parallel port FDC adapters; reading QIC-80/3010/3020 tapes into
   `ASPITAP.SYS` and OpenTape.
4. **Floppy tape, writing and formatting**: segments with ECC, bad sector
   map, volume table, streaming.
5. **Optional**: the floppy drive as a SCSI disk; proprietary CD-ROM
   interfaces; further buses from [devices](devices.md#further-ideas-of-the-same-kind).

## Testing

No emulator has these devices, so there are two ways:

- **Real hardware**, with logs of `DEVICE=PORTASPI.SYS /V` and the
  OpenASPI tools (`ASPISCAN`, `ASPITEST`).
- **A device model for tests**: QEMU's parallel port can be connected to a
  chardev; a small program on the host could play a PPA or IMM ZIP drive
  (or an EPAT adapter with an ATAPI CD-ROM) behind it. That would let the
  OpenASPI QEMU test suites run against PORTASPI, as they do for AHCIASPI
  and EUSBASPI. Floppy tape would need the same for QEMU's FDC.

## Toolchain

As the other ASPI managers: JWasm or NASM for the driver, Open Watcom for C
parts, built on Linux; the GeosOne DOS Installer for the release.
