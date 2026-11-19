# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lib-ffmpeg.sh: the FFmpeg builds of the group ffmpeg (sourced by run-ffmpeg.sh after lib/regress.sh): the recipe's
# FFmpeg with some of its patches left out (the code before a patch, for the tests that compare the two), and a build
# that keeps its source and build trees, to run FATE in them. Both take the source from rg_ff_source (lib/regress.sh):
# the archive checked against PKG_SHA256, the image's patches in the order of scripts/unpack for PROJECT=Generic, each
# with -F0.

# ff_variant <dest> <ERE of patch names left out> <configure options...>: that FFmpeg built static for the host into
# <dest>/prefix (rg_ffmpeg with RG_FF_LEAVE_OUT)
ff_variant() {
  local D=$1 skip=$2
  shift 2
  RG_FF_LEAVE_OUT=$skip rg_ffmpeg "$D" "$@"
}

# ff_fate <dest> <samples folder>: the source with every patch (<dest>/src) configured out of its tree into <dest>/build
# for FATE (static, every component, GPL, no network, no autodetection; zlib and libxml2 of the build folder's
# toolchain, which the IMF demuxer of ffmpeg-9934 and the zlib parts of g2meet, Matroska and FLV need; x86 assembly
# where nasm is there), with --samples, and built. The source is only read by the build (a mark is made before it).
ff_fate() {
  local D=$1 FS=$2 TC=$REGRESS_LE_BUILD/toolchain ASM=--disable-x86asm rc
  command -v nasm > /dev/null && ASM=
  rg_ff_source "$D" - "fate $ASM $FS"
  rc=$?
  [ $rc = 2 ] && { echo "FF: $D already built from the same archive and patches"; return 0; }
  [ $rc = 0 ] || return 1
  rg_fresh "$D/build"
  touch "$D/src.mark"
  sleep 1
  ( cd "$D/build" &&
    PKG_CONFIG_PATH=$TC/lib/pkgconfig nice -n 19 "$D/src/configure" --prefix="$D/prefix" --disable-shared --enable-static \
      --disable-doc --disable-network --disable-autodetect $ASM --enable-gpl --enable-zlib --enable-libxml2 \
      --extra-cflags="-I$TC/include" --extra-ldflags="-L$TC/lib -Wl,-rpath,$TC/lib" --samples="$FS" \
      > "$D/configure.log" 2>&1 < /dev/null ) || { tail -n 20 "$D/configure.log"; echo "FF: CONFIGURE FAILED $D"; return 1; }
  echo "FF: configured ${ASM:-with x86 assembly}; $(grep -E '^#define CONFIG_(ZLIB|LIBXML2|IMF_DEMUXER|G2M_DECODER|CDXL_DECODER|SUNRAST_DECODER) ' "$D/build/config.h" | cut -d' ' -f2- | tr '\n' ' ')"
  ( cd "$D/build" && nice -n 19 make -j"$RG_JOBS" > "$D/make.log" 2>&1 < /dev/null ) \
    || { tail -n 30 "$D/make.log"; echo "FF: BUILD FAILED $D"; return 1; }
  : > "$D/built"
  echo "FF: built for FATE in $D/build: $(grep -c 'warning:' "$D/make.log") warning lines"
}
