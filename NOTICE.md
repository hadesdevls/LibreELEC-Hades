# NOTICE for LibreELEC Hades

(En español: NOTICE.es.md)

## What this is

LibreELEC Hades is an unofficial build of LibreELEC for 64-bit PCs (Generic x86_64), with changes. Its main addition is native Dolby Vision output over HDMI from Kodi on a PC, with a graphics card or the graphics built into the processor. README.md, in the repository of the project, says what it does and where it has been tested.

**It is not LibreELEC and it is not official.** LibreELEC Hades is not made, supported, endorsed or certified by Team LibreELEC, by Team Kodi or the XBMC Foundation, by Dolby Laboratories, or by NVIDIA, AMD or Intel, and has no affiliation with any of them. Do not ask them for help with it. The media centre of this build is a modified version of Kodi; it is not supplied by, endorsed by or a product of the XBMC Foundation, and it does not show itself as Kodi: its screen says "LibreELEC Hades", and a page of its system information says what it is.

The Dolby Vision output is an independent implementation that follows a public specification (ETSI GS CCM 001). It contains no Dolby code, and Dolby has neither tested nor certified it.

HDR10+ is named in this build only as a format: the metadata that it reads from a video and sends to a display. LibreELEC Hades is not certified by HDR10+ Technologies.

## Work by others that it is built on

- **LibreELEC** (Team LibreELEC, and OpenELEC before it): the whole distribution and its build system. LibreELEC's original code is under the GNU General Public License, version 2. https://github.com/LibreELEC/LibreELEC.tv
- **Kodi** (Team Kodi and its contributors): the media centre. GPL-2.0-or-later. https://github.com/xbmc/xbmc
- **CroqueMr's Dolby Vision fork of LibreELEC**, and its contributors: where this work started. Its Dolby Vision module for Kodi (`dvbridge`, GPL-3.0-or-later), its Dolby Vision output for Intel graphics in the Linux kernel, and its changes to FFmpeg and libplacebo are used here, as the files it published or adapted. The Dolby vendor InfoFrame that its kernel patches added to `drivers/video/hdmi.c` and `include/linux/hdmi.h` was written again for this build, with the same interface (`linux-9899`), and is offered under the permission notice of those two files (Copyright (C) 2012 Avionic Design GmbH; the MIT licence). https://github.com/CroqueMr/libreelec-x86-DV
- **FFmpeg**, **libplacebo** and the **Linux kernel**, with patches.
- **Dom Cobley (popcornmix)**: the decoding of the second view of H.264 MVC video (3D Blu-ray), his series of changes to FFmpeg's H.264 decoder (FFmpeg pull request 24303, LGPL-2.1-or-later), applied here to a copy of that decoder; and, in Kodi (GPL-2.0-or-later), how the two clips of a 3D disc are read together and one change to how the 3D modes of a display are read, as he wrote them.
- **cinema-ONE's LibreELEC tree**: four changes to the AMD graphics driver of the Linux kernel for 3D over HDMI, used here with fixes. https://github.com/cinema-ONE/LibreELEC.tv
- **Adrian Betschart**: the change to AMD's kernel driver that sends HDMI RGB in limited range on CTA modes, which is part of Linux 7.3; two patches of this build build on it (`linux-9971`, `linux-9983`).
- **cinemaONE**: Kodi pull request 29463 (GPL-2.0-or-later), its two changes as written: the opaque parts of the GUI kept opaque when it is composited over HDR video, and the video seen through a video window then.
- **NVIDIA open GPU kernel modules** (NVIDIA Corporation, MIT/GPL-2.0), compiled from their published source with patches of this project, **egl-gbm** (NVIDIA Corporation, MIT), the GBM platform library of NVIDIA's EGL, compiled from its published source in place of the copy in NVIDIA's installer, and **nvidia-vaapi-driver** (MIT); for GeForce GTX 900 and 10 and TITAN V cards, the open parts of NVIDIA's 580 driver (NVKMS and nvidia-drm, MIT/GPL-2.0) are compiled the same way, with patches of this project.
- **Jost**, a typeface by indestructible type* (SIL Open Font License 1.1): the name of the build is set in it in the pictures of the build, which a script of this project draws (`packages/mediacenter/kodi/brand`).
- **avahi** (its authors, LGPL-2.1-or-later): 42 changes from its own repository, for 17 CVEs and 10 other faults, applied to avahi 0.8 as patches with their authors kept. `avahi-9043`, which limits the clients of its simple protocol server (CVE-2025-59529), was written for this build after the ideas of avahi's advisory, of its pull request 808 and of the review of that pull request.
- **libdvdcss** (VideoLAN and its contributors, GPL-2.0-or-later): Saifelden Mohamed Ismail's change `811ce97`, which reads CPXM discs that have no decryption set up, applied to libdvdcss 1.6.0 in Kodi's build.
- **libbluray** (VideoLAN and its contributors, LGPL-2.1-or-later): the Java part of its support for Blu-ray Java (BD-J) menus, `/usr/share/java/libbluray-j2se-<version>.jar` and `libbluray-awt-j2se-<version>.jar`, is built for this image from the same source and patches as the library (`packages/multimedia/libbluray-bdj`). It contains **ASM** 5.0.4, Copyright (c) 2000-2011 INRIA, France Telecom, under the BSD 3-Clause licence (its text in the image: `/usr/share/licenses/libbluray-bdj/ASM-LICENSE.txt`). No Java runtime comes with the image: the menus use the one that the user installs.
- **dovi_tool** and **hdr10plus_tool** by quietvoid (MIT): references for the syntax of the Dolby Vision and HDR10+ metadata, and the sources of rules and defaults that this build follows when it makes Dolby Vision metadata (dovi_tool's for content mapping v2.9, hdr10plus_tool's scenes).
- **CoreELEC**, and **Amlogic**'s code in it (GPL-2.0), read as data: the layout of the HDR10+ vendor InfoFrame and the coding of its fields, the byte layout of the Dolby Vision vendor InfoFrame (`linux-9899`, and the HDMI 1.4b one, `linux-9993`), the Low-Latency signal, the content mapping v2.9 part that displays of display management 2 and 3 are sent, and which Dolby Vision displays, those with version 1 blocks among them, get which signal; no code of it is copied. And a fix to Kodi's UPnP HTTP parser by User65k, from CoreELEC's Kodi (`kodi-9012`). https://github.com/CoreELEC
- **OpenSSL 4**: the change to LibreELEC's recipe is Rudi Heitbaum's (OpenSSL 4.0.3, LibreELEC pull request 11104), taken as his commit and updated here since.
- **Estuary**, Kodi's skin (phil65, Team Kodi; its code GPL-2.0, its artwork CC BY-SA 4.0): the player information of this build is made from its files for every skin (`system/hades`), with four of its images.
- **YBLOD** (dangerouslaser/libreelec-yblod): ideas for a few features, written anew here; no code of it is used.
- Fixes from the projects' own repositories (among them FFmpeg, glibc, OpenSSL, curl, avahi, expat, BlueZ, libbluray, libarchive, libtiff, libass, BusyBox, OpenSSH, libnfs and Kodi's `Piers` branch, and Debian's for tinyxml), applied as patches with their authors kept.
- Changes of this project to other packages of LibreELEC (default settings, scripts and recipes), each one in the tree with its reason.
- Everything else comes from LibreELEC unchanged, each package under its own licence.

Each patch of this build says in its header where it comes from, what it does and why, but for sixteen files of CroqueMr's fork, kept as the fork published them, without a header: `linux-9900-00` to `linux-9900-07` and `linux-9901` (Intel graphics), `ffmpeg-9900-00` to `ffmpeg-9900-02`, `libplacebo-9900-0`, `libplacebo-9910-2`, `libplacebo-9910-6-centered-lanczos` and `kodi-9990-native-dv` (with `tools/dvbridge/PROVENANCE.json`). Their changes are the fork's, under the licence of the files that they change.

## Licences

- The licence texts are in the folder `licenses`, next to this file.
- Because the Dolby Vision module added to Kodi is GPL-3.0-or-later, the Kodi program in this build is distributed under GPL-3.0-or-later.
- The licence of each package is named in its recipe (`PKG_LICENSE` in its `package.mk`).

## Parts that are not free software

As LibreELEC images do, this one contains firmware for many devices and Intel processor microcode, under their own terms. For the firmware and the microcode built into the kernel, the texts of those terms are in the folder `licenses` (`LicenseRef-LICENSE.*`, `LicenseRef-LICENCE.*` and `LicenseRef-intel-ucode.txt`). For all the firmware that comes from linux-firmware, the running system has its `WHENCE` (which licence each file comes under, with the copyright notices) and every licence text, in `/usr/share/licenses/kernel-firmware`. It also contains:

- **NVIDIA user-space libraries and firmware**, copyright NVIDIA Corporation, taken unmodified from NVIDIA's driver installer and for use with NVIDIA graphics cards only. They come under the **NVIDIA Driver License Agreement**: its text is `licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt`. No source code is available for them.
- **NVIDIA's 580 driver for GeForce GTX 900 and 10 and TITAN V** (Maxwell, Pascal and Volta: the 160 PCI IDs that NVIDIA's list sends to its 580 branch), which the image chooses at boot by the PCI ID of the card: its kernel module `nvidia.ko` contains NVIDIA's closed core (`nv-kernel.o_binary`), linked unmodified with the MIT-licensed interface code of NVIDIA's driver package, and its user-space libraries are taken unmodified from that package. They come under the NVIDIA Driver License Agreement (`licenses/LicenseRef-NVIDIA-Driver-License-Agreement.txt`, also in `/usr/share/licenses/nvidia-legacy` of the running system). No source code is available for the closed core. It makes the image about 120 MB larger.

## Source code

The complete source of this build is the LibreELEC Hades tree at the commit that `BUILD_ID` names in `/etc/os-release` of the running system, together with the source archives named in the recipes of that tree, each with the SHA-256 written there.

That tree is published in the repository of the project, which is where its images are offered. If you got this image from someone else, they have to give you that source as well, or tell you where it is offered: ask them.

The installer for Windows that is offered with the images is part of the project (GPL-2.0-or-later); its source is in that tree, in `tools/hades-installer`.

## Trademarks

Kodi is a registered trademark of the XBMC Foundation. Dolby, Dolby Vision and Dolby Atmos are trademarks of Dolby Laboratories. NVIDIA, GeForce and RTX are trademarks of NVIDIA Corporation. AMD, Radeon and Ryzen are trademarks of Advanced Micro Devices, Inc. Intel is a trademark of Intel Corporation. HDMI is a trademark of HDMI Licensing Administrator, Inc. HDR10+ is a trademark of HDR10+ Technologies, LLC. LibreELEC is the name of the project of Team LibreELEC. Other names belong to their owners.

These names are used only to say what this software is based on and what it works with. Their use does not mean that any of their owners approves, sponsors or certifies LibreELEC Hades.

## No warranty

This software is provided "as is", without warranty of any kind, to the extent permitted by law. See sections 11 and 12 of the GNU General Public License, version 2, and sections 15 and 16 of version 3.
