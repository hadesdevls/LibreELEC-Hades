#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of the wait of "Wait for network before starting Kodi": network-wait (packages/sysutils/wait-time-sync/
# scripts), and trust_keep() of clock-measure (packages/sysutils/busybox/scripts), which writes what network-wait reads
# (/storage/.cache/clock-trusted). Their paths (the storage, /etc/adjtime, the clock of the computer, the flags of the
# time synchronised) are moved into a folder; wait-time-sync, connmand-wait-online and logger are replaced by fakes that
# end when and how a check says, and the busybox of a system runs the scripts.
#
# Usage: network_wait_test.sh <busybox> <network-wait> <clock-measure>
# Then each of its mutations (a copy of a script with one rule taken out) must fail at least one check: "mutation Mn
# caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test could not run.
set -u
BBI=${1:-}; NW=${2:-}; CM=${3:-}
[ -x "$BBI" ] && [ -f "$NW" ] && [ -f "$CM" ] || { echo "usage: $0 <busybox> <network-wait> <clock-measure>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
rm -f "$T/bb/logger"
# the fakes: each says that it ran, waits, and ends as the check asks
cat > "$T/bb/logger" <<EOF
#!$T/bb/sh
[ "\$1" = -t ] && shift 2; [ "\$1" = -- ] && shift
echo "\$*" >> "$T/journal"
EOF
cat > "$T/wts" <<EOF
#!$T/bb/sh
echo "wts \$*" >> "$T/calls"; sleep \${WTS_AFTER:-0}; exit \${WTS_RC:-0}
EOF
cat > "$T/wol" <<EOF
#!$T/bb/sh
echo "wol \$*" >> "$T/calls"; sleep \${WOL_AFTER:-0}; exit \${WOL_RC:-0}
EOF
chmod +x "$T/bb/logger" "$T/wts" "$T/wol"

# a script with its paths moved into the folder: <script> <out>
moved() {
  sed -e "s#^CACHE=/storage/.cache#CACHE=$T/cache#" -e "s#/etc/adjtime#$T/adjtime#g" \
      -e "s#^WAIT_TIME_SYNC=.*#WAIT_TIME_SYNC=$T/wts#" -e "s#^WAIT_ONLINE=.*#WAIT_ONLINE=$T/wol#" \
      -e "s#^RTC=.*#RTC=$T/rtc#" -e "s#^CHANGED=.*#CHANGED=$T/run/clock-changed#" \
      -e "s#^SYNCED_CONNMAN=.*#SYNCED_CONNMAN=$T/synced#" -e "s#^SYNCED_TIMESYNCD=.*#SYNCED_TIMESYNCD=$T/none#" "$1" > "$2"
}

ok=0; bad=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
fresh() { rm -rf "$T/cache" "$T/run" "$T/journal" "$T/calls"; mkdir -p "$T/cache" "$T/run"; : > "$T/journal"; : > "$T/calls"; : > "$T/out"; }
adjtime() { printf '0.0 0 0\n0\n%s\n' "$1" > "$T/adjtime"; }
now() { date +%s; }
# a marker as clock-measure writes it: <age in s> <mode> <offset>
marker() { printf 'trusted\n%s\n%s\n%s\n%s\n' $(($(now) - $1)) "$2" "$3" "test UTC: read as $2, 0 s off" > "$T/cache/clock-trusted"; }
# network-wait, as the unit runs it, in the time zone of New York in winter (-0500): status, seconds taken, journal
nw() {
  local s e rc
  s=$(date +%s%N)
  env -i PATH="$T/bb" TZ=EST5 WTS_AFTER="$1" WTS_RC="$2" WOL_AFTER="$3" WOL_RC="$4" "$T/bb/sh" "$T/nw.sh" "${5-3}" >> "$T/out" 2>&1
  rc=$?
  e=$(date +%s%N)
  RC=$rc; TOOK=$(( (e - s) / 100000000 ))  # tenths of a second
}
says() { grep -c -- "$1" "$T/journal"; }
trusted() { env -i PATH="$T/bb" TZ=EST5 "$T/bb/sh" "$T/nw.sh" check | cut -d: -f1; }

suite_nw() {
  moved "$NW" "$T/nw.sh"
  # 1. nothing found right: LibreELEC's wait, unchanged
  fresh; adjtime LOCAL
  check "1 no marker: not trusted" "$(trusted)" "not trusted"
  nw 0.3 0 0.1 0
  check "1 no marker: the wait for the time alone, its status" "$RC/$(says 'as in LibreELEC')/$(grep -c '^wol' "$T/calls")/$(grep -c '^wts --timeout 3$' "$T/calls")" "0/1/0/1"
  nw 0.3 1 0.1 0
  check "1 no marker: the time does not come, the status of wait-time-sync (1)" "$RC" 1
  # 2. found right a day ago, read as local time in the same time zone: the network ends the wait
  fresh; adjtime LOCAL; marker 86400 LOCAL -0500
  check "2 found right: trusted" "$(trusted)" "trusted"
  nw 3 0 0.2 0
  check "2 found right: the network first ends it at once, status 0" "$RC/$([ $TOOK -lt 15 ] && echo fast || echo "slow $TOOK")/$(says 'as connman has the network')" "0/fast/1"
  check "2 found right: both waits asked, with the longest wait" "$(grep -c '^wts --timeout 3$' "$T/calls")/$(grep -c '^wol --timeout=3$' "$T/calls")" "1/1"
  # 3. the time first
  fresh; adjtime LOCAL; marker 60 LOCAL -0500
  nw 0.2 0 3 0
  check "3 the time first: status 0, said" "$RC/$([ $TOOK -lt 15 ] && echo fast || echo "slow $TOOK")/$(says 'as the time came')" "0/fast/1"
  # 4. connman's wait gives up: the time is waited for
  fresh; adjtime LOCAL; marker 60 LOCAL -0500
  nw 0.6 0 0.1 110
  check "4 connman's wait fails: the time still ends it" "$RC/$(says 'ended without the network')/$(says 'as the time came')" "0/1/1"
  # 5. neither
  fresh; adjtime LOCAL; marker 60 LOCAL -0500
  nw 0.4 1 0.2 110
  check "5 neither comes: the status of wait-time-sync (1), said" "$RC/$(says 'neither the network nor the time')" "1/1"
  # 6. to 11. what is not trusted
  fresh; adjtime LOCAL; marker -1000 LOCAL -0500
  check "6 the clock is before the time it was found right (reset): not trusted" "$(trusted)" "not trusted"
  fresh; adjtime LOCAL; marker $((32 * 86400)) LOCAL -0500
  check "7 found right 32 days ago: not trusted" "$(trusted)" "not trusted"
  fresh; adjtime UTC; marker 60 LOCAL -0500
  check "8 read as UTC now, found right as local time: not trusted" "$(trusted)" "not trusted"
  fresh; adjtime LOCAL; marker 60 LOCAL -0400
  check "9 another offset of the time zone (summer time): not trusted" "$(trusted)" "not trusted"
  fresh; adjtime LOCAL; marker 60 LOCAL -0500; mv "$T/cache/clock-trusted" "$T/cache/real"; ln -s real "$T/cache/clock-trusted"
  check "10 the marker is a link: not trusted" "$(trusted)" "not trusted"
  fresh; adjtime LOCAL; printf 'trusted\nsoon\nLOCAL\n-0500\n' > "$T/cache/clock-trusted"
  check "11 a marker that is not what clock-measure writes: not trusted" "$(trusted)" "not trusted"
  fresh; adjtime LOCAL; marker 60 LOCAL -0500
  nw 0.3 0 0.1 0 ""
  check "11 no longest wait given: a wrong use, nothing waited for" "$RC/$(wc -l < "$T/calls")" "2/0"
  # 12. read as UTC, the offset does not matter
  fresh; adjtime UTC; marker 60 UTC -0400
  check "12 read as UTC: trusted whatever the time zone" "$(trusted)" "trusted"
  fresh; adjtime LOCAL; marker 60 LOCAL -0500
  nw 0.3 0 0.1 0 9x
  check "13 a longest wait that is no number: a wrong use" "$RC" 2
}

# clock-measure: what it keeps of this start's reading of the clock
cm() { env -i PATH="$T/bb" TZ=EST5 "$T/bb/sh" "$T/cm.sh" >> "$T/out" 2>&1; }
inode() { ls -i "$T/cache/clock-trusted" 2> /dev/null | cut -d' ' -f1; }
# the time the marker says, 0 where there is none (an arithmetic error would end the suite unseen)
when() { local w; w=$(sed -n 2p "$T/cache/clock-trusted" 2> /dev/null); case "$w" in '' | *[!0-9]*) echo 0 ;; *) echo "$w" ;; esac; }
suite_cm() {
  moved "$CM" "$T/cm.sh"
  # the RTC keeps local time (-0500), 1 s behind
  fresh; adjtime LOCAL; : > "$T/synced"; echo $(($(now) - 18000 - 1)) > "$T/rtc"
  cm
  check "a: local time, 1 s off: found right, as LOCAL in -0500" "$(sed -n 1p "$T/cache/clock-trusted")/$(sed -n 3,4p "$T/cache/clock-trusted" | tr '\n' ' ')" "trusted/LOCAL -0500 "
  check "a: when, within a few seconds of now" "$(( $(now) - $(when) < 5 ))" 1
  i=$(inode); cm
  check "b: found right again a moment later: not written again" "$(inode)" "$i"
  echo $(($(now) - 18000 - 120)) > "$T/rtc"; cm
  check "c: 120 s off: the marker is taken away" "$([ -e "$T/cache/clock-trusted" ] && echo there || echo gone)" gone
  check "c: and said" "$(grep -c 'taken away' "$T/out")" 1
  fresh; adjtime UTC; : > "$T/synced"; echo $(($(now) + 2)) > "$T/rtc"; cm
  check "d: UTC, 2 s ahead: found right as UTC" "$(sed -n 1p "$T/cache/clock-trusted")/$(sed -n 3p "$T/cache/clock-trusted")" "trusted/UTC"
  printf 'trusted\n%s\nUTC\n-0500\nold\n' $(($(now) - 8 * 86400)) > "$T/cache/clock-trusted"; i=$(inode); cm
  check "e: found right 8 days ago: written again, with the time of now" "$([ "$(inode)" != "$i" ] && echo new || echo same)/$(( $(now) - $(when) < 5 ))" "new/1"
  fresh; adjtime LOCAL; rm -f "$T/synced"; echo $(($(now) - 18000)) > "$T/rtc"; cm
  check "f: the time has not come from the internet: nothing kept" "$([ -e "$T/cache/clock-trusted" ] && echo there || echo none)" none
}

: > "$T/out"
suite_nw; suite_cm
echo "network-wait: $ok checks passed, $bad failed"
total=$bad

# the mutations: each must fail a check
mutate() { # <name> <script var> <sed expression>
  local name=$1 which=$2 expr=$3 keep=$NW keepc=$CM f b
  f=$T/mut-$name; cp "$([ "$which" = nw ] && echo "$NW" || echo "$CM")" "$f"
  sed -i "$expr" "$f"
  if cmp -s "$f" "$([ "$which" = nw ] && echo "$NW" || echo "$CM")"; then echo "MUTATION $name NOT APPLIED"; total=$((total + 1)); return; fi
  [ "$which" = nw ] && NW=$f || CM=$f
  ok=0; bad=0
  { suite_nw; suite_cm; } > "$T/mut.log" 2>&1
  b=$bad; NW=$keep; CM=$keepc
  if [ $b -gt 0 ]; then echo "mutation $name caught ($b checks failed: $(grep '^FAIL' "$T/mut.log" | cut -c6- | tr '\n' ';'))"; else echo "MUTATION $name SURVIVED"; total=$((total + 1)); fi
  JUDGED=$((JUDGED + 1))
}
JUDGED=0
mutate M1 nw 's/if \[ "\$now" -lt "\$l2" \]; then/if false; then/'
mutate M2 nw 's/if \[ \$((now - l2)) -gt \$MAX_AGE \]; then/if false; then/'
mutate M3 nw 's/if \[ "\$mode" != "\$l3" \]; then/if false; then/'
mutate M4 nw 's/if \[ "\$mode" = LOCAL \] \&\& \[ "\$(date +%z)" != "\$l4" \]; then/if false; then/'
mutate M5 nw 's/^if ! clock_trusted; then/if true; then/'
mutate M6 cm 's/^TRUST_MARGIN=30/TRUST_MARGIN=100000/'
mutate M7 cm 's/^  r=\$((rtc - o - now))/  r=$((rtc - now))/'
# (each mutation judged: one whose suite stopped half way would say nothing)
[ $JUDGED = 7 ] || { echo "FAIL only $JUDGED of 7 mutations were judged"; total=$((total + 1)); }
[ $total = 0 ] && { echo "NETWORK-WAIT TEST PASSED"; exit 0; }
echo "NETWORK-WAIT TEST FAILED"; exit 1
