# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2020-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="wait-time-sync"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-only"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="make:host gcc:host"
PKG_LONGDESC="A simple tool and systemd service to wait until NTP time is synced"

post_makeinstall_target() {
  # Hades: the wait of "Wait for network before starting Kodi" (kodi-waitonnetwork.service)
  mkdir -p ${INSTALL}/usr/lib/libreelec
  cp ${PKG_DIR}/scripts/network-wait ${INSTALL}/usr/lib/libreelec
  chmod 0755 ${INSTALL}/usr/lib/libreelec/network-wait
}

post_install() {
  enable_service wait-time-sync.service
}
