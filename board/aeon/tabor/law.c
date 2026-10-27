// SPDX-License-Identifier: GPL-2.0+
/*
 * AmigaOne A1222 (Tabor) static local access windows. Tabor has no NOR/NAND flash and no PIXIS
 * FPGA, so the one static LAW is the 64 KiB local-bus window of the Main CPLD (eLBC chip
 * select 0) at physical 0xF_E0000000. This matches the official firmware's table; the DDR and
 * PCIe windows are created at run time.
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 * Copyright (C) 2026 Richard Gibbs
 */

#include <common.h>
#include <asm/fsl_law.h>
#include <asm/mmu.h>

struct law_entry law_table[] = {
	SET_LAW(CONFIG_SYS_CPLD_BASE_PHYS, LAW_SIZE_64K, LAW_TRGT_IF_LBC),
};

int num_law_entries = ARRAY_SIZE(law_table);
