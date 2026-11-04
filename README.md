# PORTASPI - ASPI manager for parallel port and floppy controller devices

**Status: ZIP drives, LS-120 and BackPack CD-ROM.** `PORTASPI.SYS` 0.4
drives Iomega's parallel port ZIP drives in nibble, PS/2 and EPP mode: the
ZIP 100 (PPA chip) and the ZIP Plus and ZIP 250 (IMM chip). It also drives
ATAPI drives behind two bridges: the Shuttle EPAT, as in the parallel port
SuperDisk LS-120 (nibble mode so far), and the Micro Solutions BackPack,
as in the BackPack CD-ROM (nibble and PS/2 mode). Everything else below is
still the plan.

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
DEVICE=C:\OPENASPI\PORTASPI.SYS [/P:378] [/M:NIBBLE|PS2|EPP] [/T:15] [/ID:name] [/Q] [/V]
DEVICE=C:\OPENASPI\ASPIFDD.SYS
```

- `/P:` the port address in hex. By default PORTASPI tries the LPT ports
  in the BIOS data area and takes the first one with a drive.
- `/M:` the port mode. By default it uses PS/2 when the port can read bytes
  (or has an ECR), else nibble mode. EPP is only used with `/M:EPP`.
- `/T:` the command timeout in seconds (default 15).
- PORTASPI finds the chip on its own. It tries the IMM first (it answers
  IEEE 1284.3 command packets), then the EPAT (the drive's task file must
  keep what is written to it), then the BackPack (units 0 to 7, which
  answer with their address and its complement), then the PPA.
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
(`tests/dos_86box_bpck.sh`).
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
  `src/device/lpt_bpck.c`),
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
