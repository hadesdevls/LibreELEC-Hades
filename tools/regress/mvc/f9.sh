#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) finding 9: an inter-view reference list modification (modification_of_pic_nums_idc 4 or 5) whose
# abs_diff_view_idx_minus1 is outside the inter-view dependency list (H.7.4.3.1.1: 0 to num_*_refs - 1) is
# refused by the decoder, which says so, and is never narrowed to a negative int and wrapped into the list.
# The streams (craft.py viewmod-one, from the 960x540 stream of the reference encoder, one
# inter-view reference): (5, 1), (5, 2147483647) and (4, 4294967294) in one slice of the dependent view.
# With a twin built with UndefinedBehaviorSanitizer (name ending in -san), any report counts as a failure.
# Usage: f9.sh <twin>...
. "$(dirname "$0")/env.sh"
S=$W/streams
export UBSAN_OPTIONS=print_stacktrace=0 ASAN_OPTIONS=detect_leaks=0
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  fail=0
  for n in f9-bad-range:1 f9-bad-huge:2147483647 f9-bad-max:4294967294; do
    s=${n%%:*}; v=${n##*:}
    timeout -s KILL 300 $T/ffmpeg -nostdin -nostats -hide_banner -v error -threads 1 -c:v h264_mvc -i $S/$s.264 \
      -map 0:vidx:1 -f null - < /dev/null > /dev/null 2> $W/ffmpeg/f9-$TW.err
    said=$(grep -c "abs_diff_view_idx_minus1 $v out of range" $W/ffmpeg/f9-$TW.err)
    ub=$(grep -c 'runtime error:' $W/ffmpeg/f9-$TW.err)
    echo "$TW $s: refused with its value ($v): $([ $said -gt 0 ] && echo yes || echo NO); sanitizer reports $ub$([ $ub -gt 0 ] && echo " ($(grep -m1 'runtime error:' $W/ffmpeg/f9-$TW.err | sed 's|.*/ffmpeg-9.0.2/||' | cut -c1-140))")"
    [ $said -gt 0 ] && [ $ub = 0 ] || fail=1
  done
  [ $fail = 0 ] && echo "F9 $TW PASSED" || echo "F9 $TW FAILED"
done
