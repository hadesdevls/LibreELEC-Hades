# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: what setuptools-scm is made of since its version 10 (the reading of a version from the tags of a
# repository, or from the PKG-INFO of an archive, moved to this package, of the same authors and the same
# repository). A tool of the build: it is not in the image.

PKG_NAME="vcs-versioning"
PKG_VERSION="2.6.0"
PKG_SHA256="22e9159288e2d8bca2fa6f6c31c34c9c66aec157f60e9e526f5d160d2516d60f"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/pypa/setuptools-scm"
PKG_URL="https://files.pythonhosted.org/packages/source/v/vcs_versioning/vcs_versioning-${PKG_VERSION}.tar.gz"
PKG_DEPENDS_HOST="Python3:host setuptools:host pypackaging:host"
PKG_LONGDESC="the blessed package to manage your versions by vcs metadata"
PKG_TOOLCHAIN="python"
