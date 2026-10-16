# Changelog

The versions of LibreELEC Hades. Before 1.0 RC1 there were only private test builds.

*Las versiones de LibreELEC Hades, cada una también en español. Antes de 1.0 RC1 solo hubo builds de prueba privadas.*

## 1.0 RC2

### What's new for you

- **Native Dolby Vision, rebuilt exactly as the specification defines it.** Real Dolby Vision, handled by the TV itself as always; the picture sent to it is now rebuilt exactly as the published Dolby Vision specification describes it, and sent the way certified players send it. Colours, edges and bright highlights are more faithful, for every profile (5, 7 FEL, 7 MEL and 8.1).
- **Smoother on modest graphics.** Less work for the GPU on every frame, a repeated picture is not drawn again, and only on graphics that cannot keep up does the picture lose a tiny, measured amount of precision, and only as much as needed. Strong graphics stay bit-exact.
- **Menus at 120 Hz just work.** Starting and stopping Dolby Vision, HDR10 and HLG is a single display change, also with the menus at 119.88 or 120 Hz, and on NVIDIA, AMD and Intel.
- **Turn the TV on after the PC.** The TV is looked at again when each video starts, so Dolby Vision works even if the PC booted with the TV off.
- **New "Overview" page** in the player information: what is really sent to the TV (signal, mode, colour depth, the metadata actually transmitted), the video, the audio and the system, at a glance.
- **Projectors and TVs without Dolby Vision:** Dolby Vision can be mapped to the brightness of your display (off by default), and the Dolby Vision output can be chosen for each video.
- **Older AMD graphics:** native Dolby Vision now also on Raven, Picasso, Navi 1x, Renoir and Cezanne.
- **HDR10 at 10 bits** on Intel and AMD where the cable could only carry 8 bits before.
- **Live TV:** audio passthrough (Atmos included) on live channels, 10-bit channels that showed no picture, faster channel changes.
- **Profile 7 MEL** no longer decodes its empty enhancement layer: less work, the same picture.
- **Up to date and safer:** Linux 7.2.11, Kodi and LibreELEC at their latest commits, and fixes for known security issues in glibc, FFmpeg, curl, libnfs, libass and more.

### Details

**Dolby Vision picture**

- The reconstruction follows the normative composer of ETSI GS CCM 001: identical bit for bit to an integer reference of the specification in every case tested. libplacebo's reshaping, which every free Dolby Vision player uses and which departs from the specification by 4 to 15 codes on most pixels (and by hundreds at strong colour edges), stays available as an expert setting.
- Sent the way a certified source device sends it: the composer's output in the colour representation of its RPU (IPT for profile 5), with the RPU's own matrices in the DM metadata; menus and subtitles are blended into it, and pixels they do not touch are sent exactly as composed. An expert setting sends it converted to BT.2020, as 1.0 RC1 did.
- Chroma of both layers at the position the stream signals; 4:2:2 taken from the even pixel; exact Lanczos weights for whole-ratio scaling; very bright pixels that came out black are fixed; exact BT.2020, P3 and BT.709 matrices.
- Level 5 describes what is sent, also when menus or subtitles are drawn over the bars; metadata up to 512 bytes; level 11 checked as dovi_tool checks it; scene refresh after a gap.
- Profile 7 MEL: the enhancement layer, which carries no residual, is not decoded; the picture is the same, byte for byte. FEL always is.

**Performance**

- Automatic fidelity step-down only where frames are actually missed (measured per format, remembered per GPU and driver); it can be turned off.
- Repeated pictures are not drawn again; shader cache on disk; GPU programs prepared before the first frame; fewer imports per frame.
- A switch to check on the GPU that frames send the bytes they claim (`<dvverify>` in advancedsettings.xml).

**Display and HDMI**

- One display change at the start and end of Dolby Vision, HDR10 and HLG on all drivers.
- The TV's EDID is read again at each video start (a TV switched on after boot, Deep Colour switched on later, another TV).
- A frame whose page flip never comes no longer stalls playback.
- HDR10 in YCbCr 4:2:0 at 10 bits where RGB would be 8 bits (Intel, AMD); Intel's BT.2020 YCbCr matrix.
- The reason when native Dolby Vision is not used, as the graphics driver reports it; native Dolby Vision on AMD DCN 1.0 and 2.x.

**Settings and audio**

- Dolby Vision engine settings, Automatic by default; per-video Dolby Vision output; Dolby Vision as HDR10 mapped to the display.
- Passthrough on live TV and streams (a setting, on by default); DRM PRIME hands back what it cannot decode.

**Updates and security**

- Linux 7.2.11, libplacebo 804dc71cd38d, Kodi Piers up to 01d4146d5d25, LibreELEC master up to 3ca0d49bee40.
- Fixes taken from upstream, each read whole: glibc 2.44 (nine CVEs), FFmpeg (about ninety), libnfs (CVE-2026-108985), libass (CVE-2026-95127), curl, libbluray, libdvdread, libdvdnav, libarchive, Samba, BusyBox; BlueZ CVE-2026-105460 fixed by Hades.

Every change was measured on a software renderer (Mesa) against exact references, with tests that fail on purpose when the code is broken. What has been seen on a TV is in the README, "What has been tested". Some ideas came from other Dolby Vision builds and were written again for Hades; the README credits them.

## 1.0 RC1

The first public release. What it adds to LibreELEC, what has been tested and on what, its known limits and the versions of its main parts are in the [README](README.md): "What it adds to LibreELEC", "What has been tested", "Known limits" and "Technical notes".

*La primera versión pública. Qué añade a LibreELEC, qué se ha probado y en qué, sus límites conocidos y las versiones de sus partes principales están en el [README en español](README.es.md).*

---

The changes of LibreELEC itself are in [its own history](https://github.com/LibreELEC/LibreELEC.tv/commits/master); each version of this build is made on one of its commits, which is in the history of this repository.
