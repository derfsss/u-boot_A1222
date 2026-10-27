#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2026 Richard Gibbs
"""Read-only summary of an A-Eon A1222 (Tabor) SD image. Run with `python3 -I`.

Reads only the first --head bytes (default 16 MiB) and never writes. Prints the MBR, the nonzero
extents of the firmware area, U-Boot version strings, the environment block at 0x600000 (CRC-checked),
the header of any ELF image at 0x800000 (where an official card keeps A-Eon's AmigaBoot) and the
first BMP at 0x900000. MACs are not printed.

  python3 -I inspect_image.py card.img [--head 0x1000000]
"""
import argparse
import hashlib
import re
import struct
import zlib

# AB_OFF: where an official card keeps an ELF image (A-Eon's AmigaBoot). Only reported; this firmware does not use it.
ENV_OFF, ENV_SIZE, AB_OFF, DATA_OFF = 0x600000, 0x2000, 0x800000, 0x900000


def extents(d, lo, hi, g):
    out, cur = [], None
    for o in range(lo, min(hi, len(d)), g):
        nz = any(d[o:o + g])
        if nz and cur is None:
            cur = o
        if not nz and cur is not None:
            out.append((cur, o))
            cur = None
    if cur is not None:
        out.append((cur, min(hi, len(d))))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image")
    ap.add_argument("--head", type=lambda x: int(x, 0), default=16 << 20)
    a = ap.parse_args()
    with open(a.image, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        f.seek(0)
        d = f.read(a.head)
    print("file size %d (%#x); sha256 of first %d MiB: %s" % (size, size, len(d) >> 20, hashlib.sha256(d).hexdigest()[:16]))
    print("MBR signature", d[510:512].hex())
    for i in range(4):
        e = d[446 + 16 * i:462 + 16 * i]
        if e[4]:
            print("  part %d type %#x start %d count %d" % (i + 1, e[4], *struct.unpack("<II", e[8:16])))

    # eSDHC boot header (P1022 RM 4.6.1.3): the on-chip ROM looks for 'BOOT' at 0x40 + i*0x200.
    for i in range(24):
        o = 0x40 + i * 0x200
        if d[o:o + 4] == b"BOOT":
            w = lambda x: struct.unpack(">I", d[o - 0x40 + x:o - 0x40 + x + 4])[0]
            n = w(0x68)
            print("eSDHC boot header at %#x (copy %d): code length %#x, source %#x, target %#x, exec %#x, %d config pairs" %
                  (o - 0x40, i, w(0x48), w(0x50), w(0x58), w(0x60), n))
            for k in range(min(n, 40)):
                addr, data = w(0x80 + 8 * k), w(0x84 + 8 * k)
                if addr & 1:
                    what = ("end" if addr & 0x80000000 else "") + (" delay" if addr & 0x40000000 else "")
                    print("  pair %d: control %#010x data %#010x (%s)" % (k + 1, addr, data, what.strip()))
                else:
                    print("  pair %d: [%#010x] <= %#010x" % (k + 1, addr, data))
            break
    else:
        print("no eSDHC BOOT signature at 0x40 + i*0x200 (i < 24)")

    print("nonzero extents below 0x100000 (512 B granularity):")
    for x, y in extents(d, 0, 0x100000, 512):
        print("  %#08x-%#08x (%d B)" % (x, y, y - x))
    for m in re.finditer(rb"U-Boot [a-z]+-[0-9-]+ \([^)]*\)", d[:0x100000]):
        print("version string at %#x: %s" % (m.start(), m.group().decode()))

    blk = d[ENV_OFF:ENV_OFF + ENV_SIZE]
    if len(blk) == ENV_SIZE and any(blk):
        stored = struct.unpack(">I", blk[:4])[0]
        calc = zlib.crc32(blk[4:]) & 0xFFFFFFFF
        end = blk[4:].find(b"\0\0")
        print("env @%#x: crc stored %#010x calc %#010x -> %s; %d bytes used" %
              (ENV_OFF, stored, calc, "OK" if stored == calc else "MISMATCH", end + 6))
    else:
        print("env @%#x: empty or short" % ENV_OFF)

    e = d[AB_OFF:AB_OFF + 64]
    if e[:4] == b"\x7fELF":
        ph = struct.unpack(">I", e[28:32])[0]
        t, off, va, pa, fs, ms = struct.unpack(">IIIIII", d[AB_OFF + ph:AB_OFF + ph + 24])
        print("ELF image @%#x: class %d data %d machine %d entry %#x load vaddr %#x filesz %#x" %
              (AB_OFF, e[4], e[5], struct.unpack(">H", e[18:20])[0], struct.unpack(">I", e[24:28])[0], va, fs))
    else:
        print("no ELF at %#x (first bytes %s)" % (AB_OFF, e[:4].hex()))

    b = d[DATA_OFF:DATA_OFF + 54]
    if b[:2] == b"BM":
        w, h, _, bpp = struct.unpack("<iiHH", b[18:30])
        print("first BMP @%#x: %dx%d %dbpp, %d bytes" % (DATA_OFF, w, h, bpp, struct.unpack("<I", b[2:6])[0]))
    else:
        print("no BMP at %#x" % DATA_OFF)


if __name__ == "__main__":
    main()
