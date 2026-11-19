#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of kodi-failed-notice (packages/mediacenter/kodi/scripts), as kodi.service runs it: "start" before Kodi,
# "stop" after it with systemd's SERVICE_RESULT, EXIT_CODE and EXIT_STATUS. Its paths (/dev/tty1, /run, /proc/uptime,
# /etc/os-release, /flash) are moved into a folder, and the busybox of a system runs it.
#
# Usage: failed_notice_test.sh <busybox> <kodi-failed-notice>
# With a script that does nothing, 7 checks fail. Exit 0 when every check passes, 1 when one fails, 2 when the test
# could not run.
set -u
BBI=${1:-}; S=${2:-}
[ -x "$BBI" ] && [ -f "$S" ] || { echo "usage: $0 <busybox> <kodi-failed-notice>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb" "$T/run" "$T/flash" "$T/etc"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
printf 'PRETTY_NAME="LibreELEC (Hades 1.0): devel-test"\n' > "$T/etc/os-release"
sed -e "s#^TTY=/dev/tty1#TTY=$T/tty1#" -e "s#/run/#$T/run/#g" -e "s#/proc/uptime#$T/uptime#" \
    -e "s#/etc/os-release#$T/etc/os-release#" -e "s#ls /flash #ls $T/flash #" "$S" > "$T/s.sh"
ok=0; bad=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
at() { echo "$1.42 9.00" > "$T/uptime"; }
run() { env -i PATH="$T/bb" "$@" "$T/bb/sh" "$T/s.sh" "${ARG}" >> "$T/out" 2>&1; }
start() { at "$1"; ARG=start run; }
stop() { at "$1"; ARG=stop run SERVICE_RESULT="$2" EXIT_CODE=exited EXIT_STATUS="$3"; }
shown() { [ -s "$T/tty1" ] && echo yes || echo no; }

start 10; stop 14 exit-code 255
start 16; stop 20 exit-code 255
check "two quick failures: no notice yet" "$(shown)" no
start 22; stop 26 exit-code 255
check "the third quick failure in a row: the notice is on the console" "$(shown)" yes
check "in English and in Spanish, with the count and the version" \
  "$(grep -c "could not start its interface (Kodi). It stopped 3 times in a row" "$T/tty1")/$(grep -c "no ha podido iniciar su interfaz (Kodi). Se ha detenido 3 veces seguidas" "$T/tty1")/$(grep -c "LibreELEC (Hades 1.0): devel-test" "$T/tty1")" "1/1/1"
check "with the diagnostics off it says how to turn them on" "$(grep -c "rename" "$T/tty1")/$(grep -c "DIAG-OFF.TXT a DIAG-ON.TXT" "$T/tty1")" "1/1"
check "no middle dot and no long dash in the notice" "$(grep -c "·\|—\|–" "$T/tty1")" 0
check "the journal says what happened" "$(grep -c "the notice is on the console" "$T/out")/$(grep -c "3 time(s) in a row" "$T/out")" "1/1"
: > "$T/tty1"; : > "$T/flash/DIAG-ON.TXT"
start 28; stop 31 signal 11
check "a fourth one, a crash: shown again, with the diagnostics on" "$(grep -c "4 times in a row" "$T/tty1")/$(grep -c "The diagnostics are on" "$T/tty1")" "1/1"
: > "$T/tty1"
start 33; stop 600 success 0
start 610; stop 612 exit-code 1
start 614; stop 616 exit-code 1
check "a clean exit starts the count again: two failures after it show nothing" "$(shown)" no
start 620; stop 700 exit-code 1
start 702; stop 704 exit-code 1
start 706; stop 708 exit-code 1
check "a Kodi that ran for a minute or more starts the count again too" "$(shown)" no
start 710; stop 712 exit-code 1
check "and the third quick one after it shows the notice" "$(grep -c "3 times in a row" "$T/tty1")" 1
: > "$T/tty1"; rm -f "$T/run/"*
ARG=stop; at 800; run SERVICE_RESULT=exit-code EXIT_CODE=exited EXIT_STATUS=1
ARG=stop; at 801; run SERVICE_RESULT=exit-code EXIT_CODE=exited EXIT_STATUS=1
ARG=stop; at 802; run SERVICE_RESULT=exit-code EXIT_CODE=exited EXIT_STATUS=1
check "a stop without a start that the script saw counts nothing" "$(shown)" no
echo "failed-notice: $ok checks passed, $bad failed"
[ $bad = 0 ]
