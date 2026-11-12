# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="openssh"
PKG_VERSION="10.6p2"
PKG_SHA256="9ae44872090cf2f06b3cf9ff147e7f16f309671340763ed2f99248398ae29d5b"
PKG_LICENSE="BSD-2-Clause AND ISC"
PKG_SITE="https://www.openssh.com/"
PKG_URL="https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/${PKG_NAME}-${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain openssl zlib"
PKG_LONGDESC="An open re-implementation of the SSH package."
PKG_TOOLCHAIN="autotools"
PKG_BUILD_FLAGS="+lto -cfg-libs"

PKG_CONFIGURE_OPTS_TARGET="ac_cv_header_rpc_types_h=no \
                           --sysconfdir=/etc/ssh \
                           --libexecdir=/usr/lib/openssh \
                           --disable-strip \
                           --disable-lastlog \
                           --with-sandbox=seccomp_filter \
                           --disable-utmp \
                           --disable-utmpx \
                           --disable-wtmp \
                           --disable-wtmpx \
                           --without-rpath \
                           --without-ssl-engine \
                           --with-privsep-user=nobody \
                           --disable-pututline \
                           --disable-pututxline \
                           --disable-etc-default-login \
                           --with-keydir=/storage/.cache/ssh \
                           --without-pam"

pre_configure_target() {
  export LD="${CC}"
  export LDFLAGS="${TARGET_CFLAGS} ${TARGET_LDFLAGS}"
}

post_makeinstall_target() {
  rm -rf ${INSTALL}/usr/lib/openssh/ssh-keysign
  rm -rf ${INSTALL}/usr/lib/openssh/ssh-pkcs11-helper
  if [ ! ${SFTP_SERVER} = "yes" ]; then
    rm -rf ${INSTALL}/usr/lib/openssh/sftp-server
  fi
  rm -rf ${INSTALL}/usr/bin/ssh-add
  rm -rf ${INSTALL}/usr/bin/ssh-agent
  rm -rf ${INSTALL}/usr/bin/ssh-keyscan

  sed -e "s|^#PermitRootLogin.*|PermitRootLogin yes|g" \
      -e "s|^#StrictModes.*|StrictModes no|g" \
      -i ${INSTALL}/etc/ssh/sshd_config

  # Hades: runtime settings, e.g. no password logins while root has the default password;
  # the Include must come first, since the first value of each keyword wins
  { echo "# Hades: runtime settings; keep first, the first value of each keyword wins"
    echo "Include /run/libreelec/sshd_config.d/*.conf"
    echo ""
    cat ${INSTALL}/etc/ssh/sshd_config
  } >${INSTALL}/etc/ssh/sshd_config.hades
  mv ${INSTALL}/etc/ssh/sshd_config.hades ${INSTALL}/etc/ssh/sshd_config

  mkdir -p ${INSTALL}/usr/lib/libreelec
    cp ${PKG_DIR}/scripts/sshd-default-password ${INSTALL}/usr/lib/libreelec
    sed -e "s|@ROOT_PASSWORD@|${ROOT_PASSWORD}|g" -i ${INSTALL}/usr/lib/libreelec/sshd-default-password

  debug_strip ${INSTALL}/usr
}

post_install() {
  if [ "${SSH_ENABLED_DEFAULT}" = "yes" ]; then
    sed -e "\|^Condition.*|d" -i ${INSTALL}/usr/lib/systemd/system/sshd.service
  fi
  enable_service sshd.service
}
