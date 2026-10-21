# How the Dolby Vision output works

LibreELEC Hades sends Dolby Vision the way TVs call **"Standard"** or **"TV-led"**: the PC rebuilds the picture, and the TV receives it with the Dolby Vision metadata of every frame and does its own display mapping. **Low-latency ("player-led") Dolby Vision is not implemented**: a TV input that offers only that gets HDR10, and Hades says so on screen.

All of this is about native Dolby Vision output, and Dolby Vision shown as HDR10 where native output is not possible. SDR, HDR10 and HLG videos play through Kodi's ordinary code, as in LibreELEC.

## From the file to the HDMI cable

| Step | Done by | What happens |
| --- | --- | --- |
| Read and decode | Kodi and FFmpeg | The base video is decoded in hardware with VAAPI, which native output and Dolby Vision shown as HDR10 need: a stream that VAAPI does not decode plays as Kodi plays it, and the Dolby Vision page of the player information says why. FFmpeg reads the Dolby Vision metadata (the RPU) of every frame. |
| Enhancement layer (profile 7) | the bridge's FEL module | A full enhancement layer (FEL) is decoded by a second HEVC decoder, also with VAAPI. Each enhancement picture is paired with its base picture: by picture order when both layers are in one track, by presentation time when they are two tracks (the `.m2ts` of a Blu-ray). The metadata used for a picture is the one that came with its enhancement picture. A minimal enhancement layer (MEL), whose metadata rebuild no residual, is not decoded at all: the picture is the same, byte for byte. |
| Compose | libplacebo, with this project's patches | The picture is composed from its layers and metadata as the composer of ETSI GS CCM 001 V1.1.1 (clause 5.4) defines it, in integers: the polynomial and MMR mappings of the RPU and, with FEL, the residual of the enhancement layer, resampled with the filters of annex B.3. On a software renderer, on 8 pictures of a profile 7 FEL stream and 12 made-up cases, the composed planes are those of two transcriptions of the clause written apart, sample for sample. A picture that the clause does not cover (for example MMR on the luma, or a layer of another size) is composed by libplacebo's own reshaping, which an expert setting can also choose for every picture; it differs from the clause by 4 to 15 codes of 4095 in almost every pixel, and by up to 742 at edges of high luma contrast. The area outside the active picture (the black bars that the metadata describe) is written first and is not composed. |
| Menus and subtitles | Kodi | When menus, the OSD or subtitles are on screen, they are drawn over the picture, at the brightness set for HDR. |
| Pack | the bridge's packers (OpenGL ES shaders) | The picture becomes the Dolby Vision "tunnel": 12-bit YCbCr 4:2:2 carried in an 8-bit RGB frame, with the display-management metadata of the frame in the low bits of the chroma. By default it is sent the way a certified source device sends it (clause 6): the composer's output picture as it is, in the colour representation of its RPU (YCbCr for profiles 7 and 8, IPT for profile 5), with display-management metadata that describe that picture; its 4:2:0 chroma is brought to 4:2:2 with a 12-tap Lanczos filter. Where menus or subtitles are drawn over the video, those pixels are converted back into that representation, and the pixels they do not touch are sent exactly as composed. A video that is scaled to the screen (a 1080p one) and a picture composed by libplacebo's reshaping are rebuilt in RGB and converted to BT.2020 YCbCr, as 1.0 RC1 sent every picture; an expert setting sends every picture that way. |
| Send | the output backend of the graphics driver (DRM/KMS) | The frame is shown with the Dolby vendor-specific InfoFrame and full-range RGB, 8 bits per component. The output must be bit-exact: no colour conversion, look-up table, dithering, scaling, variable refresh or DSC. |

The tunnel follows the public specification ETSI GS CCM 001 V1.1.1. No Dolby software, library or key is used.

## What is sent as metadata

Every frame carries its own metadata; nothing of an earlier frame is reused. Hades does not change, add or invent display-management data: the TV's mapping is the one the source was mastered for.

| Level | What it is | How it is sent |
| --- | --- | --- |
| L1 | Minimum, average and maximum brightness of the scene | As in the stream. An average outside its minimum and maximum (an encoder error) is moved inside them instead of refusing the frame |
| L2 | Trims for target displays | As in the stream |
| L5 | Active area (the black bars) | Converted to the place of the picture in the 3840x2160 HDMI frame, after Kodi's scaling and letterboxing. On each edge where menus or subtitles are drawn over the bars, the offset sent is 0, so that the active area holds what is drawn there |
| L4 | Further display-management data (content mapping v2.9) | As in the stream |
| L3, L8, L9, L10, L11, L254 | Content mapping v4.0 data | As in the stream to a TV of display management 4 or later. A TV of display management 2 or 3 does not read them and is not sent them: an RPU carries them apart, where a reader of content mapping v2.9 does not look, and ETSI GS CCM 001 V1.1.1 reserves their levels. The Dolby Vision page of the player information says it ("Content mapping sent") |
| L6 | Mastering display, MaxCLL and MaxFALL | Not part of what the tunnel carries. Used as the HDR10 static metadata when Dolby Vision is shown as HDR10 |
| L255 | Encoder debug data | Left out |

The header of the metadata describes the picture that is sent: by default the colour matrices and offsets of the RPU and its bit depth (clause 6.2.2); for a picture converted to BT.2020, BT.2020 YCbCr in limited range at 12 bits. The metadata of a frame may take up to 512 bytes (clause 6.3.3.3).

A frame that comes more than 1.5 frame periods after the one before it, or more than a tenth of a second, starts a new scene in the metadata it is sent with: the frame in between, which was not shown, may have been the first of a scene, and the TV would carry its temporal filtering over the cut. Where most of the last 16 gaps were that long (a frame rate that does not describe the stream), only the tenth of a second counts.

## When native Dolby Vision is used

Only when every condition below holds. Otherwise the video plays without native output (its HDR10 or SDR picture, or rebuilt as HDR10 where the settings say so: see below), and `kodi.log` and the Dolby Vision page of the player information say why, with the reason the graphics driver gives where it refuses the tunnel (Intel and AMD).

| Condition | Notes |
| --- | --- |
| The video is Dolby Vision with valid metadata, progressive and not stereoscopic 3D, decoded with VAAPI | Any profile, in HEVC or AV1. What has been tried on which hardware is in the [README](../README.md); AV1 with Dolby Vision has not been tried (no sample) |
| "Native Dolby Vision output" is on (the default), and the video's own choice in the player's video settings is the automatic one | See [settings.md](settings.md) |
| The display has exactly one Dolby Vision block (VSVDB) in valid EDID blocks: version 2 with the Standard interface, or version 1 in its 12-byte form with the low-latency field at 1, of any display management version | A display that advertises only low-latency Dolby Vision (often a TV input set to its game or PC mode) gets HDR10 and a notice. Older blocks (version 0, the 15-byte version 1 block, or the low-latency field at 0) ask for the HDMI 1.4b vendor InfoFrame, which Hades does not send: such a display gets HDR10 |
| The graphics driver has a Dolby Vision backend (see "Graphics drivers" below) and modesetting is atomic | Kodi and the Intel and AMD kernel backends read the Dolby Vision block with the same rule |
| NVIDIA: the display lets the source select the RGB range (VCDB QS bit) | Full-range RGB has to be signalled |
| The display mode is 3840x2160, progressive, at 23.976, 24, 25, 29.97, 30, 50, 59.94 or 60 Hz, without a 3D layout | Above 30 Hz, the display must also take Dolby Vision at 2160p60 |

The mode is the one Kodi chooses for the video. **Dolby Vision never changes the refresh rate Kodi would choose**; progressive video gets its exact rate (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). When the menus run at 1920x1080, the video's mode is 3840x2160 at the same rate. When the refresh rate is not adjusted to the video and the menus run at a rate that the tunnel does not have (100 Hz or more, or 48 Hz), the video plays without native output.

The display's EDID is read again when each video starts, never in the middle of one: a TV that was switched on after the PC, or whose input setting changed since, is taken as it is now. An EDID that cannot be read at that moment replaces nothing.

**It fails closed.** If the native output cannot start, or stops working during a video (the driver refuses a commit, frames cannot be drawn, or the page flips of its frames stop coming: one that does not come within 2 seconds twice in a row, or within 5 seconds once), the video continues on the ordinary output and `kodi.log` says why. Native output is tried again with the next video; if it fails again, the wait grows to two, four and at most eight videos, and a display that changed is tried at once. A frame whose enhancement layer cannot be used is drawn without it, and counted in the log.

## Two ways of drawing a frame

With the default transport, a frame with nothing over it is drawn **directly**: its planes are composed and packed into the tunnel in one pass. A frame with menus, subtitles or the player bar over it is **composed**: the picture is decoded from the tunnel into a full-precision picture, Kodi draws over it, and the packer converts the pixels that were drawn over back.

A picture that goes out converted to BT.2020 (a scaled video, or the expert settings of the transport or of the composition) can be drawn either way when nothing is over it: directly (rebuilt and packed in one pass) or composed (rebuilt into a full-precision picture, then packed). Which way is faster depends on the graphics chip, so Hades measures the GPU time of both for each picture format (size, active area, with or without an enhancement layer) whose frames take a large part of the refresh period, and uses the faster one; formats that take little go the way found for the others. What it finds is kept for each graphics chip, driver and build: only the first Dolby Vision video after installing a build measures. Frames with menus or subtitles, and pictures that do not fill the screen, are always composed. The expert setting "Drawing path" can fix the way.

On a software renderer, with the composer of the specification, the two ways gave the same bytes in all 48 drawings of 8 pictures of a profile 7 FEL stream. With libplacebo's reshaping they differ by at most one code of 4096 on fewer than 0.1 % of the pixels (two shaders that round the same value), never in the bars and never in a bit of metadata.

A picture that stays on the screen for more than one refresh (a film at 60 Hz, a paused video) is kept there instead of being drawn and sent again: the display gets the same bytes, and the graphics chip does less work.

## A frame ahead, when needed

Where the graphics chip needs, on average over a second of film, at least half of a refresh period to prepare a frame (an integrated chip with profile 7 FEL; at 50 or 60 Hz, where the period is shorter, more chips), frames are presented one refresh ahead: the GPU's work is fenced, the commit does not wait for the screen, and Kodi schedules each frame one period earlier so that picture and sound stay in sync. The GPU then has up to two refresh periods for a frame. When the frames take less than 35 % of the period again, they go back to the way that waits. The cost: menus over such a video respond one frame later.

A frame that the driver refuses this way is presented the ordinary way at once, so no frame is lost. Three such frames close together (a minute of film without one forgets them) turn it off for that native output; three native outputs in a row that turned it off, or an output without enough buffers for it, turn it off until Kodi starts again. The setting "Present frames a frame ahead" chooses when needed (the default), always or never ([settings.md](settings.md)).

## A step down in fidelity, where the graphics chip cannot keep up

On a graphics chip that cannot prepare native frames in the time of a refresh, even drawn the faster way and a frame ahead, the picture of a format is composed a little less precisely, one level at a time and only as far as its frames need to keep up. This is the automatic setting "Fidelity step-down", the default. A graphics chip that keeps up always shows the faithful picture, level 0.

How it decides, for each picture format (size, active area, and whether the residual of an enhancement layer is drawn):

- frames are measured in windows of 48, of one level, one way of drawing, one pace and one refresh rate, leaving out frames with menus, subtitles or a capture, a picture that stands still, the first two seconds of a video and after a seek, and the first frames after a change;
- a window is behind only when the GPU times and the refreshes both say so: frames presented a frame ahead that take more than 85 % of the period on average (without the slowest one), or frames presented the way that waits of which 5 % or more (at least 3) take more than 80 % of it; and in both cases 5 % or more of its presentations (at least 3, of at least 24 whose outcome is known) missed their refresh;
- two windows behind in a row, or one in which the chip could not keep up in any case, take the format one level down, and that level is measured before anything more is decided. A level that is measured not to save time is passed over; when none keeps up, the format stays at the fastest level measured until the refresh rate or the pace changes;
- the format goes back up when what the level above would take now fits with room to spare (75 % of the period a frame ahead, 70 % the way that waits), no sooner than a minute of film after it went down, and twice that each time it had to come down again, up to about an hour of film;
- nothing is decided while the two ways of drawing a format are compared, and no format goes down while frame ahead can still be had and is not in use yet;
- what is found is kept for the graphics chip, its driver and the build, so that a later session draws such a format at its level from its first frame (not what is found while an expert setting of the engine makes the frames other work).

What a level changes depends on how the picture is composed and sent. With the defaults (the composer of the specification, sent as certified players send it), only frames that draw the residual of a full enhancement layer (FEL) can step down, to levels 1 and 2; every other frame is always drawn at level 0. Where the picture goes out converted to BT.2020 (a scaled video, or the expert transport setting), level 3 is used as well, and it is the only level of a frame without a residual.

| Level | With the composer of the specification | Measured on picture 51 of a profile 7 FEL stream |
| --- | --- | --- |
| 1 | The luma of the enhancement layer is resampled between columns from the two nearest samples instead of with the 8-tap filter of annex B.3: 25 million texture fetches fewer of about 197 million for a 3840x2160 frame | 11.7 % of the pixels of the active picture change, by 9 codes of 4095 at most, 0.17 on average |
| 2 | Also between rows (the two nearest samples instead of 4 taps), and its chroma between columns: 39 million fewer | 34.3 %, by 12 codes at most, 0.52 on average |
| 3 | Also the composed chroma scaled up bilinearly instead of with Lanczos: a pass fewer, and 106 million fetches fewer in all | 2.1 % to 47.8 % of the pixels on the three pictures measured; the composed chroma alone, on picture 51, 28.5 % by 0.36 on average, but up to 331 codes at edges of strong colour (4866 pixels by 16 or more) |

These were measured on a software renderer (Mesa's OpenGL ES, zink on lavapipe) against the faithful picture, on the composed planes (codes of 4095) of pictures 3, 27 and 51 of a profile 7 FEL stream. Pictures 3 and 27 have a flat enhancement layer: levels 1 and 2 change nothing there. Level 3 comes last although it saves the most, because it changes the most.

With libplacebo's reshaping (an expert setting), the planes that are scaled up to the size of the picture are scaled up bilinearly instead of with Lanczos: level 1 the chroma of the base layer, level 2 also the chroma of the enhancement layer, level 3 also its luma. On three pictures of that stream the three levels change up to 22 %, 48 % and 56 % of the pixels, most of them by one code of 4095 and none by more than 7, 10 and 16. A frame without the residual of an enhancement layer can step down to level 1 only.

What a level saves in GPU time is not known before it is measured on the graphics chip at hand, and may be nothing: a level is kept only for what it was measured to do there. The level of each frame is in the GPU-time lines of `kodi.log` and on the Dolby Vision page of the player information. The setting can also turn the step-down off, or set a level for every frame to measure it ([settings.md](settings.md)). Nothing else changes at any level: the metadata, the tunnel, the bars. Dolby Vision shown as HDR10 always uses the faithful picture.

## Leaving Dolby Vision

When the video ends, the ordinary output comes back: the menus' mode, the colour properties and range of before, the InfoFrame off. When Kodi sets another mode of the same size right after the video (it goes back to the menus' refresh rate), that restoration waits for the change of mode, two seconds at most with the last frame on screen, and goes out in the same commit: one change of the HDMI link instead of two, with every graphics driver. Where no such change comes, the output is restored on its own; on NVIDIA it is then turned off and on, so that the HDMI link is set up from scratch.

Starting is one change too: when Kodi sets the video's mode before the video opens, that mode waits for the first frame of the native output, and goes out with it, where that can be done (an output that is on, and a mode of the same size as the one on the screen). The same holds for HDR10 and HLG videos, whose HDR signalling goes out with the mode of the video, and ends with the mode of the menus. Each such wait has a deadline (3 seconds at the start, 2 at the end), after which what waited goes out alone.

## Dolby Vision shown as HDR10

Where native output is not possible (a display without Dolby Vision, a TV input that offers only low-latency, a mode that cannot carry the tunnel), a Dolby Vision video can be rebuilt the same way and sent as HDR10, inside Kodi's ordinary HDR10 path, so any window size, zoom or shift works.

By default this is done only for profile 5, whose base picture is not a usable picture on its own (without it, profile 5 plays with wrong colours), and for an AV1 stream whose base layer is compatible with nothing; profile 7 with an enhancement layer can be added in the settings; other videos play their HDR10 picture. In the player's video settings, a Dolby Vision video can also be sent as HDR10, or played as Kodi plays it (but not one of profile 5), for that video only. If 48 frames in a row cannot be rebuilt, the video continues on Kodi's ordinary path.

By default there is **no tone mapping**: the display receives the master and adapts it as plain HDR10, and the HDR10 static metadata come from the stream's L6. For projectors and displays that adapt HDR10 poorly, the setting "Dolby Vision as HDR10: map to the display" adapts the picture to the peak and the colours of the display set there, shot by shot with the brightness of each shot (L1), with libplacebo's tone and gamut mapping, and tells the display that the picture now fits it. While it is on, and "Dolby Vision as HDR10 when needed" is on, every Dolby Vision video that is not sent natively is sent this way, whatever its profile. The creative trims of Dolby Vision (L2 and L8) are not applied: this is libplacebo's mapping, not Dolby's.

## Other parts

- **Exact refresh rates on NVIDIA.** NVIDIA's driver offered many TV modes only at 23.976, 29.97, 59.94 or 119.88 Hz. A patch to its open kernel modules adds the twin at exactly 24, 30, 60 and 120 Hz of each such mode, with the exact clock, as Linux's own EDID code does.
- **HDR10 at more than 8 bits on NVIDIA.** NVIDIA's EGL offers no 10-bit window, only 8-bit and 16-bit floating point; 10-bit video goes out through the 16-bit one. If the graphics chip refuses it, the 8-bit one comes back.
- **HDR10 and HLG at 10 bits on Intel and AMD where RGB would be 8.** At 3840x2160 above 30 Hz over HDMI TMDS (HDMI 2.0, and HDMI 2.1 without FRL, which Intel never uses and AMD has off by default), RGB fits at 8 bits only, and the drivers sent HDR at 8 bits. Where the TV takes the mode in YCbCr 4:2:0 too, with deep colour in 4:2:0, HDR now goes out in 4:2:0 at 10 bits or more (12 on older Intel graphics): the video is 4:2:0 already, so none of it is lost, and only the menus and subtitles over it have half the colour resolution. Intel's driver now encodes 4:2:0 with the BT.2020 matrix that the TV is told (it used the BT.709 one for every YCbCr output). SDR and Dolby Vision are not touched. It can be turned off in the settings ([settings.md](settings.md)), and the player information says the depth and the format the driver sends.
- **Screenshots during Dolby Vision** read the picture, not the tunnel.
- **The GPU programs** of the ways a native output can draw its frames are made ready before its first frame, and kept on disk from one session to the next.
- **A check on the GPU of the bytes sent:** with `<dvverify>` in `advancedsettings.xml`, frames are compared with the same picture drawn again with every optimisation that must not change bytes turned off, for a test on a graphics chip ([settings.md](settings.md)).

## Graphics drivers

The backend is found by the properties of the HDMI connector. What has been tried on which graphics chip is in the [README](../README.md).

| Driver | How the tunnel is signalled | Notes |
| --- | --- | --- |
| Intel `i915` | Connector property `DVBRIDGE_DV_STANDARD_LAB`, from kernel patches of CroqueMr's fork, with this project's changes (the Dolby Vision block read with the same rule as Kodi and amdgpu, and the reason of a refusal) | Display version 9 and later |
| AMD `amdgpu` | The same property, from kernel patches of this project | Display hardware DCN 1.0 and 1.0.1 (Raven, Picasso, Dali, Pollock), DCN 2.0 (Navi 1x), DCN 2.1 (Renoir, Lucienne, Cezanne) and DCN 3.x. Not DCN 2.0.1, DCN 4 or the older DCE. Needs an HDMI TMDS link (HDMI FRL is off by default in amdgpu) |
| NVIDIA `nvidia-drm`, open kernel modules (driver 615) | The driver's `NV_HDMI_VSIF_METADATA` property for the InfoFrame; `max bpc` (a patch of this project) at 8; `dithering mode` off; the colour properties neutral | Turing and newer: GeForce GTX 16, RTX 20 and newer, and the professional chips of those generations (the 615 branch drives no older graphics) |

## Where it is in the source

| Part | Where |
| --- | --- |
| The bridge: metadata and transport, the enhancement layer, libplacebo, the packers | `tools/dvbridge/` in Kodi's source once patched: `dvbridge_core.c`, `dvbridge_metadata.h`, `dvbridge_fel.c`, `dvbridge_mel.h`, `dvbridge_placebo.c`, `dvbridge_render.c`, `dvbridge_source.c` (the transport of certified source devices), `dvbridge_gl_pack.c`, `pack_gles300.frag`, `dvbridge_cache.c` (the programs kept on disk), `dvbridge_verify.c`. It comes from CroqueMr's fork (`projects/Generic/patches/kodi/kodi-9990-native-dv.patch`, as the fork published it), with this project's changes on top |
| Kodi's renderer and the output | `xbmc/cores/VideoPlayer/VideoRenderers/DVBridgeGLES.cpp`; `xbmc/windowing/gbm/` (`WinSystemGbm.cpp`, `WinSystemGbmGLESContext.cpp`, `drm/DVBridgeState.cpp`, `DVPathChoice.h`, `DVAhead.h`, `DVStepDown.h`, `DVEngineSettings.h`, `DVPresentStall.h`, `ModeChangeHold.h`, `DisplayLook.h`, `DVVerify.h`); `xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h` |
| Patches to Kodi | `projects/Generic/patches/kodi/` and `projects/Generic/patches/kodi/x86/`: one patch for each change, each saying in its header what it does and why |
| FFmpeg, libplacebo | `packages/multimedia/ffmpeg/patches/`; `packages/addons/addon-depends/multimedia-tools-depends/libplacebo/patches/` (the composer of the specification is `libplacebo-9930`, its planes for the transport `libplacebo-9932`, its step-down `libplacebo-9940`) |
| Linux | `projects/Generic/patches/linux/`: `linux-9900-*` (Intel, from CroqueMr's fork), `linux-9910` and `linux-9940` (AMD), `linux-9950` to `linux-9955` (the reason of a refusal), `linux-9965` to `linux-9969` (the Dolby Vision block rule of i915, HDR10 in 4:2:0) |
| NVIDIA | `packages/graphics/nvidia-open-kernel-modules/patches/` |
