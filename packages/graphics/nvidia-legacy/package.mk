# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2021-present Team LibreELEC (https://libreelec.tv)
# Copyright (C) 2026-present Hades

PKG_NAME="nvidia-legacy"
# The newest release of NVIDIA's 580 branch, the last one for Maxwell, Pascal and Volta (the
# 615 driver of nvidia lists them as legacybranch "580.xx" in its supported-gpus.json).
PKG_VERSION="580.196.07"
PKG_SHA256="2189c47fa87ae45b7a166b58c3742dd1629035069cb0fb4103ed375229c55867"
PKG_ARCH="x86_64"
PKG_LICENSE="LicenseRef-nonfree"
PKG_SITE="https://www.nvidia.com/en-us/drivers/unix/"
PKG_URL="https://download.nvidia.com/XFree86/Linux-x86_64/${PKG_VERSION}/NVIDIA-Linux-x86_64-${PKG_VERSION}-no-compat32.run"
PKG_DEPENDS_TARGET="toolchain linux libglvnd nvidia nvidia-legacy-open"
PKG_LONGDESC="NVIDIA's 580 driver for Maxwell, Pascal and Volta (GeForce GTX 900 and 10, TITAN V), which the image chooses at boot by the PCI ID of the card."
PKG_TOOLCHAIN="manual"
# NVIDIA's licence lets its files be passed on only as they are (as nvidia).
PKG_BUILD_FLAGS="-strip"

PKG_IS_KERNEL_PKG="yes"

unpack() {
  # The extraction of nvidia, without executing the .run: its makeself header checks the
  # payload with cksum and md5sum over "tail -n +4" and extracts it with
  # "tail -n +${skip} | zstd -d | tar x"; do the same, and stop on any mismatch. (The header of
  # 580.196.07 differs from that of 615.78.08 in versions, sums, sizes, dates and the help of
  # two options.)
  local run="${SOURCES}/${PKG_NAME}/${PKG_SOURCE_NAME}"
  local skip crc md5
  skip=$(head -n 20 "${run}" | sed -n 's/^skip=\([0-9]\+\)$/\1/p')
  crc=$(head -n 20 "${run}" | sed -n 's/^CRCsum=\([0-9]\+\)$/\1/p')
  md5=$(head -n 20 "${run}" | sed -n 's/^MD5=\([0-9a-f]\{32\}\)$/\1/p')
  [ -n "${skip}" -a -n "${crc}" -a -n "${md5}" ] || die "nvidia-legacy: unexpected .run header"
  [ "$(tail -n +4 "${run}" | cksum | cut -d' ' -f1)" = "${crc}" ] || die "nvidia-legacy: .run CRC mismatch"
  [ "$(tail -n +4 "${run}" | md5sum | cut -b-32)" = "${md5}" ] || die "nvidia-legacy: .run MD5 mismatch"

  [ -d ${PKG_BUILD} ] && rm -rf ${PKG_BUILD}
  mkdir -p ${PKG_BUILD}
  tail -n +${skip} "${run}" | zstd -dq | tar -xf - -C ${PKG_BUILD} --no-same-owner
}

# The files of kernel/ that come from nvidia-legacy-open (with Hades' patches): nvidia-drm, but
# its nvidia-drm-linux.c (which names the licence of the module, "MIT" here, "Dual MIT/GPL" in
# kernel-open/), and the two KAPI headers that nvidia-drm and NVKMS share.
nvidia_legacy_open_files() {
  (cd kernel && ls nvidia-drm/* | grep -v -x 'nvidia-drm/nvidia-drm-linux.c')
  echo common/inc/nvkms-kapi.h
  echo common/inc/nvkms-api-types.h
}

make_target() {
  local open="$(get_build_dir nvidia-legacy-open)"
  local archive="${SOURCES}/nvidia-legacy-open/nvidia-legacy-open-$(get_pkg_version nvidia-legacy-open).tar.gz"
  local top="open-gpu-kernel-modules-$(get_pkg_variable nvidia-legacy-open PKG_COMMIT)"
  local f

  # The closed-flavor tree of the .run, as NVIDIA builds it, but:
  # - its NVKMS core, nv-modeset-kernel.o_binary, is the one compiled from NVIDIA's source of
  #   the same release by nvidia-legacy-open, with Hades' patches (no other closed object of
  #   kernel/ is replaced: nvidia.ko keeps the RM core nv-kernel.o_binary);
  # - nvidia-drm and the KAPI headers are the patched ones. Only where the source of the open
  #   repository is that of this kernel/ before the patches: a file that differs stops the build.
  [ -f "${archive}" ] || die "nvidia-legacy: no ${archive}"
  for f in $(cd "${open}/kernel-open" && ls nvidia-drm/*); do
    [ -e "kernel/${f}" ] || die "nvidia-legacy: kernel-open/${f} of nvidia-legacy-open is not in kernel/"
  done
  for f in $(nvidia_legacy_open_files); do
    tar -xzOf "${archive}" "${top}/kernel-open/${f}" | cmp -s - "kernel/${f}" ||
      die "nvidia-legacy: kernel/${f} of the .run is not the kernel-open/${f} of nvidia-legacy-open"
  done
  for f in $(nvidia_legacy_open_files); do
    cp -p "${open}/kernel-open/${f}" "kernel/${f}"
  done
  cp -p "${open}/src/nvidia-modeset/_out/Linux_x86_64/nv-modeset-kernel.o" \
        kernel/nvidia-modeset/nv-modeset-kernel.o_binary

  unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

  make -C kernel modules \
    NV_KERNEL_MODULES="nvidia nvidia-uvm nvidia-modeset nvidia-drm" \
    TARGET_ARCH=x86_64 \
    ARCH=x86_64 \
    CC=${CC} \
    LD=${LD} \
    AR=${AR} \
    CXX=${CXX} \
    OBJCOPY=${OBJCOPY} \
    SYSSRC=$(kernel_path) \
    SYSOUT=$(kernel_path)

  # nvidia.ko, which holds NVIDIA's closed core (nv-kernel.o_binary), goes as kbuild linked it: strip
  # would take nothing out of it (the core has no debug information, and the kernel is built without
  # any) and only write the file again around NVIDIA's object. scripts/image leaves it as well.
  ${STRIP} --strip-debug kernel/nvidia-modeset.ko \
                         kernel/nvidia-drm.ko \
                         kernel/nvidia-uvm.ko
}

makeinstall_target() {
  local overlay="$(get_kernel_overlay_dir ${PKG_NAME})"
  local open_version="$(get_pkg_version nvidia)"
  local name file

  # The agreement of this driver is the one in licenses/ (that of 615.78.08, byte for byte).
  cmp -s LICENSE ${ROOT}/licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt || \
    die "nvidia-legacy: licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt is not the LICENSE of this driver"
  mkdir -p ${INSTALL}/usr/share/licenses/nvidia-legacy
    cp -p LICENSE ${INSTALL}/usr/share/licenses/nvidia-legacy

  # The kernel modules, in the image overlay that kernel-overlays-setup puts over base for
  # these cards (nvidia/scripts/stack-select). No firmware: GSP is for Turing and later.
  mkdir -p ${INSTALL}/$(get_full_module_dir ${PKG_NAME})/nvidia
    cp -P kernel/nvidia.ko         ${INSTALL}/$(get_full_module_dir ${PKG_NAME})/nvidia
    cp -P kernel/nvidia-drm.ko     ${INSTALL}/$(get_full_module_dir ${PKG_NAME})/nvidia
    cp -P kernel/nvidia-uvm.ko     ${INSTALL}/$(get_full_module_dir ${PKG_NAME})/nvidia
    cp -P kernel/nvidia-modeset.ko ${INSTALL}/$(get_full_module_dir ${PKG_NAME})/nvidia

  # The PCI device IDs of the cards it is for, from the supported-gpus.json of both drivers.
  python3 ${PKG_DIR}/scripts/legacy-pci-ids.py \
    $(get_build_dir nvidia)/supported-gpus/supported-gpus.json \
    supported-gpus/supported-gpus.json > ${INSTALL}/${overlay}/pci-ids ||
    die "nvidia-legacy: no list of PCI IDs"

  # User space: the files of nvidia, of this release (their names carry the version, so they
  # are next to those of 615.78.08). libnvidia-egl-gbm (nvidia_egl-gbm, from source) and the
  # JSON files of EGL are those of nvidia (the same bytes in both installers).
  mkdir -p ${INSTALL}/usr/lib
  for name in libnvidia-allocator libEGL_nvidia libnvidia-eglcore libnvidia-glsi \
              libGLESv2_nvidia libcuda libnvcuvid libnvidia-gpucomp libnvidia-tls \
              libnvidia-ml; do
    cp -p ${name}.so.${PKG_VERSION} ${INSTALL}/usr/lib
  done
  mkdir -p ${INSTALL}/usr/lib/nvidia/${PKG_VERSION}
    cp -p nvidia-smi ${INSTALL}/usr/lib/nvidia/${PKG_VERSION}/nvidia-smi
  mkdir -p ${INSTALL}/usr/share/nvidia
    cp -P nvidia-application-profiles-${PKG_VERSION}-rc ${INSTALL}/usr/share/nvidia

  # Where the names that user space opens lead with this driver: those of nvidia
  # (/usr/lib/nvidia/stacks/open), with this version, and each file has to be there.
  mkdir -p ${INSTALL}/usr/lib/nvidia/stacks
  sed -e "s#\.${open_version}\$#.${PKG_VERSION}#" -e "s#/${open_version}/#/${PKG_VERSION}/#" \
    $(get_install_dir nvidia)/usr/lib/nvidia/stacks/open > ${INSTALL}/usr/lib/nvidia/stacks/legacy
  while read -r name file; do
    [ -f "${INSTALL}${file}" ] || die "nvidia-legacy: ${name} would lead to ${file}, which is not there"
  done < ${INSTALL}/usr/lib/nvidia/stacks/legacy
  grep -q "${open_version}" ${INSTALL}/usr/lib/nvidia/stacks/legacy &&
    die "nvidia-legacy: a link of the 580 driver would lead to a file of ${open_version}"
  true
}
