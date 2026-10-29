# PORTASPI - ASPI manager for parallel port and floppy controller devices

**Status: idea.** There is no code yet. This repository collects the plan.

`PORTASPI.SYS` is planned as an ASPI manager (device `SCSIMGR$`) for DOS. It
would make old devices on the parallel port and the floppy controller
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
both.

## Hardware wanted

QEMU and 86Box emulate none of these devices, so testing needs real
hardware. Reports are welcome. If you have any of these and can test, please
open an issue: ZIP parallel, SuperDisk parallel, Backpack, Freecom, SyQuest
parallel, Colorado/Ditto/Conner floppy or parallel tape drives, Ditto Dash
or FC-20 controller cards.

License: GNU General Public License version 2 only.
