#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-mvhevc.sh: the host tests of MV-HEVC 3D in Hades (both views of an HEVC stream of two layers decoded by
# FFmpeg's own HEVC decoder, put side by side by Kodi's CMultiviewPacker), against the ITU-T MV-HEVC conformance
# bitstreams (H.265.1, the nine MVHEVCS_* archives of itu.int), each check in both directions where it is Kodi's:
#   ffmpeg   a small static host FFmpeg from the release tarball (SHA256 checked; FFmpeg 9.1 by default), offline
#   streams  the archives checked against their SHA256 (below) and unpacked
#   views    FFmpeg alone, opened as Kodi opens it (view_ids "-1", 1 and 8 frame threads): the MD5 of each view is
#            the reference decoder's for the streams of two views (A-C, E, F); D (views of two sizes) and G, H, I
#            (three views and more, which FFmpeg does not decode) give their base view as the reference
#   files    MP4 files with 'lhvC' (hvc1 and hev1) and a Matroska file made of one, by that FFmpeg: two views
#            paired in the MP4 files; the private data of each kept for the next stage
#   choice   mvhevc_choice_test.cpp on Kodi's MultiviewChoice.h (AddressSanitizer, UBSan), and its mutants
#   pack     mvhevc_pack_test.cpp on Kodi's MultiviewPacker.cpp and that FFmpeg, and its mutants: also the views as
#            NVIDIA's decoder gives them (NV12, P010), and the disparity adjustment of Apple's spatial video
#   mkv      mvhevc_mkv_test.cpp: Matroska files without the second view's sets in their private data, made of the
#            MP4 files of 'hev1' (the sets in band: found, both views as the reference) and of 'hvc1' (none)
#   note     mvhevc_note_test.cpp on Kodi's HadesPlayerInfo.h (what the player information says), and its mutants
#   depth    mvhevc_depth.cpp with Kodi's StereoDepth.cpp: the depth of the nearest thing measured on the two
#            views of A, B, C, E and F as Kodi measures it (each pair reduced to 480 x 270), against the same measure
#            twice as fine (960 x 540): found in most pairs, and within the margin of the subtitles (0.25 % of the
#            width) of the finer one
#   p20      a Dolby Vision profile 20 3D file made of stream A (mkp20.py: a profile 5 RPU of dovi_tool's test assets
#            last in each access unit, and a 'dvvC' of profile 20 with compatibility 0 in an MP4 of 'lhvC'), through
#            mvhevc_p20_test.cpp: Kodi's choice decodes both views where Dolby Vision is rebuilt in 3D and one view
#            elsewhere, both views carry the metadata of the RPU, Kodi's packer keeps them on the picture of the two,
#            the views are those of the same file without the RPU; and its mutants (profile 20 always one view, as before; the packer
#            dropping the metadata)
#   threads  frame threading of two layers (ffmpeg-9974, 9975, 9978, 9979): FFmpeg of this tree with and without them, every picture
#            (mvhevc_frames.c) with 1 to 16 frame threads against the one without it with 1 thread, on the conformance
#            streams, files, seeks, two x265 streams, streams made by mvhevc_au.py and the 22 single-layer HEVC
#            conformance streams of FATE, and those with a PPS again after the first slice; where the slices of the
#            base layer wait; and its mutants (stage_threads)
#   base     the base layer by a separate decoder (ffmpeg-9976, base_layer_decoder): FFmpeg of this tree with and without
#            it, the separate decoder in software standing in for a graphics card's (and repacked as NV12/P010 as a
#            card gives them): every picture of FFmpeg without it with 1 thread, 1-16 threads, on the streams of
#            threads and more (a stream of twenty times A, MP4 files, seeks); damaged streams with cleared buffers;
#            injected faults of the separate decoder (fault_separate.py); and the mutants of its checks
# Settings (environment):
#   MVHEVC_WORK    the work folder (required; nothing is written anywhere else)
#   MVHEVC_KODI    the Kodi tree under test (read only; required)
#   MVHEVC_ITU     the folder with the nine MVHEVCS_*.zip (required)
#   MVHEVC_FFMPEG  the FFmpeg release tarball (required); MVHEVC_FFMPEG_SHA256 its SHA256 (default: ffmpeg-9.1.tar.xz)
#   MVHEVC_RPU     for p20: profile5.bin of dovi_tool's test assets (MIT, quietvoid; dovi_tool 614c816b, SHA256 below)
#   MVHEVC_INPUTS  for threads and base: folders of inputs named by SHA256 (default REGRESS_INPUTS): the two x265 streams below and
#                  the single-layer conformance streams that ../ffmpeg/inputs-ffmpeg.tsv lists
#   MVHEVC_LE      for threads and base: the LibreELEC tree whose FFmpeg patches are built (default: the one of this runner)
#   MVHEVC_JOBS    for threads: jobs at once (default 8)
# usage: run-mvhevc.sh all | <stage>...     Prints MVHEVC PASSED or MVHEVC FAILED (n) at the end.
set -uo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
W=${MVHEVC_WORK:?set MVHEVC_WORK}
K=${MVHEVC_KODI:?set MVHEVC_KODI to the Kodi tree}
ITU=${MVHEVC_ITU:?set MVHEVC_ITU to the folder of the MVHEVCS archives}
FFT=${MVHEVC_FFMPEG:?set MVHEVC_FFMPEG to the FFmpeg release tarball}
FFSHA=${MVHEVC_FFMPEG_SHA256:-1a4d394ae63d190a526cd052eb4415c156b5c5e2df37efab41a7de0b8fde2433}
P=$W/ffmpeg
S=$W/streams
mkdir -p "$W/logs"
fails=0
item() { # <name> PASSED|FAILED <text>
  echo "$1: $2 $3" | tee -a "$W/logs/items.txt"
  [ "$2" = PASSED ] || fails=$((fails + 1))
}
CXX="nice -n 19 g++ -std=c++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"

# the archives (ITU-T H.265.1, MV-HEVC, as itu.int serves them on 2026-11-24)
SUMS="0f15f37b293112ae39017f99771ceeaa3f11a7812986bb9ca811bab3da7684e4  MVHEVCS_A_Qualcomm_3.zip
3a433642c470c432c6de2d82fa2f3a2fde02b7db69ca1c3b8e0070e9b12b7a6e  MVHEVCS_B_Sharp_3.zip
6562fd25e41470daa63c9c4cfab344d011fa7cdd0a032145e0a4dfa5a2b4a050  MVHEVCS_C_Sony_3.zip
98d4d97c1bd5bd9a4519a762f34bb3804df3bf23072bd2cdacfe0c287d3ff7f8  MVHEVCS_D_NTT_3.zip
a53b5d65d9e61f1c4b5f81514526d83c7b1e68497da5c3f4889a74812d1b7bde  MVHEVCS_E_Qualcomm_3.zip
94e3c3b6cedcfbe5f1c7649b7dda52a979cd26fd1ef1108ddfe51aaf3df7db62  MVHEVCS_F_Qualcomm_3.zip
a620a4092dd663d4906658acc6d7cb029d619e26ce5818b65f5daa057fbe8efd  MVHEVCS_G_NTT_3.zip
7a7c2dd044b337d999c601d190dd6ab6513ca359e7718daed65ba52cf8bb6e75  MVHEVCS_H_LGE_3.zip
170312273e9de6cfa62b20255c05fd1f820395afd3fe27f0f490eb7d86c0be9e  MVHEVCS_I_Nokia_3.zip"

stage_ffmpeg() {
  [ "$(sha256sum "$FFT" | cut -c1-64)" = "$FFSHA" ] || { item ffmpeg FAILED "$FFT is not $FFSHA"; return; }
  [ -x "$P/bin/ffmpeg" ] && [ -f "$P/.sha" ] && [ "$(cat "$P/.sha")" = "$FFSHA" ] && { item ffmpeg PASSED "built before"; return; }
  rm -rf "$W/ffsrc" "$P" && mkdir -p "$W/ffsrc" && tar --no-same-owner -xf "$FFT" -C "$W/ffsrc" || { item ffmpeg FAILED "unpack"; return; }
  local src
  src=$(ls -d "$W"/ffsrc/ffmpeg-*)
  (cd "$src" && unshare -rn ./configure --prefix="$P" --disable-everything --disable-autodetect --disable-doc \
    --disable-network --disable-x86asm --enable-static --disable-shared \
    --enable-decoder=hevc,rawvideo --enable-parser=hevc --enable-demuxer=hevc,matroska,mov \
    --enable-muxer=matroska,mp4,mov,null --enable-bsf=hevc_mp4toannexb,extract_extradata \
    --enable-protocol=file,pipe --enable-ffmpeg > "$W/logs/ff-configure.log" 2>&1 &&
    nice -n 19 unshare -rn make -j8 > "$W/logs/ff-make.log" 2>&1 && make install > "$W/logs/ff-install.log" 2>&1) ||
    { item ffmpeg FAILED "the build (logs/ff-*.log)"; return; }
  echo "$FFSHA" > "$P/.sha"
  rm -rf "$W/ffsrc"
  item ffmpeg PASSED "$("$P/bin/ffmpeg" -hide_banner -version | head -n 1)"
}

stage_streams() {
  local bad=0 z
  mkdir -p "$S"
  while read -r sum z; do
    [ "$(sha256sum "$ITU/$z" 2> /dev/null | cut -c1-64)" = "$sum" ] || { echo "$z: SHA256 differs or missing"; bad=$((bad + 1)); continue; }
    python3 -I -c 'import zipfile, sys, os
for i in zipfile.ZipFile(sys.argv[1]).infolist():
    n = os.path.basename(i.filename)
    if n and n.endswith((".bit", ".md5")):
        open(os.path.join(sys.argv[2], n), "wb").write(zipfile.ZipFile(sys.argv[1]).read(i))' "$ITU/$z" "$S"
  done <<< "$SUMS"
  [ $bad = 0 ] && item streams PASSED "9 archives, SHA256 as recorded" || item streams FAILED "$bad archives"
}

stage_views() {
  gcc -O1 -Wall -Wextra -Werror -I"$P/include" -o "$W/views" "$HERE/mvhevc_views.c" -L"$P/lib" \
    -lavformat -lavcodec -lavutil -lm -lpthread > "$W/logs/views-build.log" 2>&1 || { item views FAILED "build"; return; }
  local n t out base other e0 e1 bad=0
  for n in A B C D E F G H I; do
    for t in 1 8; do
      out=$("$W/views" "$S/MVHEVCS_$n.bit" $t 2> /dev/null)
      echo "$out" > "$W/logs/views-$n-$t.txt"
      base=$(echo "$out" | awk '$1 == "view" {print $6; exit}')
      other=$(echo "$out" | awk '$1 == "view" {n++; if (n == 2) print $6}')
      e0=$(awk '/_0\.yuv/ {print $1}' "$S/MVHEVCS_$n.md5")
      e1=$(awk '/_1\.yuv/ {print $1}' "$S/MVHEVCS_$n.md5")
      case $n in
        D | G | H | I)
          # (D with frame threads loses base pictures of refused packets: one thread only for it)
          [ $n = D ] && [ $t = 8 ] && continue
          [ "$base" = "$e0" ] && [ -z "$other" ] || { echo "$n/$t: base $base (want $e0), other '$other'"; bad=$((bad + 1)); } ;;
        *) [ "$base" = "$e0" ] && [ "$other" = "$e1" ] || { echo "$n/$t: $base $other, want $e0 $e1"; bad=$((bad + 1)); } ;;
      esac
    done
  done
  [ $bad = 0 ] && item views PASSED "A-C, E, F: both views as the reference with 1 and 8 threads; D, G, H, I: the base view" ||
    item views FAILED "$bad runs"
}

stage_files() {
  local f=$W/files bad=0 out
  rm -rf "$f" && mkdir -p "$f"
  for n in A E; do
    "$P/bin/ffmpeg" -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$S/MVHEVCS_$n.bit" -c copy -tag:v hvc1 \
      -disposition:v:0 +multilayer "$f/$n.mp4" || bad=$((bad + 1))
    "$P/bin/ffmpeg" -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$S/MVHEVCS_$n.bit" -c copy -tag:v hev1 \
      -disposition:v:0 +multilayer "$f/${n}_hev1.mp4" || bad=$((bad + 1))
    "$P/bin/ffmpeg" -hide_banner -loglevel error -y -i "$f/$n.mp4" -c copy "$f/$n.mkv" || bad=$((bad + 1))
    for x in "$n.mp4" "${n}_hev1.mp4"; do
      out=$("$W/views" "$f/$x" 4 -1 "$f/$x.extradata" 2> /dev/null)
      echo "$out" > "$W/logs/files-$x.txt"
      echo "$out" | grep -q '^disposition_multilayer 1$' && echo "$out" | awk '$1 == "pairs" && $2 > 0 && $4 == 0 {ok = 1} END {exit !ok}' ||
        { echo "$x: not two views paired"; bad=$((bad + 1)); }
    done
    "$W/views" "$f/$n.mkv" 1 -1 "$f/$n.mkv.extradata" > "$W/logs/files-$n.mkv.txt" 2> /dev/null || bad=$((bad + 1))
  done
  [ $bad = 0 ] && item files PASSED "MP4 (hvc1, hev1) of A and E: two views paired; Matroska made of them written" ||
    item files FAILED "$bad"
}

# mutant <stage> <name> <file under the Kodi tree> <old> <new>: a copy of that file with one rule changed in $W/mut/<name>
mutant() {
  local d=$W/mut/$2
  rm -rf "$d" && mkdir -p "$d/$(dirname "$3")"
  python3 -I - "$K/$3" "$d/$3" "$4" "$5" << 'PY' || return 3
import sys
src, dst, old, new = sys.argv[1:5]
t = open(src, encoding='utf-8').read()
if t.count(old) != 1:
    print('MUTANT NOT APPLIED: the text is there %d times: %r' % (t.count(old), old[:80]))
    sys.exit(3)
open(dst, 'w', encoding='utf-8').write(t.replace(old, new))
PY
}

choice_args() { echo "$S $W/files/A.mp4.extradata 1 $W/files/A_hev1.mp4.extradata 1 $W/files/E.mp4.extradata 1 $W/files/A.mkv.extradata none $W/files/E.mkv.extradata none"; }
stage_choice() {
  local V=xbmc/cores/VideoPlayer
  $CXX -I"$K/$V" -o "$W/choice" "$HERE/mvhevc_choice_test.cpp" > "$W/logs/choice-build.log" 2>&1 || { item choice FAILED "build"; return; }
  # shellcheck disable=SC2046
  "$W/choice" $(choice_args) > "$W/logs/choice.txt" 2>&1 && item choice PASSED "$(tail -n 1 "$W/logs/choice.txt")" ||
    item choice FAILED "$(grep -m3 FAIL "$W/logs/choice.txt" | tr '\n' ' ')"
  local M=$V/MultiviewChoice.h
  try_choice() { # <name> <old> <new>
    mutant choice "$1" "$M" "$2" "$3" || { item "choice-mutant-$1" FAILED "not applied"; return; }
    if ! $CXX -I"$W/mut/$1/$V" -o "$W/mut/$1/t" "$HERE/mvhevc_choice_test.cpp" > "$W/mut/$1/build.log" 2>&1; then
      item "choice-mutant-$1" FAILED "does not build"; return
    fi
    # shellcheck disable=SC2046
    if "$W/mut/$1/t" $(choice_args) > "$W/mut/$1/out.txt" 2>&1; then item "choice-mutant-$1" FAILED "NOT CAUGHT"
    else item "choice-mutant-$1" PASSED "caught: $(grep -c '^FAIL' "$W/mut/$1/out.txt") checks fail"; fi
  }
  try_choice sets-ignored '  if (!whole || !layer || !sps[*layer] || !pps[*layer])' '  if (!whole || !layer)'
  try_choice layers-not-two '  if (!baseInternal || !baseAvailable || maxLayersMinus1 != 1 ||' '  if (!baseInternal || !baseAvailable || maxLayersMinus1 < 1 ||'
  try_choice epb-kept '      if (zeros >= 2 && nal[i] == 3)' '      if (false && nal[i] == 3)'
  try_choice inverted-ignored '      return inverted ? BaseEye::RIGHT : BaseEye::LEFT;' '      return inverted ? BaseEye::LEFT : BaseEye::LEFT;'
  try_choice one-eye-taken '    case ContainerLayout::ONE_EYE:
    case ContainerLayout::OTHER:
      break;' '    case ContainerLayout::ONE_EYE:
      return BaseEye::LEFT;
    case ContainerLayout::OTHER:
      break;'
  try_choice stream-not-believed '  return stream == PictureEye::LEFT    ? BaseEye::LEFT' '  return false ? BaseEye::LEFT'
  try_choice p20-two-views '  return dvProfile == 20 && dvCompatibility == 0 && !rebuiltIn3D;' '  return dvProfile == 20 && dvCompatibility == 0 && !rebuiltIn3D && false;'
  try_choice p20-kept-before '  return dvProfile == 20 && dvCompatibility == 0 && !rebuiltIn3D;' '  return dvProfile == 20 && dvCompatibility == 0 && (rebuiltIn3D || true);'
  try_choice pace-no-grace '  if (pace.seen < PACE_GRACE)' '  if (false)'
  try_choice pace-not-in-a-row '  pace.slowWindows = pace.lateInWindow >= PACE_LATE_IN_WINDOW ? pace.slowWindows + 1 : 0;' '  pace.slowWindows = pace.lateInWindow >= PACE_LATE_IN_WINDOW ? pace.slowWindows + 1 : pace.slowWindows;'
  try_choice pace-judged-ignored '  if (!judged)
    return false;' '  if (!judged && false)
    return false;'
  try_choice work-never '  return work.slowWindows >= WORK_SLOW_WINDOWS;' '  return false;'
  try_choice work-no-margin '  work.slowWindows = work.rate < WORK_MARGIN * fps ? work.slowWindows + 1 : 0;' '  work.slowWindows = work.rate < 1.1 * fps ? work.slowWindows + 1 : 0;'
  try_choice work-not-in-a-row '  work.slowWindows = work.rate < WORK_MARGIN * fps ? work.slowWindows + 1 : 0;' '  work.slowWindows = work.rate < WORK_MARGIN * fps ? work.slowWindows + 1 : work.slowWindows;'
  try_choice work-no-skip '  if (work.skipped < WORK_SKIP)' '  if (false)'
  try_choice work-judged-ignored '  if (!judged || !(fps > 0.0))' '  if ((!judged && false) || !(fps > 0.0))'
  try_choice slow-any-file '  return !file.empty() && slow.file == file && slow.width == width && slow.height == height;' '  return !file.empty() && slow.width == width && slow.height == height;'
  try_choice slow-no-name '  return !file.empty() && slow.file == file && slow.width == width && slow.height == height;' '  return slow.file == file && slow.width == width && slow.height == height;'
  try_choice hevc-not-asked '  if (track.hevc)' '  if (false)'
  try_choice inband-sets-not-needed '        return !search.sps.empty() && !search.pps.empty() ? InBand::FOUND : InBand::NOT_THERE;' '        return InBand::FOUND;'
  try_choice inband-no-end '  return search.packets >= IN_BAND_PACKETS ? InBand::NOT_THERE : InBand::LOOKING;' '  return InBand::LOOKING;'
  try_choice nvdec-old-driver '  if (*driverSeries < NVDEC_TWO_VIEWS_DRIVER)' '  if (false)'
  try_choice nvdec-against-setting '  if (!hardwareAllowed)' '  if (!hardwareAllowed && false)'
  try_choice hybrid-against-setting '         : !hardwareAllowed                              ? HybridBase::NOT_ALLOWED' '         : !hardwareAllowed && false                     ? HybridBase::NOT_ALLOWED'
  try_choice hybrid-over-nvdec '         : nvdecAsked                                    ? HybridBase::NVDEC' '         : nvdecAsked && false                           ? HybridBase::NVDEC'
  try_choice hybrid-after-failure '         : failedBefore                                  ? HybridBase::FAILED_BEFORE' '         : failedBefore && false                         ? HybridBase::FAILED_BEFORE'
  try_choice hybrid-setting-ignored '         : wanted == HYBRID_NEVER                        ? HybridBase::TURNED_OFF' '         : wanted == HYBRID_NEVER && false               ? HybridBase::TURNED_OFF'
  try_choice hybrid-for-everyone '         : wanted == HYBRID_WHERE_SLOW && !processorSlow ? HybridBase::PROCESSOR_KEEPS_UP' '         : wanted == HYBRID_WHERE_SLOW && !processorSlow && false ? HybridBase::PROCESSOR_KEEPS_UP'
  try_choice hybrid-default-always 'inline constexpr int HYBRID_DEFAULT = HYBRID_WHERE_SLOW;' 'inline constexpr int HYBRID_DEFAULT = HYBRID_ALWAYS;'
  try_choice hybrid-tried-again '  return !hybridNow && !hybridTried && wouldBe == HybridBase::ASKED;' '  return !hybridNow && (!hybridTried || true) && wouldBe == HybridBase::ASKED;'
  try_choice hybrid-no-reopen '    return state.separate || mode == "separate" ? HybridStep::REOPEN : HybridStep::GIVEN_UP;' '    return HybridStep::GIVEN_UP;'
  try_choice hybrid-reopen-always '    return state.separate || mode == "separate" ? HybridStep::REOPEN : HybridStep::GIVEN_UP;' '    return HybridStep::REOPEN;'
  try_choice hybrid-given-up-picture-shown '    return state.separate || mode == "separate" ? HybridStep::REOPEN : HybridStep::GIVEN_UP;' '    return state.separate ? HybridStep::REOPEN : HybridStep::GIVEN_UP;'
  try_choice hybrid-not-said '  if (mode == "separate" && !state.separate)' '  if (mode == "separate" && false)'
  # the default of the setting is the one of the code (KODI::MULTIVIEW::HYBRID_DEFAULT)
  local hd sd
  hd=$(grep -o 'inline constexpr int HYBRID_DEFAULT = HYBRID_[A-Z_]*;' "$K/$M" | sed 's/.*= //; s/;//')
  hd=$(grep -o "inline constexpr int $hd = [0-9]*;" "$K/$M" | sed 's/.*= //; s/;//')
  sd=$(sed -n '/id="videoplayer.hades3dmvcbase"/,/<\/setting>/p' "$K/system/settings/settings.xml" | grep -o '<default>[0-9]*</default>' | tr -dc '0-9')
  [ -n "$hd" ] && [ "$hd" = "$sd" ] && item choice-hybrid-default PASSED "videoplayer.hades3dmvcbase defaults to $sd, as HYBRID_DEFAULT" ||
    item choice-hybrid-default FAILED "settings.xml $sd, HYBRID_DEFAULT $hd"
  try_choice disparity-sign '  return static_cast<int>(-steps * step);' '  return static_cast<int>(steps * step);'
}

stage_pack() {
  local V=xbmc/cores/VideoPlayer/DVDCodecs/Video STUB=$HERE/../kodi-host/fix/stubs
  build_pack() { # <dir of MultiviewPacker.cpp> <out>
    $CXX -I"$STUB" -I"$K/xbmc" -I"$K/$V" -I"$P/include" -o "$2" "$HERE/mvhevc_pack_test.cpp" "$1/MultiviewPacker.cpp" \
      -L"$P/lib" -lavformat -lavcodec -lavutil -lm -lpthread
  }
  local mp4=$W/files/A_hev1.mp4 m0 m1
  m0=$(awk '$1 == "view" {print $6; exit}' "$W/logs/files-A_hev1.mp4.txt")
  m1=$(awk '$1 == "view" {n++; if (n == 2) print $6}' "$W/logs/files-A_hev1.mp4.txt")
  build_pack "$K/$V" "$W/pack" > "$W/logs/pack-build.log" 2>&1 || { item pack FAILED "build"; return; }
  "$W/pack" "$S" "$mp4" "$m0" "$m1" > "$W/logs/pack.txt" 2>&1 && item pack PASSED "$(tail -n 1 "$W/logs/pack.txt")" ||
    item pack FAILED "$(grep -m3 FAIL "$W/logs/pack.txt" | tr '\n' ' ')"
  try_pack() { # <name> <old> <new>
    mutant pack "$1" "$V/MultiviewPacker.cpp" "$2" "$3" || { item "pack-mutant-$1" FAILED "not applied"; return; }
    build_pack "$W/mut/$1/$V" "$W/mut/$1/t" > "$W/mut/$1/build.log" 2>&1 || { item "pack-mutant-$1" FAILED "does not build"; return; }
    if "$W/mut/$1/t" "$S" "$mp4" "$m0" "$m1" > "$W/mut/$1/out.txt" 2>&1; then item "pack-mutant-$1" FAILED "NOT CAUGHT"
    else item "pack-mutant-$1" PASSED "caught: $(grep -c '^FAIL' "$W/mut/$1/out.txt") checks fail"; fi
  }
  try_pack eye-of-picture-ignored '    eye = EyeOfBaseView(m_baseEye, EyeOf(base));' '    eye = EyeOf(base) == PictureEye::LEFT ? m_baseEye : m_baseEye;'
  try_pack stereo3d-left '  av_frame_remove_side_data(m_packed, AV_FRAME_DATA_STEREO3D);' ''
  try_pack pace-not-reset '  m_pace = {};' ''
  try_pack halves-swapped '  const AVFrame* halves[2] = {BaseOnLeft(eye) ? base : second, BaseOnLeft(eye) ? second : base};' '  const AVFrame* halves[2] = {BaseOnLeft(eye) ? second : base, BaseOnLeft(eye) ? base : second};'
  try_pack hevc-decoder-none '  if (codecId != AV_CODEC_ID_HEVC)
    return nullptr;' '  return nullptr;'
  try_pack p010-not-converted '  return view == AV_PIX_FMT_P010LE ? AV_PIX_FMT_YUV420P10LE : view;' '  return view;'
  try_pack p010-bits '      out[x] = static_cast<uint16_t>(in[x - shift] >> 6);' '      out[x] = static_cast<uint16_t>(in[x - shift] >> 5);'
  try_pack p010-chroma-shift '      v[x] = static_cast<uint16_t>(in[2 * (x - chromaShift) + 1] >> 6);' '      v[x] = static_cast<uint16_t>(in[2 * x + 1] >> 6);'
  try_pack disparity-ignored '  const int leftShift = LeftEyeShift(m_disparity, base->width, 1 << desc->log2_chroma_w);' '  const int leftShift = 0;'
  try_pack disparity-one-way '    const int shift = half == 0 ? leftShift : -leftShift;' '    const int shift = leftShift;'
  try_pack edge-not-empty '      std::memcpy(edge + static_cast<ptrdiff_t>(x) * bytes, empty, bytes);' '      (void)edge;'
}

stage_mkv() {
  local V=xbmc/cores/VideoPlayer f=$W/files bad=0 n e0 e1
  $CXX -I"$K/$V" -I"$P/include" -o "$W/mkvtest" "$HERE/mvhevc_mkv_test.cpp" -L"$P/lib" -lavformat -lavcodec -lavutil     -lm -lpthread > "$W/logs/mkv-build.log" 2>&1 || { item mkv FAILED "build"; return; }
  for n in A E; do
    e0=$(awk '/_0\.yuv/ {print $1}' "$S/MVHEVCS_$n.md5")
    e1=$(awk '/_1\.yuv/ {print $1}' "$S/MVHEVCS_$n.md5")
    # (an MP4 file of 'hev1', its parameter sets in its samples too, into Matroska: they stay in its packets)
    "$P/bin/ffmpeg" -hide_banner -loglevel error -y -i "$f/${n}_hev1.mp4" -c copy "$f/${n}_inband.mkv" || bad=$((bad + 1))
    "$W/mkvtest" "$f/${n}_inband.mkv" found "$e0" "$e1" > "$W/logs/mkv-$n-inband.txt" 2>&1 ||
      { echo "$n in band: $(grep -m2 FAIL "$W/logs/mkv-$n-inband.txt")"; bad=$((bad + 1)); }
    "$W/mkvtest" "$f/$n.mkv" none > "$W/logs/mkv-$n-none.txt" 2>&1 ||
      { echo "$n none: $(grep -m2 FAIL "$W/logs/mkv-$n-none.txt")"; bad=$((bad + 1)); }
  done
  [ $bad = 0 ] && item mkv PASSED "A and E in Matroska made of hev1: sets in band found, both views as the reference; made of hvc1: not there, 2D" ||
    item mkv FAILED "$bad"
}

stage_note() {
  local U=xbmc/utils
  $CXX -I"$K/$U" -o "$W/note" "$HERE/mvhevc_note_test.cpp" > "$W/logs/note-build.log" 2>&1 || { item note FAILED "build"; return; }
  "$W/note" > "$W/logs/note.txt" 2>&1 && item note PASSED "$(tail -n 1 "$W/logs/note.txt")" ||
    item note FAILED "$(grep -m3 FAIL "$W/logs/note.txt" | tr '\n' ' ')"
  try_note() { # <name> <old> <new>
    mutant note "$1" "$U/HadesPlayerInfo.h" "$2" "$3" || { item "note-mutant-$1" FAILED "not applied"; return; }
    $CXX -I"$W/mut/$1/$U" -o "$W/mut/$1/t" "$HERE/mvhevc_note_test.cpp" > "$W/mut/$1/build.log" 2>&1 ||
      { item "note-mutant-$1" FAILED "does not build"; return; }
    if "$W/mut/$1/t" > "$W/mut/$1/out.txt" 2>&1; then item "note-mutant-$1" FAILED "NOT CAUGHT"
    else item "note-mutant-$1" PASSED "caught: $(grep -c '^FAIL' "$W/mut/$1/out.txt") checks fail"; fi
  }
  try_note slow-unsaid '  if (in.secondView == "slow")' '  if (in.secondView == "slow" && false)'
  try_note slow-a-note '    warn.push_back({Severity::WARN, "second_view", in.secondView, "system"});' '    note.push_back({Severity::NOTE, "second_view", in.secondView, "system"});'
  try_note dvipt-unsaid '  if (in.secondView == "dvipt")' '  if (in.secondView == "dvipt" && false)'
}

stage_depth() {
  local V=xbmc/cores/VideoPlayer/VideoRenderers n line pairs fa fw aa aw bad=0
  nice -n 19 g++ -std=c++20 -O2 -Wall -Wextra -Werror -I"$P/include" -I"$K/$V" -o "$W/depth" "$HERE/mvhevc_depth.cpp"     "$K/$V/StereoDepth.cpp" -L"$P/lib" -lavformat -lavcodec -lavutil -lm -lpthread > "$W/logs/depth-build.log" 2>&1 ||
    { item depth FAILED "build"; return; }
  for n in A B C E F; do
    "$W/depth" "$S/MVHEVCS_$n.bit" > "$W/logs/depth-$n.txt" 2>&1
    line=$(grep '^summary' "$W/logs/depth-$n.txt")
    echo "$n: $line"
    read -r pairs fa fw aa aw <<< "$(echo "$line" | awk '{print $3, $5, $6, $8, $9}')"
    # found in nine pairs of ten or more, and within the margin of the finer measure in nine of ten found
    if [ -z "$pairs" ] || [ "$pairs" -lt 2 ] || [ $((fw * 10)) -lt $((pairs * 9)) ] || [ $((aw * 10)) -lt $((fw * 9)) ] ||
      { [ "$fa" -gt 0 ] && [ $((aa * 10)) -lt $((fa * 9)) ]; }; then
      echo "$n: not as it must be"; bad=$((bad + 1))
    fi
  done
  [ $bad = 0 ] && item depth PASSED "A, B, C, E, F: the nearest thing found, within 0.25 % of the measure twice as fine" ||
    item depth FAILED "$bad streams (logs/depth-*.txt)"
}

RPUSHA=f2e6a33cdcad3bbe2be1baf0af6ff53a41dc63431477d29d138dbe4167dc13f2
stage_p20() {
  local V=xbmc/cores/VideoPlayer D=xbmc/cores/VideoPlayer/DVDCodecs/Video STUB=$HERE/../kodi-host/fix/stubs f=$W/files
  local rpu=${MVHEVC_RPU:-} e0 e1
  [ -n "$rpu" ] && [ "$(sha256sum "$rpu" | cut -c1-64)" = "$RPUSHA" ] || { item p20 FAILED "MVHEVC_RPU is not profile5.bin ($RPUSHA)"; return; }
  mkdir -p "$f"
  # (the halves of the same file made without the RPU, in the order mvhevc_views lists them: stage files)
  e0=$(awk '$1 == "view" {print $6; exit}' "$W/logs/files-A.mp4.txt")
  e1=$(awk '$1 == "view" {n++; if (n == 2) print $6}' "$W/logs/files-A.mp4.txt")
  python3 -I "$HERE/mkp20.py" inject "$S/MVHEVCS_A.bit" "$rpu" "$f/A_p20.hevc" > "$W/logs/p20-make.log" 2>&1 &&
    "$P/bin/ffmpeg" -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$f/A_p20.hevc" -c copy -tag:v hvc1 \
      -disposition:v:0 +multilayer "$f/A_p20-nodv.mp4" >> "$W/logs/p20-make.log" 2>&1 &&
    python3 -I "$HERE/mkp20.py" dvvc "$f/A_p20-nodv.mp4" "$f/A_p20.mp4" 20 0 >> "$W/logs/p20-make.log" 2>&1 ||
    { item p20 FAILED "the file could not be made: $(tail -n 2 "$W/logs/p20-make.log" | tr '\n' ' ')"; return; }
  build_p20() { # <xbmc folder taken first (MultiviewChoice.h)> <dir of MultiviewPacker.cpp> <out>
    $CXX -I"$STUB" -I"$1" -I"$K/xbmc" -I"$2" -I"$P/include" -o "$3" "$HERE/mvhevc_p20_test.cpp" "$2/MultiviewPacker.cpp" \
      -L"$P/lib" -lavformat -lavcodec -lavutil -lm -lpthread
  }
  build_p20 "$K/xbmc" "$K/$D" "$W/p20" > "$W/logs/p20-build.log" 2>&1 || { item p20 FAILED "build"; return; }
  "$W/p20" "$f/A_p20.mp4" "$e0" "$e1" > "$W/logs/p20.txt" 2>&1 &&
    item p20 PASSED "$(grep -m1 '^views' "$W/logs/p20.txt"); $(tail -n 1 "$W/logs/p20.txt")" ||
    item p20 FAILED "$(grep -m3 FAIL "$W/logs/p20.txt" | tr '\n' ' ')"
  # the mutants: profile 20 kept to one view whatever the window system says (the rule before); the packer drops the
  # metadata of the base view
  try_p20() { # <name> <xbmc folder taken first> <dir of MultiviewPacker.cpp>
    build_p20 "$2" "$3" "$W/mut/$1/t" > "$W/mut/$1/build.log" 2>&1 || { item "p20-mutant-$1" FAILED "does not build"; return; }
    if "$W/mut/$1/t" "$f/A_p20.mp4" "$e0" "$e1" > "$W/mut/$1/out.txt" 2>&1; then item "p20-mutant-$1" FAILED "NOT CAUGHT"
    else item "p20-mutant-$1" PASSED "caught: $(grep -c '^FAIL' "$W/mut/$1/out.txt") checks fail"; fi
  }
  if mutant p20 kept-before "$V/MultiviewChoice.h" '  return dvProfile == 20 && dvCompatibility == 0 && !rebuiltIn3D;' \
    '  return dvProfile == 20 && dvCompatibility == 0 && (rebuiltIn3D || true);'; then
    try_p20 kept-before "$W/mut/kept-before/xbmc" "$K/$D"
  else item p20-mutant-kept-before FAILED "not applied"; fi
  if mutant p20 metadata-dropped "$D/MultiviewPacker.cpp" '  av_frame_remove_side_data(m_packed, AV_FRAME_DATA_STEREO3D);' \
    '  av_frame_remove_side_data(m_packed, AV_FRAME_DATA_STEREO3D);
  av_frame_remove_side_data(m_packed, AV_FRAME_DATA_DOVI_METADATA);'; then
    cp "$K/$D/MultiviewPacker.h" "$W/mut/metadata-dropped/$D/"
    try_p20 metadata-dropped "$K/xbmc" "$W/mut/metadata-dropped/$D"
  else item p20-mutant-metadata-dropped FAILED "not applied"; fi
}

# threads: frame threading of two layers (ffmpeg-9974, cinemaONE's; 9975, its debug line). FFmpeg of this tree (the archive of MVHEVC_FFMPEG with every
# FFmpeg patch of the image, in the recipe's order, but 9913, which needs h264-mvc/twin.sh and touches no HEVC code)
# and the same without ffmpeg-9974 and 9975, built for the host (C only, offline); mvhevc_frames.c gives the MD5 of every
# picture as Kodi opens the decoder. The tree's FFmpeg with 1, 2, 4, 8 and 16 frame threads (two runs of each above 1)
# must give, line for line, the pictures of the one without the patch with 1 thread for: the ITU streams A-C, E and F,
# MP4 files of A and E and two seeks in each, two x265 streams of two views (MVHEVC_INPUTS: 8 bits, one slice a
# picture, no WPP; 10 bits, four slices a picture and WPP), streams made of A, C, E, F and those by mvhevc_au.py (a
# start in the middle of a GOP, the second view missing in ten access units and to the end), skip_frame, explode, the
# base view alone, and the 22 single-layer HEVC conformance streams of FATE (MVHEVC_INPUTS), and those of them with a
# picture of more than one slice with their PPS sent again after its first slice (mvhevc_au.py latepps: setup ends at
# a NAL unit of the base layer there; ffmpeg-9978 sets nothing aside then, as upstream; given as whole access units,
# one packet each as in an MP4 or Matroska file, since FFmpeg's hevc parser would end an access unit at that PPS;
# their pictures must be those of the original stream, and ffmpeg-9974 without 9978 sets them aside). ffmpeg-9979
# (the POC of each layer's picture put back before its slices set aside are decoded) changes no picture of a
# conforming stream, where every picture of an access unit has one POC; no stream here has layers that disagree, so
# it has no mutant: these runs show only that it changes nothing else. Where upstream itself
# gives other pictures with frame threads (D,
# G, H, I; and the damaged streams: the base view missing in one access unit, a base-layer slice cut short or changed,
# each decoded into cleared buffers, "zero"), the same pictures as the one without the patch with the same threads.
# The change must set aside the slices (of both layers) with 2 threads or more in every access unit of the plain form
# (mvhevc_au.py info), and never with 1 thread, with the base view alone, or on a single-layer stream
# its mutants must fail. Needs the stages ffmpeg, streams, views and files before it (the MP4 files).
X265SUMS="2f2cd1840a35ee8c3a37e7fcf824fd55dca69b8ad671eb1912413d5e589cfe14  mv8-1080-nowpp.hevc
038201e0b274ce0357a71af78458cfab0a0ede036b811b0159b350ab88268da6  mv10-1080-s4.hevc"
thr_find() { # <sha256> <ext>: the input of that SHA256 in MVHEVC_INPUTS
  local d
  for d in ${MVHEVC_INPUTS:-${REGRESS_INPUTS:-}}; do
    [ -f "$d/$1.$2" ] && [ "$(sha256sum < "$d/$1.$2" | cut -c1-64)" = "$1" ] && { echo "$d/$1.$2"; return 0; }
  done
  return 1
}
thr_build() { # <dir> <ERE of patch names left out>: FFmpeg of the tree in <dir> (in-tree build, kept for the mutants)
  local D=$1 skip=$2 p
  rm -rf "$D" && mkdir -p "$D" && tar --no-same-owner -xf "$FFT" --strip-components=1 -C "$D" || return 1
  for p in "$LE"/packages/multimedia/ffmpeg/patches/postproc/*.patch "$LE"/packages/multimedia/ffmpeg/patches/libreelec/*.patch \
    $(ls "$LE"/projects/Generic/patches/ffmpeg/*.patch | sort); do
    case $(basename "$p") in ffmpeg-9913-*) continue ;; esac
    basename "$p" | grep -E -q -- "$skip" && continue
    patch -d "$D" -p1 -F0 -s --no-backup-if-mismatch < "$p" || { echo "PATCH FAILED $(basename "$p")"; return 1; }
  done
  (cd "$D" && unshare -rn ./configure --disable-everything --disable-autodetect --disable-doc --disable-network \
    --disable-x86asm --enable-static --disable-shared --disable-programs --enable-decoder=hevc --enable-parser=hevc \
    --enable-demuxer=hevc,mov --enable-protocol=file > configure.log 2>&1 &&
    nice -n 19 unshare -rn make -j"${MVHEVC_JOBS:-8}" > make.log 2>&1) || return 1
}
thr_frames() { # <ffmpeg dir> <out>
  gcc -O1 -Wall -Wextra -Werror -I"$1" -o "$2" "$HERE/mvhevc_frames.c" -L"$1/libavformat" -L"$1/libavcodec" \
    -L"$1/libavutil" -lavformat -lavcodec -lavutil -lm -lpthread
}
# thr_run <tool> <list> <out dir> <runs above 1 thread> <threads...>: each line of the list with each thread count
thr_run() {
  local tool=$1 list=$2 O=$3 runs=$4 label input views extra t r; shift 4
  mkdir -p "$O"
  while read -r label input views extra; do
    for t in "$@"; do
      for r in $(seq 1 "$runs"); do
        [ "$t" = 1 ] && [ "$r" -gt 1 ] && continue
        echo "$tool $input $t $views${extra:+ $extra} > $O/$label.$t.$r.txt 2> $O/$label.$t.$r.err"
      done
    done
  done < "$list" | xargs -P "${MVHEVC_JOBS:-8}" -I{} sh -c 'timeout 900 {}'
}
stage_threads() {
  local T=$W/threads TI=$W/threads/in LE=${MVHEVC_LE:-$(cd "$HERE/../../.." && pwd)} f n x m t r sum name label views
  local extra want bad=0 total=0 kept slices A="python3 -I $HERE/mvhevc_au.py"
  [ "$(sha256sum "$FFT" | cut -c1-64)" = "$FFSHA" ] || { item threads FAILED "$FFT is not $FFSHA"; return; }
  ls "$LE"/projects/Generic/patches/ffmpeg/ffmpeg-9974-*.patch > /dev/null 2>&1 || { item threads FAILED "no ffmpeg-9974 in $LE"; return; }
  [ -s "$W/files/A.mp4" ] && [ -s "$W/files/E.mp4" ] || { item threads FAILED "no MP4 files: run the stage files first"; return; }
  rm -rf "$T" && mkdir -p "$TI"
  # (without 9976, 9978 and 9979 too, which are made on 9974; kept has them, 9976's option off)
  { thr_build "$T/before" '^ffmpeg-997[45689]-' > "$W/logs/threads-build-before.log" 2>&1 && thr_frames "$T/before" "$T/frames-before" &&
    thr_build "$T/kept" '^$' > "$W/logs/threads-build-kept.log" 2>&1 && thr_frames "$T/kept" "$T/frames-kept"; } ||
    { item threads FAILED "the builds (logs/threads-build-*.log)"; return; }
  # the inputs: <label> <file> <views> [options]; "same" lines are compared with the same threads
  local L=$T/list.txt LS=$T/same.txt
  : > "$L"; : > "$LS"
  for n in A B C E F; do echo "itu-$n $S/MVHEVCS_$n.bit -1" >> "$L"; done
  for n in D G H I; do echo "itu-$n $S/MVHEVCS_$n.bit -1" >> "$LS"; done
  for n in A E; do
    echo "mp4-$n $W/files/$n.mp4 -1" >> "$L"
    echo "seek50-$n $W/files/$n.mp4 -1 seek 50" >> "$L"
    echo "seek20-$n $W/files/$n.mp4 -1 seek 20" >> "$L"
  done
  while read -r sum name; do
    f=$(thr_find "$sum" hevc) || { item threads FAILED "$name ($sum) is not in MVHEVC_INPUTS"; return; }
    cp "$f" "$TI/$name"
    echo "x265-${name%.hevc} $TI/$name -1" >> "$L"
  done <<< "$X265SUMS"
  for x in A:$S/MVHEVCS_A.bit C:$S/MVHEVCS_C.bit E:$S/MVHEVCS_E.bit F:$S/MVHEVCS_F.bit N8:$TI/mv8-1080-nowpp.hevc \
    S10:$TI/mv10-1080-s4.hevc; do
    n=${x%%:*}; f=${x#*:}
    { $A midgop "$f" "$TI/midgop-$n.bit" > /dev/null && $A dropview "$f" "$TI/dropview-$n.bit" 5 15 &&
      $A dropview "$f" "$TI/dropview-tail-$n.bit" 20 1000 && $A dropbase "$f" "$TI/dropbase-$n.bit" 7 &&
      $A cut "$f" "$TI/cut-$n.bit" 9 60 && $A cut "$f" "$TI/cut-early-$n.bit" 13 10 && $A flip "$f" "$TI/flip-$n.bit" 11; } ||
      { item threads FAILED "mvhevc_au.py on $f"; return; }
    for m in midgop dropview dropview-tail; do echo "$m-$n $TI/$m-$n.bit -1" >> "$L"; done
    for m in dropbase cut cut-early flip; do echo "$m-$n $TI/$m-$n.bit -1 zero" >> "$LS"; done
  done
  echo "skipnonref-A $S/MVHEVCS_A.bit -1 skip 8" >> "$L"
  echo "skipbidir-N8 $TI/mv8-1080-nowpp.hevc -1 skip 16" >> "$L"
  echo "explode-A $S/MVHEVCS_A.bit -1 explode" >> "$L"
  echo "unset-A $S/MVHEVCS_A.bit none" >> "$L"
  local n1
  n1=$(wc -l < "$L")
  grep 'fate-suite/hevc-conformance/' "$HERE/../ffmpeg/inputs-ffmpeg.tsv" | while IFS=$'\t' read -r sum _ name _; do
    f=$(thr_find "$sum" bit) && echo "single-$(basename "$name" .bit) $f none" || echo "MISSING $name"
  done >> "$L"
  grep -q '^MISSING' "$L" && { item threads FAILED "single-layer streams not in MVHEVC_INPUTS: $(grep -c '^MISSING' "$L")"; return; }
  [ $(($(wc -l < "$L") - n1)) = 22 ] || { item threads FAILED "not 22 single-layer conformance streams"; return; }
  # the single-layer streams with a picture of more than one slice, their PPS sent again after its first slice
  : > "$T/late.txt"
  while read -r label f views extra; do
    case $label in single-*) ;; *) continue ;; esac
    m=$($A latepps "$f" "$TI/latepps-${label#single-}.aus") || { item threads FAILED "mvhevc_au.py latepps on $f"; return; }
    if [ "${m%% *}" -gt 0 ]; then echo "single-latepps-${label#single-} $TI/latepps-${label#single-}.aus none" >> "$T/late.txt"
    else rm -f "$TI/latepps-${label#single-}.aus"; fi
  done < "$L"
  [ "$(wc -l < "$T/late.txt")" -ge 3 ] || { item threads FAILED "$(wc -l < "$T/late.txt") single-layer streams with a picture of more than one slice"; return; }
  cat "$T/late.txt" >> "$L"
  cat "$L" "$LS" > "$T/all.txt"
  thr_run "$T/frames-before" "$L" "$T/before" 1 1
  thr_run "$T/frames-before" "$LS" "$T/before" 1 1 2 4 8 16
  thr_run "$T/frames-kept" "$T/all.txt" "$T/kept" 2 1 2 4 8 16
  while read -r label f views extra; do
    grep -q '^eof' "$T/before/$label.1.1.txt" || { echo "$label: the reference did not end"; bad=$((bad + 1)); continue; }
    for t in 1 2 4 8 16; do
      want=$T/before/$label.1.1.txt
      grep -q "^$label " "$LS" && want=$T/before/$label.$t.1.txt
      for r in 1 2; do
        [ "$t" = 1 ] && [ "$r" = 2 ] && continue
        total=$((total + 1))
        cmp -s "$want" "$T/kept/$label.$t.$r.txt" || { echo "$label: $t threads (run $r) differ"; bad=$((bad + 1)); }
      done
    done
  done < "$T/all.txt"
  [ $bad = 0 ] && item threads PASSED "$total runs of $(wc -l < "$T/all.txt") streams with 1-16 threads: the pictures of the decoder without ffmpeg-9974 to 9976 and 9978 with 1 thread ($(wc -l < "$LS") with the same threads, where upstream depends on them)" ||
    item threads FAILED "$bad of $total runs"
  # the kept path is taken where it is meant to and nowhere else
  bad=0
  for x in itu-A itu-B itu-C itu-E itu-F x265-mv8-1080-nowpp x265-mv10-1080-s4; do
    case $x in itu-*) f=$S/MVHEVCS_${x#itu-}.bit ;; *) f=$TI/${x#x265-}.hevc ;; esac
    # (the access units of the plain form, and all their slices, of both layers)
    kept=$($A info "$f" | awk '{print $6}')
    slices=$($A info "$f" | awk '{print $10}')
    for t in 2 4 8 16; do
      [ "$(grep -h '^deferred' "$T/kept/$x.$t.1.err" | cut -d' ' -f2-)" = "$kept $slices" ] ||
        { echo "$x, $t threads: $(grep -h '^deferred' "$T/kept/$x.$t.1.err"), want $kept AUs $slices slices"; bad=$((bad + 1)); }
    done
    grep -q '^deferred 0 0$' "$T/kept/$x.1.1.err" || { echo "$x: kept with 1 thread"; bad=$((bad + 1)); }
  done
  for x in unset-A $(grep -h '^single-' "$L" "$LS" | cut -d' ' -f1); do
    grep -q '^deferred 0 0$' "$T/kept/$x.8.1.err" || { echo "$x: kept"; bad=$((bad + 1)); }
  done
  # the streams with their PPS again: the pictures of the original stream, upstream with 1 thread (their MD5 in order)
  for x in $(cut -d' ' -f1 "$T/late.txt"); do
    cmp -s <(grep '^frame' "$T/before/single-${x#single-latepps-}.1.1.txt" | awk '{print $NF}') \
      <(grep '^frame' "$T/before/$x.1.1.txt" | awk '{print $NF}') || { echo "$x: not the original's pictures"; bad=$((bad + 1)); }
  done
  [ $bad = 0 ] && item threads-kept PASSED "slices set aside in every plain AU with 2-16 threads (all their slices), never with 1 thread or one layer" ||
    item threads-kept FAILED "$bad"
  # the mutants: the tree's FFmpeg with one edit in hevcdec.c, rebuilt, on the streams of two views, 1 and 8 threads
  grep -h -E '^(itu-[ACEF]|x265-|midgop-N8|dropview-S10|seek50-A|single-latepps-[^ ]*) ' "$L" "$LS" > "$T/mut.txt"
  try_threads() { # <name> <old> <new>
    local M=$T/mut-$1 out=0
    rm -rf "$M" && cp -a "$T/kept" "$M"
    python3 -I - "$M/libavcodec/hevc/hevcdec.c" "$2" "$3" << 'PY' || { item "threads-mutant-$1" FAILED "not applied"; rm -rf "$M"; return; }
import sys
p, old, new = sys.argv[1:4]
t = open(p, encoding='utf-8').read()
if t.count(old) != 1:
    print('MUTANT NOT APPLIED: the text is there %d times: %r' % (t.count(old), old[:80]))
    sys.exit(3)
open(p, 'w', encoding='utf-8').write(t.replace(old, new))
PY
    { (cd "$M" && nice -n 19 make -j"${MVHEVC_JOBS:-8}" > make-mutant.log 2>&1) && thr_frames "$M" "$M/frames"; } ||
      { item "threads-mutant-$1" FAILED "does not build"; rm -rf "$M"; return; }
    thr_run "$M/frames" "$T/mut.txt" "$M/out" 2 1 8
    while read -r label f views extra; do
      # (against upstream with 1 thread, or with the same threads where the list of those says so)
      for x in 1.1 8.1 8.2; do
        want=$T/before/$label.1.1.txt
        grep -q "^$label " "$LS" && want=$T/before/$label.${x%.*}.1.txt
        cmp -s "$want" "$M/out/$label.$x.txt" || out=$((out + 1))
      done
      # (and the access units and slices kept, as the tree's FFmpeg keeps them)
      [ "$(grep -h '^deferred' "$M/out/$label.8.1.err")" = "$(grep -h '^deferred' "$T/kept/$label.8.1.err")" ] || out=$((out + 1))
    done < "$T/mut.txt"
    [ $out -gt 0 ] && item "threads-mutant-$1" PASSED "caught: $out runs differ or keep nothing" ||
      item "threads-mutant-$1" FAILED "NOT CAUGHT"
    rm -rf "$M"
  }
  try_threads never-kept '        s->defer_slices = s->avctx->active_thread_type == FF_THREAD_FRAME &&' '        s->defer_slices = 0 && s->avctx->active_thread_type == FF_THREAD_FRAME &&'
  # (ffmpeg-9978: a single-layer stream whose setup ends at a parameter set after the first slice set aside again)
  try_threads single-layer-kept '                          s->finish_setup_nal_idx > nal_idx &&
                          s->pkt.nals[s->finish_setup_nal_idx].nuh_layer_id > 0;' '                          s->finish_setup_nal_idx > nal_idx;'
  try_threads header-not-restored '        s->sh = d->sh;
        if (d->own_short_term_rps)' '        if (d->own_short_term_rps)'
  rm -rf "$T/mut-"*
}

# base: the base layer by a separate decoder (ffmpeg-9976, the option base_layer_decoder): FFmpeg of this tree with
# and without it, built for the host (C only, offline), the separate decoder in software standing in for the graphics
# card's. With 1, 2, 4, 8 and 16 frame threads (two runs of each above 1), and the separate decoder's pictures as they
# are (separate) and repacked as a hardware decoder gives them in memory (separate_semiplanar: NV12, P010), every
# picture must be the one of FFmpeg without ffmpeg-9976 with 1 thread, on: the ITU streams A-C, E and F, MP4 files of
# A and E and two seeks in each, a stream of twenty times A and its MP4 file and a seek in it (with the windows of
# pictures compared after the first 8), the two x265 streams, the streams of mvhevc_au.py that start in the middle of
# a GOP or lose the second view, skip_frame, explode, and the 22 single-layer conformance streams and A without
# view_ids (the separate decoder given up: one layer); D, G, H and I with the same threads as the one without it.
# Damaged streams (the base view missing in one access unit, a base-layer slice cut short or changed): the parts of a
# damaged picture that no slice decodes are what the picture buffer held, in the separate decoder too, so they are
# compared with its buffers cleared as this decoder's are ("zero", zero_separate.py): the pictures of the one without
# it with 1 thread (the separate decoder runs one thread, and upstream itself gives other pictures with frame threads
# there). The separate decoder must have been used to the end in every stream of two views that is not damaged.
# Faults of the separate decoder (fault_separate.py): a wrong picture among the first 8 and in a later window: the
# pictures of the one without it, the fault found; a wrong picture after those (a control: it is used, as designed);
# a picture missing, and one out of step: found, and every picture not marked corrupt the reference's; the second view
# predicting its motion from the base picture (made to count so): found, the same; every picture wrong from a window
# on, with the pictures set up meanwhile in flight (8 threads), and a copy into a frame that fails (also of 10 bits):
# found, and every picture not marked corrupt the reference's (the pictures of the separate decoder returned after it
# was given up are marked as they are returned). No sample of any picture above its bit depth. And the mutants of the
# checks.
BASE_FAULT_WINDOW=482
stage_base() {
  local T=$W/base TI=$W/base/in LE=${MVHEVC_LE:-$(cd "$HERE/../../.." && pwd)} A="python3 -I $HERE/mvhevc_au.py"
  local f n x m t r b sum name label views extra want got bad=0 total=0 used=0 L LS LZ
  [ "$(sha256sum "$FFT" | cut -c1-64)" = "$FFSHA" ] || { item base FAILED "$FFT is not $FFSHA"; return; }
  ls "$LE"/projects/Generic/patches/ffmpeg/ffmpeg-9976-*.patch > /dev/null 2>&1 || { item base FAILED "no ffmpeg-9976 in $LE"; return; }
  [ -s "$W/files/A.mp4" ] && [ -s "$W/files/E.mp4" ] || { item base FAILED "no MP4 files: run the stage files first"; return; }
  rm -rf "$T" && mkdir -p "$TI"
  # (without 9978 too, whose lines are made on 9976's)
  { thr_build "$T/before" '^ffmpeg-997[68]-' > "$W/logs/base-build-before.log" 2>&1 && thr_frames "$T/before" "$T/frames-before" &&
    thr_build "$T/kept" '^$' > "$W/logs/base-build-kept.log" 2>&1 && thr_frames "$T/kept" "$T/frames-kept"; } ||
    { item base FAILED "the builds (logs/base-build-*.log)"; return; }
  # a copy of the build with an edit of baselayer.c (a test-only one, or a mutant), rebuilt: base_variant <dir> <cmd...>
  base_variant() {
    local D=$1; shift
    rm -rf "$D" && cp -a "$T/kept" "$D" && "$@" > "$D/edit.log" 2>&1 &&
      (cd "$D" && nice -n 19 make -j"${MVHEVC_JOBS:-8}" > make-variant.log 2>&1) && thr_frames "$D" "$D/frames"
  }
  base_variant "$T/zero" python3 -I "$HERE/zero_separate.py" "$T/zero" || { item base FAILED "the zero build"; return; }

  L=$T/list.txt; LS=$T/same.txt; LZ=$T/zero.txt; : > "$L"; : > "$LS"; : > "$LZ"
  for n in A B C E F; do echo "itu-$n $S/MVHEVCS_$n.bit -1" >> "$L"; done
  for n in D G H I; do echo "itu-$n $S/MVHEVCS_$n.bit -1" >> "$LS"; done
  for n in A E; do
    echo "mp4-$n $W/files/$n.mp4 -1" >> "$L"
    echo "seek50-$n $W/files/$n.mp4 -1 seek 50" >> "$L"
    echo "seek20-$n $W/files/$n.mp4 -1 seek 20" >> "$L"
  done
  for r in $(seq 20); do cat "$S/MVHEVCS_A.bit"; done > "$TI/long-A.bit"
  "$P/bin/ffmpeg" -nostdin -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$TI/long-A.bit" -c copy -tag:v hvc1 \
    -disposition:v:0 +multilayer "$TI/long-A.mp4" || { item base FAILED "long-A.mp4"; return; }
  echo "long-A $TI/long-A.bit -1" >> "$L"; echo "longmp4-A $TI/long-A.mp4 -1" >> "$L"
  echo "longseek-A $TI/long-A.mp4 -1 seek 60" >> "$L"
  while read -r sum name; do
    f=$(thr_find "$sum" hevc) || { item base FAILED "$name ($sum) is not in MVHEVC_INPUTS"; return; }
    cp "$f" "$TI/$name"
    echo "x265-${name%.hevc} $TI/$name -1" >> "$L"
  done <<< "$X265SUMS"
  for r in 1 2 3 4; do cat "$TI/mv8-1080-nowpp.hevc"; done > "$TI/long-N8.hevc"
  "$P/bin/ffmpeg" -nostdin -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$TI/long-N8.hevc" -c copy -tag:v hvc1     -disposition:v:0 +multilayer "$TI/long-N8.mp4" || { item base FAILED "long-N8.mp4"; return; }
  echo "longmp4-N8 $TI/long-N8.mp4 -1" >> "$L"
  "$P/bin/ffmpeg" -nostdin -hide_banner -loglevel error -y -fflags +genpts -r 24 -i "$TI/mv10-1080-s4.hevc" -c copy -tag:v hvc1 \
    -disposition:v:0 +multilayer "$TI/mv10.mp4" || { item base FAILED "mv10.mp4"; return; }
  echo "mp4-S10 $TI/mv10.mp4 -1" >> "$L"
  for x in A:$S/MVHEVCS_A.bit C:$S/MVHEVCS_C.bit E:$S/MVHEVCS_E.bit F:$S/MVHEVCS_F.bit N8:$TI/mv8-1080-nowpp.hevc \
    S10:$TI/mv10-1080-s4.hevc; do
    n=${x%%:*}; f=${x#*:}
    { $A midgop "$f" "$TI/midgop-$n.bit" > /dev/null && $A dropview "$f" "$TI/dropview-$n.bit" 5 15 &&
      $A dropview "$f" "$TI/dropview-tail-$n.bit" 20 1000 && $A dropbase "$f" "$TI/dropbase-$n.bit" 7 &&
      $A cut "$f" "$TI/cut-$n.bit" 9 60 && $A cut "$f" "$TI/cut-early-$n.bit" 13 10 && $A flip "$f" "$TI/flip-$n.bit" 11; } ||
      { item base FAILED "mvhevc_au.py on $f"; return; }
    for m in midgop dropview dropview-tail; do echo "$m-$n $TI/$m-$n.bit -1" >> "$L"; done
    for m in dropbase cut cut-early flip; do echo "$m-$n $TI/$m-$n.bit -1 zero" >> "$LZ"; done
  done
  echo "skipnonref-A $S/MVHEVCS_A.bit -1 skip 8" >> "$L"
  echo "skipbidir-N8 $TI/mv8-1080-nowpp.hevc -1 skip 16" >> "$L"
  echo "explode-A $S/MVHEVCS_A.bit -1 explode" >> "$L"
  echo "unset-A $S/MVHEVCS_A.bit none" >> "$L"
  n=$(wc -l < "$L")
  grep 'fate-suite/hevc-conformance/' "$HERE/../ffmpeg/inputs-ffmpeg.tsv" | while IFS=$'\t' read -r sum _ name _; do
    f=$(thr_find "$sum" bit) && echo "single-$(basename "$name" .bit) $f none" || echo "MISSING $name"
  done >> "$L"
  grep -q '^MISSING' "$L" && { item base FAILED "single-layer streams not in MVHEVC_INPUTS"; return; }
  [ $(($(wc -l < "$L") - n)) = 22 ] || { item base FAILED "not 22 single-layer conformance streams"; return; }

  # the references, then every run (one list of command lines, run MVHEVC_JOBS at once)
  base_runs() { # <tool> <list> <out> <base mode or -> <threads...>
    local tool=$1 list=$2 O=$3 mode=$4 label input views extra t r; shift 4
    mkdir -p "$O"
    while read -r label input views extra; do
      for t in "$@"; do for r in 1 2; do
        [ "$t" = 1 ] && [ "$r" = 2 ] && continue
        [ "$mode" = - ] && [ "$r" = 2 ] && continue
        echo "$tool $input $t $views${extra:+ $extra}$([ "$mode" = - ] || echo " base $mode") > $O/$label.$t.$r.txt 2> $O/$label.$t.$r.err"
      done; done
    done < "$list"
  }
  {
    base_runs "$T/frames-before" "$L" "$T/ref" - 1
    base_runs "$T/frames-before" "$LZ" "$T/ref" - 1
    base_runs "$T/frames-before" "$LS" "$T/ref" - 1 2 4 8 16
    for b in separate separate_semiplanar; do
      base_runs "$T/frames-kept" "$L" "$T/$b" $b 1 2 4 8 16
      base_runs "$T/frames-kept" "$LS" "$T/$b" $b 1 2 4 8 16
      base_runs "$T/zero/frames" "$LZ" "$T/$b-zero" $b 1 2 4 8 16
      base_runs "$T/frames-kept" "$LZ" "$T/$b-plain" $b 1 4 16
    done
  } | xargs -P "${MVHEVC_JOBS:-8}" -I{} sh -c 'timeout 900 {}'
  for b in separate separate_semiplanar; do
    while read -r label f views extra; do
      for t in 1 2 4 8 16; do for r in 1 2; do
        [ "$t" = 1 ] && [ "$r" = 2 ] && continue
        total=$((total + 1))
        want=$T/ref/$label.1.1.txt
        grep -q "^$label " "$LS" && want=$T/ref/$label.$t.1.txt
        got=$T/$b/$label.$t.$r
        grep -q "^$label " "$LZ" && got=$T/$b-zero/$label.$t.$r
        grep -q '^eof' "$want" || { echo "$label: the reference did not end"; bad=$((bad + 1)); continue; }
        cmp -s "$want" "$got.txt" || { echo "$label $b $t/$r: other pictures"; bad=$((bad + 1)); continue; }
        x=$(grep '^base ' "$got.err")
        case $label in
          single-* | unset-*) echo "$x" | grep -q 'separate 0 .* error only one layer is decoded$' ||
            { echo "$label $b $t/$r: not given up as one layer: $x"; bad=$((bad + 1)); } ;;
          itu-[ABCEF] | mp4-* | seek* | long* | x265-* | dropview* | midgop-[EFNS]*)
            echo "$x" | grep -q -- ' error -$' && ! echo "$x" | grep -q '^base separate 0 ' && used=$((used + 1)) ||
              { echo "$label $b $t/$r: the separate decoder was not used to the end: $x"; bad=$((bad + 1)); } ;;
        esac
      done; done
    done < <(cat "$L" "$LS" "$LZ")
    # damaged, the separate decoder's buffers as the pool left them: it ends, and the pictures before the damage are
    # the reference's (what follows is the separate decoder's concealment, as it would be a graphics card's)
    while read -r label f views extra; do
      for t in 1 4 16; do
        total=$((total + 1))
        grep -q '^eof' "$T/$b-plain/$label.$t.1.txt" || { echo "$label $b plain $t: did not end"; bad=$((bad + 1)); }
      done
    done < "$LZ"
  done
  [ $bad = 0 ] && item base PASSED "$total runs: the pictures of FFmpeg without ffmpeg-9976 with 1 thread (same threads for D, G, H, I); the separate decoder used to the end in $used runs of two views; one layer: given up" ||
    item base FAILED "$bad of $total runs"

  # faults: base_fault <name> <kind> <n> <input label> <expect: same|samemarks|differ|flagged> <error ERE>
  # (samemarks: with 1 thread the same; with 8 the same pictures, some marked corrupt: those of the separate decoder set up
  # before it was given up and returned after; flagged: every picture not marked corrupt the reference's, paired by the
  # timestamps of the input, so an input with timestamps)
  bad=0
  base_fault() {
    local D=$T/fault-$1 kind=$2 n=$3 in=$4 expect=$5 ere=$6 t ok=1 out
    base_variant "$D" python3 -I "$HERE/fault_separate.py" "$D" "$kind" "$n" || { item "base-fault-$1" FAILED "not built"; return; }
    f=$(awk -v l="$in" '$1 == l {print $2}' "$L")
    for t in 1 8; do
      timeout 300 "$D/frames" "$f" $t -1 base separate_semiplanar > "$D/run.$t.txt" 2> "$D/run.$t.err"
      x=$(grep '^base ' "$D/run.$t.err")
      case $expect in
        same) cmp -s "$T/ref/$in.1.1.txt" "$D/run.$t.txt" || { echo "fault $1 $t: other pictures"; ok=0; } ;;
        samemarks) if [ $t = 1 ]; then cmp -s "$T/ref/$in.1.1.txt" "$D/run.$t.txt"; else
            cmp -s <(sed 's/ corrupt$//' "$T/ref/$in.1.1.txt") <(sed 's/ corrupt$//' "$D/run.$t.txt"); fi ||
          { echo "fault $1 $t: other pictures"; ok=0; } ;;
        differ) cmp -s "$T/ref/$in.1.1.txt" "$D/run.$t.txt" && { echo "fault $1 $t: the same pictures"; ok=0; } ;;
        flagged | inflight) out=$(python3 -I "$HERE/mvhevc_base_check.py" "$T/ref/$in.1.1.txt" "$D/run.$t.txt") ||
          { echo "fault $1 $t: $out"; ok=0; } ;;
      esac
      # (inflight: with 8 threads some pictures of the separate decoder were set up before it was given up and returned
      # after, and are marked: the case the marking is there for is exercised)
      [ "$expect" = inflight ] && [ $t = 8 ] && ! echo "$x" | grep -E -q ' corrupt [1-9]' &&
        { echo "fault $1 $t: no picture in flight was marked: $x"; ok=0; }
      echo "$x" | grep -E -q -- "error $ere" || { echo "fault $1 $t: $x (want error $ere)"; ok=0; }
      grep -q -x 'range 0' "$D/run.$t.err" || { echo "fault $1 $t: samples above the bit depth ($(grep '^range ' "$D/run.$t.err"))"; ok=0; }
      echo "fault $1 $t: $x${out:+; $out}" >> "$W/logs/base-faults.txt"
    done
    [ $ok = 1 ] && item "base-fault-$1" PASSED "$kind at picture $n of $in: $expect" || item "base-fault-$1" FAILED "$kind $n"
  }
  : > "$W/logs/base-faults.txt"
  base_fault checked flip 3 long-A same "its picture of POC [0-9]+ is not this decoder's"
  base_fault window flip $BASE_FAULT_WINDOW long-A samemarks "its picture of POC [0-9]+ is not this decoder's"
  base_fault used flip 200 long-A differ "-$"
  base_fault missing drop 200 longmp4-A flagged "the separate decoder gave"
  base_fault stale stale 200 longmp4-A flagged "the separate decoder gave"
  base_fault inter inter 0 longmp4-N8 flagged "the layer above the base predicts its motion"
  base_fault inflight flipfrom $BASE_FAULT_WINDOW longmp4-A inflight "its picture of POC [0-9]+ is not this decoder's"
  base_fault fillfail fillfail 30 longmp4-A flagged "its picture of POC [0-9]+ could not be copied in"
  base_fault fillfail10 fillfail 1 mp4-S10 flagged "its picture of POC [0-9]+ could not be copied in"

  # mutants of the checks, each with the fault it is there for: base_mutant <name> <old> <new> <fault kind or -> <n>
  # <input> <same or flagged> <the error ERE>: caught where the run does not give what the fault must give
  base_mutant() {
    local D=$T/mut-$1 out=0 t
    base_variant "$D" python3 -I - "$D/libavcodec/hevc/baselayer.c" "$2" "$3" << 'PY' || { item "base-mutant-$1" FAILED "not applied"; return; }
import sys
p, old, new = sys.argv[1:4]
t = open(p, encoding='utf-8').read()
if t.count(old) != 1:
    print('MUTANT NOT APPLIED: the text is there %d times: %r' % (t.count(old), old[:80]))
    sys.exit(3)
open(p, 'w', encoding='utf-8').write(t.replace(old, new))
PY
    if [ "$4" != - ]; then
      python3 -I "$HERE/fault_separate.py" "$D" "$4" "$5" > /dev/null && (cd "$D" && make -j"${MVHEVC_JOBS:-8}" > make-fault.log 2>&1) &&
        thr_frames "$D" "$D/frames" || { item "base-mutant-$1" FAILED "fault not applied"; return; }
    fi
    f=$(awk -v l="$6" '$1 == l {print $2}' "$L")
    for t in 1 8; do
      timeout 120 "$D/frames" "$f" $t -1 base separate_semiplanar > "$D/run.$t.txt" 2> "$D/run.$t.err"
      # caught where what the fault (or none) must give is not given: its pictures, or its error
      case $7 in
        same) cmp -s "$T/ref/$6.1.1.txt" "$D/run.$t.txt" || { out=$((out + 1)); continue; } ;;
        flagged) python3 -I "$HERE/mvhevc_base_check.py" "$T/ref/$6.1.1.txt" "$D/run.$t.txt" > /dev/null ||
          { out=$((out + 1)); continue; } ;;
      esac
      grep '^base ' "$D/run.$t.err" | grep -E -q -- "error $8" || out=$((out + 1))
      grep -q -x 'range 0' "$D/run.$t.err" || out=$((out + 1))
    done
    [ $out -gt 0 ] && item "base-mutant-$1" PASSED "caught: $out of 2 runs" || item "base-mutant-$1" FAILED "NOT CAUGHT"
    rm -rf "$D"
  }
  E1="its picture of POC [0-9]+ is not this decoder's"
  base_mutant fill-no-copy '        ff_hevc_base_layer_copy(f->f, s->base_unit.pic[s->base_pic].frame,
                                sps->width, sps->height) < 0) {' '        0) {' - 0 itu-A same '-$'
  base_mutant no-progress '        ff_progress_frame_report(&f->tf, INT_MAX);
}' '        ;
}' - 0 x265-mv8-1080-nowpp same '-$'
  base_mutant compare-blind '    if (plane >= 0) {' '    if (0) {' flip 3 long-A same "$E1"
  base_mutant trusted-at-once '    } else if (state == STATE_CHECKING) {' '    } else if (0) {' flip 3 long-A same "$E1"
  base_mutant no-window '        bl->window_left  = HEVC_BASE_LAYER_WINDOW;' '        bl->window_left  = 0;' flip $BASE_FAULT_WINDOW long-A same "$E1"
  base_mutant poc-blind 'if (u->pic[i].frame && u->pic[i].poc == s->poc) {' 'if (u->pic[i].frame) {' stale 200 longmp4-A flagged 'the separate decoder gave'
  base_mutant motion-blind '    return col_lacks_motion(s);
}' '    return 0;
}' drop 200 longmp4-A flagged 'the separate decoder gave'
  base_mutant returned-blind '    if (!atomic_load(&v->given_up))
        return 0;' '    if (1)
        return 0;' flipfrom $BASE_FAULT_WINDOW longmp4-A flagged "$E1"
  base_mutant returned-blind-fill '    if (!atomic_load(&v->given_up))
        return 0;' '    if (1)
        return 0;' fillfail 30 longmp4-A flagged 'could not be copied in'
  base_mutant fill-bytes '                if (sps->pixel_shift) {
                    for (int x = 0; x < w; x++)
                        AV_WN16(row + 2 * x, grey);
                } else
                    memset(row, grey, w);' '                memset(row, 0x80, w << sps->pixel_shift);' fillfail 1 mp4-S10 flagged 'could not be copied in'
  base_mutant upper-blind '    if (s->base_mode == HEVC_BASE_LAYER_USED && picture_lacks_motion(s)) {' '    if (0) {' inter 0 longmp4-N8 flagged 'the layer above the base predicts its motion'
  # (the motion field of a picture of the separate decoder cleared, ff_hevc_base_layer_fill(): no mutant; in the
  # streams at hand a picture above the base takes the base picture of its access unit for its collocated one only
  # where that picture is intra and its own references are all inter-layer, long-term, so no motion vector is taken
  # from it whatever it holds; measured: leaving the field uncleared changes no picture of them)
  rm -rf "$T/fault-"* "$T/zero" "$T/before" "$T/kept"
}

[ "${1:-}" = all ] && set -- ffmpeg streams views files choice pack mkv note depth p20 threads base
: > "$W/logs/items.txt"
for s in "$@"; do "stage_$s"; done
rm -rf "$W/mut"
[ $fails = 0 ] && echo "MVHEVC PASSED" || echo "MVHEVC FAILED ($fails)"
[ $fails = 0 ]
