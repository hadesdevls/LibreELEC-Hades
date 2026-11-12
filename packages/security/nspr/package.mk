# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2012 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2019-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="nspr"
PKG_VERSION="4.39"
PKG_LICENSE="MPL-2.0"
PKG_SITE="http://www.linuxfromscratch.org/blfs/view/svn/general/nspr.html"
PKG_DEPENDS_HOST="ccache:host"
PKG_DEPENDS_TARGET="autotools:host gcc:host nss:host nspr:host"
PKG_DEPENDS_UNPACK="nss"
PKG_LONGDESC="Netscape Portable Runtime (NSPR) provides a platform-neutral API for system level and libc like functions"
PKG_TOOLCHAIN="configure"
PKG_BUILD_FLAGS="-parallel -cfg-libs"

if [ "${TARGET_ARCH}" = "x86_64" ]; then
  TARGET_USE_64="--enable-64bit"
fi

PKG_CONFIGURE_OPTS_TARGET="--with-pthreads ${TARGET_USE_64}"
PKG_MAKE_OPTS_TARGET="NSINSTALL=${TOOLCHAIN}/bin/nsinstall"
PKG_MAKEINSTALL_OPTS_TARGET="NSINSTALL=${TOOLCHAIN}/bin/nsinstall"

configure_host() {
  cd $(get_build_dir nss)/nspr
  ./configure --with-pthreads --enable-64bit --with-pthreads --prefix=${TOOLCHAIN}
}

pre_make_host() {
  cd $(get_build_dir nss)/nspr
  make clean
}

configure_target() {
  cd $(get_build_dir nss)/nspr
  ./configure --with-pthreads ${TARGET_USE_64} ${TARGET_CONFIGURE_OPTS}
}

pre_make_target() {
  cd $(get_build_dir nss)/nspr
  make clean
}

make_target() {
  # Hades: NSPR writes the time of its build into its three libraries (_BUILD_STRING and _BUILD_TIME, in
  # the version description of each one), from date and its own program "now", which do not read
  # SOURCE_DATE_EPOCH: with it (config/options) they are given that time, in the same forms (UTC, as
  # the builds of Hades have them, and microseconds)
  if [ -n "${SOURCE_DATE_EPOCH}" ]; then
    make ${PKG_MAKE_OPTS_TARGET} SH_DATE="$(date -u -d "@${SOURCE_DATE_EPOCH}" "+%Y-%m-%d %T")" SH_NOW="${SOURCE_DATE_EPOCH}000000"
  else
    make ${PKG_MAKE_OPTS_TARGET}
  fi
}
