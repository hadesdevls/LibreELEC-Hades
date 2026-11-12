# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2022-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="kmsxx"
PKG_VERSION="e0d593d298a3590d306bdf850cf0295d078fbf73"
PKG_SHA256="7da8d6297f519c354ce1d63c5632676fdb7f1ab8bd26f7c42702080616b83d3c"
PKG_LICENSE="MPL-2.0"
PKG_SITE="https://github.com/tomba/kmsxx"
PKG_URL="https://github.com/tomba/kmsxx/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain libdrm libfmt"
PKG_LONGDESC="Library and utilities for kernel mode setting"
PKG_BUILD_FLAGS="-sysroot"

PKG_MESON_OPTS_TARGET="-Ddefault_library=shared \
                       -Dkmscube=false \
                       -Domap=disabled \
                       -Dpykms=disabled"
