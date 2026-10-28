# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

PKG_NAME="nvidia-legacy-open"
PKG_VERSION="580.196.07"
# NVIDIA's open-gpu-kernel-modules at the tag 580.196.07 (git ls-remote: the tag object
# 1365c67cd1de65aaed10398025ae4063f4c64552 points at this commit). Its kernel-open/ is the
# kernel-open/ of the 580.196.07 .run file by file, but for the two prebuilt objects.
PKG_COMMIT="02e64bf229e77873c9df22ebcfed6a3e0b813206"
PKG_SHA256="5e9e92f1c70d1487a365179704ae14bab3eedfd60b5e005c214bba40a926db45"
PKG_ARCH="x86_64"
PKG_LICENSE="MIT OR GPL-2.0-only"
PKG_SITE="https://github.com/NVIDIA/open-gpu-kernel-modules"
PKG_URL="${PKG_SITE}/archive/${PKG_COMMIT}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="The open parts of NVIDIA's 580 driver, NVKMS and nvidia-drm, with Hades' patches, for the kernel modules of nvidia-legacy."
PKG_TOOLCHAIN="manual"

PKG_IS_KERNEL_PKG="yes"

make_target() {
  # Only the OS-agnostic core of NVKMS (src/nvidia-modeset, nv-modeset-kernel.o): nvidia-legacy
  # builds the modules from the kernel/ tree of the .run, whose nvidia.ko needs the closed RM
  # core of Maxwell, Pascal and Volta (the open one is GSP only). Built with the compiler of
  # the kernel and without the target's user-space flags, as nvidia-open-kernel-modules.
  unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

  make -C src/nvidia-modeset \
    TARGET_ARCH=x86_64 \
    ARCH=x86_64 \
    CC=${CC} \
    LD=${LD} \
    AR=${AR} \
    CXX=${CXX} \
    OBJCOPY=${OBJCOPY}
}

makeinstall_target() {
  # Nothing of this goes in the image by itself: nvidia-legacy takes the NVKMS core and the
  # sources of nvidia-drm from this build directory.
  :
}
