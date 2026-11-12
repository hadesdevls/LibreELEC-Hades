#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# black_run.sh: the black and the first codes of Dolby Vision sent as HDR10 and mapped to the display, against
# ITU-R BT.2390 (black_test.c), in its two directions, and that the bridge still gives both sides PQ black.
# Usage: black_run.sh <libplacebo prefix (include/, lib/libplacebo.a), as built for the image> <Kodi tree> [<work>]
# Exit 0 when every check passes.
set -u
export LC_ALL=C
LP=$1; KODI=$2; W=${3:-$(mktemp -d)}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W"
fail=0
gcc -std=gnu11 -O1 -Wall -Werror -I"$LP/include" -o "$W/black_test" "$HERE/black_test.c" "$LP/lib/libplacebo.a" \
  -lstdc++ -lm -pthread -ldl > "$W/build.log" 2>&1 || { cat "$W/build.log"; echo "BLACK-RUN: FAIL (build)"; exit 1; }
"$W/black_test" > "$W/bridge.txt"; rc=$?
tail -2 "$W/bridge.txt"
[ $rc -eq 0 ] || { echo "FAIL: with the bridge's black, a dark code moves"; fail=1; }
# the other direction: a black level of 0.005 cd/m2 on the display's side (what the bridge had before it gave both
# sides PQ black) must lift the dark codes
"$W/black_test" 0.005 > "$W/lifted.txt"; rc=$?
tail -2 "$W/lifted.txt"
[ $rc -eq 1 ] || { echo "FAIL: a black level of 0.005 cd/m2 is not seen"; fail=1; }
# the bridge sets both blacks so (map_to_display())
R=$KODI/tools/dvbridge/dvbridge_render.c
n1=$(grep -c 'source->hdr.min_luma = PL_COLOR_HDR_BLACK;' "$R")
n2=$(grep -c '\.hdr = {\.min_luma = PL_COLOR_HDR_BLACK, \.max_luma = m->peak,' "$R")
if [ "$n1" = 1 ] && [ "$n2" = 1 ]; then echo "ok: map_to_display() gives the source and the display PQ black"
else echo "FAIL: map_to_display() does not give both sides PQ black ($n1, $n2)"; fail=1; fi
echo "BLACK-RUN: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
