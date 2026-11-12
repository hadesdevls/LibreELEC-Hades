#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder, no network) finding 1 end to end: the field-coded stream of two views (MVCICT-1, the only one of the
# ITU-T set whose slices of both views are fields) in packets of one access unit (one field of each view) and in
# the layout that keeps the second view of a unit in the next packet, handed to h264_mvc of each twin tree by
# mvcflush with one flush, before packet 2, 3, ... 80 in turn, one thread. Per run: exit status
# (134 = an assertion), and how many pictures are not, byte for byte, those of the run without a flush of the
# SHIPPED twin for that packet time and view.
# Usage: flushf1.sh <twin>...   (the first is the reference twin)
set -uo pipefail
export LC_ALL=C
. "$(dirname "$0")/env.sh"
F=$(find $W/itu -name MVCICT-1.264)
O=$W/ffmpeg/flushf1; mkdir -p $O
python3 -I $TOOLS/mvcpack.py $F asis $O/asis.pkt > $O/asis.n
python3 -I $TOOLS/mvcpack.py $F shifted $O/shifted.pkt > $O/shifted.n
REF=$1
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  ( cd $T && gcc -O1 -I. -o $O/mvcflush-$TW $TOOLS/mvcflush.c libavcodec/libavcodec.a libavutil/libavutil.a -lm -lpthread ) || exit 1
done
for lay in asis shifted; do
  $O/mvcflush-$REF h264_mvc 1 frame all $O/$lay.pkt > $O/ref-$lay.txt 2> /dev/null < /dev/null
done
for TW in "$@"; do
  aborts=0; crashes=0; wrong=0; runs=0; where=""
  for lay in asis shifted; do
    for at in $(seq 2 80); do
      timeout -s KILL 60 $O/mvcflush-$TW h264_mvc 1 frame all $O/$lay.pkt $at > $O/run.txt 2> $O/run.err < /dev/null
      rc=$?; runs=$((runs + 1))
      bad=$(python3 -I - $O/ref-$lay.txt $O/run.txt <<'PY'
import sys
ref = {}
for l in open(sys.argv[1]):
    p = l.split()
    if p[0] == "pts":
        ref[(p[1], p[3])] = (p[4], p[6], p[7])
bad = 0
for l in open(sys.argv[2]):
    p = l.split()
    if p[0] == "pts" and (ref.get((p[1], p[3])) != (p[4], p[6], p[7]) or " ERRORS" in l):
        bad += 1
print(bad)
PY
)
      wrong=$((wrong + bad))
      if [ $rc = 134 ]; then aborts=$((aborts + 1)); where="$where $lay@$at:abort($(grep -m1 -o 'Assertion.*' $O/run.err | cut -c1-80))"
      elif [ $rc != 0 ]; then crashes=$((crashes + 1)); where="$where $lay@$at:rc$rc"; fi
      echo "$TW $lay $at rc $rc bad $bad" >> $O/runs-$TW.txt
    done
  done
  echo "$TW: runs $runs, assertion aborts $aborts, other crashes $crashes, pictures not the shipped no-flush decode's $wrong"
  [ -n "$where" ] && echo "   where:$(echo $where | cut -c1-600)"
done
