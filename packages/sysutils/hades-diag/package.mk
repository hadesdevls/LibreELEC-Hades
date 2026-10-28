# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: diagnostics of a start, saved to the boot partition while it has a file named DIAG-ON.TXT.
# boot/DIAG-OFF.TXT is that switch as an image carries it (scripts/image, scripts/mkimage).

PKG_NAME="hades-diag"
PKG_VERSION="18"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_LONGDESC="Diagnostics of a start on real hardware (displays, graphics drivers, logs), every file through a filter that puts markers in the place of addresses, serial numbers and other identifiers and takes credentials and the names of media out, written to the folder hades-diag of the boot partition while that partition has a file named DIAG-ON.TXT. Each start has a folder of its own there, numbered on from the start before it, also across an update of the system, and the last eight are kept. They read what the system is doing; once per start, for a display that is plugged in and not seen, they also ask the empty HDMI and DisplayPort outputs again, list the PCI devices (which wakes a graphics card that sleeps) and, where no display is seen on any socket, leave the kernel's display messages turned up for about three minutes. Nothing on that partition is run."
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/hades-diag
  cp ${PKG_DIR}/scripts/start.sh ${PKG_DIR}/scripts/collect.sh ${PKG_DIR}/scripts/watch-dv.sh \
     ${PKG_DIR}/scripts/shutdown.sh ${PKG_DIR}/scripts/redact.sh ${PKG_DIR}/scripts/redact-ids.awk \
     ${PKG_DIR}/scripts/redact.sed ${PKG_DIR}/scripts/folder.sh \
     ${PKG_DIR}/scripts/README.txt ${PKG_DIR}/scripts/LEEME.txt ${PKG_DIR}/scripts/NO-ROOM.txt \
     ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/*.sh
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/redact.sed ${INSTALL}/usr/lib/hades-diag/redact-ids.awk ${INSTALL}/usr/lib/hades-diag/*.txt
  # (read by the scripts, not run)
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/folder.sh
}

post_makeinstall_target() {
  # (the copy to a USB drive, which the settings and hades-diag-usb@.service run)
  cp ${PKG_DIR}/scripts/to-usb.sh ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/to-usb.sh
}

post_install() {
  enable_service hades-diag.service
}
