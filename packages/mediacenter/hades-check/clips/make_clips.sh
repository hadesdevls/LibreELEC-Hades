#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
#
# Makes the clips of "Check this setup" at build time, on the build host, into one folder: the
# pictures (gen_video.py: the Hades check card in SDR, HDR10, HLG, Dolby Vision 8.1 and 5, clips.json)
# and the sounds (make_audio.py), and a SHA256SUMS of everything made. Nothing of a third party goes in:
# the card, its font and the sounds are made here. The encoders run only here; none goes into the image.
#
# usage: make_clips.sh --x265 <x265> --ffmpeg <ffmpeg> --ffprobe <ffprobe> --python <python3> --out <dir>
#                      [--video-only]
# --video-only (for the tests of the pictures) leaves the sounds out; without it a missing make_audio.py
# stops the build, so that the sounds cannot go missing from the image unseen. The folder must be empty
# or not there: it holds what this run makes and nothing else.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
X265= FFMPEG= FFPROBE= PYTHON= OUT= VIDEO_ONLY=no
while [ $# -gt 0 ]; do
  case "$1" in
    --x265) X265=$2; shift 2 ;;
    --ffmpeg) FFMPEG=$2; shift 2 ;;
    --ffprobe) FFPROBE=$2; shift 2 ;;
    --python) PYTHON=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --video-only) VIDEO_ONLY=yes; shift ;;
    *) echo "make_clips.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
for v in X265 FFMPEG FFPROBE PYTHON OUT; do
  eval "val=\${$v}"
  [ -n "$val" ] || { echo "make_clips.sh: --$(echo $v | tr 'A-Z' 'a-z') is missing" >&2; exit 2; }
done
for p in "$X265" "$FFMPEG" "$FFPROBE" "$PYTHON"; do
  [ -x "$p" ] || { echo "make_clips.sh: $p cannot be run" >&2; exit 2; }
done
# (before any encoding, so that the build stops at once)
if [ "$VIDEO_ONLY" != yes ] && [ ! -f "$HERE/make_audio.py" ]; then
  echo "make_clips.sh: make_audio.py is missing: the sounds of the check would not be made" >&2
  exit 1
fi

export LC_ALL=C TZ=UTC
mkdir -p "$OUT"
# the folder holds what this run makes and nothing else
if [ -n "$(ls -A "$OUT")" ]; then
  echo "make_clips.sh: $OUT is not empty" >&2
  exit 2
fi

# (-B: no __pycache__ beside the scripts, in the package's folder, whose files make the build's stamp)
"$PYTHON" -I -B "$HERE/gen_video.py" --x265 "$X265" --ffprobe "$FFPROBE" --out "$OUT"
if [ "$VIDEO_ONLY" != yes ]; then
  "$PYTHON" -I -B "$HERE/make_audio.py" --ffmpeg "$FFMPEG" --ffprobe "$FFPROBE" --out "$OUT"
fi

# SHA256SUMS: every file made, by name, in the order of the C locale
SUMS=$(cd "$OUT" && find . -maxdepth 1 -type f ! -name 'SHA256SUMS*' | sed 's|^\./||' | sort | while read -r f; do
  sha256sum "$f"
done)
[ -n "$SUMS" ] || { echo "make_clips.sh: nothing was made" >&2; exit 1; }
printf '%s\n' "$SUMS" > "$OUT/SHA256SUMS"
