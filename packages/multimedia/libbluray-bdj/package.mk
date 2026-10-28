# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2019-present Team LibreELEC (https://libreelec.tv)
# Copyright (C) 2026-present Hades

# The Java part of libbluray for BD-J menus: libbluray-j2se-<version>.jar and
# libbluray-awt-j2se-<version>.jar in /usr/share/java, where the library looks for them
# (../share/java next to it). No Java runtime: the user installs "JRE for BD-J menus"
# (tools.jre.zulu, a Zulu 8) from LibreELEC's repository.
#
# libbluray takes only the jar of its own version, because the two halves call each other by
# signature (1.5.0 changed the constructor of org.videolan.TIMark that the native side calls). So
# the jar is built here from the archive and the patches of packages/multimedia/libbluray: the
# version in its name and its Java code are those of the library of the image. The add-on brings
# the jar of the libbluray it was built with (1.4.0 in tools.jre.zulu-12.80.7.0), which cannot
# work with this one; libbluray-9036 makes the library look here when the add-on's LIBBLURAY_CP
# holds no jar of its version.
#
# A package of its own, not -Dbdj_jar=enabled in libbluray's recipe: the library of the image stays
# as it is. The JDK that compiles the jar is given to meson as jdk_home, and libbluray writes that
# path into the library as a place to load a JVM from (JDK_HOME, src/libbluray/bdj/bdj.c): a path
# of the build host would end up in the image, and the library would look for a JVM there. The
# library that is built here beside the jar is not installed.
#
# After LibreELEC's jre-libbluray (packages/addons/addon-depends/jre-depends), which compiles the
# jar of its add-on with Azul's JDK 8; here, the JDK 8 of the build host (Ubuntu:
# openjdk-8-jdk-headless, and ant), named, never what is first on PATH (OpenJDK 25 there, for
# Kodi's build). With a javac older than 9, libbluray's build compiles its classes for Java 1.4 and
# ASM for 1.5 (class files of version 48 and 49), as build.xml intends; a newer javac would take
# 1.8 without --release and could link to methods that Java 8 does not have.

. $(get_pkg_directory libbluray)/package.mk

PKG_NAME="libbluray-bdj"
PKG_LICENSE="LGPL-2.1-or-later AND BSD-3-Clause"
PKG_LONGDESC="The Java part of libbluray for BD-J menus, built from the archive and patches of libbluray."
PKG_URL=""
PKG_DEPENDS_UNPACK+=" libbluray"
PKG_PATCH_DIRS+=" $(get_pkg_directory libbluray)/patches"
PKG_BUILD_FLAGS="-sysroot"

HADES_BDJ_JDK="/usr/lib/jvm/java-8-openjdk-amd64"
# the packages of the build host that make the jar, at the versions that were reviewed for it (the
# supply-chain manifest): an update of the host stops the build until the new one is reviewed
HADES_BDJ_HOST_DEBS="openjdk-8-jdk-headless=8u504-ga-1ubuntu1~26.04.3 openjdk-8-jre-headless=8u504-ga-1ubuntu1~26.04.3 ant=1.10.15-1build1"
# the classes of libbluray (1.4: 48) and of ASM (1.5: 49), and nothing newer
HADES_BDJ_MAX_CLASS=49
PKG_MESON_OPTS_TARGET="${PKG_MESON_OPTS_TARGET/-Dbdj_jar=disabled/-Dbdj_jar=enabled} \
                       -Djdk_home=${HADES_BDJ_JDK}"

unpack() {
  local archive="${SOURCES}/libbluray/libbluray-${PKG_VERSION}.tar.xz"
  # the archive that libbluray's recipe verified when it was downloaded, checked again here
  echo "${PKG_SHA256}  ${archive}" | sha256sum -c --quiet - ||
    die "libbluray-bdj: ${archive} is not the archive of libbluray ${PKG_VERSION}"
  mkdir -p ${PKG_BUILD}
  tar --strip-components=1 -xf "${archive}" -C ${PKG_BUILD}
}

pre_configure_target() {
  # Fail closed: the jar is compiled and packed by the JDK 8 named above, or the build stops.
  local jdk="${HADES_BDJ_JDK}" deb have
  for deb in ${HADES_BDJ_HOST_DEBS}; do
    have="$(dpkg-query -W -f='${Version}' "${deb%%=*}" 2>/dev/null)"
    [ "${have}" = "${deb#*=}" ] ||
      die "libbluray-bdj: the build host has ${deb%%=*} ${have:-(not installed)}, the version reviewed is ${deb#*=}"
  done
  [ -x "${jdk}/bin/javac" -a -x "${jdk}/bin/java" ] ||
    die "libbluray-bdj: no JDK in ${jdk} (the build host needs openjdk-8-jdk-headless)"
  case "$("${jdk}/bin/javac" -version 2>&1)" in
    "javac 1.8.0_"*) ;;
    *) die "libbluray-bdj: ${jdk}/bin/javac is not Java 8: $("${jdk}/bin/javac" -version 2>&1)" ;;
  esac
  case "$("${jdk}/bin/java" -version 2>&1 | head -n 1)" in
    *'version "1.8.0_'*) ;;
    *) die "libbluray-bdj: ${jdk}/bin/java is not Java 8" ;;
  esac
  command -v ant >/dev/null || die "libbluray-bdj: no ant on the build host"

  # ant runs on JAVA_HOME; meson finds javac on PATH (libbluray's meson.build passes that javac to
  # ant as the compiler, and picks the class versions by its version)
  export JAVA_HOME="${jdk}"
  export PATH="${jdk}/bin:${PATH}"
  case "$(ant -diagnostics 2>/dev/null | sed -n 's/^java.version : //p')" in
    "1.8.0_"*) ;;
    *) die "libbluray-bdj: ant does not run on the Java 8 of ${jdk}" ;;
  esac
}

post_configure_target() {
  # what meson took: the javac of that JDK, for the compiler (a name that it runs from PATH, set above)
  # and for ant (a full path)
  python3 -I - "${PKG_BUILD}/.${TARGET_NAME}" "${HADES_BDJ_JDK}/bin/javac" <<'EOF' ||
import json, os, shutil, sys
build, javac = sys.argv[1], os.path.realpath(sys.argv[2])
java = json.load(open(os.path.join(build, "meson-info", "intro-compilers.json"))).get("host", {}).get("java")
exe = shutil.which(java["exelist"][0]) if java else None
if not exe or os.path.realpath(exe) != javac or not java["version"].startswith("1.8.0"):
    sys.exit(f"meson took another Java compiler: {java} ({exe})")
ninja = open(os.path.join(build, "build.ninja")).read()
if f"-Djavac_path={sys.argv[2]}" not in ninja and f"-Djavac_path={javac}" not in ninja:
    sys.exit("ant is not given that javac (-Djavac_path)")
EOF
    die "libbluray-bdj: the jar would not be compiled by ${HADES_BDJ_JDK}"
}

pre_make_target() {
  # the version that the library looks for in the name of the jar (its meson.build, patched)
  HADES_BDJ_VERSION="$(sed -n "s/^ *version: '\([0-9.]*\)',$/\1/p" ${PKG_BUILD}/meson.build | head -n 1)"
  [ -n "${HADES_BDJ_VERSION}" ] || die "libbluray-bdj: no version in ${PKG_BUILD}/meson.build"
  # only the jars: the library of this build is not installed
  PKG_MAKE_OPTS_TARGET="src/libbluray/bdj/libbluray-j2se-${HADES_BDJ_VERSION}.jar"
}

makeinstall_target() {
  local jar date
  # every entry of the jars gets the date of the files of libbluray's archive (the newest one), so
  # that two builds give the same bytes
  date="$(tar -tvJf "${SOURCES}/libbluray/libbluray-${PKG_VERSION}.tar.xz" | awk '{print $4}' | sort | tail -n 1)"
  mkdir -p ${INSTALL}/usr/share/java
  for jar in libbluray-j2se-${HADES_BDJ_VERSION}.jar libbluray-awt-j2se-${HADES_BDJ_VERSION}.jar; do
    [ -f "${PKG_BUILD}/.${TARGET_NAME}/src/libbluray/bdj/${jar}" ] || die "libbluray-bdj: ${jar} was not built"
    python3 -I ${PKG_DIR}/scripts/jar-same-bytes.py \
      "${PKG_BUILD}/.${TARGET_NAME}/src/libbluray/bdj/${jar}" \
      "${INSTALL}/usr/share/java/${jar}" "${date}" "${HADES_BDJ_MAX_CLASS}" ||
      die "libbluray-bdj: ${jar} could not be written"
  done

  # The jar holds ASM (contrib/asm of libbluray's archive), whose BSD licence asks for its notice to go with
  # the binary: its own text, with its copyright line (licenses/BSD-3-Clause.txt is the generic one).
  [ -f ${PKG_BUILD}/contrib/asm/LICENSE.txt ] || die "libbluray-bdj: no contrib/asm/LICENSE.txt"
  mkdir -p ${INSTALL}/usr/share/licenses/${PKG_NAME}
    cp -p ${PKG_BUILD}/contrib/asm/LICENSE.txt ${INSTALL}/usr/share/licenses/${PKG_NAME}/ASM-LICENSE.txt
}
