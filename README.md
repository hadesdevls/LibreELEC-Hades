# LibreELEC Hades

**English** | [Español](README.es.md)

LibreELEC Hades is an unofficial build of [LibreELEC](https://libreelec.tv) for ordinary 64-bit PCs (the "Generic x86_64" image). It is LibreELEC with one main addition: its media centre, which is Kodi® with changes, can send Dolby Vision video to a Dolby Vision TV over HDMI from the PC's graphics, whether a graphics card or the graphics built into the processor, instead of falling back to HDR10. Everything else LibreELEC does is kept. It grew out of CroqueMr's Dolby Vision fork of LibreELEC for Intel graphics; see "Credits".

> **This is not LibreELEC and it is not official.** LibreELEC Hades is not made, supported, endorsed or certified by Team LibreELEC, by Team Kodi or the XBMC Foundation, by Dolby Laboratories, or by NVIDIA, AMD or Intel, and it has no affiliation with any of them. Please do not ask those projects or companies for help with it. Kodi is a registered trademark of the XBMC Foundation. All other trademarks belong to their owners; see [NOTICE.md](NOTICE.md).

**Why the screen says "LibreELEC Hades" and not "Kodi".** The media centre of this build is a modified Kodi that the XBMC Foundation has not approved, and Kodi's trademark policy asks such a build not to use Kodi's name and logos as its own. So the splash screen, the logo, the icons and the texts that named the program say LibreELEC Hades, and a page of the system information, "About this build", says what it is and where to support Kodi and LibreELEC. Underneath it is Kodi: add-ons, skins and remote-control apps work as with Kodi, its log is still `kodi.log`, and what other programs read (the user agent, JSON-RPC, DLNA and AirPlay names) still says Kodi. In this document "Kodi" means that program.

**Version 1.0, release candidate 1 (October 2026).** Native Dolby Vision, profile 7 with its full enhancement layer included, has been watched on NVIDIA, AMD and Intel graphics, with Dolby Atmos passed through untouched. As a release candidate it is ready for more hardware: try it on yours and tell what you see in the issues. "What has been tested" says what has been seen so far.

## What it adds to LibreELEC

- **Dolby Vision over HDMI from a PC.** The picture is rebuilt in the PC, including the extra layer of "profile 7" discs (FEL), and sent to the TV with its Dolby Vision information for every frame. The TV then does its own Dolby Vision processing. This is the mode TVs call "Standard" or "TV-led".
- **Graphics from NVIDIA, AMD and Intel,** cards and the graphics built into processors alike (NVIDIA with NVIDIA's open kernel modules).
- **It never leaves you with a black screen on purpose.** Dolby Vision is used only when the video, the TV, the HDMI link and the graphics driver all qualify. If anything is in doubt, the video plays the ordinary Kodi way (HDR10 or SDR).
- **The frame rate is Kodi's.** Dolby Vision never changes the refresh rate Kodi would choose. Progressive video gets its exact rate (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). On NVIDIA, the modes at exactly 24, 30, 60 and 120 Hz are offered too: NVIDIA's driver offered many of a TV's modes only at 23.976, 29.97, 59.94 or 119.88 Hz, so a video at exactly 24 fps went out at 60 Hz or in a 4096-pixel-wide mode.
- **Room for slower graphics chips.** Where drawing a Dolby Vision frame takes most of a refresh period (integrated graphics with profile 7), the frame is prepared one refresh ahead, and the faster of two ways of drawing is measured and remembered for each graphics chip.
- **Dolby Vision shown as HDR10** on outputs that cannot take it natively: by default only for profile 5, whose base video is not a usable picture on its own, and for profile 7 if you turn it on (*System > Display*). The result is HDR10 without tone mapping.
- **PCs with more than one graphics chip, and laptops.** Kodi starts on the TV, whichever of them it is connected to, rather than on the first display it finds. If a TV is connected after Kodi has started, Kodi offers to move to it.
- **Stereoscopic 3D (new in this version, and never tried on a 3D display).** Both views of 3D Blu-ray video (H.264 MVC) are decoded when the display says it takes 3D, from a disc, a disc image or a disc folder and from a Matroska file made of one, and a 3D TV or projector is switched to 3D by itself over HDMI: frame packing where the display and the graphics driver have it, otherwise the half-resolution format that is drawn. See "Known limits".
- **Notices that explain what is happening:** when the TV input offers only "low-latency" Dolby Vision, when the TV input is in an HDMI 1.4 compatibility mode, when no TV is seen at all, and a Dolby Vision page in the player information.
- **Safer defaults** for a PC that also holds your other disks (see below), and a small installer for Windows.
- **A diagnostic switch.** Off by default. Turned on (a tick box in the installer), each start keeps a filtered, size-limited record of what the system saw, on the drive itself, to send with a report.

## What you need

- A 64-bit PC. **Secure Boot must be turned off**: Hades does not start with it yet.
- A USB stick or USB disk of 8 GB or more. A USB SSD is much smoother than a stick.
- For Dolby Vision:
  - **A TV that advertises Dolby Vision "Standard" (TV-led) on the HDMI input you use.** Many TVs advertise only "low-latency" Dolby Vision on an input set up for games or for a PC; Hades does not send native Dolby Vision to such an input and tells you so. On the one TV used for testing (an LG C5), "Quick Frame Transport" has to be off on that input.
  - **Graphics that Hades has a Dolby Vision output for,** a graphics card or the graphics built into the processor:
    - NVIDIA GeForce GTX 16 series, RTX 20 series or newer. Older NVIDIA cards (GTX 10 series and before) are not supported by the driver branch Hades uses.
    - AMD Radeon graphics of recent generations (older display hardware, "DCN 1" and "DCN 2", is not covered).
    - Intel graphics, with the Dolby Vision output of CroqueMr's fork, which this work started from.
  - **The PC connected by HDMI straight to the TV.** A sound bar or AV receiver between the PC and the TV has not been tried.
- A Dolby Vision video. Hades contains no films, no add-ons and no media sources.

## What has been tested

All tests were made by the author with the test builds that led to this version: on the author's own PC, an AMD Ryzen 9 7950X3D with an NVIDIA GeForce RTX 4080 SUPER, both on the NVIDIA card and on the processor's integrated Radeon graphics, with an LG C5 TV and the sound through a TCL Q85H Pro sound bar (on the TV's eARC), the system on a HIKSEMI HS-USB-SYNC C PRO 128 GB USB SSD; and on a laptop with an Intel Core i5-12450H. On NVIDIA, with the driver of this version, 615.78.08. "Works" means seen working on that hardware, with correct colours and a clean return to the menus; "measured" adds that the logs were checked for per-frame metadata and dropped frames. The TV's Dolby Vision logo alone is not counted as proof. "Untested" means exactly that.

| What | Status |
| --- | --- |
| Dolby Vision profile 7 with the full enhancement layer (FEL) | Works, measured: RTX 4080 SUPER, and the integrated Radeon graphics of the Ryzen 9 7950X3D (240 frames every 10 s, none dropped, the enhancement layer in every one; see "Known limits"). Works: the integrated graphics of the Core i5-12450H |
| Dolby Vision profile 8.1 | Works, measured: RTX 4080 SUPER. Other graphics: **untested** |
| Dolby Vision profile 5 | Works, measured: RTX 4080 SUPER. Other graphics: **untested** |
| Dolby Vision in two video tracks (Blu-ray `.m2ts` structure) | Works: RTX 4080 SUPER |
| Dolby Vision with the refresh rate not adjusted to the video (60 Hz) | Works, measured: RTX 4080 SUPER |
| Leaving Dolby Vision with the menus at 120 Hz | **Untested** with this version, which fixes a lost picture that the test builds before it had there on NVIDIA |
| Dolby Vision shown as HDR10 where native output is not possible | Ran on the RTX 4080 SUPER with a profile 5 video; checked by software |
| AV1 video with Dolby Vision | **Untested** (no sample to try) |
| HDR10 with more than 8 bits on NVIDIA | Works: RTX 4080 SUPER |
| A video at exactly 24 fps sent at exactly 24 Hz on NVIDIA (modes that this version adds to what the driver offers) | Works, measured: RTX 4080 SUPER |
| Dolby TrueHD with Atmos, passed through to the sound bar | Works: RTX 4080 SUPER, and the integrated graphics of the Ryzen 9 7950X3D and of the Core i5-12450H |
| Dolby Digital Plus with Atmos, passed through to the sound bar | Works: RTX 4080 SUPER |
| Sound keeps working when the display and the audio output change (sound follows the screen) | Works, measured: the author's PC, from the NVIDIA card to the processor's graphics and back |
| A PC or laptop with more than one graphics chip: Kodi starting on the TV, whichever chip it is connected to | Works on the author's PC: TV on the NVIDIA card, with the processor's graphics and its own HDMI port also present. Other combinations untested |
| Moving to a TV that is connected after Kodi started | **Untested** |
| Other graphics: NVIDIA GTX 16, RTX 20, 30 and 50; other AMD and Intel graphics | **Untested** |
| TVs other than the LG C5; older TVs with a "version 1" Dolby Vision block | **Untested** |
| Stereoscopic 3D: decoding both views, and switching a display to 3D | **Untested on any hardware.** The decoder gives the pictures of the reference decoder for the 3D streams it was checked with, except one superseded stream (see "Technical notes"); the rest has only been read and compiled |
| 3D from a disc, a disc image or a disc folder | **Untested: no 3D disc was at hand.** What reads the two clips of a disc has been read and compiled, and its rule for taking the second view up again after a gap was measured on streams made for the purpose |
| Started from USB: internal disks hidden | Works, measured |
| Installed on an internal disk: the PC's other disks hidden | In a virtual machine only |
| Boot entry that shows the internal disks read-only, and the setting that makes it the default | In a virtual machine only |
| Windows installer | "Update" has been run on a real USB disk. Every operation (erase, make room, partition, update, remove) passes the installer's own test on a virtual disk, and what it writes starts in a virtual machine. On a real internal disk, **none has been run** |

SDR, HDR10, HLG, audio pass-through, menus and refresh-rate switching are meant to behave as in LibreELEC. They use Kodi's normal code.

## Known limits

- Dolby Vision needs an exact, untouched HDMI signal. Variable refresh rate, scaling and colour adjustments in the graphics driver are not compatible with it.
- On AMD integrated graphics, profile 7 (FEL) is close to the limit of the chip. The first Dolby Vision video after installing a new build is used to measure which of two ways of drawing is faster there, and it may stutter briefly while that is measured; what is found is kept for later videos.
- No Secure Boot.
- **3D is new and unproven.**
  - Nothing of it has run on real graphics hardware or been seen on a 3D display. If a 3D video shows a black or broken picture, turn off *System > Display > Switch the display to 3D (HDMI)*; the two halves then go out as an ordinary picture and the display is switched to 3D by hand, as in LibreELEC.
  - Both views are decoded from a 3D Blu-ray disc, disc image or disc folder, and from a Matroska (`.mkv`) file made of one. No 3D disc was at hand: the disc has never been tried. Where the second view of a disc cannot be read or does not meet the first, the film goes on in 2D and the log says why; after such a gap the second view comes back at the next picture from which it can be decoded cleanly, which can take a second or two. An interlaced 3D video in a file, and a file that keeps what describes the second view outside the video itself (some MP4 files), play in 2D, as in LibreELEC.
  - Both views are decoded in software, by the processor, not by the video decoder of the graphics.
  - Frame packing, which keeps both eyes at full resolution, exists in HDMI for 1080p at 24 Hz: for a film, Kodi's adjustment of the display's refresh rate to the video has to be on. Expect one more change of display mode when a 3D video starts and when it stops.
  - The display has to be on when Kodi starts, or Kodi does not learn that it takes 3D.
  - Frame packing on NVIDIA depends on something that could not be found out without the hardware (whether its driver gives Kodi a buffer of the kind the output needs). If it does not, Kodi uses the half-resolution format instead.
  - Do not use Kodi's screen calibration while a 3D picture is shown in frame packing: its controls are for the ordinary mode, and what is changed there is not kept.
  - With NVIDIA graphics, a display that advertises 3D over HDMI gets no HDR10. That is a rule of NVIDIA's driver, with or without Hades.
  - With a display that does not say it takes 3D, a 3D video plays in 2D as before, unless you set *Player > Videos > Decode both views of 3D Blu-ray video (MVC)* to "Always" (for anaglyph glasses, for example).
- This is an independent implementation written from a public specification. It has not been tested or certified by Dolby, and it may differ from a certified player.

## Defaults that differ from LibreELEC

Nothing from LibreELEC is removed. What is unsafe by default on a PC with other disks is set to a safe value and can be turned back on.

- **Your PC's disks.** Started from USB (the "Run" and "Live" entries), Hades does not start the NVMe and SATA controllers, so the internal disks do not exist for it. A separate boot entry, "Run (internal disks read-only)", shows them, read-only. Installed on an internal disk by the Windows installer, Hades does not start the PC's other NVMe and SATA disks either, and does not mount the other partitions of its own disk. USB drives and SD cards are mounted as usual. A switch in the LibreELEC settings (*System > Internal disks > Show the internal disks (read-only)*) makes the entry that shows them the one the boot menu starts by itself. It is off by default; it explains what it means and asks before it is turned on. Shown, the disks can be read by everything that runs in Kodi, add-ons included; "read-only" is how they are mounted, not a lock; and that entry starts the kernel without its lock-down. Choosing "Run" once in the boot menu turns the switch off again, and so do a reset to defaults and an update made with the installer.
- **The installer entry** is there, but the image starts in "Run", not in the installer.
- **Kernel lock-down.** The entries that hide the disks also start the kernel in "integrity" lock-down: it loads only the modules signed for that build.
- **Samba (file sharing) is off.** When you turn it on, it asks for a password that Hades generates and shows in the settings. Sharing without a password can still be chosen.
- **Updates.** Hades does not contact LibreELEC's update server, because an official LibreELEC update would remove Dolby Vision. There is no automatic check; you update by hand. Update files that are not Hades are refused. No usage statistics are sent by default.
- **Network.** The firewall starts in "home" mode on a new installation. AirPlay is off. SSH is off, as in LibreELEC, and while the root password is still the default one it accepts only keys, not passwords.
- **Bluetooth** uses stricter pairing rules, with an option for older input devices.
- **Clock.** The PC's hardware clock is left in local time and never written, so Windows keeps the right time on a PC that starts both systems.
- **32-bit programs** are off by default (`ia32_emulation=1` on the kernel line turns them on).

## What is not included

- **No third-party add-ons and no media sources.** The image has Kodi's standard add-ons, the Kodi add-on repository and the LibreELEC add-on repository, like LibreELEC. The only folders set up in Kodi are LibreELEC's empty local ones (Videos, TV Shows, Music, Pictures). What you install is up to you.
- **No Dolby software.** There is no Dolby code, library or licence key in Hades.
- **No telemetry.** Hades adds none and, by default, sends no usage statistics and no identifier of your PC. Like LibreELEC and Kodi, it still contacts the services they use (add-on repositories, time servers and the like): the page "Privacy policy" of the system information starts with a note that lists them. The diagnostic record, when you turn it on, stays on your drive: nothing sends it anywhere.

## Installing and starting

You need an image file (`LibreELEC-Generic.x86_64-...img.gz`): from the releases of the repository this file is in, or built by yourself.

1. Write the image to a USB drive, with the Hades installer for Windows (`LibreELEC-Hades-Installer.exe`, whose own README explains each step) or with any program that writes a disk image to a USB drive. The drive is erased.
2. Restart the PC, open the boot menu of the firmware (F8, F11, F12 or Esc on most PCs) and choose the USB drive.
3. The first start prepares the storage and restarts once.

To go back to your usual system, shut Hades down and start the PC normally. Started from USB, Hades does not change the boot settings of the PC or anything in Windows.

## Updating

- With the Windows installer: choose the drive that already has Hades and "Update". Settings and add-ons are kept.
- Or as in LibreELEC: copy the `.tar` file of the new build to the `Update` folder and restart.

## Building from source

In outline; LibreELEC's own documentation applies to everything not said here.

1. Use a Linux machine (the author uses Ubuntu 26.04; LibreELEC keeps container recipes in `tools/docker/`), an unprivileged user, a path without spaces, and about 75 GB of free disk.
2. Get this repository: `main` is the current version, and the source of a build is the commit that its `BUILD_ID` names.
3. Build: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. The results appear in `target/`: an `.img.gz` (for a USB drive), a `.tar` (for updating) and an `.ova` (virtual machine).

Things to know:

- The build downloads the source of every package from the address in its recipe (`package.mk`) and checks it against the SHA-256 written there. No recipe has an empty hash, and prebuilt "reusable" toolchain packages are not used (`USE_REUSABLE="no"`).
- The NVIDIA driver package is downloaded from NVIDIA and unpacked without being run. Only its user-space libraries and firmware are used, unchanged; the kernel modules are compiled from NVIDIA's open source code. By building you accept NVIDIA's licence for those files.
- The author's own builds are made without network access, from sources fetched and verified beforehand. A clean build takes between twenty and forty-five minutes on a 32-thread machine; much longer on a small one.
- **If you change the tree and share your build, give it a name of your own** (`BUILDER_NAME=<your name> make image`) and say that it is yours. Without one the build calls itself "Hades 1.0 RC1".

The Windows installer is a separate small program (C#, in `tools/hades-installer/`); `build.ps1` builds it with the compiler that Windows already has.

## Technical notes

The [documentation](docs/README.md) has more: how the Dolby Vision output works, every setting of this build, and the diagnostics.

- **Signal.** Dolby Vision "Standard" is sent as an 8-bit full-range RGB HDMI signal that carries 12-bit YCbCr 4:2:2 video with the display-management metadata in the low bits of the chroma, plus a Dolby vendor-specific InfoFrame. The transport follows the public specification ETSI GS CCM 001 V1.1.1. The output must be bit-exact: no colour conversion, look-up table, dithering, scaling, variable refresh or DSC.
- **Common part:** FFmpeg reads the Dolby Vision metadata (RPU), libplacebo reshapes the picture and adds the enhancement layer, and a module in Kodi, `dvbridge`, packs and presents the frame.
- **One output per driver,** found by DRM properties: Intel `i915` (kernel patches from CroqueMr's fork), AMD `amdgpu` (a kernel patch of this project; it needs an HDMI TMDS link) and NVIDIA `nvidia-drm` with the open kernel modules (the vendor InfoFrame property of the driver, 8 bits per component and dithering off).
- **Which TVs qualify:** an EDID with a Dolby Vision block of version 2 and the "Standard" interface, or of version 1 (12 bytes) with display management 4 or later; on NVIDIA the TV must also allow the quantisation range to be selected.
- **3D, decoding.** FFmpeg decodes only the base view of H.264 MVC. Hades builds a second H.264 decoder, `h264_mvc`, from a copy of FFmpeg's decoder with Dom Cobley's multiview series applied (FFmpeg pull request 24303, not merged); FFmpeg's own H.264 decoder is left byte for byte as it is, and Kodi asks for the second one by name, only for a stream that has a second view. Checked on a host build against the H.264 reference decoder (JM): the 27 MVC conformance streams of ITU-T H.264.1 (base view identical in all; second view identical in 18 of the 19 the reference decoder gives one for, the exception being a superseded stream), and two stereo streams made for the purpose, identical in both views (1920x1080, and 960x540, which wraps the frame number of its second view, as a disc does all the time and no conformance stream does). The series is taken as it stood on 2026-10-14, carried to FFmpeg 9.0.2, with two fixes of this project.
- **3D, discs.** On a 3D Blu-ray the second view is in a clip of its own. What reads the two clips together is Dom Cobley's demuxer for Kodi, as he wrote it, with changes of this project around it: the second clip is read only where both views are wanted, the playlist says which clip and which angle, and after a picture that went on without its second view nothing of it is given to the decoder before a picture from which it can be decoded cleanly (an anchor picture).
- **3D, output.** A display learns the layout of a 3D picture from the HDMI vendor InfoFrame, which drivers send for a mode that has the layout in its flags. Intel's driver has such modes. For AMD the kernel carries the `amdgpu` patches of cinema-ONE's LibreELEC tree with fixes of this project; for NVIDIA, a patch of this project to the open kernel modules for frame packing, and the driver's vendor InfoFrame property for the half-resolution formats. A 3D mode is never one of Kodi's resolutions: it is how the resolution Kodi chose is sent while a 3D picture is drawn, at the same refresh rate.
- **Versions in 1.0 RC1:** Linux 7.2.10, Kodi 22 (the `Piers` branch after 22.0rc1), FFmpeg 9.0.2, Mesa 26.2.5, libplacebo from its main branch, NVIDIA 615.78.08.
- **Based on** LibreELEC `master` (13.0 development). LibreELEC has not released version 13.

## Credits

What this build adds to LibreELEC was written for it, except what is named here, which is the work of others and is used with thanks:

- **CroqueMr's Dolby Vision fork of LibreELEC, and its contributors** ([CroqueMr/libreelec-x86-DV](https://github.com/CroqueMr/libreelec-x86-DV)), where this work began. Taken from it as it published them: its Dolby Vision module for Kodi (`dvbridge`), from which the Dolby Vision output of this build grew; its Dolby Vision output for Intel graphics in the Linux kernel; and its changes to FFmpeg and libplacebo, among them a faster scaler for profile 7 from a later release. Three things of this build follow ideas of its later releases and were written anew here: the black bars written before the picture is rebuilt (faster profile 7), screenshots during Dolby Vision, and Dolby Vision shown as HDR10.
- **Dom Cobley (popcornmix)**: the decoding of the second view of 3D Blu-ray video (his series of changes to FFmpeg's H.264 decoder) and how Kodi reads the two clips of a 3D disc, as he wrote them.
- **cinema-ONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): the changes to AMD's kernel driver that switch a display to 3D over HDMI, on which the 3D output of this build for AMD is built.
- **quietvoid** ([dovi_tool](https://github.com/quietvoid/dovi_tool)): a reference consulted for the Dolby Vision metadata; no code of it is in this build.
- **Team LibreELEC and Team Kodi**, and the authors of every program in the image. Patches taken from other projects keep their authors in their headers, and the one commit of another author that this build takes as it is (OpenSSL 4, by Rudi Heitbaum, from LibreELEC) keeps its author in this repository's history.

[NOTICE.md](NOTICE.md) lists them with their licences.

## Licences and source code

- LibreELEC's build system is under the GNU General Public License, version 2; each file says whether "only" or "or later". Each patch has the licence of the program it changes. Kodi is GPL-2.0-or-later; the Dolby Vision module added to it is GPL-3.0-or-later, so the Kodi built here is distributed under GPL-3.0-or-later.
- The texts of the licences are in `licenses/`, and the licence of each package is in its recipe (`PKG_LICENSE`).
- The NVIDIA user-space libraries and firmware are proprietary and come under the NVIDIA Driver License Agreement, not under a free licence. Some firmware files for other hardware have their own licences.
- The complete source for a build is this repository at the commit named in `BUILD_ID` in `/etc/os-release` of the running system, plus the source archives its recipes name. See [NOTICE.md](NOTICE.md) for credits, trademarks and how to obtain the source.
- LibreELEC's own README, with its licence and copyright statement, is kept as [README.LibreELEC.md](README.LibreELEC.md), and its guide to contributing to LibreELEC as [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Reporting problems

Report problems with Hades in the issue tracker of the repository this file is in, **not** to LibreELEC, Kodi, Dolby, NVIDIA, AMD or Intel. Please include:

- the build (the `BUILD_ID` and `PRETTY_NAME` lines of `/etc/os-release`, or *Settings > System information*);
- the graphics (the card, or the processor if it is its integrated graphics), the TV model and how they are connected;
- what you played (container, Dolby Vision profile) and what you saw;
- the Kodi log, or the folder `hades-diag` of the drive if the diagnostic switch was on. Look through it first: it is filtered, but it is yours to check.

[docs/diagnostics.md](docs/diagnostics.md) says how to get the diagnostics and the log, and which lines of the log tell most.

If the same problem also happens with an official LibreELEC image, it belongs to LibreELEC or Kodi and can be reported there, against their image.

There is no warranty of any kind. You use this at your own risk.
