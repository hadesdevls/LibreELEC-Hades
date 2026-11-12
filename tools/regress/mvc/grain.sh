#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) a concern of the review that it left unverified: film grain synthesis skipped for the base view where
# the switch to the dependent view ends its picture (ff_h264_field_end(..., 1)). The stream of the reference
# encoder with a film grain SEI in every access unit (craft22.py grain): the base view out of h264_mvc of each
# twin, both views decoded (the second view to a null output), 1 and 8 frame threads, must be the stock decoder's, grain included (the MD5 of each decoded frame, the last column of framemd5: the timestamps of a run with two outputs differ).
# Usage: grain.sh <twin>...
. "$(dirname "$0")/env.sh"
S=$W/streams
[ -s $S/g-grain.264 ] || python3 -I $TOOLS/craft22.py grain $S/sd540-wrap.264 $S/g-grain.264
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  fail=0
  ref=$($T/ffmpeg -nostdin -v error -threads 1 -c:v h264 -i $S/g-grain.264 -f framemd5 - 2> /dev/null < /dev/null | grep -v '^#' | awk -F, '{ print $NF }' | md5sum | cut -c1-12)
  plain=$($T/ffmpeg -nostdin -v error -threads 1 -c:v h264 -export_side_data film_grain -i $S/g-grain.264 -f framemd5 - 2> /dev/null < /dev/null | grep -v '^#' | awk -F, '{ print $NF }' | md5sum | cut -c1-12)
  echo "$TW stock h264: with grain $ref, without (exported) $plain$([ $ref = $plain ] && echo ' (THE GRAIN CHANGES NOTHING: no test)')"
  [ $ref != $plain ] || fail=1
  for th in 1 8; do
    got=$($T/ffmpeg -nostdin -v error -threads $th -thread_type frame -c:v h264_mvc -i $S/g-grain.264 -map 0:vidx:0 -f framemd5 - -map 0:vidx:1 -f null /dev/null 2> $W/ffmpeg/grain-$TW.err < /dev/null | grep -v '^#' | awk -F, '{ print $NF }' | md5sum | cut -c1-12)
    echo "$TW h264_mvc base view (both views decoded), $th frame threads: $got $([ $got = $ref ] && echo '(the stock decoder'"'"'s)' || echo '(DIFFERENT)')"
    [ $got = $ref ] || fail=1
  done
  [ $fail = 0 ] && echo "GRAIN $TW PASSED" || echo "GRAIN $TW FAILED"
done
