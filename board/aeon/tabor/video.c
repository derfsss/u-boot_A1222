// SPDX-License-Identifier: GPL-2.0+
/*
 * video.c - AmigaOne A1222 (Varisys Tabor) video via x86 VBIOS POST.
 *
 * Ported from the X5000 port's video.c, where it has been tested with an
 * RX560 and an RX Vega 64 on PCIe1. Not yet tested on an A1222.
 *
 * A PCIe Radeon's own x86 Video BIOS is POSTed inside U-Boot's x86 BIOS emulator
 * (CONFIG_BIOSEMU) to bring up a linear framebuffer, so U-Boot's console, the boota menu and
 * the AmigaOS kernel have a display.
 *
 * The card is found by PCI class rather than by a device-id list: every GCN and later Radeon
 * presents a display-class function carrying a legacy option ROM. Matching on class also skips
 * the HDMI-audio (0x0403), USB-C (0x0c03) and UCSI (0x0c80) functions that share the card's PCI
 * device number.
 *
 * The VESA mode is probed rather than hardcoded: video_hw_init() walks a preference-ordered
 * candidate list and takes the first mode the card's VBIOS delivers as a linear, unpadded
 * framebuffer in a pixel format cfb_console can draw. Set the environment variable 'vesamode'
 * (hex) to force one specific mode.
 *
 * A card behind PCI-to-PCI bridges needs those bridges opened up before the POST or its I/O
 * cycles go unclaimed and machine-check the CPU; see tabor_bridge_vga_enable() below.
 *
 * Constraints:
 *  - The card must sit on U-Boot's *PCIe1* controller (CCSR 0xa000; I/O window 0xffc20000).
 *    VIDEO_IO_OFFSET (include/configs/tabor.h) points the emulator's x86 I/O ports at
 *    CONFIG_SYS_PCIE1_IO_VIRT at compile time. On the A1222, PCIe controllers 2 and 3 are
 *    disabled in DEVDISR (as in the official firmware), so PCIe1 is the one left. A warning
 *    is printed, not a refusal, for a card on another controller.
 *  - A card whose option ROM holds only a UEFI GOP image still passes the 0x55AA signature check
 *    in drivers/bios_emulator/atibios.c and the emulator will then execute PE/COFF as real-mode
 *    code and hang. Walking the ROM's PCIR image chain for a code-type 0x00 image is the fix; it
 *    is not implemented, so broad detection makes that failure reachable.
 *
 * Provides:
 *   video_hw_init() - called by cfb_console at stdio_add_devices: POSTs the card, selects a mode
 *                     and returns a GraphicDevice so the framebuffer console renders text.
 *                     Needs CONFIG_VIDEO + CONFIG_CFB_CONSOLE and 'vga' in stdout.
 *   videoboot cmd   - manual bring-up/debug: POST only, scan the candidate modes, or POST + set
 *                     one mode and print its geometry.
 *
 * Copyright (C) 2026 Richard Gibbs
 */

#include <common.h>
#include <command.h>
#include <env.h>
#include <pci.h>
#include <bios_emul.h>
#include <vbe.h>
#include <video_fb.h>
#include <vsprintf.h>
#include <asm/io.h>
#include <linux/string.h>

#define PCI_VENDOR_ATI		0x1002

/* VBE mode_attributes bit 7: a linear (non-banked) framebuffer is available. */
#define VBE_ATTR_LINEAR_FB	0x80

/*
 * Preference-ordered VESA mode numbers tried by video_hw_init().
 *
 * 16bpp before 32bpp at a given resolution for two reasons: the framebuffer is
 * written across the PCIe window, so halving the bytes per pixel noticeably
 * speeds up console scrolling, and the boot splash progress bar in cfb_console
 * only draws in GDF_16BIT_565RGB. 1024x768 first: it is the highest linear
 * mode an RX560 advertises, and 0x117/0x118 are carried by every AMD legacy
 * VBIOS.
 *
 * Each attempt costs a full VBIOS POST (about 1.3 s on an RX560), because the
 * emulator API only sets a mode as part of POSTing. In practice the first entry
 * succeeds and there is exactly one POST; later entries only cost time on a card
 * that refuses 1024x768. A card with no usable VBIOS at all fails the POST
 * itself and aborts the walk immediately rather than retrying six times.
 */
static const u16 tabor_vesa_modes[] = {
	0x117,		/* 1024x768x16 */
	0x118,		/* 1024x768x32 */
	0x114,		/* 800x600x16 */
	0x115,		/* 800x600x32 */
	0x111,		/* 640x480x16 */
	0x112,		/* 640x480x32 */
};

/* tabor_post_mode() results. */
#define TABOR_POST_OK		0	/* POSTed and the mode was set */
#define TABOR_POST_NOMODE	1	/* POSTed, but this mode was refused */
#define TABOR_POST_FAIL		(-1)	/* no usable VBIOS / emulator error */

/*
 * Find the display function of an AMD/ATI card. Walks the hoses the same way
 * pci_find_devices() does, so it does not depend on PCI bus numbers being
 * contiguous across the PCIe controllers. Returns the BDF and, optionally,
 * the owning hose and the card's IDs; negative if there is no such device.
 */
static pci_dev_t tabor_find_display(struct pci_controller **hosep,
				    u16 *vendorp, u16 *devicep)
{
	struct pci_controller *hose;
	struct pci_controller *fallback_hose = NULL;
	pci_dev_t fallback = -1;
	int bus;

	for (hose = pci_get_hose_head(); hose; hose = hose->next) {
		for (bus = hose->first_busno; bus <= hose->last_busno; bus++) {
			pci_dev_t bdf;
			int found_multi = 0;

			for (bdf = PCI_BDF(bus, 0, 0);
			     bdf < PCI_BDF(bus + 1, 0, 0);
			     bdf += PCI_BDF(0, 0, 1)) {
				u32 class_rev;
				u16 vendor, class;
				u8 hdr_type;

				if (!PCI_FUNC(bdf)) {
					pci_hose_read_config_byte(hose, bdf,
							PCI_HEADER_TYPE,
							&hdr_type);
					found_multi = hdr_type & 0x80;
				} else if (!found_multi) {
					continue;
				}

				pci_hose_read_config_word(hose, bdf,
							  PCI_VENDOR_ID,
							  &vendor);
				if (vendor != PCI_VENDOR_ATI)
					continue;

				pci_hose_read_config_dword(hose, bdf,
							   PCI_CLASS_REVISION,
							   &class_rev);
				/* Base class + subclass; ignore the prog-if. */
				class = class_rev >> 16;
				if (class != PCI_CLASS_DISPLAY_VGA &&
				    class != PCI_CLASS_DISPLAY_OTHER)
					continue;

				if (class != PCI_CLASS_DISPLAY_VGA) {
					/*
					 * A function not decoding legacy VGA
					 * cycles is a poor POST target; keep it
					 * only as a last resort.
					 */
					if (fallback < 0) {
						fallback = bdf;
						fallback_hose = hose;
					}
					continue;
				}

				if (hosep)
					*hosep = hose;
				if (vendorp)
					*vendorp = vendor;
				if (devicep)
					pci_hose_read_config_word(hose, bdf,
							PCI_DEVICE_ID, devicep);
				return bdf;
			}
		}
	}

	if (fallback >= 0) {
		if (hosep)
			*hosep = fallback_hose;
		if (vendorp)
			*vendorp = PCI_VENDOR_ATI;
		if (devicep)
			pci_hose_read_config_word(fallback_hose, fallback,
						  PCI_DEVICE_ID, devicep);
	}
	return fallback;
}

/*
 * Is this controller the one VIDEO_IO_OFFSET points at? The emulator's x86 I/O
 * port window is a compile-time constant, so only a card on PCIe1 sees its own
 * I/O space during POST.
 */
static int tabor_hose_is_pcie1(struct pci_controller *hose)
{
	int i;

	if (!hose)
		return 0;

	for (i = 0; i < hose->region_count; i++) {
		if ((hose->regions[i].flags & PCI_REGION_TYPE) != PCI_REGION_IO)
			continue;
		return hose->regions[i].phys_start == CONFIG_SYS_PCIE1_IO_PHYS;
	}
	return 0;
}

/*
 * Map the VBIOS-reported pixel format onto a cfb_console GDF index. VBE reports
 * both 5:5:5 and 5:6:5 as 16bpp on some BIOSes, so the green mask width is the
 * discriminator. Indexed 8bpp is rejected: it needs a video_set_lut() hook this
 * board does not provide.
 */
static int tabor_gdf_index(const struct vesa_mode_info *vm)
{
	switch (vm->bits_per_pixel) {
	case 15:
		return GDF_15BIT_555RGB;
	case 16:
		return vm->green_mask_size == 5 ? GDF_15BIT_555RGB
						: GDF_16BIT_565RGB;
	case 24:
		return GDF_24BIT_888RGB;
	case 32:
		return GDF_32BIT_X888RGB;
	default:
		return -1;
	}
}

/*
 * Can cfb_console drive this mode? It needs a linear framebuffer, a pixel
 * format we can draw, and a scanline pitch with no padding: cfb_console derives
 * its line length from winSizeX * gdfBytesPP and offers no runtime pitch
 * override, so a padded mode would shear. Returns the GDF index, or -1.
 */
static int tabor_mode_usable(const struct vbe_mode_info *mi)
{
	const struct vesa_mode_info *vm = &mi->vesa;
	int gdf;

	if (!mi->valid || !(vm->mode_attributes & VBE_ATTR_LINEAR_FB))
		return -1;
	if (!vm->x_resolution || !vm->y_resolution || !vm->phys_base_ptr)
		return -1;

	gdf = tabor_gdf_index(vm);
	if (gdf < 0)
		return -1;

	if (vm->bytes_per_scanline !=
	    vm->x_resolution * ((vm->bits_per_pixel + 7) / 8))
		return -1;

	return gdf;
}

/*
 * Legacy VGA routing across PCI-to-PCI bridges.
 *
 * A card sitting behind a bridge only sees legacy VGA cycles if every bridge on
 * the path has VGA Enable set, and only sees its own I/O BAR if every bridge
 * forwards that address. U-Boot's legacy autoconfig (drivers/pci/pci_auto_old.c)
 * does neither: it never touches PCI_BRIDGE_CTL_VGA (only the driver-model path
 * in pci-uclass.c does), and it programs 4K-aligned I/O windows that cannot
 * cover the sub-64K ports the VBIOS uses - nor the card's I/O BAR after
 * atibios.c's PCI_fixupIObase() relocates it below 64K.
 *
 * This matters because it is not an exotic topology: every Vega 10 card carries
 * two onboard bridges (1022:1470 -> 1022:1471) ahead of the GPU, so the GPU is
 * never directly attached however it is plugged in. Without this, the emulator's
 * first port access goes unclaimed and the resulting PCIe completion timeout
 * machine-checks the CPU until U-Boot gives up.
 *
 * With an RX Vega 64 on the X5000's PCIe1 the walk finds three type-1 headers
 * (the card's two bridges plus the PCIe1 root port at 00:00.0), each left by
 * autoconfig with VGA Enable clear and an I/O window of 0x11-0x11. That decodes
 * as a 4K window at 0x1000-0x1fff (low nibble 1 = 32-bit capable, upper nibble
 * = base/limit >> 12), which covers neither the VGA ports nor the relocated I/O
 * BAR, so without this code the POST faults at VIDEO_IO_OFFSET + 0x3c3 or
 * + 0xc300.
 *
 * So before POSTing, walk the path and widen each bridge; restore afterwards.
 * The failure mode is deliberately conservative: if the walk cannot account for
 * every hop, we skip the POST entirely and boot headless rather than POST blind
 * and take an unrecoverable machine check.
 */
#define TABOR_MAX_BRIDGES	4

struct tabor_bridge_save {
	pci_dev_t bdf;
	u16 command;
	u16 bridge_ctl;
	u16 io_base_upper;
	u16 io_limit_upper;
	u8 io_base;
	u8 io_limit;
};

static void tabor_bridge_restore(struct pci_controller *hose,
				 struct tabor_bridge_save *saved, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		struct tabor_bridge_save *s = &saved[i];

		pci_hose_write_config_word(hose, s->bdf, PCI_BRIDGE_CONTROL,
					   s->bridge_ctl);
		pci_hose_write_config_byte(hose, s->bdf, PCI_IO_BASE,
					   s->io_base);
		pci_hose_write_config_byte(hose, s->bdf, PCI_IO_LIMIT,
					   s->io_limit);
		pci_hose_write_config_word(hose, s->bdf, PCI_IO_BASE_UPPER16,
					   s->io_base_upper);
		pci_hose_write_config_word(hose, s->bdf, PCI_IO_LIMIT_UPPER16,
					   s->io_limit_upper);
		pci_hose_write_config_word(hose, s->bdf, PCI_COMMAND,
					   s->command);
	}
}

/* Find the bridge on an upstream bus whose secondary bus is `bus`, or -1. */
static pci_dev_t tabor_find_bridge_to(struct pci_controller *hose, int bus)
{
	int pbus;

	for (pbus = hose->first_busno; pbus < bus; pbus++) {
		pci_dev_t bdf;
		int found_multi = 0;

		for (bdf = PCI_BDF(pbus, 0, 0);
		     bdf < PCI_BDF(pbus + 1, 0, 0);
		     bdf += PCI_BDF(0, 0, 1)) {
			u8 hdr_type, sec;
			u16 vendor;

			if (!PCI_FUNC(bdf)) {
				pci_hose_read_config_byte(hose, bdf,
							  PCI_HEADER_TYPE,
							  &hdr_type);
				found_multi = hdr_type & 0x80;
			} else if (!found_multi) {
				continue;
			}

			pci_hose_read_config_word(hose, bdf, PCI_VENDOR_ID,
						  &vendor);
			if (vendor == 0xffff || !vendor)
				continue;

			pci_hose_read_config_byte(hose, bdf, PCI_HEADER_TYPE,
						  &hdr_type);
			if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
				continue;

			pci_hose_read_config_byte(hose, bdf, PCI_SECONDARY_BUS,
						  &sec);
			if (sec == bus)
				return bdf;
		}
	}
	return -1;
}

/*
 * Open legacy VGA + full 16-bit I/O forwarding on every bridge between the root
 * bus and `dev`. Returns the number of bridges programmed (0 for a directly
 * attached card such as an RX560), or -1 if the path could not be resolved -
 * in which case nothing is left modified and the caller must not POST.
 */
static int tabor_bridge_vga_enable(struct pci_controller *hose, pci_dev_t dev,
				   struct tabor_bridge_save *saved, int max)
{
	int bus = PCI_BUS(dev);
	int count = 0;

	while (bus > hose->first_busno) {
		struct tabor_bridge_save *s;
		pci_dev_t bridge;

		if (count >= max) {
			printf("video: VGA path to %02x:%02x.%x is deeper than "
			       "%d bridges; skipping POST\n", PCI_BUS(dev),
			       PCI_DEV(dev), PCI_FUNC(dev), max);
			tabor_bridge_restore(hose, saved, count);
			return -1;
		}

		bridge = tabor_find_bridge_to(hose, bus);
		if (bridge < 0) {
			printf("video: no bridge opens onto bus %02x; cannot "
			       "route legacy VGA to %02x:%02x.%x, skipping "
			       "POST\n", bus, PCI_BUS(dev), PCI_DEV(dev),
			       PCI_FUNC(dev));
			tabor_bridge_restore(hose, saved, count);
			return -1;
		}

		s = &saved[count];
		s->bdf = bridge;
		pci_hose_read_config_word(hose, bridge, PCI_COMMAND,
					  &s->command);
		pci_hose_read_config_word(hose, bridge, PCI_BRIDGE_CONTROL,
					  &s->bridge_ctl);
		pci_hose_read_config_byte(hose, bridge, PCI_IO_BASE,
					  &s->io_base);
		pci_hose_read_config_byte(hose, bridge, PCI_IO_LIMIT,
					  &s->io_limit);
		pci_hose_read_config_word(hose, bridge, PCI_IO_BASE_UPPER16,
					  &s->io_base_upper);
		pci_hose_read_config_word(hose, bridge, PCI_IO_LIMIT_UPPER16,
					  &s->io_limit_upper);

		/*
		 * Forward all of 0x0000-0xffff so the window covers both the
		 * VGA ports and wherever PCI_fixupIObase() parks the card's I/O
		 * BAR. The low nibble of IO_BASE/IO_LIMIT is read-only
		 * addressing capability, so only the top nibble is ours to set.
		 * VGA Enable additionally forwards the 0xa0000-0xbffff memory
		 * aperture, which the VBIOS POST also touches.
		 */
		pci_hose_write_config_byte(hose, bridge, PCI_IO_BASE, 0x00);
		pci_hose_write_config_byte(hose, bridge, PCI_IO_LIMIT, 0xf0);
		pci_hose_write_config_word(hose, bridge, PCI_IO_BASE_UPPER16,
					   0);
		pci_hose_write_config_word(hose, bridge, PCI_IO_LIMIT_UPPER16,
					   0);
		pci_hose_write_config_word(hose, bridge, PCI_BRIDGE_CONTROL,
					   s->bridge_ctl | PCI_BRIDGE_CTL_VGA);
		pci_hose_write_config_word(hose, bridge, PCI_COMMAND,
					   s->command | PCI_COMMAND_IO);

		printf("video: VGA routing via bridge %02x:%02x.%x "
		       "(was ctl 0x%04x, io 0x%02x-0x%02x)\n", PCI_BUS(bridge),
		       PCI_DEV(bridge), PCI_FUNC(bridge), s->bridge_ctl,
		       s->io_base, s->io_limit);

		count++;
		bus = PCI_BUS(bridge);
	}

	return count;
}

/* POST the card and set a VESA linear mode; fills *mi. See TABOR_POST_*. */
static int tabor_post_mode(struct pci_controller *hose, pci_dev_t dev, int mode,
			   struct vbe_mode_info *mi)
{
	struct tabor_bridge_save saved[TABOR_MAX_BRIDGES];
	BE_VGAInfo *vga = NULL;
	int nbridges, ret;

	memset(mi, 0, sizeof(*mi));

	nbridges = tabor_bridge_vga_enable(hose, dev, saved, ARRAY_SIZE(saved));
	if (nbridges < 0)
		return TABOR_POST_FAIL;

	if (biosemu_setup(dev, &vga)) {
		ret = TABOR_POST_FAIL;
	} else if (biosemu_run(dev, NULL, 0, vga, 1, mode, mi)) {
		/* clean_up=1: mi is filled before the emulator is torn down. */
		ret = TABOR_POST_FAIL;
	} else {
		/*
		 * A refused mode leaves valid == false although the POST itself
		 * succeeded, so the caller can go on to the next candidate.
		 */
		ret = mi->valid ? TABOR_POST_OK : TABOR_POST_NOMODE;
	}

	tabor_bridge_restore(hose, saved, nbridges);
	return ret;
}

static void tabor_print_mode(int mode, const struct vbe_mode_info *mi)
{
	const struct vesa_mode_info *vm = &mi->vesa;

	printf("  mode 0x%03x: %ux%u %ubpp, pitch %u, fb(bus) 0x%08x, "
	       "attrs 0x%04x, linear %s%s\n",
	       mode, vm->x_resolution, vm->y_resolution, vm->bits_per_pixel,
	       vm->bytes_per_scanline, vm->phys_base_ptr, vm->mode_attributes,
	       (vm->mode_attributes & VBE_ATTR_LINEAR_FB) ? "yes" : "no",
	       tabor_mode_usable(mi) < 0 ? "  [not usable by cfb_console]" : "");
}

/*
 * Jumper JP2 / GPIO2_31 "VGA_BIOS_ENABLE#" (Tabor TRM tables 4 and 23):
 * 0 = jumper fitted = U-Boot uses the VGA card, 1 = not fitted = serial only.
 * The official firmware reads this bit (GPIO2 data, CCSR 0xf108) before the VGA
 * bring-up. The pin is muxed as GPIO_2[31] by PMUXCR[IRQ_DEBUG1]. Set
 * 'vga_jumper=ignore' in the environment to POST regardless.
 */
static int tabor_vga_enabled(void)
{
	u32 gpdat = in_be32((void __iomem *)(CONFIG_SYS_CCSRBAR + 0xf108));
	const char *s = env_get("vga_jumper");

	if (!(gpdat & 1))
		return 1;
	if (s && !strcmp(s, "ignore"))
		return 1;
	printf("video: VGA disabled, jumper JP2 not fitted (GPIO2_31 = 1); "
	       "set vga_jumper=ignore to override\n");
	return 0;
}

/*
 * cfb_console hook. POST the card, pick a mode and return a GraphicDevice
 * pointing at the (bus->virt translated) linear framebuffer.
 */
void *video_hw_init(void)
{
	if (!tabor_vga_enabled())
		return NULL;

	static GraphicDevice gdev;
	struct pci_controller *hose = NULL;
	struct vbe_mode_info mi;
	pci_dev_t dev;
	u16 vendor = 0, device = 0;
	const char *s;
	void *fb;
	int i, ret, mode = -1, gdf = -1;

	dev = tabor_find_display(&hose, &vendor, &device);
	if (dev < 0) {
		printf("video: no AMD/ATI display device found on PCI\n");
		return NULL;
	}
	if (!tabor_hose_is_pcie1(hose))
		printf("video: warning: %04x:%04x is not on PCIe1; the VBIOS "
		       "emulator's I/O window is fixed there, so POST may "
		       "fail\n", vendor, device);

	s = env_get("vesamode");
	if (s) {
		mode = (int)simple_strtoul(s, NULL, 16);
		if (tabor_post_mode(hose, dev, mode, &mi) == TABOR_POST_OK) {
			gdf = tabor_mode_usable(&mi);
			if (gdf < 0) {
				/* Honour an explicit request even if imperfect. */
				gdf = tabor_gdf_index(&mi.vesa);
				if (gdf >= 0)
					printf("video: forced mode 0x%x is "
					       "padded or non-linear; the "
					       "console may shear\n", mode);
			}
		}
	} else {
		for (i = 0; i < (int)ARRAY_SIZE(tabor_vesa_modes); i++) {
			mode = tabor_vesa_modes[i];
			ret = tabor_post_mode(hose, dev, mode, &mi);
			if (ret == TABOR_POST_FAIL)
				break;	/* no usable VBIOS; retrying is futile */
			if (ret == TABOR_POST_OK) {
				gdf = tabor_mode_usable(&mi);
				if (gdf >= 0)
					break;
			}
		}
	}

	if (gdf < 0) {
		if (s)
			printf("video: forced mode 0x%x (env 'vesamode') "
			       "failed; unset it to auto-select\n", mode);
		else
			printf("video: %04x:%04x VBIOS POST / mode set "
			       "failed\n", vendor, device);
		return NULL;
	}

	/*
	 * phys_base_ptr is the framebuffer's PCI *bus* address (inside the
	 * PCIe1 bus window at 0xe0000000). The CPU reaches it through that
	 * controller's virtual window (CONFIG_SYS_PCIE1_MEM_VIRT), so translate
	 * bus -> virt via the hose regions and the board address map - the same
	 * path atibios.c uses to read the option ROM over BAR0.
	 */
	fb = pci_bus_to_virt(dev, mi.vesa.phys_base_ptr, PCI_REGION_MEM,
			     (unsigned long)mi.vesa.y_resolution *
			     mi.vesa.bytes_per_scanline, MAP_NOCACHE);
	if ((unsigned long)fb < CONFIG_SYS_PCIE1_MEM_VIRT) {
		printf("video: framebuffer bus 0x%08x did not translate into a "
		       "mapped PCIe window (got %p)\n",
		       mi.vesa.phys_base_ptr, fb);
		return NULL;
	}

	memset(&gdev, 0, sizeof(gdev));
	gdev.winSizeX = gdev.plnSizeX = mi.vesa.x_resolution;
	gdev.winSizeY = gdev.plnSizeY = mi.vesa.y_resolution;
	gdev.gdfBytesPP = (mi.vesa.bits_per_pixel + 7) / 8;
	gdev.gdfIndex = gdf;
	gdev.frameAdrs = (unsigned int)(unsigned long)fb;
	gdev.memSize = (unsigned int)mi.vesa.y_resolution *
		       mi.vesa.bytes_per_scanline;
	snprintf(gdev.modeIdent, sizeof(gdev.modeIdent),
		 "%04x:%04x %ux%u %ubpp", vendor, device,
		 mi.vesa.x_resolution, mi.vesa.y_resolution,
		 mi.vesa.bits_per_pixel);

	printf("video: %04x:%04x %ux%u %ubpp linear fb @ %p (VESA mode 0x%x)\n",
	       vendor, device, gdev.winSizeX, gdev.winSizeY,
	       mi.vesa.bits_per_pixel, fb, mode);
	return &gdev;
}

static int do_videoboot(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	struct pci_controller *hose = NULL;
	struct vbe_mode_info mi;
	pci_dev_t dev;
	u16 vendor = 0, device = 0;
	int i, vesa_mode = -1;

	dev = tabor_find_display(&hose, &vendor, &device);
	if (dev < 0) {
		printf("videoboot: no AMD/ATI display device found on PCI\n");
		return CMD_RET_FAILURE;
	}
	printf("videoboot: found %04x:%04x at %02x:%02x.%x%s\n", vendor, device,
	       PCI_BUS(dev), PCI_DEV(dev), PCI_FUNC(dev),
	       tabor_hose_is_pcie1(hose) ? "" : "  (not on PCIe1)");

	if (argc > 1 && !strcmp(argv[1], "scan")) {
		printf("videoboot: probing %d candidate modes "
		       "(one VBIOS POST each)...\n",
		       (int)ARRAY_SIZE(tabor_vesa_modes));
		for (i = 0; i < (int)ARRAY_SIZE(tabor_vesa_modes); i++) {
			int mode = tabor_vesa_modes[i];
			int ret = tabor_post_mode(hose, dev, mode, &mi);

			if (ret == TABOR_POST_FAIL) {
				printf("videoboot: POST failed (missing/invalid"
				       " VBIOS, or emulator error)\n");
				return CMD_RET_FAILURE;
			}
			if (ret == TABOR_POST_NOMODE)
				printf("  mode 0x%03x: refused by the VBIOS\n",
				       mode);
			else
				tabor_print_mode(mode, &mi);
		}
		return CMD_RET_SUCCESS;
	}

	if (argc > 1)
		vesa_mode = (int)simple_strtoul(argv[1], NULL, 16);

	if (vesa_mode == -1) {
		struct tabor_bridge_save saved[TABOR_MAX_BRIDGES];
		int nbridges, ok;

		printf("videoboot: POST only (no mode set)...\n");
		nbridges = tabor_bridge_vga_enable(hose, dev, saved,
						   ARRAY_SIZE(saved));
		if (nbridges < 0)
			return CMD_RET_FAILURE;
		ok = BootVideoCardBIOS(dev, NULL, 1);
		tabor_bridge_restore(hose, saved, nbridges);
		if (!ok) {
			printf("videoboot: POST failed "
			       "(missing/invalid VBIOS, or emulator error)\n");
			return CMD_RET_FAILURE;
		}
		printf("videoboot: POST OK\n");
		return CMD_RET_SUCCESS;
	} else {
		printf("videoboot: POST + set VESA mode 0x%x...\n", vesa_mode);
		if (tabor_post_mode(hose, dev, vesa_mode, &mi) != TABOR_POST_OK) {
			printf("videoboot: mode 0x%x failed/not valid\n",
			       vesa_mode);
			return CMD_RET_FAILURE;
		}
		printf("videoboot: mode 0x%x OK:\n", vesa_mode);
		tabor_print_mode(vesa_mode, &mi);
		return CMD_RET_SUCCESS;
	}
}

U_BOOT_CMD(
	videoboot, 2, 0, do_videoboot,
	"POST the Radeon VBIOS (x86 emulator), optionally set a VESA mode",
	"\n"
	"    - POST the AMD/ATI card's VBIOS only (smoke test)\n"
	"videoboot scan\n"
	"    - POST + try each candidate VESA mode and print what the card\n"
	"      reports, so a new card's usable modes can be seen\n"
	"videoboot <mode>\n"
	"    - POST + set VESA linear <mode> (hex, e.g. 0x117) and print the\n"
	"      framebuffer geometry/base\n"
	"\n"
	"The environment variable 'vesamode' (hex) forces the mode chosen at\n"
	"boot by the framebuffer console; unset it to auto-select."
);
