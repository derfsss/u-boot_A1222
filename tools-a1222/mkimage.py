#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2026 Richard Gibbs
"""Assemble an A1222 (Tabor) SD-card image in the layout of the official firmware card. Run with `python3 -I`.

Layout:
  0x000000  sector 0: eSDHC boot header (control words + 6 config pairs) and an MBR with one placeholder partition
  0x000400  user code = the build's u-boot-with-spl.bin (SPL padded to 128 KiB, then U-Boot); the boot ROM
            copies it to 0xF8F80000. The SPL finds U-Boot at header.source + CONFIG_SYS_MMC_U_BOOT_OFFS
            (0x400 + 0x20000 = 0x20400, drivers/mmc/fsl_esdhc_spl.c)
  0x600000  U-Boot environment block (optional, from --env: 0x2000 bytes, big-endian CRC32 + data)
  0x900000  boot-splash data image (optional, supplied by you with --data)

The header and the config pairs are what Freescale's boot_format tool writes (kergoth/boot-format, GPL-2.0)
with its SRAM configuration (config_sram_p2020ds.dat), which is the same set of config pairs as on the
official A1222 card. The length is the user code size rounded up to 512, the source address is 0x400
(boot_format's SPI mode starts user code at sector 2) and the exec address is 0xF8F80000.

The image contains only what this tool generates and the files you pass. Images made with it boot
an A1222.
"""
import argparse
import struct
import sys
import zlib

SECTOR = 512
USER_OFFSET = 0x400
ENV_OFFSET = 0x600000
ENV_SIZE = 0x2000
DATA_OFFSET = 0x900000
TARGET = 0xF8F80000

# (address, data) pairs and control words, as in boot_format's config_sram_p2020ds.dat and the official image.
CONFIG_PAIRS = [
    (0xFF720100, 0xF8F80000),   # L2SRBAR0: L2 SRAM base
    (0xFF720E44, 0x0000000C),   # L2ERRINJLO (stock Freescale SRAM boot value)
    (0xFF720000, 0x80010000),   # L2CTL: enable, SRAM mode
    (0xFF72E40C, 0x00000040),   # eSDHC DCR
    (0x40000001, 0x00000100),   # delay 0x100 x 8 CCB clocks
    (0x80000001, 0x80000001),   # end of configuration
]

# MBR bytes 0x1BC..0x1FF as found in the official image: a placeholder FAT32 (type 0x0B) partition entry at
# sector 7690, 7,708,672 sectors, then the 55AA signature. The boot ROM ignores it; hosts see a partition.
MBR_TAIL = bytes.fromhex(
    "0000"                      # 0x1BC..0x1BD
    "00" "020301"               # boot flag, start CHS
    "0b" "79f9bc"               # type FAT32 (LBA), end CHS
    "0a1e0000" "00a07500"       # start LBA 7690, 7,708,672 sectors
    + "00" * 48 + "55aa")


def be32(v):
    return struct.pack(">I", v)


def build_header(user_len):
    sec0 = bytearray(SECTOR)
    sec0[0x40:0x44] = b"BOOT"
    sec0[0x48:0x4C] = be32(user_len)
    sec0[0x50:0x54] = be32(USER_OFFSET)
    sec0[0x58:0x5C] = be32(TARGET)
    sec0[0x60:0x64] = be32(TARGET)
    sec0[0x68:0x6C] = be32(len(CONFIG_PAIRS))
    for i, (a, d) in enumerate(CONFIG_PAIRS):
        sec0[0x80 + 8 * i:0x84 + 8 * i] = be32(a)
        sec0[0x84 + 8 * i:0x88 + 8 * i] = be32(d)
    sec0[0x1BC:0x200] = MBR_TAIL
    return bytes(sec0)


def build_env(path):
    """Environment block from a mkenvimage-style text file (NAME=value lines), U-Boot layout."""
    data = b""
    with open(path, "rb") as f:
        for line in f.read().splitlines():
            if line.strip() and not line.lstrip().startswith(b"#"):
                data += line + b"\0"
    data += b"\0"
    body = data.ljust(ENV_SIZE - 4, b"\0")
    if len(data) > ENV_SIZE - 4:
        sys.exit("environment does not fit in 0x%x bytes" % ENV_SIZE)
    return be32(zlib.crc32(body) & 0xFFFFFFFF) + body


def put(img, off, data):
    if len(img) < off + len(data):
        img.extend(b"\0" * (off + len(data) - len(img)))
    img[off:off + len(data)] = data


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--user-code", required=True, help="u-boot-with-spl.bin from the build")
    ap.add_argument("--out", required=True)
    ap.add_argument("--env", help="environment text file (NAME=value per line)")
    ap.add_argument("--data", help="boot-splash data image (not included)")
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0, help="pad the image to this many bytes")
    a = ap.parse_args()

    user = open(a.user_code, "rb").read()
    user_len = (len(user) + SECTOR - 1) & ~(SECTOR - 1)
    user = user.ljust(user_len, b"\0")

    img = bytearray()
    put(img, 0, build_header(user_len))
    put(img, USER_OFFSET, user)
    if USER_OFFSET + user_len > ENV_OFFSET:
        sys.exit("user code overlaps the environment")
    if a.env:
        put(img, ENV_OFFSET, build_env(a.env))
    if a.data:
        put(img, DATA_OFFSET, open(a.data, "rb").read())
    if a.size and len(img) < a.size:
        img.extend(b"\0" * (a.size - len(img)))
    with open(a.out, "wb") as f:
        f.write(img)
    print("wrote %s: %d bytes; user code %#x bytes at %#x; header length field %#x" %
          (a.out, len(img), user_len, USER_OFFSET, user_len))


if __name__ == "__main__":
    main()
