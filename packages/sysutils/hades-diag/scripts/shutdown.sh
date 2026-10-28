#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: at every power-off or reboot (hades-diag.service is stopped after Kodi has stopped),
# copy this session's Kodi logs and journal, the kernel log, and what is connected to each output at that
# moment to hades-diag/start-N/shutdown/ on the boot partition (N: the number of this start, as start.sh
# left it), with build.txt, the build that wrote them. The logs go through the filter for logs (redact.sh),
# everything else through the filter for identifiers (redact.sh ids: addresses, serial numbers, UUIDs, SSH
# keys, network names and the name of the system become markers). Without the filter nothing is copied, and
# nothing is done once the switch (a file DIAG-ON... at the top of the boot partition) is gone, or with less
# than 128 MiB free there (a note on the partition then says so). The folder of the start is made this
# start's before anything goes into it (folder.sh). The stop is bounded: the last 16 MiB of each log, from
# the start of a line, the journal for 15 s at most, the lock for 10 s.
# The files are prepared in /run, which stays mounted until the very end (/tmp is unmounted meanwhile), and
# only the boot partition is flushed, by remounting it read-only: a global sync would also flush everything
# pending on the (slow) data partition here.
D=/flash/hades-diag
R=/usr/lib/hades-diag/redact.sh
ls /flash 2>/dev/null | grep -qi '^diag-on' && [ -f $R ] || exit 0
. /usr/lib/hades-diag/folder.sh
N=$(cat /run/hades-diag-start 2>/dev/null)
# (without the number of this start: a start of its own, numbered as start.sh numbers one)
case "$N" in
  ''|*[!0-9]*) N=$(next_start) ;;
esac
H=start-$N
T=/run/hades-shutdown
rm -rf $T; mkdir -p $T
this_build > $T/build.txt
# power-off or reboot: the job that systemd is running, asked for at most 5 s
systemctl list-jobs --no-legend > $T/jobs 2>/dev/null &
j=$!
( sleep 5; kill $j 2>/dev/null ) >/dev/null 2>&1 &
w=$!
wait $j
kill $w 2>/dev/null
reason=$(sed -n -E 's/.* (poweroff|reboot|halt|kexec)\.target .*/\1/p' $T/jobs | head -n 1)
rm -f $T/jobs
{
  date -u '+%F %T UTC'; echo "reason: ${reason:-not known}"; echo "uptime: $(cut -d' ' -f1 /proc/uptime) s"
  # (as the kernel has them now: nothing is asked of an output for this)
  echo "== outputs at this moment"
  for c in /sys/class/drm/card*-*; do
    [ -e $c/status ] && echo "${c##*/}: status=$(cat $c/status 2>/dev/null) enabled=$(cat $c/enabled 2>/dev/null) dpms=$(cat $c/dpms 2>/dev/null)"
  done
  echo "== graphics cards, and whether each is awake"
  for c in /sys/class/drm/card[0-9]*; do
    case ${c##*/} in *-*) continue ;; esac
    echo "${c##*/}: driver=$(basename "$(readlink $c/device/driver 2>/dev/null)") runtime_status=$(cat $c/device/power/runtime_status 2>/dev/null)"
  done
  echo "kernel display messages: drm.debug=$(cat /sys/module/drm/parameters/debug 2>/dev/null)"
} 2>&1 | sh $R ids > $T/shutdown.txt
# The last $1 bytes of the file $2, from the start of a line: an address that is cut in two is no address to
# the filter.
last() {
  z=$(($(wc -c < "$2")))
  if [ $z -gt $1 ]; then tail -c +$((z - $1 + 1)) "$2" | sed 1d; else cat "$2"; fi
}
for f in /storage/.kodi/temp/kodi.log /storage/.kodi/temp/kodi.old.log; do
  [ -f $f ] && last 16777216 $f | sh $R | gzip > $T/${f##*/}.gz
done
dmesg | sh $R ids | gzip > $T/dmesg.txt.gz
# storage: the timeline kept by watch-dv.sh and the final statistics of each disk
[ -f /run/hades-io.log ] && sh $R ids < /run/hades-io.log | gzip > $T/io-timeline.txt.gz
{
  grep -E ' /storage | /flash ' /proc/mounts
  echo "(/sys/block/<dev>/stat: reads rmerged rsectors read_ms writes wmerged wsectors write_ms in_flight busy_ms weighted_ms discards dmerged dsectors discard_ms flushes flush_ms)"
  for b in /sys/block/sd* /sys/block/nvme* /sys/block/mmcblk*; do [ -e "$b/stat" ] && echo "${b##*/}:$(cat $b/stat)"; done
} 2>&1 | sh $R ids > $T/storage.txt
# the journal, for at most 15 s
journalctl -b --no-pager 2>&1 | sh $R | gzip > $T/journal.txt.gz &
j=$!
( sleep 15; kill $j 2>/dev/null ) >/dev/null 2>&1 &
w=$!
wait $j
kill $w 2>/dev/null
# One writer of the boot partition at a time (watch-dv.sh may still be copying a capture): wait for its
# lock at most 10 s, then write anyway.
w=0
while ! mkdir /run/hades-flash.lock 2>/dev/null && [ $w -lt 10 ]; do w=$((w + 1)); sleep 1; done
if mount -o remount,rw /flash; then
  if [ "$(df -k /flash 2>/dev/null | awk 'END { print $4 + 0 }')" -gt 131072 ]; then
    if own_start $H; then
      rm -rf $D/NO-ROOM.txt $D/$H/shutdown
      mkdir -p $D/$H/shutdown && cp $T/* $D/$H/shutdown/ && echo $FILTERED > $D/$H/.filtered
    fi
  else
    # (a note on the partition says so, once: nothing else there would tell why nothing was saved)
    [ -e $D/NO-ROOM.txt ] || { mkdir -p $D && cp /usr/lib/hades-diag/NO-ROOM.txt $D/; }
  fi
  mount -o remount,ro /flash
fi
rmdir /run/hades-flash.lock 2>/dev/null
rm -rf $T
exit 0
