#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades: the check of the output for Dolby Vision (hades-output-check), made once when the settings asked for it
# (System > Submit Log, "Check the Dolby Vision output": the file request below, then Kodi started again).
# hades-output-check.service runs this before Kodi starts, and Kodi waits for it; without the request the unit is
# not started at all (its condition), so a start costs nothing. The request goes first, so that a check that
# fails is not made again at the next start. The report goes to result.txt below and the file new tells the
# settings to show it once Kodi is back; the tool also writes its result line to the kernel log. With the
# diagnostics on, the report goes into the folder of this start too (output-check/, through their filter).
C=/storage/.cache/hades-output-check
L=/usr/lib/hades-diag
[ -e $C/request ] || exit 0
rm -f $C/request $C/new
mkdir -p $C
# (the tool stops itself after 90 s; the unit gives this 120)
$L/hades-output-check --out $C/result.txt > /dev/null 2>&1
rc=$?
if [ $rc -ne 0 ] || [ ! -s $C/result.txt ]; then
  printf 'result: not-judged\nreason: tool\nsummary: the check did not finish (exit status %s)\nmeaning: nothing was measured.\n' "$rc" > $C/result.txt
fi
touch $C/new

# Into the folder of this start of the diagnostics, through their filter, while they are on
ls /flash 2>/dev/null | grep -qi '^diag-on' || exit 0
D=/flash/hades-diag
R=$L/redact.sh
[ -f $R ] && [ -s /run/hades-diag-start ] || exit 0
. $L/folder.sh
H=start-$(cat /run/hades-diag-start)
T=/tmp/hades-output-check
rm -rf $T $T.filtered
mkdir -p $T && cp $C/result.txt $T/output-check-$(date -u +%H%M%S).txt && sh $R tree $T $T.filtered || { rm -rf $T $T.filtered; exit 0; }
# (one writer of the boot partition at a time, as collect.sh and watch-dv.sh; 128 MiB are always left free)
n=0
while ! mkdir /run/hades-flash.lock 2>/dev/null; do
  n=$((n + 1)); [ $n -le 60 ] || { rm -rf $T $T.filtered; exit 0; }; sleep 1
done
if mount -o remount,rw /flash; then
  if [ "$(df -k /flash 2>/dev/null | awk 'END { print $4 + 0 }')" -gt 131072 ] && own_start $H; then
    mkdir -p $D/$H/output-check && cp $T.filtered/output-check-*.txt $D/$H/output-check/ && echo $FILTERED > $D/$H/.filtered
  fi
  mount -o remount,ro /flash
fi
rmdir /run/hades-flash.lock 2>/dev/null
rm -rf $T $T.filtered
exit 0
