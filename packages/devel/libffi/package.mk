# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2019-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="libffi"
PKG_VERSION="3.8.1"
PKG_SHA256="81e879c094cc5ec3186c48510c6385ac6b3e866192e8bc71a35245703757ee40"
PKG_LICENSE="MIT"
PKG_SITE="http://sourceware.org/${PKG_NAME}/"
PKG_URL="https://github.com/libffi/libffi/releases/download/v${PKG_VERSION}/${PKG_NAME}-${PKG_VERSION}.tar.gz"
PKG_DEPENDS_HOST="ccache:host autoconf:host automake:host libtool:host pkg-config:host"
PKG_DEPENDS_TARGET="autotools:host gcc:host"
PKG_LONGDESC="Foreign Function Interface Library."
PKG_TOOLCHAIN="autotools"

PKG_CONFIGURE_OPTS_TARGET="--disable-debug \
                           --enable-static --disable-shared \
                           --with-pic \
                           --enable-structs \
                           --enable-raw-api \
                           --disable-purify-safety \
                           --with-gnu-ld"
