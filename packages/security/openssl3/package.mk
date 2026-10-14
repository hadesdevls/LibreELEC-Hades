# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: the libraries of OpenSSL 3, beside the OpenSSL 4 of the image.
#
# OpenSSL 4 names its libraries libssl.so.4 and libcrypto.so.4. Two things still need libssl.so.3 and
# libcrypto.so.3:
# - the binary add-ons of LibreELEC's repository, which are built against OpenSSL 3 and ask for those two
#   files by name (25 of the 228 add-ons of the repository on 2026-10-13, none of them for a symbol newer
#   than those of 3.0.0): without them they do not load;
# - Python 3.14, which does not build against OpenSSL 4 (the Python3 package builds its ssl and hashlib
#   modules against this one, for the image and for the build host: Samba's build imports ssl).
# They come from the 3.5 series, the long-term one, built with the options of LibreELEC's openssl package.
#
# Nothing of this package is in the way of OpenSSL 4: the two libraries have their own names, and the rest
# (the provider and the engines that they load, and the headers and link names for the Python3 package) is
# under lib/openssl-3 (/usr/lib/openssl-3 in the image, the same under the toolchain for the build host).
# The configuration folder, /etc/ssl, with the certificates of the system, is the same for both.

PKG_NAME="openssl3"
PKG_VERSION="3.5.9"
PKG_SHA256="603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"
PKG_LICENSE="Apache-2.0"
PKG_SITE="https://openssl-library.org"
PKG_URL="https://github.com/openssl/openssl/releases/download/openssl-${PKG_VERSION}/openssl-${PKG_VERSION}.tar.gz"
PKG_SOURCE_DIR="openssl-${PKG_VERSION}"
PKG_DEPENDS_HOST="ccache:host"
PKG_DEPENDS_TARGET="autotools:host gcc:host"
PKG_LONGDESC="The libraries of OpenSSL 3 (libssl.so.3 and libcrypto.so.3, from its long-term series), for Python 3.14 and for programs that were built against them"
PKG_TOOLCHAIN="configure"

PKG_OPENSSL3_PREFIX="/usr/lib/openssl-3"

PKG_CONFIGURE_OPTS_SHARED="--libdir=lib \
                           shared \
                           threads \
                           no-ec2m \
                           no-md2 \
                           no-rc5 \
                           no-rfc3779 \
                           no-sctp \
                           no-ssl-trace \
                           no-ssl3 \
                           no-unit-test \
                           no-weak-ssl-ciphers \
                           no-zlib \
                           no-zlib-dynamic \
                           no-static-engine"

PKG_CONFIGURE_OPTS_HOST="--prefix=${TOOLCHAIN}/lib/openssl-3 \
                         --openssldir=${TOOLCHAIN}/etc/ssl"
PKG_CONFIGURE_OPTS_TARGET="--prefix=${PKG_OPENSSL3_PREFIX} \
                           --openssldir=/etc/ssl"

pre_configure_host() {
  mkdir -p ${PKG_BUILD}/.${HOST_NAME}
  cp -a ${PKG_BUILD}/* ${PKG_BUILD}/.${HOST_NAME}/
}

configure_host() {
  cd ${PKG_BUILD}/.${HOST_NAME}
  ./Configure ${PKG_CONFIGURE_OPTS_HOST} ${PKG_CONFIGURE_OPTS_SHARED} linux-${MACHINE_HARDWARE_NAME} ${CFLAGS} ${LDFLAGS}
}

makeinstall_host() {
  # as for the image: all of it under its own prefix, and the two libraries where the programs of the
  # toolchain look for a library by name
  make install_sw
  cp -P libssl.so.3 libcrypto.so.3 ${TOOLCHAIN}/lib
}

pre_configure_target() {
  mkdir -p ${PKG_BUILD}/.${TARGET_NAME}
  cp -a ${PKG_BUILD}/* ${PKG_BUILD}/.${TARGET_NAME}/

  case ${TARGET_ARCH} in
    x86_64)
      OPENSSL_TARGET=linux-x86_64
      PLATFORM_FLAGS=enable-ec_nistp_64_gcc_128
      ;;
    arm)
      OPENSSL_TARGET=linux-armv4
      ;;
    aarch64)
      OPENSSL_TARGET=linux-aarch64
      ;;
  esac
}

configure_target() {
  cd ${PKG_BUILD}/.${TARGET_NAME}
  ./Configure ${PKG_CONFIGURE_OPTS_TARGET} ${PKG_CONFIGURE_OPTS_SHARED} ${PLATFORM_FLAGS} ${OPENSSL_TARGET} ${CFLAGS} ${LDFLAGS}
}

makeinstall_target() {
  # for the package that is built against these libraries: all of it, under its own prefix, where nothing
  # that looks for OpenSSL in the usual places finds it
  make DESTDIR=${SYSROOT_PREFIX} install_sw

  # for the image: the two libraries where the loader looks for them by name, and what they load themselves
  # where they look for it
  mkdir -p ${INSTALL}/usr/lib
    cp -P libssl.so.3 libcrypto.so.3 ${INSTALL}/usr/lib
  mkdir -p ${INSTALL}${PKG_OPENSSL3_PREFIX}/lib/ossl-modules
    cp -P providers/legacy.so ${INSTALL}${PKG_OPENSSL3_PREFIX}/lib/ossl-modules
  mkdir -p ${INSTALL}${PKG_OPENSSL3_PREFIX}/lib/engines-3
    cp -P engines/afalg.so engines/capi.so engines/loader_attic.so engines/padlock.so ${INSTALL}${PKG_OPENSSL3_PREFIX}/lib/engines-3
}
