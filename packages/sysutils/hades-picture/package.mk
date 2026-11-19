# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
# Hades: the picture of the start, and the notices that the start of Kodi lays over it, on every display at the size
# of that display (src/hades-picture.c says how and why), from the moment the graphics driver has its frame buffer
# until Kodi shows its interface.

PKG_NAME="hades-picture"
PKG_VERSION="1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain libdrm libpng"
PKG_DEPENDS_INIT="toolchain libdrm libpng"
PKG_LONGDESC="The picture of the start of LibreELEC Hades on every display at its own size and in its middle, drawn again once the graphics driver has its frame buffer (the one of the firmware is gone then), and the notices of the start of Kodi (one display; Kodi could not start) laid over it. Where displays of different sizes are on, each gets a picture of its own size through DRM, in the mode it has (an atomic commit that may not change a mode), and the program lets go of the displays before Kodi starts, its pictures staying until Kodi shows its own; else the picture goes into the console's frame buffer, as ply-image draws it."
PKG_TOOLCHAIN="manual"

make_target() {
  # (src/picture-test.c is its test on the build host, tools/regress/boot; not part of the image. libpng is built as a
  # static library only, so it comes with what it needs: zlib and libm)
  ${CC} ${CFLAGS} -std=gnu11 -Wall -Wextra $(${PKG_CONFIG} --cflags libdrm libpng) -o ${PKG_BUILD}/hades-picture \
    ${PKG_DIR}/src/hades-picture.c ${PKG_DIR}/src/picture.c ${LDFLAGS} $(${PKG_CONFIG} --libs libdrm) \
    $(${PKG_CONFIG} --libs --static libpng) || die "hades-picture: it did not compile"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin ${INSTALL}/usr/lib/hades-picture ${INSTALL}/usr/share/splash
  cp ${PKG_BUILD}/hades-picture ${INSTALL}/usr/bin
  chmod 0755 ${INSTALL}/usr/bin/hades-picture
  cp ${PKG_DIR}/scripts/boot-picture ${INSTALL}/usr/lib/hades-picture
  chmod 0755 ${INSTALL}/usr/lib/hades-picture/boot-picture
  # the splash screens of the start, which the initramfs has (plymouth-lite), here for the system after it
  find_file_path "splash/splash-*.png" && cp ${FOUND_PATH} ${INSTALL}/usr/share/splash
  ls ${INSTALL}/usr/share/splash/splash-1080.png >/dev/null || die "hades-picture: no splash-1080.png"
}

# The init of the initramfs draws the picture of the start with it (packages/sysutils/busybox/scripts/init, load_splash),
# before the system is there: libpng and zlib inside it, and libdrm, which needs only the C library, next to it (it is
# made as a shared library only). The splash screens are in the initramfs already (plymouth-lite).
make_init() {
  ${CC} ${CFLAGS} -std=gnu11 -Wall -Wextra $(${PKG_CONFIG} --cflags libdrm libpng) -o ${PKG_BUILD}/hades-picture-init \
    ${PKG_DIR}/src/hades-picture.c ${PKG_DIR}/src/picture.c ${LDFLAGS} $(${PKG_CONFIG} --libs libdrm) \
    -Wl,-Bstatic -lpng16 -lz -Wl,-Bdynamic -lm || die "hades-picture: it did not compile for the initramfs"
}

makeinstall_init() {
  mkdir -p ${INSTALL}/usr/bin ${INSTALL}/usr/lib
  cp ${PKG_BUILD}/hades-picture-init ${INSTALL}/usr/bin/hades-picture
  chmod 0755 ${INSTALL}/usr/bin/hades-picture
  # (as the system has it: what the recipe of libdrm installs)
  cp -P $(get_install_dir libdrm)/usr/lib/libdrm.so.2 $(get_install_dir libdrm)/usr/lib/libdrm.so.2.* ${INSTALL}/usr/lib
}

post_install() {
  enable_service hades-picture.socket
  enable_service hades-boot-picture.service
}
