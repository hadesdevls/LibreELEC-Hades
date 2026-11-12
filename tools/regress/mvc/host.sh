#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder, no network) tools/regress/mvc: a host FFmpeg as the image patches it, and twin trees with h264_mvc.
# Outputs in $MVC_WORK/ffmpeg; the package files are read from the tree under test ($MVC_LE, read only).
#   base    FFmpeg 9.0.2 for the host as the image has it (hash of the recipe; patches in the order of
#           scripts/unpack for PROJECT=Generic: postproc, libreelec, projects/Generic/patches/ffmpeg),
#           configured small, the stock H.264 decoder only (ffmpeg-9913 left out here, put in by gen)
#   pkg     the h264-mvc package files: from the tree as it is now, or with PKGREV=<git revision> from that
#           commit; DROPFIX="9001 ..." leaves those fixes out
#   gen     the tree with the second decoder (twin.sh of the package files), configured, its copies compiled
#   build   compile all
#   objs    the stock decoder's objects in the two trees, without debug information
# TWDIR=<name> selects the twin tree (default ship). Lowest priority, 8 jobs: the machine is shared.
set -euo pipefail
export LC_ALL=C
. "$(dirname "$0")/env.sh" && W=$W/ffmpeg
TWD=${TWDIR:-ship}
P=$W/pkg-$TWD/h264-mvc
J=${JOBS:-8}
mkdir -p "$W"
TWIN_O="h264dec h264_slice h264_refs h264_ps h264_picture h264_direct h264_parse h2645_parse h264_cabac h264_cavlc h264_loopfilter h264_mb h264_sei"
conf() { sed 's/--enable-decoder=h264,/--enable-decoder=h264,h264_mvc,/' $W/base/configure-options.txt; }
case "${1:-}" in
base)
  SRC=$SRCD/ffmpeg/ffmpeg-9.0.2.tar.xz
  EXPECTED=$(grep '^PKG_SHA256=' $LT/packages/multimedia/ffmpeg/package.mk | head -n 1 | cut -d'"' -f2)
  GOT=$(sha256sum "$SRC" | cut -c1-64)
  [ "$GOT" = "$EXPECTED" ] || { echo "ABORT: sha256 $GOT != package.mk $EXPECTED"; exit 1; }
  echo "source verified: $GOT"
  rm -rf "$W/base"; mkdir -p "$W/base"; cd "$W/base"
  tar --no-same-owner -xf "$SRC"
  cd ffmpeg-9.0.2
  n=0
  for d in packages/multimedia/ffmpeg/patches/postproc packages/multimedia/ffmpeg/patches/libreelec projects/Generic/patches/ffmpeg; do
    for p in $(ls $LT/$d/*.patch 2> /dev/null | sort); do
      case $p in *ffmpeg-9913-h264-mvc-*) continue ;; esac
      /usr/bin/patch -p1 -F0 -s < $p || { echo "PATCH FAILED: $p"; exit 1; }
      n=$((n + 1))
    done
  done
  echo "patches applied without fuzz: $n (all of the image's but ffmpeg-9913, which gen applies)"
  cp -a . ../pristine-src
  CONF="--disable-everything --disable-autodetect --disable-doc --disable-network --disable-x86asm --enable-static --disable-shared \
    --enable-decoder=h264,rawvideo,pcm_s16le --enable-encoder=rawvideo,wrapped_avframe,pcm_s16le \
    --enable-parser=h264 --enable-demuxer=h264,matroska,mov,mpegts,avi,flv \
    --enable-muxer=framecrc,framemd5,md5,null,rawvideo,crc,hash,framehash,matroska \
    --enable-bsf=h264_mp4toannexb,extract_extradata,null \
    --enable-filter=scale,format,null,copy,aresample,anull,setpts,fps,crop,hstack,vstack \
    --enable-protocol=file,pipe,md5,crc --enable-ffmpeg --enable-ffprobe"
  echo "$CONF" > ../configure-options.txt
  # shellcheck disable=SC2086
  nice -n 19 ./configure --prefix="$W/base/prefix" $CONF > "$W/base/configure.log" 2>&1 || { tail -n 30 "$W/base/configure.log"; exit 1; }
  nice -n 19 make -j$J > "$W/base/make.log" 2>&1 || { tail -n 40 "$W/base/make.log"; exit 1; }
  ./ffmpeg -hide_banner -version | head -n 1
  echo BASE-DONE
  ;;
pkg)
  rm -rf $W/pkg-$TWD; mkdir -p $W/pkg-$TWD
  if [ -n "${PKGREV:-}" ]; then
    git -C $LT archive "$PKGREV" packages/multimedia/ffmpeg/h264-mvc | tar -x -C $W/pkg-$TWD
    echo "h264-mvc from $PKGREV ($(git -C $LT rev-parse --short "$PKGREV"))"
  else
    mkdir -p $W/pkg-$TWD/packages/multimedia/ffmpeg
    cp -a $LT/packages/multimedia/ffmpeg/h264-mvc $W/pkg-$TWD/packages/multimedia/ffmpeg/
    echo "h264-mvc from the tree ($(git -C $LT rev-parse --short HEAD), changed: $(git -C $LT status --porcelain packages/multimedia/ffmpeg | wc -l))"
  fi
  # ffmpeg-9913 (the three build lines) always from the tree as it is now
  mkdir -p $W/pkg-$TWD/projects/Generic/patches/ffmpeg
  cp $LT/projects/Generic/patches/ffmpeg/ffmpeg-9913-h264-mvc-*.patch $W/pkg-$TWD/projects/Generic/patches/ffmpeg/
  ln -sfn packages/multimedia/ffmpeg/h264-mvc $W/pkg-$TWD/h264-mvc
  for n in ${DROPFIX:-}; do rm -v $P/fixes/$n-*.patch; done
  # EXTRAFIX="<patch>...": fixes that are not in the tree yet (a proposal), added to the copy
  for f in ${EXTRAFIX:-}; do cp -v $f $P/fixes/; done
  echo "series: $(ls $P/series | wc -l); fixes: $(ls $P/fixes 2> /dev/null | tr '\n' ' ')"
  ;;
gen)
  [ -d $W/base/pristine-src ] || { echo "ABORT: no patched source of the baseline yet"; exit 1; }
  rm -rf $W/$TWD; mkdir -p $W/$TWD
  cp -a $W/base/pristine-src $W/$TWD/ffmpeg-9.0.2
  cd $W/$TWD/ffmpeg-9.0.2
  /usr/bin/patch -p1 -F0 -s < $(ls $W/pkg-$TWD/projects/Generic/patches/ffmpeg/ffmpeg-9913-h264-mvc-*.patch) || { echo "ABORT: ffmpeg-9913"; exit 1; }
  bash $P/twin.sh "$PWD" "$P" > $W/$TWD/gen.log 2>&1 || { cat $W/$TWD/gen.log; exit 1; }
  grep -c ': applied' $W/$TWD/gen.log | sed 's/^/parts applied: /'; tail -n 1 $W/$TWD/gen.log
  # shellcheck disable=SC2046
  nice -n 19 ./configure --prefix="$W/$TWD/prefix" $(conf) ${CONFX:-} > $W/$TWD/configure.log 2>&1 || { tail -n 30 $W/$TWD/configure.log; exit 1; }
  objs=""; for o in $TWIN_O; do objs="$objs libavcodec/h264mvc/$o.o"; done
  # shellcheck disable=SC2086
  nice -n 19 make -j$J $objs > $W/$TWD/make-objs.log 2>&1 || { grep -n -B2 -A12 'error' $W/$TWD/make-objs.log | head -n 60; exit 1; }
  echo "the thirteen copies compile; warnings: $(grep -c 'warning:' $W/$TWD/make-objs.log || true)"
  # shellcheck disable=SC2086
  nm --defined-only -g $objs | awk 'NF == 3 { print $3 }' | sort -u > $W/$TWD/defined.txt
  left=$(grep -v 'mvc' $W/$TWD/defined.txt || true)
  if [ -n "$left" ]; then echo "ABORT: defined by the copies and not renamed: $(echo $left | tr '\n' ' ')"; exit 1; fi
  echo "every global symbol of the copies has a name of its own ($(wc -l < $W/$TWD/defined.txt))"
  echo GEN-DONE
  ;;
build)
  cd $W/$TWD/ffmpeg-9.0.2
  nice -n 19 make -j$J > $W/$TWD/make.log 2>&1 || { grep -n -B2 -A12 'error\|multiple definition' $W/$TWD/make.log | head -n 60; exit 1; }
  echo "warnings in the copies: $(grep 'h264mvc/' $W/$TWD/make.log | grep -c 'warning:' || true)"
  ./ffmpeg -hide_banner -decoders 2> /dev/null | grep -i 'h264'
  echo BUILD-DONE
  ;;
objs)
  X=$(mktemp -d); trap 'rm -rf "$X"' EXIT
  same=0; diff=0
  for o in $TWIN_O h264_parser h264data h274 h2645data h2645_vui h2645_sei error_resilience; do
    a=$W/base/ffmpeg-9.0.2/libavcodec/$o.o; b=$W/$TWD/ffmpeg-9.0.2/libavcodec/$o.o
    [ -f "$a" ] && [ -f "$b" ] || { echo "  $o.o: missing in one of the trees"; continue; }
    objcopy --strip-debug "$a" $X/a.o; objcopy --strip-debug "$b" $X/b.o
    if cmp -s $X/a.o $X/b.o; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFFERENT: $o.o"; fi
  done
  echo "objects of the stock decoder and of what it shares, without debug information: identical $same, different $diff"
  ;;
*) echo "usage: host.sh base|pkg|gen|build|objs"; exit 1 ;;
esac
