#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder, no network) the separate MVC decoder driven as a player drives it, with seeks: packets of
# each stream of two views, in two layouts (mvcpack.py asis|shifted), handed to h264_mvc one by one by
# mvcflush (compiled against the libraries of the given twin tree), with a flush before packet 1, before the
# packet at a third and before the one at two thirds of the stream (one flush a run), all views asked for.
# For every run: did the decoder survive (exit status); and every picture that came out, is it the picture
# that the run without a flush gave for the same packet time and view (not damaged, not the view of another
# access unit)?
# Usage: flush.sh <twin folder name> [quick]
set -uo pipefail
export LC_ALL=C
. "$(dirname "$0")/env.sh" && W=$W/ffmpeg
TW=${1:-twin}
T=$W/$TW/ffmpeg-9.0.2
O=$W/flush-$TW; rm -rf $O; mkdir -p $O
cd $T || exit 1
gcc -O1 -I. -o $O/mvcflush $TOOLS/mvcflush.c libavcodec/libavcodec.a libavutil/libavutil.a -lm -lpthread > $O/cc.log 2>&1 || { cat $O/cc.log; exit 1; }
[ -d $ITU ] || { echo "ABORT: no conformance streams (run streams.sh first)"; exit 1; }
# the streams with a second view (the reference decoder gave one): read from the views comparison
streams=$( (find $ITU -iname '*.264'; ls $J/stereo/*.264 $J/stereo3/*.264 2> /dev/null) | sort)
MODES="1:frame 8:frame 16:frame 8:slice"
[ "${2:-}" = quick ] && MODES="1:frame 8:frame"
runs=0; crashes=0; wrong=0; pictures=0; streams_used=0
: > $O/summary.txt
: > $O/runs.txt
for f in $streams; do
  n=$(basename $f .264)
  python3 -I $TOOLS/mvcpack.py $f asis $O/$n.asis.pkt > $O/$n.npk 2> /dev/null || continue
  python3 -I $TOOLS/mvcpack.py $f shifted $O/$n.shifted.pkt > /dev/null 2>&1 || continue
  # the references: one thread, no flush, one for each layout (packet times differ between them)
  for lay in asis shifted; do
    $O/mvcflush h264_mvc 1 frame all $O/$n.$lay.pkt > $O/$n.ref-$lay.txt 2> $O/$n.ref-$lay.err < /dev/null
  done
  # only streams of which a second view comes out
  if ! grep -q ' view [1-9]' $O/$n.ref-asis.txt; then rm -f $O/$n.*; continue; fi
  streams_used=$((streams_used + 1))
  # the two layouts give, view by view, the same pictures in the same order
  same=$(python3 -I - $O/$n.ref-asis.txt $O/$n.ref-shifted.txt <<'PY2'
import sys
def seq(path):
    d = {}
    for l in open(path):
        p = l.split()
        if p[0] == "pts":
            d.setdefault(p[3], []).append((p[4], p[6], p[7]))
    return d
a, b = seq(sys.argv[1]), seq(sys.argv[2])
print("same" if a == b else "DIFFERENT views %s/%s pictures %s/%s" % (sorted(a), sorted(b), [len(v) for v in a.values()], [len(v) for v in b.values()]))
PY2
)
  [ "$same" = same ] || echo "LAYOUTS DIFFER: $n: $same" | tee -a $O/summary.txt
  np=$(cat $O/$n.npk)
  for lay in asis shifted; do
    for at in 1 $((np / 3)) $((2 * np / 3)); do
      for m in $MODES; do
        th=${m%%:*}; ty=${m##*:}
        timeout -s KILL 120 $O/mvcflush h264_mvc $th $ty all $O/$n.$lay.pkt $at > $O/run.txt 2> $O/run.err < /dev/null
        rc=$?
        runs=$((runs + 1))
        res=$(python3 -I - $O/$n.ref-$lay.txt $O/run.txt <<'PY'
import sys
ref = {}
for l in open(sys.argv[1]):
    p = l.split()
    if p[0] == "pts":
        ref[(p[1], p[3])] = (p[4], p[6], p[7], " ERRORS" in l)
n = bad = 0
for l in open(sys.argv[2]):
    p = l.split()
    if p[0] != "pts":
        continue
    n += 1
    r = ref.get((p[1], p[3]))
    if r is None or r[0] != p[4] or r[1] != p[6] or r[2] != p[7] or (" ERRORS" in l):
        bad += 1
print(n, bad)
PY
)
        set -- $res; got=${1:-0}; bad=${2:-0}
        echo "$n $lay $at $th $ty rc $rc got $got bad $bad" >> $O/runs.txt
        pictures=$((pictures + got)); wrong=$((wrong + bad))
        if [ $rc != 0 ]; then crashes=$((crashes + 1)); echo "CRASH rc $rc: $n $lay flush before $at, threads $th $ty: $(tail -n 2 $O/run.err | tr '\n' ' ' | cut -c1-200)" | tee -a $O/summary.txt
        elif [ "$bad" != 0 ]; then echo "WRONG: $n $lay flush before $at, threads $th $ty: $bad of $got pictures not the reference's" | tee -a $O/summary.txt; fi
      done
    done
  done
  rm -f $O/$n.*
done
echo "streams with a second view: $streams_used; runs: $runs; crashes: $crashes; pictures out: $pictures; not the reference's: $wrong" | tee -a $O/summary.txt
echo FLUSH-DONE
