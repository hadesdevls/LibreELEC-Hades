# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# The isolated service for the Java (BD-J) menus of Blu-ray discs: the Java code of a disc runs in
# hades-bdj@.service, one instance per disc, never in Kodi's process (Kodi runs as root, and Java
# 24 and later have no security manager). The service's user, its socket, and the folder of the
# discs' persistent storage. The server itself (/usr/lib/hades-bdj/hades-bdj-host) is built with
# libbluray, the Java runtime (/usr/lib/hades-bdj/jre) by openjdk, the jar by libbluray-bdj.
#
# No network (PrivateNetwork=yes) in hades-bdj@.service. BD-Live, the Internet for the Java of a
# disc, is the player's setting (bluray.hadesbdlive, off by default): on, the player connects to
# hades-bdj-net.socket, whose hades-bdj-net@.service reaches IPv4 addresses of the Internet only
# (that unit says how). Names are resolved by the player; the JVM gets them through the resolver of
# the module org.videolan.hades.bdlive (bdlive/, JEP 418), built here with the JDK of the build host
# that also builds openjdk, and installed as /usr/lib/hades-bdj/hades-bdlive.jar.

PKG_NAME="hades-bdj"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="libbluray libbluray-bdj openjdk libseccomp"
PKG_SECTION="mediacenter"
PKG_LONGDESC="The isolated service in which the Java (BD-J) menus of a Blu-ray disc run."
PKG_TOOLCHAIN="manual"

HADES_BDLIVE_JDK="/usr/lib/jvm/java-26-openjdk-amd64"
# the package of the build host that compiles the module, at the version reviewed (openjdk's boot JDK)
HADES_BDLIVE_HOST_DEB="openjdk-26-jdk-headless=26.0.2+10-2~26.04.2"

make_target() {
  local deb="${HADES_BDLIVE_HOST_DEB}" have
  have="$(dpkg-query -W -f='${Version}' "${deb%%=*}" 2>/dev/null)" || :
  [ "${have}" = "${deb#*=}" ] ||
    die "hades-bdj: the build host has ${deb%%=*} ${have:-(not installed)}, the version reviewed is ${deb#*=}"
  mkdir -p ${PKG_BUILD}/bdlive/classes
  # for Java 21 and later (the runtime is openjdk of this tree); classes only of the JDK's API
  "${HADES_BDLIVE_JDK}/bin/javac" --release 21 -Xlint:all -Werror -d ${PKG_BUILD}/bdlive/classes \
    ${PKG_DIR}/bdlive/module-info.java ${PKG_DIR}/bdlive/org/videolan/hades/bdlive/ResolverProvider.java ||
    die "hades-bdj: the module org.videolan.hades.bdlive did not compile"
  # every entry with the date of the sources of this package, so that two builds give the same bytes
  "${HADES_BDLIVE_JDK}/bin/jar" --create --date="2026-11-04T00:00:00Z" --no-manifest \
    --file ${PKG_BUILD}/bdlive/hades-bdlive.jar -C ${PKG_BUILD}/bdlive/classes . ||
    die "hades-bdj: hades-bdlive.jar could not be written"
}

makeinstall_target() {
  # system.d/ and tmpfiles.d/ of this package are installed by the build system
  mkdir -p ${INSTALL}/usr/lib/hades-bdj
    cp -p ${PKG_BUILD}/bdlive/hades-bdlive.jar ${INSTALL}/usr/lib/hades-bdj/
}

post_install() {
  # a user of its own: nothing else runs as it, it owns nothing but the discs' persistent storage
  add_group hadesbdj 975
  add_user hadesbdj x 975 975 "Blu-ray Java menus" "/var/lib/hades-bdj" "/bin/false"
  enable_service hades-bdj.socket
  enable_service hades-bdj-net.socket
}
