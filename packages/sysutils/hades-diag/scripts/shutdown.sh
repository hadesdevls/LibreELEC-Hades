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
# start's before anything goes into it (folder.sh). The stop is bounded: the last 16 MiB of each of Kodi's logs,
# from the start of a line, the journal read for 10 s at most, all of them filtered for 20 s at most, the end of
# each first (what was filtered by then is kept, and says what is missing), the lock for 10 s.
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
# The logs as they are, before their filter, are in $U, in RAM, and nothing of $U is ever copied.
U=/run/hades-shutdown-in
rm -rf $U; mkdir -p $U
# A process and all that it started, and they theirs (busybox has no setsid: they are found by their parent)
tree() {
  local c
  echo $1
  for c in $(pgrep -P $1); do tree $c; done
}
# Logs through the filter for logs, all at once, for at most $1 s: limited <seconds> <name> [<name> ...], each
# $U/<name>.in packed into $T/<name>. The end of each goes through first, its last 2 MiB (from the start of a line
# that is no row of hex: see hold() in redact-ids.awk), then the rest, so that where the time is up the end of the
# log, where it stopped, is there. Each is fed to its filter by a process of its own (into a FIFO); the filter gives
# a line for each line it reads (and a line of its own before a batch whose digests sha256sum did not give, which
# is not counted), and what it gives is packed in two parts, $U/<name>.b.gz (the end) and .a.gz, put back in the
# order of the log once it has ended. When the time is up, the feeding stops: the filter ends what it had (the line
# it was cutting through left out: HADES_REDACT_DROP_UNENDED), what it wrote stays, a line where lines are missing
# says which, and so does shutdown.txt. A filter that has not ended 5 s after that is stopped with all its processes,
# nothing of it is kept, and shutdown.txt says so. So nothing gets to the boot partition that the filter did not
# write. (A last line with no line end is left out first: it was being written.)
limited() {
  local s=$1 jobs= feeds= n k p c t tb ta
  shift
  for n in "$@"; do
    [ "$(tail -c 1 $U/$n.in | wc -l)" = 1 ] || sed -i '$d' $U/$n.in
    t=$(wc -l < $U/$n.in)
    if [ $(($(wc -c < $U/$n.in))) -le 2097152 ]; then
      tb=$t
    else
      # (the first line of the last 2 MiB may have begun before them: it goes with the rest)
      tb=$(tail -c 2097152 $U/$n.in | awk 'NR > 1 && (s || ($0 !~ /^[ \t]*[0-9a-f]{32}[ \t]*$/ && $0 !~ /^([0-9a-f][0-9a-f] ){15}[0-9a-f][0-9a-f] *$/)) { s = 1; n++ } END { print n + 0 }')
    fi
    ta=$((t - tb))
    echo "$tb $ta" > $U/$n.lines
    mkfifo $U/$n.fifo || { echo "${n%.gz}: not saved, no FIFO for its filter" >> $T/shutdown.txt; continue; }
    ( HADES_REDACT_DROP_UNENDED=1 sh $R < $U/$n.fifo | awk -v N=$tb -v A="gzip > $U/$n.a.gz" -v B="gzip > $U/$n.b.gz" -v C=$U/$n.count '
        BEGIN { printf "" | A; printf "" | B }
        index($0, "[the filter could not make ") != 1 { n++ }
        { if (n <= N) print | B; else print | A }
        END { close(A); close(B); print n + 0 > C }' ) &
    jobs="$jobs $!:$n"
    { tail -n $tb $U/$n.in; [ $ta -gt 0 ] && head -n $ta $U/$n.in; } > $U/$n.fifo &
    feeds="$feeds $!"
  done
  ( sleep $s; for p in $feeds; do kill $(tree $p) 2>/dev/null; done; sleep 5; for p in $jobs; do kill $(tree ${p%%:*}) 2>/dev/null; done ) >/dev/null 2>&1 &
  k=$!
  for p in $jobs; do wait ${p%%:*}; done
  kill $(tree $k) 2>/dev/null
  for p in $jobs; do
    n=${p#*:}
    read tb ta < $U/$n.lines
    t=$((tb + ta))
    if [ ! -f $U/$n.count ]; then
      echo "${n%.gz}: not saved, its filter had not ended 5 s after the $s s it had" >> $T/shutdown.txt
      continue
    fi
    c=$(cat $U/$n.count)
    if [ $c -ge $t ]; then
      cat $U/$n.a.gz $U/$n.b.gz > $T/$n
    elif [ $c -ge $tb ]; then
      { cat $U/$n.a.gz; echo "[cut here: the filter had not finished after $s s; lines $((c - tb + 1)) to $ta of the $t lines of the log are not saved; the last $tb, which went first, are]" | gzip; cat $U/$n.b.gz; } > $T/$n
      echo "${n%.gz}: lines $((c - tb + 1)) to $ta of its $t lines not saved: its filter had not finished after $s s (its last $tb lines went first, and are saved)" >> $T/shutdown.txt
    else
      { [ $ta -gt 0 ] && echo "[cut here: the filter had not finished after $s s; lines 1 to $ta of the $t lines of the log are not saved]" | gzip; cat $U/$n.b.gz; echo "[cut here: the filter had not finished after $s s; the last $((tb - c)) of the $t lines are not saved]" | gzip; } > $T/$n
      echo "${n%.gz}: only lines $((ta + 1)) to $((ta + c)) of its $t lines saved: its filter had not finished after $s s (its last $tb lines went first)" >> $T/shutdown.txt
    fi
  done
}
# Kodi's logs (their last 16 MiB, from the start of a line) and the journal (what journalctl gives in 10 s at most:
# its last line, if cut, is left out), filtered all at once for at most 20 s: with the 5 s after it
# and the lock's 10 s, the stop stays within the 60 s that hades-diag.service gives it on a slow PC too, and the
# folder is written.
journalctl -b --no-pager > $U/journal.txt.gz.in 2>&1 &
jw=$!
( sleep 10; kill $jw 2>/dev/null ) >/dev/null 2>&1 &
jk=$!
set --
for f in /storage/.kodi/temp/kodi.log /storage/.kodi/temp/kodi.old.log; do
  [ -f $f ] && last 16777216 $f > $U/${f##*/}.gz.in && set -- "$@" ${f##*/}.gz
done
wait $jw
[ $? -ge 128 ] && echo "journal: read for 10 s, and cut there" >> $T/shutdown.txt
kill $jk 2>/dev/null
limited ${HADES_DIAG_LOG_SECONDS:-20} "$@" journal.txt.gz
rm -rf $U
dmesg | sh $R ids | gzip > $T/dmesg.txt.gz
# storage: the timeline kept by watch-dv.sh and the final statistics of each disk
[ -f /run/hades-io.log ] && sh $R ids < /run/hades-io.log | gzip > $T/io-timeline.txt.gz
{
  grep -E ' /storage | /flash ' /proc/mounts
  echo "(/sys/block/<dev>/stat: reads rmerged rsectors read_ms writes wmerged wsectors write_ms in_flight busy_ms weighted_ms discards dmerged dsectors discard_ms flushes flush_ms)"
  for b in /sys/block/sd* /sys/block/nvme* /sys/block/mmcblk*; do [ -e "$b/stat" ] && echo "${b##*/}:$(cat $b/stat)"; done
} 2>&1 | sh $R ids > $T/storage.txt
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
rm -rf $T $U
exit 0
