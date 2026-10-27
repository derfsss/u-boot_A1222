# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2026 Richard Gibbs
#
# Cross-build environment for AmigaOne A1222 "Tabor" (NXP/Freescale QorIQ P1022,
# dual e500v2) U-Boot. See docs/building.md.
#
# Target: U-Boot v2021.04, mpc85xx / e500v2.
# Toolchain: powerpc-linux-gnu- (Debian bullseye => GCC 10, a close era match to the 2021.04
# tree). U-Boot is freestanding (-ffreestanding), so the glibc target prefix is fine; nothing
# links against glibc. U-Boot's own config.mk selects -mcpu=8548 for e500v2.
#
# Build:  docker build -t a1222-uboot-builder docker-a1222/
# Use (mount the source tree at /src; out-of-tree build keeps objects off the slow bind mount):
#   docker run --rm -v <repo>:/src a1222-uboot-builder bash -lc \
#     "make O=/tmp/obj Tabor_SDCARD_defconfig && make O=/tmp/obj -j\$(nproc)"
FROM debian:bullseye-slim

ENV DEBIAN_FRONTEND=noninteractive

# Bullseye has left the live mirrors: use the Debian archive so the image stays rebuildable.
RUN printf 'deb http://archive.debian.org/debian bullseye main\ndeb http://archive.debian.org/debian-security bullseye-security main\n' > /etc/apt/sources.list \
    && printf 'Acquire::Check-Valid-Until "false";\n' > /etc/apt/apt.conf.d/99archive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-powerpc-linux-gnu \
        make \
        bison \
        flex \
        bc \
        libssl-dev \
        python3 \
        python3-dev \
        python3-setuptools \
        swig \
        device-tree-compiler \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# U-Boot picks these up automatically; override on the command line if needed.
ENV CROSS_COMPILE=powerpc-linux-gnu-
ENV ARCH=powerpc

WORKDIR /src
CMD ["bash"]
