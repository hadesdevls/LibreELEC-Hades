#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) finding 8 on twin trees: f8parse.c (how much of the operation point table the parser reads) on the
# subset SPS of MVCDS1 (1 level value), f8-levels40 and f8-levels64; and the damaged count (f8-huge: 2^32 - 1
# operation points, nothing after): how long the parser takes, and what h264_mvc gives for the stream
# Usage: f8.sh <twin>...
. "$(dirname "$0")/env.sh"
S=$W/streams
mkdir -p $W/f8
I=$(find $W/itu -name MVCDS1.264)
[ -s $S/f8-huge.264 ] || python3 -I $TOOLS/craft22.py levels-huge $I $S/f8-huge.264
for s in $I $S/f8-levels40.264 $S/f8-levels64.264 $S/f8-huge.264; do
  n=$(basename $s .264)
  [ -s $W/f8/$n.sps ] || python3 -I $TOOLS/craft22.py spsbits $s $W/f8/$n.sps
done
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  O=$W/ffmpeg/$TW/f8parse
  gcc -O1 -iquote $T/libavcodec/h264mvc -iquote $T/libavcodec -I$T -include $T/libavcodec/h264mvc/h264mvc_rename.h \
    -o $O $TOOLS/f8parse.c $T/libavcodec/libavcodec.a $T/libavutil/libavutil.a -lm -lpthread > $O.cc.log 2>&1 || { echo "== $TW: f8parse does not compile"; head -n 20 $O.cc.log; continue; }
  echo "== $TW f8parse (valid tables)"
  $O $W/f8/MVCDS1.sps $W/f8/f8-levels40.sps $W/f8/f8-levels64.sps 2>&1 | sed "s|$W/f8/||"
  echo "== $TW f8parse (the damaged count), timed, killed after 120 s"
  s0=$(date +%s%N)
  timeout -s KILL 120 $O $W/f8/f8-huge.sps > $O.huge.txt 2>&1; rc=$?
  ms=$(( ($(date +%s%N) - s0) / 1000000 ))
  grep -v "^F8-PARSE" $O.huge.txt | sed "s|$W/f8/||" | grep -v '^\[NULL' ; grep '^\[NULL' $O.huge.txt | sed 's/^\[[^]]*\] //' | sort | uniq -c
  echo "exit $rc after $ms ms$([ $rc = 137 ] && echo ' (KILLED: still parsing)')"
  # the damaged count must be refused at once: the subset SPS is rejected (exit 1) within one second
  if [ $rc = 1 ] && [ $ms -lt 1000 ]; then echo "F8-HUGE PASSED"; else echo "F8-HUGE FAILED (exit $rc, $ms ms)"; fi
done
