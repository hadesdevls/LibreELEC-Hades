#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lane.sh <runner> <log folder>: one group of run-regress.sh, inside the memory scope that run-regress.sh made: the
# runner's "all" (its own output into <log folder>/lane.log), then the peak memory of the scope into memory-peak.txt.
set -u
R=$1 L=$2
bash "$R" all > "$L/lane.log" 2>&1 < /dev/null
rc=$?
cg=/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup | head -n 1)
if [ -r "$cg/memory.peak" ]; then
  echo "$(( $(cat "$cg/memory.peak") / 1048576 )) MiB ($(basename "$cg"))" > "$L/memory-peak.txt"
else
  echo "not measured (no memory.peak in $cg)" > "$L/memory-peak.txt"
fi
exit $rc
