# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# Hades: a start with UEFI Secure Boot, which the installer for Windows sets up when its box "Start with Secure Boot"
# is ticked (off by default; without it nothing here is used). shim and MokManager of Debian 13 (shim-signed,
# shim-helpers-signed) go to the boot partition of the image (/secureboot, scripts/mkimage) and to the system
# (/usr/share/hades-secureboot), where an update in place takes them from. hades-sb-sign signs GRUB and the kernel with
# the key of the computer, which lives in the key slot of the storage (/storage/.hades-secureboot/key); the init of the
# system that applies an update in place runs the one of the system that runs, never the update's, and only for an
# update that the update channel of that system signed (hades_sb_proof and hades_sb_update in the init).

PKG_NAME="hades-secureboot"
PKG_VERSION="1"
PKG_ARCH="x86_64"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain shim-signed shim-helpers-signed Python3 openssl"
PKG_LONGDESC="A start with UEFI Secure Boot: shim and MokManager of Debian 13, and the signature of GRUB and the kernel with the key of the computer, which an update in place makes again."
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/hades-secureboot
  cp ${PKG_DIR}/scripts/hades-sb-sign ${INSTALL}/usr/lib/hades-secureboot/hades-sb-sign
  chmod 0755 ${INSTALL}/usr/lib/hades-secureboot/hades-sb-sign
}
