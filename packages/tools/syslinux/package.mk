# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="syslinux"
PKG_VERSION="6.03"
PKG_SHA256="26d3986d2bea109d5dc0e4f8c4822a459276cf021125e8c9f23c3cca5d8c850e"
PKG_ARCH="x86_64"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="http://syslinux.zytor.com/"
PKG_URL="http://www.kernel.org/pub/linux/utils/boot/${PKG_NAME}/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_HOST="util-linux:host"
PKG_DEPENDS_TARGET="toolchain util-linux e2fsprogs syslinux:host"
PKG_LONGDESC="The SYSLINUX project covers lightweight linux bootloaders."

pre_configure_target() {
  PKG_MAKE_OPTS_TARGET="CC=${CC} AR=${AR} RANLIB=${RANLIB} installer"

  # Unset all compiler FLAGS
  unset CFLAGS
  unset CPPFLAGS
  unset CXXFLAGS
  unset LDFLAGS
}

pre_build_target() {
  mkdir -p ${PKG_BUILD}/.${TARGET_NAME}
  cp -RP ${PKG_BUILD}/* ${PKG_BUILD}/.${TARGET_NAME}
}

pre_build_host() {
  mkdir -p ${PKG_BUILD}/.${HOST_NAME}
  cp -RP ${PKG_BUILD}/* ${PKG_BUILD}/.${HOST_NAME}
}

pre_make_target() {
  cd .${TARGET_NAME}
}

pre_make_host() {
  cd .${HOST_NAME}
}

make_host() {
  make CC=${CC} \
       AR=${AR} \
       RANLIB=${RANLIB} \
       CFLAGS="-I${TOOLCHAIN}/include -I${PKG_BUILD}/libinstaller -I${PKG_BUILD}/libfat -I${PKG_BUILD}/bios -I${PKG_BUILD}/utils -fomit-frame-pointer -D_FILE_OFFSET_BITS=64" \
       LDFLAGS="-L${TOOLCHAIN}/lib" \
       installer
  hades_make_mbr
}

# Hades: mbr.bin and gptmbr.bin (the code of the master boot record that the image and the installer of
# the image write) made from their source, in place of the ones that the release ships built: with
# syslinux-9001 they come out byte for byte the same (assembly; checked with scripts/build). The rest of
# bios/ (ldlinux.sys, ldlinux.c32, the modules) stays as the release ships it: built again it is not the
# same (another compiler), and it is what boots.
# Built without the flags that the build exports for its own programs, as the target stage of this recipe
# builds all of syslinux: in the host stage they are gcc options (-Wl,-rpath, -march=native), and
# mbr/Makefile hands LDFLAGS to ld itself. In a subshell, so that nothing else loses them.
hades_make_mbr() {
  rm -rf bios/mbr
  (
    unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
    make bios BSUBDIRS=mbr ISUBDIRS= ITARGET= BTARGET= CC=${CC}
  )
}

post_make_target() {
  hades_make_mbr
}

makeinstall_host() {
  mkdir -p ${TOOLCHAIN}/bin
    cp bios/linux/syslinux ${TOOLCHAIN}/bin
    cp bios/mtools/syslinux ${TOOLCHAIN}/bin/syslinux.mtools

  mkdir -p ${TOOLCHAIN}/share/syslinux
    cp bios/mbr/mbr.bin ${TOOLCHAIN}/share/syslinux
    cp bios/mbr/gptmbr.bin ${TOOLCHAIN}/share/syslinux
    cp efi64/efi/syslinux.efi ${TOOLCHAIN}/share/syslinux/bootx64.efi
    cp efi64/com32/elflink/ldlinux/ldlinux.e64  ${TOOLCHAIN}/share/syslinux

  # Hades: the BIOS menu with the look of the build (scripts/mkimage puts it on the boot partition):
  # vesamenu.c32 and the two libraries it loads, from the same release as ldlinux.c32 (they must match
  # it), and theme/hades (menu.cfg and its background, made by make-boot-theme.py)
  rm -rf ${TOOLCHAIN}/share/syslinux/hades
  mkdir -p ${TOOLCHAIN}/share/syslinux/hades
    cp bios/com32/menu/vesamenu.c32 bios/com32/lib/libcom32.c32 bios/com32/libutil/libutil.c32 \
       ${TOOLCHAIN}/share/syslinux/hades
    cp ${PKG_DIR}/theme/hades/* ${TOOLCHAIN}/share/syslinux/hades
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
    cp bios/linux/syslinux ${INSTALL}/usr/bin

  ${STRIP} ${INSTALL}/usr/bin/syslinux

  mkdir -p ${INSTALL}/usr/share/syslinux
    cp bios/mbr/mbr.bin ${INSTALL}/usr/share/syslinux
    cp bios/mbr/gptmbr.bin ${INSTALL}/usr/share/syslinux
    cp efi64/efi/syslinux.efi ${INSTALL}/usr/share/syslinux/bootx64.efi
    cp efi64/com32/elflink/ldlinux/ldlinux.e64  ${INSTALL}/usr/share/syslinux
}
