# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2016-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="soxr"
PKG_VERSION="a246900b0b8e7b1c1d30a581802e0a2fd4b8de8a"
PKG_SHA256="84cd2c2152554aa95c0bf32b8dddfbe8c71ab39d3108da0f98cadfa081359903"
PKG_LICENSE="LGPL-2.1-or-later"
PKG_SITE="https://github.com/dofuuz/soxr"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain cmake:host"
PKG_LONGDESC="The SoX Resampler library performs one-dimensional sample-rate conversion. It may be used to resample PCM-encoded audio."
PKG_BUILD_FLAGS="+pic"

PKG_CMAKE_OPTS_TARGET="-DBUILD_EXAMPLES=OFF \
                       -DWITH_OPENMP=OFF \
                       -DBUILD_SHARED_LIBS=OFF \
                       -DBUILD_TESTS=OFF \
                       -DWITH_AVFFT=OFF"

if [ "${TARGET_ARCH}" = "arm" ]; then
  if target_has_feature neon; then
    PKG_CMAKE_OPTS_TARGET+=" -DWITH_CR32=OFF"
  else
    PKG_CMAKE_OPTS_TARGET+=" -DWITH_CR32S=OFF"
  fi
fi
