#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) a git repository of the files that h264-mvc/twin.sh copies, as twin.sh stages them: FFmpeg 9.0.2
# with the image's patches (the base tree of host.sh), then each patch of the series (the parts for those
# files, as twin.sh cuts them), then each fix of the tree, one commit each. A new fix is written on top of it
# and exported with git format-patch, so that it applies to what twin.sh stages, without fuzz or offset.
# Checks that the staged files are the shipped twin's, but for the edits twin.sh makes after the fixes.
set -euo pipefail
. "$(dirname "$0")/env.sh"
P=$LT/packages/multimedia/ffmpeg/h264-mvc
FX=$W/fx
TWIN_C="h264dec.c h264_slice.c h264_refs.c h264_ps.c h264_picture.c h264_direct.c h264_parse.c
        h2645_parse.c h264_cabac.c h264_cavlc.c h264_loopfilter.c h264_mb.c h264_sei.c"
TWIN_H="h264dec.h h264_ps.h h264_parse.h h264_sei.h h2645_parse.h h264_mvpred.h
        h264_mb_template.c h264_mc_template.c"
export GIT_AUTHOR_NAME=Hades GIT_AUTHOR_EMAIL=15710513+hadesdevls@users.noreply.github.com
export GIT_COMMITTER_NAME=Hades GIT_COMMITTER_EMAIL=15710513+hadesdevls@users.noreply.github.com TZ=UTC
rm -rf $FX; mkdir -p $FX/libavcodec
for f in $TWIN_C $TWIN_H; do cp $W/ffmpeg/base/pristine-src/libavcodec/$f $FX/libavcodec/; done
cd $FX
git init -q -b main .
git config core.abbrev 12
git add -A; git commit -q -m "FFmpeg 9.0.2 with the patches of the image: the files twin.sh copies"
part_of() {
  awk -v list="$(echo $TWIN_C $TWIN_H)" '
    BEGIN { n = split(list, a, " "); for (i = 1; i <= n; i++) want["libavcodec/" a[i]] = 1; head = 1; keep = 0; any = 0 }
    /^diff --git a\// { head = 0; path = $3; sub(/^a\//, "", path); keep = (path in want); if (keep) any = 1 }
    head { hdr = hdr $0 "\n"; next }
    keep { body = body $0 "\n" }
    END { if (any) printf "%s%s", hdr, body }
  ' "$1"
}
for p in $(ls $P/series/*.patch | sort) $(ls $P/fixes/*.patch | sort); do
  part_of $p > $W/fx-part.tmp
  [ -s $W/fx-part.tmp ] || continue
  patch -p1 --fuzz=0 --no-backup-if-mismatch -s < $W/fx-part.tmp
  git add -A; git commit -q -m "$(basename $p)"
done
rm -f $W/fx-part.tmp
git tag shipped
echo "fx: $(git rev-list --count HEAD) commits, head $(git rev-parse --short HEAD)"
# against the shipped twin: only twin.sh's own edits may differ
S=$W/ffmpeg/ship/ffmpeg-9.0.2/libavcodec/h264mvc
for f in $TWIN_C $TWIN_H; do
  d=$(diff $FX/libavcodec/$f $S/$f | grep '^[<>]' | grep -v 'h264mvc_nohw.h\|\.p\.name  *= "h264\|CODEC_LONG_NAME(' || true)
  [ -z "$d" ] || { echo "DIFFERENT from the shipped twin: $f"; echo "$d" | head -5; exit 1; }
done
echo "fx: every file is the shipped twin's but for twin.sh's own edits"
