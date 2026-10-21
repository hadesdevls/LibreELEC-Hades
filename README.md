# LibreELEC Hades

**English** | [Español](README.es.md)

LibreELEC Hades is an unofficial build of [LibreELEC](https://libreelec.tv) for ordinary 64-bit PCs (the "Generic x86_64" image). It is LibreELEC with one main addition: its media centre, which is Kodi® with changes, can send Dolby Vision video to a Dolby Vision TV over HDMI from the PC's graphics, whether a graphics card or the graphics built into the processor, instead of falling back to HDR10. Everything else LibreELEC does is kept. It grew out of CroqueMr's Dolby Vision fork of LibreELEC for Intel graphics; see "Credits".

> **This is not LibreELEC and it is not official.** LibreELEC Hades is not made, supported, endorsed or certified by Team LibreELEC, by Team Kodi or the XBMC Foundation, by Dolby Laboratories, by HDR10+ Technologies, or by NVIDIA, AMD or Intel, and it has no affiliation with any of them. Please do not ask those projects or companies for help with it. Kodi is a registered trademark of the XBMC Foundation. Dolby Vision and HDR10+ are named here only as video formats: LibreELEC Hades uses no logo of either, and it is not certified by Dolby Laboratories or by HDR10+ Technologies. All other trademarks belong to their owners; see [NOTICE.md](NOTICE.md).

**Why the screen says "LibreELEC Hades" and not "Kodi".** The media centre of this build is a modified Kodi that the XBMC Foundation has not approved, and Kodi's trademark policy asks such a build not to use Kodi's name and logos as its own. So the splash screen, the logo, the icons and the texts that named the program say LibreELEC Hades, and a page of the system information, "About this build", says what it is and where to support Kodi and LibreELEC. Underneath it is Kodi: add-ons, skins and remote-control apps work as with Kodi, its log is still `kodi.log`, and what other programs read (the user agent, JSON-RPC, DLNA and AirPlay names) still says Kodi. In this document "Kodi" means that program.

**Version 1.0, release candidate 3 (October 2026).** Native Dolby Vision, profile 7 with its full enhancement layer included, has been watched on NVIDIA, AMD and Intel graphics, with Dolby Atmos passed through untouched. As a release candidate it is ready for more hardware: try it on yours and tell what you see in the issues. "What has been tested" says what has been seen so far, and with which version. What changed in each version is in the [changelog](CHANGELOG.md).

## What it adds to LibreELEC

- **Dolby Vision over HDMI from a PC.** The picture is rebuilt in the PC, including the extra layer of "profile 7" discs (FEL), and sent to the TV with its Dolby Vision information for every frame. The TV then does its own Dolby Vision processing. This is the mode TVs call "Standard" or "TV-led".
- **Dolby Vision Low-Latency, as an option.** A TV input that offers Dolby Vision only in its low-latency ("player-led") form can be sent that form instead of HDR10: the picture is then mapped to the display by Hades, with its own mapping, not Dolby's. Off by default.
- **Graphics from NVIDIA, AMD and Intel,** cards and the graphics built into processors alike (NVIDIA with NVIDIA's open kernel modules).
- **It never leaves you with a black screen on purpose.** Dolby Vision is used only when the video, the TV, the HDMI link and the graphics driver all qualify. If anything is in doubt, the video plays the ordinary Kodi way (HDR10 or SDR).
- **The frame rate is Kodi's.** Dolby Vision never changes the refresh rate Kodi would choose. Progressive video gets its exact rate (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). On NVIDIA, the modes at exactly 24, 30, 60 and 120 Hz are offered too: NVIDIA's driver offered many of a TV's modes only at 23.976, 29.97, 59.94 or 119.88 Hz, so a video at exactly 24 fps went out at 60 Hz or in a 4096-pixel-wide mode.
- **Room for slower graphics chips.** Where drawing a Dolby Vision frame takes most of a refresh period (integrated graphics with profile 7), the frame is prepared one refresh ahead, and the faster of two ways of drawing is measured and remembered for each graphics chip. A frame with menus or subtitles over it is made again only where they are drawn.
- **HDR10+** to a display that takes it: the dynamic metadata of each frame go out with that frame, on NVIDIA, AMD and Intel graphics, within the limits of each driver (see "Known limits").
- **Other formats as Dolby Vision, as options (off by default):** an HDR10+ video sent as Dolby Vision to a TV that takes Dolby Vision but not HDR10+, with metadata made from its own HDR10+ metadata; an HDR10 video sent as Dolby Vision with metadata that Hades measures from the picture, which say so; and Dolby Vision without its trims, for projectors that show washed-out blacks with them.
- **Dolby Vision shown as HDR10** on outputs that cannot take it natively: by default only for profile 5, whose base video is not a usable picture on its own, and for profile 7 if you turn it on (*System > Display*). The result is HDR10 without tone mapping, or mapped to a projector's brightness if you choose it.
- **A display without HDR** gets HDR10, HLG and Dolby Vision (profiles 5, 7 and 8.1) mapped to SDR with the mapping of a reference display (ITU-R BT.2390), the default tone mapping method. Kodi's own methods are still there.
- **PCs with more than one graphics chip, and laptops.** Kodi starts on the TV, whichever of them it is connected to, rather than on the first display it finds. If a TV is connected after Kodi has started, Kodi offers to move to it.
- **Stereoscopic 3D (never tried on a 3D display).** Both views of 3D Blu-ray video (H.264 MVC) are decoded when the display says it takes 3D, from a disc, a disc image or a disc folder and from a Matroska file made of one, and a 3D TV or projector is switched to 3D by itself over HDMI: frame packing where the display and the graphics driver have it, otherwise the half-resolution format that is drawn. See "Known limits".
- **Notices that explain what is happening:** when the TV input offers only "low-latency" Dolby Vision, when the TV input is in an HDMI 1.4 compatibility mode, when no TV is seen at all, and a Dolby Vision page in the player information, which shows in any skin.
- **Safer defaults** for a PC that also holds your other disks (see below), and a small installer for Windows.
- **A diagnostic switch.** Off by default. Turned on (a tick box in the installer), each start keeps a filtered, size-limited record of what the system saw, on the drive itself, to send with a report. It can be copied to a USB drive, also with nothing on the screen.

## What you need

- A 64-bit PC. **Secure Boot must be turned off**: Hades does not start with it yet.
- A USB stick or USB disk of 8 GB or more. A USB SSD is much smoother than a stick.
- For Dolby Vision:
  - **A TV that advertises Dolby Vision "Standard" (TV-led) on the HDMI input you use.** Many TVs advertise only "low-latency" Dolby Vision on an input set up for games or for a PC; Hades tells you so, and sends such an input HDR10, or low-latency Dolby Vision if you turn that on. On the one TV used for testing (an LG C5), "Quick Frame Transport" has to be off on that input for "Standard".
  - **Graphics that Hades has a Dolby Vision output for,** a graphics card or the graphics built into the processor:
    - NVIDIA GeForce GTX 16 series, RTX 20 series or newer. Older NVIDIA cards (GTX 10 series and before) are not supported by the driver branch Hades uses.
    - AMD Radeon graphics whose display hardware is DCN 1.0 or newer, from Raven, Picasso and Navi 1x to the Radeon RX 9000 series. Not the older display hardware (DCE), nor the few versions that [docs/how-it-works.md](docs/how-it-works.md#graphics-drivers) names.
    - Intel graphics of display version 9 and newer (the UHD 6xx graphics and later), with the Dolby Vision output of CroqueMr's fork, which this work started from.
  - **Best, the PC connected by HDMI straight to the TV,** which is how it has been tested. A sound bar or AV receiver in between has to pass Dolby Vision through.
- A Dolby Vision video. Hades contains no films, no add-ons and no media sources.

## What has been tested

All tests were made by the author: on the author's own PC, an AMD Ryzen 9 7950X3D with an NVIDIA GeForce RTX 4080 SUPER, both on the NVIDIA card and on the processor's integrated Radeon graphics, with an LG C5 TV and the sound through a TCL Q85H Pro sound bar (on the TV's eARC), the system on a HIKSEMI HS-USB-SYNC C PRO 128 GB USB SSD; and on a laptop with an Intel Core i5-12450H. On NVIDIA, with the driver 615.78.08. "Works" means seen working on that hardware, with correct colours and a clean return to the menus; "measured" adds that the logs were checked for per-frame metadata and dropped frames. The TV's Dolby Vision logo alone is not counted as proof. "Untested" means exactly that.

**1.0 RC3 was tried with its test builds of 2026-10-21** on the RTX 4080 SUPER and on the integrated Radeon graphics, with the LG C5: Dolby Vision profiles 7 with FEL, 8.1 and 5, HDR10, seeking, leaving to menus at 120 Hz, TrueHD and streaming Atmos, the author's add-ons and the player information worked on both. The first table is what was seen with 1.0 RC2 and the test builds before it. What 1.0 RC3 adds is in the second table: what that test showed, and **"Untested on hardware"** for what came after it or was not tried; only what is seen on hardware is marked as working.

| What | Status |
| --- | --- |
| Dolby Vision profile 7 with the full enhancement layer (FEL) | Works, measured: RTX 4080 SUPER, and the integrated Radeon graphics of the Ryzen 9 7950X3D (240 frames every 10 s, none dropped, the enhancement layer in every one; see "Known limits"). Works: the integrated graphics of the Core i5-12450H |
| Dolby Vision profile 7 with the minimal enhancement layer (MEL) | Works: the integrated Radeon graphics of the Ryzen 9 7950X3D (1.0 RC2) |
| Dolby Vision profile 8.1 | Works: RTX 4080 SUPER and the integrated Radeon graphics of the Ryzen 9 7950X3D (1.0 RC2); measured on the RTX 4080 SUPER with earlier test builds |
| Dolby Vision profile 5 | Works: the author's PC with 1.0 RC2 (sent as certified players send it); measured on the RTX 4080 SUPER with earlier test builds |
| Dolby Vision in two video tracks (Blu-ray `.m2ts` structure) | Works: RTX 4080 SUPER |
| Dolby Vision with the refresh rate not adjusted to the video (60 Hz) | Works, measured: RTX 4080 SUPER |
| Dolby Vision with the menus at 119.88 Hz or 120 Hz, starting and leaving | Works: RTX 4080 SUPER (119.88 and 120 Hz) and the integrated Radeon graphics (120 Hz), with the refresh rate adjusted to the video (1.0 RC2). With it not adjusted, at 119.88 Hz on the RTX 4080 SUPER, the video goes out as HDR10, as it should: native Dolby Vision goes out at up to 60 Hz |
| Dolby Vision shown as HDR10 where native output is not possible | Ran on the RTX 4080 SUPER with a profile 5 video; checked by software |
| AV1 video with Dolby Vision | **Untested** (no sample to try) |
| HDR10 with more than 8 bits on NVIDIA | Works: RTX 4080 SUPER |
| A video at exactly 24 fps sent at exactly 24 Hz on NVIDIA (modes that Hades adds to what the driver offers) | Works, measured: RTX 4080 SUPER |
| Dolby TrueHD with Atmos, passed through to the sound bar | Works: RTX 4080 SUPER, and the integrated graphics of the Ryzen 9 7950X3D and of the Core i5-12450H |
| Dolby Digital Plus with Atmos, passed through to the sound bar | Works: RTX 4080 SUPER |
| DTS-HD Master Audio, passed through | Works: RTX 4080 SUPER, to a TCL Q85H Pro sound bar (1.0 RC2) |
| Sound keeps working when the display and the audio output change (sound follows the screen) | Works, measured: the author's PC, from the NVIDIA card to the processor's graphics and back |
| A PC or laptop with more than one graphics chip: Kodi starting on the TV, whichever chip it is connected to | Works on the author's PC: TV on the NVIDIA card, with the processor's graphics and its own HDMI port also present. Other combinations untested |
| Moving to a TV that is connected after Kodi started | **Untested** |
| Other graphics: NVIDIA GTX 16, RTX 20, 30 and 50; other AMD and Intel graphics | **Untested**. Of what 1.0 RC3 adds for them: HDR at 12 bits over HDMI on GTX 16 and RTX 20 (off by default, see [docs/settings.md](docs/settings.md#outside-the-settings-screens)), the Radeon RX 9000 series, HDR10 in YCbCr 4:2:2 on AMD (not on the integrated graphics of the Ryzen 7000 series, which keeps 4:2:0), Intel UHD 6xx graphics, and the HDMI port of a laptop that NVIDIA graphics drive as a dual-mode DisplayPort connector, with its picture after a wake from suspend |
| TVs other than the LG C5; older TVs with a "version 1" Dolby Vision block, and TVs that read Dolby Vision display management 2 or 3 | **Untested** |
| Stereoscopic 3D: decoding both views, and switching a display to 3D (also at twice the rate of a 30p or 25p video) | **Untested on any hardware.** The decoder gives the pictures of the reference decoder for the 3D streams it was checked with, except one superseded stream (see "Technical notes"); the rest has only been read and compiled |
| 3D from a disc, a disc image or a disc folder | **Untested: no 3D disc was at hand.** What reads the two clips of a disc has been read and compiled, and its rule for taking the second view up again after a gap was measured on streams made for the purpose |
| Started from USB: internal disks hidden | Works, measured |
| Installed on an internal disk: the PC's other disks hidden | In a virtual machine only |
| Boot entry that shows the internal disks read-only, and the setting that makes it the default | In a virtual machine only |
| Windows installer | "Update" has been run on a real USB disk. Every operation (erase, make room, partition, update, remove) passes the installer's own test on a virtual disk, and what it writes starts in a virtual machine. On a real internal disk, **none has been run** |

SDR, HDR10, HLG, audio pass-through, menus and refresh-rate switching are meant to behave as in LibreELEC. They use Kodi's normal code, except where this README says otherwise of them: HDR10+, HDR10 at more bits on some graphics, menus over HDR video, and HDR on a display without HDR.

**New in 1.0 RC3**

| What | Status |
| --- | --- |
| Menus, the OSD, the seek bar and subtitles over native Dolby Vision, with less GPU time for each frame | Works, measured: profiles 7 FEL and 8.1 with menus over them on the integrated Radeon graphics (238 to 241 frames of 240 every 10 s) and on the RTX 4080 SUPER. Profile 5 with the player information open on the integrated graphics still dropped frames (182 to 211 of 240); what this version adds for it, a simplified rounding under the menus only where such frames are measured not to fit, is **untested on hardware** |
| Dolby Vision Low-Latency on a TV input that offers only that (an option, off by default) | Works: natively, on the integrated Radeon graphics through an HDMI sync box that offers Dolby Vision only as Low-Latency, mapped by Hades to the peak that the box gives. The shorter notice of this version is **untested on hardware** |
| HDR10+ to a display that takes it | **Untested on a display that shows it:** over DisplayPort from the RTX 4080 SUPER the metadata went out with each frame of five videos, and the monitor tried showed HDR10. What this version changes for it (the DisplayPort packet without the hardware checksum, HDMI on RTX 40 graphics next to HDMI variable refresh, Intel at 3840x2160 at 24, 25 and 30 Hz) is **untested on hardware** |
| HDR10+ sent as Dolby Vision, HDR10 sent as Dolby Vision with measured metadata, and Dolby Vision without its trims (options, off by default) | **To be tested** |
| HDR10, HLG and Dolby Vision on a display without HDR, with the reference tone mapping (BT.2390) | **To be tested** |
| Dolby Vision in two video tracks of MKV and MP4 files, and in streams without a Dolby Vision record | **To be tested** |
| A list of native Dolby Vision videos at different frame rates: one display change from one to the next | **To be tested** |
| PCM to a TV that declares 2 channels of PCM; an HDMI audio output that is set but not there | Works: a DTS track decoded to PCM in no more channels than the TV declared (2, or 6 where it declared 8), on both graphics. **Untested on hardware:** the limit kept through a change of display mode, and multichannel sound sent as Dolby to a TV that declares stereo PCM only (a setting, on) |
| Dolby Vision with a sound bar or AV receiver between the PC and the TV | **To be tested** |
| The diagnostics copied to a USB drive, from the settings and with nothing on the screen; an update with the installer that keeps them | Works: an update with the installer kept the diagnostics of the test drive (513 files, read back the same). The copy to a USB drive: **untested on hardware** |
| The hardware clock kept in UTC; the phone remote control in one step; the weekly trim of the storage partition | **To be tested** |
| The Windows installer of this version, which keeps the diagnostics on an update, on its own test on a virtual disk | Updates of the test drive work. Its test on a virtual disk: **to be run** |
| The mouse pointer (fractions of a pixel kept, motions that pile up handled as one) | Works: no stutters of the pointer with the last test build |
| *System > Display > Monitor* shown again, and moving Kodi to another display while it runs | **Untested on hardware** |
| Linux 7.2.12 (the tests above were made with 7.2.11) | **Untested on hardware** |

## Known limits

- Dolby Vision needs an exact, untouched HDMI signal. Variable refresh rate, scaling and colour adjustments in the graphics driver are not compatible with it.
- On AMD integrated graphics, profile 7 (FEL) is close to the limit of the chip. The first Dolby Vision video after installing a new build is used to measure which of two ways of drawing is faster there, and it may stutter briefly while that is measured; what is found is kept for later videos.
- **Dolby Vision Low-Latency is mapped by LibreELEC Hades, not by Dolby's display management**, which is not public: the TV is in its Dolby Vision mode, and the picture it gets is Hades' mapping to the brightness and colours its Dolby Vision block gives. It is used only where the TV input offers no "Standard" mode, and only where the graphics driver can send it, which the kernel and the NVIDIA modules of this build can.
- **HDR10+ goes out only where the driver can send it with each frame;** elsewhere the video goes out as HDR10. The drivers write the HDR10+ data of a frame when its commit is made, not in step with its flip, so a change may reach the display a frame early or late: not verified. Where it can go out:
  - NVIDIA: over HDMI on GTX 16, RTX 20, 30 and 40 graphics and newer, and over DisplayPort from the RTX 40 series on; not over DisplayPort on the RTX 20 and 30 series; on GTX 16, RTX 20 and RTX 30 graphics not to a display that declares HDMI variable refresh, to which the driver sends its own variable refresh data in the slot HDR10+ needs; on RTX 40 graphics it then goes in another slot, which NVIDIA does not document for HDMI (not verified on a display); on RTX 30 graphics not where the driver keeps that slot for a workaround of its own; and over DisplayPort, the official HDR10+ Gaming FAQ says that on a PC it goes over HDMI only (not verified on a monitor);
  - AMD: over HDMI and DisplayPort on display hardware DCN 1.0 and newer, not on a laptop's own panel;
  - Intel: over HDMI from display version 10; at 3840x2160 at 24, 25 or 30 Hz only to an HDMI 2.0 display that lists the CTA VIC of that mode (not verified on a display); not over DisplayPort.
- **HDR10 sent as Dolby Vision** carries metadata that Hades measures as the video plays, not metadata of the studio: scene cuts are found from the picture, and a few are missed or found where there is none (more in fast animation). **HDR10+ sent as Dolby Vision** keeps the brightness of each scene, not the studio's tone curve, its target display or its windows.
- **Intel UHD 6xx graphics** (display version 9) send Dolby Vision at 3840x2160 up to 30 Hz only, from an HDMI port of the graphics itself.
- No Secure Boot.
- **3D is new and unproven.**
  - Nothing of it has run on real graphics hardware or been seen on a 3D display. If a 3D video shows a black or broken picture, turn off *System > Display > Switch the display to 3D (HDMI)*; the two halves then go out as an ordinary picture and the display is switched to 3D by hand, as in LibreELEC.
  - Both views are decoded from a 3D Blu-ray disc, disc image or disc folder, and from a Matroska (`.mkv`) file made of one. No 3D disc was at hand: the disc has never been tried. Where the second view of a disc cannot be read or does not meet the first, the film goes on in 2D and the log says why; after such a gap the second view comes back at the next picture from which it can be decoded cleanly, which can take a second or two. An interlaced 3D video in a file, and a file that keeps what describes the second view outside the video itself (some MP4 files), play in 2D, as in LibreELEC.
  - Both views are decoded in software, by the processor, not by the video decoder of the graphics.
  - Frame packing, which keeps both eyes at full resolution, exists in HDMI for 1080p at 24 Hz: for a film, Kodi's adjustment of the display's refresh rate to the video has to be on. Expect one more change of display mode when a 3D video starts and when it stops. A 30p or 25p video whose display has no 3D mode at that rate gets the 3D mode at twice the rate, where the display has it, and each picture is shown twice.
  - The display has to be on when Kodi starts, or Kodi does not learn that it takes 3D.
  - Frame packing on NVIDIA depends on something that could not be found out without the hardware (whether its driver gives Kodi a buffer of the kind the output needs). If it does not, Kodi uses the half-resolution format instead.
  - Do not use Kodi's screen calibration while a 3D picture is shown in frame packing: its controls are for the ordinary mode, and what is changed there is not kept.
  - With NVIDIA graphics, a display that advertises 3D over HDMI gets no HDR10. That is a rule of NVIDIA's driver, with or without Hades.
  - With a display that does not say it takes 3D, a 3D video plays in 2D as before, unless you set *Player > Videos > Decode both views of 3D Blu-ray video (MVC)* to "Always" (for anaglyph glasses, for example).
- This is an independent implementation written from public specifications. It has not been tested or certified by Dolby or by HDR10+ Technologies, and it may differ from a certified player.

## Defaults that differ from LibreELEC

Nothing from LibreELEC is removed. What is unsafe by default on a PC with other disks is set to a safe value and can be turned back on.

- **Your PC's disks.** Started from USB (the "Run" and "Live" entries), Hades does not start the NVMe and SATA controllers, so the internal disks do not exist for it. A separate boot entry, "Run (internal disks read-only)", shows them, read-only. Installed on an internal disk by the Windows installer, Hades does not start the PC's other NVMe and SATA disks either, and does not mount the other partitions of its own disk. USB drives and SD cards are mounted as usual. A switch in the LibreELEC settings (*System > Internal disks > Show the internal disks (read-only)*) makes the entry that shows them the one the boot menu starts by itself. It is off by default; it explains what it means and asks before it is turned on. Shown, the disks can be read by everything that runs in Kodi, add-ons included; "read-only" is how they are mounted, not a lock; and that entry starts the kernel without its lock-down. Choosing "Run" once in the boot menu turns the switch off again, and so do a reset to defaults and an update made with the installer.
- **The installer entry** is there, but the image starts in "Run", not in the installer.
- **Kernel lock-down.** The entries that hide the disks also start the kernel in "integrity" lock-down: it loads only the modules signed for that build.
- **Samba (file sharing) is off.** When you turn it on, it asks for a password that Hades generates and shows in the settings. Sharing without a password can still be chosen.
- **Updates.** Hades does not contact LibreELEC's update server, because an official LibreELEC update would remove Dolby Vision. There is no automatic check; you update by hand, and *Updates > Available Versions* says where the versions are published. A signed update channel of Hades is built in but off: no image has a channel yet, so nothing is looked for or downloaded. Update files that are not Hades are refused. No usage statistics are sent by default.
- **Network.** The firewall starts in "home" mode on a new installation. AirPlay is off. SSH is off, as in LibreELEC, and while the root password is still the default one it accepts only keys, not passwords.
- **Bluetooth** uses stricter pairing rules, with an option for older input devices.
- **Clock.** The PC's hardware clock is left in local time and never written, so Windows keeps the right time on a PC that starts both systems. On a PC without Windows, or whose Windows keeps the clock in UTC, *System > OS Configuration > Hardware clock* sets it to UTC, from the next start.
- **32-bit programs** are off by default (`ia32_emulation=1` on the kernel line turns them on).
- **Hardening.** Kernel options that make memory bugs harder to exploit, sysctl settings that take no ICMP redirects and keep the kernel log for root (a file in `/storage/.config/sysctl.d` can change them back), BusyBox not setuid root, and the folders that hold the add-ons' data, the network passphrases and other secrets readable by root only. Compiler hardening only on the libraries where it was measured to cost nothing in playback.
- **SSD trim.** Once a week the unused blocks of the storage partition are given back to the drive, with the lowest priority; only that partition. `systemctl mask fstrim.timer` turns it off.
- **RGB range on AMD.** AMD graphics send limited-range RGB to a TV that cannot select the range, as the TV expects (AMD's fix, which no Linux release has yet, and this build's for the 4K film modes). If you turned on Kodi's *System > Display > Use limited colour range (16-235)* for such a TV, turn it off.
- **Tone mapping.** On a display without HDR, Kodi's tone mapping defaults to "Reference (BT.2390)". A choice of Hable, Kodi's own default, saved before becomes it once.

## What is not included

- **No third-party add-ons and no media sources.** The image has Kodi's standard add-ons, the Kodi add-on repository and the LibreELEC add-on repository, like LibreELEC. The only folders set up in Kodi are LibreELEC's empty local ones (Videos, TV Shows, Music, Pictures). What you install is up to you.
- **No Dolby software.** There is no Dolby code, library or licence key in Hades, and no code or specification of HDR10+ Technologies: FFmpeg reads the HDR10+ metadata, and the layout of their InfoFrame is taken from GPL code (see "Credits").
- **No telemetry.** Hades adds none and, by default, sends no usage statistics and no identifier of your PC. Like LibreELEC and Kodi, it still contacts the services they use (add-on repositories, time servers and the like): the page "Privacy policy" of the system information starts with a note that lists them. The diagnostic record, when you turn it on, stays on your drive: nothing sends it anywhere.

## Installing and starting

You need an image file (`LibreELEC-Generic.x86_64-...img.gz`): from the releases of the repository this file is in, or built by yourself.

1. Write the image to a USB drive, with the Hades installer for Windows (`LibreELEC-Hades-Installer.exe`, whose own README explains each step) or with any program that writes a disk image to a USB drive. The drive is erased.
2. Restart the PC, open the boot menu of the firmware (F8, F11, F12 or Esc on most PCs) and choose the USB drive.
3. The first start prepares the storage and restarts once.

To go back to your usual system, shut Hades down and start the PC normally. Started from USB, Hades does not change the boot settings of the PC or anything in Windows.

## Updating

- With the Windows installer: choose the drive that already has Hades and "Update". Settings and add-ons are kept, and so is the folder of the diagnostics.
- Or as in LibreELEC: copy the `.tar` file of the new build to the `Update` folder and restart.

## Building from source

In outline; LibreELEC's own documentation applies to everything not said here.

1. Use a Linux machine (the author uses Ubuntu 26.04; LibreELEC keeps container recipes in `tools/docker/`), an unprivileged user, a path without spaces, and about 75 GB of free disk.
2. Get this repository: `main` is the current version, and the source of a build is the commit that its `BUILD_ID` names.
3. Build: `PROJECT=Generic DEVICE=Generic ARCH=x86_64 make image`
4. The results appear in `target/`: an `.img.gz` (for a USB drive), a `.tar` (for updating) and an `.ova` (virtual machine).

Things to know:

- The build downloads the source of every package from the address in its recipe (`package.mk`) and checks it against the SHA-256 written there. No recipe has an empty hash, and prebuilt "reusable" toolchain packages are not used (`USE_REUSABLE="no"`).
- The NVIDIA driver package is downloaded from NVIDIA and unpacked without being run. Only its user-space libraries and firmware are used, unchanged; the kernel modules are compiled from NVIDIA's open source code, and so is the GBM library of its EGL (egl-gbm). By building you accept NVIDIA's licence for those files.
- The author's own builds are made without network access, from sources fetched and verified beforehand. A clean build takes between twenty and forty-five minutes on a 32-thread machine; much longer on a small one.
- **If you change the tree and share your build, give it a name of your own** (`BUILDER_NAME=<your name> make image`) and say that it is yours. Without one the build calls itself "Hades 1.0 RC3".

The Windows installer is a separate small program (C#, in `tools/hades-installer/`); `build.ps1` builds it with the compiler that Windows already has.

## Technical notes

The [documentation](docs/README.md) has more: how the Dolby Vision output works, every setting of this build, and the diagnostics.

- **Signal.** Dolby Vision "Standard" is sent as an 8-bit full-range RGB HDMI signal that carries 12-bit YCbCr 4:2:2 video with the display-management metadata in the low bits of the chroma, plus a Dolby vendor-specific InfoFrame. The transport follows the public specification ETSI GS CCM 001 V1.1.1. The output must be bit-exact: no colour conversion, look-up table, dithering, scaling, variable refresh or DSC. Low-latency Dolby Vision uses the same tunnel with the picture mapped by Hades and no metadata in it, a vendor InfoFrame that says low latency, and an AVI InfoFrame of YCbCr 4:2:2 in limited range.
- **Common part:** FFmpeg reads the Dolby Vision metadata (RPU), libplacebo rebuilds the picture with the composer of the specification and adds the enhancement layer, and a module in Kodi, `dvbridge`, packs and presents the frame.
- **One output per driver,** found by DRM properties: Intel `i915` and `xe` (kernel patches from CroqueMr's fork, with this project's changes), AMD `amdgpu` (kernel patches of this project; it needs an HDMI TMDS link) and NVIDIA `nvidia-drm` with the open kernel modules (the vendor InfoFrame property of the driver, 8 bits per component and dithering off; on a dual-mode DisplayPort connector too, with this project's patches). HDR10+ goes out through one DRM property, `HDR10PLUS_VSIF`, that this project's kernel patches give each driver.
- **Which TVs qualify:** an EDID with a Dolby Vision block of version 2 and the "Standard" interface, or of version 1 (12 bytes) with the low-latency field at 1, whatever its display management version (a TV of display management 2 or 3 is sent the content mapping v2.9 part of the metadata, the part it reads); on NVIDIA the TV must also allow the quantisation range to be selected. With Dolby Vision Low-Latency on, also a block that offers only low latency. The older signalling of Dolby Vision (the HDMI 1.4b vendor InfoFrame of version 0 blocks, of the 15-byte version 1 block and of the low-latency field at 0) is not sent.
- **3D, decoding.** FFmpeg decodes only the base view of H.264 MVC. Hades builds a second H.264 decoder, `h264_mvc`, from a copy of FFmpeg's decoder with Dom Cobley's multiview series applied (FFmpeg pull request 24303, not merged); FFmpeg's own H.264 decoder is left byte for byte as it is, and Kodi asks for the second one by name, only for a stream that has a second view. Checked on a host build against the H.264 reference decoder (JM): the 27 MVC conformance streams of ITU-T H.264.1 (base view identical in all; second view identical in 18 of the 19 the reference decoder gives one for, the exception being a superseded stream), and two stereo streams made for the purpose, identical in both views (1920x1080, and 960x540, which wraps the frame number of its second view, as a disc does all the time and no conformance stream does). The series is taken as it stood on 2026-10-14, carried to FFmpeg 9.0.2, with five fixes of this project.
- **3D, discs.** On a 3D Blu-ray the second view is in a clip of its own. What reads the two clips together is Dom Cobley's demuxer for Kodi, as he wrote it, with changes of this project around it: the second clip is read only where both views are wanted, the playlist says which clip and which angle, and after a picture that went on without its second view nothing of it is given to the decoder before a picture from which it can be decoded cleanly (an anchor picture).
- **3D, output.** A display learns the layout of a 3D picture from the HDMI vendor InfoFrame, which drivers send for a mode that has the layout in its flags. Intel's driver has such modes. For AMD the kernel carries the `amdgpu` patches of cinema-ONE's LibreELEC tree with fixes of this project; for NVIDIA, a patch of this project to the open kernel modules for frame packing, and the driver's vendor InfoFrame property for the half-resolution formats. A 3D mode is never one of Kodi's resolutions: it is how the resolution Kodi chose is sent while a 3D picture is drawn, at the same refresh rate, or at twice it for a 30p or 25p video whose display has no 3D mode at its rate.
- **Versions in 1.0 RC3:** Linux 7.2.12, Kodi 22 (the `Piers` branch after 22.0rc1, up to de8192b4200c), FFmpeg 9.0.2, Mesa 26.2.5, libplacebo from its main branch (285d1cc8af2a), NVIDIA 615.78.08.
- **Based on** LibreELEC `master` (13.0 development, up to fb1edf29eb07). LibreELEC has not released version 13.

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
- The complete source for a build is this repository at the commit named in `BUILD_ID` in `/etc/os-release` of the running system, plus the source archives its recipes name. See [NOTICE.md](NOTICE.md) for credits, trademarks and how to obtain the source.
- LibreELEC's own README, with its licence and copyright statement, is kept as [README.LibreELEC.md](README.LibreELEC.md), and its guide to contributing to LibreELEC as [CONTRIBUTING.LibreELEC.md](CONTRIBUTING.LibreELEC.md).

## Reporting problems

Report problems with Hades in the issue tracker of the repository this file is in, **not** to LibreELEC, Kodi, Dolby, HDR10+ Technologies, NVIDIA, AMD or Intel. Please include:

- the build (the `BUILD_ID` and `PRETTY_NAME` lines of `/etc/os-release`, or *Settings > System information*);
- the graphics (the card, or the processor if it is its integrated graphics), the TV model and how they are connected;
- what you played (container, Dolby Vision profile) and what you saw;
- the Kodi log, or the folder `hades-diag` of the drive if the diagnostic switch was on (from a system installed on a disk inside the PC, copy it to a USB drive first). Look through it first: it is filtered, but it is yours to check.

[docs/diagnostics.md](docs/diagnostics.md) says how to get the diagnostics and the log, and which lines of the log tell most.

If the same problem also happens with an official LibreELEC image, it belongs to LibreELEC or Kodi and can be reported there, against their image.

There is no warranty of any kind. You use this at your own risk.
