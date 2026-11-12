#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) finding 6, what the pictures say of their view: f6-views (MVCDS1, then MVCDS1 again with its second
# view renamed 3 and its subset SPS and PPS given id 2) handed to h264_mvc packet by packet (mvcflush, one
# access unit a packet and the layout of the next packet), 1 and 8 frame threads: the view_id that each picture
# carries (AV_FRAME_DATA_VIEW_ID) must be the view it was decoded in: 580 of view 0, 290 of view 1, 290 of
# view 3. (The pictures themselves are compared with JM by views.sh.)
# Usage: f6.sh <twin>...
. "$(dirname "$0")/env.sh"
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  O=$W/ffmpeg/f6-$TW; rm -rf $O; mkdir -p $O
  ( cd $T && gcc -O1 -I. -o $O/mvcflush $TOOLS/mvcflush.c libavcodec/libavcodec.a libavutil/libavutil.a -lm -lpthread ) || exit 1
  fail=0
  for lay in asis shifted; do
    python3 -I $TOOLS/mvcpack.py $W/streams/f6-views.264 $lay $O/$lay.pkt > /dev/null
    for m in 1:frame 8:frame; do
      timeout -s KILL 300 $O/mvcflush h264_mvc ${m%%:*} ${m##*:} all $O/$lay.pkt > $O/out.txt 2> /dev/null < /dev/null
      got=$(awk '$1 == "pts" { n[$4]++ } END { for (v in n) printf "%s:%d ", v, n[v] }' $O/out.txt | tr ' ' '\n' | sort | tr '\n' ' ')
      ok=no; [ "$got" = "0:580 1:290 3:290 " ] && ok=yes
      [ $ok = yes ] || fail=1
      echo "$TW $lay $m: pictures by view_id $got$([ $ok = yes ] || echo ' (WRONG: want 0:580 1:290 3:290)')"
    done
  done
  [ $fail = 0 ] && echo "F6 $TW PASSED" || echo "F6 $TW FAILED"
done
