# A1222 SD card layout

The A1222 boots from an SD card through the P1022's on-chip eSDHC boot ROM. As on the X5000, the
firmware card is a raw image with no usable filesystem, and each part sits at a fixed offset. The
layout below is the one the official A1222 firmware uses; this port follows it, so its images can
replace an official card image.

## Layout

| Offset | Contents | Used by this port |
|---|---|---|
| 0x000000 | sector 0: eSDHC boot header in the MBR boot-code area, then an MBR | yes |
| 0x000400 | the code the boot ROM loads: the SPL, padded to 128 KiB | yes |
| 0x020400 | the main U-Boot (`u-boot.bin`, at most 768 KiB) | yes |
| 0x600000 | U-Boot environment, 8 KiB | yes |
| 0x800000 | the official firmware's AmigaOS loader (AmigaBoot) | no |
| 0x900000 | the official firmware's boot-splash images (8-bit BMPs) to the end of the image | no |

The official release image is 0x242FC00 bytes. `tools-a1222/mkimage.py` (see
[building.md](building.md)) writes sector 0, the code from 0x400 and, optionally, the environment
and a splash data image; it never writes anything at 0x800000.

## Sector 0: the boot header

The boot ROM looks for the ASCII signature `BOOT` at offset 0x40 of a sector (it tries the first 24
sectors). The header fits inside the 446-byte MBR boot-code area, so the same sector also carries a
normal MBR partition table and the 0x55AA signature.

| Offset | Field | Value |
|---|---|---|
| 0x40 | signature | `BOOT` |
| 0x48 | length of the code to load, bytes (a multiple of 512) | see below |
| 0x50 | source: card byte offset of the code | 0x400 |
| 0x58 | target address | 0xF8F80000 |
| 0x60 | execution start address | 0xF8F80000 |
| 0x68 | number of configuration pairs | 6 |
| 0x80 + 8k | configuration pair k: address, data | below |

The boot ROM performs the configuration writes, copies the code to the target and jumps to it. The
six pairs (the same as Freescale's `boot_format` tool writes for SRAM boot, and as the official card
has) set up the L2 cache as SRAM at 0xF8F80000:

| # | Address | Data | Register (CCSR at its reset base 0xFF700000) |
|---|---|---|---|
| 1 | 0xFF720100 | 0xF8F80000 | L2SRBAR0: L2 SRAM base address |
| 2 | 0xFF720E44 | 0x0000000C | L2ERRINJLO (stock Freescale value) |
| 3 | 0xFF720000 | 0x80010000 | L2CTL: L2 enabled, SRAM mode |
| 4 | 0xFF72E40C | 0x00000040 | eSDHC DCR (DMA control) |
| 5 | delay | 0x100 | wait 0x100 x 8 platform clocks |
| 6 | end | | end of configuration |

The boot ROM does no DDR or memory-window setup; the SPL does that.

**Length.** The SPL loads U-Boot from `source + 0x20000` (0x20400) for `length - 0x20000` bytes.
`mkimage.py` therefore sets the length to the size of `u-boot-with-spl.bin` (0x20000 plus the size
of `u-boot.bin`), rounded up to 512. The length in the official card's header is smaller than
this port's U-Boot, so it must not be reused: this port's SPL loads exactly `length - 0x20000`
bytes, and would then load a truncated U-Boot.

**MBR.** The official card has one placeholder partition entry: type 0x0B (FAT32), starting at
sector 7690, 7,708,672 sectors long. There is no FAT filesystem there; the entry only covers the
firmware area, so a computer that reads the card sees an empty, unformatted partition.
`mkimage.py` writes the same entry.

## The code at 0x400

The boot ROM copies the code to the L2 SRAM at 0xF8F80000 and runs it. This is the SPL. It sets up
DDR and the console, then reads the main U-Boot from card offset 0x20400 to DDR at 0x11000000 and
jumps to it. U-Boot's text starts 0x1000 into the image (0x11001000).

## Environment (0x600000)

8 KiB: a big-endian CRC32 of the remaining 0x1FFC bytes, then NUL-separated `name=value` strings
ending in an empty string. There is no redundant copy and no flag byte. This is the same format
as the X5000's.

On an A1222 the environment is read from and saved to this offset (`CONFIG_ENV_OFFSET`). An image
made with `--env` carries this port's default environment there; otherwise U-Boot uses its built-in
defaults until the first `saveenv`.

An image without `--env` ends after the U-Boot code. Written over a card that already holds a
valid environment at 0x600000 (an official card, for example), it leaves that environment in place,
and U-Boot will use it. Build the image with `--env`, or run `env default -a` and `saveenv` at the
prompt, to start from this port's defaults.
