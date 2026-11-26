#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-kodi-host.sh: the host tests of Kodi's headers and logic that Hades changed, each against the Kodi tree of
# REGRESS_KODI_REF (lib/regress.sh: rg_kodi), built with g++ and AddressSanitizer/UndefinedBehaviorSanitizer (and
# ThreadSanitizer where threads take part), and each in both directions: it passes on the tree, and every mutant of
# mutants.py that names it (a copy of the tree with one rule of the code changed, made by mutate.py) makes it fail
# with the number of failed checks written there. A few tests carry a model of the rule before inside themselves
# (-DOLD_RULE and the like): those runs must fail too.
#   existing  settings_resize_test.cpp and vrr_test.cpp (the settings window when the mode changes; HDMI VRR left
#             off with native Dolby Vision), vrr_gui_test.cpp (the setting "HDMI variable refresh rate (VRR)":
#             HdmiVrrOff.h), vrr_video_test.cpp ("Video at its own rate with VRR": VrrVideo.h, with the rule of 1.1
#             for which flips are paced, which must fail, and its mutants) and
#             display_modes_test.cpp (the mode of the menus of each display: DisplayModeMemory.h),
#             display_lut_test.cpp (the 3D LUT of each display: which outputs take one, a LUT made for another kind
#             of picture refused, one display's LUT never another's: DisplayLut.h), display_lut_loader_test.cpp (its
#             files read off the render thread, one at a time, the table of before let go first, a read no longer
#             asked for never given: DisplayLutLoader.h, with address and undefined, and with thread sanitizers), and
#             in the source what no host test runs (lut_src_check.py: no file read on the render thread, the bridges
#             let go first, Kodi's renderer putting the table on the GPU a part a frame with no copy, "loading"),
#             exact_rate_test.cpp (a mode at the video's rate at another size of the display, without a whitelist
#             of the user: ExactRateElsewhere.h), video_size_test.cpp ("Adjust display resolution": the mode of the
#             video's size at the refresh rate Kodi chose, VideoSizeMode.h), content_type_test.cpp (the content
#             type of the AVI InfoFrame and the Filmmaker Mode it brings: ContentType.h, and its place in the transport
#             of native Dolby Vision, TransportSpecs.h; with the rule before, Kodi never setting it, which must fail),
#             picture_timing_test.cpp (HEVC with frame_field_info_present_flag
#             progressive by the picture timing of its first access units: ProgressiveVideo.cpp, with the FFmpeg
#             headers of the build folder),
#             vrr_pacer_bench.cpp (how exact Kodi's own timing of flips is on this host: a measure, its numbers
#             in the item; the item says only that it built and ran, the numbers are no verdict)
#   policy    the choice of output of a Dolby Vision video (DVPlaybackPolicy.h), what a Dolby Vision video with
#             HDR10+ of its own is sent with (dualhdr_test.cpp), the passes of the interface loop (VideoPasses.h),
#             and the strings of the projector settings in Estuary
#   window    the windows loaded ahead (WindowPreload.h), the frame that ends the Dolby Vision signal
#             (DVEndOfStream.h; dvbridge_eos.h on the software GPU)
#   modes     the modes of the display read again (ModeListRefresh.h), the resolutions replaced
#             (ResolutionListReplace.h)
#   output    the output of native Dolby Vision: frames ahead (DVAhead.h, with the 10 s windows of real logs, an
#             input), dual-mode connectors, the frame tests, the wake, the pictures kept, the lag, the
#             presentation fixes of 1.0 RC3, the link set up again after a hot-plug (HotplugRelink.h), and the
#             change of mode from TMDS to HDMI FRL with the way back from its hang (FrlModeChange.h), and a link that
#             the kernel marks Bad set up again by a commit of link-status Good alone, the output as it is
#             (linkbad_test.cpp: drm/LinkStatus.h against a model of the kernel's atomic commit; linkbad_src.py: its
#             callers, CDRMAtomic::DrmAtomicCommit() and CWinSystemGbm::LookAtLinkStatus())
#   linkdepth the depth of the link (LinkDepth.h) and what in the sources writes "max bpc" (src_check.py)
#   fix       a render that libplacebo failed (dvbridge_render.c on the software GPU, a real profile 7 stream as
#             input), the reads of the connector (DRMConnector.cpp), what a replacement retires (RetiredKept.h),
#             the callers of both, the list of the audio outputs (ActiveAESink.cpp), and in the source what kodi.log
#             says of a power action and of where it came from (power_src_check.py: a Standby of CEC with its setting,
#             the builtins, JSON-RPC, the idle timer, the shutdown function, the action carried out or refused by PVR),
#             and a drain of the video decoder that keeps the picture the renderer had no room for
#             (decoderdrain_test.cpp: DecoderDrain.h, and in the source that VideoPlayerVideo drains only through it),
#             and why an HLG video stays HLG where HLG as Dolby Vision cannot take it (hlgreason_test.cpp:
#             HlgDolbyVisionIneligibility() against HlgDolbyVisionEligible(), DVPlaybackPolicy.h)
#   own       the trace of a seek (SeekTrace.h, SeekPending.h, ModeChangeTime.h), a slow source while the player
#             buffers (SourceRate.h), the plan of the speakers (SpeakerPlan.h, on real and made EDIDs), the module xbmcaddon through the
#             build's swig, pwr() of hades-diag's watch-dv.sh with the image's busybox, and the plane of the speakers
#             of the player information through Kodi's data cache and the audio engine's clear of it (speakers_test.cpp:
#             HadesPlayerInfo.h with DataCacheCore.cpp and AEChannelInfo.cpp), and the use of an amdgpu GPU from the
#             time of its graphics engine in the fdinfo of Kodi's DRM files (gpusensors_test.cpp: GPUSensors.h)
#   ux        the depth of a scrolling label, Kodi's TestActiveAEFollowDisplay with googletest, the Bluetooth logic
#             of the LibreELEC settings add-on, its A/B comparison of native Dolby Vision and its frame packing card
#             (LibreELEC-settings-9073, 9074, with stand-ins for Kodi), how often the player information is processed
#             (PlayerInfoRefresh.h; and in the source, info_refresh_src.py: every frame while a control of it animates,
#             in both dialogs), the keys that open it while a video plays (keymap_check.py: a long press of Menu, and
#             the short press of each key as before, in Kodi's default keymaps), the sentence of its overview and the
#             frames that Kodi loses (verdict_test.cpp: CLostFrames of HadesPlayerInfo.h, only sustained loss in
#             steady playback is said, with its cause; lost_frames_src.py: what the window system samples and when it
#             starts again; check_verdict_test.py: the sentence as Check this setup says it, LibreELEC-settings-9087,
#             and the same with 9087 reversed, which must fail), the settings that change the picture
#             from the film (PictureDefaults.h),
#             subtitles in the black bars (SubtitleBars.h; its measurement, SubtitleBarsGLES.cpp, on the host's Mesa
#             llvmpipe through EGL surfaceless, with the GLES headers of the build folder)
#   overlay   the GL matrix stacks: no function of Kodi pops a level that it did not push (overlay/matrix_balance.py)
#   chroma    the chroma of the 4:2:0 pictures that Kodi's GLES renderer draws itself: the siting and the
#             step-down of its filter (chroma_step_test.cpp: ChromaStepDown.h), and Kodi's YUV shaders of the tree
#             on the software GPU against a model of each way of taking the chroma, in the four layouts of the
#             renderer, with the basic and the filter shader (chroma_shader_test.c); and the dither of the bridge's
#             copy into an 8-bit window (copy_dither_test.c: tools/dvbridge/dvbridge_gl_pack.c on the software GPU);
#             the step-down by missed refreshes where the GPU has no usable timer, the flips of the commits that tell
#             them (chroma_step_test.cpp: CFlipChain), and in the source the bits of the GPU's timestamps, the choice
#             of the measure, the pictures told and what kodi.log says (chroma_src_check.py); and GL_GPU_DISJOINT_EXT
#             read once a frame and told to every reader of GPU times (gpu_disjoint_test.cpp: GpuDisjoint.h), read in
#             one place of the source with a reader of its own for each (disjoint_src_check.py)
#   stereo    Blu-ray 3D: the plane offsets (Kodi's TestBlurayOffsetMetadata with googletest; on the access units
#             of a real MVC stream, an input) and the depth of a subtitle; the depth measured from a 3D
#             video's two pictures (StereoDepth.cpp: pictures of known depth, the planner, five seconds of a film
#             through SubtitleDepth(), and the rule before, which must fail), and the same through the graphics
#             (StereoDepthCapture.cpp and StereoDepthMeter.cpp on Mesa's software OpenGL ES 3, EGL without a window:
#             the host's libEGL.so.1 and libGLESv2.so.2, the build folder's GLES and EGL headers); an HDR video in a
#             3D output through the graphics (hdr3d_gl_test.cpp: StereoViews.h, WholeBuffer.h and StereoGap.h on the
#             same OpenGL ES 3, frames of Kodi in frame packing, side by side, top and bottom, 2D of a 3D video and
#             anaglyph: each eye its own half of the picture under the GUI composited over the whole frame; the rule
#             before, which must fail; an SDR video in 3D and an HDR video in 2D the same bytes as before; the callers);
#             and the depth measured from a 3D video in HDR10 and HLG (hdrdepth_gl_test.cpp: the pictures of known depth
#             in PQ and HLG, at full light and a quarter of it, in a framebuffer of 10 bits, against their SDR copies;
#             the caller in RenderManager.cpp); a 3D video and the Dolby Vision bridge (stereo3d_test.cpp: Stereo3DVideo.h,
#             how a 3D video goes out, which 3D pictures the bridge draws and that the default draws none it did not
#             before, the views cut at the seam and placed in the eye, their timing, SDR in a wide gamut in 3D,
#             profile 20 in 3D, and the callers; the rules before, which must fail); the frame of HDMI 3D frame
#             packing on nvidia-drm in buffers that Kodi allocates itself where the driver refuses the window's
#             (issue #33; fpown_gl_test.cpp: FramePackedOwn.h, OwnScanout.cpp and StereoGap.h on the same OpenGL ES
#             3, with the build folder's gbm.h and drm_fourcc.h: when they are made, which buffers go out, the lines
#             of 1080p and 720p frame packing, the checks of 8 bits and of 10 as before, and a frame of Kodi flipped
#             by the GPU into the buffer that goes to the screen: the left eye, 45 black lines, the right eye; and
#             fpown_src.py: in the source, where they are made, asked, kept and taken away, and that the driver is
#             only asked in a test commit); the GUI's FBO of the HDR composite in a 3D output over several frames
#             (issue #34; guifbo_gl_test.cpp: WholeBuffer.h's ClearWhole(), StereoViews.h and StereoGap.h on the same
#             OpenGL ES 3: the menus of before a video and the subtitles before never left in the right eye, its GUI
#             the left eye's at the other eye's depth, also for an FBO made in a frame whose GUI pass is left out; the
#             rule before, which must fail; 2D the same bytes; the caller) and a 3D video gone back to 2D and the
#             bridge (issue #34; fallback2d_test.cpp: Stereo3DVideo.h's IsStereo() with Kodi's RenderFlags.cpp: the base
#             view alone decided as the picture in 2D from the start, every reader of a picture's stereo mode on its
#             way to the bridge; the rule before, which must fail); the driver's dumb buffers of a frame-packed mode
#             of nvidia-drm after GBM's, and the copy of the processor into them (fpown_gl_test.cpp, fpown_src.py);
#             1.2.2 (pc3d_test.cpp: the pixel ratio that libass is given in a 3D output, the glyphs of their shape
#             side by side and top and bottom, the rule before failing; the Atmos badge; holding Start on a game
#             controller; a 3D Dolby Vision video in the player information)
#   capture   the SDR copy of an HDR screenshot and the peak it is mapped from (Kodi's TestCaptureConvert with
#             googletest: CaptureConvert.cpp with libswscale and libavutil of the FFmpeg of the recipe, built for the
#             host as the bridge's is, rg_ff_bridge), and when the keys of the TV's remote and the commands of the TV
#             are taken (TestPeripheralCecKeys and TestPeripheralCecPicture: PeripheralCecKeys.h with the libcec
#             headers of the build folder's sysroot); the frame of the video at its own size (issue #30): where it
#             comes from (Kodi's TestVideoFrameCapture with googletest: VideoFrameCapture.h), the picture drawn again
#             at its own size and the one before Kodi's scaler read value for value on Mesa's OpenGL ES 3, at 8 and 10
#             bits and in half floats, the state of GL put back, the SDR copy of 1.2 made from a 10-bit PQ frame
#             (videoframe_gl_test.cpp: CaptureOwnSize.cpp, CaptureReadback.cpp and CaptureConvert.cpp), and in the
#             source which picture the renderer gives, where it is served, how it is asked for and its strings
#             (videoframe_src.py)
#   setup     the display and its connection: whether an EDID comes through a sound bar or AV receiver, and its sound
#             (edidpath_test.cpp: EdidHdmiPath.h, on an LG C5 read both ways, AV receivers and TVs; edid_path_diag_test.sh:
#             hades-diag's edid-path.awk with the image's busybox); the defaults of the file cache from the RAM and from
#             the <cache> of advancedsettings.xml (cachedefaults_test.cpp: FileCacheDefaults.h, and the rule of 1.1, which
#             must fail); after an update, Dolby Vision starting from what the last build found with the same GPU
#             (dvpathprior_test.cpp: DVPathChoice.h, and the rule of 1.1, which must fail); "Your display" and "Share my
#             results" of the LibreELEC settings add-on (check_display_test.py, LibreELEC-settings-9085 with stand-ins for
#             Kodi, and the same with 9085 reversed, which must fail); and LibreELEC-settings-9086 (a setting that the
#             A/B comparison or the frame packing card could not put back, the second journal of "Your display"): the
#             tests of 9085, 9073 and 9074 with 9086 reversed, each of which must fail
#   projector the network control of a home-cinema projector (xbmc/network): what it asks of the projector, the
#             shape of a picture and the addresses of a home network (projector_policy_test.cpp: ProjectorPolicy.h,
#             ProjectorFeed.cpp, ProjectorLink.cpp), and the protocols of JVC D-ILA, Sony ADCP and SDCP and Epson
#             ESC/VP.net against a simulator of each on 127.0.0.1 (projector_link_test.py driving projector_cli.cpp
#             built with ProjectorLink.cpp), and in its source that its notices are made of translated strings
#             (projector_src_check.py: ProjectorControl.cpp)
#   cefix     the fixes taken from CoreELEC's forks (CREDITS.md names them): Kodi's TestAEPackIEC61937,
#             TestAEStreamParser and TestAEChannelLayout with googletest, built as a release build is (NDEBUG) with
#             the sanitizers (the IEC 61937 packers, a DTS header without a channel arrangement, the top-back channels
#             of a decoded layout), TestStereoEyeSize (one eye of a full frame of 3D), the 3D patterns of file names
#             (cefix/stereo_names.py), the family names of font collections with a face that does not load or has no
#             name, with FreeType (cefix/font_gen.py, font_test.cpp), and in the source what no host test can run
#             (cefix/src_check.py: curl, the player's thread, SMB while the system sleeps, a thread's end)
#   permemory the settings of films and episodes from add-ons kept by their ids, not by their links (FilmMemory.h): Kodi's
#             TestFilmMemory with googletest and the sanitizers (identities that never mix two films, tracks found by
#             what they are and never by their number, what each release takes: the delays only from the same release,
#             the zoom from a picture of the same shape, the picture settings from a video of the same format, a text of
#             another version), and in the source what no host test runs (permemory/src_check.py: where the player takes
#             the settings and keeps them, which items keep them by their ids, the table of the video database, the
#             setting and its strings)
#   devchange the messages of the audio engine and its device change events: Kodi's TestActorProtocol with googletest
#             (the messages that a purge takes out of a queue are given back, counted and shown to the caller: Kodi
#             pull request 29331), TestActiveAEDeviceChange (whether a change of the devices reopens the output) and
#             TestActiveAEDeviceChangePace (the bound of a second of the listings, the events kept while the engine
#             reconfigures), and in the source the change of the devices of a driver or of the default device that
#             takes the events of both kinds queued behind it and lists the outputs once for them (devchange/src_check.py)
# usage: run-kodi-host.sh all | verdict [<log>] | <stage>...      (settings: lib/regress.sh)
# Needs REGRESS_KODI_GIT, REGRESS_LE_BUILD (googletest, swig, libdrm and GLES headers, busybox), REGRESS_SOURCES
# (libplacebo, FFmpeg, LibreELEC-settings) and REGRESS_INPUTS (inputs-kodi-host.tsv).
# The windows of output/dvahead_test.cpp: one row per "last 10 s" line of DVBridge in a kodi.log: the set of logs,
# the log, GL_RENDERER, the native output it is in, the time, "last", frames shown, kept, not shown, with GUI
# elements, prepare avg/max, present avg/max, the longest gap, presentations over 1.5 refresh periods, frames ahead,
# flips known, longest flip gap, flips late, GPU avg/max, frames timed, frames standing still (tab separated). The
# runner gives the set as the first 12 hexadecimal digits of the SHA256 of its name.
set -uo pipefail
. "$(dirname "$0")/../lib/regress.sh"
rg_init kodi-host
H=$RG_REGRESS/kodi-host
MUT="$RG_PY $H/mutate.py"
CXX="g++ -std=c++20 -O1 -g -Wall -Wextra"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
STAGES="existing policy window modes output linkdepth fix own ux overlay stereo capture chroma cefix setup projector permemory devchange"
SWIG_HADES_LINES=66 # the lines of the generated xbmcaddon module with the text of Hades' typemap
IN=$RG_W/in
VP=xbmc/cores/VideoPlayer/DVDCodecs/Video
GB=xbmc/windowing/gbm

kodi() { rg_kodi || rg_fail "no Kodi tree of $REGRESS_KODI_REF"; }
inputs() { rg_inputs "$H/inputs-kodi-host.tsv" "$IN" > "$RG_W/logs/inputs.log" 2>&1; local e=$?; cat "$RG_W/logs/inputs.log"; return $e; }
# the libdrm headers of the build folder's sysroot, in a folder of their own
drminc() {
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/drminc
  [ -f "$s/xf86drmMode.h" ] && [ -d "$s/libdrm" ] || { echo "no libdrm headers in $s" >&2; return 1; }
  mkdir -p "$d" && ln -sfn "$s/xf86drm.h" "$d/xf86drm.h" && ln -sfn "$s/xf86drmMode.h" "$d/xf86drmMode.h" && ln -sfn "$s/libdrm" "$d/libdrm"
  echo "$d"
}
# the GLES 3 and EGL headers of the build folder's sysroot, in a folder of their own (made once)
glesinc() {
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/glesinc x
  [ -f "$s/GLES3/gl3.h" ] && [ -f "$s/EGL/egl.h" ] && [ -d "$s/KHR" ] || { echo "no GLES 3 or EGL headers in $s" >&2; return 1; }
  mkdir -p "$d" && for x in GLES2 GLES3 EGL KHR; do
    [ "$(readlink "$d/$x")" = "$s/$x" ] || ln -sfn "$s/$x" "$d/$x" || return 1
  done
  echo "$d"
}
# gbm.h and libdrm's drm_fourcc.h of the build folder's sysroot, in a folder of their own (made once; issue #33)
fpinc() {
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/fpinc
  [ -f "$s/gbm.h" ] && [ -f "$s/libdrm/drm_fourcc.h" ] || { echo "no gbm.h or drm_fourcc.h in $s" >&2; return 1; }
  mkdir -p "$d" || return 1
  [ "$(readlink "$d/gbm.h")" = "$s/gbm.h" ] || ln -sfn "$s/gbm.h" "$d/gbm.h" || return 1
  [ "$(readlink "$d/libdrm")" = "$s/libdrm" ] || ln -sfn "$s/libdrm" "$d/libdrm" || return 1
  # (xf86drm.h: OwnScanout.cpp makes the driver's dumb buffers)
  [ -f "$s/xf86drm.h" ] || { echo "no xf86drm.h in $s" >&2; return 1; }
  [ "$(readlink "$d/xf86drm.h")" = "$s/xf86drm.h" ] || ln -sfn "$s/xf86drm.h" "$d/xf86drm.h" || return 1
  echo "$d"
}
# the headers of FFmpeg of the build folder's sysroot (libavcodec, libavutil), in a folder of their own
ffinc() {
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/ffinc
  [ -f "$s/libavcodec/codec_id.h" ] && [ -f "$s/libavutil/pixdesc.h" ] || { echo "no FFmpeg headers in $s" >&2; return 1; }
  # (made once: the mutants build at once, and a link made again is missing for a moment)
  local x
  mkdir -p "$d" && for x in libavcodec libavutil; do
    [ "$(readlink "$d/$x")" = "$s/$x" ] || ln -sfn "$s/$x" "$d/$x" || return 1
  done
  echo "$d"
}
# the libcec headers of the build folder's sysroot, in a folder of their own
cecinc() {
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/cecinc
  [ -f "$s/libcec/cectypes.h" ] || { echo "no libcec headers in $s" >&2; return 1; }
  mkdir -p "$d" && { [ "$(readlink "$d/libcec")" = "$s/libcec" ] || ln -sfn "$s/libcec" "$d/libcec"; } && echo "$d"
}
# googletest of the build folder (libfmt's copy), built once
gtest() {
  local gt d=$RG_SHARED/gtest
  gt=$(ls -d "$REGRESS_LE_BUILD"/build/libfmt-*/test/gtest 2> /dev/null | sort | tail -n 1)
  [ -f "$gt/gmock-gtest-all.cc" ] || { echo "no googletest in $REGRESS_LE_BUILD" >&2; return 1; }
  if [ ! -f "$d/gtest-all.o" ] || [ "$(cat "$d/from" 2> /dev/null)" != "$gt" ]; then
    rg_fresh "$d"
    printf '#include <gtest/gtest.h>\nint main(int argc, char** argv)\n{\n  testing::InitGoogleTest(&argc, argv);\n  return RUN_ALL_TESTS();\n}\n' > "$d/main.cpp"
    g++ -std=c++20 -O1 -pthread -w -I"$gt" -c "$gt/gmock-gtest-all.cc" -o "$d/gtest-all.o" > "$d/build.log" 2>&1 &&
      g++ -std=c++20 -O1 -pthread -I"$gt" -c "$d/main.cpp" -o "$d/main.o" >> "$d/build.log" 2>&1 || { cat "$d/build.log" >&2; return 1; }
    echo "$gt" > "$d/from"
  fi
  echo "$gt"
}

# ------------------------------------------------------------------------------------------------- the tests
# build <test> <tree> <dir>: the test against a tree into <dir> (<dir>/t and <dir>/build.log); WERR=-Werror for the
# tree as it is (a mutant may leave a parameter unused)
build() {
  local t=$1 K=$2 D=$3 X=$2/xbmc S=$H
  mkdir -p "$D"
  {
    case $t in
      p5always) $CXX $SAN -I $K/$VP -I $X $S/policy/p5always_test.cpp -o $D/t ;;
      policy) $CXX ${WERR:-} -I $K/$VP $S/policy/policy_test.cpp -o $D/t ;;
      videopasses_missed|videopasses) $CXX ${WERR:-} $SAN -I $X/application $S/policy/${t}_test.cpp -o $D/t ;;
      dvconvert|gap_policy|dualhdr) $CXX ${WERR:-} $SAN -I $K/$VP $S/policy/${t}_test.cpp -o $D/t ;;
      reachable) $CXX ${WERR:-} $SAN -I $K/$VP $S/policy/reachable_test.cpp -o $D/t ;;
      reachable-old-rule) $CXX $SAN -DOLD_RULE -I $K/$VP $S/policy/reachable_test.cpp -o $D/t ;;
      anyhz) $CXX ${WERR:-} -Wshadow $SAN -I $X $S/policy/anyhz_test.cpp -o $D/t ;;
      strings) cp $S/policy/projector_strings.py $D/t ;;
      widths) cp $S/policy/measure_es.py $D/t ;;
      windowpreload) $CXX ${WERR:-} $SAN -I $X/guilib $S/window/windowpreload_test.cpp -o $D/t ;;
      dveos) $CXX ${WERR:-} $SAN -I $K/$GB $S/window/dveos_test.cpp -o $D/t ;;
      eos) RG_BRIDGE_SKIP="dvbridge_fel dvbridge_dmabuf" EXTRA_CF="$SAN" LDEXTRA="$SAN" \
             rg_bridge "$K" "$LP" "$FF" $S/window/eos_test.c $D/t || { cat $D/t.build.log; false; } ;;
      modelist) $CXX ${WERR:-} -Wshadow $SAN -I $X -I $K/$GB $S/modes/modelist_test.cpp -o $D/t ;;
      reslist-address|reslist-thread) $CXX ${WERR:-} -Wshadow -pthread -fsanitize=${t#reslist-} -I $X $S/modes/reslist_test.cpp -o $D/t ;;
      dvahead|dvrepeatkeep|presentlag) $CXX ${WERR:-} -Wshadow $SAN -I $K/$GB $S/output/${t}_test.cpp -o $D/t ;;
      dualmode|dualmode-table) $CXX ${WERR:-} -Wshadow $SAN -I $K/$GB -I $K/$GB/drm $S/output/dualmode_test.cpp -o $D/t ;;
      dvframetest|drmwake) $CXX ${WERR:-} -Wshadow $SAN -I $K/$GB/drm $S/output/${t}_test.cpp -o $D/t ;;
      dvframetest-old-rule|presentlag-old-rule) $CXX $SAN -DOLD_RULE $S/output/${t%-old-rule}_test.cpp -o $D/t ;;
      # (Hades: a 3D video and the bridge, Stereo3DVideo.h; -DOLD_RULE the rules before, which must fail)
      stereo3d|stereo3d-old-rule) ff=$(ffinc) && $CXX $([ $t = stereo3d ] && echo "${WERR:-}" || echo -DOLD_RULE) -Wshadow \
                         $SAN -DKODI_TREE="\"$K\"" -I $X -isystem $ff $S/stereo/stereo3d_test.cpp -o $D/t ;;
      sdrcontainer_gl) gles=$(glesinc) && ff=$(ffinc) && $CXX ${WERR:-} -Wshadow $SAN -DKODI_TREE="\"$K\"" -I $X \
                         -isystem $gles -isystem $ff $S/output/sdrcontainer_gl_test.cpp \
                         /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      linkbad_src) cp $S/output/presfix/linkbad_src.py $D/t ;;
      *_test) $CXX ${WERR:-} -Wshadow $SAN -I $X -I $K/$GB -I $S/output/presfix $S/output/presfix/$t.cpp -o $D/t ;;
      linkdepth) $CXX ${WERR:-} -Wshadow $SAN -I $X $S/linkdepth/linkdepth_test.cpp -o $D/t ;;
      src_check) cp $S/linkdepth/src_check.py $D/t ;;
      render_refused) RG_BRIDGE_SKIP="dvbridge_fel dvbridge_dmabuf" EXTRA_CF="$SAN" \
                        LDEXTRA="$SAN -Wl,--wrap=pl_renderer_get_errors" \
                        rg_bridge "$K" "$LP" "$FF" $S/fix/render_refused_test.c $D/t || { cat $D/t.build.log; false; } ;;
      drmconnector-*) [ -x $D/conn ] || $CXX $SAN -fno-sanitize-recover=all -DHAVE_FORGET -I $S/fix/stubs -I $K/$GB/drm -I $X \
                        -isystem $DRM -isystem $DRM/libdrm $K/$GB/drm/DRMConnector.cpp $K/$GB/drm/DRMObject.cpp \
                        $S/fix/drmconnector_test.cpp -o $D/conn ;;
      retiredkept) $CXX ${WERR:-} -Wshadow $SAN -fno-sanitize-recover=all -I $X $S/fix/retiredkept_test.cpp -o $D/t ;;
      eofpresentation) $CXX ${WERR:-} -Wshadow $SAN -fno-sanitize-recover=all -I $X/cores/VideoPlayer $S/fix/eofpresentation_test.cpp -o $D/t ;;
      hlgreason) $CXX ${WERR:-} -Wshadow $SAN -fno-sanitize-recover=all -I $X $S/fix/hlgreason_test.cpp -o $D/t ;;
      decoderdrain) $CXX ${WERR:-} -Wshadow $SAN -fno-sanitize-recover=all -DKODI_TREE="\"$K\"" -I $X/cores/VideoPlayer \
                      $S/fix/decoderdrain_test.cpp -o $D/t ;;
      cecline) $CXX ${WERR:-} -Wshadow $SAN -fno-sanitize-recover=all -I $X/peripherals/devices $S/fix/cecline_test.cpp -o $D/t ;;
      power_src) cp $S/fix/power_src_check.py $D/t ;;
      pieces) $RG_PY $S/fix/pieces_gen.py $K $D/gen && $CXX $SAN -fno-sanitize-recover=all -I $D/gen -I $S/fix/stubs \
                -I $K/$GB/drm -I $X -isystem $DRM -isystem $DRM/libdrm $K/$GB/drm/DRMConnector.cpp $K/$GB/drm/DRMObject.cpp \
                $S/fix/pieces_test.cpp -o $D/t ;;
      aesink-*) [ -x $D/aesink-address ] || {
                  $RG_PY $S/fix/aesink_gen.py $K $D/gen &&
                  for san in address thread; do
                    f="-fsanitize=$san"; [ $san = address ] && f=$SAN
                    $CXX -pthread $f -I $D/gen -I $X -I $X/platform/posix -isystem $FF/include $S/fix/aesink_test.cpp \
                      $X/cores/AudioEngine/Utils/AEDeviceInfo.cpp $X/cores/AudioEngine/Utils/AEChannelInfo.cpp -o $D/aesink-$san || return 1
                  done; } ;;
      seek) $CXX ${WERR:-} $SAN -DKODI_TREE="\"$K\"" -I $X/cores/VideoPlayer -I $X/utils $S/own/seek_test.cpp \
              -o $D/t -lpthread ;;
      # (Kodi's own files built as they are: not with -Werror, which their warnings would stop)
      speakers) $CXX $SAN -pthread -DTARGET_POSIX -DTARGET_LINUX -I $S/own/speakers_stub -I $X $X/cores/DataCacheCore.cpp \
                  $X/platform/posix/threads/RecursiveMutex.cpp $X/cores/AudioEngine/Utils/AEChannelInfo.cpp \
                  $S/own/speakers_test.cpp -o $D/t ;;
      # (Hades: the plan of the speakers, with the host's libswresample of the build folder)
      speakerplan) f=$(ls -d $REGRESS_LE_BUILD/build/ffmpeg-[0-9]*/.x86_64-linux-gnu | head -n 1) &&
                     $CXX ${WERR:-} $SAN -I $X -I $f -I $f/.. $S/own/speaker_plan_test.cpp \
                       $f/libswresample/libswresample.a $f/libavutil/libavutil.a -lm -lpthread -o $D/t ;;
      swig) cp $S/own/swig_check.py $D/t ;;
      gpusensors) $CXX ${WERR:-} -Wshadow $SAN -pthread -I $X/windowing/gbm $S/own/gpusensors_test.cpp -o $D/t ;;
      fadelabel) mkdir -p $D/src $D/stub/guilib/guiinfo $D/stub/utils $D/stub/windowing &&
                   cp $X/guilib/GUIFadeLabelControl.cpp $X/guilib/GUIFadeLabelControl.h $S/ux/fadelabel_depth_test.cpp $D/src/ &&
                   cp $S/ux/fadelabel_stubs.h $D/stub/stubs.h &&
                   for h in GUIControl.h GUILabel.h GUIMessage.h ServiceBroker.h guilib/guiinfo/GUIInfoLabel.h utils/Random.h \
                            windowing/WinSystem.h; do printf '#pragma once\n#include "stubs.h"\n' > $D/stub/$h; done &&
                   g++ -std=c++20 -Wall -O1 $SAN -I $D/stub -I $X $D/src/GUIFadeLabelControl.cpp $D/src/fadelabel_depth_test.cpp -o $D/t ;;
      audio-follow) $CXX -pthread -I $GT -I $X -c $X/cores/AudioEngine/Sinks/test/TestActiveAEFollowDisplay.cpp -o $D/t.o &&
                      g++ -pthread -o $D/t $D/t.o $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o ;;
      offsets) $CXX $SAN -pthread -I $S/stereo/stub -I $X -I $X/cores/VideoPlayer/DVDDemuxers -I $GT \
                 $X/cores/VideoPlayer/DVDDemuxers/test/TestBlurayOffsetMetadata.cpp $X/cores/VideoPlayer/DVDDemuxers/BlurayOffsetMetadata.cpp \
                 $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      # (Kodi's own files and tests as they are: not with -Werror)
      capture-convert) $CXX $SAN -pthread -I $S/capture/stub -I $X -isystem $FFB/include -I $GT \
                         $X/rendering/capture/test/TestCaptureConvert.cpp $X/rendering/capture/CaptureConvert.cpp \
                         $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o $FFB/lib/libswscale.a $FFB/lib/libavutil.a \
                         -lm -o $D/t ;;
      # (Hades: the frame of the video at its own size; Kodi's own capture files as they are: not with -Werror)
      video-frame) $CXX $SAN -pthread -I $X -I $GT $X/rendering/capture/test/TestVideoFrameCapture.cpp \
                     $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      videoframe_gl) gles=$(glesinc) && $CXX $SAN -pthread -DHAS_GLES=3 -I $S/capture/stub -I $S/stereo/glstub -I $X \
                       -isystem $gles -isystem $FFB/include $S/capture/videoframe_gl_test.cpp \
                       $X/rendering/capture/CaptureOwnSize.cpp $X/rendering/capture/CaptureReadback.cpp \
                       $X/rendering/capture/CaptureConvert.cpp $FFB/lib/libswscale.a $FFB/lib/libavutil.a \
                       /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -lm -o $D/t ;;
      videoframe_src) cp $S/capture/videoframe_src.py $D/t ;;
      cec-keys) $CXX $SAN -pthread -I $X -isystem $CEC -I $GT $X/peripherals/test/TestPeripheralCecKeys.cpp \
                  $X/peripherals/test/TestPeripheralCecPicture.cpp $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o \
                  -o $D/t ;;
      aus) $CXX $SAN -I $S/stereo/stub -I $X/cores/VideoPlayer/DVDDemuxers $S/stereo/aus_test.cpp \
             $X/cores/VideoPlayer/DVDDemuxers/BlurayOffsetMetadata.cpp -o $D/t ;;
      depth) $RG_PY $S/stereo/depth_gen.py $K $D/gen && $CXX $SAN -I $S/stereo/stub -I $X/cores/VideoPlayer/DVDDemuxers \
               $D/gen/depth_test.cpp $X/cores/VideoPlayer/DVDDemuxers/BlurayOffsetMetadata.cpp -o $D/t ;;
      autodepth|autodepth-old-rule)
        $RG_PY $S/stereo/depth_gen.py $K $D/gen && $CXX $([ $t = autodepth ] && echo "${WERR:-}" || echo -DOLD_RULE) $SAN \
          -I $D/gen -I $X/cores/VideoPlayer/VideoRenderers $S/stereo/autodepth_test.cpp \
          $X/cores/VideoPlayer/VideoRenderers/StereoDepth.cpp -o $D/t ;;
      autodepth_gl) gles=$(glesinc) && $CXX ${WERR:-} $SAN -pthread -DHAS_GLES=3 -I $S/stereo/glstub -I $X \
                      -I $X/cores/VideoPlayer/VideoRenderers -isystem $gles $S/stereo/autodepth_gl_test.cpp \
                      $X/cores/VideoPlayer/VideoRenderers/StereoDepth.cpp $X/cores/VideoPlayer/VideoRenderers/StereoDepthCapture.cpp \
                      $X/cores/VideoPlayer/VideoRenderers/StereoDepthMeter.cpp /usr/lib/x86_64-linux-gnu/libEGL.so.1 \
                      /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      hdrdepth_gl) gles=$(glesinc) && $CXX ${WERR:-} $SAN -pthread -DHAS_GLES=3 -DKODI_TREE="\"$K\"" \
                     -I $S/stereo/glstub -I $X -I $X/cores/VideoPlayer/VideoRenderers -isystem $gles \
                     $S/stereo/hdrdepth_gl_test.cpp $X/cores/VideoPlayer/VideoRenderers/StereoDepth.cpp \
                     $X/cores/VideoPlayer/VideoRenderers/StereoDepthCapture.cpp \
                     $X/cores/VideoPlayer/VideoRenderers/StereoDepthMeter.cpp /usr/lib/x86_64-linux-gnu/libEGL.so.1 \
                     /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      # (issue #34: the GUI's FBO of the HDR composite over frames; -DOLD_RULE the clear before, which must fail)
      guifbo_gl|guifbo_gl-old-rule)
        gles=$(glesinc) && $CXX $([ $t = guifbo_gl ] && echo "${WERR:-}" || echo -DOLD_RULE) $SAN -pthread -DHAS_GLES=3 \
          -DKODI_TREE="\"$K\"" -I $S/stereo/glstub -I $X -isystem $gles $S/stereo/guifbo_gl_test.cpp \
          /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      # (issue #34: a 3D video gone back to 2D and the bridge, with Kodi's RenderFlags.cpp; -DOLD_RULE the rule before)
      # (1.2.2: the subtitles of a 3D output, the Atmos badge, holding Start, a 3D Dolby Vision video in the player
      # information; -DOLD_RULE the pixel ratio of libass as before)
      pc3d|pc3d-old-rule) $CXX $([ $t = pc3d ] && echo "${WERR:-}" || echo -DOLD_RULE) $SAN -DKODI_TREE="\"$K\"" \
                         -I $S/stereo/glstub -I $X \
                         $S/stereo/pc3d_test.cpp -o $D/t ;;
      fallback2d|fallback2d-old-rule) ff=$(ffinc) && $CXX $([ $t = fallback2d ] && echo "${WERR:-}" || echo -DOLD_RULE) \
                         -Wshadow $SAN -DKODI_TREE="\"$K\"" -I $X -isystem $ff $S/stereo/fallback2d_test.cpp \
                         $X/cores/VideoPlayer/VideoRenderers/RenderFlags.cpp -o $D/t ;;
      hdr3d_gl|hdr3d_gl-old-rule)
        gles=$(glesinc) && $CXX $([ $t = hdr3d_gl ] && echo "${WERR:-}" || echo -DOLD_RULE) $SAN -pthread -DHAS_GLES=3 \
          -DKODI_TREE="\"$K\"" -I $S/stereo/glstub -I $X -isystem $gles $S/stereo/hdr3d_gl_test.cpp \
          /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      # (Kodi's OwnScanout.cpp as it is, with the defines of the image's build of it: not with -Werror, which its
      # warnings would stop; the build folder's gbm.h, drm_fourcc.h and xf86drm.h, the host's libgbm and libdrm only to
      # link)
      fpown_gl) gles=$(glesinc) && fp=$(fpinc) && $CXX $SAN -pthread -DHAS_GLES=3 -DHAS_GBM_MODIFIERS \
                  -I $S/stereo/fpstub -I $S/stereo/glstub -I $X -isystem $gles -isystem $fp -isystem $fp/libdrm \
                  $S/stereo/fpown_gl_test.cpp $X/windowing/gbm/OwnScanout.cpp /usr/lib/x86_64-linux-gnu/libgbm.so.1 \
                  /usr/lib/x86_64-linux-gnu/libdrm.so.2 \
                  /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -o $D/t ;;
      fpown_src) cp $S/stereo/fpown_src.py $D/t ;;
      settings_resize) $CXX -pthread $SAN -I $X $H/settings_resize_test.cpp -o $D/t ;;
      settings_resize-D*) $CXX -pthread $SAN -D${t#settings_resize-D} -I $X $H/settings_resize_test.cpp -o $D/t ;;
      settings_resize-tsan) $CXX -pthread -fsanitize=thread -I $X $H/settings_resize_test.cpp -o $D/t ;;
      vrr) $CXX -Wno-mismatched-new-delete $SAN -pthread -I $X $H/vrr_test.cpp -o $D/t ;;
      vrr_gui|vrr_video|display_modes|display_lut|exact_rate|video_size|content_type) $CXX ${WERR:-} -Wshadow $SAN -I $X $H/${t}_test.cpp -o $D/t ;;
      display_lut_loader) $CXX ${WERR:-} -Wshadow $SAN -pthread -I $X $H/display_lut_loader_test.cpp -o $D/t ;;
      display_lut_loader-tsan) $CXX ${WERR:-} -Wshadow -fsanitize=thread -pthread -I $X $H/display_lut_loader_test.cpp -o $D/t ;;
      lut_src) cp $H/lut_src_check.py $D/t ;;
      content_type-old-rule) $CXX $SAN -DOLD_RULE -I $X $H/content_type_test.cpp -o $D/t ;;
      picture_defaults|subtitle_bars|info_refresh|verdict) $CXX ${WERR:-} -Wshadow $SAN -I $X $S/ux/${t}_test.cpp -o $D/t ;;
      lost_frames_src) cp $S/ux/lost_frames_src.py $D/t ;;
      info_refresh_src) cp $S/ux/info_refresh_src.py $D/t ;;
      keymap_check) cp $S/ux/keymap_check.py $D/t ;;
      # (Kodi's file with stubs of its system_gl.h and utils/log.h, and the GLES headers of the build folder)
      subtitle_bars_gl) $CXX ${WERR:-} -Wshadow $SAN -I $S/ux/stub -I "$GLINC" -I $X \
                          $X/cores/VideoPlayer/VideoRenderers/SubtitleBarsGLES.cpp $S/ux/subtitle_bars_gl_test.cpp -o $D/t \
                          /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 ;;
      # (Kodi's own file as it is: not with -Werror)
      picture_timing) $CXX $SAN -I $X -isystem "$(ffinc)" $H/picture_timing_test.cpp $X/utils/ProgressiveVideo.cpp -o $D/t ;;
      vrr_video-old-rule) $CXX $SAN -DOLD_RULE -I $X $H/vrr_video_test.cpp -o $D/t ;;
      # (Kodi's own files and tests as they are, as a release build compiles them: NDEBUG)
      cefix-ae) A=$X/cores/AudioEngine/Utils
                $CXX -DNDEBUG $SAN -pthread -DTARGET_POSIX -DTARGET_LINUX -I $S/fix/stubs -I $X -I $X/platform/posix \
                  -isystem $FF/include -I $GT $A/test/TestAEPackIEC61937.cpp $A/test/TestAEStreamParser.cpp \
                  $A/test/TestAEChannelLayout.cpp $A/AEPackIEC61937.cpp $A/AEStreamInfo.cpp $A/AEUtil.cpp $A/AEChannelInfo.cpp \
                  $S/cefix/ae_clock.cpp \
                  $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o $FF/lib/libavutil.a -lm -o $D/t ;;
      # (Kodi's own VideoSettings.cpp as it is: not with -Werror; fmt's headers of the build folder, header only)
      permemory) $CXX $SAN -pthread -DTARGET_POSIX -DTARGET_LINUX -DFMT_HEADER_ONLY -isystem $FMT -I $X -I $X/platform/posix                    -I $GT $X/cores/VideoPlayer/test/TestFilmMemory.cpp $X/cores/VideoPlayer/FilmMemory.cpp                    $X/cores/VideoSettings.cpp $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      permemory-src) cp $S/permemory/src_check.py $D/t ;;
      stereo-eye) $CXX $SAN -pthread -I $X -I $GT $X/cores/VideoPlayer/test/TestStereoEyeSize.cpp \
                    $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      stereo-names) cp $S/cefix/stereo_names.py $D/t ;;
      font) $RG_PY $S/cefix/font_gen.py $K $D/gen && $CXX $SAN -I $S/cefix -isystem $FTINC $D/gen/font_pieces.cpp \
              $S/cefix/font_test.cpp $FTLIB -o $D/t ;;
      cefix-src) cp $S/cefix/src_check.py $D/t ;;
      # (Kodi's own files and tests as they are, as a release build compiles them: NDEBUG; Kodi pull request 29331)
      ae-actor) $CXX -DNDEBUG $SAN -pthread -DTARGET_POSIX -DTARGET_LINUX -I $X -I $X/platform/posix -I $GT \
                  $X/utils/test/TestActorProtocol.cpp $X/utils/ActorProtocol.cpp $X/threads/Event.cpp \
                  $X/platform/posix/threads/RecursiveMutex.cpp $RG_SHARED/gtest/main.o $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      ae-devchange) $CXX -DNDEBUG $SAN -pthread -I $X -I $GT $X/cores/AudioEngine/Sinks/test/TestActiveAEDeviceChange.cpp \
                      $X/cores/AudioEngine/Sinks/test/TestActiveAEDeviceChangePace.cpp $RG_SHARED/gtest/main.o \
                      $RG_SHARED/gtest/gtest-all.o -o $D/t ;;
      devchange-src) cp $S/devchange/src_check.py $D/t ;;
      edidpath) $CXX ${WERR:-} -Wshadow $SAN -I $X $S/setup/edidpath_test.cpp -o $D/t ;;
      cachedefaults|cachedefaults-old-rule)
        $CXX $([ $t = cachedefaults ] && echo "${WERR:-}" || echo -DOLD_RULE) -Wshadow $SAN -I $X \
          $S/setup/cachedefaults_test.cpp -o $D/t ;;
      dvpathprior|dvpathprior-old-rule)
        $CXX $([ $t = dvpathprior ] && echo "${WERR:-}" || echo -DOLD_RULE) -Wshadow $SAN -I $K/$GB \
          $S/setup/dvpathprior_test.cpp -o $D/t ;;
      # (a measure of time: no sanitizer, optimised as the image is)
      vrr_pacer) $CXX ${WERR:-} -O2 -I $X $H/vrr_pacer_bench.cpp -o $D/t ;;
      chroma_step) $CXX ${WERR:-} -Wshadow $SAN -I $X/cores/VideoPlayer/VideoRenderers $S/chroma/chroma_step_test.cpp -o $D/t ;;
      chroma_src) cp $S/chroma/chroma_src_check.py $D/t ;;
      gpu_disjoint) $CXX ${WERR:-} -Wshadow $SAN -I $K/$GB $S/chroma/gpu_disjoint_test.cpp -o $D/t ;;
      disjoint_src) cp $S/chroma/disjoint_src_check.py $D/t ;;
      chroma_shader) gcc -std=gnu11 -O1 -g -Wall -Wextra ${WERR:-} -I "$GLINC" $S/chroma/chroma_shader_test.c -o $D/t                        /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -lm ;;
      copy_dither) gcc -std=gnu11 -O1 -g -Wall -Wextra ${WERR:-} -I "$GLINC" -I $K/tools/dvbridge $S/chroma/copy_dither_test.c                      $K/tools/dvbridge/dvbridge_gl_pack.c -o $D/t                      /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -lm ;;
      projector_policy) $CXX ${WERR:-} -Wshadow $SAN -I $X $S/projector/projector_policy_test.cpp \
                          $X/network/ProjectorFeed.cpp $X/network/ProjectorLink.cpp -o $D/t ;;
      projector_link) $CXX ${WERR:-} -Wshadow $SAN -I $X/network $S/projector/projector_cli.cpp \
                        $X/network/ProjectorLink.cpp -o $D/cli && cp $S/projector/projector_link_test.py $D/t ;;
      projector_src) cp $S/projector/projector_src_check.py $D/t ;;
      *) echo "no test $t"; false ;;
    esac
  } > "$D/build.log" 2>&1 < /dev/null
}
# run <test> <tree> <dir>: the test's run, its whole output on stdout
run() {
  local t=$1 K=$2 D=$3 e
  case $t in
    modelist) if [ "$K" = "$KT" ]; then $D/t 20000; else $D/t 4000; fi ;;
    reslist-address|reslist-thread) e=0
      [ $t = reslist-address ] && { $D/t same || e=1; }
      $D/t race new 20000 || e=1
      # (the way before, which the test carries: it must be shown wrong)
      $D/t race old 20000 > $D/old.log 2>&1; echo "the way before: exit $?; $(grep -c 'ERROR: \|WARNING: ThreadSanitizer' $D/old.log) sanitizer reports"
      return $e ;;
    dvahead) $D/t $IN/windows-hashed.tsv $IN/windows-new-hashed.tsv ;;
    dualmode-table) $D/t > $D/out.log 2>&1; grep -a '^table:' $D/out.log > $D/table.txt
      diff $H/ref/dualmode-table.txt $D/table.txt | grep '^[<>]' | sed 's/^/FAIL: table line differs: /' | head -n 2000
      echo "table: $(wc -l < $D/table.txt) lines, ref/dualmode-table.txt $(wc -l < $H/ref/dualmode-table.txt)"
      cmp -s $H/ref/dualmode-table.txt $D/table.txt ;;
    strings) $RG_PY $D/t $K ;;
    src_check) $RG_PY $D/t $K ;;
    eos) env $RG_GPU_ENV LP_NUM_THREADS=${LP_NUM_THREADS:-4} timeout 900 $D/t ;;
    render_refused) env $RG_GPU_ENV LP_NUM_THREADS=${LP_NUM_THREADS:-4} timeout 1200 $D/t $IN/BL_EL.mkv ;;
    drmconnector-*) ( cd $D && ./conn ${t#drmconnector-} ) ;;
    pieces) ( cd $D && ./t ) ;;
    aesink-same) $D/aesink-address same > $D/same.txt; e=$?; cat $D/same.txt
      diff $H/ref/aesink-same.txt $D/same.txt | grep '^[<>]' | sed 's/^/FAIL: differs from ref\/aesink-same.txt: /'
      [ $e = 0 ] && cmp -s $H/ref/aesink-same.txt $D/same.txt ;;
    aesink-race) e=0
      for san in thread address; do
        TSAN_OPTIONS="halt_on_error=0 report_signal_unsafe=0" timeout 1200 $D/aesink-$san race 300 > $D/race-$san.log 2>&1 || e=1
        echo "race ($san): $(grep -c 'WARNING: ThreadSanitizer' $D/race-$san.log) ThreadSanitizer reports, $(grep -c 'ERROR: AddressSanitizer' $D/race-$san.log) AddressSanitizer errors; $(grep '^race:' $D/race-$san.log)"
        grep -q 'WARNING: ThreadSanitizer\|ERROR: AddressSanitizer' $D/race-$san.log && e=1
      done
      return $e ;;
    swig) $RG_PY $D/t $K "$REGRESS_LE_BUILD" $D/out > $D/out.txt 2>&1; e=$?; cat $D/out.txt
      local n; n=$(sed -n 's/^lines with Hades text: //p' $D/out.txt)
      [ "$n" = "$SWIG_HADES_LINES" ] || echo "FAIL: lines with Hades text ${n:-none}, $SWIG_HADES_LINES expected"
      return $e ;;
    widths) $RG_PY $D/t $K ;;
    aus) $RG_PY $H/stereo/mkaus.py $IN/bd1080.264 $D/aus && $D/t $D/aus ;;
    settings_resize-tsan) TSAN_OPTIONS=halt_on_error=1 timeout 180 $D/t ;;
    settings_resize*) timeout 120 $D/t ;;
    vrr_pacer) timeout 60 $D/t 10 ;;
    chroma_shader) env $RG_GPU_ENV timeout 600 $D/t $K ;;
    copy_dither) env $RG_GPU_ENV timeout 600 $D/t ;;
    stereo-names|cefix-src|permemory-src|devchange-src) $RG_PY $D/t $K ;;
    edidpath) $D/t $IN/edid/lg-c5-direct.bin $IN/edid/lg-c5-sound-bar.bin $IN/edid/receiver-denon.bin \
                $IN/edid/receiver-onkyo.bin $IN/edid/receiver-pioneer.bin -- $IN/edid/tv-lg-1.bin $IN/edid/tv-lg-2.bin \
                $IN/edid/tv-lg-3.bin ;;
    cachedefaults*) $D/t $K ;;
    speakerplan) $D/t $K $IN/edid/lg-c5-direct.bin $IN/edid/lg-c5-sound-bar.bin $IN/edid/receiver-denon.bin \
                   $IN/edid/receiver-onkyo.bin $IN/edid/tv-lg-1.bin ;;
    projector_link) timeout 600 $RG_PY $D/t $D/cli ;;
    projector_src) $RG_PY $D/t $K ;;
    lut_src|info_refresh_src|keymap_check|lost_frames_src|videoframe_src) $RG_PY $D/t $K ;;
    display_lut_loader-tsan) TSAN_OPTIONS=halt_on_error=1 timeout 120 $D/t ;;
    chroma_src|disjoint_src|power_src|fpown_src|linkbad_src) $RG_PY $D/t $K ;;
    font) $D/t $K/addons/skin.estuary/fonts/NotoSans-Regular.ttf $K/addons/skin.estuary/fonts/NotoMono-Regular.ttf ;;
    *) $D/t ;;
  esac
}
# the line that says a test passed, and the line of a failed check
pass_re() {
  case $1 in
    p5always) echo '^ALL PASSED: [0-9]+ of [0-9]+$' ;;
    policy) echo '^policy: [0-9]+ checks, 0 failed' ;;
    reachable) echo '^REACHABLE-TEST: [0-9]+ cases .*, 0 wrong' ;;
    anyhz) echo '^ANYHZ-TEST PASSED' ;;
    strings) echo '^PROJECTOR-STRINGS PASSED' ;;
    widths) echo 'rows over their space: 0' ;;
    dveos) echo '^X1 16 cases, 0 failed' ;;
    eos) echo '^eos: [0-9]+ checks, 0 failed' ;;
    modelist) echo '^PASSED: 0 of [0-9]+ checks failed' ;;
    reslist-*) echo '^race new: [0-9]+ lists made again, [0-9]+ reads, 0 entries read half made$' ;;
    dualmode-table) echo '^table: [0-9]+ lines' ;;
    *_test) echo '(-TEST PASSED|^HDRSETTLE-TEST: [0-9]+ checks, 0 failed)' ;;
    linkdepth) echo 'LINKDEPTH-TEST PASSED' ;;
    src_check) echo '^SRC-CHECK PASSED' ;;
    render_refused) echo '^RENDER-REFUSED PASSED' ;;
    drmconnector-*) echo ' PASSED \(0 failed\)$' ;;
    retiredkept) echo '^RETIRED-KEPT PASSED' ;;
    pieces) echo '^PIECES PASSED' ;;
    aesink-same) echo 'SAME DONE' ;;
    aesink-race) echo '^race \(address\): 0 ThreadSanitizer reports, 0 AddressSanitizer errors' ;;
    seek) echo '^PASSED: [0-9]+ passed, 0 failed' ;;
    speakers) echo '^SPEAKERS-TEST PASSED$' ;;
    speakerplan) echo '^SPEAKER-PLAN PASSED$' ;;
    edidpath) echo '^EDIDPATH-TEST PASSED$' ;;
    cachedefaults*) echo '^CACHEDEFAULTS-TEST PASSED$' ;;
    dvpathprior*) echo '^DVPATHPRIOR-TEST PASSED$' ;;
    swig) echo "^lines with Hades text: $SWIG_HADES_LINES\$" ;;
    fadelabel) echo '^PASSED: 0 of [0-9]+ passes' ;;
    audio-follow|offsets|capture-convert|cec-keys|cefix-ae|stereo-eye|permemory|video-frame|ae-actor|ae-devchange) echo '^\[  PASSED  \] [0-9]+ tests' ;;
    videoframe_gl) echo '^PASSED$' ;;
    videoframe_src) echo '^VIDEOFRAME-SRC PASSED$' ;;
    stereo-names) echo '^STEREO-NAMES PASSED$' ;;
    font) echo '^FONT-TEST PASSED$' ;;
    cefix-src) echo '^CEFIX-SRC PASSED$' ;;
    permemory-src) echo '^PERMEMORY-SRC PASSED$' ;;
    devchange-src) echo '^DEVCHANGE-SRC PASSED$' ;;
    aus|depth|autodepth*|hdr3d_gl*|hdrdepth_gl|sdrcontainer_gl|stereo3d*|fpown_gl|guifbo_gl*|fallback2d*|pc3d*) echo '^0 failed$' ;;
    fpown_src) echo '^FPOWN-SRC PASSED$' ;;
    linkbad_src) echo '^LINKBAD-SRC PASSED$' ;;
    settings_resize*) echo '^[0-9]+ checks, 0 failed' ;;
    vrr|vrr_gui|vrr_video|display_modes|display_lut|exact_rate|video_size|picture_timing|content_type) echo '^PASSED$' ;;
    display_lut_loader|display_lut_loader-tsan) echo '^PASSED$' ;;
    lut_src) echo '^LUT-SRC PASSED$' ;;
    info_refresh_src) echo '^INFO-REFRESH-SRC PASSED$' ;;
    keymap_check) echo '^KEYMAP-CHECK PASSED$' ;;
    lost_frames_src) echo '^LOST-FRAMES-SRC PASSED$' ;;
    power_src) echo '^POWER-SRC PASSED$' ;;
    picture_defaults|subtitle_bars|subtitle_bars_gl|info_refresh|verdict) echo '^PASSED$' ;;
    vrr_pacer) echo '^interval against the one asked for: ' ;;
    chroma_step) echo '^CHROMA-STEP PASSED$' ;;
    chroma_src) echo '^CHROMA-SRC PASSED$' ;;
    gpu_disjoint) echo '^GPU-DISJOINT PASSED$' ;;
    disjoint_src) echo '^DISJOINT-SRC PASSED$' ;;
    chroma_shader) echo '^CHROMA-SHADER PASSED$' ;;
    copy_dither) echo '^COPY-DITHER PASSED$' ;;
    projector_policy) echo '^PROJECTOR-POLICY PASSED$' ;;
    projector_link) echo '^PROJECTOR-LINK PASSED$' ;;
    projector_src) echo '^PROJECTOR-SRC PASSED$' ;;
    *) echo '^ALL PASSED' ;;
  esac
}
fail_re() {
  case $1 in
    audio-follow|offsets|capture-convert|cec-keys|cefix-ae|stereo-eye|permemory|video-frame|ae-actor|ae-devchange) echo '^\[  FAILED  \] [A-Za-z0-9_]+\.[A-Za-z0-9_]+' ;;
    widths) echo 'OVER' ;;
    reachable*) echo '^WRONG' ;;
    *) echo '^FAIL' ;;
  esac
}
# the tests that run with the GPU or a minute's wait, one at a time
heavy() { case $1 in eos|render_refused|chroma_shader|drmconnector-bounded|pieces|aesink-race|reslist-*) return 0 ;; *) return 1 ;; esac; }

# check <test>...: each test against the tree as it is: built (WERR=-Werror where its flags take it), run, judged
check() {
  local t D L v
  for t in "$@"; do
    D=$RG_W/t/$t L=$RG_W/logs/$t.log
    rg_fresh "$D"
    if ! WERR=-Werror build $t "$KT" "$D"; then
      rg_item "$t" FAILED "not built: $(grep -a -m1 -E 'error|FAIL' $D/build.log | cut -c1-180)"; continue
    fi
    rg_log "$L" run $t "$KT" "$D"
    v=$(rg_pass "$L" "$(pass_re $t)" "$(fail_re $t)")
    rg_item "$t" "$(rg_say "$v")" "$(rg_rest "$v")"
  done
}
# must_fail <test> <expected>: a test built with the rule before it carries (-DOLD_RULE and the like) must fail
must_fail() {
  local t=$1 want=$2 D=$RG_W/t/$1 L=$RG_W/logs/$1.log v
  rg_fresh "$D"
  build $t "$KT" "$D" || { rg_item "$t" FAILED "not built: $(grep -a -m1 error $D/build.log | cut -c1-180)"; return; }
  rg_log "$L" run $t "$KT" "$D"
  if [ "$want" = exit ]; then
    [ "$(rg_exit $L)" != 0 ] && [ -n "$(rg_exit $L)" ] && v="PASSED it fails as it must: exit $(rg_exit $L)" || v="FAILED it passes: exit $(rg_exit $L)"
  else v=$(rg_fails "$L" "$want" "$(fail_re $t)"); fi
  rg_item "$t" "$(rg_say "$v")" "$(rg_rest "$v")"
}
# mutants <test>...: every mutant of mutants.py that names one of these tests, in parallel (the heavy ones one at a
# time): the tree copied with the mutant's edit, the test built against it and run; it must fail as written there
mutants() {
  local list=$RG_W/mutants-$1.txt name t want n=0
  $MUT list "$@" > "$list" || { rg_item "mutants-$1" FAILED "mutate.py list failed"; return; }
  while read -r name t want; do
    n=$((n + 1))
    while [ "$(jobs -r | wc -l)" -ge "$RG_JOBS" ]; do wait -n; done
    if heavy $t; then mutant_one "$name" "$t"; else mutant_one "$name" "$t" & fi
  done < "$list"
  wait
  while read -r name t want; do mutant_item "$name" "$t" "$want"; done < "$list"
  [ $n -gt 0 ] || rg_item "mutants-$1" FAILED "no mutant names $*"
}
mutant_one() { # <mutant> <test>: into $RG_W/m/<mutant>/<test>
  local name=$1 t=$2 M=$RG_W/m/$1 D
  D=$M/run-$t
  rg_fresh "$D"
  ( flock 9
    [ -d "$M/tree" ] || $MUT make "$name" "$KT" "$M/tree" > "$M/make.log" 2>&1 ) 9> "$M.lock"
  [ -d "$M/tree" ] && grep -q '^mutant ' "$M/make.log" || { echo "no tree" > "$D/state"; return; }
  build $t "$M/tree" "$D" || { echo "not built" > "$D/state"; return; }
  rg_log "$D/run.log" run $t "$M/tree" "$D"
  echo ran > "$D/state"
}
mutant_item() { # <mutant> <test> <expected>
  local name=$1 t=$2 want=$3 M=$RG_W/m/$1 D v e
  D=$M/run-$t
  case "$(cat $D/state 2> /dev/null)" in
    "no tree") v="FAILED the mutant no longer applies: $(grep -a -m1 -E 'NOT APPLIED|no mutant|exists' $M/make.log | cut -c1-200)" ;;
    "not built")
      if [ "$want" = static ] && grep -a -q 'error: static assertion failed' $D/build.log; then
        v="PASSED it fails as it must: a static assertion of the test fails ($(grep -a -m1 'static assertion failed' $D/build.log | sed 's/.*error: //' | cut -c1-100))"
      else v="FAILED the test does not build against the mutant: $(grep -a -m1 -E 'error|FAIL' $D/build.log | cut -c1-160)"; fi ;;
    ran) e=$(rg_exit $D/run.log)
      if [ "$want" = exit ]; then
        if [ -n "$e" ] && [ "$e" != 0 ]; then v="PASSED it fails as it must: exit $e; $(grep -a -m1 -E 'ERROR: [A-Za-z]+Sanitizer|WARNING: ThreadSanitizer|^FAIL' $D/run.log | cut -c1-120)"
        else v="FAILED nothing fails on the mutant: exit $e"; fi
      else v=$(rg_fails "$D/run.log" "$want" "$(fail_re $t)"); fi ;;
    *) v="FAILED it did not run" ;;
  esac
  rg_item "$t.mut.$name" "$(rg_say "$v")" "$(rg_rest "$v")"
}
# done with the mutated trees of these mutants (their logs stay)
mutants_clean() { local m; for m in "$RG_W"/m/*/tree; do [ -d "$m" ] && rg_rm "$m"; done; return 0; }

# ------------------------------------------------------------------------------------------------- the stages
KT=$(kodi) || exit 1
echo "Kodi tree: $KT ($(rg_kodi_commit 2> /dev/null | cut -c1-12))"

stage_existing() {
  check settings_resize vrr settings_resize-tsan vrr_gui vrr_video vrr_pacer display_modes display_lut exact_rate video_size     picture_timing \
    content_type display_lut_loader display_lut_loader-tsan lut_src
  must_fail settings_resize-DOLD_RULE 11
  must_fail settings_resize-DIS_PLAYING_RULE 3
  must_fail settings_resize-DNO_SIGNAL 11
  must_fail settings_resize-DNO_VIDEO_PLAYS 1
  must_fail vrr_video-old-rule 4
  # (the rule before: Kodi never set the content type; 9 checks and the verdict line)
  must_fail content_type-old-rule 10
  mutants settings_resize vrr vrr_gui vrr_video display_modes display_lut exact_rate video_size picture_timing content_type \
    display_lut_loader lut_src
  mutants_clean
}
stage_policy() {
  # (widths: those strings where Estuary shows them, measured with its fonts by Pillow)
  check p5always policy videopasses_missed videopasses dvconvert reachable gap_policy dualhdr anyhz strings widths
  must_fail reachable-old-rule 6
  mutants p5always policy videopasses_missed videopasses dvconvert reachable gap_policy dualhdr anyhz strings widths
  mutants_clean
}
stage_window() {
  LP=$(rg_lp_shared) && FF=$(rg_ff_bridge) || rg_fail "libplacebo or FFmpeg not built"
  check windowpreload dveos eos
  mutants windowpreload dveos eos
  mutants_clean
}
stage_modes() {
  check modelist reslist-address reslist-thread
  local t
  for t in reslist-address reslist-thread; do
    grep -q 'the way before: exit [1-9][0-9]*; [1-9][0-9]* sanitizer reports' $RG_W/logs/$t.log &&
      rg_item $t.old "PASSED" "the way before is shown wrong: $(grep -m1 'the way before' $RG_W/logs/$t.log)" ||
      rg_item $t.old "FAILED" "the way before is not shown wrong: $(grep -m1 'the way before' $RG_W/logs/$t.log)"
  done
  mutants modelist reslist-address reslist-thread
  mutants_clean
}
stage_output() {
  inputs || rg_fail "an input of inputs-kodi-host.tsv is missing"
  # (the sets of logs by the SHA256 of their names: see the head of this file)
  local f
  for f in windows windows-new; do
    $RG_PY -c 'import hashlib, sys
for line in open(sys.argv[1], encoding="utf-8"):
    f = line.rstrip("\n").split("\t")
    f[0] = hashlib.sha256(f[0].encode()).hexdigest()[:12]
    sys.stdout.write("\t".join(f) + "\n")' "$IN/$f.tsv" > "$IN/$f-hashed.tsv" || rg_fail "the windows $f"
  done
  check dvahead dualmode dualmode-table dvframetest drmwake dvrepeatkeep presentlag \
    finishrequest_test stall_ahead_test keep_pacing_test flip_recovery_test held_mode_test float_blame_test \
    dv_announce_test gui_hold_test dv_move_test hdrsettle_test relink_test frl_mode_change_test frl_keep_test sdrsignal_test dvform_test \
    linkbad_test linkbad_src \
    sdrcontainer_gl convstep_test
  must_fail dvframetest-old-rule 4
  must_fail presentlag-old-rule 5
  mutants dvahead dualmode dualmode-table dvframetest drmwake dvrepeatkeep presentlag \
    finishrequest_test stall_ahead_test keep_pacing_test flip_recovery_test held_mode_test float_blame_test \
    dv_announce_test gui_hold_test dv_move_test hdrsettle_test relink_test frl_mode_change_test frl_keep_test sdrsignal_test dvform_test \
    linkbad_test linkbad_src \
    sdrcontainer_gl convstep_test
  mutants_clean
}
stage_linkdepth() {
  check linkdepth src_check
  mutants linkdepth src_check
  mutants_clean
}
stage_fix() {
  inputs || rg_fail "an input of inputs-kodi-host.tsv is missing"
  LP=$(rg_lp_shared) && FF=$(rg_ff_bridge) || rg_fail "libplacebo or FFmpeg not built"
  DRM=$(drminc) || rg_fail "no libdrm headers"
  # ("bounded" waits a minute: it runs beside the others)
  check drmconnector-bounded &
  check render_refused drmconnector-connected drmconnector-never drmconnector-kept drmconnector-failed retiredkept \
    pieces aesink-same aesink-race eofpresentation decoderdrain hlgreason cecline power_src
  wait
  mutants render_refused drmconnector-never drmconnector-kept drmconnector-failed drmconnector-bounded retiredkept \
    pieces aesink-same aesink-race eofpresentation decoderdrain hlgreason cecline power_src
  mutants_clean
}
stage_own() {
  local bb L
  inputs || rg_fail "an input of inputs-kodi-host.tsv is missing"
  check seek swig speakers speakerplan gpusensors
  mutants seek swig speakers speakerplan gpusensors
  mutants_clean
  # pwr() of watch-dv.sh (hades-diag, the LibreELEC tree of REGRESS_LE_REF) with the image's busybox; its six
  # mutations are made by the test itself, on the function it takes out of the script
  bb=$(ls "$REGRESS_LE_BUILD"/install_pkg/busybox-*/usr/bin/busybox 2> /dev/null | head -n 1)
  rg_le_tree $RG_W/le packages/sysutils/hades-diag/scripts/watch-dv.sh
  L=$RG_W/logs/pwr.log
  if [ -z "$bb" ]; then rg_item pwr FAILED "no busybox in $REGRESS_LE_BUILD/install_pkg"
  else
    rg_log "$L" bash $H/own/pwr_test.sh "$bb" $RG_W/le/packages/sysutils/hades-diag/scripts/watch-dv.sh
    rg_item pwr "$(rg_say "$(rg_pass "$L" '^PWR TEST PASSED$' '^FAIL|^PWR TEST FAILED')")" "$(rg_rest "$(rg_pass "$L" '^PWR TEST PASSED$' '^FAIL|^PWR TEST FAILED')")"
    local p
    for p in P1 P2 P3 P4 P5 P6; do
      grep -a -q "^mutation $p caught$" $L && rg_item pwr.mut.$p PASSED "caught" ||
        rg_item pwr.mut.$p FAILED "$(grep -a -m1 "MUTATION $p" $L || echo 'no line of it')"
    done
  fi
}
stage_ux() {
  GT=$(gtest) || rg_fail "googletest of the build folder"
  GLINC=$(rg_glinc) || rg_fail "no GLES headers in the build folder"
  check fadelabel audio-follow picture_defaults subtitle_bars subtitle_bars_gl info_refresh info_refresh_src keymap_check \
    verdict lost_frames_src
  mutants fadelabel audio-follow picture_defaults subtitle_bars subtitle_bars_gl info_refresh info_refresh_src keymap_check \
    verdict lost_frames_src
  mutants_clean
  # the Bluetooth logic of the LibreELEC settings add-on: bluetooth.py of its recipe (archive checked against
  # PKG_SHA256, every patch with -F0), and the same with the part of LibreELEC-settings-9019 that changes it reversed
  local P=packages/mediacenter/LibreELEC-settings ver sha tb S=$RG_W/settings L p
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  tb=$(rg_source LibreELEC-settings "LibreELEC-settings-$ver.tar.gz")
  rg_fresh $S/src $S/before
  rg_le_tree $S/le $P/patches
  if [ -z "$tb" ] || [ "$(sha256sum < "$tb" | cut -c1-64)" != "$sha" ]; then
    rg_item bt-addon FAILED "no LibreELEC-settings-$ver.tar.gz with PKG_SHA256 in REGRESS_SOURCES"
    rg_item bt-addon.without-9019 FAILED "no LibreELEC-settings source"; return 0
  fi
  tar -xzf "$tb" --strip-components=1 -C $S/src
  for p in $(ls $S/le/$P/patches/*.patch | sort); do
    patch -d $S/src -p1 -F0 -s --no-backup-if-mismatch < "$p" || { rg_item bt-addon FAILED "patch $(basename $p) does not apply"; return 0; }
  done
  L=$RG_W/logs/bt-addon.log
  rg_log "$L" $RG_PY $H/ux/addon_logic_test.py $S/src/resources/lib/modules/bluetooth.py
  rg_item bt-addon "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  mkdir -p $S/before/resources/lib/modules && cp $S/src/resources/lib/modules/bluetooth.py $S/before/resources/lib/modules/
  $RG_PY - $S/le/$P/patches/LibreELEC-settings-9019-*.patch $S/before/9019-bluetooth.patch <<'PY'
import sys
text = open(sys.argv[1], encoding='utf-8').read()
at = text.index('diff --git a/resources/lib/modules/bluetooth.py b/resources/lib/modules/bluetooth.py')
end = text.find('\ndiff --git ', at + 1)
open(sys.argv[2], 'w', encoding='utf-8').write(text[at:] if end < 0 else text[at:end + 1])
PY
  L=$RG_W/logs/bt-addon.without-9019.log
  if patch -R -p1 -F0 -s -d $S/before < $S/before/9019-bluetooth.patch; then
    rg_log "$L" $RG_PY $H/ux/addon_logic_test.py $S/before/resources/lib/modules/bluetooth.py
    [ "$(rg_exit $L)" != 0 ] && rg_item bt-addon.without-9019 PASSED "it fails without 9019: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item bt-addon.without-9019 FAILED "it passes without 9019"
  else rg_item bt-addon.without-9019 FAILED "the part of 9019 that changes bluetooth.py does not reverse"; fi
  # the patches of the add-on numbered after $1, the last first
  settings_after() {
    local p n
    for p in $(ls $S/le/$P/patches/LibreELEC-settings-[0-9]*-*.patch | sort -r); do
      n=$(basename "$p" | sed -E 's/^LibreELEC-settings-([0-9]+)-.*/\1/')
      [ "$n" -gt "$1" ] && echo "$p"
    done
  }
  # Hades: the A/B comparison of native Dolby Vision of Check this setup (hades_check.py and hades_check_ui.py of
  # LibreELEC-settings-9073, with stand-ins for Kodi), and the same with 9073 reversed, which must fail
  L=$RG_W/logs/check-ab.log
  rg_log "$L" $RG_PY $H/ux/check_ab_test.py $S/src/resources/lib
  rg_item check-ab "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  rg_fresh $S/without-9073 && cp -a $S/src/. $S/without-9073/
  L=$RG_W/logs/check-ab.without-9073.log
  # (every patch of the add-on numbered after 9073 reversed first, the last first: they change the same files)
  local later
  for later in $(settings_after 9073); do
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9073 < "$later" || rg_item check-ab.without-9073 FAILED "$(basename $later) does not reverse"
  done
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9073 < "$(ls $S/le/$P/patches/LibreELEC-settings-9073-*.patch)"; then
    rg_log "$L" $RG_PY $H/ux/check_ab_test.py $S/without-9073/resources/lib
    [ "$(rg_exit $L)" != 0 ] && rg_item check-ab.without-9073 PASSED "it fails without 9073: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item check-ab.without-9073 FAILED "it passes without 9073"
  else rg_item check-ab.without-9073 FAILED "9073 does not reverse"; fi
  rg_rm $S/without-9073
  # Hades: the frame packing card of Check this setup (hades_check.py and hades_check_ui.py of
  # LibreELEC-settings-9074, with stand-ins for Kodi), and the same with 9074 reversed, which must fail
  L=$RG_W/logs/check-fp.log
  rg_log "$L" $RG_PY $H/ux/check_fp_test.py $S/src/resources/lib
  rg_item check-fp "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  rg_fresh $S/without-9074 && cp -a $S/src/. $S/without-9074/
  L=$RG_W/logs/check-fp.without-9074.log
  # (every patch of the add-on numbered after 9074 reversed first, the last first: they change the same files)
  for later in $(settings_after 9074); do
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9074 < "$later" || rg_item check-fp.without-9074 FAILED "$(basename $later) does not reverse"
  done
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9074 < "$(ls $S/le/$P/patches/LibreELEC-settings-9074-*.patch)"; then
    rg_log "$L" $RG_PY $H/ux/check_fp_test.py $S/without-9074/resources/lib
    [ "$(rg_exit $L)" != 0 ] && rg_item check-fp.without-9074 PASSED "it fails without 9074: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item check-fp.without-9074 FAILED "it passes without 9074"
  else rg_item check-fp.without-9074 FAILED "9074 does not reverse"; fi
  rg_rm $S/without-9074
  # Hades: the card "3D: HDR" of Check this setup (hades_check.py and hades_check_ui.py of LibreELEC-settings-9097,
  # with stand-ins for Kodi; the file of each display's answer against Kodi's Stereo3DVideo.h), and the same with 9097
  # reversed, which must fail
  L=$RG_W/logs/check-hdr3d.log
  rg_log "$L" $RG_PY $H/ux/check_hdr3d_test.py $S/src/resources/lib "$(kodi)"
  rg_item check-hdr3d "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  rg_fresh $S/without-9097 && cp -a $S/src/. $S/without-9097/
  L=$RG_W/logs/check-hdr3d.without-9097.log
  for later in $(settings_after 9097); do
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9097 < "$later" || rg_item check-hdr3d.without-9097 FAILED "$(basename $later) does not reverse"
  done
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9097 < "$(ls $S/le/$P/patches/LibreELEC-settings-9097-*.patch)"; then
    rg_log "$L" $RG_PY $H/ux/check_hdr3d_test.py $S/without-9097/resources/lib "$(kodi)"
    [ "$(rg_exit $L)" != 0 ] && rg_item check-hdr3d.without-9097 PASSED "it fails without 9097: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item check-hdr3d.without-9097 FAILED "it passes without 9097"
  else rg_item check-hdr3d.without-9097 FAILED "9097 does not reverse"; fi
  rg_rm $S/without-9097
  # Hades: the sentence of the player information as Check this setup says it, with the frames that Kodi lost in
  # steady playback (hades_check.py of LibreELEC-settings-9087 and Estuary's Variables.xml of the Kodi tree, with
  # stand-ins for Kodi), and the same with 9087 reversed, which must fail
  local K
  K=$(kodi)
  L=$RG_W/logs/check-verdict.log
  rg_log "$L" $RG_PY $H/ux/check_verdict_test.py $S/src/resources/lib "$K"
  rg_item check-verdict "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  rg_fresh $S/without-9087 && cp -a $S/src/. $S/without-9087/
  L=$RG_W/logs/check-verdict.without-9087.log
  for later in $(settings_after 9087); do
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9087 < "$later" || rg_item check-verdict.without-9087 FAILED "$(basename $later) does not reverse"
  done
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9087 < "$(ls $S/le/$P/patches/LibreELEC-settings-9087-*.patch)"; then
    rg_log "$L" $RG_PY $H/ux/check_verdict_test.py $S/without-9087/resources/lib "$K"
    [ "$(rg_exit $L)" != 0 ] && rg_item check-verdict.without-9087 PASSED "it fails without 9087: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item check-verdict.without-9087 FAILED "it passes without 9087"
  else rg_item check-verdict.without-9087 FAILED "9087 does not reverse"; fi
  rg_rm $S/without-9087
}
# the GL matrix stacks (CMatrixGLStack): every C++ source of the tree, no function pops a level that it did not push
# (a Pop() on a level pushed by a caller restores the caller's matrix in the middle of its drawing); and a copy of the
# tree whose OverlayRendererGLES.cpp has lost its first glMatrixModview.Push() must fail it, naming that file
stage_overlay() {
  local K L M f=xbmc/cores/VideoPlayer/VideoRenderers/OverlayRendererGLES.cpp v
  K=$(kodi)
  L=$RG_W/logs/matrix-balance.log
  rg_log "$L" $RG_PY "$H/overlay/matrix_balance.py" "$K"
  v=$(rg_pass "$L" '^[1-9][0-9]* uses of a matrix stack in functions checked, 0 unbalanced$' '^UNBALANCED')
  rg_item matrix-balance "$(rg_say "$v")" "$(rg_rest "$v")"
  M=$RG_W/matrix-mut
  rg_fresh "$M"
  cp -as "$K/." "$M/" || rg_fail "the tree of links"
  rm -f "$M/$f"
  if $RG_PY -c "import sys; s = open(sys.argv[1], encoding='utf-8').read(); o = 'glMatrixModview.Push();'; assert o in s; open(sys.argv[2], 'w', encoding='utf-8').write(s.replace(o, '', 1))" "$K/$f" "$M/$f"; then
    L=$RG_W/logs/matrix-balance-mutant.log
    rg_log "$L" $RG_PY "$H/overlay/matrix_balance.py" "$M"
    rg_item matrix-balance-mutant "$([ "$(rg_exit "$L")" = 1 ] && grep -a -q "^UNBALANCED: $f: " "$L" && echo PASSED || echo FAILED)" \
      "the first glMatrixModview.Push() of OverlayRendererGLES.cpp taken out: exit $(rg_exit "$L"); $(grep -a -m1 '^UNBALANCED' "$L" | cut -c1-140)"
  else rg_item matrix-balance-mutant FAILED "the mutant no longer applies: no glMatrixModview.Push(); in $f"; fi
  rg_rm "$M"
}

stage_stereo() {
  inputs || rg_fail "an input of inputs-kodi-host.tsv is missing"
  GT=$(gtest) || rg_fail "googletest of the build folder"
  check offsets aus depth autodepth autodepth_gl hdr3d_gl hdrdepth_gl stereo3d fpown_gl fpown_src guifbo_gl fallback2d pc3d
  must_fail autodepth-old-rule 3
  must_fail hdr3d_gl-old-rule 19
  must_fail stereo3d-old-rule 5
  # (issue #34: the right eye of each 3D layout, two frame sequences each; the base view alone taken for 3D)
  must_fail guifbo_gl-old-rule 9
  must_fail fallback2d-old-rule 1
  must_fail pc3d-old-rule 3
  mutants offsets aus depth autodepth autodepth_gl hdr3d_gl hdrdepth_gl stereo3d fpown_gl fpown_src guifbo_gl fallback2d pc3d
  mutants_clean
}
stage_chroma() {
  GLINC=$(rg_glinc) || rg_fail "no GLES headers in the build folder"
  check chroma_step chroma_src chroma_shader copy_dither gpu_disjoint disjoint_src
  mutants chroma_step chroma_src chroma_shader copy_dither gpu_disjoint disjoint_src
  mutants_clean
}

stage_capture() {
  GT=$(gtest) || rg_fail "googletest of the build folder"
  FFB=$(rg_ff_bridge) || rg_fail "the FFmpeg of the bridge"
  CEC=$(cecinc) || rg_fail "the libcec headers of the build folder"
  check capture-convert cec-keys video-frame videoframe_gl videoframe_src
  mutants capture-convert cec-keys video-frame videoframe_gl videoframe_src
  mutants_clean
}

stage_cefix() {
  GT=$(gtest) || rg_fail "googletest of the build folder"
  FF=$(rg_ff_bridge) || rg_fail "FFmpeg not built"
  # FreeType: the headers of its source in the build folder, the library of the host
  FTINC=$(ls -d "$REGRESS_LE_BUILD"/build/freetype-*/include 2> /dev/null | sort | tail -n 1)
  FTLIB=$(ls /usr/lib/x86_64-linux-gnu/libfreetype.so.6 2> /dev/null | head -n 1)
  local t="cefix-ae stereo-eye stereo-names font cefix-src"
  if [ -z "$FTINC" ] || [ -z "$FTLIB" ]; then
    rg_item font FAILED "no FreeType: headers '${FTINC:-none}', library '${FTLIB:-none}'"; t=${t/ font/}
  fi
  # shellcheck disable=SC2086
  check $t
  # shellcheck disable=SC2086
  mutants $t
  mutants_clean
}

stage_setup() {
  local P=packages/mediacenter/LibreELEC-settings ver sha tb S=$RG_W/setup L p bb
  inputs || rg_fail "an input of inputs-kodi-host.tsv is missing"
  check edidpath cachedefaults dvpathprior
  must_fail cachedefaults-old-rule 24
  must_fail dvpathprior-old-rule 3
  mutants edidpath cachedefaults dvpathprior
  mutants_clean
  # hades-diag's edid-path.awk with the image's busybox, on the tree of REGRESS_LE_REF
  bb=$(ls "$REGRESS_LE_BUILD"/install_pkg/busybox-*/usr/bin/busybox 2> /dev/null | head -n 1)
  rg_le_tree $RG_W/le packages/sysutils/hades-diag
  L=$RG_W/logs/edid-path-diag.log
  if [ -z "$bb" ]; then rg_item edid-path-diag FAILED "no busybox in $REGRESS_LE_BUILD/install_pkg"
  else
    rg_log "$L" bash $H/setup/edid_path_diag_test.sh "$bb" $RG_W/le/packages/sysutils/hades-diag $IN/edid/lg-c5-direct.bin \
      $IN/edid/lg-c5-sound-bar.bin $IN/edid/receiver-onkyo.bin $IN/edid/tv-lg-1.bin
    rg_item edid-path-diag "$(rg_say "$(rg_pass "$L" '^EDID-PATH-DIAG PASSED$' '^FAIL')")" \
      "$(rg_rest "$(rg_pass "$L" '^EDID-PATH-DIAG PASSED$' '^FAIL')")"
  fi
  # "Your display" and "Share my results" (LibreELEC-settings-9085) on the add-on of the recipe with every patch, and
  # the same with 9085 reversed, which must fail
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  tb=$(rg_source LibreELEC-settings "LibreELEC-settings-$ver.tar.gz")
  rg_fresh $S/src $S/without-9085
  rg_le_tree $S/le $P/patches
  if [ -z "$tb" ] || [ "$(sha256sum < "$tb" | cut -c1-64)" != "$sha" ]; then
    rg_item check-display FAILED "no LibreELEC-settings-$ver.tar.gz with PKG_SHA256 in REGRESS_SOURCES"; return 0
  fi
  tar -xzf "$tb" --strip-components=1 -C $S/src
  for p in $(ls $S/le/$P/patches/*.patch | sort); do
    patch -d $S/src -p1 -F0 -s --no-backup-if-mismatch < "$p" || { rg_item check-display FAILED "patch $(basename $p) does not apply"; return 0; }
  done
  L=$RG_W/logs/check-display.log
  rg_log "$L" $RG_PY $H/setup/check_display_test.py $S/src/resources/lib $IN/edid/lg-c5-direct.bin $IN/edid/lg-c5-sound-bar.bin
  rg_item check-display "$(rg_say "$(rg_pass "$L" '^ALL PASSED$')")" "$(rg_rest "$(rg_pass "$L" '^ALL PASSED$')") ($(grep -c '^ok:' $L) ok)"
  cp -a $S/src/. $S/without-9085/
  L=$RG_W/logs/check-display.without-9085.log
  # (every patch of the add-on numbered after 9085 reversed first, the last first: they change the same files)
  for p in $(ls $S/le/$P/patches/LibreELEC-settings-[0-9]*-*.patch | sort -r); do
    [ "$(basename "$p" | sed -E 's/^LibreELEC-settings-([0-9]+)-.*/\1/')" -gt 9085 ] || continue
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9085 < "$p" ||
      rg_item check-display.without-9085 FAILED "$(basename $p) does not reverse"
  done
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9085 < "$(ls $S/le/$P/patches/LibreELEC-settings-9085-*.patch)"; then
    rg_log "$L" $RG_PY $H/setup/check_display_test.py $S/without-9085/resources/lib $IN/edid/lg-c5-direct.bin \
      $IN/edid/lg-c5-sound-bar.bin
    [ "$(rg_exit $L)" != 0 ] && rg_item check-display.without-9085 PASSED "it fails without 9085: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
      rg_item check-display.without-9085 FAILED "it passes without 9085"
  else rg_item check-display.without-9085 FAILED "9085 does not reverse"; fi
  rg_rm $S/without-9085
  # LibreELEC-settings-9086 (a setting that the A/B comparison or the frame packing card could not put back, said as
  # one of the check; the second journal of "Your display" that cannot be written): the three tests with it reversed,
  # each of which must fail
  local t fails= failed=
  rg_fresh $S/without-9086 && cp -a $S/src/. $S/without-9086/
  if patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without-9086 < "$(ls $S/le/$P/patches/LibreELEC-settings-9086-*.patch)"; then
    for t in setup/check_display_test.py ux/check_ab_test.py ux/check_fp_test.py; do
      L=$RG_W/logs/$(basename $t .py).without-9086.log
      rg_log "$L" $RG_PY $H/$t $S/without-9086/resources/lib $IN/edid/lg-c5-direct.bin $IN/edid/lg-c5-sound-bar.bin
      [ "$(rg_exit $L)" != 0 ] && failed="$failed $(basename $t .py)" || fails="$fails $(basename $t .py)"
    done
    [ -z "$fails" ] && rg_item settings-9086.without PASSED "each fails without 9086:$failed" ||
      rg_item settings-9086.without FAILED "passes without 9086:$fails"
  else rg_item settings-9086.without FAILED "9086 does not reverse"; fi
  rg_rm $S/without-9086
}

# the network control of a projector (xbmc/network/Projector*): its policy, and its protocols against simulators
stage_projector() {
  check projector_policy projector_link projector_src
  mutants projector_policy projector_link projector_src
  mutants_clean
}

stage_permemory() {
  GT=$(gtest) || rg_fail "googletest of the build folder"
  FMT=$(ls -d "$REGRESS_LE_BUILD"/build/libfmt-*/include 2> /dev/null | sort | tail -n 1)
  [ -f "$FMT/fmt/format.h" ] || rg_fail "no fmt headers in $REGRESS_LE_BUILD"
  check permemory permemory-src
  mutants permemory permemory-src
  mutants_clean
}

stage_devchange() {
  GT=$(gtest) || rg_fail "googletest of the build folder"
  check ae-actor ae-devchange devchange-src
  mutants ae-actor ae-devchange devchange-src
  mutants_clean
}

rg_main "$STAGES" "$@"
