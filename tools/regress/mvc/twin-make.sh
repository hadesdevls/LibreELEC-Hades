#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder) a twin tree: twin-make.sh <name> [<git revision of the tree>] [<fixes to leave out>]
. "$(dirname "$0")/env.sh"
H=$TOOLS/host.sh
O=$W/ffmpeg
export TWDIR=$1
export PKGREV=${2:-}
export DROPFIX=${3:-}
start=$(date +%s)
bash $H pkg > $O/pkg-$1.out 2>&1 || { echo "pkg $1 FAILED"; cat $O/pkg-$1.out; exit 1; }
bash $H gen > $O/gen-$1.out 2>&1 || { echo "gen $1 FAILED"; tail -n 30 $O/gen-$1.out; exit 1; }
bash $H build > $O/build-$1.out 2>&1 || { echo "build $1 FAILED"; tail -n 30 $O/build-$1.out; exit 1; }
bash $H objs > $O/objs-$1.out 2>&1
echo "$1: $(head -n 1 $O/pkg-$1.out | cut -c1-120) | $(tail -n 1 $O/pkg-$1.out | cut -c1-200) | $(tail -n 1 $O/objs-$1.out) | $(( $(date +%s) - start )) s"
