# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2017-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="x265"
PKG_VERSION="4.2"
PKG_SHA256="04978f795943e49fcea76eb5ede9c1bd0fe9b6c073518897be6fc43b44f60850"
# When changing PKG_VERSION remember to sync PKG_X265_SONAME with X265_BUILD in source/CMakeLists.txt
PKG_X265_SONAME="216"
PKG_ARCH="aarch64 x86_64"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://www.videolan.org/developers/x265.html"
PKG_URL="https://bitbucket.org/multicoreware/x265_git/get/${PKG_VERSION}.tar.bz2"
PKG_DEPENDS_HOST="toolchain:host"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="x265 is a H.265/HEVC video encoder application library"
PKG_TOOLCHAIN="make"

pre_configure_target() {
  LDFLAGS+=" -ldl"
  ${CMAKE} -G "Unix Makefiles" ./source
}

# Hades: x265 for the build host, for the clips of "Check this setup" (packages/mediacenter/hades-check).
# Only the command line program, of 8 bits with the 10-bit library linked in (as x265's own
# build/linux/multilib.sh makes it), static. So that what it encodes depends on its source and not on the
# build host: C code only (no assembly), for the baseline of the host's architecture (without
# -march=native), with floating-point arithmetic as written (no contraction, and without the -ffast-math
# that x265's CMakeLists.txt adds where the compiler takes it). It goes outside the toolchain's PATH, where
# nothing else finds it, and never into the image. (The target build above is not changed by this: it
# configures in ${PKG_BUILD}, these two trees are under ${PKG_BUILD}/.${HOST_NAME}.)
make_host() {
  local cflags="${CFLAGS//${HOST_CFLAGS_OPTIM_NATIVE}/} -ffp-contract=off"
  local cxxflags="${CXXFLAGS//${HOST_CFLAGS_OPTIM_NATIVE}/} -ffp-contract=off"
  local common=(-G "Unix Makefiles"
    -DCMAKE_C_FLAGS="${cflags}" -DCMAKE_CXX_FLAGS="${cxxflags}"
    -DENABLE_SHARED=OFF -DENABLE_ASSEMBLY=OFF -DENABLE_LIBNUMA=OFF -DCC_HAS_FAST_MATH=OFF)

  mkdir -p ${PKG_BUILD}/.${HOST_NAME}/main10 ${PKG_BUILD}/.${HOST_NAME}/main
  cd ${PKG_BUILD}/.${HOST_NAME}/main10
  ${CMAKE} "${common[@]}" -DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=OFF -DENABLE_CLI=OFF ${PKG_BUILD}/source
  make

  cd ${PKG_BUILD}/.${HOST_NAME}/main
  ln -sf ../main10/libx265.a libx265_main10.a
  ${CMAKE} "${common[@]}" -DEXTRA_LIB="x265_main10.a" -DEXTRA_LINK_FLAGS=-L. -DLINKED_10BIT=ON -DENABLE_CLI=ON \
    ${PKG_BUILD}/source
  make
}

makeinstall_host() {
  mkdir -p ${TOOLCHAIN}/lib/hades-check/bin
  cp ${PKG_BUILD}/.${HOST_NAME}/main/x265 ${TOOLCHAIN}/lib/hades-check/bin/x265
}
