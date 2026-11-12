#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) unit.sh <twin>...: the unit-level tests compiled against the headers of each twin tree and linked
# with its libraries: refs-test.c (findings 2 and 3) and f1reset.c (finding 1)
. "$(dirname "$0")/env.sh"
for TW in "$@"; do
  T=$W/ffmpeg/$TW/ffmpeg-9.0.2
  for t in ${TESTS:-refs-test f1reset}; do
    O=$W/ffmpeg/$TW/$t
    if ! gcc -O1 -iquote $T/libavcodec/h264mvc -iquote $T/libavcodec -I$T -include $T/libavcodec/h264mvc/h264mvc_rename.h \
         -o $O $TOOLS/$t.c $T/libavcodec/libavcodec.a $T/libavutil/libavutil.a -lm -lpthread ${LDX:-} > $O.cc.log 2>&1; then
      echo "== $TW $t: does not compile"; head -n 20 $O.cc.log; continue
    fi
    echo "== $TW $t"
    $O > $O.out 2>&1; rc=$?
    grep -v '^\[NULL @' $O.out
    echo "exit $rc"
  done
done
