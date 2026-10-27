# A1222 port: status

This is a port of U-Boot v2021.04 to the AmigaOne A1222 ("Tabor" board, NXP QorIQ P1022). **It
boots AmigaOS 4.1 on an A1222.**

## Tested on an A1222

- The firmware starts from an SD card image made with `tools-a1222/mkimage.py`: the first stage
  sets up DDR and loads the main U-Boot, which relocates and reaches the autoboot countdown.
- `boota` finds the AmigaOS partitions, shows the boot menu and boots AmigaOS 4.1.
- The USB keyboard works in the boot menu and at the prompt, including after `usb reset`
  (`run massboot`), with console multiplexing on.

Everything else below builds and is part of the firmware that was tested, but has not been
checked on its own yet.

## What it is built from

| Layer | Source |
|---|---|
| U-Boot | mainline v2021.04, the last release with P1022 SoC support (`ARCH_P1022`, `p1022_serdes.c`) |
| Base board | the Freescale P1022DS reference board, restored from mainline v2020.07 (it was removed in v2020.10) and kept in `board/freescale/p1022ds` as a build baseline |
| Tabor board | `board/aeon/tabor`, `include/configs/tabor.h`, `configs/Tabor_SDCARD_defconfig`: derived from the P1022DS port, changed to match what the official A1222 firmware, the Tabor Linux kernel and the Tabor Technical Reference Manual show |
| AmigaOS support | `boota`, the AmigaOS filesystems and exFAT, the same code as in the X5000 port; see [booting-amigaos.md](booting-amigaos.md) |

There is no Tabor board in mainline U-Boot at any version, and the official firmware's source is
not public, so the board code is written from the facts in [hardware.md](hardware.md).

## Done

**Board**

- DDR: SPD-driven (SPD EEPROM on I2C bus 1 at 0x51), four chip selects, the same board timing
  table as the P1022DS.
- Fixed 66.66 MHz system and DDR clocks (no PIXIS FPGA, no ICS307 clock chip).
- The official firmware's TLB map, the one static LAW (Main CPLD) and the eLBC chip-select 0 setup.
- The official pin multiplexing (PMUXCR, PMUXCR2), block disables (DEVDISR: PCIe controllers 2 and 3,
  the security engine and the second USB controller off) and the supervisor-MCU GPIO lines parked.
- Ethernet: two eTSEC ports with Micrel KSZ9021 PHYs at addresses 3 and 7, with the official
  RGMII pad-skew setup.
- MAC addresses read from the board EEPROMs when the environment has none; the environment is
  then saved before booting AmigaOS, as the official firmware does.
- The TFP410 DVI encoder is enabled (there is no DIU framebuffer driver; see below).
- Commands: `shutdown` and `mcu` (the supervisor MCU on UART1), `restart`.
- SD boot in the official card layout, and an image tool (`tools-a1222/mkimage.py`); see
  [sd-card-layout.md](sd-card-layout.md).

**Shared with the X5000 port**

- `boota`: U-Boot's own AmigaOS 4 loader and boot menu. See
  [booting-amigaos.md](booting-amigaos.md).
- Filesystems: NGFS, SFS (including SFS\2), FFS, and read-only exFAT; the Amiga RDB partition
  table with boot priorities.
- Graphics: a PCIe Radeon's VBIOS POSTed in U-Boot's x86 emulator, for the screen console
  (`board/aeon/tabor/video.c`). The VGA jumper JP2 is honoured.
- A black boot screen with a progress bar for the autoboot countdown.
- The USB keyboard and EHCI driver changes, including the official firmware's extra EHCI delay
  for Tabor.

## Not done

- **Checking each board feature on its own**: Ethernet, the MCU commands, the TFP410 DVI
  encoder, the screen console on the A1222.
- **The on-chip display (DIU)**: the official firmware falls back to the DIU (800x600, through the
  TFP410 to the DVI-D connector) when no graphics card is fitted. Not implemented; U-Boot needs a
  PCIe graphics card for a screen.
- **The official graphical boot menu** (`amigabootmenu`, with its animation). The A1222 uses the
  same text menu as the X5000 port.
- **`fwupdate`**: X5000 only for now (see [booting-amigaos.md](booting-amigaos.md)).
- The NXID board EEPROM and the `mac` command.
- The WM8776 audio codec clock.

## Not yet verified

- Whether the DDR settings computed from the SPD match the official firmware's. (DDR comes up with
  them and AmigaOS boots; the register values have not been compared.)
- The real clock ratios (the board's POR straps), and therefore the real DDR clock.
- The SerDes slot names, which are the P1022DS's.
- Ethernet, and USB storage.
- The screen console on a PCIe graphics card, and the VGA jumper.
- Whether the official firmware hands the AmigaOS kernel anything that `boota` does not (see
  [booting-amigaos.md](booting-amigaos.md#tested-on-the-a1222)).

See [hardware.md](hardware.md#not-yet-verified) for the hardware details that are still open.

## Building

See [building.md](building.md).
