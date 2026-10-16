# How the Dolby Vision output works

LibreELEC Hades sends Dolby Vision the way TVs call **"Standard"** or **"TV-led"**: the PC rebuilds the picture, and the TV receives it with the Dolby Vision metadata of every frame and does its own display mapping. **Low-latency ("player-led") Dolby Vision is not implemented**: a TV input that offers only that gets HDR10, and Hades says so on screen.

All of this is about native Dolby Vision output. SDR, HDR10 and HLG videos play through Kodi's ordinary code, as in LibreELEC.

## From the file to the HDMI cable

| Step | Done by | What happens |
| --- | --- | --- |
| Read and decode | Kodi and FFmpeg | The base video is decoded as Kodi decodes it, in hardware (VAAPI) where it can. FFmpeg reads the Dolby Vision metadata (the RPU) of every frame. |
| Enhancement layer (profile 7 FEL) | the bridge's FEL module | A second HEVC decoder, in hardware (VAAPI), decodes the enhancement layer. Each enhancement picture is paired with its base picture: by picture order when both layers are in one track, by presentation time when they are two tracks (the `.m2ts` of a Blu-ray). The metadata used for a picture is the one that came with its enhancement picture. |
| Rebuild | libplacebo | The reshaping of the RPU (polynomial and MMR curves) and, with FEL, the residual of the enhancement layer rebuild the master picture. The area outside the active picture (the black bars that the metadata describe) is written first and is not rebuilt. |
| Menus and subtitles | Kodi | When menus, the OSD or subtitles are on screen, they are drawn over the picture, at the brightness set for HDR. |
| Pack | the bridge's packer (an OpenGL ES shader) | The picture becomes the Dolby Vision "tunnel": 12-bit YCbCr 4:2:2 carried in an 8-bit RGB frame, with the display-management metadata of the frame in the low bits of the chroma. |
| Send | the output backend of the graphics driver (DRM/KMS) | The frame is shown with the Dolby vendor-specific InfoFrame and full-range RGB, 8 bits per component. The output must be bit-exact: no colour conversion, look-up table, dithering, scaling, variable refresh or DSC. |

The tunnel follows the public specification ETSI GS CCM 001 V1.1.1. No Dolby software, library or key is used.

## What is sent as metadata

Every frame carries its own metadata; nothing of an earlier frame is reused. Hades does not change, add or invent display-management data: the TV's mapping is the one the source was mastered for.

| Level | What it is | How it is sent |
| --- | --- | --- |
| L1 | Minimum, average and maximum brightness of the scene | As in the stream. An average outside its minimum and maximum (an encoder error) is moved inside them instead of refusing the frame |
| L2 | Trims for target displays | As in the stream |
| L5 | Active area (the black bars) | Converted to the place of the picture in the 3840x2160 HDMI frame, after Kodi's scaling and letterboxing |
| L3, L4, L8, L9, L10, L11, L254 | Further display-management data | As in the stream |
| L6 | Mastering display, MaxCLL and MaxFALL | Not part of what the tunnel carries. Used as the HDR10 static metadata when Dolby Vision is shown as HDR10 |
| L255 | Encoder debug data | Left out |

## When native Dolby Vision is used

Only when every condition below holds. Otherwise the video plays the ordinary Kodi way (its HDR10 or SDR picture), and `kodi.log` says why.

| Condition | Notes |
| --- | --- |
| The video is Dolby Vision with valid metadata, progressive and not stereoscopic 3D | Any profile, in HEVC or AV1. Tried: profiles 5, 7 with FEL, and 8.1, in HEVC. AV1 with Dolby Vision has not been tried (no sample) |
| The display has exactly one Dolby Vision block (VSVDB) in valid EDID blocks: version 2 with the Standard interface, or version 1 in its 12-byte form with display management version 4 or later | A display that advertises only low-latency Dolby Vision (often a TV input set to its game or PC mode) gets HDR10 and a notice. Older blocks describe a signal that Hades does not implement |
| The graphics driver has a Dolby Vision backend (see "Graphics drivers" below) and modesetting is atomic | Kodi and the Intel and AMD kernel backends read the Dolby Vision block with the same rule |
| NVIDIA: the display lets the source select the RGB range (VCDB QS bit) | Full-range RGB has to be signalled |
| The display mode is 3840x2160, progressive, at 23.976, 24, 25, 29.97, 30, 50, 59.94 or 60 Hz, without a 3D layout | Above 30 Hz, the display must also take Dolby Vision at 2160p60 |

The mode is the one Kodi chooses for the video. **Dolby Vision never changes the refresh rate Kodi would choose**; progressive video gets its exact rate (25 fps at 25 Hz, 29.97 fps at 29.97 Hz). When the menus run at 1920x1080, the video's mode is 3840x2160 at the same rate.

**It fails closed.** If the native output cannot start, or stops working during a video (the driver refuses a commit, frames cannot be drawn), the video continues on the ordinary output and `kodi.log` says why. Native output is tried again with the next video; if it fails again, the wait grows to two, four and at most eight videos. The display is looked at again when each video starts, never in the middle of one. A frame whose enhancement layer cannot be used is drawn without it, and counted in the log.

## Two ways of drawing a frame

A frame with no menu or subtitle on it can be drawn in two ways: **direct** (rebuilt and packed in one pass) or **composed** (rebuilt into a full-precision picture, then packed). Frames with menus or subtitles are always composed. Which way is faster depends on the graphics chip, so Hades measures the GPU time of both, for each picture format (size, active area, with or without an enhancement layer), and uses the faster one. What it finds is kept for each graphics chip, driver and build: only the first Dolby Vision video after installing a build measures.

The two ways do not give identical bytes: they differ by at most one code of 4096 on fewer than 0.1 % of the pixels (two shaders that round the same value), never in the bars and never in a bit of metadata. This was measured with the bridge running on a software OpenGL ES.

## A frame ahead, when needed

Where the graphics chip needs, on average, at least half of a refresh period to prepare a frame (an integrated chip with profile 7 FEL, or any chip at 50 or 60 Hz), frames are presented one refresh ahead: the GPU's work is fenced, the commit does not wait for the screen, and Kodi schedules each frame one period earlier so that picture and sound stay in sync. The GPU then has up to two refresh periods for a frame. The cost: menus over such a video respond one frame later.

A commit that the driver refuses this way is repeated at once the ordinary way, so no frame is lost; three refusals in a row turn it off for that output. It can be turned off in the settings ([settings.md](settings.md)).

## A step down in fidelity

For a graphics chip that cannot draw a frame in the time of a refresh, the rebuilt picture can step down from the faithful one: the planes that are scaled up to the size of the picture are scaled up bilinearly, with the sampling of the GPU itself, instead of with Lanczos. Level 1 does it for the chroma of the base layer, level 2 also for the chroma of the enhancement layer, level 3 also for its luma. On three pictures of a profile 7 FEL stream, drawn on a software OpenGL ES, the three levels change up to 22 %, 48 % and 56 % of the pixels, most of them by one code of 4095 and none by more than 7, 10 and 16. From level 1 the luma of the base layer, which is not scaled, is also read the way it was before the exact read of unscaled planes, which needs another plane scaled with Lanczos: with linear filtering at the centres of its texels, which gives the texels themselves where the graphics chip computes the weights of the filter in fixed point (the software renderer differs from them by one code in under 0.02 % of the pixels). Nothing else changes: the metadata, the tunnel, the bars.

The faithful picture is never changed on graphics that keep up. For now the level is 0 unless `advancedsettings.xml` forces one ([settings.md](settings.md)), which is there to measure on a given graphics chip how much GPU time each level saves. When Hades is to step down by itself will be decided with those measurements. The output as HDR10 always uses the faithful picture.

## Leaving Dolby Vision

When the video ends, the ordinary output comes back: the menus' mode, the colour properties and range of before, the InfoFrame off. On NVIDIA the output is also turned off and on, so that the HDMI link is set up from scratch. When Kodi is about to set another mode of the same size right after the video (it goes back to the menus' refresh rate), that restoration waits for the change of mode, for two seconds at most with the last frame on screen, and goes in the same commit: one change of the HDMI link instead of two. Where no such change comes, the output is restored as before.

## Dolby Vision shown as HDR10

Where native output is not possible (a display without Dolby Vision, a TV input that offers only low-latency, a mode that cannot carry the tunnel), a Dolby Vision video can be rebuilt the same way and sent as HDR10, inside Kodi's ordinary HDR10 path, so any window size, zoom or shift works. There is **no tone mapping**: the display receives the master and adapts it as plain HDR10. The HDR10 static metadata come from the stream's L6.

By default this is done only for profile 5, whose base picture is not a usable picture on its own (without it, profile 5 plays with wrong colours). Profile 7 with an enhancement layer can be added in the settings; other videos play their HDR10 picture. If 48 frames in a row cannot be rebuilt, the video continues on Kodi's ordinary path.

## Other parts

- **Exact refresh rates on NVIDIA.** NVIDIA's driver offered many TV modes only at 23.976, 29.97, 59.94 or 119.88 Hz. A patch to its open kernel modules adds the twin at exactly 24, 30, 60 and 120 Hz of each such mode, with the exact clock, as Linux's own EDID code does.
- **HDR10 at more than 8 bits on NVIDIA.** NVIDIA's EGL offers no 10-bit window, only 8-bit and 16-bit floating point; 10-bit video goes out through the 16-bit one. If the card refuses it, the 8-bit one comes back.
- **HDR10 and HLG at 10 bits on Intel and AMD where RGB would be 8.** At 3840x2160 above 30 Hz over HDMI TMDS (HDMI 2.0, and HDMI 2.1 without FRL, which Intel never uses and AMD has off by default), RGB fits at 8 bits only, and the drivers sent HDR at 8 bits. Where the TV takes the mode in YCbCr 4:2:0 too, with deep colour in 4:2:0, HDR now goes out in 4:2:0 at 10 bits or more (12 on older Intel graphics): the video is 4:2:0 already, so none of it is lost, and only the menus and subtitles over it have half the colour resolution. Intel's driver now encodes 4:2:0 with the BT.2020 matrix that the TV is told (it used the BT.709 one for every YCbCr output). SDR and Dolby Vision are not touched. It can be turned off in the settings ([settings.md](settings.md)), and the player information says the depth and the format the driver sends.
- **Screenshots during Dolby Vision** read the picture, not the tunnel.

## Graphics drivers

The backend is found by the properties of the HDMI connector.

| Driver | How the tunnel is signalled | Notes |
| --- | --- | --- |
| Intel `i915` | Connector property `DVBRIDGE_DV_STANDARD_LAB`, from kernel patches of CroqueMr's fork | Display version 9 and later. Tried only on the integrated graphics of a Core i5-12450H |
| AMD `amdgpu` | The same property, from a kernel patch of this project | Needs an HDMI TMDS link (HDMI FRL is off by default in amdgpu). Display hardware DCN 1 and DCN 2 is not covered |
| NVIDIA `nvidia-drm`, open kernel modules (driver 615) | The driver's `NV_HDMI_VSIF_METADATA` property for the InfoFrame; `max bpc` (a patch of this project) at 8; `dithering mode` off; the colour properties neutral | GeForce GTX 16, RTX 20 and newer (the 615 branch drives no older card) |

## Where it is in the source

| Part | Where |
| --- | --- |
| The bridge: metadata and transport, the enhancement layer, libplacebo, the packer | `tools/dvbridge/` in Kodi's source once patched: `dvbridge_core.c`, `dvbridge_metadata.h`, `dvbridge_fel.c`, `dvbridge_placebo.c`, `dvbridge_render.c`, `dvbridge_gl_pack.c`, `pack_gles300.frag`. It comes from CroqueMr's fork (`projects/Generic/patches/kodi/kodi-9990-native-dv.patch`, as the fork published it), with this project's changes on top |
| Kodi's renderer and the output | `xbmc/cores/VideoPlayer/VideoRenderers/DVBridgeGLES.cpp`; `xbmc/windowing/gbm/` (`WinSystemGbm.cpp`, `WinSystemGbmGLESContext.cpp`, `drm/DVBridgeState.cpp`, `DVPathChoice.h`, `DVAhead.h`); `xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h` |
| Patches to Kodi | `projects/Generic/patches/kodi/` and `projects/Generic/patches/kodi/x86/`: one patch for each change, each saying in its header what it does and why |
| FFmpeg, libplacebo | `packages/multimedia/ffmpeg/patches/`; `packages/addons/addon-depends/multimedia-tools-depends/libplacebo/patches/` |
| Linux | `projects/Generic/patches/linux/` (Intel), `packages/linux/patches/default/` (`linux-9910`, AMD) |
| NVIDIA | `packages/graphics/nvidia-open-kernel-modules/patches/` |
