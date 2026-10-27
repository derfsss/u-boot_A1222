# Tabor (A1222) hardware for U-Boot developers

Facts about the AmigaOne A1222 mainboard ("Tabor", Varisys/A-Eon) that the U-Boot port depends on.
They come from the public Tabor Technical Reference Manual (TRM 1.0.3), the Tabor Linux kernel
(platform code and device trees), NXP's P1022 Reference Manual and data sheet, and the behaviour of
the official A1222 firmware. Register names are the P1022 Reference Manual's. CCSR offsets are
relative to CCSR at 0xF_FFE00000 (the reset base is 0xFF700000).

The board is a Varisys design derived from Freescale's P1022RDK reference design. Mainline U-Boot
never had a P1022RDK board, so this port starts from the related P1022DS board instead. The
P1022DS's PIXIS FPGA, ICS307 clock chip, NOR and NAND flash do not exist on Tabor.

## CPU and clocks

- NXP QorIQ P1022: two e500v2 cores (Power ISA 2.03 with SPE), 256 KiB L2 cache, 36-bit physical
  addressing (`CONFIG_PHYS_64BIT`).
- Fixed 66.66 MHz oscillator for both the system and the DDR clock. The port uses 66,660,000 Hz
  for both, and 66,666,000 Hz for the first stage's serial console divisor, the same values as the
  official firmware.
- Clocks derive in the usual way: platform (CCB) = sysclk x PLAT_RATIO, cores from the CCB, DDR =
  DDR clock x ratio, local bus = CCB / 8. The ratios come from the POR straps (PORPLLSR).
- The board does not program the PCIe clock generator (I2C 0x6E) or any other clock output.

## Memory (DDR)

- One DDR3 SO-DIMM socket: standard 1.5 V, unbuffered, non-ECC. The controller supports up to
  DDR3-800 (800 MT/s). The TRM reports testing with 2 GB Kingston modules.
- DDR is SPD-driven: the SPD EEPROM is on I2C bus 1 at address 0x51 (256 bytes). All timings are
  computed from it at boot.
- Four chip selects per controller (`CONFIG_CHIP_SELECTS_PER_CTRL 4`), one DIMM slot.
- Board timing table (identical to the P1022DS's `board/freescale/p1022ds/ddr.c`):

  | Ranks | DDR clock up to | clk_adjust | cpo | write_data_delay | 2T |
  |---|---|---|---|---|---|
  | 1 | 549 MHz | 5 | 31 (automatic) | 3 | off |
  | 1 | 850 MHz | 5 | 31 | 5 | off |
  | 2 | 549 MHz | 5 | 31 | 3 | off |
  | 2 | 850 MHz | 5 | 31 | 5 | off |

- Other options, as the P1022DS and the official firmware: ZQ calibration on, half-strength
  drivers, self-refresh counter SR_CNTR = 0x000B0000, no interleaving. ODT: off for reads, on for
  the written chip select on all four; RTT_Nom 40 ohm on CS0. Write leveling is enabled (the DDR3
  default of the Freescale DDR driver). ECC is not used.
- Only the SPL programs the DDR controller (CCSR 0x2000). U-Boot reads the SPD again only for the
  size.
- At most 2 GiB of RAM is mapped by the TLB (two 1 GiB entries). In the official firmware the DDR
  local access window and the PCIe inbound windows still cover all of the installed RAM; only the
  TLB mapping is limited.

## Memory map

### TLB1 (main U-Boot)

| Entry | Virtual | Physical | Size | Use |
|---|---|---|---|---|
| 0 | 0xFFFFF000 | 0x0_FFFFF000 | 4 KiB | boot page (secondary core start) |
| 1 | 0xFFE00000 | 0xF_FFE00000 | 1 MiB | CCSR |
| 2 | 0xE0000000 | 0xF_E0000000 | 64 KiB | Main CPLD (eLBC CS0), cache-inhibited and guarded, supervisor read/write |
| 3 | 0x80000000 | 0xC_00000000 | 1 GiB | PCIe memory: controllers 3 and 2 |
| 4 | 0xC0000000 | 0xC_40000000 | 256 MiB | PCIe controller 1 memory, first half |
| 5 | 0xD0000000 | 0xC_50000000 | 256 MiB | PCIe controller 1 memory, second half |
| 6 | 0xFFC00000 | 0xF_FFC00000 | 256 KiB | PCIe I/O (three 64 KiB windows) |
| 8 | 0x00000000 | 0x0_00000000 | 1 GiB | DDR |
| 9 | 0x40000000 | 0x0_40000000 | 1 GiB | DDR |

The SPL maps the boot page, CCSR at its reset address (0xFF700000) and the L2 SRAM at 0xF8F80000
(256 KiB), where it runs. This is the same TLB layout as the official firmware.

### Local access windows

There is one static LAW: 64 KiB at 0xF_E0000000, target local bus (eLBC), for the Main CPLD. The
DDR and PCIe windows are created at run time.

### PCIe windows

U-Boot names the controllers by their CCSR block: PCIe1 at 0xA000, PCIe2 at 0x9000, PCIe3 at
0xB000.

| Controller | Memory (physical) | Bus address | Virtual | I/O (physical) |
|---|---|---|---|---|
| PCIe1 | 0xC_40000000, 512 MiB | 0xE0000000 | 0xC0000000 | 0xF_FFC20000, 64 KiB |
| PCIe2 | 0xC_20000000, 512 MiB | 0xE0000000 | 0xA0000000 | 0xF_FFC10000, 64 KiB |
| PCIe3 | 0xC_00000000, 512 MiB | 0xE0000000 | 0x80000000 | 0xF_FFC00000, 64 KiB |

Only PCIe1 is in use: it is wired to the PCIe slot, and the official firmware disables PCIe2 and
PCIe3 (see DEVDISR below).

## SerDes

- SerDes 1, lanes 0-3: the PCIe slot (a x16 connector wired x4), on PCIe1.
- SerDes 2: SATA 1 and SATA 2.

## Pin multiplexing and block disables

Values written by `board_early_init_f()`, as in the official firmware (GUTS at CCSR 0xE0000):

| Register | Value | Meaning |
|---|---|---|
| PMUXCR (GUTS+0x60) | 0x40011030 | eLBC/DIU pins = DIU; USB1 pins = USB; SPI with 4 chip selects; eSDHC data pads on; UART0 2-wire + SSI Rx (no I2C1 on these pins); UART1 2-wire + I2C2; SSI TX; IRQ[0:5] as interrupts; IRQ_DEBUG1/2 as GPIO |
| PMUXCR2 (GUTS+0x64) | 0xF7000000 | MCP0, MCP1, UDE0, UDE1, PWROK, TRIG and READY pins as GPIO_3[14:17,19:22]; IRQ_OUT stays IRQ_OUT; both eTSEC ports as RGMII |
| DEVDISR (GUTS+0x70) | 0x07400000 | disables PCIe2, PCIe3, the security engine (SEC) and the second USB controller |

The SPL sets two PMUXCR fields first: UART1 2-wire + I2C2, and the eSDHC data pads.

Official firmware releases differ in one bit of DEVDISR (the security engine enabled or
disabled); this port disables it, as the current official release does.

## GPIO

Three banks at CCSR 0xF000, 0xF100, 0xF200 (GPDIR, GPODR, GPDAT at +0, +4, +8).

| Pin | Function |
|---|---|
| GPIO3_15 | HARD_RESET# to the supervisor MCU (open drain, pulled up) |
| GPIO3_16 | POWER_OFF# to the supervisor MCU (open drain, pulled up) |
| GPIO2_31 | VGA_BIOS_ENABLE#, jumper JP2: fitted (0) = use the VGA card; not fitted (1) = serial only |
| GPIO3_9 | jumper 1 |
| GPIO3_10, GPIO3_11 | CPU and HDD LEDs |
| GPIO3_12..14 | GPIO CPLD PROGRAM, INIT, DONE |

`board_early_init_f()` drives GPIO3_15 and GPIO3_16 high (inactive) as open-drain outputs. Linux
resets the board by clearing 0x00010000 and powers it off by clearing 0x10008000 in GPIO3's data
register (CCSR 0xF208).

## I2C

The TRM lists one I2C bus with all the board's devices: the controller at CCSR 0x3100 (I2C2),
U-Boot's bus 1. PMUXCR gives the pins of the first controller (0x3000) to UART0.

| Address | Device |
|---|---|
| 0x1A | WM8776 audio codec |
| 0x38 | TI TFP410 DVI encoder (DIU output to the DVI-D connector) |
| 0x50 | board ID EEPROM (Freescale NXID format) and MAC address 2 |
| 0x51 | DDR3 SPD |
| 0x53 | system configuration EEPROM |
| 0x57 | MAC address 1 |
| 0x5B | unique ID |
| 0x6E | PCIe clock generator |
| 0x6F | MCP7941x RTC and its SRAM |

### MAC addresses

| Variable | EEPROM | Offset | Length |
|---|---|---|---|
| `ethaddr` | 0x57 | 0xF2 | 6 bytes (1-byte register address) |
| `eth1addr` | 0x50 | 0xFA | 6 bytes |

A MAC address is read only when the variable is not already set; the environment wins. When one
is read, the environment is saved before AmigaOS is booted, as the official firmware does.

The NXID record at 0x50 (70 bytes from offset 0: serial number, errata, build date, version, CRC32
at 0x42) is read by the official firmware but not by this port.

## Ethernet

- Two eTSEC2 controllers (CCSR 0xB0000, 0xB1000), RGMII with internal delay, MDIO at 0x24000.
- PHYs: Micrel KSZ9021RN at MDIO addresses 3 and 7 (TBI PHY at 2).
- Before the eTSECs start, each PHY's RGMII pad skew is set through the extended registers
  (register 11 = address, register 12 = data): 0x104 = 0xF0F0, 0x105 = 0, 0x106 = 0; then BMCR =
  0x1340.

## Supervisor MCU

- On UART1 (CCSR 0x4600) at 38400 8N1. Commands are `#<c>\n`, replies `$<c>...\n`:
  - `s`: power off;
  - `t`: three temperatures (PCB, CPU, PCB 2) as signed hex degrees C;
  - `v`: eleven supply voltages (`XXYY`: XX volts, YY x 10 mV);
  - `f`: CPU fan, PWM and RPM.
- The power button gives a 1 ms low pulse on IRQ4#.
- The reset and power-off lines are GPIO3_15 and GPIO3_16 (above).

The port's `mcu t|v|f|s` command talks to it; `shutdown` sends `#s` and, if the board is still
running, clears the power-off GPIO lines.

## Main CPLD

- On the local bus, eLBC chip select 0: BR0 = 0xE0000801 (8-bit port, GPCM, valid), OR0 =
  0xFFF00010 (1 MiB mask, one wait state). No other chip select is used.
- LCRR = 0x80010004 (DBYP, EADC 1, CLKDIV /8); `board_early_init_r()` also writes LBCR = 0, as
  the official firmware does.
- An 8-bit multiplexed index/data interface at 0xE0000000. Its registers (TRM section 6) include
  signature bytes 0xDE, 0xAD, hardware and CPLD revisions, a BCD build date and time, fan
  tachometers (CPU and two case fans), the Ethernet PHY count, a scratch register, and a mailbox
  with 8 KiB of dual-port RAM shared with the GPIO CPLD (a Lattice LCMXO2-640, reprogrammable over
  SPI through GPIO3_12..14).
- Neither the official firmware nor this port accesses the CPLD at boot.

## Other peripherals

| Block | CCSR | Notes |
|---|---|---|
| UART0 | 0x4500 | console (`ttyS0`, the DB9 connector), 115200 8N1 |
| UART1 | 0x4600 | supervisor MCU |
| eSDHC | 0x2E000 | the boot SD card |
| SATA | 0x18000, 0x19000 | two Freescale SATA controllers |
| USB | 0x22000 | one EHCI host controller with a ULPI PHY; the second (0x23000) is disabled |
| eSPI | 0x7000 | no flash fitted |
| DIU | 0x10000 | on-chip display controller, output through the TFP410 to DVI-D |
| SSI | 0x15000 | audio, with the WM8776 codec |
| MPIC | 0x40000 | interrupt controller |

As in the official firmware, the EHCI driver waits an extra millisecond while starting the
controller, and the USB keyboard is polled through the interrupt queue
(`CONFIG_SYS_USB_EVENT_POLL_VIA_INT_QUEUE`).

## Video

- A PCIe graphics card in the slot (on PCIe1) is the display. The official firmware POSTs its x86
  VBIOS in the emulator and uses VESA mode 0x117 (1024x768, 16 bpp); this port does the same with
  the code shared with the X5000 port. The VBIOS's I/O port window is PCIe1's.
- Jumper JP2 (GPIO2_31) must be fitted for U-Boot to use the card. The port honours it; set
  `vga_jumper=ignore` to override.
- Without a card, the official firmware falls back to the on-chip DIU at a fixed 800x600, 32 bpp,
  60 Hz (pixel clock about 40 MHz), through the TFP410 to the DVI-D connector. This port enables
  the TFP410 but has no DIU driver.
- The framebuffer of a PCIe Radeon is little-endian; the console swaps pixel bytes for it.

## Not yet verified

- The real POR straps (PORPLLSR ratios), and so the real core, platform and DDR clocks.
- The SerDes protocol strap; the SerDes slot names used are the P1022DS's.
- Whether the DDR settings computed from the SPD match the official firmware's on real modules.
- Which GPIO pins carry the DVI DDC (the monitor's I2C bus): GPIO3_20/21 or, as the TRM says,
  GPIO2_20/21.
- The contents and format of the EEPROMs at 0x53 and 0x5B, and the PCIe clock chip at 0x6E.
- The framebuffer byte order on the e500v2 (it is right on the X5000's e5500).
