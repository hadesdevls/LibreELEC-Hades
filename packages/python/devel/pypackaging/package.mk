# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2024-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="pypackaging"
PKG_VERSION="26.4"
PKG_SHA256="c4b671c41ddb9bf26e543c37723610d28fb6bf7affc4da17cf048d419d2c062a"
PKG_LICENSE="Apache-2.0 OR BSD-2-Clause"
PKG_SITE="https://pypi.org/project/build/"
PKG_URL="https://files.pythonhosted.org/packages/source/p/packaging/packaging-${PKG_VERSION}.tar.gz"
PKG_SOURCE_DIR="packaging-${PKG_VERSION}"
PKG_DEPENDS_HOST="flit:host pyinstaller:host"
PKG_LONGDESC="Reusable core utilities for various Python Packaging interoperability specifications."
PKG_TOOLCHAIN="python-flit"
