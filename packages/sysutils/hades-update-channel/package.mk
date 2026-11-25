# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: the signed update channel of LibreELEC Hades, the side of the image. The settings add-on
# (LibreELEC-settings-9016, hades_update.py) looks for a new version only when the system has, in
# /usr/share/hades/update: channel.conf (the address of the manifest, which every release of LibreELEC Hades
# on GitHub carries with its signature, and the name of the channel), keys/hades-1.pub and keys/hades-2.pub
# (the two Ed25519 keys that may sign it: the one in use and the one kept apart, so that the first can be
# replaced without a new installation) and release-serial (this version, as a whole number that grows from
# one version to the next: scripts/release-serial.sh).
# The private halves of the keys are never in this tree, in the build or on GitHub: whoever publishes
# LibreELEC Hades keeps them. keys/ holds the public halves and their proofs (each key's signature of
# "hades-update-key-proof-1", which shows that the key is a whole one and that its private half is held).
# The build stops when the keys are not there, are not whole, are the same key twice, or are test keys
# (test-keys.txt), or, with HADES_CHANNEL_PINNED (a file of the publisher's two key lines, kept outside the tree),
# are not exactly those. A build named other than Hades gets no channel (its updates would be Hades'); so does a
# build with HADES_UPDATE_CHANNEL=no, which is for builds that are never published.

PKG_NAME="hades-update-channel"
PKG_VERSION="1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain openssl:host"
PKG_LONGDESC="The update channel of LibreELEC Hades in the image: the address of its signed manifest, the two public keys that may sign it, and the serial of this version."
PKG_TOOLCHAIN="manual"
# (what the files of the package do not say, but what is installed depends on)
PKG_STAMP="${BUILDER_NAME} ${BUILDER_VERSION} ${HADES_UPDATE_CHANNEL:-} ${HADES_CHANNEL_PINNED:-}"

makeinstall_target() {
  local serial

  if [ "${BUILDER_NAME}" != "Hades" ]; then
    echo "hades-update-channel: a build named \"${BUILDER_NAME}\", not Hades: no update channel in this image"
    return 0
  fi
  if [ "${HADES_UPDATE_CHANNEL:-}" = "no" ]; then
    echo "hades-update-channel: HADES_UPDATE_CHANNEL=no: no update channel in this image (it is not one to publish)"
    return 0
  fi
  serial="$(bash ${PKG_DIR}/scripts/release-serial.sh "${BUILDER_VERSION}")" ||
    die "hades-update-channel: BUILDER_VERSION \"${BUILDER_VERSION}\" is not a version that the channel can order"
  OPENSSL="${TOOLCHAIN}/bin/openssl" bash ${PKG_DIR}/scripts/check-keys.sh ${PKG_DIR}/keys ${PKG_DIR}/test-keys.txt \
    ${HADES_CHANNEL_PINNED:-} ||
    die "hades-update-channel: the keys of the update channel are not the ones of LibreELEC Hades (above)"
  [ -n "${HADES_CHANNEL_PINNED:-}" ] ||
    echo "hades-update-channel: the keys are not compared with the publisher's pinned ones (HADES_CHANNEL_PINNED unset); the check before publishing compares them"

  mkdir -p ${INSTALL}/usr/share/hades/update/keys
  cp ${PKG_DIR}/channel.conf ${INSTALL}/usr/share/hades/update/channel.conf
  cp ${PKG_DIR}/keys/hades-1.pub ${PKG_DIR}/keys/hades-2.pub ${INSTALL}/usr/share/hades/update/keys
  echo "${serial}" >${INSTALL}/usr/share/hades/update/release-serial
  chmod 0644 ${INSTALL}/usr/share/hades/update/channel.conf ${INSTALL}/usr/share/hades/update/release-serial \
    ${INSTALL}/usr/share/hades/update/keys/*.pub
  echo "hades-update-channel: channel $(sed -n 's/^CHANNEL=//p' ${PKG_DIR}/channel.conf), version ${BUILDER_VERSION} = ${serial}"
}
