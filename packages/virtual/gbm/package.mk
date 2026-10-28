# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="gbm"
PKG_VERSION=""
PKG_LICENSE="GPL-2.0-only"
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain"
PKG_SECTION="virtual"
PKG_LONGDESC="Base packages for running on GBM"

# NVIDIA drivers for Linux
if listcontains "${GRAPHIC_DRIVERS}" "nvidia-ng"; then
  # Hades: and NVIDIA's 580 driver for Maxwell, Pascal and Volta, chosen at boot
  PKG_DEPENDS_TARGET+=" nvidia nvidia-legacy"
fi
