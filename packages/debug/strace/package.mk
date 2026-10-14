# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2016-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="strace"
PKG_VERSION="7.2"
PKG_SHA256="4bde6246926890dcee824f6e6ac42a06752f47d77e5097d86e3c0d6d4b709fe5"
PKG_LICENSE="LGPL-2.1-or-later"
PKG_SITE="https://strace.io/"
PKG_URL="https://strace.io/files/${PKG_VERSION}/strace-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_TARGET="toolchain libunwind"
PKG_LONGDESC="strace is a diagnostic, debugging and instructional userspace utility"
PKG_TOOLCHAIN="autotools"
PKG_BUILD_FLAGS="-cfg-libs"

# Hades: stack tracing (strace -k) with libunwind, declared: configure finds libdw, libunwind or
# libiberty by itself, depending on what other packages have installed when strace is configured, so
# builds of the same tree differed.
PKG_CONFIGURE_OPTS_TARGET="--enable-stacktrace=yes --with-libunwind --without-libdw --without-libiberty"

if [ "${TARGET_ARCH}" = x86_64 -o "${TARGET_ARCH}" = "aarch64" ]; then
  PKG_CONFIGURE_OPTS_TARGET+=" --enable-mpers=no"
fi
