# Booting AmigaOS 4 on the A1222

U-Boot boots AmigaOS 4 itself with the `boota` command: it finds the AmigaOS volumes on the
attached disks, shows a boot menu, loads the chosen kickstart and starts the kernel. The official
A1222 firmware does this by starting A-Eon's AmigaBoot loader from the SD card; this port does not
use AmigaBoot, and there is no command to start it. `boota` is the default boot command
(`bootcmd=boota`).

It boots AmigaOS 4.1 on the A1222, and is the same code that boots AmigaOS 4.1 on the AmigaOne
X5000 in the sister X5000 port.

The code is in `cmd/boota.c` and `common/aos4/`; the AmigaOS filesystems are in `fs/ngfs/`,
`fs/sfs/` and `fs/affs/`.

## What `boota` does

1. If a MAC address was read from the board EEPROM at start-up (the environment had none), saves
   the environment first, as the official firmware does.
2. Starts USB (`usb start`) and SATA (`sata init`).
3. Scans the devices named by `os4_bootdevice` (`auto`: SATA, then USB) for Amiga RDB partitions
   with a filesystem U-Boot can read: NGFS (`NGF\0`, `NGF\1`), SFS (`SFS\0`, `SFS\2`) and
   FFS/OFS (`DOS\x`).
4. Reads `Kickstart/Kicklayout` from each such partition. Every `LABEL` in it becomes a menu
   entry: the partition's RDB drive name (e.g. `DH0`) and the label.
5. Shows the menu, or with `amigaboot_quiet=y` picks the default entry straight away.
6. Loads the entry's `EXEC` (the kernel loader, normally `Kickstart/loader`) and its `MODULE`
   files into memory, builds the module list, board information and device tree described below,
   and enters the kernel loader.

The default entry is the first configuration on the partition with the highest RDB boot priority.

## The boot menu

The debug build (`Tabor_SDCARD_defconfig`) shows the menu; the production build
(`Tabor_SDCARD_PRODUCTION_defconfig`) boots the default entry without it (`amigaboot_quiet=y`, as
in the official environment). Set `amigaboot_quiet` and `saveenv` to change it.

| Key | Action |
|---|---|
| `1`-`9`, `A`-`Z` | boot that entry |
| UP / DOWN | move the selection |
| RETURN | boot the selected entry |
| SPACE | stop the countdown |
| Ctrl-C | leave the menu for the U-Boot prompt |

When the countdown (`boota_timeout`) runs out, the selected entry boots. While the kickstart
loads, a progress bar shows each file; then the screen is cleared and the kernel starts. If a
`MODULE` cannot be read, `boota` asks whether to boot without it (C) or abort (A). A missing kernel
loader always aborts.

From the prompt:

```
boota          show the menu
boota 2        boot entry 2 without the menu (1-9, A-Z)
boota -n       load the kickstart but do not start it; print the module list,
               the board information, the device tree and a CRC32 per file
```

### Console

The A1222 build has U-Boot's console multiplexing (`CONFIG_CONSOLE_MUX`), as the X5000 build
does: the console can use several devices at once. `boota` sets `stdin` to `serial,usbkbd`, so the
menu can be driven from the serial console or a USB keyboard. While the menu and the kickstart
loading are shown, the output goes to the serial console and the screen; afterwards it returns to
the previous setting (by default `serial`).

The official firmware's release notes report that console multiplexing upset `usb reset` with a
USB keyboard. On this firmware `usb reset` and `run massboot` work with the USB keyboard as input
(tested on an A1222).

U-Boot needs a PCIe graphics card for its screen (see [hardware.md](hardware.md#video)). Without
one, the menu and messages are on the serial console only.

## Settings

The defaults follow the official A1222 firmware's environment where it has the same variables.
The debug and production builds differ only in `amigaboot_quiet` and `os4_commandline` (see
[building.md](building.md)).

| Variable | A1222 default | Meaning |
|---|---|---|
| `bootcmd` | `boota` | what runs when the autoboot countdown ends |
| `bootdelay` | `3` | autoboot countdown, seconds |
| `amigaboot_quiet` | `n` (debug build), `y` (production build) | `y`: no menu, boot the default entry; `n`: show the menu. The name is the official firmware's, so existing environments keep working. |
| `bootable_only` | `y` | `y`: list only partitions marked bootable in the RDB |
| `boota_timeout` | not set (5) | menu countdown in seconds; `0` waits for a key |
| `os4_bootdevice` | `auto` | `auto` (SATA, then USB), or interface names such as `usb,sata` |
| `os4_commandline` | `debug munge debuglevel=1` (debug), `debug munge debuglevel=0` (production) | kernel command line |
| `harddiskboot` | `sata init;boota` | as in the official environment |
| `massboot` | `usb reset;boota` | as in the official environment |
| `stdin`, `stdout` | `serial,usbkbd`, `serial` | start-up messages on the serial port only; the screen stays black with the countdown progress bar |

Change a setting with `setenv name value` and keep it with `saveenv`.

## The kernel handoff

This section is for developers. It describes what the AmigaOS kernel loader receives from `boota`.
It is the interface used by A-Eon's AmigaBoot and by BBoot, as measured on an X5000/20. The
A1222's AmigaOS kernel boots with it (see the end of this page).

### Memory

The kickstart goes in a 64 MiB area that ends 1 MiB below U-Boot's global data (just under U-Boot
in RAM). In order, from the bottom of the area:

1. the module list header;
2. each file in Kicklayout order (the EXEC first), each starting on a 32-byte boundary, followed
   directly by its 36-byte module node (no alignment) and its name string;
3. the board information (4-byte aligned), then the device tree (8-byte aligned, 8 KiB reserved);
4. the strings `"AmigaOS4"` and the command line.

The kernel loader copies itself to 0x01000000-0x010078A4 and runs there, so that range must be free
when it starts; the kickstart area is far above it.

### Entry

The EXEC is a flat, position-independent binary entered at its first byte:

| Register | Value |
|---|---|
| r3 | pointer to the string `"AmigaOS4"` |
| r4 | module list |
| r5 | board information |
| r6 | command line (`os4_commandline`) |
| r1 | U-Boot's stack |

The CPU is left as U-Boot runs it: MMU on with U-Boot's TLB1 map (see
[hardware.md](hardware.md)), U-Boot's exception vectors, caches on. The decrementer interrupt is
switched off (TCR[DIE] cleared) before the call, so U-Boot's timer tick cannot fire into the
kernel loader.

### Module list

An Exec `MinList` (`head`, `tail` = 0, `tailpred`) of 36-byte nodes, all fields big-endian:

| Offset | Field |
|---|---|
| +0 / +4 | `succ` / `pred` |
| +8 | name: pointer to the file's base name, e.g. `"kernel"` |
| +12 | options, 0 |
| +16 | u16 id: 1 = EXEC (the first node), 2 = MODULE |
| +18 | u16 flags: 3 |
| +20 / +24 | alloc / dealloc: 0 here (the kernel does not use them) |
| +28 | address of the file data |
| +32 | size of the file in bytes |

There is no device-tree module; the device tree is passed through the board information.

### Board information (r5)

A 72-byte structure: the U-Boot `bd_t` layout followed by an extended tail.

| Offset | Value |
|---|---|
| +0 | memory start, 0 |
| +4 | memory size passed to the kernel (see "Second core" below; at most 2 GiB) |
| +44 | CPU clock, Hz |
| +48 | bus clock, Hz |
| +52 | serial baud rate |
| +60 | tail magic `0x12345678` |
| +64 | u16 tail version, 0 |
| +66 | u16 structure size, 72 |
| +68 | pointer to the device tree |

All other words are 0.

### Device tree

A small flattened device tree (version 17), placed after the board information:

```
/ {
    cpus {
        #address-cells = <1>;
        #size-cells = <0>;
        cpu@0 {
            device_type = "cpu";
            reg = <0>;
            enable-method = "spin-table";
            cpu-release-addr = /bits/ 64 <spin_table + 0x00>;
        };
        cpu@1 { ... reg = <1>; cpu-release-addr = /bits/ 64 <spin_table + 0x40>; };
    };
    memory {
        device_type = "memory";
        reg = <0 memstart 0 memsize>;
    };
};
```

There is one `cpu@N` node per core: two on the P1022.

### Second core (SMP)

U-Boot parks the second core in its spin table (the ePAPR spin-table protocol of the mpc85xx
code), with one 64-byte entry per core. `cpu-release-addr` of `cpu@N` is the address of entry N, so a
multicore kernel can start the other core by writing an entry address there. The Tabor TLB table
maps the reset page (0xFFFFF000), so U-Boot can install the secondary core's boot page without any
board change.

When a core is parked, the memory size given to the kernel ends below U-Boot (at U-Boot's
relocation address, rounded down to 1 MiB), so the spin loop is never overwritten.

## Not available on the A1222

- **`fwupdate`.** The X5000 port's `fwupdate` writes the X5000 SD card layout, so it is not built
  for the A1222. An A1222 version needs this board's layout ([sd-card-layout.md](sd-card-layout.md)):
  the boot header and MBR in sector 0, the SPL and U-Boot from 0x400, the environment at 0x600000.
  Until then, write a whole image with an SD card writer.
- **A graphical boot menu.** The official firmware's `amigabootmenu` (with its animation) is not
  reproduced; `boota` has a text menu.

## Known issue: exFAT USB sticks

U-Boot can read exFAT (mainline U-Boot's driver, read-only), but on some USB sticks exFAT reads
stop after a few dozen USB transfers and the command hangs (seen on the X5000). The same stick
formatted FAT32 works. Use FAT32 for USB sticks.

## Tested on the A1222

`boota` boots AmigaOS 4.1 on an A1222 with the handoff described above, the same as on the X5000,
so the A1222's kernel accepts the X5000's board information layout.

Not compared yet: whether the official firmware hands the kernel anything more. `tools-a1222/aos4-probe`
can check this. Run as the EXEC of a test Kicklayout entry, it prints everything a loader hands
the kernel. Running it once under the official A1222 firmware and once under `boota`, and
comparing the two outputs, shows any difference. See
[../tools-a1222/aos4-probe/README.md](../tools-a1222/aos4-probe/README.md).

## Other commands

- `shutdown` powers the machine off through the supervisor MCU.
- `restart` restarts the machine (the same as `reset`).
- `mcu t|v|f` reads temperatures, voltages and the fan speed from the MCU.

All the commands are described in [commands.md](commands.md).
