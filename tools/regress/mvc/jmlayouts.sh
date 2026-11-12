#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) jmlayouts.sh <twin> <stream.264>...: each stream in packets of one access unit (asis) and in the
# layout that keeps the second view of a unit in the next packet (shifted), decoded by h264_mvc of the twin
# packet by packet (mvcmd5.c) with 1 and 8 frame threads and 8 slice threads, each view picture by picture
# against the reference decoder (JM 19.1; its decodes as views.sh caches them, by stream SHA256; a stream
# with a .jmparts file against the decodes of its parts, one after the other). JMLAYOUTS PASSED when every view
# of every run is JM's.
set -uo pipefail
export LC_ALL=C
. "$(dirname "$0")/env.sh"
TW=$1; shift
T=$W/ffmpeg/$TW/ffmpeg-9.0.2
O=$W/ffmpeg/jmlayouts-$TW; rm -rf $O; mkdir -p $O
( cd $T && gcc -O1 -I. ${LDX:-} -o $O/mvcmd5 $TOOLS/mvcmd5.c libavcodec/libavcodec.a libavutil/libavutil.a -lm -lpthread ) || exit 1
export ASAN_OPTIONS=detect_leaks=1:exitcode=86 UBSAN_OPTIONS=print_stacktrace=1
: > $O/san.txt
fail=0
cache() { # stream -> the cache folder of its JM decode (views.sh makes it)
  local d=$W/jmcache/$(sha256sum "$1" | cut -c1-64)
  [ -s $d/done ] || NOSTOCK=1 MODES=1:frame bash $TOOLS/views.sh $TW "$1" > /dev/null 2>&1
  echo $d
}
for f in "$@"; do
  f=$(realpath "$f"); n=$(basename $f .264)
  : > $O/r0; : > $O/r1
  if [ -s "${f%.264}.jmparts" ]; then
    for p in $(cat "${f%.264}.jmparts"); do d=$(cache "$(dirname $f)/$p"); cat $d/v0.md5 >> $O/r0; cat $d/v1.md5 >> $O/r1; done
  else
    d=$(cache $f); cp $d/v0.md5 $O/r0; cp $d/v1.md5 $O/r1
  fi
  line="$n:"
  for lay in asis shifted; do
    python3 -I $TOOLS/mvcpack.py $f $lay $O/$n.$lay.pkt > /dev/null
    for m in 1:frame 8:frame 8:slice; do
      timeout -s KILL 300 $O/mvcmd5 ${m%%:*} ${m##*:} $O/$n.$lay.pkt $O/out > /dev/null 2> $O/run.err < /dev/null
      rc=$?; [ $rc = 0 ] || { fail=1; line="$line | $lay ${m/:/ }: EXIT $rc"; }
      grep -E "runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer" $O/run.err | sed "s|^|$n $lay $m: |" >> $O/san.txt
      a=$(python3 -I $TOOLS/cmp-pics.py $O/r0 $O/out.v0.md5); b=$(python3 -I $TOOLS/cmp-pics.py $O/r1 $O/out.v1.md5)
      # a stream that JM decodes without a second view, and the decoder neither
      [ ! -s $O/r1 ] && [ ! -s $O/out.v1.md5 ] && b="no second view, as in JM"
      case "$a$b" in *DIFFERENT*|*NOTHING*|*NO-REF*) fail=1; line="$line | $lay ${m/:/ }: view 0 $a; view 1 $b" ;;
                     *) line="$line | $lay ${m/:/ } JM's" ;; esac
    done
  done
  echo "$line"
done
echo "sanitizer reports: $(grep -c . $O/san.txt)"; [ -s $O/san.txt ] && { fail=1; head -n 5 $O/san.txt; }
[ $fail = 0 ] && echo "JMLAYOUTS PASSED" || echo "JMLAYOUTS FAILED"
