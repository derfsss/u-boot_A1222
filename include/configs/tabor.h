/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AmigaOne A1222 "Tabor" (Varisys, QorIQ P1022, dual e500v2).
 *
 * Copyright 2010-2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2026 Richard Gibbs
 *
 * Derived from include/configs/P1022DS.h (v2020.07, Copyright 2010-2012 Freescale
 * Semiconductor, Inc.). NOR flash, NAND, SPI flash, PIXIS and the ICS307 clock chip are gone:
 * the Tabor board has none of them. Board facts (DDR, clocks, memory map) are in
 * docs/hardware.md.
 *
 * Boots AmigaOS 4.1 on an A1222. Settings noted as inherited from the P1022DS have not been
 * checked against the Tabor board one by one.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/stringify.h>

#ifdef CONFIG_SDCARD
/*
 * SD boot layout (the P1022DS values: 128 KiB SPL, U-Boot 128 KiB after it). The boot ROM
 * copies u-boot-with-spl.bin from card offset 0x400 into L2 SRAM; the SPL then loads U-Boot
 * from 0x400 + 128 KiB. The environment is at 0x600000, as on the official card. The official
 * card splits its first stages differently; see docs/sd-card-layout.md.
 */
#define CONFIG_SPL_FLUSH_IMAGE
#define CONFIG_SPL_TARGET		"u-boot-with-spl.bin"
#define CONFIG_SPL_PAD_TO		0x20000
#define CONFIG_SPL_MAX_SIZE		(128 * 1024)
#define CONFIG_SYS_MMC_U_BOOT_SIZE	(768 << 10)
#define CONFIG_SYS_MMC_U_BOOT_DST	(0x11000000)
#define CONFIG_SYS_MMC_U_BOOT_START	(0x11000000)
#define CONFIG_SYS_MMC_U_BOOT_OFFS	(128 << 10)
#define CONFIG_SYS_MPC85XX_NO_RESETVEC
#ifdef CONFIG_SPL_BUILD
#define CONFIG_SPL_COMMON_INIT_DDR
#endif
#endif

/* High Level Configuration Options */

#ifndef CONFIG_RESET_VECTOR_ADDRESS
#define CONFIG_RESET_VECTOR_ADDRESS	0xeffffffc
#endif

#define CONFIG_PCIE1			/* PCIE controller 1 */
#define CONFIG_PCIE2			/* PCIE controller 2 */
#define CONFIG_PCIE3			/* PCIE controller 3 */
#define CONFIG_FSL_PCI_INIT		/* Use common FSL init code */
#define CONFIG_SYS_PCI_64BIT		/* enable 64-bit PCI resources */

#define CONFIG_ENABLE_36BIT_PHYS

#ifdef CONFIG_PHYS_64BIT
#define CONFIG_ADDR_MAP
#define CONFIG_SYS_NUM_ADDR_MAP		16	/* number of TLB1 entries */
#endif

/* Fixed oscillator, see get_board_sys_clk() in board/aeon/tabor/tabor.c */
#ifndef __ASSEMBLY__
unsigned long get_board_sys_clk(void);
unsigned long get_board_ddr_clk(void);
#endif
#define CONFIG_SYS_CLK_FREQ	get_board_sys_clk()
#define CONFIG_DDR_CLK_FREQ	get_board_ddr_clk()

/*
 * These can be toggled for performance analysis, otherwise use default.
 */
#define CONFIG_L2_CACHE
#define CONFIG_BTB

#define CONFIG_SYS_CCSRBAR		0xffe00000
#define CONFIG_SYS_CCSRBAR_PHYS_LOW	CONFIG_SYS_CCSRBAR

/* CCSR stays at its reset address in the SPL; the main U-Boot relocates it (as the official one does) */
#ifdef CONFIG_SPL_BUILD
#define CONFIG_SYS_CCSR_DO_NOT_RELOCATE
#endif

/* DDR Setup: SPD driven, as in the official firmware */
#define CONFIG_DDR_SPD
#define CONFIG_VERY_BIG_RAM

#ifdef CONFIG_DDR_ECC
#define CONFIG_ECC_INIT_VIA_DDRCONTROLLER
#define CONFIG_MEM_INIT_VALUE		0xdeadbeef
#endif

#define CONFIG_SYS_DDR_SDRAM_BASE	0x00000000
#define CONFIG_SYS_SDRAM_BASE		CONFIG_SYS_DDR_SDRAM_BASE

#define CONFIG_DIMM_SLOTS_PER_CTLR	1
/* Tabor's controller uses four chip selects (as stated in A-Eon's firmware release notes) */
#define CONFIG_CHIP_SELECTS_PER_CTRL	4

/* I2C address of the SPD EEPROM: bus 1, address 0x51, as in the official firmware. */
#define CONFIG_SYS_SPD_BUS_NUM		1
#define SPD_EEPROM_ADDRESS		0x51	/* CTLR 0 DIMM 0 */

/*
 * Memory map (same as the official firmware's TLB table; see docs/hardware.md)
 *
 * 0x0000_0000	0x7fff_ffff	DDR			2G
 * 0x8000_0000	0xdfff_ffff	PCI Express Mem		1.5G non-cacheable
 * 0xffc0_0000	0xffc2_ffff	PCI IO range		192K non-cacheable
 * 0xffd0_0000	0xffd0_3fff	L1 for stack		16K Cacheable TLB0
 * 0xffe0_0000	0xffef_ffff	CCSR			1M non-cacheable
 */

/*
 * Main CPLD on eLBC chip select 0 (Tabor TRM section 6 accesses it with 'mw.b e0000000 ...'):
 * an 8-bit multiplexed index/data window. There is no flash. The settings match the official
 * firmware: only CS0 is programmed, BR0 = 0xE0000801 (8-bit GPCM), OR0 = 0xFFF00010 (1 MiB
 * mask, one wait state); the window is a 64 KiB TLB entry and a 64 KiB local-bus LAW. Neither
 * the official firmware nor this port talks to the CPLD at boot.
 */
#define CONFIG_SYS_CPLD_BASE		0xe0000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_CPLD_BASE_PHYS	0xfe0000000ull
#else
#define CONFIG_SYS_CPLD_BASE_PHYS	CONFIG_SYS_CPLD_BASE
#endif
#define CONFIG_SYS_BR0_PRELIM		(BR_PHYS_ADDR(CONFIG_SYS_CPLD_BASE_PHYS) | BR_PS_8 | BR_V)
#define CONFIG_SYS_OR0_PRELIM		(OR_AM_1MB | OR_GPCM_SCY_1)
/* EADC 1, CLKDIV /8 (LCRR_CLKDIV_8 is 4 on the P1022); board_early_init_r() adds DBYP, as the official firmware does */
#define CONFIG_SYS_LBC_LCRR		(LCRR_EADC_1 | LCRR_CLKDIV_8)

#ifndef CONFIG_SYS_MONITOR_BASE
#ifdef CONFIG_SPL_BUILD
#define CONFIG_SYS_MONITOR_BASE		CONFIG_SPL_TEXT_BASE
#else
#define CONFIG_SYS_MONITOR_BASE		CONFIG_SYS_TEXT_BASE	/* start of monitor */
#endif
#endif

#define CONFIG_HWCONFIG

#define CONFIG_SYS_INIT_RAM_LOCK
#define CONFIG_SYS_INIT_RAM_ADDR	0xffd00000 /* Initial L1 address */
#define CONFIG_SYS_INIT_RAM_SIZE		0x00004000 /* Size of used area in RAM */

#define CONFIG_SYS_GBL_DATA_OFFSET	\
	(CONFIG_SYS_INIT_RAM_SIZE - GENERATED_GBL_DATA_SIZE)
#define CONFIG_SYS_INIT_SP_OFFSET	CONFIG_SYS_GBL_DATA_OFFSET

#define CONFIG_SYS_MONITOR_LEN		(768 * 1024)
#define CONFIG_SYS_MALLOC_LEN		(10 * 1024 * 1024)

/*
 * Config the L2 Cache as L2 SRAM (the official first stage runs from 0xf8f80000, 256 KiB)
 */
#if defined(CONFIG_SPL_BUILD)
#if defined(CONFIG_SDCARD)
#define CONFIG_SYS_INIT_L2_ADDR	0xf8f80000
#define CONFIG_SYS_INIT_L2_ADDR_PHYS	CONFIG_SYS_INIT_L2_ADDR
#define CONFIG_SYS_L2_SIZE		(256 << 10)
#define CONFIG_SYS_INIT_L2_END	(CONFIG_SYS_INIT_L2_ADDR + CONFIG_SYS_L2_SIZE)
#define CONFIG_SPL_RELOC_TEXT_BASE	0xf8f81000
#define CONFIG_SPL_RELOC_STACK		(CONFIG_SYS_INIT_L2_ADDR + 116 * 1024)
#define CONFIG_SPL_RELOC_MALLOC_ADDR	(CONFIG_SYS_INIT_L2_ADDR + 148 * 1024)
#define CONFIG_SPL_RELOC_MALLOC_SIZE	(108 << 10)
#define CONFIG_SPL_GD_ADDR		(CONFIG_SYS_INIT_L2_ADDR + 112 * 1024)
#endif
#endif

/*
 * Serial Port: UART0 at CCSR+0x4500 is the console (serial0 in the Tabor device tree)
 */
#define CONFIG_SYS_NS16550_SERIAL
#define CONFIG_SYS_NS16550_REG_SIZE	1
#define CONFIG_SYS_NS16550_CLK		get_bus_freq(0)

#define CONFIG_SYS_BAUDRATE_TABLE	\
	{300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200}

#define CONFIG_SYS_NS16550_COM1	(CONFIG_SYS_CCSRBAR+0x4500)
#define CONFIG_SYS_NS16550_COM2	(CONFIG_SYS_CCSRBAR+0x4600)

/*
 * Video: a PCIe Radeon card is the display. Its x86 VBIOS is POSTed in the emulator
 * (board/aeon/tabor/video.c, ported from the X5000 port). VIDEO_IO_OFFSET is referenced
 * unconditionally by the emulator and must point at the I/O window of the controller the card
 * is on: PCIe1, the only controller left enabled (as in the official firmware).
 */
#ifdef CONFIG_BIOSEMU
#define CONFIG_FRAMEBUFFER_SET_VESA_MODE
#define VIDEO_IO_OFFSET		CONFIG_SYS_PCIE1_IO_VIRT
#endif

/*
 * Do not allocate BARs for PCI-to-PCI bridges (pci_auto_old.c); a Radeon with onboard bridges
 * (every Vega) otherwise exhausts the small window. Taken from the X5000 port; not yet tested
 * on an A1222.
 */
#define CONFIG_PCIAUTO_SKIP_BRIDGE_BARS

/* I2C: two controllers; the MCP7941x RTC is on the second (from the Tabor Linux device tree) */
#ifndef CONFIG_DM_I2C
#define CONFIG_SYS_I2C
#define CONFIG_SYS_FSL_I2C_SPEED	400000
#define CONFIG_SYS_FSL_I2C_SLAVE	0x7F
#define CONFIG_SYS_FSL_I2C_OFFSET	0x3000
#define CONFIG_SYS_FSL_I2C2_SPEED	400000
#define CONFIG_SYS_FSL_I2C2_SLAVE	0x7F
#define CONFIG_SYS_FSL_I2C2_OFFSET	0x3100
#define CONFIG_SYS_I2C_NOPROBES		{{0, 0x29}}
#endif
#define CONFIG_SYS_I2C_FSL

/*
 * MAC addresses: the factory environment has no ethaddr. misc_init_r() in board/aeon/tabor/tabor.c
 * reads them from the board EEPROMs, as the official firmware does. The P1022DS's
 * CONFIG_ID_EEPROM (board/freescale/common/sys_eeprom.c) is not used.
 */

/*
 * General PCI
 * Memory space is mapped 1-1, but I/O space must start from 0.
 * Same map as the Tabor device tree and the official TLB table.
 */

/* controller 1, tgtid 1, Base address a000 */
#define CONFIG_SYS_PCIE1_MEM_VIRT	0xc0000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE1_MEM_BUS	0xe0000000
#define CONFIG_SYS_PCIE1_MEM_PHYS	0xc40000000ull
#else
#define CONFIG_SYS_PCIE1_MEM_BUS	0xc0000000
#define CONFIG_SYS_PCIE1_MEM_PHYS	0xc0000000
#endif
#define CONFIG_SYS_PCIE1_MEM_SIZE	0x20000000	/* 512M */
#define CONFIG_SYS_PCIE1_IO_VIRT	0xffc20000
#define CONFIG_SYS_PCIE1_IO_BUS		0x00000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE1_IO_PHYS	0xfffc20000ull
#else
#define CONFIG_SYS_PCIE1_IO_PHYS	0xffc20000
#endif
#define CONFIG_SYS_PCIE1_IO_SIZE	0x00010000	/* 64k */

/* controller 2, tgtid 2, Base address 9000 */
#define CONFIG_SYS_PCIE2_MEM_VIRT	0xa0000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE2_MEM_BUS	0xe0000000
#define CONFIG_SYS_PCIE2_MEM_PHYS	0xc20000000ull
#else
#define CONFIG_SYS_PCIE2_MEM_BUS	0xa0000000
#define CONFIG_SYS_PCIE2_MEM_PHYS	0xa0000000
#endif
#define CONFIG_SYS_PCIE2_MEM_SIZE	0x20000000	/* 512M */
#define CONFIG_SYS_PCIE2_IO_VIRT	0xffc10000
#define CONFIG_SYS_PCIE2_IO_BUS		0x00000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE2_IO_PHYS	0xfffc10000ull
#else
#define CONFIG_SYS_PCIE2_IO_PHYS	0xffc10000
#endif
#define CONFIG_SYS_PCIE2_IO_SIZE	0x00010000	/* 64k */

/* controller 3, tgtid 3, Base address b000 */
#define CONFIG_SYS_PCIE3_MEM_VIRT	0x80000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE3_MEM_BUS	0xe0000000
#define CONFIG_SYS_PCIE3_MEM_PHYS	0xc00000000ull
#else
#define CONFIG_SYS_PCIE3_MEM_BUS	0x80000000
#define CONFIG_SYS_PCIE3_MEM_PHYS	0x80000000
#endif
#define CONFIG_SYS_PCIE3_MEM_SIZE	0x20000000	/* 512M */
#define CONFIG_SYS_PCIE3_IO_VIRT	0xffc00000
#define CONFIG_SYS_PCIE3_IO_BUS		0x00000000
#ifdef CONFIG_PHYS_64BIT
#define CONFIG_SYS_PCIE3_IO_PHYS	0xfffc00000ull
#else
#define CONFIG_SYS_PCIE3_IO_PHYS	0xffc00000
#endif
#define CONFIG_SYS_PCIE3_IO_SIZE	0x00010000	/* 64k */

#ifdef CONFIG_PCI
#define CONFIG_PCI_INDIRECT_BRIDGE
#define CONFIG_PCI_SCAN_SHOW		/* show pci devices on startup */
#endif

/* SATA: two controllers */
#define CONFIG_FSL_SATA_V2

#define CONFIG_SYS_SATA_MAX_DEVICE	2
#define CONFIG_SATA1
#define CONFIG_SYS_SATA1		CONFIG_SYS_MPC85xx_SATA1_ADDR
#define CONFIG_SYS_SATA1_FLAGS		FLAGS_DMA
#define CONFIG_SATA2
#define CONFIG_SYS_SATA2		CONFIG_SYS_MPC85xx_SATA2_ADDR
#define CONFIG_SYS_SATA2_FLAGS		FLAGS_DMA

#ifdef CONFIG_FSL_SATA
#define CONFIG_LBA48
#endif

#ifdef CONFIG_MMC
#define CONFIG_SYS_FSL_ESDHC_ADDR	CONFIG_SYS_MPC85xx_ESDHC_ADDR
#endif

#ifdef CONFIG_TSEC_ENET

#define CONFIG_TSECV2

#define CONFIG_TSEC1		1
#define CONFIG_TSEC1_NAME	"eTSEC1"
#define CONFIG_TSEC2		1
#define CONFIG_TSEC2_NAME	"eTSEC2"

/* From the Tabor device tree: MDIO PHY addresses 3 and 7, RGMII */
#define TSEC1_PHY_ADDR		3
#define TSEC2_PHY_ADDR		7

#define TSEC1_FLAGS		(TSEC_GIGABIT | TSEC_REDUCED)
#define TSEC2_FLAGS		(TSEC_GIGABIT | TSEC_REDUCED)

#define TSEC1_PHYIDX		0
#define TSEC2_PHYIDX		0

#define CONFIG_ETHPRIME		"eTSEC1"
#define CONFIG_HAS_ETH1
#endif

/*
 * Environment: on the SD card (the official layout keeps it at 0x600000, 0x2000 bytes,
 * big-endian CRC32, non-redundant; see docs/sd-card-layout.md)
 */
#if defined(CONFIG_SDCARD)
#define CONFIG_FSL_FIXED_MMC_LOCATION
#define CONFIG_SYS_MMC_ENV_DEV	0
#endif

#define CONFIG_LOADS_ECHO
#define CONFIG_SYS_LOADS_BAUD_CHANGE

/*
 * USB: one controller (the second is disabled in the device tree)
 */
#define CONFIG_HAS_FSL_DR_USB
#ifdef CONFIG_HAS_FSL_DR_USB
#ifdef CONFIG_USB_EHCI_HCD
#define CONFIG_EHCI_HCD_INIT_AFTER_RESET
#define CONFIG_USB_EHCI_FSL
#endif
#endif
/* As in the official firmware: the USB keyboard is polled through the interrupt queue. */
#define CONFIG_SYS_USB_EVENT_POLL_VIA_INT_QUEUE

/*
 * Miscellaneous configurable options
 */
#define CONFIG_SYS_LOAD_ADDR	0x2000000	/* default load address */

/*
 * For booting Linux, the board info and command line data
 * have to be in the first 64 MB of memory, since this is
 * the maximum mapped by the Linux kernel during initialization.
 */
#define CONFIG_SYS_BOOTMAPSZ	(64 << 20)	/* Initial Memory map for Linux*/
#define CONFIG_SYS_BOOTM_LEN	(64 << 20)	/* Increase max gunzip size */

#ifdef CONFIG_CMD_KGDB
#define CONFIG_KGDB_BAUDRATE	230400	/* speed to run kgdb serial port */
#endif

/*
 * Environment Configuration. The net/ram boot commands below are the generic P1022DS ones for
 * Linux. The AmigaOS variables are read by boota (cmd/boota.c, see docs/booting-amigaos.md):
 *   amigaboot_quiet   y: boot the default entry without showing the boot menu. The name is
 *                     kept from the official firmware so environments saved by it still work.
 *   bootable_only     y: list only partitions marked bootable in the RDB.
 *   os4_bootdevice    where to look: "auto" (SATA, then USB) or interface names.
 *   os4_commandline   AmigaOS kernel command line.
 * The official firmware's default environment has more variables; only these are set here.
 *
 * AmigaOne A1222: the boota defaults of a debug build (the default) and of a production build
 * (CONFIG_BOOTA_PRODUCTION, the *_PRODUCTION_defconfig). Production follows the official
 * environment: no boot menu, debuglevel=0. Debug shows the boot menu and uses debuglevel=1.
 */
#ifdef CONFIG_BOOTA_PRODUCTION
#define AOS4_ENV_QUIET		"y"
#define AOS4_ENV_CMDLINE	"debug munge debuglevel=0"
#else
#define AOS4_ENV_QUIET		"n"
#define AOS4_ENV_CMDLINE	"debug munge debuglevel=1"
#endif

#define CONFIG_HOSTNAME		"tabor"
#define CONFIG_ROOTPATH		"/opt/nfsroot"
#define CONFIG_BOOTFILE		"uImage"
#define CONFIG_UBOOTPATH	u-boot.bin	/* U-Boot image on TFTP server */

#define CONFIG_LOADADDR		1000000

#define	CONFIG_EXTRA_ENV_SETTINGS				\
	"netdev=eth0\0"						\
	"uboot=" __stringify(CONFIG_UBOOTPATH) "\0"		\
	"consoledev=ttyS0\0"					\
	"ramdiskaddr=2000000\0"					\
	"ramdiskfile=ramdisk.uboot\0"				\
	"fdtaddr=c00000\0"					\
	"fdtfile=tabor.dtb\0"					\
	"bdev=sda3\0"						\
	"hwconfig=esdhc;audclk:12;usb1:dr_mode=host,phy_type=ulpi\0"	\
	"stdin=serial,usbkbd\0"					\
	"stdout=serial\0"					\
	"stderr=serial\0"					\
	"amigaboot_quiet=" AOS4_ENV_QUIET "\0"			\
	"bootable_only=y\0"					\
	"os4_bootdevice=auto\0"					\
	"os4_commandline=" AOS4_ENV_CMDLINE "\0"			\
	"harddiskboot=sata init;boota\0"			\
	"massboot=usb reset;boota\0"

#define CONFIG_HDBOOT					\
	"setenv bootargs root=/dev/$bdev rw "		\
	"console=$consoledev,$baudrate $othbootargs;"	\
	"tftp $loadaddr $bootfile;"			\
	"tftp $fdtaddr $fdtfile;"			\
	"bootm $loadaddr - $fdtaddr"

#define CONFIG_NFSBOOTCOMMAND						\
	"setenv bootargs root=/dev/nfs rw "				\
	"nfsroot=$serverip:$rootpath "					\
	"ip=$ipaddr:$serverip:$gatewayip:$netmask:$hostname:$netdev:off " \
	"console=$consoledev,$baudrate $othbootargs;"			\
	"tftp $loadaddr $bootfile;"					\
	"tftp $fdtaddr $fdtfile;"					\
	"bootm $loadaddr - $fdtaddr"

#define CONFIG_RAMBOOTCOMMAND						\
	"setenv bootargs root=/dev/ram rw "				\
	"console=$consoledev,$baudrate $othbootargs;"			\
	"tftp $ramdiskaddr $ramdiskfile;"				\
	"tftp $loadaddr $bootfile;"					\
	"tftp $fdtaddr $fdtfile;"					\
	"bootm $loadaddr $ramdiskaddr $fdtaddr"

/*
 * Default autoboot action: boot AmigaOS 4 with boota, U-Boot's own boot menu and kickstart
 * loader (cmd/boota.c, common/aos4/; as in the X5000 port). The board information and device
 * tree passed to the kernel have the X5000's layout, which the A1222's kernel boots with (see
 * docs/booting-amigaos.md). Change it with setenv bootcmd.
 * stdout defaults to serial only: the screen stays black with a countdown progress bar (quiet
 * boot, as in the X5000 port). Typing at the countdown restores the screen console. Set
 * stdout=serial,vga for a visible console.
 */
#define CONFIG_BOOTCOMMAND		"boota"

#endif
