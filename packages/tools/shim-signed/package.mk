# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

# Hades: shim of Debian 13, the first program of a start with UEFI Secure Boot. It carries both signatures of
# Microsoft (UEFI CA 2011 and UEFI CA 2023), so that a firmware that trusts either one starts it, and it starts GRUB
# signed with the key of the computer (the installer for Windows, box "Start with Secure Boot"). A program that is
# not built here, an exception made for 1.1 and kept only while all three hold:
#   - the .deb is the one of Debian's archive: PKG_SHA256 is the one of the Packages file of trixie, which the InRelease
#     signed by Debian's archive key (13/trixie) vouches for;
#   - both signatures of Microsoft verify with the certificates of Microsoft, as Microsoft publishes them;
#   - shim built from its source as Debian builds it (the .buildinfo of the build) is the same, byte for byte, but for
#     the signature.
# How each one was checked, in CHANGELOG.md (1.1). Only shimx64.efi is taken; fbx64.efi never goes to an
# image (shim would start it from EFI/BOOT, and it makes boot entries).
#
# The terms of shim go into the system with it, in /usr/share/licenses/shim-signed (NOTICE.md says where):
#   - copyright: Debian's file of this .deb, as the package has it;
#   - shim-source-copyright: debian/copyright of the source that shimx64.efi and mmx64.efi are built from
#     (shim_16.1-2~deb13u1.debian.tar.xz), which names every part of the program (shim, the parts of TianoCore, gnu-efi
#     and OpenSSL 1.0.2k in it) with its copyright notices and its licence;
#   - COPYRIGHT: shim's own notice, from its release (shim_16.1.orig.tar.bz2);
#   - BSD-2-Clause-Patent.txt: the licence that the files of shim name (SPDX-License-Identifier), from licenses/.
# The two files of the source are kept in licenses/ of this package, unchanged; their SHA-256 below is checked at
# install. They were taken from the source package whose .dsc Debian's Sources file of trixie vouches for (the same
# check as the .deb, above).

PKG_NAME="shim-signed"
PKG_VERSION="1.51~1+deb13u1+16.1-2~deb13u1"
PKG_SHA256="3c802fa303c0e6bf126adee74028d1042e3120360d159c2c05181dc9b2f61005"
PKG_ARCH="x86_64"
PKG_LICENSE="BSD-2-Clause-Patent"
PKG_SITE="https://tracker.debian.org/pkg/shim-signed"
PKG_URL="https://deb.debian.org/debian/pool/main/s/shim-signed/shim-signed_${PKG_VERSION}_amd64.deb"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="shim 16.1 of Debian 13 with both signatures of Microsoft (UEFI CA 2011 and 2023), for a start with Secure Boot."
PKG_TOOLCHAIN="manual"

# the program in the package, as it was checked
SHIM_FILE="usr/lib/shim/shimx64.efi.signed"
SHIM_SHA256="e103c5d02657879f141b086ed703584d28bb9e2721b423333be085ee1b4d397b"
# Debian's copyright file in the package
SHIM_COPYRIGHT="usr/share/doc/shim-signed/copyright"
# the notices of the source, as they were taken from it (licenses/ of this package)
SHIM_LICENCE_SHA256="15edf527919ddcb2f514ab9d16ad07ef219e4bb490e0b79560be510f0c159cc2  COPYRIGHT
b78494bfbf482648c6083d070bf138409d341a909baf09a87d49f6256ccd5a5d  shim-source-copyright"

unpack() {
  mkdir -p ${PKG_BUILD}
  # (a .deb is an ar archive; its member data.tar.xz holds the files)
  python3 -I - "${SOURCES}/${PKG_NAME}/${PKG_SOURCE_NAME}" "${PKG_BUILD}/data.tar.xz" <<'PY' ||
import sys
d = open(sys.argv[1], "rb").read()
if d[:8] != b"!<arch>\n":
    sys.exit(1)
at = 8
while at + 60 <= len(d):
    name = d[at:at + 16].decode("ascii").strip().rstrip("/")
    size = int(d[at + 48:at + 58])
    if name == "data.tar.xz":
        open(sys.argv[2], "wb").write(d[at + 60:at + 60 + size])
        sys.exit(0)
    at += 60 + size + (size & 1)
sys.exit(1)
PY
    die "${PKG_NAME}: the .deb has no data.tar.xz"
  tar -xJf ${PKG_BUILD}/data.tar.xz -C ${PKG_BUILD} ./${SHIM_FILE} ./${SHIM_COPYRIGHT}
}

makeinstall_target() {
  echo "${SHIM_SHA256}  ${PKG_BUILD}/${SHIM_FILE}" | sha256sum -c --quiet - ||
    die "${PKG_NAME}: ${SHIM_FILE} is not the one that was checked"
  mkdir -p ${INSTALL}/usr/share/hades-secureboot ${TOOLCHAIN}/share/hades-secureboot
  cp ${PKG_BUILD}/${SHIM_FILE} ${INSTALL}/usr/share/hades-secureboot/shimx64.efi
  cp ${PKG_BUILD}/${SHIM_FILE} ${TOOLCHAIN}/share/hades-secureboot/shimx64.efi

  (cd ${PKG_DIR}/licenses && echo "${SHIM_LICENCE_SHA256}" | sha256sum -c --quiet -) ||
    die "${PKG_NAME}: licenses/ of the package is not as it was taken from the source of shim"
  [ -s ${PKG_BUILD}/${SHIM_COPYRIGHT} ] || die "${PKG_NAME}: the .deb has no ${SHIM_COPYRIGHT}"
  mkdir -p ${INSTALL}/usr/share/licenses/${PKG_NAME}
    cp ${PKG_BUILD}/${SHIM_COPYRIGHT} ${INSTALL}/usr/share/licenses/${PKG_NAME}/copyright
    cp ${PKG_DIR}/licenses/COPYRIGHT ${PKG_DIR}/licenses/shim-source-copyright ${INSTALL}/usr/share/licenses/${PKG_NAME}
    cp ${ROOT}/licenses/BSD-2-Clause-Patent.txt ${INSTALL}/usr/share/licenses/${PKG_NAME}
}
