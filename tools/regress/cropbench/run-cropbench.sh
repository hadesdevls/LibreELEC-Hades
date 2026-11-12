#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# LibreELEC Hades: the cases of cropbench (native Dolby Vision of pictures cropped to sizes that are not multiples of 4:
# 3840x1746, 1606, 1744, 1608, 1920x1038, 1036, a 3834-wide crop, "own" metadata, the converted transport, and the
# 4096-wide check) through the bridge of a Kodi tree on a software OpenGL ES (zink on lavapipe). TEST ONLY.
# Every input is a variable; those without a default are required (the data and tools are not part of this repository).
#   run-cropbench.sh [build|run|verdict|all]            (default all; about 25 min, 4 runs at a time, 3 GB each)
#   KODI_TREE   Kodi worktree under test (required); only tools/dvbridge is read
#   LP_PREFIX   host libplacebo, static, OpenGL, with the patches of this tree (required; the oracle builds one)
#   CB_DATA     the decoded layers of the test pictures, p<N>/ (required); CB_META its profile 8 RPU (required)
#   CB_GL, CB_FFMPEG, CB_FIDTOOLS, CB_VASTUB  the GL headers and wrapper (inc/, mkshim.py), the host FFmpeg prefix, the tools that
#               make the layers and the stub of <va/va.h> (required)
#   CB_OUT      work folder (default $HADES_REGRESS/cropbench, or $HOME/hades-regress/cropbench)
# Verdict, counted from the logs: every expected case has its CROPBENCH-OK line and no log has a FAIL line; the wide check
# says CROPBENCH-WIDE-DONE. A missing case is NOT-RUN and fails the verdict.
set -uo pipefail
export LC_ALL=C TZ=UTC
HERE=$(cd "$(dirname "$0")" && pwd)
KODI_TREE=${KODI_TREE:?set KODI_TREE to the Kodi worktree under test}
LP=${LP_PREFIX:?set LP_PREFIX to the host libplacebo prefix}
D=${CB_DATA:?set CB_DATA to the folder of the decoded layers}
P8=${CB_META:?set CB_META to the profile 8 RPU file}
O=${CB_OUT:-${HADES_REGRESS:-$HOME/hades-regress}/cropbench}
BLD=${BUILD:?set BUILD to the build folder of LibreELEC (the Vulkan headers are read from it)}
W=${CB_GL:?set CB_GL to the folder with the GL headers (inc/) and mkshim.py}; P=${CB_FFMPEG:?set CB_FFMPEG to the host FFmpeg prefix}
FT=${CB_FIDTOOLS:?set CB_FIDTOOLS to the folder of the layer tools}; VA=${CB_VASTUB:?set CB_VASTUB to the folder with the stub of va/va.h}
CASES="full:1:0:0:3840:2160 c1746:1:0:206:3840:1746 c1744:1:0:208:3840:1744 c1606:1:0:276:3840:1606 c1608:1:0:276:3840:1608 half:2:0:0:1920:1080 c1038:2:0:20:1920:1038 c1036:2:0:22:1920:1036 c3834:1:2:280:3834:1600 c1746own:1:0:206:3840:1746 fullconv:1:0:0:3840:2160 c1746conv:1:0:206:3840:1746"
mkdir -p "$O/runs"
build() {
  VK=$(ls -d $BLD/build/vulkan-headers-*/include | head -n 1)
  [ -f "$LP/lib/libplacebo.a" ] && [ -d "$KODI_TREE/tools/dvbridge" ] && [ -n "$VK" ] || { echo "BUILD-FAILED: no libplacebo ($LP), tree ($KODI_TREE) or Vulkan headers"; return 1; }
  B=$O/cb.obj; rm -rf -- "$B"; mkdir -p $B/tree
  cp -a $KODI_TREE/tools $B/tree/ || return 1
  git -C $KODI_TREE rev-parse --short=10 HEAD > $B/kodi-commit.txt
  S=$B/tree/tools/dvbridge
  python3 -I - "$S" "$B" <<'PY'
import pathlib, sys
s, o = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
BS, Q, NL = chr(92), chr(34), chr(10)
def lit(text):
    return "".join(Q + l.replace(BS, BS + BS).replace(Q, BS + Q) + BS + "n" + Q + NL for l in text.splitlines())
(o / "pack_sources.h").write_text("static const char PACK_VERTEX[] =" + NL + lit((s / "pack_gles300.vert").read_text()) + ";" + NL +
                                  "static const char PACK_FRAGMENT[] =" + NL + lit((s / "pack_gles300.frag").read_text()) + ";" + NL)
PY
  export CPATH=$VA
  CF="-std=gnu11 -O1 -g -Wall -Wno-unused-function -I$LP/include -I$P/include -I$W/inc -I$VK -I$S -I$B -I$FT"
  for f in $S/*.c; do b=$(basename $f .c)
    case $b in dvbridge_fel|dvbridge_dmabuf) continue;; esac
    gcc $CF -c $f -o $B/$b.o 2> $B/$b.warn || { cat $B/$b.warn; echo "BUILD-FAILED: $b.c"; return 1; }
  done
  cp $HERE/cropbench.c $B/test.c
  gcc $CF -c $B/test.c -o $B/test.o 2> $B/t.warn || { cat $B/t.warn; echo "BUILD-FAILED: cropbench.c"; return 1; }
  nm -u $B/*.o | awk '$1=="U" && $2 ~ /^(__real_)?gl[A-Z]/ {sub(/^__real_/, "", $2); print $2}' | sort -u > $B/gl-symbols.txt
  python3 -I $W/mkshim.py $W/inc/GLES3/gl32.h $W/inc/GLES2/gl2ext.h < $B/gl-symbols.txt > $B/gles_shim.c || return 1
  gcc $CF -c $B/gles_shim.c -o $B/gles_shim.o || return 1
  g++ -o $O/cropbench $B/*.o $LP/lib/libplacebo.a -L$P/lib -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl /usr/lib/x86_64-linux-gnu/libEGL.so.1 || { echo "BUILD-FAILED: link"; return 1; }
  echo "BUILD-DONE cropbench (Kodi $(cat $B/kodi-commit.txt); libplacebo $LP)"
}
one() { # <tag> <rpu> <step> <x0> <y0> <w> <h> [ENV=..]
  local t=$1; shift; local rpu=$1 st=$2 x=$3 yy=$4 w=$5 h=$6; shift 6
  env "$@" DV_MEM_LIMIT_MB=2500 MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_ALWAYS_SOFTWARE=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
    nice -n 19 timeout 1800 $O/cropbench $D ${PIC:-960} $rpu $O/runs $t $st $x $yy $w $h > $O/runs/$t.log 2> $O/runs/$t.err < /dev/null
  echo "exit $?" >> $O/runs/$t.log
}
run() {
  [ -x $O/cropbench ] || { echo "RUN-FAILED: no $O/cropbench (run build)"; return 1; }
  find "$O/runs" -maxdepth 1 -type f \( -name '*.log' -o -name '*.err' -o -name '*.rgba' \) -delete
  echo "start $(date -u +%T)"
  n=0
  for c in $CASES; do
    IFS=: read t st x y w h <<< "$c"
    rpu=$P8; env_=""; [ $t = c1746own ] && rpu=-
    case $t in fullconv|c1746conv) env_="CB_SOURCE=0";; esac
    one $t $rpu $st $x $y $w $h $env_ &
    n=$((n + 1)); [ $((n % 4)) = 0 ] && wait
  done
  wait
  env CB_WIDE=1 DV_MEM_LIMIT_MB=2500 MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_ALWAYS_SOFTWARE=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
    nice -n 19 timeout 600 $O/cropbench $D ${PIC:-960} $P8 $O/runs wide 1 0 0 2 2 > $O/runs/wide.log 2> $O/runs/wide.err < /dev/null; echo "exit $?" >> $O/runs/wide.log
  echo "end $(date -u +%T)"
}
verdict() {
  local p=0 f=0 r=0 t
  for c in $CASES; do t=${c%%:*}
    if [ ! -f $O/runs/$t.log ]; then echo "CROPBENCH-ITEM $t: NOT-RUN | no log"; r=$((r + 1))
    elif grep -a -qx "CROPBENCH-OK $t" $O/runs/$t.log && ! grep -a -q FAIL $O/runs/$t.log && grep -a -qx 'exit 0' $O/runs/$t.log; then echo "CROPBENCH-ITEM $t: PASSED | $(grep -a -m1 '^composed: packets' $O/runs/$t.log)"; p=$((p + 1))
    else echo "CROPBENCH-ITEM $t: FAILED | $(grep -a -E 'FAIL|exit' $O/runs/$t.log | head -n 2 | tr '\n' ' ')"; f=$((f + 1)); fi
  done
  if [ ! -f $O/runs/wide.log ]; then echo "CROPBENCH-ITEM wide: NOT-RUN | no log"; r=$((r + 1))
  elif grep -a -qx CROPBENCH-WIDE-DONE $O/runs/wide.log && ! grep -a -q FAIL $O/runs/wide.log && grep -a -qx 'exit 0' $O/runs/wide.log; then echo "CROPBENCH-ITEM wide: PASSED | 4096-wide checks"; p=$((p + 1))
  else echo "CROPBENCH-ITEM wide: FAILED | $(grep -a -E 'FAIL|exit' $O/runs/wide.log | head -n 2 | tr '\n' ' ')"; f=$((f + 1)); fi
  echo "CROPBENCH-DONE $((p + f + r)) items, $p passed, $f failed, $r not run"
  [ $f = 0 ] && [ $r = 0 ] && { echo "CROPBENCH-VERDICT PASSED"; return 0; }
  echo "CROPBENCH-VERDICT FAILED"; return 1
}
case ${1:-all} in
  build) build;; run) run;; verdict) verdict;;
  all) build && run; verdict;;
  *) echo "usage: run-cropbench.sh [build|run|verdict|all]"; exit 2;;
esac
