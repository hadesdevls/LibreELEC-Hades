# Credits

LibreELEC Hades is LibreELEC and Kodi with additions for a home cinema PC. What Hades adds was written for it, except what this page names: code by other people, findings and measurements that led to a fix, and ideas that a feature of Hades follows. Each entry says which of these it is and where it comes from. The page is kept current with every version.

- **Code** is taken with its authors: their names stay in the header of each patch and in the history of this repository. [NOTICE.md](NOTICE.md) gives the licence of each part.
- **Ideas** are written anew for Hades: the feature follows someone's idea, and the code is Hades' own.
- **Findings** are faults or facts that someone found and Hades then fixed or used in its own way.
- **Measurements** are figures that someone took on their own hardware and that a change of Hades rests on.

Patch names such as `kodi-9110` or `linux-9935` are files of the version in whose source they appear.

## Contents

- [The base](#the-base)
- [Code taken from others](#code-taken-from-others)
- [Read as data](#read-as-data)
- [Ideas written anew](#ideas-written-anew)
- [Findings and measurements](#findings-and-measurements)
- [Where requests came from](#where-requests-came-from)
- [Reports and tests](#reports-and-tests)

## The base

- **[LibreELEC](https://github.com/LibreELEC/LibreELEC.tv)** (Team LibreELEC, and OpenELEC before it): the whole distribution and its build system.
- **[Kodi](https://github.com/xbmc/xbmc)** (Team Kodi and its contributors): the media centre.
- **The authors of every program in the image**, each under its own licence.

## Code taken from others

### Dolby Vision

- **CroqueMr's Dolby Vision fork of LibreELEC, and its contributors** ([CroqueMr/libreelec-CB1](https://github.com/CroqueMr/libreelec-CB1)), where Dolby Vision on a PC began and where this work started. Code: its Dolby Vision module for Kodi (`dvbridge`), from which the Dolby Vision output of Hades grew; its Dolby Vision output for Intel graphics in the Linux kernel; and its changes to FFmpeg and libplacebo, among them a faster scaler for profile 7. The Dolby vendor InfoFrame that its kernel patches added to `drivers/video/hdmi.c` was written again for Hades with the same interface, under the licence of that file. Sixteen of its files are kept as the fork published them; [NOTICE.md](NOTICE.md) names them and gives the licence of each file of the module.
- **Rudi Heitbaum**: the update of LibreELEC to OpenSSL 4 ([LibreELEC pull request 11104](https://github.com/LibreELEC/LibreELEC.tv/pull/11104)), taken as his commit.

### 3D

- **Dom Cobley (popcornmix)**: the decoding of the second view of 3D Blu-ray video, his series of changes to FFmpeg's H.264 decoder ([FFmpeg pull request 24303](https://code.ffmpeg.org/FFmpeg/FFmpeg/pulls/24303)), applied to a copy of that decoder; and, in Kodi, how the two clips of a 3D disc are read together and one change to how the 3D modes of a display are read.
- **cinemaONE** ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)): four changes to AMD's kernel driver for 3D over HDMI (`linux-9911` to `linux-9914`) and the frame-packed 3D modes in Kodi (`kodi-9115`); with Dom Cobley, the subtitle depth of 3D remuxes and their plane offsets; and the lunasvg fixes that Hades carries.
- **Portisch**: full side by side and top and bottom in 3D file names, and one eye of a full 3D frame given the size of the whole frame, from his changes for CoreELEC ([CoreELEC/xbmc ae5090539e](https://github.com/CoreELEC/xbmc/commit/ae5090539edee29d72163a6091638167952d3492), [40f14dcdbe](https://github.com/CoreELEC/xbmc/commit/40f14dcdbef14b685b260fe8677f9e15195f9074)).

### Graphics and HDMI

- **Adrian Betschart**: the change to AMD's kernel driver that sends HDMI RGB in limited range on CTA modes, part of Linux 7.3, on which two patches of Hades build (`linux-9944`, `linux-9955`).
- **big-fella**: five changes to the HDMI and display code of the Intel and AMD kernel drivers (`linux-9932` to `linux-9936`), from [YBLOD's tree](https://github.com/dangerouslaser/libreelec-yblod/commit/3b3a983f93), with his authorship.
- **harment**: the file that keeps chosen outputs on for their sound (`hades-keep-outputs`, `kodi-9631`), his patch, from [issue 24](https://github.com/hadesdevls/LibreELEC-Hades/issues/24).
- **The Linux kernel developers**: the fixes that Hades takes from mainline before a release has them, and five fixes of Intel's GMBUS for Panther Lake and Wildcat Lake by Ville Syrjälä, from Intel's development tree, drm-intel-next.
- **NVIDIA**: the [open GPU kernel modules](https://github.com/NVIDIA/open-gpu-kernel-modules), [egl-gbm](https://github.com/NVIDIA/egl-gbm) and the open parts of driver 580, compiled from their published source; and **nvidia-vaapi-driver** ([elFarto/nvidia-vaapi-driver](https://github.com/elFarto/nvidia-vaapi-driver)).

### Sound

- **tanio99**: the fixes of DTS-HD passthrough ([Kodi pull request 30440](https://github.com/xbmc/xbmc/pull/30440), sent by samnazarko for OSMC).
- **heit88**: Dolby TrueHD passthrough after a seek ([Kodi pull request 30326](https://github.com/xbmc/xbmc/pull/30326)).
- **Martin Ellis (malard)**: a DTS core frame confirmed before it is taken as the stream (Kodi pull request 29641), and the JSON-RPC server changes of Kodi pull requests 29623 and 29636.
- **cinemaONE**: two fixes of the DTS-HD stream parser of Kodi.

### Blu-ray, DVD and subtitles

- **SamuriHL**: his changes to libbluray for Blu-ray Java menus, from [SamuriHL/CoreELEC bd95c331afe2](https://github.com/SamuriHL/CoreELEC/commit/bd95c331afe2), with his authorship, and the Kodi side of two of them, written with him (co-developed).
- **matthane**: two Blu-ray fixes of Kodi's `master` ([e5a50069a8](https://github.com/xbmc/xbmc/commit/e5a50069a8e038226ba7668c6b119135e7ce229a), [63362443735b](https://github.com/xbmc/xbmc/commit/63362443735bd151c803662927503085fd3e46d3)); the PGS fix `ffmpeg-9960`, which follows [his patch for CoreELEC](https://github.com/CoreELEC/CoreELEC/commit/0e28918fc1ca); and his fixes of external subtitles for CoreELEC's Kodi: a `.sup` file that seeks back to its start ([b8dbf95318](https://github.com/CoreELEC/xbmc/commit/b8dbf95318dc585ef986ba6ca87d04ace154eef8)), a chapter jump that moves the subtitle file ([1f443a651f](https://github.com/CoreELEC/xbmc/commit/1f443a651f248102895dd87ea8a1db6e8a0103c0)), the start of a seek ([ea33584d44](https://github.com/CoreELEC/xbmc/commit/ea33584d4425dd91e25931914e836c30c91820b2)), a subtitle file that is closed or replaced ([7be06c82f3](https://github.com/CoreELEC/xbmc/commit/7be06c82f3530e61ce0069121547939d69812180)), dense files read while the queues are full ([54a4a3f213](https://github.com/CoreELEC/xbmc/commit/54a4a3f2136300907d3524938fd1e818ced1940c)) and an atomic counter of demuxer ids ([ce-canary/xbmc 9414798e63](https://github.com/ce-canary/xbmc/commit/9414798e63e20903d3ca25f35ce7a878ebd31eb8)).
- **cinemaONE**: [Kodi pull request 29060](https://github.com/xbmc/xbmc/pull/29060) (picture subtitles resized and moved) and the changes around it that tell picture subtitles from menu graphics.
- **Daniel Oberlin**: fixes of libbluray for Blu-ray Java menus.
- **Saifelden Mohamed Ismail**: the change of libdvdcss that reads CPXM discs ([811ce97](https://code.videolan.org/videolan/libdvdcss/-/commit/811ce97)).
- **fandangos**: the libbluray change that lets the Java menus draw ([fandangos/libbluray 47726b99](https://github.com/fandangos/libbluray/commit/47726b99922899bb9c4ea688356199f2068d156a)).

### Network, files and the system

- **Rudi Heitbaum**: FTP sources with a user and password that no longer stop at the first seek with curl 8.23 ([Kodi pull request 30391](https://github.com/xbmc/xbmc/pull/30391)).
- **Daniil Gentili**: an HLS stream that loads only the variant it plays ([Kodi pull request 30046](https://github.com/xbmc/xbmc/pull/30046)), with its FFmpeg part written again for FFmpeg 9.1.
- **Portisch**: a font file with one broken face keeps the others ([Kodi pull request 30317](https://github.com/xbmc/xbmc/pull/30317)), and network shares that are not reopened while the system goes to sleep ([Kodi pull request 30662](https://github.com/xbmc/xbmc/pull/30662)).
- **User65k**: a fix of Kodi's UPnP HTTP parser, from [CoreELEC's Kodi](https://github.com/CoreELEC/xbmc/commit/5e7435aa7f25687d8032edcbd29266ddd971748e).
- **pannal, Stuart Marks and CoreELEC**: the CEC fixes of [pannal's CoreELEC tree](https://github.com/pannal/CoreELEC) (`libcec-9001` to `libcec-9003`), taken unchanged.
- **The authors of the commits of Kodi's `Piers` branch and `master`** that Hades takes before a release of Kodi has them, among them Alexander Golovanov, vincent71711, Garrett Brown, Marcin, ksooo, Kel Modderman, olympia, Chris, jjd-uk, 78andyp, Markus Härer and Dom Cobley.
- **Fixes from the projects' own repositories**, with their authors: avahi (42 changes; `avahi-9043` follows the ideas of its advisory and of its pull request 808), expat, OpenSSL, curl, OpenSSH, glibc, Python, iptables, libdrm, FFmpeg, BlueZ, BusyBox, libarchive, libtiff, libass, libnfs, libbluray, and Debian's fix for tinyxml.

### Upscalers

- **Bin Jin**: RAVU ([bjin/mpv-prescalers](https://github.com/bjin/mpv-prescalers)), an mpv shader, shipped unchanged.
- **igv**: FSRCNNX 8 and 16 ([igv/FSRCNN-TensorFlow](https://github.com/igv/FSRCNN-TensorFlow)), mpv shaders, shipped unchanged.
- **Alexkral**: AiUpscale for film and for animation ([Alexkral/AviSynthAiUpscale](https://github.com/Alexkral/AviSynthAiUpscale)), mpv shaders, shipped unchanged.

### Look, start and boot

- **Estuary**, Kodi's skin (phil65 and Team Kodi): the player information of Hades is made from its files for every skin, with four of its images.
- **Jost**, a typeface by indestructible type*: the name of the build in its pictures.
- **shim and MokManager** (Red Hat and the contributors of shim), as Debian 13 builds and signs them, for the optional start with Secure Boot.

## Read as data

Sources that Hades read to learn a format or a rule, and then wrote its own code for:

- **quietvoid**: [dovi_tool](https://github.com/quietvoid/dovi_tool) and [hdr10plus_tool](https://github.com/quietvoid/hdr10plus_tool) (MIT), the references for the syntax of the Dolby Vision and HDR10+ metadata, the rules and defaults that Hades follows when it makes Dolby Vision metadata, the definitions of the HDR10+ percentiles, and the fields of level 254.
- **CoreELEC, and the GPL kernel code of Amlogic in it** ([CoreELEC](https://github.com/CoreELEC)): the layouts of the HDR10+ InfoFrame and of the Dolby vendor InfoFrames, the Low-Latency signal, and which Dolby Vision displays get which signal; and the format of the `Player.Process(video.sidedata)` label, which add-ons read, from [its Kodi branch](https://github.com/CoreELEC/xbmc/commit/6da7084a843b).
- **smp79**: [Kodi pull request 30099](https://github.com/xbmc/xbmc/pull/30099) and pull request 30192, as CroqueMr's fork carries them: the settings for the colour format and the depth of the link, written again on the property of the Linux kernel.
- **Public specifications**: the Dolby Vision output follows ETSI GS CCM 001, and the trims are applied as SMPTE ST 2094-10 and ETSI TS 103 572 publish them. The tone mapping was measured against an open test film under CC BY 4.0; nothing of it is in the image or in this repository.
- **ht2tweak**: his [Full SBS 3D Converter](https://github.com/htfan8atic/Full-SBS-3D-Converter) (MIT), for the colours of Dolby Vision profile 20 without its Dolby Vision processing, and for taking the eye of each MV-HEVC view from what the stream says before the order of the views.
- **The network protocols of home-cinema projectors**, for *Projector control over the network*, written again from these descriptions; no code of any of them is in Hades:
  - **Steve Easley**: [pyjvcprojector](https://github.com/SteveEasley/pyjvcprojector) (MIT), the network handshake of JVC D-ILA projectors, the hashed password of the 2024 models and the command codes;
  - **Galala7**: [pySDCP](https://github.com/Galala7/pySDCP) (MIT), and the Sony projector binding of [openHAB](https://github.com/openhab/openhab-addons) (EPL-2.0): the framing of Sony SDCP and its item and value codes;
  - **kennymc-c**: [ucr-integration-sonyADCP](https://github.com/kennymc-c/ucr-integration-sonyADCP) (GPL-3.0), the authentication and the commands of Sony ADCP, taken as facts of the protocol only;
  - **pszafer**: [epson_projector](https://github.com/pszafer/epson_projector) (MIT), the header and status codes of Epson ESC/VP.net and the ESC/VP21 commands.

## Ideas written anew

### From CroqueMr's build

[CroqueMr/libreelec-CB1](https://github.com/CroqueMr/libreelec-CB1). Each feature below follows an idea of that build; the code is Hades' own.

- Blu-ray Java menus with nothing to install.
- Changing the output of a video from the player information (O, then OK), after its Quick Switch ([pull request 165](https://github.com/CroqueMr/libreelec-CB1/pull/165)), asked for by ht2tweak (see [where requests came from](#where-requests-came-from)).
- Upscalers, with one choice for videos of 1080p and above and one for smaller videos ([pull request 150](https://github.com/CroqueMr/libreelec-CB1/pull/150)); the idea of neural upscalers in Kodi is ashlar's (below).
- SDR videos made in BT.2020 kept in their wide colours ([pull request 153](https://github.com/CroqueMr/libreelec-CB1/pull/153)).
- 8-bit HLG videos sent as Dolby Vision, asked for by lvasan ([issue 73](https://github.com/CroqueMr/libreelec-CB1/issues/73)).
- Low-Latency mapped to the black level that the display gives ([pull request 158](https://github.com/CroqueMr/libreelec-CB1/pull/158)).
- The interface composited by the packer of the signal in Low-Latency ([pull request 197](https://github.com/CroqueMr/libreelec-CB1/pull/197)).
- "As SDR" and the choices of an HLG video in the output of one video (with YBLOD), and Kodi's original player information on the first press (with YBLOD).
- The settings for the colour format and the depth of the link (from smp79's pull requests, above).
- In the packing of the Dolby Vision signal: the value that the two pixels of a pair share, worked out once through the quad operations of the GPU, a shorter path for black pixels of the interface, and other ways to take less GPU time while the interface is shown.
- The black of the display for projectors; the packet that sends HDR over HDMI on Intel HD and UHD 6xx graphics; HDR10 whose mastering display is written in two ways; the check of which video the graphics can show by the layout of each picture; a frame that the driver refuses; the check of an Intel HDMI link after the TV drops its signal; the black lines between the eyes of frame packing; an EDID extension block that is not valid; a Blu-ray play item that starts between key pictures, and telling a play item before its bytes; the start of a video on its clock; an enhancement layer that starts late; a refused BT.2020 tag; the line of each Dolby Vision video in `kodi.log`; the `previous-boot` folder of the diagnostics and the copy of the kernel log while a start runs; and a stream named HLG or HDR10 by its picture in the player information.

### From YBLOD

[dangerouslaser/libreelec-yblod](https://github.com/dangerouslaser/libreelec-yblod):

- Dolby Vision through the DisplayPort-to-HDMI converter of a board, with the first board of its list.
- HDR sent as YCbCr 4:2:2 at 12 bits inside the Dolby Vision signal.
- Dolby Vision for the menus.
- The peak of the TV set by hand for the automatic tone mapping (v4.0).
- The choice of a curve for the menus over HDR, "As SDR" for one video and Kodi's original player information (the last two with CroqueMr's build).
- The strings of Hades in a block of their own, and labels and settings for skins.
- The HDMI variable refresh rate of AMD graphics kept on TVs without a FreeSync code, with Linux 7.4.
- AMD's compute power profile while the Dolby Vision bridge draws, after big-fella's idea and measurement on a Vega 8 (dv-engine-vk), taken up by YBLOD.

### From the builders of CoreELEC and of Kodi

- **CorentinB**: films rebuilt from interlaced video, with fieldmatch and decimate ([CroqueMr/libreelec-CB1 pull request 101](https://github.com/CroqueMr/libreelec-CB1/pull/101)); measuring the time of a picture to the screen instead of a fixed latency ([pull request 106](https://github.com/CroqueMr/libreelec-CB1/pull/106)); the zoom to the active area of Dolby Vision videos ([pull request 136](https://github.com/CroqueMr/libreelec-CB1/pull/136)); and subtitles moved by a fixed step ([pull request 120](https://github.com/CroqueMr/libreelec-CB1/pull/120)).
- **ashlar**: neural upscalers in Kodi, as CroqueMr pointed out ([CroqueMr/libreelec-CB1 pull request 289](https://github.com/CroqueMr/libreelec-CB1/pull/289), and the [forum thread of Hades](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209632#post209632)).
- **cinemaONE**: 3D through a passive DisplayPort adaptor, and side by side at 1080p for a 3D display that takes it without listing it ([cinema-ONE/LibreELEC.tv](https://github.com/cinema-ONE/LibreELEC.tv)).
- **pannal** (CoreELEC builds, p3i): audio settings that apply during playback, the dynamic range compression of Dolby Digital, downmix settings, dialogue normalisation, a picture with a damaged timestamp that no longer holds the queue, and the audio and subtitle streams chosen in Blu-ray navigation ([pannal/CoreELEC](https://github.com/pannal/CoreELEC)).
- **avdvplus** (CoreELEC builds): the saturation of picture subtitles, a limit on the luminance that HDR10 tells a projector, an automatic choice of Low-Latency, the Atmos objects and dialogue normalisation of a stream, and the rules of content mapping v4.0.
- **SamuriHL** (CoreELEC builds, [SamuriHL/CoreELEC](https://github.com/SamuriHL/CoreELEC)): VC-1 at its own rate, a simulated 3D display for 3D discs that need one, the picture-in-picture streams of a Blu-ray never opened by themselves, and dialogue normalisation.
- **matthane** (CoreELEC's Kodi): a picture subtitle taken by the frame nearest its time, and telling libbluray which stream was chosen in Kodi.
- **Portisch** (CoreELEC): asking an amplifier over CEC only what volume control needs, and keeping the scaler of a small thumbnail for the next one.
- **malard and pannal**: a stream that goes on after its connection drops.
- **primez** (pannal's Kodi tree): network reads that end when the player stops.
- **damnms** (CoreELEC's settings add-on): nameservers of your own, also with DHCP.
- **Pro-me3us** (CoreELEC, "udev: Enable USB disk start/stop management"): a disk on USB stopped when the system powers off.
- **mephis** ([flhs0606/LibreELEC-Hades](https://github.com/flhs0606/LibreELEC-Hades)): a disc image that an add-on streams from an address that does not say so ([4fdd375477](https://github.com/flhs0606/LibreELEC-Hades/commit/4fdd375477)), a change of subtitle track without a jump and the variety of a language that goes first ([dcfd7435b4](https://github.com/flhs0606/LibreELEC-Hades/commit/dcfd7435b4)), HDR10 mapped scene by scene, and seeking and disc images on servers that allow one connection.

## Findings and measurements

- **CroqueMr**: the dynamic expansion of the formatter of Radeon RX 400, RX 500 and Vega cards; Kodi looking for CEC adapters only once; Dolby Digital at 32 kHz never found; a seek in a file without sound or without video that waited a second; the chapter marks lost around a Blu-ray jump; the static HDR packet of AMD on a stream that keeps the pipe of the one before ([pull request 306](https://github.com/CroqueMr/libreelec-CB1/pull/306), commit `94a0dbadf8`); an SDR video in BT.2020 after an HDR10 one that needs an InfoFrame that says SDR (pull request 306, commit `dd2c267ccd`); and the last pictures of a file dropped at its end ([CroqueMr/xbmc 2da20195f1](https://github.com/CroqueMr/xbmc/commit/2da20195f1)).
- **KarlK90**: a Dolby Vision RPU without its terminator, refused whole ([CroqueMr/libreelec-CB1 issue 208](https://github.com/CroqueMr/libreelec-CB1/issues/208)).
- **YBLOD**: Kodi's tone mapping method left at "Off" in a new profile (its patches `kodi-9909` and `kodi-9960`, [dangerouslaser/libreelec-yblod](https://github.com/dangerouslaser/libreelec-yblod)).
- **fandangos**: a level 5 block that a Dolby Vision video does not have ([CroqueMr/libreelec-CB1 pull request 212](https://github.com/CroqueMr/libreelec-CB1/pull/212)), and the range of a Blu-ray menu page ([pull request 211](https://github.com/CroqueMr/libreelec-CB1/pull/211)). His measurements on an LG C1 and a Hisense TV, with the TV's own player as the reference (pull request 212), and what the licensed driver of an OPPO player sends ([fandangos/oppo-dv-mkv, fix 24](https://github.com/fandangos/oppo-dv-mkv#24-tv-led-dolby-vision-metadata-lost-on-the-way-to-the-tv-cm-29-and-cm-40-v200)), are what the A/B comparison of *Check this setup* and the expert choices of the form of the Dolby Vision metadata are built on.
- **SamuriHL**: multichannel PCM with heights behind that lost its top back channels ([commit](https://github.com/SamuriHL/coreelec-xbmc/commit/0820cb342ca2dc1675aac85e62860c967b03bf47)), a passthrough frame too large for its burst ([commit](https://github.com/SamuriHL/coreelec-xbmc/commit/505a0b814c66a5fc2ed67583d189ef9e4da0eaf7)), a DTS header without a channel arrangement ([commit](https://github.com/SamuriHL/coreelec-xbmc/commit/b09f7a5763ac523ae60940663b32f87bb7333f26)), an empty list of refresh rates ([commit](https://github.com/SamuriHL/coreelec-xbmc/commit/5149f344982f1cf146d13c9da597aa09686109b6)) and a thread that dies on an exception ([commit](https://github.com/SamuriHL/coreelec-xbmc/commit/32a1fb009b3cb139a09d75ff2518b01dc47ede41)); with pannal, faults of TrueHD after a start or a seek and of Blu-ray menus, and the menus of Ultra HD Blu-rays drawn in PQ; with matthane, the PGS of a profile 5 video; and the joins of a disc and the menu loops that he measured.
- **vertox**: an HLG stream of an add-on that the player information did not call HLG, and the format that a PQ output is told by.
- **cinemaONE**: measurements of a licensed player on three HDR10+ films, and side by side at 1080p seen on a JVC projector.
- **corgeone**: the measurements with an Anthem AVM 90 between an Intel PC and an LG G2 ([issue 28](https://github.com/hadesdevls/LibreELEC-Hades/issues/28)) behind the empty HDMI vendor InfoFrame as a choice and the port kept off a moment before its link is set up again.
- **Felix Oghină**: what a Chromebook needs for its USB-C output, Dolby Vision through a USB-C hub and its surround sound ([pull request 29](https://github.com/hadesdevls/LibreELEC-Hades/pull/29)), done again for Hades after his findings, with his name on each commit.
- **NTM-3D and Khazar321**: frame packing that NVIDIA's driver refuses, on five NVIDIA cards ([issue 33](https://github.com/hadesdevls/LibreELEC-Hades/issues/33), [forum](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209639#post209639)); NTM-3D also a display that gives no EDID through a passive adaptor ([issue 31](https://github.com/hadesdevls/LibreELEC-Hades/issues/31)), and Khazar321 the start that stopped with an ASUS Xonar card ([issue 14](https://github.com/hadesdevls/LibreELEC-Hades/issues/14)).
- **JonnyVee**: the use of the graphics that two Ryzen processors of different generations reported differently ([forum](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209789#post209789)).
- **lvasan**: the brightness of native Dolby Vision on a Sony A80K, compared on his TV ([forum](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209589#post209589)), behind the switch that refreshes the scene in every frame and the A/B comparison of *Check this setup*.
- **MrPhenix007**: screenshots in HDR that came out dark ([issue 30](https://github.com/hadesdevls/LibreELEC-Hades/issues/30)).

## Where requests came from

Where a feature of Hades was asked for, or a credit was asked for, this is who asked and where.

| Request | Who asked, and where | In Hades |
| --- | --- | --- |
| Change the output of a video while it plays, as CroqueMr's build does (the idea was taken, above) | ht2tweak, [forum thread of Hades, post 80](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209259#post209259) | Since 1.1: O, then OK. The idea is credited to CroqueMr's build. |
| Credit for the idea of neural upscalers | CroqueMr, [post 166](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209630#post209630), [post 168](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209632#post209632) and [post 179](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209666#post209666) | Credited above: ashlar for neural upscalers, CroqueMr's build for the choice by resolution. |
| Screenshots in HDR that do not come out dark | MrPhenix007, [issue 30](https://github.com/hadesdevls/LibreELEC-Hades/issues/30#issuecomment-7035604561) | In this version: an SDR copy of every HDR screenshot, written for Hades. |
| SDR in BT.2020 or DCI-P3 when HDR is mapped to SDR, as a Lumagen video processor does | JonnyVee, [post 175](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209652#post209652) and [post 195](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209789#post209789) | In this version: a choice of the gamut of the SDR output when HDR is mapped to SDR, written for Hades from this request. |
| A way to compare the brightness of native Dolby Vision on a Sony A80K | lvasan, [post 154](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209589#post209589), [post 173](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209640#post209640) and [post 194](https://forum.libreelec.tv/thread/30682-x86-64-hadesdevls-builds-libreelec-hades-native-dolby-vision-hdr10-and-3d/?postID=209733#post209733) | An expert switch that refreshes the scene in every frame, and the A/B comparison of *Check this setup*, written for Hades. |

## Reports and tests

Everyone who tried Hades and said what they saw, in the [issues](https://github.com/hadesdevls/LibreELEC-Hades/issues) of this repository and on the LibreELEC and Kodi forums, shaped it. [What has been tested](docs/tested.md) gives their results with the source of each. Among them roronoazoro-blip, BigRedJim, cinema-ONE, wright96d, sultan917, Iftuga, andrei-er, Levi741i, vinc-lab-creator, harment, bevigilant, Khazar321, pulento, sat4all1-dev, nissel, SunnySnacks, RaphS8, SeaMonkey82, NTM-3D, corgeone, MrPhenix007, Mibix, buckypat, doppingkoala and Felix Oghină on GitHub, and testmest, ht2tweak, lfemperor, vertox, astrotrain, louyeh, MrMagic, theged, JonnyVee, lvasan, Alaric and siriusbox on the forums.
