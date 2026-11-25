#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# LibreELEC Hades: the part of picture_race_qemu.sh that runs in the guest, as root.
# usage: race.sh <hades-picture> [normal]
# Without "normal": a picture of the start that takes seconds to load is asked for, and "release" comes 0.4 s later.
# With "normal": a picture of the start, then "release", then another picture of the start, one after the other.
# Then the line RACE-MASTER y|n: whether the service holds the DRM master of the device (from debugfs); without "normal",
# also RELEASE-MASTER y|n, the same at the moment "release" has answered (when Kodi would start and ask for the master).
BIN=$1
systemctl stop kodi.service hades-picture.service hades-picture.socket > /dev/null 2>&1
sleep 1
for p in $(pidof hades-picture-tree hades-picture-mutant hades-picture); do kill $p; done
sleep 1
rm -f /run/hades-picture.kodi /run/hades-picture.held /run/hades-picture.sock
grep -q ' /flash [^ ]* rw' /proc/mounts || mount -o remount,rw /flash
[ -f /flash/slow.png ] || cp /storage/pay/slow.png /flash/slow.png
grep -q debugfs /proc/mounts || mount -t debugfs none /sys/kernel/debug
for c in /sys/class/drm/card0-*; do echo "display ${c##*/}: $(cat $c/status) $(cat $c/enabled)"; done
$BIN serve 2> /tmp/race-serve.log &
SP=$!
sleep 0.5
if [ "$2" = normal ]; then
  echo "NORMAL-1 $($BIN show fit boot /flash/slow)"
  echo "NORMAL-2 $($BIN release)"
  echo "NORMAL-3 $($BIN show fit boot /flash/slow)"
else
  ( $BIN show fit boot /flash/slow > /tmp/race-show.out 2>&1 ) &
  WP=$!
  sleep 0.4
  said=$($BIN release)
  rm=$(cat /sys/kernel/debug/dri/*/clients 2> /dev/null | awk -v p=$SP '$2 == p { print $4 }' | sort -u)
  echo "RELEASE $said"
  echo "RELEASE-MASTER ${rm:-none}"
  wait $WP
  echo "SHOW $(cat /tmp/race-show.out)"
fi
sleep 0.5
echo "service:"; cat /tmp/race-serve.log
cat /sys/kernel/debug/dri/*/clients 2> /dev/null | sort -u
m=$(cat /sys/kernel/debug/dri/*/clients 2> /dev/null | awk -v p=$SP '$2 == p { print $4 }' | sort -u)
echo "RACE-MASTER ${m:-none}"
kill $SP
sleep 1
rm -f /run/hades-picture.kodi /run/hades-picture.held
