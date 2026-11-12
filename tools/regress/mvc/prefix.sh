#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) finding 10: the prefix NAL units (type 14) that are exactly the four bytes of their header, and what
# h264_mvc of a twin tree says of them when it decodes the streams that have them (both views, one thread)
. "$(dirname "$0")/env.sh"
TW=$1; shift
T=$W/ffmpeg/$TW/ffmpeg-9.0.2
fail=0
for f in "$@"; do
  sizes=$(python3 -I $TOOLS/prefix-sizes.py $f)
  timeout -s KILL 300 $T/ffmpeg -nostdin -nostats -hide_banner -v warning -threads 1 -c:v h264_mvc -i $f \
    -map 0:vidx:0 -f null - -map 0:vidx:1 -f null - < /dev/null > /dev/null 2> $W/ffmpeg/prefix-$TW.err
  n=$(grep -c 'Failed to parse header of NALU (type 14)' $W/ffmpeg/prefix-$TW.err)
  [ "$n" = 0 ] || fail=1
  echo "$sizes | $TW: 'Failed to parse header of NALU (type 14)' $n times"
done
[ $fail = 0 ] && echo "PREFIX PASSED" || echo "PREFIX FAILED"
