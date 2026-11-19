#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
#
# run-rpu.sh: FFmpeg's Dolby Vision RPU parser on hand-made RPUs, with and without
# projects/Generic/patches/ffmpeg/ffmpeg-9935 (an RPU whose CRC32 holds is kept without its 0x80 terminator).
#
# usage: run-rpu.sh <ffmpeg source> <ffmpeg host build> <work folder>
#   <ffmpeg source>      an unpacked FFmpeg of the build (build/ffmpeg-*), with or without ffmpeg-9935
#   <ffmpeg host build>  its host build, with config.h and the static libavcodec.a and libavutil.a
#                        (build/ffmpeg-*/.x86_64-linux-gnu, made for hades-check)
#   <work folder>        empty or not there
# Both copies of the parser (dovi_rpudec.c, dovi_rpu.c and dovi_rpu.h, the rest of libavcodec linked in place)
# are built with AddressSanitizer and UndefinedBehaviorSanitizer and run on the same vectors
# (rpu_vectors.py, from the RPU of the check clips). The test holds only both ways: without the patch, the
# vectors of a missing terminator are rejected (so the test sees the fault), and with it every vector does what
# its column says. Last line: RPU-GROUP PASSED|FAILED.
set -euo pipefail
export LC_ALL=C TZ=UTC
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=${1:?ffmpeg source}
HB=${2:?ffmpeg host build}
W=${3:?work folder}
PATCH=$(ls "$TREE"/projects/Generic/patches/ffmpeg/ffmpeg-9935-*.patch)
CLIPS=$TREE/packages/mediacenter/hades-check/clips
trap 'echo "run-rpu.sh: STOPPED at line $LINENO"; echo "RPU-GROUP FAILED"' ERR
for f in "$SRC/libavcodec/dovi_rpudec.c" "$HB/config.h" "$HB/libavcodec/libavcodec.a" "$HB/libavutil/libavutil.a" \
  "$PATCH" "$CLIPS/dovi.py"; do
  [ -f "$f" ] || { echo "run-rpu.sh: $f is missing"; exit 2; }
done
mkdir -p "$W"
[ -z "$(ls -A "$W")" ] || { echo "run-rpu.sh: $W is not empty"; exit 2; }

python3 -I -B "$HERE/rpu_vectors.py" --clips "$CLIPS" --out "$W/vectors.txt"

# the copies: libavcodec as symbolic links, the three files of the parser as files of their own
copy() { # <name> with|without
  local d=$W/$1
  mkdir -p "$d"
  cp -rs "$SRC/libavcodec" "$d/"
  for f in dovi_rpudec.c dovi_rpu.c dovi_rpu.h; do
    rm "$d/libavcodec/$f"
    cp "$SRC/libavcodec/$f" "$d/libavcodec/$f"
  done
  if grep -q rpu_crc32_holds "$d/libavcodec/dovi_rpudec.c"; then
    [ "$2" = with ] || (cd "$d" && patch -s -R -p1 --fuzz=0 --no-backup-if-mismatch < "$PATCH")
  else
    [ "$2" = without ] || (cd "$d" && patch -s -p1 --fuzz=0 --no-backup-if-mismatch < "$PATCH")
  fi
  if [ "$2" = with ]; then grep -q rpu_crc32_holds "$d/libavcodec/dovi_rpudec.c"; else ! grep -q rpu_crc32_holds "$d/libavcodec/dovi_rpudec.c"; fi
}
FLAGS="-std=c17 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
  -D_ISOC11_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600
  -DHAVE_AV_CONFIG_H -Wall -Werror=implicit-function-declaration -Werror=incompatible-pointer-types"
build() { # <name>
  local d=$W/$1
  # shellcheck disable=SC2086
  for f in dovi_rpudec dovi_rpu; do
    gcc $FLAGS -I"$d" -I"$HB" -I"$SRC" -c "$d/libavcodec/$f.c" -o "$d/$f.o"
  done
  # shellcheck disable=SC2086
  gcc $FLAGS -I"$d" -I"$HB" -I"$SRC" -c "$HERE/rpu_parse_test.c" -o "$d/rpu_parse_test.o"
  # shellcheck disable=SC2086
  gcc $FLAGS -o "$d/rpu_parse_test" "$d/rpu_parse_test.o" "$d/dovi_rpudec.o" "$d/dovi_rpu.o" \
    "$HB/libavcodec/libavcodec.a" "$HB/libavutil/libavutil.a" -pthread -lm -latomic
}
copy without without
copy with with
build without
build with

export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
verdict=PASSED
echo "== without ffmpeg-9935, against what it does today"
"$W/without/rpu_parse_test" "$W/vectors.txt" unpatched | tee "$W/without-unpatched.txt" || verdict=FAILED
echo "== without ffmpeg-9935, against what the patch makes it do (must fail)"
if "$W/without/rpu_parse_test" "$W/vectors.txt" patched > "$W/without-patched.txt"; then
  echo "the test does not see the missing patch"; verdict=FAILED
else
  grep -c ': FAIL$' "$W/without-patched.txt" | sed 's/^/failures, as it must: /'
  grep -q '^RPU-TEST FAILED' "$W/without-patched.txt" || verdict=FAILED
fi
echo "== with ffmpeg-9935"
"$W/with/rpu_parse_test" "$W/vectors.txt" patched | tee "$W/with-patched.txt" || verdict=FAILED
echo "RPU-GROUP $verdict"
[ $verdict = PASSED ]
