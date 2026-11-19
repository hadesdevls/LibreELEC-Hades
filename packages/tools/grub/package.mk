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
  # Hades: with the line of Hades for its .sbat section (sbat.hades.csv.in), which configure appends to the sbat.csv
  # that it makes with GRUB's own line at the generation of this release (GLOBAL_SBAT_LEVEL of configure.ac)
  mkdir -p ${PKG_BUILD}/.x86_64-efi
  sed -e "s/@GRUB_VERSION@/${PKG_VERSION}/" ${PKG_DIR}/sbat.hades.csv.in >${PKG_BUILD}/sbat.hades.csv
  (
    cd ${PKG_BUILD}/.x86_64-efi
    ${PKG_BUILD}/configure ${TARGET_CONFIGURE_OPTS} --target=x86_64-pc-linux --disable-nls --with-platform=efi \
      --with-sbat-file=${PKG_BUILD}/sbat.hades.csv
    make CC=${CC} \
         AR=${AR} \
         RANLIB=${RANLIB} \
         CFLAGS="-I${SYSROOT_PREFIX}/usr/include -fomit-frame-pointer -D_FILE_OFFSET_BITS=64" \
         LDFLAGS="-L${SYSROOT_PREFIX}/usr/lib"
  )
}

makeinstall_target() {
  # Hades: test, font, gfxterm, gfxmenu, png, video_colors and bitmap_scale for the look of the boot menu, and
  # gfxterm_background for the picture of the start (background_image, in theme.cfg)
  # (theme/, which scripts/mkimage puts next to grub.cfg; grub-9000 says the mode that gfxterm got).
  # Hades: part_msdos as well as part_gpt. The installer of the image (boot entry "Installer")
  # gives a disk an MBR partition table when it runs from a start in legacy BIOS and the disk is
  # under 2.2 TB, and puts this GRUB on it for UEFI; without part_msdos GRUB cannot tell the
  # partition it was started from, its prefix is the whole disk and it stops at its console.
  ${PKG_BUILD}/.${HOST_NAME}/grub-mkimage -d grub-core -o bootia32.efi -O i386-efi -p /EFI/BOOT \
    boot chain configfile ext2 fat linux search efi_gop \
    part_gpt part_msdos gzio gettext loadenv loadbios memrw \
    test font gfxterm gfxmenu png video_colors bitmap_scale gfxterm_background

  # Hades: the fonts of the boot menu inside the 64-bit GRUB as well, in a memdisk: a tar that grub-mkimage puts into
  # the program, which GRUB reads as the device (memdisk). With UEFI Secure Boot GRUB is locked down and refuses a font
  # from a disk ("prohibited by secure boot policy": since the fixes for CVE-2022-2601 and CVE-2022-3775 the shim_lock
  # verifier, grub-core/kern/efi/sb.c, has kernels checked, lets configs, themes and pictures through, and refuses the
  # kinds of file that it has no rule for, fonts among them), so the menu fell back to text. A file of the memdisk is part of the program
  # that shim verified, and GRUB reads it without asking the verifiers (grub-core/kern/verifiers.c,
  # grub_verifiers_open); font.c looks in (memdisk) itself for that reason. Nothing of GRUB's checks is changed. The
  # theme picks the copy (theme.cfg): the memdisk only when GRUB runs locked down (lockdown=y, which GRUB sets when it
  # starts with Secure Boot: kern/efi/init.c), the fonts of EFI/BOOT/hades/fonts otherwise, as before. The pictures of
  # the theme are read from the disk in both cases (the verifier lets them through). The licence of the fonts
  # (OFL.txt) goes with them. The tar is made the same every time: names sorted, owner and times 0.
  rm -rf ${PKG_BUILD}/.memdisk
  mkdir -p ${PKG_BUILD}/.memdisk/fonts
  cp ${PKG_DIR}/theme/hades/fonts/*.pf2 ${PKG_DIR}/theme/hades/fonts/OFL.txt ${PKG_BUILD}/.memdisk/fonts
  (
    cd ${PKG_BUILD}/.memdisk
    LC_ALL=C tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 --mode=u=rwX,go=rX \
      -cf ${PKG_BUILD}/memdisk.tar fonts
  ) || die "grub: the memdisk of the fonts could not be made"

  # Hades: with a .sbat section (SBAT: the generation numbers by which shim refuses a boot loader with known holes,
  # in a start with Secure Boot): GRUB's own line at the generation of this release and the line of Hades. GRUB reads
  # nothing of it; without Secure Boot it starts as before. grub-mkimage checks GRUB's line against the generation of
  # this release. memdisk and tar read the memdisk of the fonts (above). -p comes after -m: -m sets the prefix to
  # (memdisk)/boot/grub (util/grub-mkimage.c), and GRUB would then look for its grub.cfg there and stop at its prompt.
  ${PKG_BUILD}/.${HOST_NAME}/grub-mkimage -d ${PKG_BUILD}/.x86_64-efi/grub-core -o bootx64.efi -O x86_64-efi \
    --sbat ${PKG_BUILD}/.x86_64-efi/grub-core/sbat.csv -m ${PKG_BUILD}/memdisk.tar -p /EFI/BOOT \
    boot chain configfile ext2 fat linux search efi_gop \
    part_gpt part_msdos gzio gettext loadenv loadbios memrw \
    test font gfxterm gfxmenu png video_colors bitmap_scale gfxterm_background \
    memdisk tar
  for line in "sbat,1,SBAT Version," "grub,$(sed -n 's/^GLOBAL_SBAT_LEVEL=//p' ${PKG_BUILD}/configure.ac),Free Software Foundation," \
              "grub.hades,1,LibreELEC Hades,grub,${PKG_VERSION},"; do
    grep -a -q -F "${line}" bootx64.efi || die "grub: bootx64.efi has no .sbat line ${line}"
  done
  # (the prefix is the boot partition's /EFI/BOOT, not the memdisk; the memdisk is in the program as it was made, and
  # every font that theme.cfg loads is in it)
  ! grep -a -q -F "(memdisk)/boot/grub" bootx64.efi || die "grub: the prefix of bootx64.efi is the memdisk"
  python3 -I -c 'import sys; sys.exit(0 if open(sys.argv[2], "rb").read() in open(sys.argv[1], "rb").read() else 1)' \
    bootx64.efi ${PKG_BUILD}/memdisk.tar || die "grub: bootx64.efi does not hold the memdisk of the fonts"
  for f in $(sed -n 's|^ *\(if \)\{0,1\}loadfont "${hades_fonts}/\([^"]*\)".*|\2|p' ${PKG_DIR}/theme/hades/theme.cfg); do
    [ -f ${PKG_BUILD}/.memdisk/fonts/${f} ] || die "grub: theme.cfg loads ${f}, which the memdisk has not"
  done
  grep -q -F 'loadfont "${hades_fonts}/' ${PKG_DIR}/theme/hades/theme.cfg || die "grub: theme.cfg loads no font from hades_fonts"

  mkdir -p ${INSTALL}/usr/share/grub
     cp -P bootia32.efi bootx64.efi ${INSTALL}/usr/share/grub
  # Hades: the theme in the system as well, for an update in place (usr/share/bootloader/update.sh, theme_update)
  cp -r ${PKG_DIR}/theme/hades ${INSTALL}/usr/share/grub/hades

  mkdir -p ${TOOLCHAIN}/share/grub
     cp -P bootia32.efi bootx64.efi ${TOOLCHAIN}/share/grub

  # Hades: the look of the boot menu, for the image (scripts/mkimage puts it next to grub.cfg; the
  # installer of the image copies it from there)
  rm -rf ${TOOLCHAIN}/share/grub/hades
  cp -r ${PKG_DIR}/theme/hades ${TOOLCHAIN}/share/grub/hades
}
