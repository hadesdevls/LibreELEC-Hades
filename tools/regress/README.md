# tools/regress

Host regressions of LibreELEC Hades: what Hades changed in Kodi (its Dolby Vision bridge, `tools/dvbridge`, and the
headers and logic of its output), in FFmpeg, in libplacebo and in the installer, each checked on the build host against
the trees of a version, without the image and without a display. Nothing here goes into an image. A runner reads the
trees it tests (by `git archive`) and never writes into them, into a build folder or into its inputs.

## Groups

| Folder | Runner(s) | What it checks |
| --- | --- | --- |
| `dvbridge/` | `run-dvbridge-meta.sh` | what the bridge sends and measures: the metadata serializer, HDR10+ (the InfoFrame, what the display is sent, HDR10+ sent as Dolby Vision), the source transport rows, the pack, frames drawn with things over them, the pass times, the CCM 001 reader and its mutations, the kernel's HDR10+ helpers, the graphics metadata and menus, the mapping to the display |
| `dvbridge/` | `run-dvbridge-render.sh` | what the bridge draws, byte for byte, on a software GPU: the fidelity bench, the measured test, the render test, the step-down levels, the gamut table, the GUI step and cache, the geometry of a view, the composed planes, the composer with a BT.1886 base layer and with profile 8.4, the IPT tunnel, HDR mapped to SDR in BT.2020 and DCI-P3 (`sdrgamut`), the step-down levels of Dolby Vision rebuilt as HDR10 and SDR (`convstep`), the 3D LUT of a display (`lut3d`: `.cube`, `.3dl`, madVR's and the 1D LUTs, ArgyllCMS's `.cal`, `LUT_1D_SIZE` alone and as Resolve's shaper, read or refused, every way that takes it against the LUT applied in Python, the ways that never take it to the bit, an identity LUT to the code), and in the video shaders of Kodi's own renderer (`lutsdr`: a picture of an SDR clip drawn every way Kodi draws one itself, with each LUT against it applied in Python, an identity LUT to the code, a shader told not to apply it to the bit, the table of the shaders point by point), and a 3D picture drawn a view at a time (`stereo3d`: each view copied out and drawn into its eye, value for value the same picture in 2D, rebuilt from profile 5 as HDR10 and SDR in BT.2020 and HDR10 mapped to SDR) |
| `playerinfo/` | `run-playerinfo.sh` | the player information and the language files: `make-playerinfo.py --check`, the skin's strings, variables and pages, that native Dolby Vision Low-Latency is never shown as TV-led, the generated dialog, and every text of the panel fitting its label or text box as Kodi measures it, in English and Spanish; the layout of the settings (every id of 1.1 kept, groups, order, levels, parents, the override files of the build, the strings of the layout) |
| `kodi-host/` | `run-kodi-host.sh` | Kodi's headers and logic built with the sanitizers: the output policy of Dolby Vision (with a video that has HDR10+ of its own too), the passes of the interface loop, windows loaded ahead, the end of the signal, the mode list, frames ahead, dual-mode connectors, the link depth, the connector reads, the audio outputs, the speakers of the player information through Kodi's data cache, seeks, the GL matrix stacks, Blu-ray 3D offsets and subtitle depth, the depth of subtitles and menus measured from a 3D video (also through the graphics, on Mesa's llvmpipe), the settings window, the 3D LUT of each display (`DisplayLut.h`), HDMI VRR, the setting of HDMI VRR, video at its own rate with VRR, the SDR copy of an HDR screenshot the peak it is mapped from, the frame of the video at its own size (where it comes from, the picture drawn again or read before Kodi's scaler value for value on Mesa's OpenGL ES 3, its SDR copy, and in the source how it is asked for and its strings), when the keys of the TV's remote and the commands of the TV are taken over CEC, a measure of Kodi's own timing of flips (its numbers are no verdict), and the chroma of the 4:2:0 pictures that Kodi draws itself: its siting and step-down, and Kodi's YUV shaders on a software GPU against a model of each filter, and the fixes taken from CoreELEC's forks (stage `cefix`: the IEC 61937 packers, DTS and channel layouts with Kodi's googletest files, the eye of a full frame of 3D, 3D file names, font collections with FreeType, and in the source what no host test can run); an SDR output in BT.2020 or DCI-P3 and the composite of the GUI into it (`sdrcontainer_gl`), the step-down of Dolby Vision rebuilt as HDR10 or SDR (`convstep_test`), the depth of a 3D video in HDR10 and HLG (`hdrdepth_gl`), and a 3D video and the Dolby Vision bridge (`stereo3d`: how a 3D video goes out, which 3D pictures the bridge draws, the views placed in the eye, SDR in a wide gamut in 3D, profile 20 in 3D); and the settings of films from add-ons kept by their ids, not by their links (stage `permemory`: Kodi's googletest file of the identities, the tracks found by what they are and what each release takes, and in the source where the player takes and keeps them, the table of the video database, the setting and its strings); and the messages of the audio engine and its device change events (stage `devchange`: Kodi's googletest files of the purges of its message queues, Kodi pull request 29331, of whether a change of the devices reopens the output and of the pace of the listings, and in the source the events queued behind one taken with it) |
| `fel/` | `run-fel.sh` | the profile 7 enhancement layer: the module of the layer, the MEL rule, the join of two tracks and a drain, on real dual-layer streams decoded in software |
| `ffmpeg/` | `run-ffmpeg.sh` | the FFmpeg patches of the image that change what FFmpeg reads: FATE on what they touch, and three patches against FFmpeg without them (the third, H.264 MVC in Matroska, with Kodi's `MultiviewChoice.h` deciding on what it gives) (the third, one variant of an HLS master playlist, with AddressSanitizer) |
| `boot/` | `run-boot.sh` | the installer's boot menus with the busybox of the image, on the menus of today and of earlier images; the notice when Kodi stops at every start (`kodi-failed-notice`); the theme of the boot menu in an update in place (`update.sh`), against the `update.sh` of `REGRESS_LE_OLD` too; what a start of the entry Installer leaves for modprobe (no driver of NVIDIA or of Bluetooth by its alias), against the init of `REGRESS_LE_OLD`; the pictures of `hades-picture` pixel for pixel against `ply-image` and the theme of GRUB, and what the start asks it to draw (`boot-picture`, `kodi-display-notice`, `kodi-failed-notice`); the wait of "Wait for network before starting Kodi" (`network-wait`) and what `clock-measure` keeps for it; the drivers that the journal of a start that hung shows starting and not returned (`not-returned.awk` of the diagnostics); that the driver of the Xonar sound cards waits for nvidia to start (`after-nvidia`); what may wake the PC from sleep (`wake`: hades-wake on a /sys of files, each choice of its setting, a device plugged in, after a sleep, and what each sleep then says of it) and that setting in the LibreELEC settings (`wakesettings`); and, in QEMU against an image (`picture_race_qemu.sh`, not a stage), Kodi's `hades-picture release` that comes while a picture of the start is being drawn |
| `bdj/` | `run-bdj.sh` | Hades' patches to the Java side of libbluray (BD-J) on the jar of the recipe, built as the image builds it (JDK 8): the configuration of the HAVi devices that matches a template best (`HaviBestTest.java`), and that the test fails without the patch that does it |
| `nvkms/` | `run-nvkms.sh` | Hades' patches to NVIDIA's open modules on the source of the recipe: what NVKMS tells RM of a head that can extend its vblank (`vrr_rm_test.c`), and that it does not build without the patch that adds it |
| `patches/` | `run-patches.sh` | what the patches of the tree say of themselves: every patch of Hades (in `packages/` and `projects/`) is signed by Hades' own identity, and a copy of one signed by a misspelt account is found (`identity_check.py`) |
| `oracle/` | `run-oracle.sh` | an independent oracle of the composer of ETSI GS CCM 001 against the bridge (its own README) |
| `mvc/` | `run-mvc.sh` | the separate MVC decoder of FFmpeg (its own README) |
| `mvhevc/` | `run-mvhevc.sh` | MV-HEVC 3D: FFmpeg's HEVC decoder on the ITU-T MV-HEVC conformance streams, what Kodi decides of a track of two views (also NVDEC and the sets of the second view in a Matroska track's packets), and its picture of the two (also from NVDEC's formats, and with a disparity adjustment), and a Dolby Vision profile 20 3D file made from a conformance stream (stage `p20`) (the header of the runner); `mvhevc_speed.c` measures the decoding speed (by hand: see `docs/how-it-works.md`) |
| `black/`, `chroma/`, `cropbench/`, `gui/`, `samba/` | their scripts | the black of Dolby Vision as HDR10, the chroma of the transports, the crop bench, a smoke test of the interface in QEMU, Samba's shares |

## How a runner works

The runners of `dvbridge`, `playerinfo`, `kodi-host`, `fel`, `ffmpeg`, `boot`, `nvkms`, `bdj` and `patches` source `lib/regress.sh`:

- `run-<group>.sh all` runs every stage in order into `$REGRESS_WORK/<group>/logs/run.log`; each check prints one line
  `REGRESS-ITEM <group>/<name>: PASSED|FAILED|NOT-RUN | <detail>`, and `lib/regress_verdict.py` counts them line by line
  (a malformed line, a name given twice or a stage that did not end is a failure). The runner exits 0 only when every
  item passed. `run-<group>.sh <stage>...` runs some stages; `run-<group>.sh verdict` reads a log again.
- **Both directions.** Every check has a mutant: a copy of the tree under test (or of libplacebo or FFmpeg) with one exact
  edit that takes out what the check protects, which must make it fail, with the number of failed checks measured when
  the test was written. A mutant whose text is no longer in the tree fails as such, so that it is written again for the
  new code.
- **Inputs by SHA256, never by a path.** The videos, pictures and logs a group reads are listed in
  `<folder>/inputs-<group>.tsv` (`sha256`, bytes, the name the runner uses); `rg_inputs` finds each one by its SHA256 in
  the folders of `REGRESS_INPUTS` (files named `<sha256>` or `<sha256>.<ext>`), checks its size and SHA256, and stops the
  stage when one is missing. Small references of Hades' own (hash lists, expected outputs) are in `<folder>/ref/`.
- **What is built is built from the recipes of the tree:** libplacebo (`rg_libplacebo`) and FFmpeg (`rg_ff_source`,
  `rg_ffmpeg`) from their archives, checked against `PKG_SHA256`, with every patch of the recipe applied with `-F0`, and
  Kodi's bridge (`rg_bridge`) with the GLES/EGL and Vulkan headers of a LibreELEC build folder. So a test follows an update
  of libplacebo or FFmpeg; an output that changes with one is explained before its reference is.

## Settings (environment)

| Variable | What it is |
| --- | --- |
| `REGRESS_WORK` | the work folder (required) |
| `REGRESS_KODI_GIT`, `REGRESS_KODI_REF` | the Kodi repository and the ref under test (default `HEAD`) |
| `REGRESS_LE_GIT`, `REGRESS_LE_REF` | the LibreELEC repository (default: the one that has this folder) and its ref (default `HEAD`): the recipes of libplacebo and FFmpeg, the installer |
| `REGRESS_LE_BUILD` | a LibreELEC build folder of that tree, read only: headers, the toolchain's glad, googletest, swig, busybox |
| `REGRESS_SOURCES` | folders that may hold `<package>/<archive>` of the recipes |
| `REGRESS_LE_OLD` | for `boot`: the ref of the version updated from (default `v1.0`): its theme of the boot menu and its `update.sh` |
| `REGRESS_INPUTS` | folders of the inputs, named by their SHA256 (required by the groups that have inputs; a folder of hard links, one per input, named by SHA256 and extension, is enough) |
| `REGRESS_JOBS` | compile jobs (default 8) |
| `REGRESS_LANES`, `REGRESS_MEM` | for `run-regress.sh`: groups at once (default 3) and the memory scope of each (default 5G) |

## Running every group

```
run-regress.sh                 # every group once, the longest first, REGRESS_LANES at once
run-regress.sh kodi-host fel   # some groups
```

Each group runs in a memory scope of its own (`systemd-run --user --scope`, `MemoryMax=$REGRESS_MEM`, no swap) at the
lowest priority. `$REGRESS_WORK/summary.txt` has one line per group (its verdict, items, seconds and the peak memory of its
scope) and `REGRESS-ALL PASSED|FAILED`.

What one run of every group cost on the build host (32 threads shared with other work, load 20 to 40; `REGRESS_JOBS=8`,
three lanes of 5G): 57 minutes in all, bounded by `dvbridge-render`; one after the other they take 144 minutes. The
memory is that of the group's scope: its peak (with the page cache of the inputs it reads, which the cap reclaims) and the
largest anonymous memory sampled every 5 s (what the programs hold).

| Group | Items | Seconds | Scope peak | Anonymous |
| --- | ---: | ---: | ---: | ---: |
| `dvbridge-render` | 66 | 3396 | 4545 MiB | 2714 MiB |
| `dvbridge-meta` | 211 | 1808 | 3239 MiB | 1620 MiB |
| `fel` | 46 | 1383 | 5120 MiB | 3746 MiB |
| `kodi-host` | 236 | 1064 | 2954 MiB | 1185 MiB |
| `ffmpeg` | 23 | 902 | 5120 MiB | 3391 MiB |
| `boot` | 29 | 47 | 22 MiB | 3 MiB |
| `playerinfo` | 17 | 14 | 172 MiB | 25 MiB |

The inputs of all groups are 686 files, 4.15 GB (inputs-*.tsv).
