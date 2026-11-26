# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="mariadb-connector-c"
PKG_VERSION="3.4.11"
PKG_SHA256="7cdb35c571dd0c187f806f569285829d31b4aba4feb93957a034b0b1e887e276"
PKG_LICENSE="LGPL-2.1-or-later"
PKG_SITE="https://mariadb.org/"
PKG_URL="https://github.com/mariadb-corporation/mariadb-connector-c/archive/v${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain zlib openssl"
PKG_LONGDESC="mariadb-connector: library to connect to mariadb/mysql database server"
PKG_BUILD_FLAGS="-gold"

# Hades: REMOTEIO_PLUGIN_TYPE=OFF. CMakeLists.txt looks for curl whatever WITH_CURL says (FIND_PACKAGE(CURL)), and
# where it finds it and the remote I/O plugin is not OFF (with WITH_CURL=OFF it is not even registered, so not OFF)
# compiles libmariadb with HAVE_REMOTEIO: whether libmariadb.so.3 has the code of remote files depended on whether curl,
# which is not among the dependencies, happened to be built before it (1.2 without, 1.2.1 with). The plugin itself is
# never built (WITH_CURL=OFF) nor installed, so OFF is what WITH_CURL=OFF means, the same at any order of the build.
PKG_CMAKE_OPTS_TARGET="-DWITH_EXTERNAL_ZLIB=ON
                       -DWITH_UNIT_TESTS=OFF
                       -DCLIENT_PLUGIN_DIALOG=STATIC
                       -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC
                       -DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD=STATIC
                       -DWITH_CURL=OFF
                       -DREMOTEIO_PLUGIN_TYPE=OFF
                       -DDEFAULT_SSL_VERIFY_SERVER_CERT=OFF
                      "

pre_configure_target() {
  # glibc-2.43 made strchr/strstr C23-compliant (const-preserving return type);
  # mariadb assigns the result to non-const pointers; upstream bug: https://jira.mariadb.org/projects/CONC/issues/CONC-805
  export CFLAGS+=" -Wno-discarded-qualifiers"
}

post_makeinstall_target() {
  # keep mariadb shared library and modern authentication plugins
  LIBDIR=${INSTALL}/usr/lib
  mkdir -p ${INSTALL}/.tmp/mariadb/plugin
  mv ${LIBDIR}/mariadb/libmariadb.so* ${INSTALL}/.tmp
  mv ${LIBDIR}/mariadb/plugin/{caching_sha2_password,client_ed25519,sha256_password}.so ${INSTALL}/.tmp/mariadb/plugin

  # drop all unneeded
  rm -rf ${INSTALL}/usr

  mkdir -p ${LIBDIR}
  mv ${INSTALL}/.tmp/* ${LIBDIR}/
  rmdir ${INSTALL}/.tmp
}
