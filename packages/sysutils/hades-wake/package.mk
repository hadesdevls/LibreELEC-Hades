# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

PKG_NAME="hades-wake"
PKG_VERSION="1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="What may wake the PC from sleep, by the setting \"Wake from sleep\" of the LibreELEC settings (System > Sleep): by default the receivers of remote controls (an IR receiver of rc-core, the flirc, a USB receiver of media and system keys with no mouse and no game controller) besides the keyboards that the kernel arms by itself; or also every keyboard; or only the power button. Mice and game controllers are never armed. Set at each start and when a device is plugged in (udev), again after each sleep, and at once when the setting changes; each change is said in the kernel log (hades-wake:)."
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/hades-wake
  cp ${PKG_DIR}/scripts/wake.sh ${INSTALL}/usr/lib/hades-wake
  chmod 0755 ${INSTALL}/usr/lib/hades-wake/wake.sh
}
