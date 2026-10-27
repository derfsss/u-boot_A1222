# tools-a1222

Host-side Python tools for AmigaOne A1222 (Tabor) SD-card images, and a developer tool for the
AmigaOS boot handoff. The card layout is described in `docs/sd-card-layout.md`.

| Tool | What it does |
|---|---|
| `mkimage.py` | Assembles an SD-card image in the official A1222 layout: eSDHC boot header and placeholder MBR in sector 0, `u-boot-with-spl.bin` at `0x400`, optional environment at `0x600000`, optional boot-splash data image (not included) at `0x900000`. Sector 0 matches the official card's. The image contains only what the tool generates and the files you pass. Images made with it boot an A1222. |
| `inspect_image.py` | Read-only summary of any A1222 image: MBR, eSDHC boot header and config pairs, nonzero extents, version strings, environment (CRC-checked), any ELF image at `0x800000` (where an official card keeps A-Eon's AmigaBoot), first BMP. |
| `parse_env.py` | Read-only listing of the environment block of an image (MACs masked). |
| `aos4-probe/` | A stand-in AmigaOS kernel loader that prints, on the serial port, what a boot loader hands the kernel. A developer tool, built in the A1222 container; see its README. |

Run the Python tools with `python3 -I`. Windows Python needs `C:/...` style paths, not `/c/...`.

The P1022 has no PBL, so the X5000 port's `build_spl_img.sh` / `mksdimage.sh` do not apply.
