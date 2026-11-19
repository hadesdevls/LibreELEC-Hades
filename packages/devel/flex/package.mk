# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2019-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="flex"
PKG_VERSION="2.6.16"
PKG_SHA256="c778edc11a53b86aa33203f51d179cc294e5a5d07f84285eaa34c76c3f902bab"
PKG_LICENSE="BSD-3-Clause"
PKG_SITE="https://github.com/westes/flex"
PKG_URL="https://github.com/westes/flex/releases/download/v${PKG_VERSION}/${PKG_NAME}-${PKG_VERSION}.tar.gz"
PKG_DEPENDS_HOST="ccache:host m4:host autotools:host bison:host"
PKG_DEPENDS_TARGET="toolchain flex:host"
PKG_LONGDESC="A tool for generating programs that perform pattern-matching on text."
PKG_TOOLCHAIN="autotools"

PKG_CONFIGURE_OPTS_HOST="--enable-static --disable-shared --disable-rpath --with-gnu-ld"

PKG_CONFIGURE_OPTS_TARGET="--disable-bootstrap \
                           ac_cv_func_realloc_0_nonnull=yes \
                           ac_cv_func_malloc_0_nonnull=yes"

post_makeinstall_host() {
  cat >${TOOLCHAIN}/bin/lex  <<"EOF"
#!/bin/sh
exec flex "$@"
EOF

  chmod -v 755 ${TOOLCHAIN}/bin/lex
}
