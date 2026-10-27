#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2026 Richard Gibbs
"""Parse a U-Boot environment block out of a raw SD image and print it.

Reads only the block (default: A-Eon A1222 layout, 0x2000 bytes at 0x600000), checks the
CRC32, prints variables. MAC addresses and serials are masked unless --show-secrets is given.
Never writes anything. Run with `python3 -I`.

  python3 -I parse_env.py card.img [--offset 0x600000] [--size 0x2000]
"""
import argparse
import re
import struct
import sys
import zlib

SECRET_KEYS = {"ethaddr", "eth1addr", "eth2addr", "eth3addr", "serial#", "serial", "ethact"}
MAC = re.compile(r"([0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0x600000)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x2000)
    ap.add_argument("--show-secrets", action="store_true")
    a = ap.parse_args()

    with open(a.image, "rb") as f:
        f.seek(a.offset)
        blk = f.read(a.size)
    if len(blk) != a.size:
        sys.exit("short read")

    stored = struct.unpack(">I", blk[:4])[0]  # big-endian target: CRC stored big-endian
    calc = zlib.crc32(blk[4:]) & 0xFFFFFFFF
    print("crc stored %#010x calc %#010x -> %s" % (stored, calc, "OK" if stored == calc else "MISMATCH"))

    body = blk[4:]
    end = body.find(b"\0\0")
    if end < 0:
        sys.exit("no double-NUL terminator")
    names = body[:end].split(b"\0")
    print("%d variables, %d bytes used of %d" % (len(names), end + 2 + 4, a.size))
    for raw in names:
        k, _, v = raw.decode("latin1").partition("=")
        if not a.show_secrets and (k.lower() in SECRET_KEYS or MAC.search(v)):
            v = "<masked>"
        print("%s=%s" % (k, v))


if __name__ == "__main__":
    main()
