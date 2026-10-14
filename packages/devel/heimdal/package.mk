# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2017 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

# Hades: the two programs that the build of Samba runs on the host (asn1_compile and compile_et, which
# write C code from the ASN.1 modules and the error tables of the Heimdal that Samba bundles) are built
# from that same Heimdal: the one inside the Samba archive (third_party/heimdal), not a snapshot of Heimdal
# fetched apart. A generator of another date writes other code than the library it is linked with expects:
# the one of 2022 that was used with Samba 4.25, whose Heimdal is of 2026, gave UniversalString the tag of
# GraphicString in the X.509 templates, which Samba's own generator has right. This way generator and
# library are of one source whatever version of Samba the image has, and nothing else is downloaded.

PKG_NAME="heimdal"
PKG_VERSION="$(get_pkg_version samba)"
PKG_LICENSE="BSD-3-Clause"
PKG_SITE="https://www.samba.org"
PKG_URL=""
PKG_DEPENDS_HOST="autotools:host Python3:host ncurses:host asn1c:host flex:host"
PKG_DEPENDS_UNPACK+=" samba"
PKG_LONGDESC="The ASN.1 and error table compilers of the Heimdal that Samba bundles, built for the host."
PKG_TOOLCHAIN="autotools"
PKG_BUILD_FLAGS="-parallel"

unpack() {
  mkdir -p ${PKG_BUILD}
  tar --strip-components=3 -xf ${SOURCES}/samba/samba-${PKG_VERSION}.tar.gz -C ${PKG_BUILD} \
      samba-${PKG_VERSION}/third_party/heimdal
}

pre_configure_host() {
  # configure probe for crypt() uses implicit declaration; without this the probe
  # fails and crypt is excluded from the link, causing an undefined reference at build time
  export CFLAGS+=" -Wno-error=implicit-function-declaration"
}

PKG_CONFIGURE_OPTS_HOST="ac_cv_prog_COMPILE_ET=no \
                         --enable-static --disable-shared \
                         --without-openldap \
                         --without-capng \
                         --without-sqlite3 \
                         --without-libintl \
                         --without-openssl \
                         --without-berkeley-db \
                         --without-readline \
                         --without-libedit \
                         --without-hesiod \
                         --without-x \
                         --disable-otp \
                         --disable-heimdal-documentation"

make_host() {
  # the two generators and what they are made of. The rest of this Heimdal (its libraries, its daemons
  # and its tests) is not built here: Samba builds what it uses of it, from the same sources.
  make -C include
  make -C lib/roken
  make -C lib/vers
  make -C lib/com_err
  make -C lib/base
  make -C lib/asn1 asn1_compile
}

makeinstall_host() {
  mkdir -p ${TOOLCHAIN}/bin
    cp -PR lib/asn1/asn1_compile ${TOOLCHAIN}/bin/heimdal_asn1_compile
    cp -PR lib/com_err/compile_et ${TOOLCHAIN}/bin/heimdal_compile_et
}
