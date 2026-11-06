#!/usr/bin/env python3
"""Make a floppy tape cartridge image for 86Box's floppy tape drives, with
volumes in it, the way the QIC-80 / QIC-113 volume format lays them out:

  segment 0, 1   the header segment and its copy: the format code, the
                 geometry, the bad sector map
  segment 2      the volume table: a 128 byte "VTBL" entry a volume
  segment 3 ...  the volumes' data, one after the other

--format picks the cartridge:
  qic80    QIC-80 DC-2120 (307.5 ft), 28 tracks of 150 segments, format
           code 2: the bad sector map a 4 byte sector mask a segment from
           offset 2048, the volume table's segments in two bytes
  qic3010  QIC-3010, 40 tracks of 215 segments, and
  qic3020  QIC-3020, 40 tracks of 422 segments, both format code 6 as
           86Box's lpt_ditto.c lays them down: the header's segment
           numbers in four bytes at 234, the bad sector map a sorted list
           of three byte sector numbers from offset 256 (bit 23: the whole
           segment), the volume table in the QIC-113 rev. G layout (start
           and end segment at 76 and 80)

A segment is 32 sectors of 1024 bytes; its last three good sectors carry
the Reed-Solomon parity of each byte column (GF(256), x^8+x^7+x^2+x+1,
roots a^-1, 1, a).  Bad sectors are skipped.  The image is the tape as
86Box's fdd_tape.c sees it: segment n at n * 32768, sparse.

--damage SEG:SECTOR changes bytes of a good sector after the parity is
written, as a sector that reads back wrong without a CRC error: the
reader must put it right from the parity.

--bad SEG:* marks the whole segment bad (format code 6 only).
--last-at SEG starts the last volume at segment SEG, leaving the ones
before it unused: a volume across the end of a long track.

Usage: qictape.py IMAGE [--format F] [--bad SEG:SECTOR ...]
                  [--damage SEG:SECTOR ...] [--last-at SEG] [--label TEXT]
                  FILE...
       qictape.py --list IMAGE
"""

import argparse
import struct
import sys

SECTOR = 1024
SECTORS = 32
SEGMENT = SECTOR * SECTORS
FORMATS = {"qic80": (28, 150, 2), "qic3010": (40, 215, 6),
           "qic3020": (40, 422, 6)}
TRACKS, SPT, FMT = FORMATS["qic80"]
SEGMENTS = TRACKS * SPT
SEGS_PER_HEAD = 600                             # 150 cylinders of 4
ALL = 0xFFFFFFFF


def set_format(name):
    global TRACKS, SPT, FMT, SEGMENTS
    TRACKS, SPT, FMT = FORMATS[name]
    SEGMENTS = TRACKS * SPT

# GF(256) with x^8+x^7+x^2+x+1
EXP = [0] * 512
LOG = [0] * 256
_x = 1
for _i in range(255):
    EXP[_i] = _x
    LOG[_x] = _i
    _x <<= 1
    if _x & 0x100:
        _x ^= 0x187
for _i in range(255, 512):
    EXP[_i] = EXP[_i - 255]


def mul(a, b):
    if a == 0 or b == 0:
        return 0
    return EXP[LOG[a] + LOG[b]]


# g(x) = (x + a^-1)(x + 1)(x + a) = x^3 + c0 x^2 + c0 x + 1
G = [1, 0xC0, 0xC0, 1]
MUL_C0 = [mul(v, 0xC0) for v in range(256)]


def parity(column):
    """The three parity bytes of a column (highest degree first)."""
    r0 = r1 = r2 = 0
    for m in column:
        c = m ^ r0
        r0 = r1 ^ MUL_C0[c]
        r1 = r2 ^ MUL_C0[c]
        r2 = c
    return r0, r1, r2


def encode(sectors):
    """Data sectors (bytes of 1024) -> the same and the three parity sectors."""
    par = [bytearray(SECTOR) for _ in range(3)]
    for col in range(SECTOR):
        p = parity([s[col] for s in sectors])
        for k in range(3):
            par[k][col] = p[k]
    return list(sectors) + [bytes(p) for p in par]


def good_sectors(bad, seg):
    mask = bad.get(seg, 0)
    return [s for s in range(SECTORS) if not mask & (1 << s)]


def capacity(bad, seg):
    """Data bytes a segment holds."""
    n = len(good_sectors(bad, seg)) - 3
    return max(n, 0) * SECTOR


def put_segment(img, bad, seg, data):
    good = good_sectors(bad, seg)
    nd = len(good) - 3
    data = data.ljust(nd * SECTOR, b"\0")
    secs = [data[i * SECTOR:(i + 1) * SECTOR] for i in range(nd)]
    for pos, sec in zip(good, encode(secs)):
        img.seek(seg * SEGMENT + pos * SECTOR)
        img.write(sec)


def header(bad, label):
    h = bytearray(29 * SECTOR)
    struct.pack_into("<I", h, 0, 0xAA55AA55)
    h[4] = FMT
    if FMT == 6:
        struct.pack_into("<IIII", h, 234, 0, 1, 2, SEGMENTS - 1)
    else:
        struct.pack_into("<HHHH", h, 6, 0, 1, 2, SEGMENTS - 1)
    struct.pack_into("<H", h, 24, SPT)
    h[26] = TRACKS
    h[27] = (SEGMENTS - 1) // SEGS_PER_HEAD     # max floppy side
    h[28] = SEGS_PER_HEAD // 4 - 1              # max floppy track
    h[29] = 128                                 # max floppy sector
    h[30:74] = label.encode("ascii")[:44].ljust(44)
    if FMT == 6:
        off = 256
        for seg in sorted(bad):
            mask = bad[seg]
            if mask == ALL:
                ents = [0x800000 | (seg * SECTORS + 1)]
            else:
                ents = [seg * SECTORS + s + 1 for s in range(SECTORS)
                        if mask & (1 << s)]
            for e in ents:
                h[off:off + 3] = struct.pack("<I", e)[:3]
                off += 3
    else:
        for seg, mask in bad.items():
            struct.pack_into("<I", h, 2048 + 4 * seg, mask)
    return bytes(h)


def vtbl_entry(start, end, size, desc):
    e = bytearray(128)
    e[0:4] = b"VTBL"
    if FMT == 6:
        struct.pack_into("<I", e, 4, end - start + 1)
        struct.pack_into("<HH", e, 58, 113, 7)  # QIC-113 rev. G
        struct.pack_into("<II", e, 76, start, end)
    else:
        struct.pack_into("<HH", e, 4, start, end)
    e[8:52] = desc.encode("ascii")[:44].ljust(44)
    struct.pack_into("<Q", e, 96, size)
    return bytes(e)


def damage(path, spots):
    with open(path, "r+b") as img:
        for seg, sec in spots:
            img.seek(seg * SEGMENT + sec * SECTOR)
            data = bytearray(img.read(SECTOR))
            for col in range(0, SECTOR, 7):
                data[col] ^= (col * 37 + seg + sec) & 0xFF or 0x5A
            img.seek(seg * SEGMENT + sec * SECTOR)
            img.write(data)


def make(path, files, bad, label, last_at=None):
    with open(path, "wb") as img:
        img.truncate(SEGMENTS * SEGMENT)
        put_segment(img, bad, 0, header(bad, label))
        put_segment(img, bad, 1, header(bad, label))
        seg = 3
        table = b""
        for i, name in enumerate(files):
            data = open(name, "rb").read()
            if last_at is not None and i == len(files) - 1:
                assert last_at >= seg, "--last-at before the volumes"
                seg = last_at
            start = seg
            off = 0
            while True:
                cap = capacity(bad, seg)
                if cap:
                    put_segment(img, bad, seg, data[off:off + cap])
                    off += cap
                seg += 1
                if off >= len(data) and seg > start:
                    break
            desc = name.rsplit("/", 1)[-1]
            table += vtbl_entry(start, seg - 1, len(data), desc)
        put_segment(img, bad, 2, table)


def listing(path):
    with open(path, "rb") as img:
        hdr = img.read(SEGMENT)
        if hdr[4] == 6:
            set_format("qic3020" if struct.unpack_from("<H", hdr, 24)[0]
                       == 422 else "qic3010")
        bad = {}
        if FMT == 6:
            off = 256
            while True:
                e = int.from_bytes(hdr[off:off + 3], "little")
                if not e:
                    break
                seg, sec = divmod((e & 0x7FFFFF) - 1, SECTORS)
                bad[seg] = ALL if e & 0x800000 else \
                    bad.get(seg, 0) | (1 << sec)
                off += 3
        else:
            for seg in range(SEGMENTS):
                mask = struct.unpack_from("<I", hdr, 2048 + 4 * seg)[0]
                if mask:
                    bad[seg] = mask
        img.seek(2 * SEGMENT)
        raw = img.read(SEGMENT)
        good = good_sectors(bad, 2)
        vt = b"".join(raw[s * SECTOR:(s + 1) * SECTOR] for s in good[:-3])
        for i in range(0, len(vt), 128):
            e = vt[i:i + 128]
            if e[:4] != b"VTBL":
                break
            if FMT == 6:
                start, end = struct.unpack_from("<II", e, 76)
            else:
                start, end = struct.unpack_from("<HH", e, 4)
            size = struct.unpack_from("<Q", e, 96)[0]
            print(start, end, size, e[8:52].decode("ascii").rstrip())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("image")
    p.add_argument("files", nargs="*")
    p.add_argument("--bad", action="append", default=[],
                   help="SEG:SECTOR, a bad sector (sector 0-31)")
    p.add_argument("--damage", action="append", default=[],
                   help="SEG:SECTOR, a good sector to damage after the parity")
    p.add_argument("--format", choices=sorted(FORMATS), default="qic80")
    p.add_argument("--last-at", type=int,
                   help="the segment the last volume starts at")
    p.add_argument("--label", default="PORTASPI test tape")
    p.add_argument("--list", action="store_true")
    a = p.parse_args()
    if a.list:
        listing(a.image)
        return
    set_format(a.format)
    bad = {}
    for b in a.bad:
        seg, sec = b.split(":")
        seg = int(seg)
        if sec == "*":
            if FMT != 6:
                p.error("a whole bad segment needs format code 6")
            bad[seg] = ALL
        else:
            bad[seg] = bad.get(seg, 0) | (1 << int(sec))
    make(a.image, a.files, bad, a.label, a.last_at)
    damage(a.image, [tuple(int(v) for v in d.split(":")) for d in a.damage])


if __name__ == "__main__":
    sys.exit(main())
