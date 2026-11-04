# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

# The Java runtime for BD-J (the Java menus of Blu-ray discs), private to the BD-J service:
# /usr/lib/hades-bdj/jre, its JAVA_HOME. Nothing else in the image uses it, and nothing of it is on
# a search path: no /usr/bin/java, no library in /usr/lib, nothing in the sysroot.
#
# OpenJDK is cross-compiled from the archive of its tag with the toolchain of the image (the build
# JDK that a cross build needs is built from the same source on the way), only the server VM and
# without debug symbols; then jlink makes a runtime of the modules that libbluray's Java code needs.
#
# What libbluray asks of the JVM (src/libbluray/bdj/bdj.c, _create_jvm): java.awt.headless=false
# with its own toolkit and graphics environment, and java.base and java.desktop patched with its
# two jars, reading java.desktop, java.rmi and java.xml. With headless=false libawt loads
# libawt_xawt.so, which needs the X11 client libraries even though it never opens a display. The
# image has no X11 (Generic is GBM), and they must not appear in the sysroot or in /usr/lib, where
# other packages (Mesa, libva, FFmpeg, SDL...) would find them and change. So they are built here,
# from the archives and patches of LibreELEC's own X11 recipes (PKG_DEPENDS_UNPACK: unpacked, never
# built as packages), with the options of those recipes, into a prefix inside this package's build
# directory; only those the runtime loads are copied into it, beside libawt_xawt.so.
#
# Third-party code the JDK bundles: libjpeg, giflib, libpng and Little CMS are the JDK's own copies
# (the image has none of them as a shared library); zlib, FreeType, HarfBuzz, fontconfig (loaded at
# run time) and ALSA are the image's. CUPS: only its headers, which the JDK's configure requires;
# libcups is loaded at run time if present, and the image has none (no printing).

PKG_NAME="openjdk"
PKG_VERSION="27.0.1.0.3"
PKG_SHA256="d4f0d695767eee4842b1e6a22daf5af6a87bcaeea68f0597538a97bd8fb340a6"
PKG_LICENSE="GPL-2.0-only WITH Classpath-exception-2.0"
PKG_SITE="https://openjdk.org/"
PKG_URL="https://github.com/openjdk/jdk${PKG_VERSION%%.*}u/archive/refs/tags/jdk-${PKG_VERSION}-ga.tar.gz"
PKG_SOURCE_DIR="jdk${PKG_VERSION%%.*}u-jdk-${PKG_VERSION}-ga"
PKG_DEPENDS_TARGET="toolchain Python3:host alsa-lib fontconfig freetype harfbuzz zlib"
# the X11 client libraries and the CUPS headers: unpacked with the checks and patches of their own
# recipes, built (or read) by this recipe only
PKG_DEPENDS_UNPACK="cups util-macros xorgproto xtrans xcb-proto libpthread-stubs libXau libxcb \
                    libX11 libXext libXrender libXfixes libXi libXtst libXrandr"
for _hades_pkg in ${PKG_DEPENDS_UNPACK}; do
  PKG_NEED_UNPACK+=" $(get_pkg_directory ${_hades_pkg})"
done
unset _hades_pkg
PKG_LONGDESC="OpenJDK, a Java runtime private to the BD-J menus of Blu-ray discs."
PKG_TOOLCHAIN="manual"
PKG_BUILD_FLAGS="-sysroot"

# The commit of the tag (jdk-27.0.1.0.3-ga and jdk-27.0.1.0.3+1 name the same one: build 1). Checked
# against the archive, and written as the source revision of the runtime (its "release" file).
HADES_OPENJDK_COMMIT="53eadcb2b7896ba55b86bcf5378cb3fab612063c"
HADES_OPENJDK_BUILD="1"

# The boot JDK: OpenJDK 26 of the build host (Ubuntu 26.04, universe, signed by its archive), at the
# version that was reviewed for it (the supply-chain manifest); an update of the host stops the
# build until the new one is reviewed.
HADES_OPENJDK_BOOT_JDK="/usr/lib/jvm/java-26-openjdk-amd64"
HADES_OPENJDK_HOST_DEBS="openjdk-26-jdk-headless=26.0.2+10-2~26.04.2 openjdk-26-jre-headless=26.0.2+10-2~26.04.2"

# The modules of the runtime: those libbluray's two jars use (jdeps) and those its JVM options name
# (--add-reads), with what they require (java.datatransfer, java.prefs, java.logging); and what jdeps
# cannot see because it is reached through a service, so that a disc's Java behaves as on the full
# JREs BD-J was written for: jdk.charsets (text in Shift_JIS, Big5, EUC-KR... decodes) and
# jdk.localedata (dates, numbers and names in the disc's language, not in English). Measured in the
# squashfs of the image (zstd, level 19): jdk.charsets adds 0.5 MiB, jdk.localedata 5.4 MiB.
# No jlink compression: the squashfs compresses better what jlink leaves uncompressed (22.1 MiB
# against 32.4 MiB with --compress=zip-6, measured with the modules above minus jdk.localedata).
HADES_OPENJDK_MODULES="java.base,java.desktop,java.rmi,java.xml,jdk.charsets,jdk.localedata"

# The private runtime, the JAVA_HOME of the BD-J service in the image
HADES_OPENJDK_HOME="/usr/lib/hades-bdj/jre"

# (The build runs with errexit, config/path: a command that fails stops it; a subshell or function
# tested with || or && would run its commands without it, so none is.)

pre_configure_target() {
  local deb have
  for deb in ${HADES_OPENJDK_HOST_DEBS}; do
    have="$(dpkg-query -W -f='${Version}' "${deb%%=*}" 2>/dev/null || :)"
    [ "${have}" = "${deb#*=}" ] ||
      die "openjdk: the build host has ${deb%%=*} ${have:-(not installed)}, the version reviewed is ${deb#*=}"
  done
  [ -x "${HADES_OPENJDK_BOOT_JDK}/bin/javac" ] ||
    die "openjdk: no boot JDK in ${HADES_OPENJDK_BOOT_JDK} (the build host needs openjdk-26-jdk-headless)"

  # the archive whose SHA256 scripts/get checked is the tree of the tag's commit
  local archive="${SOURCES}/${PKG_NAME}/${PKG_NAME}-${PKG_VERSION}.tar.gz" commit
  commit="$( (gzip -dc "${archive}" 2>/dev/null || :) | git get-tar-commit-id || :)"
  [ "${commit}" = "${HADES_OPENJDK_COMMIT}" ] ||
    die "openjdk: ${archive} is the tree of ${commit:-an unknown commit}, not of ${HADES_OPENJDK_COMMIT}"
  [ "$(sed -n 's/^DEFAULT_VERSION_EXTRA1=//p' ${PKG_BUILD}/make/conf/version-numbers.conf)" = "${PKG_VERSION##*.}" ] ||
    die "openjdk: make/conf/version-numbers.conf is not of ${PKG_VERSION}"

  # what make/SourceRevision.gmk takes when the tree is not a git checkout
  echo ".:git:${HADES_OPENJDK_COMMIT:0:12}" >${PKG_BUILD}/.src-rev

  # On Linux the JDK includes X11/Intrinsic.h nowhere (only its AIX input method does), but its
  # configure checks for it; libXt is not built for that check. Fail closed if that changes.
  if grep -rl --include='*.c' --include='*.cpp' --include='*.h' 'X11/Intrinsic.h' ${PKG_BUILD}/src |
      grep -v '/src/java.desktop/aix/'; then
    die "openjdk: a source outside AIX includes X11/Intrinsic.h: libXt would be needed"
  fi
}

# Build one X11 package of LibreELEC into the private prefix, from a copy of its unpacked (and
# patched) tree. Arguments: the package, "pic" or "-" (its recipe's +pic), then the configure
# options of its recipe.
hades_openjdk_x11_pkg() {
  local pkg="${1}" pic="${2}" x11="${PKG_BUILD}/.hades-x11" src="${PKG_BUILD}/.hades-x11-src/${1}"
  shift 2
  build_msg "CLR_BUILD" "BUILD" "${pkg} (private to ${PKG_NAME})"
  rm -rf "${src}"
  cp -a "$(get_build_dir ${pkg})" "${src}"
  (
    cd "${src}"
    if [ "${pic}" = "pic" ]; then
      export CFLAGS="${CFLAGS} ${CFLAGS_OPTIM_PIC}" LDFLAGS="${LDFLAGS} ${LDFLAGS_OPTIM_PIC}"
    fi
    if [ "${pkg}" = "libX11" ]; then
      # LibreELEC patches its nls/Makefile.am and regenerates its build system (PKG_TOOLCHAIN
      # autotools, scripts/autoreconf); the same here, with the macros of util-macros and xtrans
      # from the private prefix
      touch NEWS AUTHORS ChangeLog
      mkdir -p m4
      AUTOCONF="${TOOLCHAIN}/bin/autoconf" AUTOMAKE="${TOOLCHAIN}/bin/automake" \
        AUTOHEADER="${TOOLCHAIN}/bin/autoheader" LIBTOOLIZE="${TOOLCHAIN}/bin/libtoolize" \
        ACLOCAL="${TOOLCHAIN}/bin/aclocal -I ${x11}/usr/share/aclocal" GTKDOCIZE=true \
        "${TOOLCHAIN}/bin/autoreconf" --verbose --force --install -I "${x11}/usr/share/aclocal"
    fi
    ./configure ${TARGET_CONFIGURE_OPTS} "$@"
    case "${pkg}" in
      libXext | libXi | libXtst) libtool_remove_rpath libtool ;;
    esac
    make
    make install DESTDIR="${x11}"
  )
  # libtool archives would name /usr/lib, which here is not where these are
  find "${x11}" -name '*.la' -delete
}

# The X11 client libraries that libawt_xawt.so and libsplashscreen.so need, with what building them
# takes, in ${PKG_BUILD}/.hades-x11 (a DESTDIR): pkg-config sees only that prefix (as its sysroot);
# the compiler and the linker find the C library in the toolchain's sysroot as always.
hades_openjdk_x11() {
  local x11="${PKG_BUILD}/.hades-x11"
  rm -rf "${x11}" "${PKG_BUILD}/.hades-x11-src"
  mkdir -p "${x11}" "${PKG_BUILD}/.hades-x11-src"
  (
    export PKG_CONFIG_PATH=""
    export PKG_CONFIG_LIBDIR="${x11}/usr/lib/pkgconfig:${x11}/usr/share/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="${x11}"
    export CPPFLAGS="${TARGET_CPPFLAGS}"
    export CFLAGS="${TARGET_CFLAGS}"
    export CXXFLAGS="${TARGET_CXXFLAGS}"
    export LDFLAGS="${TARGET_LDFLAGS} -L${x11}/usr/lib -Wl,-rpath-link,${x11}/usr/lib"

    hades_openjdk_x11_pkg util-macros -
    hades_openjdk_x11_pkg xorgproto - --disable-legacy
    hades_openjdk_x11_pkg xtrans - --without-xmlto
    hades_openjdk_x11_pkg xcb-proto -
    hades_openjdk_x11_pkg libpthread-stubs -
    hades_openjdk_x11_pkg libXau pic --enable-static --disable-shared --enable-xthreads
    hades_openjdk_x11_pkg libxcb pic --disable-screensaver --disable-xprint --disable-selinux --disable-xvmc
    hades_openjdk_x11_pkg libX11 - --disable-loadable-i18n --disable-loadable-xcursor --enable-xthreads \
      --disable-xcms --enable-xlocale --disable-xlocaledir --enable-xkb \
      --with-keysymdefdir="${x11}/usr/include/X11" --disable-xf86bigfont --enable-malloc0returnsnull \
      --disable-specs --without-xmlto --without-fop --enable-composecache --disable-lint-library \
      --disable-ipv6 --without-launchd --without-lint
    hades_openjdk_x11_pkg libXext - --enable-malloc0returnsnull --without-xmlto
    hades_openjdk_x11_pkg libXrender pic
    hades_openjdk_x11_pkg libXfixes pic
    hades_openjdk_x11_pkg libXi pic --enable-malloc0returnsnull --disable-silent-rules --disable-docs \
      --disable-specs --without-xmlto --without-fop --without-xsltproc --without-asciidoc --with-gnu-ld
    hades_openjdk_x11_pkg libXtst - --with-gnu-ld --without-xmlto
  )

  # libXrandr: only its header (awt_GraphicsEnv.c); the library is loaded at run time if present
  mkdir -p "${x11}/usr/include/X11/extensions"
  cp "$(get_build_dir libXrandr)/include/X11/extensions/Xrandr.h" "${x11}/usr/include/X11/extensions/"
}

# The public headers of CUPS (COREHEADERS and DRIVERHEADERS of cups/Makefile: what its install puts
# in /usr/include/cups), for the JDK's configure and CUPSfuncs.c.
hades_openjdk_cups() {
  local cups="$(get_build_dir cups)" dest="${PKG_BUILD}/.hades-cups/include/cups" h n=0
  rm -rf "${PKG_BUILD}/.hades-cups"
  mkdir -p "${dest}"
  for h in $(sed -n '/^COREHEADERS[[:space:]]*=/,/^$/p;/^DRIVERHEADERS[[:space:]]*=/,/^$/p' "${cups}/cups/Makefile" |
             tr -d '\\' | tr -s ' \t' '\n' | grep '\.h$'); do
    cp "${cups}/cups/${h}" "${dest}/" || die "openjdk: no ${h} in ${cups}/cups"
    n=$((n + 1))
  done
  [ -s "${dest}/cups.h" -a -s "${dest}/ppd.h" ] || die "openjdk: the CUPS headers are not there (${n} copied)"
}

configure_target() {
  local x11="${PKG_BUILD}/.hades-x11"
  hades_openjdk_x11
  hades_openjdk_cups

  # The JDK takes its own flags and warns that it ignores these; the target's CPU goes in as an
  # extra flag. The linker gets the private X11 prefix to resolve libX11's own dependencies. The
  # tools are named on the command line, which is where the JDK's configure takes them from (it
  # ignores AR, NM, BUILD_CC... in the environment): the target's from the toolchain, the build
  # host's for the build JDK.
  # ac_cv_header_X11_Intrinsic_h: see pre_configure_target, which stops the build if a source of
  # this JDK outside AIX includes that header.
  unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS

  # The build JDK is compiled with the options of the target (zlib of the system); for the build
  # host that is the zlib of the toolchain (zlib:host), which the host compilers reach as LibreELEC's
  # host builds do (HOST_CFLAGS, HOST_LDFLAGS: -I, -L and -rpath of ${TOOLCHAIN}).
  local bcc="${PKG_BUILD}/.hades-cc" t
  mkdir -p "${bcc}"
  for t in "cc ${HOST_CC}" "c++ ${HOST_CXX}"; do
    cat >"${bcc}/${t%% *}" <<EOF
#!/bin/sh
exec ${t#* } -I${TOOLCHAIN}/include "\$@" -L${TOOLCHAIN}/lib -Wl,-rpath,${TOOLCHAIN}/lib
EOF
    chmod +x "${bcc}/${t%% *}"
  done

  bash ${PKG_BUILD}/configure \
    CC="${TARGET_PREFIX}gcc" CXX="${TARGET_PREFIX}g++" AR="${TARGET_PREFIX}ar" NM="${TARGET_PREFIX}nm" \
    OBJCOPY="${TARGET_PREFIX}objcopy" OBJDUMP="${TARGET_PREFIX}objdump" STRIP="${TARGET_PREFIX}strip" \
    BUILD_CC="${bcc}/cc" BUILD_CXX="${bcc}/c++" \
    ac_cv_header_X11_Intrinsic_h=yes \
    --openjdk-target=${TARGET_NAME} \
    --with-sysroot=${SYSROOT_PREFIX} \
    --with-boot-jdk=${HADES_OPENJDK_BOOT_JDK} \
    --with-jvm-variants=server \
    --with-debug-level=release \
    --with-native-debug-symbols=none \
    --disable-warnings-as-errors \
    --with-version-pre= \
    --with-version-opt= \
    --with-version-build=${HADES_OPENJDK_BUILD} \
    --with-vendor-name="LibreELEC Hades" \
    --with-vendor-url="https://github.com/hadesdevls/LibreELEC-Hades" \
    --with-vendor-bug-url="https://github.com/hadesdevls/LibreELEC-Hades/issues" \
    --with-vendor-vm-bug-url="https://github.com/hadesdevls/LibreELEC-Hades/issues" \
    --with-source-date=version \
    --with-stdc++lib=dynamic \
    --with-extra-cflags="-march=${TARGET_CPU}" \
    --with-extra-cxxflags="-march=${TARGET_CPU}" \
    --with-extra-ldflags="-Wl,-rpath-link,${x11}/usr/lib" \
    --x-includes=${x11}/usr/include \
    --x-libraries=${x11}/usr/lib \
    --with-cups-include=${PKG_BUILD}/.hades-cups/include \
    --with-alsa-include=${SYSROOT_PREFIX}/usr/include \
    --with-alsa-lib=${SYSROOT_PREFIX}/usr/lib \
    --with-fontconfig-include=${SYSROOT_PREFIX}/usr/include \
    --with-freetype=system \
    --with-harfbuzz=system \
    --with-zlib=system \
    --with-libjpeg=bundled \
    --with-giflib=bundled \
    --with-libpng=bundled \
    --with-lcms=bundled \
    --with-jobs=${CONCURRENCY_MAKE_LEVEL} || die "openjdk: configure failed"
}

make_target() {
  # the jmods, and the release file the JDK's own images take their release information from; the
  # JDK's make takes JOBS, not a -j in MAKEFLAGS
  env -u MAKEFLAGS make JOBS=${CONCURRENCY_MAKE_LEVEL} jmods release-file || die "openjdk: make failed"

  # jlink of the build JDK (built from this source, for the build host) links the target's jmods,
  # with the options of the JDK's own runtime images (make/Images.gmk: JLINK_TOOL and
  # JLINK_JRE_EXTRA_OPTS): the release information, the resources in the order of the class list
  # the build generated, the pregenerated java.lang.invoke classes, one copy of each legal notice
  local out="$(pwd)" jre="${PKG_BUILD}/.hades-jre"
  local jlink="${out}/buildjdk/jdk/bin/jlink" lo="${out}/support/link_opt"
  [ -x "${jlink}" ] || die "openjdk: no jlink in the build JDK"
  [ -s "${out}/jdk/release" -a -s "${lo}/classlist" -a -s "${lo}/default_jli_trace.txt" ] ||
    die "openjdk: the build left no release file or no class list"
  rm -rf "${jre}"
  "${jlink}" --module-path "${out}/images/jmods" \
             --endian little \
             --release-info "${out}/jdk/release" \
             --order-resources="**module-info.class,@${lo}/classlist,/java.base/java/**,/java.base/jdk/**,/java.base/sun/**,/java.base/com/**,/jdk.localedata/**" \
             --dedup-legal-notices=error-if-not-same-content \
             --generate-jli-classes=@${lo}/default_jli_trace.txt \
             --add-modules "${HADES_OPENJDK_MODULES}" \
             --no-man-pages --no-header-files --strip-debug \
             --output "${jre}" || die "openjdk: jlink failed"

  hades_openjdk_x11_into_jre "${jre}"
}

# Copy into the runtime the X11 libraries its files need (DT_NEEDED, followed through the copies),
# beside libawt_xawt.so; anything needed that is neither in the runtime, nor in the private prefix,
# nor where the target's compiler finds libraries stops the build.
hades_openjdk_x11_into_jre() {
  local jre="${1}" lib="${1}/lib" x11="${PKG_BUILD}/.hades-x11" readelf="${TARGET_PREFIX}readelf"
  local f so real todo more copied=""
  # libawt_xawt.so finds them through its own RPATH ($ORIGIN). DT_RPATH, not DT_RUNPATH: the
  # dynamic loader also follows it for the dependencies of its dependencies (libX11 -> libxcb).
  "${readelf}" -d "${lib}/libawt_xawt.so" | grep -q '(RPATH).*\[\$ORIGIN[]:]' ||
    die "openjdk: libawt_xawt.so has no DT_RPATH \$ORIGIN: the X11 libraries beside it would not be found"
  todo="$(find "${jre}" -type f | sort)"
  while [ -n "${todo}" ]; do
    more=""
    for f in ${todo}; do
      "${readelf}" -h "${f}" >/dev/null 2>&1 || continue
      for so in $("${readelf}" -d "${f}" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        [ -e "${lib}/${so}" -o -e "${lib}/server/${so}" ] && continue
        # what the target's compiler finds (the sysroot, and the toolchain's libgcc_s and libstdc++)
        [ "$(${TARGET_PREFIX}gcc -print-file-name=${so})" != "${so}" ] && continue
        [ -e "${x11}/usr/lib/${so}" ] || die "openjdk: ${f#${jre}/} needs ${so}, which is nowhere"
        real="$(readlink -f "${x11}/usr/lib/${so}")"
        cp -P "${x11}/usr/lib/${so}" "${lib}/"
        [ -e "${lib}/${real##*/}" ] || cp "${real}" "${lib}/"
        copied+=" ${so}"
        more+=" ${lib}/${real##*/}"
      done
    done
    todo="${more}"
  done
  [ -n "${copied}" ] || die "openjdk: no X11 library copied: libawt_xawt.so should need some"
  echo "openjdk: X11 libraries in the runtime:${copied}"

  # their licences (each library is its package: libX11.so.6 is libX11), with those of what is
  # compiled into them (xtrans into libX11, libXau into libxcb, the headers of xorgproto and of
  # libXfixes into the others), beside the JDK's own notices
  local legal="${jre}/legal/hades-x11" pkg
  for pkg in $(for so in ${copied}; do echo "${so%%.so*}"; done) xtrans libXau libXfixes xorgproto; do
    [ -d "${legal}/${pkg}" ] && continue
    mkdir -p "${legal}/${pkg}"
    cp "$(get_build_dir ${pkg})"/COPYING* "${legal}/${pkg}/" || die "openjdk: no licence of ${pkg}"
  done
}

makeinstall_target() {
  mkdir -p ${INSTALL}${HADES_OPENJDK_HOME%/*}
  cp -a ${PKG_BUILD}/.hades-jre ${INSTALL}${HADES_OPENJDK_HOME}
}
