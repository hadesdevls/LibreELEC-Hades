# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: diagnostics of a start, saved to the boot partition while it has a file named DIAG-ON.TXT.
# boot/DIAG-OFF.TXT is that switch as an image carries it (scripts/image, scripts/mkimage).

PKG_NAME="hades-diag"
PKG_VERSION="30"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_LONGDESC="Diagnostics of a start on real hardware (displays, graphics drivers, logs), every file through a filter that puts markers in the place of addresses, serial numbers and other identifiers and takes credentials and the names of media out, written to the folder hades-diag of the boot partition while that partition has a file named DIAG-ON.TXT. Each start has a folder of its own there, numbered on from the start before it, also across an update of the system, and the last eight are kept. They read what the system is doing; once per start, for a display that is plugged in and not seen, they also ask the empty HDMI and DisplayPort outputs again, list the PCI devices (which wakes a graphics card that sleeps) and, where no display is seen on any socket, leave the kernel's display messages turned up for about three minutes. While the switch is on, the journal of each start is kept in the storage, and the first snapshot of the next start saves it (previous-boot, or previous-boot/NONE.txt, which says why there is none); turned off, that journal is deleted. Until the journal of a start is in the storage, the kernel log of the start is copied there every second (the init, then early-log.sh through hades-diag-early.service, which only the init has systemd start), so that a start that hangs in its first seconds leaves its log too (previous-boot/early-kernel-log.txt.gz). While it is on, the boot menus also start the kernel with initcall_debug=1 (kernel-line.sh, through internal-disks), so that the journal of a start that hangs while drivers start names the driver that did not return; previous-boot/not-returned.txt (and early-not-returned.txt, from the copy of the kernel log) lists the drivers that the log shows starting and not returned by its last line (not-returned.awk). A start of the boot entry Installer, whose storage is in RAM, keeps its journal and the installer's log in its folder on the boot partition as it goes (installer: every 2 seconds, 8 MiB and 1 MiB at most). Nothing on that partition is run. From the settings, the switch is turned on and off (switch.sh), and a report is made at once as one zip file in /storage/logfiles (report.sh now); the share Logfiles gives the log that createlog collects, each file through the filter (report.sh share). Nothing of those is written without the filter either. Also the check of the output for Dolby Vision (hades-output-check), made only when it is asked for in the settings: it measures with the graphics card's own CRC of its output whether the card passes the bytes of the Dolby Vision transport through unchanged."
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
  cp ${PKG_DIR}/scripts/start.sh ${PKG_DIR}/scripts/collect.sh ${PKG_DIR}/scripts/watch-dv.sh ${PKG_DIR}/scripts/early-log.sh \
     ${PKG_DIR}/scripts/shutdown.sh ${PKG_DIR}/scripts/redact.sh ${PKG_DIR}/scripts/redact-ids.awk ${PKG_DIR}/scripts/not-returned.awk ${PKG_DIR}/scripts/edid-path.awk \
     ${PKG_DIR}/scripts/redact.sed ${PKG_DIR}/scripts/folder.sh ${PKG_DIR}/scripts/installer.sh \
     ${PKG_DIR}/scripts/README.txt ${PKG_DIR}/scripts/LEEME.txt ${PKG_DIR}/scripts/NO-ROOM.txt \
     ${INSTALL}/usr/lib/hades-diag
  # (the report made on demand and the log of the share Logfiles; the switch of the settings, which makes the
  # switch file from the copy here where the boot partition has none, and the kernel line of the boot menus that
  # follows it)
  cp ${PKG_DIR}/scripts/report.sh ${PKG_DIR}/scripts/switch.sh ${PKG_DIR}/scripts/kernel-line.sh ${PKG_DIR}/boot/DIAG-OFF.TXT \
     ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/*.sh
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/redact.sed ${INSTALL}/usr/lib/hades-diag/*.awk ${INSTALL}/usr/lib/hades-diag/*.txt \
    ${INSTALL}/usr/lib/hades-diag/DIAG-OFF.TXT
  # (read by the scripts, not run)
  chmod 0644 ${INSTALL}/usr/lib/hades-diag/folder.sh
  # (each sleep says in the kernel log what the firmware offers, what may wake the PC, how it went and what woke it:
  # systemd-sleep runs it before and after each one; collect.sh runs it with "state")
  cp ${PKG_DIR}/scripts/sleep.sh ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/sleep.sh
  # (the network access in effect, which the report says: collect.sh)
  cp ${PKG_DIR}/scripts/net-access.sh ${INSTALL}/usr/lib/hades-diag
  chmod 0755 ${INSTALL}/usr/lib/hades-diag/net-access.sh
  mkdir -p ${INSTALL}/usr/lib/systemd/system-sleep
  cp ${PKG_DIR}/scripts/sleep.sh ${INSTALL}/usr/lib/systemd/system-sleep/hades-sleep
  chmod 0755 ${INSTALL}/usr/lib/systemd/system-sleep/hades-sleep
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
  # (at every start but the installer's: the kernel line of the boot menus says what the switch says, initcall_debug)
  enable_service hades-diag-kernel-line.service
}
