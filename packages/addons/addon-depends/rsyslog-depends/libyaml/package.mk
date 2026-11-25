# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="libyaml"
PKG_VERSION="0.2.7"
PKG_SHA256="0c39256b8be0fc689f5c7bfdea1bc9f3fb1fd648468fd3cc6fd375e09ffb6f13"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/yaml/libyaml"
PKG_URL="https://github.com/yaml/libyaml/releases/download/${PKG_VERSION}/yaml-${PKG_VERSION}.tar.gz"
PKG_SOURCE_DIR="yaml-${PKG_VERSION}"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="LibYAML is a YAML parser and emitter library"
PKG_BUILD_FLAGS="-sysroot -cfg-libs"
PKG_TOOLCHAIN="configure"

PKG_CONFIGURE_OPTS_TARGET="--enable-static --disable-shared"
