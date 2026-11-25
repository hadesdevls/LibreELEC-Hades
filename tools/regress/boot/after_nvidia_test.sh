#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of after-nvidia (packages/graphics/nvidia/scripts), which kmod runs in place of loading snd_virtuoso
# (modprobe.d/after-nvidia.conf): the module is loaded once the nvidia module has started, and not waited for where there
# is nothing to wait for. /sys, /dev/kmsg and modprobe are moved into a folder: the PCI devices and the state of the
# nvidia module are files that a check writes, and changes while the script waits; the fake modprobe notes each load with
# the state of nvidia at that moment. The busybox of a system runs the script, with limits of 1 s (to begin) and 3 s.
#
# Usage: after_nvidia_test.sh <busybox> <after-nvidia>
# Then each of its mutations (a copy of the script with one rule taken out) must fail at least one check: "mutation Mn
# caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test could not run.
set -u
BBI=${1:-}; AN=${2:-}
[ -x "$BBI" ] && [ -f "$AN" ] || { echo "usage: $0 <busybox> <after-nvidia>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
[ -e "$T/bb/usleep" ] || { echo "the busybox has no usleep"; exit 2; }
cat > "$T/modprobe" <<EOF
#!$T/bb/sh
if [ "\$1" = -q ] && [ "\$2" = -n ]; then [ -f "$T/available" ]; exit; fi
echo "load \$* nvidia=\$(cat "$T/sys/module/nvidia/initstate" 2>/dev/null || echo none)" >> "$T/calls"
EOF
chmod +x "$T/modprobe"

FAILS=0
check() {
  if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi
}
# pci <name> <vendor> <class>
pci() { mkdir -p "$T/sys/bus/pci/devices/$1"; echo "$2" > "$T/sys/bus/pci/devices/$1/vendor"; echo "$3" > "$T/sys/bus/pci/devices/$1/class"; }
nv() { if [ "$1" = none ]; then rm -rf "$T/sys/module/nvidia"; else mkdir -p "$T/sys/module/nvidia"; echo "$1" > "$T/sys/module/nvidia/initstate"; fi; }
reset() {
  rm -rf "$T/sys" "$T/calls" "$T/kmsg" "$T/available"
  mkdir -p "$T/sys/bus/pci/devices" "$T/sys/module"
  : > "$T/kmsg"
  pci 0000:06:04.0 0x13f6 0x040100
}
# run <script>: the script as kmod runs it, for snd_virtuoso with an option; how long it took, in tenths of a second
run() {
  local a b
  a=$(cut -d' ' -f1 /proc/uptime)
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_KMSG="$T/kmsg" HADES_MODPROBE="$T/modprobe" HADES_START_LIMIT=1 \
    HADES_WAIT_LIMIT=3 /usr/bin/timeout 15 "$T/bb/sh" "$1" snd_virtuoso index=1 > /dev/null 2>&1
  b=$(cut -d' ' -f1 /proc/uptime)
  TOOK=$(awk -v a="$a" -v b="$b" 'BEGIN { printf "%d", (b - a) * 10 }')
}
loaded_once() { [ "$(grep -c '^load --ignore-install snd_virtuoso index=1 ' "$T/calls" 2> /dev/null)" = 1 ]; }
loaded_with() { grep -q "^load --ignore-install snd_virtuoso index=1 nvidia=$1\$" "$T/calls" 2> /dev/null; }
said() { grep -q "^after-nvidia: snd_virtuoso: $1" "$T/kmsg"; }
quiet() { [ ! -s "$T/kmsg" ]; }

cases() {
  local S=$1
  echo "1. no NVIDIA card: loaded at once, nothing said"
  reset; pci 0000:01:00.0 0x1002 0x030000; run "$S"
  loaded_once && [ "$TOOK" -lt 5 ] && quiet; check "1 at once ($TOOK)" $?
  echo "2. an NVIDIA card whose nvidia has started: at once, nothing said"
  reset; pci 0000:01:00.0 0x10de 0x030000; nv live; run "$S"
  loaded_once && loaded_with live && [ "$TOOK" -lt 5 ] && quiet; check "2 at once ($TOOK)" $?
  echo "3. nvidia being started, started 1.5 s later: loaded after it"
  reset; pci 0000:01:00.0 0x10de 0x030000; nv coming
  ( sleep 1.5; nv live ) & run "$S"; wait
  loaded_once && loaded_with live && [ "$TOOK" -ge 14 ] && said "started after the driver of the NVIDIA card 0000:01:00.0"; check "3 after nvidia ($TOOK)" $?
  echo "4. nvidia not loaded yet, begins at 0.5 s and has started at 2 s (past the limit to begin): loaded after it"
  reset; pci 0000:01:00.0 0x10de 0x030000; : > "$T/available"
  ( sleep 0.5; nv coming; sleep 1.5; nv live ) & run "$S"; wait
  loaded_once && loaded_with live && [ "$TOOK" -ge 19 ] && said "started after"; check "4 begins late ($TOOK)" $?
  echo "5. an NVIDIA card with no nvidia module to load: at once, said"
  reset; pci 0000:01:00.0 0x10de 0x030000; run "$S"
  loaded_once && [ "$TOOK" -lt 5 ] && said "the NVIDIA card 0000:01:00.0 has no driver to wait for"; check "5 no module ($TOOK)" $?
  echo "6. nvidia there to load but never begins: loaded after the limit to begin (1 s), said"
  reset; pci 0000:01:00.0 0x10de 0x030000; : > "$T/available"; run "$S"
  loaded_once && loaded_with none && [ "$TOOK" -ge 9 ] && [ "$TOOK" -lt 25 ] && said "nvidia did not begin to start in 1 s"; check "6 never begins ($TOOK)" $?
  echo "7. nvidia being started that never ends: loaded after the limit (3 s), said"
  reset; pci 0000:01:00.0 0x10de 0x030000; nv coming; run "$S"
  loaded_once && loaded_with coming && [ "$TOOK" -ge 29 ] && [ "$TOOK" -lt 45 ] && said "nvidia had not started after 3 s"; check "7 never ends ($TOOK)" $?
  echo "8. nvidia being started that goes away (its start failed): loaded then, said"
  reset; pci 0000:01:00.0 0x10de 0x030000; nv coming
  ( sleep 0.5; nv none ) & run "$S"; wait
  loaded_once && loaded_with none && [ "$TOOK" -lt 9 ] && said "nvidia went away while it started"; check "8 goes away ($TOOK)" $?
  echo "9. an NVIDIA device that is not a display (its sound): at once"
  reset; pci 0000:01:00.1 0x10de 0x040300; nv coming; run "$S"
  loaded_once && [ "$TOOK" -lt 5 ] && quiet; check "9 not a display ($TOOK)" $?
  echo "10. no module named: refused, nothing loaded"
  reset; PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_KMSG="$T/kmsg" HADES_MODPROBE="$T/modprobe" "$T/bb/sh" "$S" > /dev/null 2>&1
  r=$?; [ $r != 0 ] && [ ! -f "$T/calls" ]; check "10 no module named" $?
}

echo "after-nvidia: $AN"
cases "$AN"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

# mutation <n> <what> <sed program>: the script with one rule taken out must fail a check
MUT_MISSED=0
mutation() {
  local m=$T/mutant-$1
  sed -e "$3" "$AN" > "$m"
  if cmp -s "$m" "$AN"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  cases "$m" > "$T/mutant-$1.log" 2>&1
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "never waits" 's/^if \[ -z "\${nvidia}" \]; then$/if true; then/'
mutation 2 "the state at the start is not looked at" 's/^if \[ "\$(state)" = "live" \]; then$/if false; then/'
mutation 3 "an nvidia that went away is not seen" 's/^      began=1$/      :/'
mutation 4 "no limit to begin" 's/^      if \[ \${t} -ge \$(( START_LIMIT \* 100 )) \]; then$/      if false; then/'
mutation 5 "no driver to wait for is not seen" 's/ && ! "\${MODPROBE}" -q -n nvidia 2>\/dev\/null; then$/ \&\& false; then/'
mutation 6 "the overall limit is not kept" 's/^  if \[ \${t} -ge \$(( LIMIT \* 100 )) \]; then$/  if false; then/'
mutation 7 "a device that is not a display counts" 's/^    0x03????) nvidia=/    *) nvidia=/'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "AFTER-NVIDIA PASSED"; exit 0; }
echo "AFTER-NVIDIA FAILED: $TREE_FAILS check(s) of the script failed, $MUT_MISSED mutation(s) not caught"
exit 1
