#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The streams of the tests: the ITU-T H.264.1 MVC conformance streams in $W/itu, and in $W/streams the two
# streams of the reference encoder and the streams made on purpose from them and from the ITU-T ones (craft.py,
# craft22.py: only parameter sets, NAL unit headers and the lists of slice headers change; the coded pictures
# are the same bytes). Every input is checked by its SHA256 first.
set -euo pipefail
. "$(dirname "$0")/env.sh"
check() { [ "$(sha256sum "$1" | cut -c1-64)" = "$2" ] || { echo "ABORT: $1: its SHA256 is not $2"; exit 1; }; }
check "$ITU_ZIP" $ITU_ZIP_SHA256
check "$SD540" $SD540_SHA256
check "$BD1080" $BD1080_SHA256
rm -rf $ITU; mkdir -p $ITU
( cd $ITU && unzip -o -q "$ITU_ZIP" < /dev/null )
echo "ITU-T streams: $(find $ITU -iname '*.264' | wc -l)"
S=$W/streams; rm -rf $S; mkdir -p $S
cp "$SD540" $S/sd540-wrap.264
cp "$BD1080" $S/bd1080.264
I=$(find $ITU -name MVCDS1.264); SP=$(find $ITU -name MVCSPS-1.264); SD=$S/sd540-wrap.264
C="python3 -I $TOOLS/craft.py"
$C scaling-default  $SD $S/f4-default.264          # F4: subset SPS (id shared with the SPS) with matrices
$C scaling-custom   $SD $S/f4-custom.264
$C scaling-fallback $SD $S/f4-fallback.264
$C replace          $SP $S/f5-replace.264          # F5: subset SPS replaced, PPS not sent again
$C replace  $S/f4-default.264 $S/f5-replace-shared.264  # F5, ids shared (the PPS read again in the first copy)
$C pps-first        $I  $S/f5-ppsfirst.264
$C views            $I  $S/f6-views.264 3          # F6: a second subset SPS with other views
$C levels           $I  $S/f8-levels40.264 40      # F8: more than 32 level values
$C levels           $I  $S/f8-levels64.264 64
$C levels-bad       $I  $S/f8-bad-ops.264 ops
$C levels-bad       $I  $S/f8-bad-views.264 views
$C levels-bad       $I  $S/f8-bad-cut.264 cut
python3 -I $TOOLS/craft22.py levels-huge $I $S/f8-huge.264
$C viewmod      $SD $S/f9-viewmod-ok2.264 5,0      # F9: inter-view list modifications
$C viewmod-one  $SD $S/f9-bad-range.264 5,1
$C viewmod-one  $SD $S/f9-bad-huge.264 5,2147483647
$C viewmod-one  $SD $S/f9-bad-max.264 4,4294967294
$C viewmod-one  $SD $S/f9-ok-one.264 5,0
cat $SD $SD > $S/x-sd540x2.264                     # N1: an IDR access unit in the middle of a stream
python3 -I $TOOLS/craft22.py grain $SD $S/g-grain.264   # a concern of the review: film grain
cat $S/f4-default.264 $S/f4-default.264 > $S/x-f4x2.264
python3 -I $TOOLS/bits-selftest.py $(find $ITU -iname '*.264' | sort) $SD | tail -n 1
( cd $S && sha256sum *.264 > SHA256SUMS )
echo STREAMS-DONE
