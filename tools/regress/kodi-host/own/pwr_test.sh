#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC5): test of pwr() of watch-dv.sh (hades-diag) (where the power comes from) with the shell of the image, in a made-up
# /sys, and the mutations that must make it fail.
#   pwr_test.sh <busybox of an image> <watch-dv.sh>
set -u
BB=$1; SRC=$2
T=$(mktemp -d); trap 'rm -rf $T' EXIT
run_case() { # script-with-pwr, case name, expected; the made-up /sys is $T/sys
  local got
  got=$(cd $T && $BB sh -c ". $1; pwr" 2>&1)
  if [ "$got" = "$3" ]; then echo "ok   $2"; return 0; else echo "FAIL $2: expected [$3], got [$got]"; return 1; fi
}
extract() { # watch-dv.sh -> a file with pwr() reading $T/sys
  sed -n '/^pwr() {$/,/^}$/p' "$1" | sed "s|/sys/class/power_supply|$T/sys/class/power_supply|" > "$2"
  [ -s "$2" ]
}
supply() { mkdir -p $T/sys/class/power_supply/$1; echo "$2" > $T/sys/class/power_supply/$1/type; [ -n "$3" ] && echo "$3" > $T/sys/class/power_supply/$1/$4; return 0; }
cases() { # f -> number of failures
  local f=$1 bad=0
  rm -rf $T/sys; mkdir -p $T/sys/class/power_supply
  run_case $f "a desktop: nothing" "" || bad=$((bad + 1))
  supply BAT0 Battery Charging status
  supply ADP1 Mains 1 online
  run_case $f "a laptop on its charger" "ADP1:1,BAT0:Charging" || bad=$((bad + 1))
  echo "Not charging" > $T/sys/class/power_supply/BAT0/status; echo 0 > $T/sys/class/power_supply/ADP1/online
  run_case $f "a status with a space; the charger out" "ADP1:0,BAT0:Not_charging" || bad=$((bad + 1))
  supply ucsi-source-psy-USBC000:001 USB 1 online
  supply hidpp_battery_0 Battery Discharging status
  supply hid-00:11:22 Unknown "" ""
  run_case $f "USB-C supply, a mouse battery, an unknown kind left out" "ADP1:0,BAT0:Not_charging,hidpp_battery_0:Discharging,ucsi-source-psy-USBC000:001:1" || bad=$((bad + 1))
  rm $T/sys/class/power_supply/BAT0/status
  run_case $f "a battery without status" "ADP1:0,BAT0:?,hidpp_battery_0:Discharging,ucsi-source-psy-USBC000:001:1" || bad=$((bad + 1))
  chmod 000 $T/sys/class/power_supply/ADP1/type 2>/dev/null
  [ "$(id -u)" != 0 ] && run_case $f "a supply whose type cannot be read is left out" "BAT0:?,hidpp_battery_0:Discharging,ucsi-source-psy-USBC000:001:1" || true
  chmod 644 $T/sys/class/power_supply/ADP1/type 2>/dev/null
  return $bad
}
F=$T/pwr.sh
extract "$SRC" $F || { echo "FAIL: no pwr() in $SRC"; exit 1; }
cases $F; base=$?
echo "pwr(): $base failures"
# the loop: the field is added when it changes and every 10 s, and the header names it
grep -q 'pwr=supply:status' "$SRC" && echo "ok   the header of the timeline names the field" || { echo "FAIL header"; base=$((base + 1)); }
grep -q '\[ "\$w" != "\$lastw" \] || \[ \$((t % 10)) -eq 0 \]' "$SRC" && echo "ok   the field goes in when it changes and every 10 s" || { echo "FAIL loop"; base=$((base + 1)); }
$BB sh -n "$SRC" && echo "ok   watch-dv.sh: syntax" || { echo "FAIL syntax"; base=$((base + 1)); }

mut() { # name sed-expression
  local m=$T/m-$1.sh
  extract "$SRC" $m
  cp $m $m.orig; sed -i "$2" $m
  if cmp -s $m $m.orig; then echo "MUTATION $1 NOT APPLIED"; MB=1; return; fi
  if cases $m >/dev/null 2>&1; then echo "MUTATION $1 SURVIVED"; MB=1; else echo "mutation $1 caught"; fi
}
MB=0
mut P1 "s/| tr ' ' '_'//"
mut P2 's/Battery)/Batteryx)/'
mut P3 's/\${s:+,}//'
mut P4 's/\*) continue ;;/*) v=x ;;/'
mut P5 's/\${v:-?}/${v}/'
mut P6 's/Mains | USB\*)/Mains)/'
[ $base = 0 ] && [ $MB = 0 ] && echo "PWR TEST PASSED" || { echo "PWR TEST FAILED"; exit 1; }
