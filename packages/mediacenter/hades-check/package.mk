# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades

PKG_NAME="hades-check"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/hadesdevls/LibreELEC-Hades"
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain Python3:host x265:host ffmpeg:host"
PKG_LONGDESC="The clips of Check this setup: the Hades check card in SDR, HDR10, HLG and Dolby Vision, and its sounds, made at build time."
PKG_TOOLCHAIN="manual"

# The clips are made here, at build time, by the scripts of clips/ from patterns of their own: nothing of
# a third party goes in (the card, its font and the sounds are made by these scripts). x265 and FFmpeg run
# on the build host only (x265:host, ffmpeg:host, outside the toolchain's PATH); no encoder goes into the
# image. The host tests of the clips are in tests/ (they do not run in the build).
HADES_CHECK_TOOLS="${TOOLCHAIN}/lib/hades-check/bin"

make_target() {
  case "${PKG_BUILD}" in
    */hades-check-*) rm -rf "${PKG_BUILD}/clips" ;;
    *) die "hades-check: unexpected build folder ${PKG_BUILD}" ;;
  esac
  sh ${PKG_DIR}/clips/make_clips.sh \
    --x265 ${HADES_CHECK_TOOLS}/x265 \
    --ffmpeg ${HADES_CHECK_TOOLS}/ffmpeg \
    --ffprobe ${HADES_CHECK_TOOLS}/ffprobe \
    --python ${TOOLCHAIN}/bin/python3 \
    --out ${PKG_BUILD}/clips
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/hades/check
  cp ${PKG_BUILD}/clips/* ${INSTALL}/usr/share/hades/check/
  # what is installed is what SHA256SUMS lists, file by file, and nothing else
  (cd ${INSTALL}/usr/share/hades/check && sha256sum --check --quiet --strict SHA256SUMS) ||
    die "hades-check: the installed clips do not match their SHA256SUMS"
  [ "$(ls ${INSTALL}/usr/share/hades/check | grep -vc '^SHA256SUMS$')" = "$(wc -l <${INSTALL}/usr/share/hades/check/SHA256SUMS)" ] ||
    die "hades-check: a file is installed that SHA256SUMS does not list"
}
