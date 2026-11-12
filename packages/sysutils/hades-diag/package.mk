# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: diagnostics of a start, saved to the boot partition while it has a file named DIAG-ON.TXT.
# boot/DIAG-OFF.TXT is that switch as an image carries it (scripts/image, scripts/mkimage).

PKG_NAME="hades-diag"
PKG_VERSION="21"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_LONGDESC="Diagnostics of a start on real hardware (displays, graphics drivers, logs), every file through a filter that puts markers in the place of addresses, serial numbers and other identifiers and takes credentials and the names of media out, written to the folder hades-diag of the boot partition while that partition has a file named DIAG-ON.TXT. Each start has a folder of its own there, numbered on from the start before it, also across an update of the system, and the last eight are kept. They read what the system is doing; once per start, for a display that is plugged in and not seen, they also ask the empty HDMI and DisplayPort outputs again, list the PCI devices (which wakes a graphics card that sleeps) and, where no display is seen on any socket, leave the kernel's display messages turned up for about three minutes. While the switch is on, the journal of each start is kept in the storage, and the first snapshot of the next start saves it (previous-boot); turned off, that journal is deleted. A start of the boot entry Installer, whose storage is in RAM, keeps its journal and the installer's log in its folder on the boot partition as it goes (installer: every 2 seconds, 8 MiB and 1 MiB at most). Nothing on that partition is run. From the settings, the switch is turned on and off (switch.sh), and a report is made at once as one zip file in /storage/logfiles (report.sh now); the share Logfiles gives the log that createlog collects, each file through the filter (report.sh share). Nothing of those is written without the filter either. Also the check of the output for Dolby Vision (hades-output-check), made only when it is asked for in the settings: it measures with the graphics card's own CRC of its output whether the card passes the bytes of the Dolby Vision transport through unchanged."
PKG_DEPENDS_TARGET="toolchain libdrm"
PKG_TOOLCHAIN="manual"

make_target() {
  # the check of the output (src/output-check.h says what it measures); src/output-check-test.c is its test on the
  # build host, not part of the image
  ${CC} ${CFLAGS} -std=gnu11 -Wall -Wextra $(${PKG_CONFIG} --cflags libdrm) -o ${PKG_BUILD}/hades-output-check \
    ${PKG_DIR}/src/output-check.c ${PKG_DIR}/src/output-check-analysis.c ${LDFLAGS} $(${PKG_CONFIG} --libs libdrm) -lm ||
    die "hades-diag: hades-output-check did not compile"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/hades-diag
  cp ${PKG_DIR}/scripts/start.sh ${PKG_DIR}/scripts/collect.sh ${PKG_DIR}/scripts/watch-dv.sh \
     ${PKG_DIR}/scripts/shutdown.sh ${PKG_DIR}/scripts/redact.sh ${PKG_DIR}/scripts/redact-ids.awk \
     ${PKG_DIR}/scripts/redact.sed ${PKG_DIR}/scripts/folder.sh ${PKG_DIR}/scripts/installer.sh \
     ${PKG_DIR}/scripts/README.txt ${PKG_DIR}/scripts/LEEME.txt ${PKG_DIR}/scripts/NO-ROOM.txt \
     ${INSTALL}/usr/lib/hades-diag
  # (the report made on demand and the log of the share Logfiles; the switch of the settings, which makes the
  # switch file from the copy here where the boot partition has none)
  cp ${PKG_DIR}/scripts/report.sh ${PKG_DIR}/scripts/switch.sh ${PKG_DIR}/boot/DIAG-OFF.TXT ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/*.sh
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/redact.sed ${INSTALL}/usr/lib/hades-diag/redact-ids.awk ${INSTALL}/usr/lib/hades-diag/*.txt \
    ${INSTALL}/usr/lib/hades-diag/DIAG-OFF.TXT
  # (read by the scripts, not run)
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/folder.sh
}

post_makeinstall_target() {
  # (the copy to a USB drive, which the settings and hades-diag-usb@.service run)
  cp ${PKG_DIR}/scripts/to-usb.sh ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/to-usb.sh
  # (the check of the output, which the settings ask for and hades-output-check.service runs before Kodi)
  cp ${PKG_DIR}/scripts/output-check.sh ${PKG_BUILD}/hades-output-check ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/output-check.sh ${INSTALL}/usr/lib/hades-diag/hades-output-check
}

post_install() {
  enable_service hades-diag.service
  # (a start of the boot entry "Installer": installer.sh)
  enable_service hades-diag-installer.service
  # (started with Kodi only when the settings asked for the check: its condition)
  enable_service hades-output-check.service
}
