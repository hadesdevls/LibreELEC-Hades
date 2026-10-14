# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2017-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="Python3"
# When changing PKG_VERSION remember to sync PKG_PYTHON_VERSION!
PKG_VERSION="3.14.8"
PKG_SHA256="c2215904f02b175596dc49351585104f4bc20341e1c47378b26a2c274360ce73"
PKG_LICENSE="PSF-2.0"
PKG_SITE="https://www.python.org/"
PKG_URL="https://www.python.org/ftp/python/${PKG_VERSION}/${PKG_NAME::-1}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_HOST="zlib:host bzip2:host libffi:host mpdecimal:host util-linux:host autoconf-archive:host openssl3:host"
PKG_DEPENDS_TARGET="autotools:host gcc:host Python3:host sqlite expat zlib bzip2 xz zstd openssl3 libffi readline mpdecimal ncurses util-linux"
PKG_LONGDESC="Python3 is an interpreted object-oriented programming language."
PKG_BUILD_FLAGS="-cfg-libs -cfg-libs:host"
PKG_TOOLCHAIN="autotools"

PKG_PYTHON_VERSION="python3.14"

# Hades: Python 3.14 does not build against OpenSSL 4 (it calls the fixed-version TLS methods that
# OpenSSL 4.0 removed; its authors support OpenSSL 4 from Python 3.15 on, and Python 3.15 removes things
# that add-ons of Kodi written for 3.14 still call). The OpenSSL of the image and of the toolchain is 4,
# so the ssl and hashlib modules are built against the OpenSSL 3 of the openssl3 package (the 3.5
# long-term series), whose libraries are beside those of OpenSSL 4: in the image, and in the toolchain
# for the interpreter of the build host (Samba's build imports ssl).

PKG_CONFIGURE_OPTS_HOST="ac_cv_prog_HAS_HG=/bin/false
                         ac_cv_prog_SVNVERSION=/bin/false
                         py_cv_module_unicodedata=yes
                         py_cv_module__bz2=n/a
                         py_cv_module__codecs_cn=n/a
                         py_cv_module__codecs_hk=n/a
                         py_cv_module__codecs_iso2022=n/a
                         py_cv_module__codecs_jp=n/a
                         py_cv_module__codecs_kr=n/a
                         py_cv_module__codecs_tw=n/a
                         py_cv_module__decimal=n/a
                         py_cv_module__lzma=n/a
                         py_cv_module_nis=n/a
                         py_cv_module_ossaudiodev=n/a
                         py_cv_module__dbm=n/a
                         py_cv_module__gdbm=n/a
                         --disable-pyc-build
                         --disable-sqlite3
                         --without-readline
                         --disable-tk
                         --disable-curses
                         --disable-pydoc
                         --disable-test-modules
                         --disable-idle3
                         --with-expat=builtin
                         --with-doc-strings
                         --without-pymalloc
                         --with-ensurepip=no
                         --with-openssl=${TOOLCHAIN}/lib/openssl-3
                         --enable-shared
"

PKG_CONFIGURE_OPTS_TARGET="ac_cv_prog_HAS_HG=/bin/false
                           ac_cv_prog_SVNVERSION=/bin/false
                           ac_cv_file__dev_ptmx=no
                           ac_cv_file__dev_ptc=no
                           ac_cv_have_long_long_format=yes
                           ac_cv_working_tzset=yes
                           ac_cv_func_lchflags_works=no
                           ac_cv_func_chflags_works=no
                           ac_cv_func_printf_zd=yes
                           ac_cv_buggy_getaddrinfo=no
                           ac_cv_header_bluetooth_bluetooth_h=no
                           ac_cv_header_bluetooth_h=no
                           py_cv_module_unicodedata=yes
                           py_cv_module__codecs_cn=n/a
                           py_cv_module__codecs_hk=n/a
                           py_cv_module__codecs_iso2022=n/a
                           py_cv_module__codecs_jp=n/a
                           py_cv_module__codecs_kr=n/a
                           py_cv_module__codecs_tw=n/a
                           py_cv_module__decimal=n/a
                           py_cv_module_nis=n/a
                           py_cv_module_ossaudiodev=n/a
                           py_cv_module__dbm=n/a
                           --disable-pyc-build
                           --enable-sqlite3
                           --with-readline
                           --disable-tk
                           --enable-curses
                           --disable-pydoc
                           --disable-test-modules
                           --disable-idle3
                           --with-expat=system
                           --with-doc-strings
                           --with-lto
                           --without-pymalloc
                           --without-ensurepip
                           --enable-ipv6
                           --with-build-python=${TOOLCHAIN}/bin/python
                           --with-openssl=${SYSROOT_PREFIX}/usr/lib/openssl-3
                           --enable-shared
"

pre_configure_host() {
  # control patch Python3-0300-generate-legacy-pyc-bytecode
  # this needs to be set when building host based py file
  # do not set this for py compiles being done for target use
  export DONT_BUILD_LEGACY_PYC=1

  # Hades: the host flags name the library folder of the toolchain first, and its libssl.so is the one of
  # OpenSSL 4: the folder of OpenSSL 3 goes before it, or ssl is compiled for one and linked to the other
  export LDFLAGS="-L${TOOLCHAIN}/lib/openssl-3/lib ${LDFLAGS}"
}

post_make_host() {
  # python distutils per default adds -L${LIBDIR} when linking binary extensions
  sed -e "s|^ 'LIBDIR':.*| 'LIBDIR': '/usr/lib',|g" -i $(find ${PKG_BUILD}/.${HOST_NAME} -not -path '*/__pycache__/*' -name '_sysconfigdata__*.py')
}

post_makeinstall_host() {
  ln -sf ${PKG_PYTHON_VERSION} ${TOOLCHAIN}/bin/python

  rm -fr ${PKG_BUILD}/.${HOST_NAME}/build/temp.*
}

post_make_target() {
  # fix sysconfig paths for cross compiling
  PKG_SYSCONFIG_FILE=$(find ${PKG_BUILD}/.${TARGET_NAME} -not -path '*/__pycache__/*' -name '_sysconfigdata__*.py')
  sed -e "s,\([\'|\ ]\)/usr/include,\1${SYSROOT_PREFIX}/usr/include,g" -i ${PKG_SYSCONFIG_FILE}
  sed -e "s,\([\'|\ ]\)/usr/lib,\1${SYSROOT_PREFIX}/usr/lib,g" -i ${PKG_SYSCONFIG_FILE}
}

pre_configure_target() {
  export PYTHON_MODULES_INCLUDE="${TARGET_INCDIR}"
  export PYTHON_MODULES_LIB="${TARGET_LIBDIR}"
  export DISABLED_EXTENSIONS="${PKG_PY_DISABLED_MODULES}"

  # Hades: ncurses is a static library here, and libreadline leaves the terminal functions it calls
  # (tgetent, UP...) to whoever loads it. The programs that use readline link ncurses in; Python's
  # readline module did not, and could not be imported ("undefined symbol: UP").
  export LIBREADLINE_LIBS="-lreadline -lncursesw"
}

post_makeinstall_target() {
  ln -sf ${PKG_PYTHON_VERSION} ${INSTALL}/usr/bin/python

  rm -fr ${PKG_BUILD}/.${TARGET_NAME}/build/temp.*

  PKG_INSTALL_PATH_LIB=${INSTALL}/usr/lib/${PKG_PYTHON_VERSION}

  for dir in config compiler sysconfigdata lib-dynload/sysconfigdata test; do
    rm -rf ${PKG_INSTALL_PATH_LIB}/${dir}
  done

  safe_remove ${INSTALL}/usr/bin/python*-config

  find ${INSTALL} -name '*.o' -delete

  python_compile ${PKG_INSTALL_PATH_LIB}

  # strip
  chmod u+w ${INSTALL}/usr/lib/libpython*.so.*
  debug_strip ${INSTALL}/usr
}
