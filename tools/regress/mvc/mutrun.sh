#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) mutrun.sh <mutation> <command with the word TWIN in it>: the shipped twin's h264mvc folder copied
# into the tree ffmpeg/mut (made once from ffmpeg/ship), the mutation of mut.py applied, the tree rebuilt, and
# the command run with TWIN replaced by "mut". The mutant tree is put back to the shipped sources at the end.
set -uo pipefail
. "$(dirname "$0")/env.sh"
S=$W/ffmpeg/ship/ffmpeg-9.0.2
M=$W/ffmpeg/mut/ffmpeg-9.0.2
name=$1; shift
[ -d $M ] || cp -a $W/ffmpeg/ship $W/ffmpeg/mut
rsync -a --delete $S/libavcodec/h264mvc/ $M/libavcodec/h264mvc/
touch $M/libavcodec/h264mvc/*.[ch]
python3 -I $TOOLS/mut.py $M/libavcodec/h264mvc $name || { echo "MUTATION NOT APPLIED: $name"; exit 1; }
( cd $M && nice -n 19 make -j8 > $W/ffmpeg/mut/make.log 2>&1 ) || { echo "MUTANT DOES NOT BUILD: $name"; grep -m5 error $W/ffmpeg/mut/make.log; exit 1; }
cmd=${*//TWIN/mut}
echo "--- $name: $cmd"
bash -c "$cmd"
rsync -a --delete $S/libavcodec/h264mvc/ $M/libavcodec/h264mvc/
touch $M/libavcodec/h264mvc/*.[ch]
