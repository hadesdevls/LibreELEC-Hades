#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the copies of the kernel log of a start, from the moment systemd takes the start over until the journal of the
# start is in the storage (hades-diag-early.service; the init of the initramfs made the ones before, hades_diag_early_log,
# and only it has systemd start this, while the switch of the diagnostics is on, through a drop-in of sysinit.target in
# /run). Each second, where the log has a new last line, the whole log goes to
# /storage/.cache/hades-diag/early-log/<boot ID>.txt (a new file, synced, renamed over the copy before, synced again),
# numbered on from the init's (/run/hades-diag-early: ID, N). Once journald says that it has moved the journal to the
# storage (/run/systemd/journal/flushed), the copy is deleted: the journal has all of it. After 120 s without that, the
# copy stays, for the first snapshot of the next start (collect.sh, early_log). Lines in the kernel log start with
# "hades-diag: early-log:".

ID=
N=0
. /run/hades-diag-early 2>/dev/null
[ -n "$ID" ] || exit 0
D=/storage/.cache/hades-diag/early-log
F=$D/$ID.txt
BUILD=$( . /etc/os-release 2>/dev/null; echo "$PRETTY_NAME")

write() { # copy number, where the start is
  local up rest
  read up rest < /proc/uptime
  {
    echo "hades-diag: the kernel log of a start, copied to the storage until the journal of the start was there"
    echo "start: boot ID $ID (journalctl --list-boots names the starts by it); build: $BUILD"
    echo "copy $1, $up s after the kernel started: $2"
    echo "---"
    dmesg
  } > $F.new 2> /dev/null && sync -f $F.new && mv -f $F.new $F && sync -f $F
}

i=0
last=
while [ $i -lt 120 ]; do
  if [ -e /run/systemd/journal/flushed ]; then
    rm -f $F $F.new
    sync -f $D 2> /dev/null
    echo "hades-diag: early-log: the journal of this start is in the storage: the copy of its kernel log ($N copies) is deleted" > /dev/kmsg
    exit 0
  fi
  key=$(dmesg | sed -n '$p')
  if [ "$key" != "$last" ]; then
    N=$((N + 1))
    write $N "systemd, before the journal of the start was in the storage" && last=$key
  fi
  i=$((i + 1))
  sleep 1
done
echo "hades-diag: early-log: the journal of this start was not in the storage after 120 s: the copy of its kernel log stays ($N copies)" > /dev/kmsg
