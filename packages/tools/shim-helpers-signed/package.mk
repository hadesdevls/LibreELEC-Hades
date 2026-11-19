# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

# Hades: MokManager of Debian 13, signed by Debian, which shim starts to enroll the key of the computer (the blue
# screen of the first start with Secure Boot). Built from the same source as shim (shim 16.1-2~deb13u1); the same
# exception and the same three checks as packages/tools/shim-signed (here the signature is Debian's, checked with the
# certificate that shim carries). Only mmx64.efi is taken.
# Its terms go into the system with it, in /usr/share/licenses/shim-helpers-signed: Debian's copyright file of this
# .deb (it covers the packaging and the signature), and, since MokManager is built from the source of shim, the three
# files that shim-signed installs from that source and from licenses/ (see there), checked the same way.

PKG_NAME="shim-helpers-signed"
PKG_VERSION="1+16.1+2~deb13u1"
PKG_SHA256="7e950d75d5a40ecc0a99f727452ce35780b47a04d9a2cc3b1ff7c2bb316c033e"
PKG_ARCH="x86_64"
PKG_LICENSE="BSD-2-Clause-Patent"
PKG_SITE="https://tracker.debian.org/pkg/shim-helpers-amd64-signed"
PKG_URL="https://deb.debian.org/debian/pool/main/s/shim-helpers-amd64-signed/shim-helpers-amd64-signed_${PKG_VERSION}_amd64.deb"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="MokManager of shim 16.1 of Debian 13, signed by Debian, for the first start with Secure Boot."
PKG_TOOLCHAIN="manual"

# the program in the package, as it was checked
MM_FILE="usr/lib/shim/mmx64.efi.signed"
MM_SHA256="30ea13138310f0819c1d875fcf3a6cdce8a1607cfd7578053a2df894a41630ae"
# Debian's copyright file in the package
MM_COPYRIGHT="usr/share/doc/shim-helpers-amd64-signed/copyright"
# the notices of the source of shim in licenses/ of shim-signed (the same values as SHIM_LICENCE_SHA256 there)
MM_LICENCE_SHA256="15edf527919ddcb2f514ab9d16ad07ef219e4bb490e0b79560be510f0c159cc2  COPYRIGHT
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
  tar -xJf ${PKG_BUILD}/data.tar.xz -C ${PKG_BUILD} ./${MM_FILE} ./${MM_COPYRIGHT}
}

makeinstall_target() {
  echo "${MM_SHA256}  ${PKG_BUILD}/${MM_FILE}" | sha256sum -c --quiet - ||
    die "${PKG_NAME}: ${MM_FILE} is not the one that was checked"
  mkdir -p ${INSTALL}/usr/share/hades-secureboot ${TOOLCHAIN}/share/hades-secureboot
  cp ${PKG_BUILD}/${MM_FILE} ${INSTALL}/usr/share/hades-secureboot/mmx64.efi
  cp ${PKG_BUILD}/${MM_FILE} ${TOOLCHAIN}/share/hades-secureboot/mmx64.efi

  local shim_dir=$(get_pkg_directory shim-signed)
  (cd ${shim_dir}/licenses && echo "${MM_LICENCE_SHA256}" | sha256sum -c --quiet -) ||
    die "${PKG_NAME}: licenses/ of shim-signed is not as it was taken from the source of shim"
  [ -s ${PKG_BUILD}/${MM_COPYRIGHT} ] || die "${PKG_NAME}: the .deb has no ${MM_COPYRIGHT}"
  mkdir -p ${INSTALL}/usr/share/licenses/${PKG_NAME}
    cp ${PKG_BUILD}/${MM_COPYRIGHT} ${INSTALL}/usr/share/licenses/${PKG_NAME}/copyright
    cp ${shim_dir}/licenses/COPYRIGHT ${shim_dir}/licenses/shim-source-copyright ${INSTALL}/usr/share/licenses/${PKG_NAME}
    cp ${ROOT}/licenses/BSD-2-Clause-Patent.txt ${INSTALL}/usr/share/licenses/${PKG_NAME}
}
