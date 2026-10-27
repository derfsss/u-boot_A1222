// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2026 Richard Gibbs
 *
 * AmigaOne A1222 (Tabor) SPL, derived from board/freescale/p1022ds/spl.c (v2020.07).
 * Tabor has no PIXIS, so the system clock is fixed: 66,666,000 Hz, the value the official
 * first stage uses for its serial divisor (see docs/hardware.md). SD boot only: the NAND and
 * SPI-flash boot paths of the P1022DS are removed.
 */

#include <common.h>
#include <clock_legacy.h>
#include <console.h>
#include <env.h>
#include <env_internal.h>
#include <init.h>
#include <ns16550.h>
#include <malloc.h>
#include <mmc.h>
#include <i2c.h>
#include <fsl_esdhc.h>
#include "../../freescale/common/spl.h"

DECLARE_GLOBAL_DATA_PTR;

#define TABOR_SPL_SYSCLK	66666000

phys_size_t get_effective_memsize(void)
{
	return CONFIG_SYS_L2_SIZE;
}

void board_init_f(ulong bootflag)
{
	u32 plat_ratio, bus_clk;
	ccsr_gur_t *gur = (void *)CONFIG_SYS_MPC85xx_GUTS_ADDR;

	console_init_f();

	/* Set pmuxcr to allow both i2c1 and i2c2 */
	setbits_be32(&gur->pmuxcr, in_be32(&gur->pmuxcr) | 0x1000);
	setbits_be32(&gur->pmuxcr,
		     in_be32(&gur->pmuxcr) | MPC85xx_PMUXCR_SD_DATA);

	/* Read back the register to synchronize the write. */
	in_be32(&gur->pmuxcr);

	/* initialize selected port with appropriate baud rate */
	plat_ratio = in_be32(&gur->porpllsr) & MPC85xx_PORPLLSR_PLAT_RATIO;
	bus_clk = TABOR_SPL_SYSCLK * plat_ratio / 2;

	ns16550_init((struct ns16550 *)CONFIG_SYS_NS16550_COM1,
		     bus_clk / 16 / CONFIG_BAUDRATE);
	puts("\nSD boot...\n");

	/* copy code to RAM and jump to it - this should not return */
	relocate_code(CONFIG_SPL_RELOC_STACK, 0, CONFIG_SPL_RELOC_TEXT_BASE);
}

void board_init_r(gd_t *gd, ulong dest_addr)
{
	/* Pointer is writable since we allocated a register for it */
	gd = (gd_t *)CONFIG_SPL_GD_ADDR;
	struct bd_info *bd;

	memset(gd, 0, sizeof(gd_t));
	bd = (struct bd_info *)(CONFIG_SPL_GD_ADDR + sizeof(gd_t));
	memset(bd, 0, sizeof(struct bd_info));
	gd->bd = bd;
	gd->ram_base = CONFIG_SYS_INIT_L2_ADDR;
	gd->ram_size = CONFIG_SYS_L2_SIZE;

	arch_cpu_init();
	get_clocks();
	mem_malloc_init(CONFIG_SPL_RELOC_MALLOC_ADDR,
			CONFIG_SPL_RELOC_MALLOC_SIZE);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;
	env_init();
	mmc_initialize(bd);
	/* relocate environment function pointers etc. */
	env_relocate();

#ifdef CONFIG_SYS_I2C
	i2c_init_all();
#else
	i2c_init(CONFIG_SYS_I2C_SPEED, CONFIG_SYS_I2C_SLAVE);
#endif

	dram_init();
	puts("Second program loader running in sram...\n");

	mmc_boot();
}
