#!/bin/bash
# run-libass.sh <kodi tree> <out dir>: builds libass_par_test with the toolchain of the build folder, against the
# libass of its sysroot, and runs it with the sysroot's loader (every library from the toolchain: checked)
set -euo pipefail
K=$1; O=$2
B=${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}
S=$B/toolchain/x86_64-libreelec-linux-gnu/sysroot
L=$S/usr/lib:$B/toolchain/x86_64-libreelec-linux-gnu/lib
T=$(dirname "$0")/libass_par_test.cpp
mkdir -p $O
$B/toolchain/bin/x86_64-libreelec-linux-gnu-g++ -std=c++20 -O1 -Wall -Wextra -Werror -I$K/xbmc \
  ${EXTRA_CXXFLAGS:-} $T -o $O/libass_par_test -lass
# every library that it loads comes from the toolchain of the build (nothing from the host)
missing=$(LD_TRACE_LOADED_OBJECTS=1 $S/usr/lib/ld-linux-x86-64.so.2 --library-path $L $O/libass_par_test \
  | awk '$3 != "" && index($3, "'"$B/toolchain/"'") != 1 && $1 !~ /vdso/ { print $1 " " $3 }')
[ -z "$missing" ] || { echo "FAIL: libraries from outside the toolchain: $missing"; exit 1; }
$S/usr/lib/ld-linux-x86-64.so.2 --library-path $L $O/libass_par_test \
  $K/addons/skin.estuary/fonts/NotoSans-Regular.ttf
