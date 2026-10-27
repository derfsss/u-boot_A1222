# Building the A1222 firmware

This tree is mainline U-Boot v2021.04 with the AmigaOne A1222 changes on top. It builds like any
U-Boot tree: with a PowerPC cross compiler, natively on Linux or in a container. A Dockerfile is
included as one ready-made toolchain, but nothing in the build depends on it. The firmware boots
AmigaOS 4.1 on an A1222 (see [status.md](status.md)).

## What you need

A PowerPC cross toolchain and U-Boot's host tools. Any `powerpc-linux-gnu-` (or similar) GCC that
builds U-Boot v2021.04 will do; it is tested with GCC 10 (Debian bullseye) and GCC 12 (Debian
bookworm). U-Boot selects the flags for the P1022's e500v2 cores itself. On Debian or Ubuntu:

```sh
sudo apt install gcc-powerpc-linux-gnu build-essential bison flex bc libssl-dev python3 \
    device-tree-compiler git
```

GCC 12 prints more warnings than GCC 10 (array bounds, executable stack notes from the linker);
they come from upstream U-Boot v2021.04 and do not affect the firmware.

### Optional: the toolchain container

`docker-a1222/Dockerfile` is Debian bullseye with the cross compiler and the host packages, and
sets `CROSS_COMPILE=powerpc-linux-gnu-` and `ARCH=powerpc`. Bullseye is no longer on the live
Debian mirrors, so the Dockerfile points apt at `archive.debian.org`. From the root of the U-Boot
tree, build it under any name:

```sh
docker build -t a1222-uboot-builder docker-a1222/
```

## Defconfigs

| Defconfig | What it builds |
|---|---|
| `Tabor_SDCARD_defconfig` | The A1222 firmware, **debug build**: SPL plus U-Boot, booting from the SD card, with the AmigaOS support; the boot menu is shown and the AmigaOS kernel gets `debuglevel=1`. |
| `Tabor_SDCARD_PRODUCTION_defconfig` | The A1222 firmware, **production build**: the same, but boots the default entry without showing the boot menu, with `debuglevel=0` (as the official firmware). |
| `P1022DS_SDCARD_defconfig` | The Freescale P1022DS reference board the Tabor port is derived from. A build check only; it does not run on an A1222. |

Debug and production differ only in the default environment (`CONFIG_BOOTA_PRODUCTION`):

| Setting | Debug | Production |
|---|---|---|
| `amigaboot_quiet` | `n` (boot menu shown) | `y` (default entry boots, no menu) |
| `os4_commandline` | `debug munge debuglevel=1` | `debug munge debuglevel=0` |

These are only the built-in defaults. An environment saved on the card (`saveenv`, or one left
on the card by the official firmware) keeps its own values.

## Compiling

From the root of the U-Boot tree, out of tree (`O=`) so the source stays clean:

```sh
export CROSS_COMPILE=powerpc-linux-gnu- ARCH=powerpc
make O=out Tabor_SDCARD_defconfig            # or Tabor_SDCARD_PRODUCTION_defconfig
make O=out -j$(nproc)
make O=out u-boot-initial-env
```

The same in the container (the image sets `CROSS_COMPILE` itself). On a Windows host the bind
mount is slow, so keep the object tree in a named volume:

```sh
docker run --rm -v a1222-obj:/tmp/obj -v "$PWD:/src" -w /src a1222-uboot-builder bash -lc \
  'make O=/tmp/obj Tabor_SDCARD_defconfig && make O=/tmp/obj -j$(nproc) &&
   make O=/tmp/obj u-boot-initial-env &&
   mkdir -p out && cp /tmp/obj/u-boot-with-spl.bin /tmp/obj/u-boot-initial-env out/'
```

(In PowerShell, write `${PWD}` and use a backtick instead of `\` at the end of the first line.)

Outputs in the object directory:

| File | Contents |
|---|---|
| `spl/u-boot-spl.bin` | the first stage, run from the L2 cache used as SRAM |
| `u-boot.bin` | the main U-Boot (at most 768 KiB) |
| `u-boot-with-spl.bin` | the SPL padded to 128 KiB, followed by `u-boot.bin`: the code the boot ROM loads from the card |
| `u-boot-initial-env` | the default environment as text, for the image tool |

The P1022DS code produces U-Boot's driver-model migration warnings at the end of the build; they
come from the upstream board and are expected.

## Assembling an SD card image

`tools-a1222/mkimage.py` builds a card image in the official A1222 layout (see
[sd-card-layout.md](sd-card-layout.md)): the eSDHC boot header and an MBR in sector 0, the
SPL and U-Boot from offset 0x400, and the environment at 0x600000.

```sh
python3 -I tools-a1222/mkimage.py --user-code out/u-boot-with-spl.bin \
    --env out/u-boot-initial-env --out tabor-sdcard.img
```

Options:

| Option | Meaning |
|---|---|
| `--user-code` | `u-boot-with-spl.bin` from the build (required) |
| `--out` | the image to write (required) |
| `--env` | a `NAME=value` text file; written as a CRC-checked environment block at 0x600000 |
| `--data` | a boot-splash data image to place at 0x900000 (none is included) |
| `--size` | pad the image to this many bytes |

On Windows, the Python tools need `C:/...` style paths, not `/c/...`.

`tools-a1222/inspect_image.py` prints a read-only summary of any A1222 card image (boot header,
MBR, environment, version strings), which is useful for comparing an image with an official one.

Writing an image to a card replaces the card's firmware. Use a spare card and keep the original
firmware card unchanged.

## Configuration: Kconfig and driver model

- **Kconfig.** The Tabor board (`CONFIG_TARGET_TABOR`) and each option this port adds are Kconfig
  options, set in the defconfigs and visible in `make O=out menuconfig`:
  - `CONFIG_CMD_BOOTA` and `CONFIG_BOOTA_PRODUCTION`;
  - `CONFIG_FS_NGFS`, `CONFIG_FS_SFS`, `CONFIG_FS_AFFS` and `CONFIG_FS_EXFAT`;
  - `CONFIG_BIOSEMU`.
- **Board header.** The board itself is configured the older way, in `include/configs/tabor.h`,
  as the P1022DS port it comes from: the memory map, the SD card offsets, the default
  environment, and `CONFIG_SYS_*` values that have no Kconfig option in v2021.04.
- **No driver model.** `CONFIG_DM` is off. Serial, PCI, USB, SATA, Ethernet, I2C, the SD card and
  the screen console use U-Boot's legacy (non-driver-model) drivers, and U-Boot runs without a
  control device tree of its own. The small device tree that `boota` hands to AmigaOS is built at
  boot time. The driver-model migration warnings at the end of the build are about this.

This is why the port is on v2021.04. Mainline removed the P1022DS board in v2020.10, when it
missed the driver-model migration deadlines, and the rest of the P1022 support after v2021.04.
Moving to a newer U-Boot means converting the board to driver model first.

## Developer tools

`tools-a1222/aos4-probe` is built separately with its own `Makefile`
(`make -C tools-a1222/aos4-probe`, with `CROSS_COMPILE` set as above). See its `README.md`.
