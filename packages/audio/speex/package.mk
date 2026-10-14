# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2016-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="speex"
PKG_VERSION="1.2.1"
PKG_SHA256="cc55cce69d8753940d56936f7a1fe6db4b302df144aec93a92de1c65b1a87681"
PKG_LICENSE="BSD-3-Clause"
PKG_SITE="https://speex.org"
PKG_URL="https://gitlab.xiph.org/xiph/speex/-/archive/Speex-${PKG_VERSION}/speex-Speex-${PKG_VERSION}.tar.bz2"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="An Open Source Software patent-free audio compression format designed for speech."
PKG_TOOLCHAIN="autotools"

# speexenc and speexdec are built only when configure finds libogg, which this package does not
# depend on: whether they were in the image depended on which of the two packages was built first.
PKG_CONFIGURE_OPTS_TARGET="--disable-binaries"
