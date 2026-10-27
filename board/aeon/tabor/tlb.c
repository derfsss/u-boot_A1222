// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2010 Freescale Semiconductor, Inc.
 * Copyright (C) 2026 Richard Gibbs
 * Authors: Srikanth Srinivasan <srikanth.srinivasan@freescale.com>
 *          Timur Tabi <timur@freescale.com>
 *
 * AmigaOne A1222 (Tabor) TLB table, from the P1022DS port minus the PIXIS and NAND entries.
 * The resulting tables match A-Eon's official Tabor firmware (memory map in docs/hardware.md):
 *   SPL: boot page, CCSR (at its reset address), L2 SRAM;
 *   main U-Boot: boot page, CCSR, Main CPLD, PCIe memory/IO, two 1 GiB DDR windows.
 * The Main CPLD (Tabor TRM section 6) is a 64 KiB window at 0xe0000000 (physical 0xfe0000000,
 * eLBC CS0), cache-inhibited and guarded, supervisor read/write, as in the official firmware.
 * As in the official firmware the DDR entries have no MAS2_M (not yet checked on hardware).
 */

#include <common.h>
#include <asm/mmu.h>

struct fsl_e_tlb_entry tlb_table[] = {
	/* TLB 0 - for temp stack in cache */
	SET_TLB_ENTRY(0, CONFIG_SYS_INIT_RAM_ADDR, CONFIG_SYS_INIT_RAM_ADDR,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 0, BOOKE_PAGESZ_4K, 0),
	SET_TLB_ENTRY(0, CONFIG_SYS_INIT_RAM_ADDR + 4 * 1024,
		      CONFIG_SYS_INIT_RAM_ADDR + 4 * 1024,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 0, BOOKE_PAGESZ_4K, 0),
	SET_TLB_ENTRY(0, CONFIG_SYS_INIT_RAM_ADDR + 8 * 1024,
		      CONFIG_SYS_INIT_RAM_ADDR + 8 * 1024,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 0, BOOKE_PAGESZ_4K, 0),
	SET_TLB_ENTRY(0, CONFIG_SYS_INIT_RAM_ADDR + 12 * 1024,
		      CONFIG_SYS_INIT_RAM_ADDR + 12 * 1024,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 0, BOOKE_PAGESZ_4K, 0),

	/* TLB 1 */
	/* *I*** - Covers boot page */
	SET_TLB_ENTRY(1, 0xfffff000, 0xfffff000,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I,
		      0, 0, BOOKE_PAGESZ_4K, 1),

	/* *I*G* - CCSRBAR */
	SET_TLB_ENTRY(1, CONFIG_SYS_CCSRBAR, CONFIG_SYS_CCSRBAR_PHYS,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 1, BOOKE_PAGESZ_1M, 1),

#ifndef CONFIG_SPL_BUILD
	/* *I*G* - Main CPLD, eLBC chip select 0 */
	SET_TLB_ENTRY(1, CONFIG_SYS_CPLD_BASE, CONFIG_SYS_CPLD_BASE_PHYS,
		      MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 2, BOOKE_PAGESZ_64K, 1),

	/* *I*G* - PCI */
	SET_TLB_ENTRY(1, CONFIG_SYS_PCIE3_MEM_VIRT, CONFIG_SYS_PCIE3_MEM_PHYS,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 3, BOOKE_PAGESZ_1G, 1),

	/* *I*G* - PCI */
	SET_TLB_ENTRY(1, CONFIG_SYS_PCIE3_MEM_VIRT + 0x40000000,
		      CONFIG_SYS_PCIE3_MEM_PHYS + 0x40000000,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 4, BOOKE_PAGESZ_256M, 1),

	SET_TLB_ENTRY(1, CONFIG_SYS_PCIE3_MEM_VIRT + 0x50000000,
		      CONFIG_SYS_PCIE3_MEM_PHYS + 0x50000000,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 5, BOOKE_PAGESZ_256M, 1),

	/* *I*G* - PCI I/O */
	SET_TLB_ENTRY(1, CONFIG_SYS_PCIE3_IO_VIRT, CONFIG_SYS_PCIE3_IO_PHYS,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_I|MAS2_G,
		      0, 6, BOOKE_PAGESZ_256K, 1),

	/* **** - DDR, first 1 GiB */
	SET_TLB_ENTRY(1, CONFIG_SYS_DDR_SDRAM_BASE, CONFIG_SYS_DDR_SDRAM_BASE,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 8, BOOKE_PAGESZ_1G, 1),
	/* **** - DDR, second 1 GiB (2 GiB mapped, see CONFIG_MAX_MEM_MAPPED) */
	SET_TLB_ENTRY(1, CONFIG_SYS_DDR_SDRAM_BASE + 0x40000000,
		      CONFIG_SYS_DDR_SDRAM_BASE + 0x40000000,
		      MAS3_SX|MAS3_SW|MAS3_SR, 0,
		      0, 9, BOOKE_PAGESZ_1G, 1),
#endif

#ifdef CONFIG_SYS_INIT_L2_ADDR
	/* *I*G - L2SRAM */
	SET_TLB_ENTRY(1, CONFIG_SYS_INIT_L2_ADDR, CONFIG_SYS_INIT_L2_ADDR_PHYS,
		      MAS3_SX|MAS3_SW|MAS3_SR, MAS2_G,
		      0, 11, BOOKE_PAGESZ_256K, 1)
#endif
};

int num_tlb_entries = ARRAY_SIZE(tlb_table);
