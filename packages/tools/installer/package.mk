# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2016-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="installer"
PKG_VERSION="1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="http://libreelec.tv/"
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain busybox newt parted e2fsprogs syslinux grub"
PKG_LONGDESC="LibreELEC.tv Install manager to install the system on any disk"
PKG_TOOLCHAIN="manual"

post_install() {
  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_DIR}/scripts/installer ${INSTALL}/usr/bin
    sed -e "s/@DISTRONAME@/${DISTRONAME}/g" \
        -i  ${INSTALL}/usr/bin/installer

  # Hades: the boot menus of the installed system, from those of the medium
  mkdir -p ${INSTALL}/usr/lib/libreelec
    cp ${PKG_DIR}/scripts/installer-bootmenus ${INSTALL}/usr/lib/libreelec
    chmod 0755 ${INSTALL}/usr/lib/libreelec/installer-bootmenus

  mkdir -p ${INSTALL}/etc
    find_file_path config/installer.conf
    cp ${FOUND_PATH} ${INSTALL}/etc
    sed -e "s/@SYSTEM_SIZE@/${SYSTEM_SIZE}/g" \
        -e "s/@SYSTEM_PART_START@/${SYSTEM_PART_START}/g" \
        -e "s/@SYSLINUX_PARAMETERS@/${SYSLINUX_PARAMETERS}/g" \
        -i ${INSTALL}/etc/installer.conf

  enable_service installer.service
}
