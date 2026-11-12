#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder, no network) each view of h264_mvc of a twin tree against the reference decoder (JM 19.1), picture by
# picture, with one thread, 2 and 8 frame threads and 8 slice threads; and the base view of the stock decoder.
# The comparison with the reference decoder: one run of each mode, killed after 120 s (a run that
# hangs says TIMEOUT). JM's decodes are cached as MD5 lists by stream SHA256.
# Usage: views.sh <twin> <stream.264>...      (MODES="1:frame 2:frame 8:frame 8:slice" by default)
set -uo pipefail
export LC_ALL=C
. "$(dirname "$0")/env.sh"
TW=$1; shift
T=$W/ffmpeg/$TW/ffmpeg-9.0.2
BASE=$W/ffmpeg/base/ffmpeg-9.0.2
C=$W/jmcache; mkdir -p $C
X=$(mktemp -d $W/tmp-views.XXXXXX); trap 'rm -rf "$X"' EXIT
MODES=${MODES:-"1:frame 2:frame 8:frame 8:slice"}
FF="timeout -s KILL ${TMO:-120} nice -n 19 $T/ffmpeg -nostdin -nostats -hide_banner -v error -noauto_conversion_filters"
size() { $BASE/ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$1" < /dev/null | head -n 1; }
jm_one() {
  local f=$1 sha d wh
  sha=$(sha256sum "$f" | cut -c1-64); d=$C/$sha
  if [ ! -s $d/done ]; then
    rm -rf $d; mkdir -p $d/run
    ( cd $d/run && nice -n 19 timeout 1800 $J/bin/ldecod -d $J/src/cfg/decoder.cfg -p InputFile="$f" -p OutputFile="$d/run/out.yuv" \
        -p RefFile="$d/run/none.yuv" -p DecodeAllLayers=1 -p Silent=1 > $d/log.txt 2>&1 < /dev/null; echo "ldecod exit $?" >> $d/log.txt )
    wh=$(size "$f"); local w=${wh%x*} h=${wh#*x} i=0
    for y in $(cd $d/run && ls *.yuv 2> /dev/null | sort); do
      [ -s $d/run/$y ] || continue
      python3 -I $TOOLS/picmd5.py $d/run/$y $w $h > $d/v$i.md5; i=$((i + 1))
      [ $i -lt 2 ] || break
    done
    rm -rf $d/run
    if grep -q '^ldecod exit 0$' $d/log.txt && [ $i -gt 0 ]; then echo "$wh" > $d/done; fi
  fi
  echo $d
}
for f in "$@"; do
  f=$(realpath "$f")
  n=$(basename "$f" .264)
  wh=$(size "$f"); w=${wh%x*}; h=${wh#*x}
  if [ -s "${f%.264}.jmparts" ]; then
    : > $X/r0; : > $X/r1
    for p in $(cat "${f%.264}.jmparts"); do d=$(jm_one "$(dirname "$f")/$p"); cat $d/v0.md5 >> $X/r0; cat $d/v1.md5 >> $X/r1 2> /dev/null; done
    ref="JM by parts ($(cat "${f%.264}.jmparts" | tr '\n' ' '))"
  else
    d=$(jm_one "$f"); cp $d/v0.md5 $X/r0; cp $d/v1.md5 $X/r1 2> /dev/null || : > $X/r1
    ref="JM ($(grep -c . $X/r0) + $(grep -c . $X/r1) pictures; $(tail -n 1 $d/log.txt))"
  fi
  echo "== $n ${w}x${h}: $ref"
  if [ -z "${NOSTOCK:-}" ]; then
    $FF -threads 1 -c:v h264 -i "$f" -f rawvideo -pix_fmt yuv420p -y $X/s.yuv 2> $X/s.err < /dev/null
    python3 -I $TOOLS/picmd5.py $X/s.yuv $w $h > $X/s.md5; rm -f $X/s.yuv
    echo "   stock h264, base view: $(python3 -I $TOOLS/cmp-pics.py $X/r0 $X/s.md5)"
  fi
  for v in 0 1; do
    for m in $MODES; do
      th=${m%%:*}; ty=${m##*:}
      s0=$(date +%s)
      $FF -threads $th -thread_type $ty -c:v h264_mvc -i "$f" -map 0:vidx:$v -f rawvideo -pix_fmt yuv420p -y $X/v.yuv 2> $X/v.err < /dev/null
      rc=$?; dt=$(( $(date +%s) - s0 ))
      python3 -I $TOOLS/picmd5.py $X/v.yuv $w $h > $X/v.md5 2> /dev/null; rm -f $X/v.yuv
      said=$(grep -v 'Last message repeated' $X/v.err | sed 's/^\[[^]]*\] //' | sort | uniq -c | sort -rn | head -n 2 | tr -s ' ' | tr '\n' ';' | cut -c1-150)
      t=""; [ $rc = 137 ] && t=" TIMEOUT (killed after ${dt} s)"
      echo "   h264_mvc view $v, $th $ty: $(python3 -I $TOOLS/cmp-pics.py $X/r$v $X/v.md5)$t${said:+ | said: $said}"
    done
  done
done
