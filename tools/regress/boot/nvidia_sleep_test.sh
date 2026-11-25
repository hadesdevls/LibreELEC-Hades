#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of NVIDIA's power management interface for the 580 driver (packages/graphics/nvidia/scripts): the sleep hook
# (scripts/sleep, /usr/lib/systemd/system-sleep/hades-nvidia in the image) writes what NVIDIA's README asks to
# /proc/driver/nvidia/suspend before and after a sleep, and nothing where that file is not there (the 620 driver); and
# stack-links writes the option that keeps all of the video memory for the legacy driver only. /proc/driver/nvidia/suspend
# and /run are moved into a folder; the busybox of a system runs both scripts, as systemd-sleep and kernel-overlays-setup do.
#
# Usage: nvidia_sleep_test.sh <busybox> <sleep> <stack-links>
# Then each of its mutations (a copy of a script with one rule taken out) must fail at least one check: "mutation Mn
# caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test could not run.
set -u
BBI=${1:-}; SL=${2:-}; LK=${3:-}
[ -x "$BBI" ] && [ -f "$SL" ] && [ -f "$LK" ] || { echo "usage: $0 <busybox> <sleep> <stack-links>"; exit 2; }
T=$(mktemp -d); trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done

FAILS=0
check() {
  if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi
}
# hook <script> <pre|post> <kind> [action]: as systemd-sleep runs it; the status in RC, what it printed in $T/err
hook() {
  PATH="$T/bb" HADES_NVIDIA_SUSPEND="$T/proc/suspend" SYSTEMD_SLEEP_ACTION="${4:-$3}" \
    /usr/bin/timeout 10 "$T/bb/sh" "$1" "$2" "$3" > "$T/out" 2> "$T/err"
  RC=$?
}
fresh() { chmod -R u+w "$T/proc" "$T/root" "$T/run" 2>/dev/null; rm -rf "$T/proc" "$T/root" "$T/run" "$T/out" "$T/err"; }
proc() { mkdir -p "$T/proc"; : > "$T/proc/suspend"; }
got() { [ "$(cat "$T/proc/suspend" 2>/dev/null)" = "$1" ]; }
said() { grep -q "^hades-nvidia: $1" "$T/err"; }
# links <script> <legacy|open>: stack-links with a list of one name for each driver
links() {
  mkdir -p "$T/root/usr/lib/nvidia/stacks" "$T/run"
  echo "libEGL_nvidia.so.0 /usr/lib/nvidia/580/libEGL_nvidia.so.0" > "$T/root/usr/lib/nvidia/stacks/legacy"
  echo "libEGL_nvidia.so.0 /usr/lib/libEGL_nvidia.so.620" > "$T/root/usr/lib/nvidia/stacks/open"
  PATH="$T/bb" HADES_ROOT="$T/root" HADES_RUN="$T/run" /usr/bin/timeout 10 "$T/bb/sh" "$1" "$2" > "$T/out" 2> "$T/err"
  RC=$?
}
CONF=run/modprobe.d/hades-nvidia-sleep.conf

cases() {
  local S=$1 L=$2
  echo "1. the 580 driver, before a suspend: suspend written"
  fresh; proc; hook "$S" pre suspend
  [ $RC = 0 ] && got suspend && said "pre suspend: suspend written"; check "1 pre suspend" $?
  echo "2. after it: resume written"
  fresh; proc; hook "$S" post suspend
  [ $RC = 0 ] && got resume && said "post suspend: resume written"; check "2 post suspend" $?
  echo "3. the 620 driver (no /proc/driver/nvidia/suspend): nothing made, nothing said"
  fresh; hook "$S" pre suspend
  [ $RC = 0 ] && [ ! -e "$T/proc/suspend" ] && [ ! -s "$T/err" ]; check "3 no interface" $?
  echo "4. before a hibernation and a hybrid sleep: hibernate"
  fresh; proc; hook "$S" pre hibernate; a=$RC; got hibernate; b=$?
  proc; hook "$S" pre hybrid-sleep
  [ $a = 0 ] && [ $b = 0 ] && [ $RC = 0 ] && got hibernate; check "4 hibernate" $?
  echo "5. suspend-then-hibernate: what systemd says comes now"
  fresh; proc; hook "$S" pre suspend-then-hibernate suspend; a=$RC; got suspend; b=$?
  proc; hook "$S" pre suspend-then-hibernate hibernate
  [ $a = 0 ] && [ $b = 0 ] && [ $RC = 0 ] && got hibernate; check "5 suspend-then-hibernate" $?
  echo "6. the driver does not take it: a failure, said"
  fresh; proc; chmod 0444 "$T/proc/suspend"; hook "$S" pre suspend
  [ $RC != 0 ] && said "pre suspend: the NVIDIA driver did not take suspend"; check "6 refused" $?
  echo "7. something else than pre or post: nothing written"
  fresh; proc; hook "$S" during suspend
  [ $RC = 0 ] && got ""; check "7 not pre or post" $?
  echo "8. stack-links legacy: the option of the video memory, said"
  fresh; links "$L" legacy
  [ $RC = 0 ] && [ "$(cat "$T/$CONF" 2>/dev/null)" = "options nvidia NVreg_PreserveVideoMemoryAllocations=1" ] &&
    grep -q "^NVIDIA driver: the legacy driver keeps its video memory across a sleep" "$T/err" &&
    [ "$(readlink "$T/run/nvidia/libEGL_nvidia.so.0")" = /usr/lib/nvidia/580/libEGL_nvidia.so.0 ]; check "8 legacy" $?
  echo "9. stack-links open: no option (one left from before is taken away)"
  fresh; mkdir -p "$T/run/modprobe.d"; echo stale > "$T/$CONF"; links "$L" open
  [ $RC = 0 ] && [ ! -e "$T/$CONF" ] &&
    [ "$(readlink "$T/run/nvidia/libEGL_nvidia.so.0")" = /usr/lib/libEGL_nvidia.so.620 ]; check "9 open" $?
  echo "10. stack-links legacy, /run/modprobe.d cannot be made: the links still made, said"
  fresh; mkdir -p "$T/run"; : > "$T/run/modprobe.d"; links "$L" legacy
  [ $RC = 0 ] && grep -q "could not be written" "$T/err" &&
    [ "$(readlink "$T/run/nvidia/libEGL_nvidia.so.0")" = /usr/lib/nvidia/580/libEGL_nvidia.so.0 ]; check "10 no folder" $?
}

echo "sleep: $SL"
echo "stack-links: $LK"
cases "$SL" "$LK"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

# mutation <n> <what> <script: sleep|links> <sed program>: one rule taken out must fail a check
MUT_MISSED=0
mutation() {
  local m=$T/mutant-$1 s=$SL l=$LK src
  [ "$3" = sleep ] && src=$SL || src=$LK
  sed -e "$4" "$src" > "$m"
  if cmp -s "$m" "$src"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  [ "$3" = sleep ] && s=$m || l=$m
  FAILS=0
  cases "$s" "$l" > "$T/mutant-$1.log" 2>&1
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "the 620 driver is not told apart" sleep 's/^\[ -e "\${SUSPEND}" \] || exit 0$/:/'
mutation 2 "nothing written after a sleep" sleep 's/^    what=resume$/    exit 0/'
mutation 3 "a hibernation suspends" sleep 's/^    what=hibernate$/    what=suspend/'
mutation 4 "a refusal is not said as one" sleep 's/^if echo "\${what}" > "\${SUSPEND}" 2>\/dev\/null; then$/if echo "${what}" > "${SUSPEND}" 2>\/dev\/null || true; then/'
mutation 5 "the option for every driver" links 's/^if \[ "\${stack}" = "legacy" \]; then$/if true; then/'
mutation 6 "no option for the legacy driver" links 's/^     echo "options nvidia NVreg_PreserveVideoMemoryAllocations=1" > "\${conf}"; then$/     true; then/'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "NVIDIA-SLEEP PASSED"; exit 0; }
echo "NVIDIA-SLEEP FAILED: $TREE_FAILS check(s) of the scripts failed, $MUT_MISSED mutation(s) not caught"
exit 1
