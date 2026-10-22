# LibreELEC Hades

**English** | [Español](README.es.md)

LibreELEC Hades is an unofficial build of [LibreELEC](https://libreelec.tv) for 64-bit PCs (the "Generic x86_64" image). It adds one main thing: native Dolby Vision over HDMI from the PC's graphics, a graphics card or the graphics built into the processor, where LibreELEC falls back to HDR10. Everything else LibreELEC does is kept. It grew out of CroqueMr's Dolby Vision fork of LibreELEC (see "Credits").

> **This is not LibreELEC and it is not official.** LibreELEC Hades is not made, supported, endorsed or certified by Team LibreELEC, Team Kodi or the XBMC Foundation, Dolby Laboratories, HDR10+ Technologies, NVIDIA, AMD or Intel, and has no affiliation with any of them. Please do not ask them for help with it. Kodi is a registered trademark of the XBMC Foundation. Dolby Vision and HDR10+ are named here only as video formats: LibreELEC Hades uses neither logo and is certified by neither. Other trademarks belong to their owners; see [NOTICE.md](NOTICE.md).

**Why the screen says "LibreELEC Hades".** The media centre of this build is Kodi with changes that the XBMC Foundation has not approved, and Kodi's trademark policy asks such a build not to use Kodi's name and logos as its own. Underneath, it is Kodi: add-ons, skins and remote-control apps work as with Kodi, its log is still `kodi.log`, and other programs still see Kodi. The page "About this build" of the system information says what it is and how to support Kodi and LibreELEC.

**Version 1.0 RC3 (October 2026).** Native Dolby Vision, including profile 7 with its full enhancement layer, has been seen working on NVIDIA, AMD and Intel graphics, with Dolby Atmos passed through. As a release candidate it needs more hardware: if you try it, please report what you see in the issues. The [changelog](CHANGELOG.md) lists what changed in each version.

## What it adds to LibreELEC

- **Native Dolby Vision over HDMI.** Each picture is rebuilt as the Dolby Vision specification defines it, including the full enhancement layer (FEL) of profile 7, and sent with its metadata for every frame. The TV does its own Dolby Vision processing, as with a certified player (its "Standard" or "TV-led" mode). Profiles 5, 7 (FEL and MEL) and 8.1.
- **NVIDIA, AMD and Intel graphics,** graphics cards and integrated graphics alike (NVIDIA with its open kernel modules).
- **A safe fallback.** Dolby Vision is used only when the video, the TV, the HDMI link and the graphics driver all qualify. Otherwise the video plays the ordinary way, as HDR10 or SDR, and the Dolby Vision page of the player information says why.
- **The refresh rate Kodi would choose,** exact for progressive video (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). On NVIDIA, the modes at exactly 24, 30, 60 and 120 Hz that the driver did not offer are added.
- **Made for integrated graphics too.** A frame that takes most of a refresh period is prepared one refresh ahead, and the picture under menus and subtitles is redrawn only where they are.
- **HDR10+** with the dynamic metadata of each frame, where the graphics driver can send it.
- **Dolby Vision shown as HDR10** where native output is not possible: by default for profile 5, whose base picture cannot be watched on its own, and for profile 7 if you turn it on. A display without HDR gets HDR mapped to SDR with the reference method of ITU-R BT.2390.
- **Options, off by default:** Dolby Vision Low-Latency for a TV input that offers only that, mapped to the TV by Hades; HDR10+ or HDR10 sent as Dolby Vision; Dolby Vision without its trims, for projectors.
- **3D Blu-ray video** (H.264 MVC): both views are decoded, and a 3D display is switched to 3D over HDMI.
- **PCs with two graphics chips, and laptops:** Kodi starts on the TV, whichever chip it is connected to, and offers to move to a TV connected later.
- **Notices that say what is happening** (a TV input that offers only low-latency Dolby Vision, an input in HDMI 1.4 mode, no TV found), and, in the player information, a Dolby Vision page and the use and temperature of the graphics.
- **Safer defaults, a Windows installer and a diagnostic switch** (see below).

## What you need

- A 64-bit PC with Secure Boot turned off (Hades does not start with it yet).
- A USB drive of 8 GB or more. A USB SSD runs much more smoothly than a stick.
- For Dolby Vision:
  - **A TV that offers Dolby Vision "Standard" on the HDMI input you use.** Many TVs offer only low-latency Dolby Vision on an input set up for games or a PC; Hades says so and sends that input HDR10, or low-latency Dolby Vision if you turn that on. On an LG C5, "Quick Frame Transport" has to be off on that input.
  - **Supported graphics:** NVIDIA GeForce GTX 16, RTX 20 or newer; AMD Radeon graphics with display hardware DCN 1.0 or newer, from Raven, Picasso and Navi 1x to the RX 9000 series (not the older DCE hardware, nor the few versions listed in [docs/how-it-works.md](docs/how-it-works.md#graphics-drivers)); Intel graphics of display version 9 or newer (UHD 6xx and later).
  - **The PC connected by HDMI straight to the TV** is best. A sound bar or AV receiver in between has to pass Dolby Vision through.
- Your own videos or add-ons: Hades comes with no films, add-ons or media sources.

## What has been tested

The author tests on an AMD Ryzen 9 7950X3D with an NVIDIA GeForce RTX 4080 SUPER, using both the card and the processor's integrated graphics, with an LG C5 TV, a TCL Q85H Pro sound bar on the TV's eARC and a HIKSEMI HS-USB-SYNC C PRO 128 GB USB SSD; and on a laptop with an Intel Core i5-12450H. Results marked "user" were reported by people on their own hardware, in the issues of this repository and on the LibreELEC and Kodi forums. Seen working means correct colours and a clean return to the menus; the TV's Dolby Vision logo alone does not count.

### Graphics

| Graphics | Tried on |
| --- | --- |
| NVIDIA GeForce RTX 40 series | RTX 4080 SUPER (author), RTX 4080 Laptop GPU (user) |
| NVIDIA GeForce RTX 30 series | RTX 3070 (user) |
| NVIDIA GeForce GTX 16, RTX 20 and RTX 50 series | Not tried yet |
| AMD Radeon RX 7000 series | RX 7600 (user) |
| AMD Radeon RX 5000, RX 6000 and RX 9000 series | Not tried yet |
| AMD Ryzen integrated graphics | Ryzen 9 7950X3D (author), Ryzen 7 5800H (user) |
| Intel integrated graphics | Core i5-12450H (author), Core i3-1005G1 and Core 5 255H (users) |
| Intel UHD 6xx graphics | Not tried yet |

TVs: LG C5 (author); LG G5, LG C2 and a Panasonic TV with native Dolby Vision, a Samsung S90H with HDR10+, and a Sony TV that offers Dolby Vision only as low-latency (users).

### Features

| What | Seen working on |
| --- | --- |
| Dolby Vision profile 7 with FEL | RTX 4080 SUPER, Ryzen 9 7950X3D and Core i5-12450H (author); RTX 4080 Laptop GPU, RX 7600, Ryzen 7 5800H and Core 5 255H (users) |
| Dolby Vision profile 7 with MEL | Ryzen 9 7950X3D (author); RX 7600 (user) |
| Dolby Vision profiles 8.1 and 5 | RTX 4080 SUPER and Ryzen 9 7950X3D (author); RX 7600 (user) |
| Dolby Vision in two video tracks | `.m2ts` on the RTX 4080 SUPER (author); MKV on an RTX 3070 (user) |
| Menus and subtitles over Dolby Vision at full frame rate | Profiles 7 FEL and 8.1 on the RTX 4080 SUPER and the Ryzen 9 7950X3D (author); profile 7 FEL on an RX 7600 (user) |
| Dolby Vision Low-Latency | Ryzen 9 7950X3D, through an HDMI sync box that offers only low-latency Dolby Vision (author) |
| Dolby Vision shown as HDR10 | Profile 5 on the RTX 4080 SUPER (author); profiles 5 and 7 FEL on a Core i3-1005G1 (user) |
| HDR10+ | Core i3-1005G1 with a Samsung S90H, and Ryzen 7 5800H (users) |
| HDR10+ as Dolby Vision | Ryzen 7 5800H (user) |
| HDR10 with more than 8 bits on NVIDIA | RTX 4080 SUPER (author) |
| A 24 fps video at exactly 24 Hz on NVIDIA | RTX 4080 SUPER (author) |
| Dolby TrueHD with Atmos, passed through | RTX 4080 SUPER, Ryzen 9 7950X3D and Core i5-12450H (author) |
| Dolby Digital Plus with Atmos and DTS-HD Master Audio, passed through | RTX 4080 SUPER (author) |
| 3D: both views decoded, the display switched to 3D | Ryzen 7 5800H, with a display that was not named (user) |
| Kodi on the TV in a PC with two graphics chips | The author's PC, with the TV on the NVIDIA card |
| Started from USB with the internal disks hidden | The author's PC |
| Windows installer | "Update" on a real USB drive (author); every other operation only on a virtual disk |

Not tried yet:

- Dolby Vision in AV1 video, and in two-track MP4 files.
- HDR10+ from NVIDIA graphics on a display.
- HDR10 sent as Dolby Vision, and Dolby Vision without its trims.
- HDR, HLG and Dolby Vision mapped to SDR on a display without HDR.
- Native Dolby Vision on a TV with an older Dolby Vision block (version 1) or with display management 2 or 3.
- Dolby Vision through a sound bar or AV receiver.
- 3D from a Blu-ray disc, a disc image or a disc folder.
- A TV connected after Kodi started, and moving Kodi to another display from *System > Display > Monitor*.
- Hades installed on an internal disk of a real PC (only in a virtual machine).
- The smaller additions of 1.0 RC3 listed in the changelog.

SDR, HDR10, HLG, audio passthrough, menus and refresh-rate switching use Kodi's own code and behave as in LibreELEC, apart from the additions above.

## Known limits

- **Dolby Vision needs an untouched HDMI signal** from an HDMI port of the graphics (on NVIDIA, also from a DisplayPort port that the graphics drive as HDMI, as the HDMI port of some laptops). Variable refresh rate, scaling and colour adjustments in the graphics driver are not compatible with it, and a converter between DisplayPort or USB-C and HDMI does not carry it.
- Native Dolby Vision goes out at 3840x2160, up to 60 Hz. On Intel UHD 6xx graphics, up to 30 Hz and only from an HDMI port of the graphics itself.
- On AMD integrated graphics, the first Dolby Vision video after installing a new build may stutter briefly while Hades measures which of two ways of drawing is faster there.
- On integrated graphics, profile 5 with the player information open dropped frames in testing. What 1.0 RC3 changes for it is not tried yet.
- **Dolby Vision Low-Latency is mapped by Hades, not by Dolby's display management**, which is not public. The TV is in its Dolby Vision mode and shows Hades' mapping to the brightness and colours its Dolby Vision block gives.
- **HDR10+ goes out only where the graphics driver can send it with each frame,** otherwise as HDR10; [docs/how-it-works.md](docs/how-it-works.md#hdr10) says where. The player information shows an HDR10+ video as HDR10: the display's own menu tells whether it gets HDR10+.
- **HDR10 sent as Dolby Vision** carries metadata measured from the picture as it plays, not the studio's: scene cuts are found from the picture, and a few are missed or found where there is none. **HDR10+ sent as Dolby Vision** keeps the brightness of each scene, not the studio's tone curve, target display or windows.
- **3D is new.** The author has not seen it on a 3D display, and no 3D disc has been tried. If a 3D video shows a black or broken picture, turn off *System > Display > Switch the display to 3D (HDMI)*. More in [docs/how-it-works.md](docs/how-it-works.md#stereoscopic-3d).
- No Secure Boot.
- This is an independent implementation written from public specifications. It has not been tested or certified by Dolby or by HDR10+ Technologies, and it may differ from a certified player.

## Defaults that differ from LibreELEC

Nothing from LibreELEC is removed. What is unsafe by default on a PC that also holds your other disks is set to a safe value and can be turned back on.

- **Your PC's disks.** Started from USB (the "Run" and "Live" entries), Hades does not start the NVMe and SATA controllers, so the internal disks do not exist for it. The "Run (internal disks read-only)" entry shows them, read-only. Installed on an internal disk, Hades does not see the PC's other NVMe and SATA disks either, nor mount the other partitions of its own disk. USB drives and SD cards are mounted as usual.
- **Showing the internal disks.** In the LibreELEC settings, *System > Internal disks* can make the read-only entry the one that starts by default. It is off, and it explains what it means before it is turned on: shown disks can be read by everything that runs in Kodi, add-ons included; "read-only" is how they are mounted, not a lock; and that entry starts the kernel without lock-down. Choosing "Run" once in the boot menu, a reset to defaults or an update with the installer turns it off again.
- **Kernel lock-down.** The entries that hide the disks start the kernel in "integrity" lock-down: it loads only the modules signed for that build.
- **The installer entry** of the boot menu is there, but Hades starts in "Run".
- **Samba (file sharing) is off.** Turned on, it asks for a password that Hades generates and shows in the settings; sharing without a password can still be chosen.
- **SSH is off,** as in LibreELEC, and while the root password is the default one it accepts only keys.
- **The firewall** starts in "home" mode on a new installation, and **AirPlay** is off.
- **Updates.** Hades does not contact LibreELEC's update server, because an official LibreELEC update would remove Dolby Vision. You update by hand; *Updates > Available Versions* says where versions are published. Update files that are not Hades are refused. No usage statistics are sent by default.
- **Bluetooth** uses stricter pairing rules, with an option for older input devices.
- **Clock.** The PC's hardware clock is kept in local time and never written, so Windows keeps the right time. On a PC without Windows, or whose Windows keeps the clock in UTC, *System > OS Configuration > Hardware clock* switches it to UTC from the next start.
- **Hardening.** Kernel and sysctl settings that make attacks harder (a file in `/storage/.config/sysctl.d` can change the sysctl ones back), BusyBox not setuid root, and the add-ons' data, network passphrases and other secrets readable by root only. 32-bit programs are off (`ia32_emulation=1` on the kernel line turns them on).
- **SSD trim.** Once a week, at the lowest priority, the unused blocks of the storage partition are given back to the drive. `systemctl mask fstrim.timer` turns it off.
- **RGB range on AMD.** AMD graphics send limited-range RGB to a TV that cannot select the range, as the TV expects. If you turned on *System > Display > Use limited colour range (16-235)* for such a TV, turn it off.
- **Tone mapping.** On a display without HDR, Kodi's tone mapping method defaults to "Reference (BT.2390)". A saved choice of Hable, Kodi's own default, is switched to it once.

## What is not included

- **No third-party add-ons and no media sources.** Like LibreELEC, the image has Kodi's standard add-ons, the Kodi and LibreELEC add-on repositories, and LibreELEC's empty local folders. What you install is up to you.
- **No Dolby software.** There is no Dolby code, library or licence key in Hades, and no code or specification of HDR10+ Technologies (the layout of the HDR10+ InfoFrame comes from GPL code; see "Credits").
- **No telemetry.** Hades adds none and, by default, sends no usage statistics and no identifier of your PC. Like LibreELEC and Kodi, it contacts the services they use (add-on repositories, time servers and the like), which the page "Privacy policy" of the system information lists. The diagnostics stay on your drive.

## Installing and starting

You need the image file (`LibreELEC-Generic.x86_64-...img.gz`), from the [releases](https://github.com/hadesdevls/LibreELEC-Hades/releases) of this repository or built yourself.

1. Write it to a USB drive with the Windows installer (`LibreELEC-Hades-Installer.exe`, which has its own README) or with any program that writes disk images. The drive is erased.
2. Restart the PC, open the firmware's boot menu (F8, F11, F12 or Esc on most PCs) and choose the USB drive.
3. The first start prepares the storage and restarts once.

To go back to your usual system, shut Hades down and start the PC normally. Started from USB, Hades changes nothing in the PC's boot settings or in Windows.

## Updating

- **With the Windows installer:** choose the drive that has Hades and "Update". Settings, add-ons and the diagnostics folder are kept.
- **As in LibreELEC:** copy the `.tar` file of the new version to the `Update` folder (`/storage/.update`) and restart. Over the network that folder is a Samba share, and Samba is off in Hades until you turn it on.

## Building from source

In outline; LibreELEC's own documentation covers the rest.

1. Use a Linux machine (the author uses Ubuntu 26.04; LibreELEC keeps container recipes in `tools/docker/`), an unprivileged user, a path without spaces and about 75 GB of free disk.
2. Get this repository: `main` is the current version, and the source of a build is the commit that its `BUILD_ID` names.
3. Build: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. The results are in `target/`: an `.img.gz` (for a USB drive), a `.tar` (for updating) and an `.ova` (virtual machine).

Good to know:

- Every source is downloaded from the address in its recipe (`package.mk`) and checked against the SHA-256 written there. No recipe has an empty hash, and prebuilt "reusable" toolchain packages are not used (`USE_REUSABLE="no"`).
- The NVIDIA driver package is downloaded from NVIDIA and unpacked without being run. Only its user-space libraries and firmware are used, unchanged; the kernel modules and the GBM library of its EGL (egl-gbm) are compiled from NVIDIA's open source code. By building, you accept NVIDIA's licence for those files.
- A clean build takes 20 to 45 minutes on a 32-thread machine, much longer on a small one. The author's builds run without network access, from sources fetched and verified beforehand.
- **If you change the tree and share your build, give it your own name** (`BUILDER_NAME=<your name> make image`) and say that it is yours. Without one, the build calls itself "Hades 1.0 RC3".
- The Windows installer is a separate small C# program in `tools/hades-installer/`; `build.ps1` builds it with the compiler that Windows already has.

## Documentation

- [How the Dolby Vision output works](docs/how-it-works.md): from the file to the HDMI cable, each graphics driver, HDR10+ and 3D.
- [Settings](docs/settings.md): every setting of this build.
- [Diagnostics and logs](docs/diagnostics.md): how to get them, and what to look for.

Versions in 1.0 RC3: Linux 7.2.12, Kodi 22 (the `Piers` branch after 22.0rc1, up to de8192b4200c), FFmpeg 9.0.2, Mesa 26.2.5, libplacebo from its main branch (285d1cc8af2a) and NVIDIA 615.78.08, on LibreELEC `master` (13.0 development, up to fb1edf29eb07). LibreELEC has not released version 13.

## Credits

What this build adds to LibreELEC was written for it, except what is named here, which is the work of others and is used with thanks:

- **CroqueMr's Dolby Vision fork of LibreELEC, and its contributors** ([CroqueMr/libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)), where this work began: its Dolby Vision module for Kodi (`dvbridge`), from which this build's Dolby Vision output grew, its Dolby Vision output for Intel graphics in the Linux kernel, and its changes to FFmpeg and libplacebo, among them a faster scaler for profile 7. A few features follow ideas from its later releases and were written anew here.
- **Dom Cobley (popcornmix)**: the decoding of 3D Blu-ray video (his changes to FFmpeg's H.264 decoder) and how Kodi reads 3D discs.
- **cinemaONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): the changes to AMD's kernel driver for 3D over HDMI, and Kodi pull request 29463 (opaque menus over HDR video).
- **Adrian Betschart**: the change to AMD's kernel driver that sends HDMI RGB in limited range on CTA modes.
- **quietvoid** ([dovi_tool](https://github.com/quietvoid/dovi_tool) and [hdr10plus_tool](https://github.com/quietvoid/hdr10plus_tool), MIT): references for the Dolby Vision and HDR10+ metadata.
- **CoreELEC** and the GPL kernel code of Amlogic that it carries, read as data: the layout of the HDR10+ InfoFrame, and which Dolby Vision displays are sent which InfoFrame.
- **YBLOD** ([dangerouslaser/libreelec-yblod](https://github.com/dangerouslaser/libreelec-yblod)): ideas for a few features, written anew here.
- **Team LibreELEC and Team Kodi**, and the authors of every program in the image. Patches taken from other projects keep their authors in their headers, and Rudi Heitbaum's OpenSSL 4 commit from LibreELEC keeps its author in this repository's history.

[NOTICE.md](NOTICE.md) lists them with their licences.

## Licences and source code

- LibreELEC's build system is under the GNU General Public License, version 2; each file says whether "only" or "or later". Each patch has the licence of the program it changes. Kodi is GPL-2.0-or-later; the Dolby Vision module added to it is GPL-3.0-or-later, so the Kodi built here is distributed under GPL-3.0-or-later.
- The texts of the licences are in `licenses/`, and the licence of each package is in its recipe (`PKG_LICENSE`).
- The NVIDIA user-space libraries and firmware are proprietary and come under the NVIDIA Driver License Agreement, not under a free licence. Some firmware files for other hardware have their own licences.
- The complete source of a build is this repository at the commit named in `BUILD_ID` in `/etc/os-release` of the running system, plus the source archives that its recipes name. See [NOTICE.md](NOTICE.md) for credits, trademarks and how to obtain the source.
- LibreELEC's own README, with its licence and copyright statement, is kept as [README.LibreELEC.md](README.LibreELEC.md), and its guide to contributing to LibreELEC as [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Reporting problems

Report problems with Hades in the [issues](https://github.com/hadesdevls/LibreELEC-Hades/issues) of this repository, not to LibreELEC, Kodi, Dolby, HDR10+ Technologies, NVIDIA, AMD or Intel. Please include:

- the build (*Settings > System information*, or the `PRETTY_NAME` and `BUILD_ID` lines of `/etc/os-release`);
- the graphics (the card, or the processor for integrated graphics), the TV model and how they are connected;
- what you played (container, Dolby Vision profile or HDR format) and what you saw;
- the Kodi log, or the `hades-diag` folder if the diagnostic switch was on. Look through it first: it is filtered, but it is yours to check.

[docs/diagnostics.md](docs/diagnostics.md) says how to get them, also from a system installed on an internal disk.

If the same problem happens with an official LibreELEC image, it belongs to LibreELEC or Kodi and can be reported there.

There is no warranty of any kind. You use this at your own risk.
