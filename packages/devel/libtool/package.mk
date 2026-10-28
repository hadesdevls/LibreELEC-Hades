# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2019-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="libtool"
PKG_VERSION="2.6.2"
PKG_SHA256="2ef1067c16c97db930fd740cc9bc3d3ba9a583804ae5ac42cc3e8719e49e191e"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://www.gnu.org/software/libtool/"
PKG_URL="https://ftpmirror.gnu.org/libtool/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_HOST="ccache:host autoconf:host automake:host intltool:host"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="A generic library support script."
PKG_TOOLCHAIN="autotools"

PKG_CONFIGURE_OPTS_HOST="--enable-static \
                         --disable-shared"

post_unpack() {
  chmod u+w ${PKG_BUILD}/build-aux/ltmain.sh
}

pre_configure_target() {
  # The default search path of libltdl is the image's: on Linux, libtool's configure adds the folders of
  # the /etc/ld.so.conf of the machine it runs on, the build host's also when it cross-compiles
  # (/usr/local/lib and Ubuntu's multiarch folders, and once WSL's /usr/lib/wsl/lib)
  export lt_cv_sys_dlsearch_path="/lib /usr/lib"
}

pre_make_host() {
  # do not rebuild man, or txt pages
  touch ${PKG_BUILD}/doc/*.1 \
        ${PKG_BUILD}/doc/*.txt
}

post_makeinstall_target() {
  rm -rf ${INSTALL}/usr/bin
  rm -rf ${INSTALL}/usr/share
}
