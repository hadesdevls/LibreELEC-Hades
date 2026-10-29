# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2016-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="grub"
PKG_VERSION="2.16"
PKG_SHA256="f0db0104927df0b9a48bc41b735c702936190d17eb2d73d3018fa77474a0cabe"
PKG_ARCH="x86_64"
PKG_LICENSE="GPL-3.0-or-later"
PKG_SITE="https://www.gnu.org/software/grub/index.html"
PKG_URL="https://gitlab.freedesktop.org/api/v4/projects/26558/packages/generic/source-assets/${PKG_NAME}-${PKG_VERSION}/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_HOST="toolchain:host"
PKG_DEPENDS_TARGET="toolchain flex freetype:host gettext:host grub:host"
PKG_LONGDESC="GRUB is a Multiboot boot loader."
PKG_TOOLCHAIN="autotools"
PKG_BUILD_FLAGS="-cfg-libs -cfg-libs:host"

pre_configure_host() {
  unset CFLAGS
  unset CPPFLAGS
  unset CXXFLAGS
  unset LDFLAGS
  unset CPP
}

pre_configure_target() {
  PKG_CONFIGURE_OPTS_TARGET="--target=i386-pc-linux \
                             --disable-nls \
                             --with-platform=efi"

  unset CFLAGS
  unset CPPFLAGS
  unset CXXFLAGS
  unset LDFLAGS
  unset CPP

  # configure requires explicit TARGET_PREFIX binaries when cross compiling.
  export TARGET_CC="${TARGET_PREFIX}gcc"
  export TARGET_OBJCOPY="${TARGET_PREFIX}objcopy"
  export TARGET_STRIP="${TARGET_PREFIX}strip"
  export TARGET_NM="${TARGET_PREFIX}nm"
  export TARGET_RANLIB="${TARGET_PREFIX}ranlib"
}

make_target() {
  make CC=${CC} \
       AR=${AR} \
       RANLIB=${RANLIB} \
       CFLAGS="-I${SYSROOT_PREFIX}/usr/include -fomit-frame-pointer -D_FILE_OFFSET_BITS=64" \
       LDFLAGS="-L${SYSROOT_PREFIX}/usr/lib"

  # Hades: GRUB for 64-bit UEFI, built from source out of tree with the same configure line
  # and only --target=x86_64-pc-linux; it replaces the prebuilt syslinux EFI loader as
  # bootx64.efi
  mkdir -p ${PKG_BUILD}/.x86_64-efi
  (
    cd ${PKG_BUILD}/.x86_64-efi
    ${PKG_BUILD}/configure ${TARGET_CONFIGURE_OPTS} --target=x86_64-pc-linux --disable-nls --with-platform=efi
    make CC=${CC} \
         AR=${AR} \
         RANLIB=${RANLIB} \
         CFLAGS="-I${SYSROOT_PREFIX}/usr/include -fomit-frame-pointer -D_FILE_OFFSET_BITS=64" \
         LDFLAGS="-L${SYSROOT_PREFIX}/usr/lib"
  )
}

makeinstall_target() {
  # Hades: part_msdos as well as part_gpt. The installer of the image (boot entry "Installer")
  # gives a disk an MBR partition table when it runs from a start in legacy BIOS and the disk is
  # under 2.2 TB, and puts this GRUB on it for UEFI; without part_msdos GRUB cannot tell the
  # partition it was started from, its prefix is the whole disk and it stops at its console.
  ${PKG_BUILD}/.${HOST_NAME}/grub-mkimage -d grub-core -o bootia32.efi -O i386-efi -p /EFI/BOOT \
    boot chain configfile ext2 fat linux search efi_gop \
    part_gpt part_msdos gzio gettext loadenv loadbios memrw

  ${PKG_BUILD}/.${HOST_NAME}/grub-mkimage -d ${PKG_BUILD}/.x86_64-efi/grub-core -o bootx64.efi -O x86_64-efi -p /EFI/BOOT \
    boot chain configfile ext2 fat linux search efi_gop \
    part_gpt part_msdos gzio gettext loadenv loadbios memrw

  mkdir -p ${INSTALL}/usr/share/grub
     cp -P bootia32.efi bootx64.efi ${INSTALL}/usr/share/grub

  mkdir -p ${TOOLCHAIN}/share/grub
     cp -P bootia32.efi bootx64.efi ${TOOLCHAIN}/share/grub
}
