# tools/regress

Host regressions of LibreELEC Hades: what Hades changed in Kodi (its Dolby Vision bridge, `tools/dvbridge`, and the
headers and logic of its output), in FFmpeg, in libplacebo and in the installer, each checked on the build host against
the trees of a version, without the image and without a display. Nothing here goes into an image. A runner reads the
trees it tests (by `git archive`) and never writes into them, into a build folder or into its inputs.

## Groups

| Folder | Runner(s) | What it checks |
| --- | --- | --- |
| `dvbridge/` | `run-dvbridge-meta.sh` | what the bridge sends and measures: the metadata serializer, HDR10+ (the InfoFrame, what the display is sent, HDR10+ sent as Dolby Vision), the source transport rows, the pack, frames drawn with things over them, the pass times, the CCM 001 reader and its mutations, the kernel's HDR10+ helpers, the graphics metadata and menus, the mapping to the display |
| `dvbridge/` | `run-dvbridge-render.sh` | what the bridge draws, byte for byte, on a software GPU: the fidelity bench, the measured test, the render test, the step-down levels, the gamut table, the GUI step and cache, the geometry of a view, the composed planes, the composer with a BT.1886 base layer and with profile 8.4, the IPT tunnel |
| `playerinfo/` | `run-playerinfo.sh` | the player information and the language files: `make-playerinfo.py --check`, the skin's strings, variables and pages, the generated dialog, and every text of the panel fitting its label or text box as Kodi measures it, in English and Spanish |
| `kodi-host/` | `run-kodi-host.sh` | Kodi's headers and logic built with the sanitizers: the output policy of Dolby Vision (with a video that has HDR10+ of its own too), the passes of the interface loop, windows loaded ahead, the end of the signal, the mode list, frames ahead, dual-mode connectors, the link depth, the connector reads, the audio outputs, the speakers of the player information through Kodi's data cache, seeks, the GL matrix stacks, Blu-ray 3D offsets and subtitle depth, the settings window, HDMI VRR, the setting of HDMI VRR, video at its own rate with VRR, and a measure of Kodi's own timing of flips (its numbers are no verdict) |
| `fel/` | `run-fel.sh` | the profile 7 enhancement layer: the module of the layer, the MEL rule, the join of two tracks and a drain, on real dual-layer streams decoded in software |
| `ffmpeg/` | `run-ffmpeg.sh` | the FFmpeg patches of the image that change what FFmpeg reads: FATE on what they touch, and two patches against FFmpeg without them |
| `boot/` | `run-boot.sh` | the installer's boot menus with the busybox of the image, on the menus of today and of earlier images; the notice when Kodi stops at every start (`kodi-failed-notice`); the theme of the boot menu in an update in place (`update.sh`), against the `update.sh` of `REGRESS_LE_OLD` too; what a start of the entry Installer leaves for modprobe (no driver of NVIDIA or of Bluetooth by its alias), against the init of `REGRESS_LE_OLD`; the pictures of `hades-picture` pixel for pixel against `ply-image` and the theme of GRUB, and what the start asks it to draw (`boot-picture`, `kodi-display-notice`, `kodi-failed-notice`); the wait of "Wait for network before starting Kodi" (`network-wait`) and what `clock-measure` keeps for it |
| `nvkms/` | `run-nvkms.sh` | Hades' patches to NVIDIA's open modules on the source of the recipe: what NVKMS tells RM of a head that can extend its vblank (`vrr_rm_test.c`), and that it does not build without the patch that adds it |
| `oracle/` | `run-oracle.sh` | an independent oracle of the composer of ETSI GS CCM 001 against the bridge (its own README) |
| `mvc/` | `run-mvc.sh` | the separate MVC decoder of FFmpeg (its own README) |
| `black/`, `chroma/`, `cropbench/`, `gui/`, `samba/` | their scripts | the black of Dolby Vision as HDR10, the chroma of the transports, the crop bench, a smoke test of the interface in QEMU, Samba's shares |

## How a runner works

The runners of `dvbridge`, `playerinfo`, `kodi-host`, `fel`, `ffmpeg`, `boot` and `nvkms` source `lib/regress.sh`:

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
