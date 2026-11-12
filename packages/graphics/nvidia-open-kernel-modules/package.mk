# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

PKG_NAME="nvidia-open-kernel-modules"
PKG_VERSION="620.70.06"
PKG_COMMIT="e6a2e01579fbbd1ca2ed43fef2ee2589e9bd8bd9"
PKG_SHA256="b10b72830aa2c1367f0013d3d31fad3d0865330f37da2ed893f0a726125d4175"
PKG_ARCH="x86_64"
PKG_LICENSE="MIT OR GPL-2.0-only"
PKG_SITE="https://github.com/NVIDIA/open-gpu-kernel-modules"
PKG_URL="${PKG_SITE}/archive/${PKG_COMMIT}.tar.gz"
PKG_DEPENDS_TARGET="toolchain linux"
PKG_LONGDESC="NVIDIA open GPU kernel modules (Turing and later), built entirely from source."
PKG_TOOLCHAIN="manual"

PKG_IS_KERNEL_PKG="yes"

make_target() {
  # The OS-agnostic parts (src/nvidia, src/nvidia-modeset) are compiled from source instead of
  # using the prebuilt nv-*-kernel.o_binary objects of the driver .run. Build with the same
  # compiler as the kernel and without the target's user-space flags.
  unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

  # Hades: the "nvidia id" that the modules carry names the user and host of BUILD_IDENTITY_USER and
  # BUILD_IDENTITY_HOST and the time of SOURCE_DATE_EPOCH (config/options), not those of this build
  local nvid=(NV_BUILD_USER="${BUILD_IDENTITY_USER}" NV_BUILD_HOST="${BUILD_IDENTITY_HOST}")
  [ -n "${SOURCE_DATE_EPOCH}" ] && nvid+=(DATE="LC_ALL=C date -u -d @${SOURCE_DATE_EPOCH}")

  make modules \
    TARGET_ARCH=x86_64 \
    ARCH=x86_64 \
    CC=${CC} \
    LD=${LD} \
    AR=${AR} \
    CXX=${CXX} \
    OBJCOPY=${OBJCOPY} \
    SYSSRC=$(kernel_path) \
    SYSOUT=$(kernel_path) \
    "${nvid[@]}"

  ${STRIP} --strip-debug kernel-open/nvidia.ko \
                         kernel-open/nvidia-modeset.ko \
                         kernel-open/nvidia-drm.ko \
                         kernel-open/nvidia-uvm.ko
}

makeinstall_target() {
  mkdir -p ${INSTALL}/$(get_full_module_dir)/nvidia
    cp -P kernel-open/nvidia.ko         ${INSTALL}/$(get_full_module_dir)/nvidia
    cp -P kernel-open/nvidia-drm.ko     ${INSTALL}/$(get_full_module_dir)/nvidia
    cp -P kernel-open/nvidia-uvm.ko     ${INSTALL}/$(get_full_module_dir)/nvidia
    cp -P kernel-open/nvidia-modeset.ko ${INSTALL}/$(get_full_module_dir)/nvidia
}
