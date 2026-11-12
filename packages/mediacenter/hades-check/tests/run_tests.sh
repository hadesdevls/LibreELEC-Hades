#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
#
# Host tests of the clips of "Check this setup" (they do not run in the build): two generations of
# everything in folders of their own, check_clips_tests.py on them (the pictures, the folder, the sizes),
# with its mutants, and check_audio_tests.py (the sounds, with its own generations and mutants).
#
# usage: run_tests.sh --tools <dir> --ffmpeg-build <dir> --kodi <dir> --work <dir> [--python <python3>]
#                     [--gen1 <dir> --gen2 <dir>] [--only <checks>] [--no-mutants] [--video-only]
#   --tools        x265, ffmpeg and ffprobe for the build host, as x265:host and ffmpeg:host install
#                  them (${TOOLCHAIN}/lib/hades-check/bin)
#   --ffmpeg-build the folder where ffmpeg:host was built (${PKG_BUILD}/.${HOST_NAME} of ffmpeg): its
#                  headers and libraries are installed under --work, and the test programs are linked
#                  with them, the ones of those programs
#   --kodi         a Kodi tree of Hades (the bridge's and the audio engine's own code is compiled into
#                  the test programs)
#   --python       a Python 3 with numpy, for the tests (default: python3)
#   --gen1/--gen2  two generations made already (for instance the one of the build), instead of new ones
#   --only         some checks of the pictures only (as check_clips_tests.py --only takes them)
#   --video-only   the pictures only: no sounds made, and their tests not run
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
CLIPS=$(cd "$HERE/../clips" && pwd)
TOOLS= FFB= KODI= WORK= PY=python3 GEN1= GEN2= MUTANTS=--mutants ONLY= VIDEO_ONLY=
while [ $# -gt 0 ]; do
  case "$1" in
    --tools) TOOLS=$2; shift 2 ;;
    --ffmpeg-build) FFB=$2; shift 2 ;;
    --kodi) KODI=$2; shift 2 ;;
    --work) WORK=$2; shift 2 ;;
    --python) PY=$2; shift 2 ;;
    --gen1) GEN1=$2; shift 2 ;;
    --gen2) GEN2=$2; shift 2 ;;
    --only) ONLY=$2; shift 2 ;;
    --no-mutants) MUTANTS=; shift ;;
    --video-only) VIDEO_ONLY=--video-only; shift ;;
    *) echo "run_tests.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -n "$TOOLS" ] && [ -n "$FFB" ] && [ -n "$KODI" ] && [ -n "$WORK" ] || { echo "run_tests.sh: see the usage" >&2; exit 2; }
[ -f "$FFB/libavcodec/libavcodec.a" ] && [ -f "$FFB/ffbuild/config.mak" ] || { echo "run_tests.sh: $FFB is no build of ffmpeg:host" >&2; exit 2; }
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)

# FFmpeg's headers and static libraries, from the build of ffmpeg:host (which installs only the programs)
PREFIX=$(sed -n 's/^prefix=//p' "$FFB/ffbuild/config.mak")
rm -rf "$WORK/ffmpeg"
make -C "$FFB" install-headers install-libs DESTDIR="$WORK/ffmpeg" > "$WORK/ffmpeg-install.log"
FFPREFIX="$WORK/ffmpeg$PREFIX"
[ -f "$FFPREFIX/lib/libavutil.a" ] && [ -f "$FFPREFIX/include/libavutil/dovi_meta.h" ] ||
  { echo "run_tests.sh: FFmpeg's headers and libraries were not installed" >&2; exit 1; }

gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 -I"$KODI/tools/dvbridge" -I"$FFPREFIX/include" \
  -o "$WORK/dovi_compare" "$HERE/dovi_compare.c" "$KODI/tools/dvbridge/dvbridge_hdr10plus.c" \
  "$KODI/tools/dvbridge/dvbridge_probe.c" -L"$FFPREFIX/lib" -lavformat -lavcodec -lswresample -lavutil -lm -lpthread

for n in 1 2; do
  eval "g=\$GEN$n"
  if [ -z "$g" ]; then
    g="$WORK/gen$n"
    rm -rf "$g"
    sh "$CLIPS/make_clips.sh" --x265 "$TOOLS/x265" --ffmpeg "$TOOLS/ffmpeg" --ffprobe "$TOOLS/ffprobe" \
      --python "$PY" --out "$g" $VIDEO_ONLY
    eval "GEN$n=\$g"
  fi
done

rc=0
"$PY" -I -B "$HERE/check_clips_tests.py" --clips "$CLIPS" --x265 "$TOOLS/x265" --ffmpeg "$TOOLS/ffmpeg" \
  --ffprobe "$TOOLS/ffprobe" --dovi-compare "$WORK/dovi_compare" --kodi "$KODI" --work "$WORK/checks" \
  --gen1 "$GEN1" --gen2 "$GEN2" --only "$ONLY" $MUTANTS || rc=1

if [ -z "$VIDEO_ONLY" ]; then
  rm -rf "$WORK/audio"
  mkdir -p "$WORK/audio"
  "$PY" -I -B "$HERE/check_audio_tests.py" --make-audio "$CLIPS/make_audio.py" --ffmpeg "$TOOLS/ffmpeg" \
    --ffprobe "$TOOLS/ffprobe" --kodi "$KODI" --ffmpeg-prefix "$FFPREFIX" --work "$WORK/audio" || rc=1
fi
echo "run_tests.sh: $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
