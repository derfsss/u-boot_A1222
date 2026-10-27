// SPDX-License-Identifier: GPL-2.0+
/*
 * AmigaOne A1222 "Tabor" (Varisys, QorIQ P1022).
 *
 * Copyright 2010-2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2026 Richard Gibbs
 *
 * Derived from board/freescale/p1022ds/p1022ds.c (v2020.07, Copyright 2010-2012 Freescale
 * Semiconductor, Inc.; Srikanth Srinivasan, Timur Tabi). The PIXIS FPGA, NOR flash, NAND and
 * PIXIS-driven pin multiplexing of the P1022DS reference board are gone: Tabor has none of them.
 *
 * Board-specific parts: fixed clocks, the official pin multiplexing and block disables, MAC
 * addresses from the board EEPROMs, the TFP410 DVI encoder, the Micrel PHY pad skew, and the
 * "mcu", "shutdown" and "restart" commands for the supervisor MCU. Board facts are collected
 * in docs/hardware.md.
 *
 * Boots AmigaOS 4.1 on an A1222. Parts noted below as inherited from the P1022DS have not been
 * checked against the Tabor board one by one.
 */

#include <common.h>
#include <command.h>
#include <env.h>
#include <image.h>
#include <init.h>
#include <log.h>
#include <net.h>
#include <pci.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/immap_85xx.h>
#include <asm/fsl_pci.h>
#include <fsl_ddr_sdram.h>
#include <asm/fsl_serdes.h>
#include <asm/io.h>
#include <linux/libfdt.h>
#include <fdt_support.h>
#include <fsl_mdio.h>
#include <tsec.h>
#include <asm/fsl_law.h>
#include <asm/fsl_lbc.h>
#include <netdev.h>
#include <miiphy.h>
#include <i2c.h>
#include <hwconfig.h>
#include <video_fb.h>
#include <linux/delay.h>
#include <ns16550.h>

/*
 * Tabor has a fixed oscillator (no ICS307 clock chip, no PIXIS). As in the official firmware,
 * both the system and the DDR clock are 66,660,000 Hz. (The SPL uses 66,666,000 Hz for its
 * serial divisor; see spl.c.)
 */
unsigned long get_board_sys_clk(void)
{
	return 66660000;
}

unsigned long get_board_ddr_clk(void)
{
	return 66660000;
}

/*
 * Early board setup. The register values match the official firmware; the comments decode
 * them with the P1022 Reference Manual (global utilities, chapter 26).
 */
#define TABOR_PMUXCR	0x40011030	/* eLBC/DIU = DIU, USB1 pins, 4-CS SPI, eSDHC pads, UART0 2-wire
					 * + SSI Rx, UART1 2-wire + I2C2, SSI TX, IRQ[0:5], GPIO on
					 * the IRQ_DEBUG pins */
#define TABOR_PMUXCR2	0xf7000000	/* MCP/UDE/PWROK/TRIG/READY pins as GPIO_3[14:17,19:22];
					 * eTSEC1 and eTSEC2 (RGMII) pins as ethernet */
/*
 * DEVDISR (GUTS+0x70): block disables, as in the current official firmware release: PCIe2,
 * PCIe3, the security engine and the second USB controller are off.
 */
#define TABOR_DEVDISR	0x07400000
/* GPIO_3[15] and [16]: the MCU reset and power lines (Linux tabor.c toggles these) */
#define TABOR_MCU_GPIO_MASK	0x00018000

int board_early_init_f(void)
{
	ccsr_gur_t *gur = (void *)CONFIG_SYS_MPC85xx_GUTS_ADDR;
	void __iomem *gpio3 = (void __iomem *)(CONFIG_SYS_CCSRBAR + 0xf200);

	out_be32(&gur->pmuxcr, TABOR_PMUXCR);
	out_be32(&gur->pmuxcr2, TABOR_PMUXCR2);
	out_be32(&gur->devdisr, TABOR_DEVDISR);

	/* Read back the registers to synchronize the writes. */
	in_be32(&gur->pmuxcr);
	in_be32(&gur->pmuxcr2);
	in_be32(&gur->devdisr);

	/* Drive the MCU lines high (inactive) as open drain outputs: data, open drain, direction */
	setbits_be32(gpio3 + 0x08, TABOR_MCU_GPIO_MASK);	/* GPDAT */
	setbits_be32(gpio3 + 0x04, TABOR_MCU_GPIO_MASK);	/* GPODR */
	setbits_be32(gpio3 + 0x00, TABOR_MCU_GPIO_MASK);	/* GPDIR */

	return 0;
}

/*
 * Local bus setup, as in the official firmware: LBCR = 0 and LCRR = DBYP | EADC 1 | CLKDIV /8
 * (the same shape as the X5000 port's board_early_init_r()). Clearing LBCR drops the BMTPS
 * value set earlier for erratum ELBC-A001; the official firmware does the same, for a reason
 * that is not documented.
 */
int board_early_init_r(void)
{
	fsl_lbc_t *lbc = LBC_BASE_ADDR;

	out_be32(&lbc->lbcr, 0);
	out_be32(&lbc->lcrr, LCRR_DBYP | CONFIG_SYS_LBC_LCRR);

	return 0;
}

int checkboard(void)
{
	/* The official firmware prints "Board: Tabor". */
	puts("Board: Tabor\n");

	return 0;
}

/*
 * MAC addresses live in board EEPROMs at fixed offsets on I2C bus 1 (the second controller,
 * CCSR 0x3100), as in the official firmware: ethaddr = 6 bytes at EEPROM 0x57 offset 0xf2,
 * eth1addr = 6 bytes at EEPROM 0x50 offset 0xfa, 1-byte register addresses. The environment
 * wins: an existing variable is never overwritten.
 */
#define TABOR_MAC_I2C_BUS	1

/*
 * Set when a MAC address was copied from an EEPROM into the environment. boota (cmd/boota.c)
 * then saves the environment before booting, as the official firmware does.
 */
int tabor_env_dirty;

static void tabor_mac_from_eeprom(const char *name, uint chip, uint offset)
{
	uchar mac[ARP_HLEN];

	if (env_get(name))
		return;
	if (i2c_set_bus_num(TABOR_MAC_I2C_BUS) ||
	    i2c_read(chip, offset, 1, mac, sizeof(mac))) {
		printf("%s: cannot read the MAC from EEPROM %#x\n", name, chip);
		return;
	}
	if (!is_valid_ethaddr(mac)) {
		printf("%s: no valid MAC in EEPROM %#x offset %#x\n", name, chip, offset);
		return;
	}
	eth_env_set_enetaddr(name, mac);
	tabor_env_dirty = 1;
}

/*
 * On-board DVI-D output: the DIU's output goes through a TI TFP410 encoder at I2C address 0x38
 * on the same bus (Tabor TRM table 6). It is enabled as in the official firmware and on the
 * P1022DS: register 0x08 <= 0xBF and register 0x0A <= 0x10, each read back. GPIO_3[20:21]
 * (muxed as GPIO by PMUXCR2; the TRM calls them the DVI DDC SDA/SCL lines) are driven high as
 * open-drain outputs. There is no DIU framebuffer driver: U-Boot's display is a PCIe graphics
 * card (video.c).
 */
#define TABOR_TFP410_ADDR	0x38
#define TABOR_DDC_GPIO_MASK	0x00000c00

static int tabor_tfp410_write(uint reg, u8 val)
{
	u8 chk;

	if (i2c_write(TABOR_TFP410_ADDR, reg, 1, &val, 1) < 0 ||
	    i2c_read(TABOR_TFP410_ADDR, reg, 1, &chk, 1) < 0)
		return -1;
	debug("TFP410 reg %#x = %#x\n", reg, chk);
	return 0;
}

static void tabor_dvi_init(void)
{
	void __iomem *gpio3 = (void __iomem *)(CONFIG_SYS_CCSRBAR + 0xf200);

	if (i2c_set_bus_num(TABOR_MAC_I2C_BUS) ||
	    tabor_tfp410_write(0x08, 0xbf) || tabor_tfp410_write(0x0a, 0x10))
		printf("DVI: cannot enable the TFP410 encoder\n");

	setbits_be32(gpio3 + 0x08, TABOR_DDC_GPIO_MASK);	/* GPDAT */
	setbits_be32(gpio3 + 0x04, TABOR_DDC_GPIO_MASK);	/* GPODR */
	setbits_be32(gpio3 + 0x00, TABOR_DDC_GPIO_MASK);	/* GPDIR */
}

int misc_init_r(void)
{
#ifdef CONFIG_CFB_CONSOLE
	/*
	 * Quiet boot (stdout without "vga"): replace the console-coloured screen left by video init
	 * with plain black; the autoboot countdown then draws its progress bar on it (shared with
	 * the X5000 port, see common/autoboot.c).
	 */
	const char *so = env_get("stdout");

	if (so && !strstr(so, "vga"))
		video_splash_blank();
#endif
	tabor_mac_from_eeprom("ethaddr", 0x57, 0xf2);
	tabor_mac_from_eeprom("eth1addr", 0x50, 0xfa);
	tabor_dvi_init();

	/*
	 * Not implemented: the default hwconfig has "audclk:12" and the board has a WM8776 codec,
	 * but how Tabor enables the codec's reference clock is not known (the P1022DS used its
	 * PIXIS, which Tabor does not have). Only the device tree is fixed up, in ft_codec_setup().
	 */
	return 0;
}

/*
 * A list of PCI and SATA slots
 */
enum slot_id {
	SLOT_PCIE1 = 1,
	SLOT_PCIE2,
	SLOT_PCIE3,
	SLOT_PCIE4,
	SLOT_PCIE5,
	SLOT_SATA1,
	SLOT_SATA2
};

/*
 * This array maps the slot identifiers to their names. Inherited from the P1022DS; not yet
 * checked against the Tabor board.
 */
static const char *slot_names[] = {
	[SLOT_PCIE1] = "Slot 1",
	[SLOT_PCIE2] = "Slot 2",
	[SLOT_PCIE3] = "Slot 3",
	[SLOT_PCIE4] = "Slot 4",
	[SLOT_PCIE5] = "Mini-PCIe",
	[SLOT_SATA1] = "SATA 1",
	[SLOT_SATA2] = "SATA 2",
};

/*
 * This array maps a given SERDES configuration and SERDES device to the PCI or
 * SATA slot that it connects to. Inherited from the P1022DS, where the mapping is fixed by its
 * FPGA. Tabor's SerDes protocol (PORDEVSR[IO_SEL]) has not been checked yet, so the slot names
 * printed on a Tabor may be wrong; they are informational only.
 */
static u8 serdes_dev_slot[][SATA2 + 1] = {
	[0x01] = { [PCIE3] = SLOT_PCIE4, [PCIE2] = SLOT_PCIE5 },
	[0x02] = { [SATA1] = SLOT_SATA1, [SATA2] = SLOT_SATA2 },
	[0x09] = { [PCIE1] = SLOT_PCIE1, [PCIE3] = SLOT_PCIE4,
		   [PCIE2] = SLOT_PCIE5 },
	[0x16] = { [PCIE1] = SLOT_PCIE1, [PCIE3] = SLOT_PCIE2,
		   [PCIE2] = SLOT_PCIE3,
		   [SATA1] = SLOT_SATA1, [SATA2] = SLOT_SATA2 },
	[0x17] = { [PCIE1] = SLOT_PCIE1, [PCIE3] = SLOT_PCIE2,
		   [PCIE2] = SLOT_PCIE3 },
	[0x1a] = { [PCIE1] = SLOT_PCIE1, [PCIE2] = SLOT_PCIE3,
		   [PCIE2] = SLOT_PCIE3,
		   [SATA1] = SLOT_SATA1, [SATA2] = SLOT_SATA2 },
	[0x1c] = { [PCIE1] = SLOT_PCIE1,
		   [SATA1] = SLOT_SATA1, [SATA2] = SLOT_SATA2 },
	[0x1e] = { [PCIE1] = SLOT_PCIE1, [PCIE3] = SLOT_PCIE3 },
	[0x1f] = { [PCIE1] = SLOT_PCIE1 },
};

/*
 * Returns the name of the slot to which the PCIe or SATA controller is
 * connected
 */
const char *board_serdes_name(enum srds_prtcl device)
{
	ccsr_gur_t *gur = (void *)CONFIG_SYS_MPC85xx_GUTS_ADDR;
	u32 pordevsr = in_be32(&gur->pordevsr);
	unsigned int srds_cfg = (pordevsr & MPC85xx_PORDEVSR_IO_SEL) >>
				MPC85xx_PORDEVSR_IO_SEL_SHIFT;
	enum slot_id slot = serdes_dev_slot[srds_cfg][device];
	const char *name = slot_names[slot];

	if (name)
		return name;
	else
		return "Nothing";
}

#ifdef CONFIG_PCI
void pci_init_board(void)
{
	fsl_pcie_init_board(0);
}
#endif

/*
 * The two on-board PHYs (addresses 3 and 7) are Micrel KSZ9021RN. As the official firmware
 * does, set their RGMII pad skew over MDIO before the eTSECs start: extended register 0x104
 * (clock and control pad skew) = 0xF0F0, 0x105 (RX data) = 0, 0x106 (TX data) = 0, written
 * through registers 11 (address, bit 15 = write) and 12 (data); then BMCR = 0x1340 (autoneg,
 * restart, full duplex, 1000). Register meanings are from the KSZ9021RN data sheet.
 */
static void tabor_phy_tune(const char *bus, int addr)
{
	printf("Tuning PHY @ %d\n", addr);
	miiphy_write(bus, addr, 0x0b, 0x8104);
	miiphy_write(bus, addr, 0x0c, 0xf0f0);
	miiphy_write(bus, addr, 0x0b, 0x8105);
	miiphy_write(bus, addr, 0x0c, 0x0000);
	miiphy_write(bus, addr, 0x0b, 0x8106);
	miiphy_write(bus, addr, 0x0c, 0x0000);
	miiphy_write(bus, addr, 0x00, 0x1340);
}

/*
 * Initialize on-board and/or PCI Ethernet devices
 *
 * Returns:
 *      <0, error
 *       0, no ethernet devices found
 *      >0, number of ethernet devices initialized
 */
int board_eth_init(struct bd_info *bis)
{
	struct fsl_pq_mdio_info mdio_info;
	struct tsec_info_struct tsec_info[2];
	unsigned int num = 0;

#ifdef CONFIG_TSEC1
	SET_STD_TSEC_INFO(tsec_info[num], 1);
	num++;
#endif
#ifdef CONFIG_TSEC2
	SET_STD_TSEC_INFO(tsec_info[num], 2);
	num++;
#endif

	mdio_info.regs = (struct tsec_mii_mng *)CONFIG_SYS_MDIO_BASE_ADDR;
	mdio_info.name = DEFAULT_MII_NAME;
	fsl_pq_mdio_init(bis, &mdio_info);

	tabor_phy_tune(DEFAULT_MII_NAME, TSEC1_PHY_ADDR);
	tabor_phy_tune(DEFAULT_MII_NAME, TSEC2_PHY_ADDR);

	return tsec_eth_init(bis, tsec_info, num) + pci_eth_init(bis);
}

#ifdef CONFIG_OF_BOARD_SETUP
/**
 * ft_codec_setup - fix up the clock-frequency property of the codec node
 *
 * Update the clock-frequency property based on the value of the 'audclk'
 * hwconfig option.  If audclk is not specified, then don't write anything
 * to the device tree, because it means that the codec clock is disabled.
 */
static void ft_codec_setup(void *blob, const char *compatible)
{
	const char *audclk;
	size_t arglen;
	u32 freq;

	audclk = hwconfig_arg("audclk", &arglen);
	if (audclk) {
		if (strncmp(audclk, "11", 2) == 0)
			freq = 11289600;
		else
			freq = 12288000;

		do_fixup_by_compat_u32(blob, compatible, "clock-frequency",
				       freq, 1);
	}
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	phys_addr_t base;
	phys_size_t size;

	ft_cpu_setup(blob, bd);

	base = env_get_bootm_low();
	size = env_get_bootm_size();

	fdt_fixup_memory(blob, (u64)base, (u64)size);

#ifdef CONFIG_HAS_FSL_DR_USB
	fsl_fdt_fixup_dr_usb(blob, bd);
#endif

	FT_FSL_PCI_SETUP;

	/* Update the WM8776 node's clock frequency property */
	ft_codec_setup(blob, "wlf,wm8776");

	return 0;
}
#endif

/*
 * mcu, shutdown and restart commands, modelled on the X5000 port's shutdown/restart commands.
 *
 * Supervisor MCU (Tabor TRM section 8): the second UART (CCSR+0x4600, UART1) talks to the MCU
 * at 38400 8N1. CPU to MCU messages start with '#' and end with a newline, replies start with
 * '$' and end with a newline: 's' powers the board off, 't' reads three temperatures, 'v'
 * eleven voltages, 'f' the CPU fan (PWM and RPM). This is the same protocol and UART as the
 * X5000's MCU. Not yet tested on an A1222.
 *
 * The MCU reset and power-off lines are also GPIO_3[15] / GPIO_3[16] (TRM table 4). If the
 * UART command does not power the board off, shutdown falls back to clearing the GPIO bits the
 * Linux Tabor platform code clears (0x10008000 in the GPIO3 data register, CCSR 0xf208).
 *
 * restart is a plain SoC reset (boots the firmware from scratch), as on the X5000.
 */
static struct ns16550 *tabor_mcu_uart(void)
{
	struct ns16550 *mcu = (struct ns16550 *)CONFIG_SYS_NS16550_COM2;

	ns16550_init(mcu, CONFIG_SYS_NS16550_CLK / 16 / 38400);
	return mcu;
}

/* Send "#<c>\n" and collect the reply line into buf (NUL terminated); returns its length or -1. */
static int tabor_mcu_cmd(char c, char *buf, int len, int timeout_ms)
{
	struct ns16550 *mcu = tabor_mcu_uart();
	int n = 0, t;

	while (ns16550_tstc(mcu))	/* drop stale input */
		ns16550_getc(mcu);

	ns16550_putc(mcu, '#');
	ns16550_putc(mcu, c);
	ns16550_putc(mcu, '\n');

	for (t = 0; t < timeout_ms * 10; t++) {
		while (ns16550_tstc(mcu)) {
			char ch = ns16550_getc(mcu);

			if (ch == '\n') {
				buf[n] = '\0';
				return n;
			}
			if (n < len - 1)
				buf[n++] = ch;
		}
		udelay(100);
	}
	buf[n] = '\0';
	return -1;
}

static int do_mcu(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	char reply[96];
	char c;

	if (argc != 2 || !strchr("tvfs", argv[1][0]) || argv[1][1])
		return CMD_RET_USAGE;
	c = argv[1][0];

	if (c == 's')
		printf("Powering off via the board MCU...\n");
	if (tabor_mcu_cmd(c, reply, sizeof(reply), 500) < 0) {
		if (c == 's')
			return CMD_RET_SUCCESS;	/* power dropped before a reply */
		printf("mcu: no reply\n");
		return CMD_RET_FAILURE;
	}
	printf("%s\n", reply);
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	mcu, 2, 0, do_mcu,
	"talk to the supervisor MCU (TRM section 8)",
	"t|v|f|s\n"
	"    t - temperatures: $t<sign>HH x3 (PCB, CPU, PCB 2), hex degrees C\n"
	"    v - voltages: $v then 11 x XXYY (XX volts, YY x 10 mV)\n"
	"    f - CPU fan: $fXXYYYY (PWM, RPM, hex)\n"
	"    s - power the board off"
);

static int do_shutdown(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	void __iomem *gpdat = (void __iomem *)(CONFIG_SYS_CCSRBAR + 0xf208);
	char reply[16];

	printf("Powering off via the board MCU...\n");
	mdelay(50);	/* let the message drain */
	tabor_mcu_cmd('s', reply, sizeof(reply), 1000);

	/* Still running: use the power GPIO lines, as Linux does. */
	clrbits_be32(gpdat, 0x10008000);
	mdelay(3000);
	printf("shutdown: the MCU did not power off the board\n");
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	shutdown, 1, 0, do_shutdown,
	"power off the machine (via the board MCU)",
	"\n    - sends '#s' to the MCU, then clears the power GPIO lines (GPIO_3[16] and [3])"
);

static int do_restart(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	printf("Restarting...\n");
	mdelay(50);
	return do_reset(NULL, 0, 0, NULL);
}

U_BOOT_CMD(
	restart, 1, 0, do_restart,
	"restart the machine (SoC reset, boots firmware from scratch)",
	"\n    - equivalent to 'reset'; named for symmetry with 'shutdown'"
);
