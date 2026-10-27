# U-Boot for the AmigaOne A1222

> [!WARNING]
> **Only install this firmware if you know what you are doing. You use it at your own risk.**
>
> This replaces your A1222's boot firmware (its "BIOS"). A wrong image, a faulty card, or a write
> that is interrupted can leave the machine unable to start until a working firmware card is
> fitted.
>
> - **Back up first.** Before you install anything, make a raw image of your current firmware SD
>   card (the whole card) and keep the original card unchanged in a safe place. Have a spare card,
>   a known-good firmware image and an SD card writer to hand, so you can always go back.
> - **Your settings are replaced.** Writing an image replaces the settings saved on the card
>   (there is no `fwupdate` on the A1222 yet). Note any settings you have changed first.
> - **Partly tested.** It boots AmigaOS 4.1 on an A1222, but some board features (Ethernet, USB
>   storage, the MCU commands, the screen console) have not been checked one by one yet; see
>   [docs/status.md](docs/status.md).
> - **Recovering.** The firmware lives only on the SD card; this firmware changes nothing else on
>   the computer. If the machine does not start, switch off and fit your original (or backup) card.
>   A serial console (115200 8N1) shows how far a failed start got.
> - **Beta firmware.** This is a v0.9 beta, in development. It is not the official A1222 firmware,
>   and it is not made, supported or endorsed by A-Eon Technology or the makers of the A1222. Do not
>   ask them for help with it.
> - **No warranty.** It is provided "as is", without warranty of any kind; see sections 11 and 12 of
>   the [licence](LICENSE).

Open-source boot firmware for the **AmigaOne A1222**, aimed at booting **AmigaOS 4.1**. It is
mainline [U-Boot](https://www.denx.de/wiki/U-Boot) v2021.04 with support for the A1222 added: its
own AmigaOS loader and boot menu, the Amiga filesystems and a Radeon screen console. All of it is
source code under the GPL. It is the sister project of
[u-boot_X5000](https://github.com/derfsss/u-boot_X5000), the U-Boot port for the AmigaOne X5000,
and shares its AmigaOS support.

**Status:** in development. It boots AmigaOS 4.1 on an A1222. Keep your original firmware card,
so you can always go back to it.

## Machines

| Machine | CPU | Status |
|---|---|---|
| AmigaOne A1222 | NXP QorIQ P1022, two e500v2 cores | Tested; boots AmigaOS 4.1 |

The A1222 mainboard is the A-Eon "Tabor", which is the board name used in the source.

## Features

Tested on an A1222: the firmware starts from the SD card and `boota` boots AmigaOS 4.1, with a
USB keyboard working in the boot menu and at the prompt. The AmigaOS support is the same code as
in the X5000 port. Some board features have not been checked one by one yet; see
[docs/status.md](docs/status.md).

- **Boots AmigaOS 4.1 by itself.** `boota` reads the AmigaOS partitions (NGFS, SFS including
  SFS\2, and FFS), lists every configuration in each partition's `Kickstart/Kicklayout` in a boot
  menu, loads the kickstart and starts AmigaOS. No A-Eon software is involved.
- **The A1222 board:** memory set up from the module's SPD, the official memory map and pin
  set-up, both Ethernet ports, MAC addresses from the board EEPROM, and SD card boot in the
  official card layout.
- **A quiet start.** A black screen with a progress bar for the autoboot countdown, then the boot
  menu, then AmigaOS. Press a key during the countdown to get the U-Boot prompt instead.
- **Graphics** on a PCIe Radeon card (the card's video BIOS is run in U-Boot's x86 emulator).
  The VGA jumper JP2 is honoured.
- **USB** keyboard and storage, **SATA**, **Ethernet**.
- **Ready for multicore AmigaOS.** The second core is parked in U-Boot's spin table and passed to
  the kernel in the way a multicore kernel expects.
- **Supervisor MCU commands:** `shutdown`, `restart`, and `mcu` for temperatures, voltages and the
  fan speed.

Not done yet:

- the on-chip display: without a PCIe graphics card there is no screen, only the serial console;
- the official firmware's graphical boot menu (this port has a text menu);
- firmware updates from U-Boot (`fwupdate`, which the X5000 port has).

The full list, with what is still unverified, is in
[docs/status.md](docs/status.md).

## Installing

You need a spare SD card and an SD card writer.

1. Download an image from the [Releases](https://github.com/derfsss/u-boot_A1222/releases) page,
   or build one (see [Building](#building) below). The production image boots straight into
   AmigaOS; the debug image shows the boot menu and a more detailed AmigaOS serial log.
2. Write the image raw to the whole card (for example with `dd`, or any raw image writer).
3. Put the original firmware card aside, and fit the new card.

Putting the original card back restores the machine as it was; this firmware changes nothing else
on the computer. A serial console (115200 8N1) shows the start-up messages if something goes
wrong. The card layout is in
[docs/sd-card-layout.md](docs/sd-card-layout.md).

## Updating

There is no `fwupdate` on the A1222 yet. To install a newer build, write its whole image to the
card with an SD card writer, as when installing. This also replaces the settings saved on the
card with the image's defaults.

## Using the firmware

After power-on the autoboot countdown runs for 3 seconds, then `boota` shows the boot menu:

- `1`-`9` or `A`-`Z` boots an entry; UP/DOWN and RETURN boot the marked one.
- SPACE stops the menu's countdown.
- Ctrl-C leaves the menu for the U-Boot prompt.

Press any key during the autoboot countdown to get the U-Boot prompt instead. There, `help` lists
the commands and `boota` shows the menu again.

The commands this port adds (`boota`, `videoboot`, `mcu`, `shutdown`, `restart`), the standard
commands it changes, and the full command list are described in
[docs/commands.md](docs/commands.md).

## Settings

At the prompt, `printenv name` shows a setting; `setenv name value` followed by `saveenv` changes
it.

| Setting | Default | Meaning |
|---|---|---|
| `bootdelay` | `3` | Seconds of autoboot countdown. The progress bar fills over this time. |
| `bootcmd` | `boota` | What runs when the countdown ends. |
| `boota_timeout` | not set (5) | Seconds the boot menu waits before booting the marked entry; SPACE stops it, `0` waits. |
| `amigaboot_quiet` | `n` (debug build), `y` (production build) | `y` skips the menu and boots the default entry. |
| `bootable_only` | `y` | `y` lists only partitions marked bootable; `n` lists every readable partition. |
| `os4_bootdevice` | `auto` | Where to look for AmigaOS: `auto` (SATA, then USB), or e.g. `usb,sata`. |
| `os4_commandline` | `debug munge debuglevel=1` (debug), `debug munge debuglevel=0` (production) | Options passed to the AmigaOS kernel. |
| `stdout` | `serial` | Start-up messages go to the serial port only and the screen stays black. `serial,vga` shows them on the screen too. |
| `vesamode` | not set | Force one VESA mode for the screen (hex, e.g. `114`). |
| `vga_jumper` | not set | `ignore` uses the graphics card even when jumper JP2 is not fitted. |

## Building

You need Linux (or WSL, or a container), a PowerPC cross compiler and the usual U-Boot build
tools. On Debian or Ubuntu:

```sh
sudo apt install gcc-powerpc-linux-gnu build-essential bison flex bc libssl-dev python3 \
    device-tree-compiler git
```

(Tested with GCC 10 and GCC 12. `docker-a1222/Dockerfile` is a ready-made container with all of
this; see [docs/building.md](docs/building.md).)

Then build the firmware and the SD card image:

```sh
git clone https://github.com/derfsss/u-boot_A1222.git
cd u-boot_A1222
export CROSS_COMPILE=powerpc-linux-gnu- ARCH=powerpc
make O=out Tabor_SDCARD_defconfig && make O=out -j$(nproc)
make O=out u-boot-initial-env
python3 -I tools-a1222/mkimage.py --user-code out/u-boot-with-spl.bin \
    --env out/u-boot-initial-env --out tabor-sdcard.img
```

There are two builds:

| Defconfig | Build |
|---|---|
| `Tabor_SDCARD_defconfig` | **Debug**: shows the boot menu; `debuglevel=1` for the AmigaOS kernel's serial log. |
| `Tabor_SDCARD_PRODUCTION_defconfig` | **Production**: boots the default entry without the menu; `debuglevel=0`. |

This writes `tabor-sdcard.img`; install it as described in [Installing](#installing). For the
production build, replace `Tabor_SDCARD_defconfig` with `Tabor_SDCARD_PRODUCTION_defconfig`. No file from outside
this repository is needed: `tools-a1222/mkimage.py` turns the build into an SD card image in the
official card layout. The details and the image tool's options are in
[docs/building.md](docs/building.md).

Developer tools, not needed for a normal build:

- `tools-a1222/inspect_image.py` and `tools-a1222/parse_env.py` print what is on an A1222 card
  image, and its saved settings.
- `tools-a1222/aos4-probe` is a stand-in for the AmigaOS kernel loader that prints what a loader
  hands over to the kernel.

## Source layout

```
(root)                      mainline U-Boot v2021.04 with the A1222 changes
board/aeon/tabor/           the A1222 board: DDR, TLBs, video, MCU commands, first stage
board/freescale/p1022ds/    the P1022DS reference board the port is based on
                            (restored from U-Boot v2020.07)
cmd/boota.c, common/aos4/   the AmigaOS loader and boot menu
fs/ngfs/, fs/sfs/, fs/affs/ NGFS, SFS and FFS readers
docs/                       documentation for this port
tools-a1222/                SD card image tools and the aos4-probe tool
docker-a1222/               the build container
```

Changes to upstream U-Boot files are marked with comments starting `AmigaOne A1222:` or
`AmigaOne:`, so `grep -rn "AmigaOne" .` finds them.

## Documentation

| Document | Contents |
|---|---|
| [docs/commands.md](docs/commands.md) | The commands this port adds and changes, and all other commands |
| [docs/booting-amigaos.md](docs/booting-amigaos.md) | `boota`, the boot menu and settings, the kernel handoff, SMP |
| [docs/status.md](docs/status.md) | What is done, what is tested, what is not done yet |
| [docs/building.md](docs/building.md) | Building the firmware and the SD card image |
| [docs/sd-card-layout.md](docs/sd-card-layout.md) | What is where on the firmware card |
| [docs/hardware.md](docs/hardware.md) | A1222 board details for developers: CPU, memory, memory map, pins, I2C, MCU, PCIe, video |

## Licences and third-party components

U-Boot is free software under the GNU General Public License version 2 ([`LICENSE`](LICENSE));
most files also allow any later version. Each source file states its licence in an
`SPDX-License-Identifier` line, and [`Licenses/`](Licenses/) holds the full texts
([`Licenses/README`](Licenses/README) lists them). This port's additions are GPL-2.0-or-later.

The source and the firmware image contain no proprietary code or binaries. A-Eon's firmware and
AmigaBoot are not included; `.gitignore` lists what must never be committed.

`fs/exfat/` is mainline U-Boot's exFAT driver (libexfat, Copyright Andrew Nayenko,
GPL-2.0-or-later), backported read-only.

AmigaOS 4.1 is not part of this firmware; it boots the copy installed on your disks.

## Acknowledgments

- The U-Boot project, and Freescale (now NXP), authors of the P1022DS reference board port this
  builds on.
- ACube's Sam440ep/Sam460ex U-Boot (Andrea Vallinotto), a reference for the SFS driver and
  the boot menu input handling.
- BBoot by Zoltan Balaton ([codeberg.org/qmiga/bboot](https://codeberg.org/qmiga/bboot), GPL),
  an open AmigaOS 4 loader whose kernel call this firmware's loader also uses.

## Development

This project's additions were built with help from [ClaudeAI](https://claude.ai) (Anthropic).
