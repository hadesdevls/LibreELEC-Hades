# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lib-dvbridge-render.sh: what run-dvbridge-render.sh needs besides lib/regress.sh (sourced after it).
#
# ff_internal: FFmpeg of the recipe (rg_ff_source: the archive checked against PKG_SHA256, the image's patches with
# -F0) configured with the HEVC decoder only, its libavcodec.a and libavutil.a built inside the source tree, which is
# kept: for a test that links FFmpeg's internal RPU parser (libavcodec/dovi_rpu.h, ff_dovi_rpu_parse()), which the
# installed prefix of rg_ffmpeg does not have. Shared by the lanes, made again only when the archive or the patches
# changed: echoes the source folder.
ff_internal() {
  local D=$RG_SHARED/ff-internal
  rg_locked ff-internal ff_internal_make "$D" > "$RG_SHARED/ff-internal.log" 2>&1 || { tail -n 30 "$RG_SHARED/ff-internal.log" >&2; return 1; }
  echo "$D/src"
}
ff_internal_make() {
  local D=$1 rc
  rg_ff_source "$D" - "internal: hevc decoder, libavcodec.a and libavutil.a in the source tree"
  rc=$?
  [ $rc = 2 ] && { echo "FFI: $D already built from the same archive and patches"; return 0; }
  [ $rc = 0 ] || return 1
  ( cd "$D/src" && nice -n 19 ./configure --disable-everything --disable-autodetect --disable-doc --disable-network \
      --disable-x86asm --enable-static --disable-shared --disable-programs --enable-decoder=hevc --enable-parser=hevc \
      > "$D/configure.log" 2>&1 < /dev/null &&
    nice -n 19 make -j"$RG_JOBS" libavcodec/libavcodec.a libavutil/libavutil.a > "$D/make.log" 2>&1 < /dev/null ) \
    || { tail -n 30 "$D/configure.log" "$D/make.log"; echo "FFI: BUILD FAILED $D"; return 1; }
  : > "$D/built"
  echo "FFI: built in $D/src (HEVC decoder only)"
}
