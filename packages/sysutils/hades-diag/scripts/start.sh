#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: started by hades-diag.service before Kodi, when the boot partition has a file whose name
# starts with DIAG-ON (every image carries DIAG-OFF.TXT: the name is the switch). Each start of the system
# has a folder of its own in hades-diag on that partition, start-N: its two snapshots, what is captured
# around Dolby Vision, and the logs of its shutdown. N goes up by one with each start, also across an update of
# the system and once the folders are gone (folder.sh), and the last 8 starts are kept: a drive that is taken from
# one PC to the next keeps what each of them had to tell. A note there says what is in the folders. Nothing on
# the boot partition is run.
D=/flash/hades-diag
L=/usr/lib/hades-diag
ls /flash 2>/dev/null | grep -qi '^diag-on' || exit 0
. $L/folder.sh
# (what a watcher that was stopped left behind: nothing of this is running when the unit starts)
rmdir /run/hades-flash.lock 2>/dev/null
rm -rf /run/hades-during-dv-[1-6] /run/hades-after-dv-[1-6]
# The number of this start: one more than the start before it (folder.sh). The starts that are there before
# the last 7 go, by their names: with this one, 8 are kept.
N=$(next_start)
echo $N > /run/hades-diag-start
start_mark > /dev/null
gone=$(start_folders | awk '{ a[NR] = $2 } END { for (i = 1; i <= NR - 7; i++) print a[i] }')
# A test drive made for an earlier build carried a collector of its own, which the init ran from the boot
# partition: its scripts go (the image does that work now), what it collected stays. Its launcher in
# /storage/.config removes itself when it finds them gone: it is run after this one.
old=
grep -q '^# Hades test stick' /flash/mount-storage.sh 2>/dev/null && old=1
new=1
[ "$(cat $D/README.txt 2>/dev/null)" = "$(cat $L/README.txt)" ] && [ "$(cat $D/LEEME.txt 2>/dev/null)" = "$(cat $L/LEEME.txt)" ] && new=
if [ -n "$old$new$gone" ] && mount -o remount,rw /flash; then
  if [ -n "$old" ]; then
    rm -f /flash/mount-storage.sh
    for f in autostart.sh collect.sh watch-dv.sh shutdown.sh redact.sed; do
      grep -q '^# Hades test stick' $D/$f 2>/dev/null && rm -f $D/$f
    done
  fi
  [ -n "$new" ] && mkdir -p $D && cp $L/README.txt $L/LEEME.txt $D/
  for k in $gone; do rm -rf $D/$k; done
  mount -o remount,ro /flash
fi
B=$(sed -n 's/^VERSION="*\([^"]*\)"*$/\1/p' /etc/os-release)
sh $L/collect.sh "$B" "$N"
sh $L/watch-dv.sh "$N"
exit 0
