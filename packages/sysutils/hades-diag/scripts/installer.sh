#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: a start of the boot entry "Installer" (hades-diag-installer.service). That start keeps nothing:
# its storage is in RAM, the media centre and the rest of the diagnostics do not run, and on some PCs it is the start
# that hangs or shows nothing. While the switch of the diagnostics is on (a file whose name starts with DIAG-ON at the
# top of the boot partition, as hades-diag.service looks for it), what it says is kept on that partition as it goes,
# in the folder of the start, hades-diag/start-N/installer (folder.sh: numbered on from the start before and made
# this start's before anything goes into it; the starts before the last 7 go, as start.sh does it): the journal of
# this start, from its first line, in journal.txt, and the log of the installer (/tmp/installer/install.log) in
# install.txt, both through the filter for logs (redact.sh), with build.txt and filter.txt as each capture has them.
# Every 2 s what is new is filtered and appended, under the lock of the writers of the boot partition (the
# installer's own "Save log" takes it too), and the partition is made read-only again: what was written is on the
# drive and its file system is clean, also when the PC hangs or is turned off by force a moment later. Bounded: 8 MiB
# of journal and the last 1 MiB of the installer's log; nothing is written where the partition would be left with
# 128 MiB or less (NO-ROOM.txt then says so, as for the other parts of the diagnostics). No other disk is touched or
# mounted, and nothing on the boot partition is run. With the switch off nothing of this runs. What was done is in
# the kernel log, on lines that start with "hades-diag:".
FLASH=/flash
RUN=/run
PROC=/proc
KMSG=/dev/kmsg
L=/usr/lib/hades-diag
LOG=/tmp/installer/install.log
# (seconds between two looks; bytes of the journal and of the installer's log at most; KiB that stay free)
EVERY=2
JMAX=8388608
IMAX=1048576
ROOM=131072
D=$FLASH/hades-diag
R=$L/redact.sh
S=$RUN/hades-diag-installer

say() { echo "hades-diag: $*" >> $KMSG 2>/dev/null || echo "hades-diag: $*"; }
# The type and the options of the file system at $FLASH, as /proc/mounts has them
flash_mount() { awk -v f="$FLASH" '$2 == f { t = $3; o = $4 } END { if (t != "") print t, o }' $PROC/mounts; }
free_kib() { df -k $FLASH 2>/dev/null | awk 'END { print $4 + 0 }'; }
# The last $1 bytes of the file $2, from the start of a line: an address that is cut in two is no address to the
# filter (as shutdown.sh takes them)
last() {
  local z
  z=$(($(wc -c < "$2")))
  if [ $z -gt $1 ]; then tail -c +$((z - $1 + 1)) "$2" | sed 1d; else cat "$2"; fi
}
# One writer of the boot partition at a time: the lock of the diagnostics, a directory in RAM, waited for 5 s at most
# (the installer's "Save log" holds it for a moment); not had, this look writes nothing and the next one tries again
lock() {
  local n=0
  while ! mkdir $RUN/hades-flash.lock 2>/dev/null; do
    n=$((n + 1)); [ $n -le 5 ] || return 1; sleep 1
  done
}
unlock() { rmdir $RUN/hades-flash.lock 2>/dev/null; }
# The boot partition writable for a moment (it is mounted read-only), and read-only again afterwards: that puts what
# was written on the drive and marks its file system clean. Where it cannot be made read-only again (a file is open
# on it), what is on it is put on the drive all the same.
writable() {
  WAS_RO=
  case ",$(flash_mount | cut -d' ' -f2)," in
    *,ro,*) mount -o remount,rw $FLASH || return 1; WAS_RO=1 ;;
  esac
}
settle() {
  [ -n "$WAS_RO" ] && mount -o remount,ro $FLASH && return 0
  sync -f $FLASH 2>/dev/null || sync
}

grep -qw installer $PROC/cmdline || exit 0
ls $FLASH 2>/dev/null | grep -qi '^diag-on' || exit 0
[ -f $R ] || { say "the filter of the diagnostics is missing: nothing of this start of the installer is kept"; exit 0; }
case "$(flash_mount)" in
  "vfat "*) ;;
  *) say "the boot partition is not a FAT file system at $FLASH: nothing of this start of the installer is kept"; exit 0 ;;
esac
. $L/folder.sh
rm -rf $S
mkdir -p $S || exit 0
N=$(next_start)
H=start-$N
# (the starts before the last 7 go, with the first write: with this one, 8 are kept)
GONE=$(old_starts 7)
J=0 JDONE= STOP= TERM= SL= FIRST=1 CUR= ISIG=

# One look: what is new in the journal and in the installer's log, filtered, and written
look() {
  local new= isig= cut= keep need w
  : > $S/j.f
  rm -f $S/i.f
  if [ -z "$JDONE" ]; then
    # (-q: no "-- No entries --"; the cursor of the last entry given is on a line of its own at the end, and the
    # next look asks for the entries after it: none is given twice, and none is left out)
    if [ -n "$CUR" ]; then
      journalctl -b 0 -q --no-pager -o short-monotonic --show-cursor --after-cursor="$CUR" > $S/j.raw 2>/dev/null
    else
      journalctl -b 0 -q --no-pager -o short-monotonic --show-cursor > $S/j.raw 2>/dev/null
    fi
    new=$(sed -n 's/^-- cursor: //p' $S/j.raw | tail -n 1)
    sed -i -e '/^-- cursor: /d' -e '/^-- No entries --$/d' $S/j.raw
    # (without a cursor, what came cannot be told from what comes next: it is asked for again with the next look)
    if [ -s $S/j.raw ] && [ -n "$new" ]; then
      if ! sh $R < $S/j.raw > $S/j.f 2>/dev/null; then
        # (what the filter wrote is filtered: it is kept, and says where it stopped)
        echo "[the filter stopped here: of the $(wc -l < $S/j.raw) lines that came with this part of the journal, those after this one are not saved]" >> $S/j.f
      fi
      w=$(($(wc -c < $S/j.f)))
      if [ $((J + w)) -gt $((JMAX - 256)) ]; then
        keep=$((JMAX - 256 - J))
        [ $keep -gt 0 ] && head -c $keep $S/j.f > $S/j.c || : > $S/j.c
        [ -s $S/j.c ] && [ "$(tail -c 1 $S/j.c | wc -l)" != 1 ] && sed -i '$d' $S/j.c
        echo "[cut here: the $((JMAX / 1048576)) MiB of the journal that are kept here are full; what came after is not saved]" >> $S/j.c
        mv -f $S/j.c $S/j.f
        cut=1
      fi
    else
      new=
    fi
  fi
  if [ -f $LOG ]; then
    isig=$(md5sum < $LOG)
    if [ "$isig" != "$ISIG" ]; then
      if ! last $IMAX $LOG | sh $R > $S/i.f 2>/dev/null; then
        echo "[the filter stopped here: what came after this line of the installer's log is not saved]" >> $S/i.f
      fi
    else
      isig=
    fi
  fi
  [ -s $S/j.f ] || [ -f $S/i.f ] || return 0
  need=$(( $(wc -c < $S/j.f) + $(cat $S/i.f 2>/dev/null | wc -c) ))
  lock || return 0
  if writable; then
    if [ "$(free_kib)" -le $((ROOM + need / 1024 + 64)) ]; then
      # (a note on the partition says so, once: nothing else there would tell why nothing was saved)
      [ -e $D/NO-ROOM.txt ] || { mkdir -p $D && cp $L/NO-ROOM.txt $D/; }
      say "the boot partition has $(free_kib) KiB free: nothing more of this start of the installer is kept"
      STOP=1
    elif own_start $H; then
      if [ -n "$FIRST" ]; then
        for k in $GONE; do rm -rf $D/$k; done
        rm -f $D/NO-ROOM.txt
        [ "$(cat $D/README.txt 2>/dev/null)" = "$(cat $L/README.txt)" ] || cp $L/README.txt $D/
        [ "$(cat $D/LEEME.txt 2>/dev/null)" = "$(cat $L/LEEME.txt)" ] || cp $L/LEEME.txt $D/
        grep -q '^kind: ' $D/$H/start.txt || echo "kind: a start of the boot entry Installer" >> $D/$H/start.txt
        mkdir -p $D/$H/installer
        this_build > $D/$H/installer/build.txt
        say "this start of the installer is kept in hades-diag/$H/installer on the boot partition"
      fi
      if [ -s $S/j.f ] && cat $S/j.f >> $D/$H/installer/journal.txt; then
        J=$((J + $(wc -c < $S/j.f)))
        CUR=$new
        [ -n "$cut" ] && { JDONE=1; say "the journal of this start of the installer reached its $((JMAX / 1048576)) MiB: the rest of it is not kept"; }
      fi
      if [ -f $S/i.f ] && cp $S/i.f $D/$H/installer/install.txt.new && mv -f $D/$H/installer/install.txt.new $D/$H/installer/install.txt; then
        ISIG=$isig
      fi
      [ -n "$FIRST$TERM" ] && { sh $R hostnote $D/$H/installer > $S/filter.txt 2>/dev/null && cp $S/filter.txt $D/$H/installer/filter.txt; }
      echo $FILTERED > $D/$H/.filtered
      FIRST=
    else
      say "the folder $H could not be made this start's: nothing of this start of the installer is kept"
      STOP=1
    fi
    settle
  fi
  unlock
}

# Stopped: one more look, begun after the stop, so that what came until then is kept, then the end (KillMode=mixed
# leaves what this has started running until then)
trap 'TERM=1; [ -n "$SL" ] && kill $SL 2>/dev/null' TERM INT HUP
while [ -z "$STOP" ]; do
  t=$TERM
  look
  [ -n "$t" ] && break
  [ -n "$TERM" ] && continue
  sleep $EVERY &
  SL=$!
  wait $SL
  SL=
done
rm -rf $S
exit 0
