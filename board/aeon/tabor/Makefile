# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2010 Freescale Semiconductor, Inc.
# Copyright (C) 2026 Richard Gibbs
#
# AmigaOne A1222 "Tabor" (Varisys, QorIQ P1022). Derived from the Freescale P1022DS port.

ifdef CONFIG_SPL_BUILD
obj-y	+= spl.o
endif
obj-y	+= tabor.o
obj-y	+= ddr.o
obj-y	+= law.o
obj-y	+= tlb.o
# video.o: PCIe Radeon VBIOS POST through the x86 emulator (main U-Boot only)
ifndef CONFIG_SPL_BUILD
obj-$(CONFIG_BIOSEMU)	+= video.o
endif
