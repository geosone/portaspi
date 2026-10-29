# Devices

The device families PORTASPI could support, roughly from easiest to
hardest. "Linux reference" names the driver whose code can be used
(GPL-2.0); several were removed from current kernels and are found in
older source trees.

## Parallel port: SCSI behind a protocol

| Device | Protocol | Linux reference |
|---|---|---|
| Iomega ZIP 100 parallel | PPA (SCSI over SPP/PS2/EPP) | `drivers/scsi/ppa.c` |
| Iomega ZIP Plus, ZIP 250 parallel, Jaz Traveller | IMM (SCSI over SPP/PS2/EPP) | `drivers/scsi/imm.c` |
| Parallel SCSI adapters (Trantor T348/T358, Adaptec MiniSCSI Plus) | own adapter protocols | none known; Adaptec's `APA348` documentation |

These speak SCSI already. The manager only moves CDB, data and status over
the port, so every SCSI device behind them works. The ZIP drives are the
best first target: well documented, common, and Iomega's own DOS drivers
show that ASPI on top of them works.

## Parallel port: ATA and ATAPI behind a protocol chip

| Chip / protocol | Devices | Linux reference |
|---|---|---|
| EPAT (Shuttle) | SyQuest EZ-135/EZ-Flyer/SparQ parallel, Imation SuperDisk LS-120 parallel, Avatar Shark, many CD-ROMs | `pata_parport/epat.c` (formerly `paride/epat.c`) |
| EPIA (Shuttle) | older Shuttle based adapters | `pata_parport/epia.c` |
| BPCK, BPCK6 (MicroSolutions) | Backpack CD-ROM, disk, tape, PD/CD | `pata_parport/bpck.c`, `bpck6.c` |
| FRPW, FRIQ (Freecom) | Freecom Power and IQ CD-ROM, Traveller | `pata_parport/frpw.c`, `friq.c` |
| ON20, ON26 (OnSpec) | OnSpec based adapters | `pata_parport/on20.c`, `on26.c` |
| FIT2, FIT3, KBIC, KTTI, ATEN, COMM, DSTR | various no-name adapters | `pata_parport/*.c` |

One protocol module gives the register access of an IDE channel; on top of
it ATAPI devices (CD-ROM, LS-120, tape) pass SCSI packets through
directly, and ATA disks need a small SCSI-to-ATA translation (INQUIRY, READ
CAPACITY, READ/WRITE(10), TEST UNIT READY, REQUEST SENSE). The old
`paride` high-level drivers `pcd`, `pd`, `pf`, `pt` and `pg` show how each
device type was driven.

## Parallel port: floppy tape behind an FDC replacement

| Device | Interface | Linux reference |
|---|---|---|
| Colorado Trakker, HP Colorado parallel | QIC-117 through a parallel port FDC | `ftape` (`ftape/lowlevel/fdc-*`, parallel port variants) |
| Iomega Ditto parallel | QIC-117 through a parallel port FDC | `ftape` |
| Conner/Seagate TapeStor parallel | QIC-117 | `ftape` |

The same tape logic as for the floppy controller below, only the FDC is
reached through the parallel port.

## Floppy controller: tape

| Device | Formats | Linux reference |
|---|---|---|
| Colorado Jumbo 120/250/350 | QIC-40, QIC-80 | `ftape` 3.04 / 4.x, `zftape` |
| Iomega Ditto 420/800/2GB/Max | QIC-3010, QIC-3020, Travan | `ftape` |
| Conner/Seagate TapeStor 420/800 | QIC-3010, QIC-3020, Travan TR-1/TR-3 | `ftape` |
| Ditto Dash, Colorado FC-10/FC-20, Iomega Ditto controller cards | faster FDC on its own I/O address | `ftape` (`fc-10`, `ditto-dash` support) |

These drives know no blocks and no filemarks, only tracks of segments with
Reed-Solomon ECC, a bad sector map and header segments. To be an ASPI tape
device, PORTASPI must keep the QIC-80 volume format itself, as `zftape`
does on Linux: see [design](design.md#floppy-tape).

## Floppy controller: the floppy drive

The floppy drive as a SCSI removable disk (READ/WRITE(10), READ CAPACITY,
MODE SENSE for the geometry). Of little use for normal disks, which DOS
reads anyway, but it would let `ASPIDD` image and rescue disks of odd
formats (DMF, XDF, 2.88 MB, non-DOS formats) with the same tool as all
other media.

## Further ideas of the same kind

Not parallel port or floppy controller, but old buses that are worth an
ASPI manager in the same spirit; each could be a back end of PORTASPI or a
manager of its own:

- **Proprietary CD-ROM interfaces**, mostly on sound cards: Mitsumi
  (`mcd`, `mcdx`), Sony CDU-31A/33A (`cdu31a`) and CDU-535 (`sonycd535`),
  Panasonic/Matsushita on Creative cards (`sbpcd`), Aztech (`aztcd`),
  Goldstar (`gscd`), Philips CM206 (`cm206`), Optics Storage (`optcd`),
  Sanyo (`sjcd`), ISP16 interface (`isp16`); all in `drivers/cdrom/` of
  older kernels. They would give these drives ASPICDA, SHSUCDX and digital
  CD audio.
- **QIC-02 and QIC-36 tape controller cards** (Wangtek, Archive, Everex):
  `tpqic02` in older kernels.
- **FireWire (OHCI 1394, SBP-2)**: SBP-2 is SCSI over FireWire, and the
  controller is standardised: external FireWire disks, burners and tape
  drives.
- **SD cards** through SDHCI PCI readers.
- **Virtual machines**: virtio-scsi, VMware PVSCSI; also useful for tests.
- **Network block devices**: ATA over Ethernet or iSCSI through a DOS
  packet driver.
