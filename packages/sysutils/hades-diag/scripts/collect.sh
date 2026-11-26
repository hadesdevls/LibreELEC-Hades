#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: the diagnostics of a start on real hardware, written to the boot partition (FAT,
# readable in Windows) as hades-diag/start-N/1-minute/ and hades-diag/start-N/4-minutes/, about 1 and 4
# minutes after Kodi is started, at every start. Each snapshot is copied to the FAT as soon as it is taken,
# so an early power-off still leaves the first one. Commands that talk to the GPU run in the background and
# are never waited for, so a wedged GPU cannot block the copy. Kodi's logs (the last 8 MiB of each, packed)
# and the journal pass through the filter for logs (redact.sh) where they are taken, and every file of a
# snapshot through the filter that fits it (redact.sh tree: addresses, serial numbers, UUIDs, SSH keys,
# network names and the name of the system become markers) before it is copied; without the filter nothing
# is copied. Nothing is written once the switch is off, or with little room left on the partition, and the
# folder of the start is made this start's before anything goes into it (folder.sh). Nothing is powered off or
# reconfigured.
# Arguments: the build (VERSION of os-release), written to build.txt in each snapshot, and the number of
# this start (start.sh).
# All of it only reads what the system is doing, but for what the first snapshot does for a display that is
# plugged in and not seen (redetect.txt; see redetect() and listen() for what each can change):
# - it asks once more the HDMI and DisplayPort outputs that have nothing connected and drive nothing, with
#   the kernel's display messages turned up for those seconds;
# - it lists the PCI devices then (lspci.txt), which wakes a graphics card that sleeps;
# - where no TV or monitor is seen on any socket, the kernel's display messages stay turned up until the
#   second snapshot has the kernel log, or a video is opened.
# It also keeps the firmware's description of the machine (acpi-tables.tar.gz), and how long the start took
# (boot-time.txt, systemd-analyze).
# "collect.sh report <folder>" (report.sh, the report made on demand from the settings) is one snapshot of what the
# system is doing at that moment, "now", with the journal of this start and of the start before it and the newest
# crash log of Kodi, into <folder> (in RAM, this run's, readable by root only), in the foreground, and nothing goes to
# the boot partition: report.sh puts all of it through the filter of the diagnostics. Nothing is asked of an output
# then (no redetect(), no listen()), and the PCI devices are not listed (that wakes a graphics card that sleeps).
(
D=/flash/hades-diag
T=/tmp/hades-diag
R=/usr/lib/hades-diag/redact.sh
. /usr/lib/hades-diag/folder.sh
B=$1
H=start-${2:-0}
# (the report made on demand: its folder is where the snapshot goes, and the build is read here)
REPORT=
if [ "$B" = report ]; then
  REPORT=$2; T=$2; B=$(this_build); H=
fi
# The level of the kernel's display messages, and where what it was is kept while this has it turned up
G=/sys/module/drm/parameters/debug
W=/run/hades-drm-debug.was
# Run a command for at most 20 s, with the shell only (the image has no timeout command). The watcher's
# output goes to /dev/null so that $(lim ...) does not wait for it.
lim() {
  "$@" 2>&1 &
  lp=$!
  ( sleep 20; kill $lp 2>/dev/null ) >/dev/null 2>&1 &
  lw=$!
  wait $lp
  kill $lw 2>/dev/null
}
# Redact a log (identifiers, credentials, paths of media); without the filter, print nothing of the input.
clean() {
  if [ -f $R ]; then sh $R; else cat >/dev/null; echo "(not copied: $R missing)"; fi
}
# The last $1 bytes of the file $2, from the start of a line: an address that is cut in two is no address to
# the filter.
last() {
  z=$(($(wc -c < "$2")))
  if [ $z -gt $1 ]; then tail -c +$((z - $1 + 1)) "$2" | sed 1d; else cat "$2"; fi
}

# One writer of the boot partition at a time (watch-dv.sh captures run in the background): a lock
# directory in RAM, waited for at most 60 s.
flash_lock() {
  n=0
  while ! mkdir /run/hades-flash.lock 2>/dev/null; do
    n=$((n + 1)); [ $n -le 60 ] || return 1; sleep 1
  done
}
flash_unlock() { rmdir /run/hades-flash.lock 2>/dev/null; }
# The switch: a file at the top of the boot partition whose name starts with DIAG-ON (in any case, and with
# any ending). Looked at before each write: turned off, nothing more is written.
switch_on() { ls /flash 2>/dev/null | grep -qi '^diag-on'; }
# Room on the boot partition: nothing is written with less than 128 MiB free (an update needs room there).
room_left() { [ "$(df -k /flash 2>/dev/null | awk 'END { print $4 + 0 }')" -gt 131072 ]; }
# With no room, a note on the partition says so, once: nothing else there would tell why nothing was saved.
no_room() { [ -e $D/NO-ROOM.txt ] || { mkdir -p $D && cp /usr/lib/hades-diag/NO-ROOM.txt $D/; }; }

# Copy the snapshot $1 into the folder of this start on the boot partition: each of its files through the
# filter that fits it first, into $T/$1.filtered in RAM (a file that the filter fails on is not copied;
# without the filter, nothing is). The second copy of a snapshot filters only what changed since the first.
to_flash() {
  [ -z "$REPORT" ] || return 0
  switch_on || return 0
  [ -f $R ] || return 0
  sh $R tree $T/$1 $T/$1.filtered
  flash_lock || return 0
  if mount -o remount,rw /flash; then
    # (without room for a new copy, the one that is there stays; the folder is this start's first: folder.sh)
    if room_left; then
      if own_start $H; then
        rm -rf $D/$H/$1 $D/NO-ROOM.txt
        mkdir -p $D/$H/$1 && cp -r $T/$1.filtered/. $D/$H/$1/ && echo $FILTERED > $D/$H/.filtered
      fi
    else
      no_room
    fi
    mount -o remount,ro /flash
  fi
  flash_unlock
}

# Hades: the file $2 of the snapshot $1, copied as soon as the query that writes it has ended, into the folder of
# this start on the boot partition, through the filter. The queries of the graphics cards run in the background
# and are not waited for: the copy of the snapshot 30 s after them took what they had written, and a PC turned off
# before that lost all of it. Each file is filtered in a folder of its own (the queries end at about the same
# time, and the copies of the snapshot filter into $T/$1.filtered), and copied under the lock of the writers of
# the boot partition, beside what the first copy of the snapshot wrote. Once the last copy of the snapshot is
# made, its files are gone from RAM: a query that ends after it copies nothing more.
file_to_flash() {
  local O=$T/$1.$2
  [ -z "$REPORT" ] || return 0
  switch_on && [ -f $R ] && [ -f $T/$1/$2 ] || return 0
  rm -rf $O $O.filtered
  mkdir -p $O && cp $T/$1/$2 $O/ && sh $R tree $O $O.filtered
  if [ -f $O.filtered/$2 ] && flash_lock; then
    if mount -o remount,rw /flash; then
      if room_left && own_start $H; then
        mkdir -p $D/$H/$1 && cp $O.filtered/$2 $D/$H/$1/ && echo $FILTERED > $D/$H/.filtered
      fi
      mount -o remount,ro /flash
    fi
    flash_unlock
  fi
  rm -rf $O $O.filtered
}

# What versions of the diagnostics before 15 left on the partition went there without the filter of
# identifiers, and what versions before this one left went through it without the names of Bluetooth devices:
# each of their folders (of a start, or of the layout before the folders of starts) is put through the filter
# once, file by file, in RAM, and written back with a file .filtered in it that says which filter it went
# through ($FILTERED, folder.sh), as this version writes it in every folder of a start that it writes to. A
# file that the filter fails on is not written back. The folder is written beside the old one first, and the
# old one goes only when it is there. (Run in the background before the first snapshot: start.sh is waited for
# by Kodi.)
refilter() {
  [ -f $R ] || return 0
  for d in $D/*/; do
    d=${d%/}
    [ -d "$d" ] && [ "$(cat "$d/.filtered" 2>/dev/null)" != "$FILTERED" ] && [ "${d##*/}" != "$H" ] || continue
    case ${d##*/} in *.part) continue ;; esac
    rm -rf $T.refilter
    sh $R tree "$d" $T.refilter
    mkdir -p $T.refilter && echo $FILTERED > $T.refilter/.filtered
    switch_on || break
    flash_lock || break
    # (with less than 128 MiB free, nothing is written: an update needs room there; the next start tries again)
    if room_left && mount -o remount,rw /flash; then
      rm -rf "$d.part"
      cp -r $T.refilter "$d.part" && rm -rf "$d" && mv "$d.part" "$d"
      rm -rf "$d.part"
      mount -o remount,ro /flash
    fi
    flash_unlock
  done
  rm -rf $T.refilter
}

# The graphics devices that Kodi is the DRM master of (the one it shows itself on, and those whose displays it
# turned off and holds): modetest is run on those only, so that it never becomes the master of a device itself,
# and is never the last to close one, which makes the kernel put its console back there. (A device of which
# Kodi only has the render node open, to decode with, has no master: it is left alone.)
kodi_is_master() {
  awk '$1 == "kodi.bin" && $4 == "y" { f = 1 } END { exit !f }' /sys/kernel/debug/dri/${1#card}/clients 2>/dev/null
}
kodi_cards() {
  for c in /sys/class/drm/card[0-9]*; do
    case ${c##*/} in *-*) continue ;; esac
    kodi_is_master ${c##*/} && echo ${c##*/}
  done
}
# Whether the kernel tells at all who has each graphics device open
clients_readable() {
  for f in /sys/kernel/debug/dri/[0-9]*/clients; do [ -r "$f" ] && return 0; done
  return 1
}
# The outputs of those devices and their properties, one device after another (modetest -D <node>: by the
# name of the driver only the first of two devices of one driver was ever listed). A device that Kodi is not
# the master of is not listed (what is connected to it is in results.txt all the same). Only where the kernel
# does not tell who has the devices open: one listing per display driver, as before this was known.
modetest_all() {
  pidof kodi.bin >/dev/null || { echo "kodi.bin not running: skipped"; return; }
  if clients_readable; then
    cards=$(kodi_cards)
    [ -n "$cards" ] || { echo "Kodi is the DRM master of no graphics device now: skipped"; return; }
    for c in $cards; do
      # (asked again just before the device is opened: Kodi may have ended, or started again, meanwhile)
      kodi_is_master $c || { echo "=== $c: Kodi is not its DRM master any more: skipped"; continue; }
      echo "=== $c ($(basename "$(readlink /sys/class/drm/$c/device/driver)") $(basename "$(readlink -f /sys/class/drm/$c/device)"))"
      lim modetest -D /dev/dri/$c -c
    done
  else
    for m in nvidia_drm:nvidia-drm amdgpu:amdgpu i915:i915 xe:xe; do
      [ -d /sys/module/${m%%:*} ] && { echo "=== ${m#*:}"; lim modetest -M ${m#*:} -c; }
    done
  fi
}

video_opened() { grep -qs 'VideoPlayer::OpenFile' /storage/.kodi/temp/kodi.log; }
# Bluetooth (bluetooth.txt): what tells why a device keeps losing its connection. Only read, nothing is asked of
# a device or paired: the kernel's lines on the controllers (the firmware each loaded, or that it kept the one
# that the system before this start had loaded: a restart from Windows), whether a USB controller may sleep, the
# band of the Wi-Fi beside it, the settings that BlueZ runs with, the standby of the settings add-on, and of each
# paired device its kind, its state, which keys the store has (never a key) and the connection parameters that
# it asked for. Its name and alias only as the markers that the filter puts wherever a log names the device
# (redact.sh btname): a name is often a person's ("Magic Keyboard de Juan"). Why each one disconnected is in
# kodi.log ("Bluetooth: ... disconnected").
# (busctl gives up after 5 s by itself, and its exit status tells whether the property was there)
bt_get() { busctl --timeout=5 get-property org.bluez $1 org.bluez.Device1 $2 2>/dev/null; }
bluetooth() {
  echo "== controllers (kernel log)"
  dmesg | grep -E 'Bluetooth: |btusb|btintel' | tail -60
  dmesg | grep -q 'Intel device is already patched' &&
    echo "(an Intel controller kept the firmware patch of the system that ran before this start, a restart from Windows for example: this system did not load its own; after a full power-off it does)"
  for h in /sys/class/bluetooth/hci*; do
    case ${h##*/} in *:*) continue ;; esac
    [ -e "$h/device" ] || continue
    i=$(readlink -f $h/device); u=${i%/*}
    echo "${h##*/}: driver $(basename "$(readlink $i/driver 2>/dev/null)") on ${i##*/}"
    [ -r $u/idVendor ] && echo "    usb $(cat $u/idVendor):$(cat $u/idProduct) power/control=$(cat $u/power/control 2>/dev/null) runtime_status=$(cat $u/power/runtime_status 2>/dev/null) autosuspend_delay_ms=$(cat $u/power/autosuspend_delay_ms 2>/dev/null) wakeup=$(cat $u/power/wakeup 2>/dev/null) suspended_ms=$(cat $u/power/runtime_suspended_time 2>/dev/null) active_ms=$(cat $u/power/runtime_active_time 2>/dev/null)"
  done
  echo "btusb: enable_autosuspend=$(cat /sys/module/btusb/parameters/enable_autosuspend 2>/dev/null) (Y: a USB controller may sleep while no device is connected to it)"
  echo "iwlwifi: bt_coex_active=$(cat /sys/module/iwlwifi/parameters/bt_coex_active 2>/dev/null)"
  for n in /sys/class/net/*; do
    [ -d $n/wireless ] || continue
    echo "${n##*/}: $(lim iw dev ${n##*/} link | grep -E 'freq:|signal:|Not connected' | tr -s ' \t\n' ' ')"
  done
  echo "== rfkill"; lim rfkill list
  echo "== the service and the settings that BlueZ runs with"
  echo "bluetooth: $(lim systemctl is-active bluetooth)"
  grep -h '^BLUEZ_' /storage/.cache/services/bluez.conf 2>/dev/null
  for f in /run/libreelec/bluetooth/main.conf /run/libreelec/bluetooth/input.conf; do
    echo "--- $f (what is set in it; for the rest, BlueZ's default)"
    grep -E '^[[:space:]]*[^#[:space:]]' $f 2>/dev/null
  done
  o=/storage/.kodi/userdata/addon_data/service.libreelec.settings/oe_settings.xml
  echo "--- the standby of the settings add-on (it disconnects these devices with the screensaver, and after idle_timeout minutes without input)"
  echo "idle_timeout: $(tr -d ' \t\r\n' 2>/dev/null < $o | sed -n 's|.*<idle_timeout>\([^<]*\)</idle_timeout>.*|\1|p')"
  for d in $(grep -o 'dev_[0-9A-F]\{2\}\(_[0-9A-F]\{2\}\)\{5\}' $o 2>/dev/null | sort -u); do echo "standby: $(echo ${d#dev_} | tr _ :)"; done
  echo "== paired devices (a device that is known and not paired is only counted)"
  np=0
  for p in $(busctl --timeout=5 --list tree org.bluez 2>/dev/null | grep -E '^/org/bluez/hci[0-9]+/dev_[0-9A-F]{2}(_[0-9A-F]{2}){5}$'); do
    [ "$(bt_get $p Paired)" = "b true" ] || { np=$((np + 1)); continue; }
    a=$(echo ${p##*/dev_} | tr _ :)
    echo "--- $a (${p%/*})"
    for k in AddressType Bonded Trusted Blocked Connected WakeAllowed LegacyPairing Icon Appearance Class Modalias; do
      v=$(bt_get $p $k) && echo "    $k: ${v#* }"
    done
    for f in /storage/.cache/bluetooth/*/$a/info; do
      [ -f "$f" ] || continue
      echo "    stored in $f:"
      # (the name and the alias as markers: never the name itself, not even here in RAM)
      sh $R btname "$f" 2>/dev/null | sed 's/^/        /'
      awk '
        /^\[/ { s = $0; print "      " s; next }
        { k = $0; sub(/=.*/, "", k); v = substr($0, length(k) + 2) }
        s == "[General]" && k ~ /^(AddressType|SupportedTechnologies|Trusted|Blocked|WakeAllowed|CablePairing|PreferredBearer|Appearance|Class)$/ { print "        " $0 }
        s == "[General]" && k == "Services" { print "        HID over GATT (LE): " (v ~ /00001812-/ ? "yes" : "no") ", HID (BR/EDR): " (v ~ /00001124-/ ? "yes" : "no") }
        s == "[ConnectionParameters]" && (k == "MinInterval" || k == "MaxInterval") { printf "        %s=%s (%.2f ms)\n", k, v, v * 1.25 }
        s == "[ConnectionParameters]" && k == "Latency" { print "        " $0 " (connection events that the device may skip)" }
        s == "[ConnectionParameters]" && k == "Timeout" { printf "        %s=%s (%d ms: the link is given up after that long without a packet)\n", k, v, v * 10 }
        s ~ /^\[(LongTermKey|PeripheralLongTermKey|SlaveLongTermKey)\]$/ && (k == "Authenticated" || k == "EncSize") { print "        " $0 }
        s ~ /^\[(LongTermKey|PeripheralLongTermKey|SlaveLongTermKey)\]$/ && (k == "EDiv" || k == "Rand") { print "        " k (v + 0 == 0 ? " is 0 (LE Secure Connections)" : " is not 0 (legacy pairing)") }
        s == "[LinkKey]" && (k == "Type" || k == "PINLength") { print "        " $0 }
        s == "[DeviceID]" && k != "" { print "        " $0 }
      ' "$f"
    done
  done
  echo "($np known device(s) that are not paired)"
  echo "== bluetoothd (journal)"
  lim journalctl -b -u bluetooth --no-pager | tail -60 | clean
}
# The kernel's display messages go back to what they were before this turned them up (whoever gets here
# first does it: the second snapshot, listen() at its end, or the next start of this after one that was
# killed).
debug_down() { [ -f $W ] && { cat $W > $G; rm -f $W; }; }
# Hades: the memory of an AMD card ($1, its device in sysfs) as amdgpu tells it, each used and total in MiB: the
# video memory (vram; on an integrated card, the part of the memory that the firmware sets aside for it), the part of
# it that the processor can reach (vis_vram), and the system memory that the card has mapped for itself (gtt). Only
# the ones that are there; nothing where none is (a card of another driver).
amd_memory() {
  local m="" k u t
  for k in vram vis_vram gtt; do
    u=$(cat $1/mem_info_${k}_used 2>/dev/null); t=$(cat $1/mem_info_${k}_total 2>/dev/null)
    case "$u" in ''|*[!0-9]*) continue ;; esac
    case "$t" in ''|*[!0-9]*) continue ;; esac
    m="$m, $k $((u / 1048576)) of $((t / 1048576)) MiB"
  done
  [ -z "$m" ] || echo "    memory used:${m#,}"
}
# Whether each graphics card is awake: " card0=suspended card1=active"
cards_awake() {
  for c in /sys/class/drm/card[0-9]*; do
    case ${c##*/} in *-*) continue ;; esac
    printf ' %s=%s' ${c##*/} "$(cat $c/device/power/runtime_status 2>/dev/null)"
  done
}
# The PCI devices. Listing them reads the configuration of each, which wakes a graphics card that sleeps;
# a card that wakes looks at its sockets. In the first snapshot it is done while the kernel's display
# messages are turned up, so that what the card finds then is told; the second snapshot does not list them
# again.
pci_list() {
  before=$(cards_awake)
  lim lspci -nnk > $S/lspci.txt
  sleep 2
  echo "graphics cards before the PCI devices were listed:${before:- none}; after:$(cards_awake) (listing them wakes a card that sleeps, and a card that wakes looks at its sockets)"
}
# Every HDMI and DisplayPort output that has nothing connected and drives nothing is asked once more, with the
# kernel's display messages turned up for those seconds: what the kernel says meanwhile goes to redetect.txt.
# It is the question that Kodi asks every output when it starts. What the answer is worth depends on the
# driver: i915, xe and nvidia-drm look at the socket again; amdgpu answers for its own sockets from what it
# last saw (it looks at a socket when it starts, when it wakes up and when the hot-plug line of the socket
# changes), which the file says. This is not only reading: asking can make a display appear that the kernel
# had missed (the file then shows it as connected). Left alone: an output that drives something or has a
# display (the file names them); one that the kernel command line names (video=..., which may force it on or
# off: asking would undo that); analog outputs (asking some of them flashes another display); and every
# output once a video has been opened in this session (a driver may hold a frame up for the question). At
# most 15 s are waited for the answers, and the asking ends with the wait: an output that answers later than
# that is the last one asked. The messages of the kernel go back to what they were whatever ends this.
redetect() {
  if video_opened; then echo "a video was opened in this session: not asked"; lim lspci -nnk > $S/lspci.txt; return; fi
  was=$(cat $G 2>/dev/null) || { echo "no $G: not asked"; lim lspci -nnk > $S/lspci.txt; return; }
  named=" $(tr ' ' '\n' < /proc/cmdline | sed -n 's/^video=\([^:]*\):.*/\1/p' | tr '\n' ' ')"
  # (the time of the last line of the kernel log, by the clock of that log)
  t0=$(dmesg | awk -F'[][]' '$2 + 0 > 0 { t = $2 + 0 } END { printf "%.6f\n", t + 0 }')
  echo "kernel display messages (drm.debug) were at $was, and are at 0x6 while the outputs are asked; the kernel log was at $t0 s"
  A=/tmp/hades-redetect.$$
  (
    trap 'echo "$was" > $G' EXIT
    trap 'exit 1' TERM INT HUP
    echo 0x6 > $G
    (
      pci_list
      for c in /sys/class/drm/card*-*; do
        n=${c##*/}; card=${n%%-*}; n=${n#*-}
        case $n in HDMI-*|DP-*) ;; *) continue ;; esac
        s=$(cat $c/status 2>/dev/null); e=$(cat $c/enabled 2>/dev/null)
        [ "$s" = disconnected ] && [ "$e" = disabled ] || { echo "$card-$n: $s, $e: not asked"; continue; }
        case "$named" in *" $n "*) echo "$card-$n: named on the kernel command line: left alone"; continue ;; esac
        drv=$(basename "$(readlink /sys/class/drm/$card/device/driver 2>/dev/null)")
        echo detect > $c/status
        echo "$card-$n ($drv): asked again: $(cat $c/status 2>/dev/null)$([ "$drv" = amdgpu ] && echo " (amdgpu answers for its own sockets from what it last saw: the socket itself was not looked at)")"
      done
    ) > $A 2>&1 &
    rp=$!
    k=0
    while kill -0 $rp 2>/dev/null && [ $k -lt 15 ]; do sleep 1; k=$((k + 1)); done
    # (ended with the wait: left alone it would ask the outputs that follow when its driver lets go of this
    # one, much later, with the messages back down and perhaps under a video; the signal is taken then)
    kill -0 $rp 2>/dev/null && { kill $rp 2>/dev/null; echo "(an output had not answered after 15 s: its driver is still asking it, and the outputs after it are not asked)" > $A.late; }
  )
  cat $A $A.late 2>/dev/null; rm -f $A $A.late
  echo "drm.debug is back at $(cat $G 2>/dev/null)"
  echo "== kernel messages since then"
  dmesg | awk -F'[][]' -v t="$t0" '$2 + 0 > t + 0' > $A.k
  cat $A.k
  # (modetest-connectors.txt of the same snapshot runs meanwhile, on the devices that Kodi is the DRM
  # master of: the kernel tells it that it is not, and answers from what it has)
  grep -q 'is not the DRM master, demoting to read-only probe' $A.k &&
    echo "(the lines \"... is not the DRM master, demoting to read-only probe\" are modetest's, which this snapshot runs meanwhile for modetest-connectors.txt: it is not the DRM master, so the kernel answers it from what it has and asks no display again; the outputs above that were asked again were asked by this, as root, through their status file)"
  rm -f $A.k
}
# Whether a TV or a monitor is seen on any socket
external_seen() {
  for c in /sys/class/drm/card*-*; do
    n=${c##*/}; n=${n#*-}
    case $n in HDMI-*|DP-*|DVI-*|VGA-*) [ "$(cat $c/status 2>/dev/null)" = connected ] && return 0 ;; esac
  done
  return 1
}
# Where no TV or monitor is seen on any socket, the kernel's display messages stay turned up after the
# question, until the second snapshot has taken the kernel log (about three minutes after this; 200 s at
# most): what the kernel says when a cable is pulled out and plugged back in meanwhile is then in that
# snapshot. They go down within two seconds of a video being opened, and whatever ends this. Nothing of it
# where a display is seen on a socket, or once a video was opened.
listen() {
  [ -r $G ] || return 0
  if external_seen; then echo "a TV or monitor is seen on a socket: the kernel's display messages are left as they are"; return 0; fi
  video_opened && return 0
  [ -f $W ] || cat $G > $W 2>/dev/null || return 0
  echo "no TV or monitor is seen on any socket: the kernel's display messages stay at 0x6 until the second snapshot has the kernel log (about three minutes), or until a video is opened"
  (
    trap 'debug_down' EXIT
    trap 'exit 1' TERM INT HUP
    echo 0x6 > $G
    k=0
    while [ -f $W ] && [ $k -lt 100 ]; do
      video_opened && break
      sleep 2; k=$((k + 1))
    done
  )
}

# Hades: how long this start took, and what held it up, into $S/boot-time.txt: the time of the firmware, the loader,
# the kernel and the system until its default target was reached, the chains of units that the default target and Kodi
# waited for, each unit by how long it took to start, and the jobs that systemd still runs (what a start that has not
# finished is waiting for; systemd-analyze then says that it has not finished). Read from what systemd keeps of this
# start: nothing is started or changed. Each query is stopped after 20 s.
boot_time() {
  {
    echo "== systemd-analyze"; lim systemd-analyze
    echo "== systemd-analyze critical-chain"; lim systemd-analyze critical-chain --no-pager
    echo "== systemd-analyze critical-chain kodi.service"; lim systemd-analyze critical-chain kodi.service --no-pager
    echo "== systemd-analyze blame"; lim systemd-analyze blame --no-pager
    echo "== systemctl list-jobs (what the start still waits for)"; lim systemctl list-jobs --no-pager
  } > $S/boot-time.txt 2>&1
}

# Hades: the interrupts of the devices, into $S/interrupts.txt: each interrupt of /proc/interrupts that a device takes,
# with its count summed over the processors (the file has a column for each), the controller and how it is triggered
# (fasteoi or level: a line that stays active until the device is served, the legacy INTx of PCI; edge; MSI), who takes
# it, and "shared" where more than one driver does (IRQF_SHARED); and of each, /proc/irq/N/spurious, which counts the
# interrupts that came and that no driver took. Then each PCI device with a driver: its interrupt, and whether it
# uses MSI or MSI-X (a device without them shares its line with whatever else the board puts on it). A sound card such
# as a Xonar (snd_virtuoso, oxygen) takes a shared legacy interrupt; behind a PCIe-to-PCI bridge, what shares it and
# whether it comes unhandled is what a start that hangs while the drivers load could come from (issue #14): this is
# where a start that worked says it. Only read.
interrupts() {
  local d i m
  awk '
    NR == 1 { n = NF; next }
    {
      irq = $1; sub(/:$/, "", irq)
      if (irq !~ /^[0-9]+$/ && irq != "ERR" && irq != "MIS" && irq != "SPU") next
      total = 0; i = 2
      while (i <= n + 1 && $i ~ /^[0-9]+$/) { total += $i; i++ }
      rest = ""
      for (; i <= NF; i++) rest = rest (rest == "" ? "" : " ") $i
      if (irq ~ /^[0-9]+$/ && rest !~ /[^ ]+ +[^ ]+ +[^ ]/) next
      shared = (gsub(/, /, ", ", rest) > 0) ? " (shared)" : ""
      printf "%s: %d %s%s\n", irq, total, rest, shared
    }' /proc/interrupts 2>/dev/null
  echo "== interrupts that came and that no driver took (/proc/irq/N/spurious: count, unhandled, last unhandled)"
  for i in /proc/irq/[0-9]*; do
    [ -r $i/spurious ] && ls $i 2>/dev/null | grep -q -v -x -E 'spurious|smp_affinity|smp_affinity_list|affinity_hint|effective_affinity|effective_affinity_list|node' || continue
    echo "${i##*/}: $(tr '\n' ' ' < $i/spurious)"
  done
  echo "== PCI devices with a driver: their interrupt, and MSI or MSI-X"
  for d in /sys/bus/pci/devices/*; do
    [ -e $d/driver ] || continue
    m=$(ls $d/msi_irqs 2>/dev/null | wc -l)
    echo "${d##*/} [$(cat $d/vendor 2>/dev/null | sed 's/^0x//'):$(cat $d/device 2>/dev/null | sed 's/^0x//')] class=$(cat $d/class 2>/dev/null) driver=$(basename "$(readlink $d/driver)") irq=$(cat $d/irq 2>/dev/null) $([ $m -gt 0 ] && echo "MSI or MSI-X: $m vectors" || echo "no MSI: a line (INTx)")"
  done
}
# Hades: the clock of the computer (the RTC): the setting "Hardware clock" (Automatic, the default, or local time or UTC
# set by hand, in /storage/.cache/adjtime), how this start reads the clock (the init, hades_clock_mode: /etc/adjtime,
# and /run/hades/rtc-never-written where it reads it as UTC without writing it), the verdict kept by the measurements
# with the time from the internet (/usr/lib/libreelec/clock-measure, /storage/.cache/clock-measured), a verdict that
# changed in this start, and what a measurement gives now: local time, as Windows keeps it; UTC; or neither, its date
# or time set wrong in the firmware. Nothing is written to the clock.
clock_setting() {
  local c=/storage/.cache/adjtime m=/storage/.cache/clock-measured how=auto now
  if [ -e $c ] || [ -L $c ]; then
    how=local
    [ ! -L $c ] && [ -f $c ] && [ "$(dd if=$c bs=256 count=1 2>/dev/null | sed -n 3p)" = UTC ] && how=UTC
  fi
  case $how in
    auto) echo "the setting Hardware clock: Automatic" ;;
    UTC) echo "the setting Hardware clock: UTC, set by hand ($c)" ;;
    *) echo "the setting Hardware clock: local time, set by hand ($c)" ;;
  esac
  if ! sed -n 3p /etc/adjtime 2>/dev/null | grep -q -x UTC; then
    echo "this start reads the clock as local time, and never writes it"
  elif [ -e /run/hades/rtc-never-written ]; then
    echo "this start reads the clock as UTC, as it was measured to keep, and never writes it"
  else
    echo "this start reads the clock as UTC, and sets it to UTC from the internet, as LibreELEC does"
  fi
  if [ -f $m ] && [ ! -L $m ]; then
    echo "the verdict kept ($m): $(dd if=$m bs=256 count=2 2>/dev/null | tr '\n' ' ')"
  else
    echo "the verdict kept ($m): none yet"
  fi
  [ -f /run/hades/clock-changed ] && echo "changed in this start: $(cat /run/hades/clock-changed)"
  now=$(sh /usr/lib/libreelec/clock-measure show 2>&1)
  echo "what the clock of the computer keeps, measured now: ${now#*: }"
  case "$how:${now%%:*}" in
    local:UTC) echo "the clock keeps UTC, and the setting Hardware clock says local time: Automatic, or UTC, reads it right from the start" ;;
    UTC:local) echo "the clock keeps local time, and the setting Hardware clock says UTC: Automatic, or local time, reads it right from the start, and leaves the clock of Windows alone" ;;
  esac
}
# Hades: what the kernel keeps time with (the clock source), and what it said when it changed it, or found one
# unstable (the watchdog of the clock sources compares them every half second)
clock_source() {
  echo "clock source of the kernel: $(cat /sys/devices/system/clocksource/clocksource0/current_clocksource 2>&1) (available: $(cat /sys/devices/system/clocksource/clocksource0/available_clocksource 2>&1))"
  dmesg | grep -E 'clocksource: (Switched to|Marking|Watchdog|Clocksource|Long readout|timekeeping watchdog)|tsc: Marking|TSC found unstable|sched_clock: Marking unstable' | tail -16
}

snapshot() {
  S=$T/$1; rm -rf $S $S.filtered; mkdir -p $S/edid
  echo "$B" > $S/build.txt
  dmesg > $S/dmesg.txt 2>&1
  # (the second snapshot has the kernel log now: see listen())
  [ "$1" = 4-minutes ] && debug_down
  for f in /storage/.kodi/temp/kodi*.log; do [ -f "$f" ] && last 8388608 "$f" | clean | gzip > $S/${f##*/}.gz; done
  {
  echo "== date (UTC) and uptime"; date -u; cat /proc/uptime
  # Hades: at the top, the settings that change the picture of an HDR video from the film (the trims left out,
  # v4.0 added, a peak by hand, another transport...), as Kodi said them when the last videos of each kind were
  # set up (settings/PictureDefaults.h of Kodi); a comparison of the picture with another build begins there
  echo "== picture settings away from the film (kodi.log: the last line of each kind of video; none: no HDR video yet)"
  grep -h 'Picture settings: for this ' /storage/.kodi/temp/kodi.log 2>/dev/null | tail -4 | clean
  echo "== clock: system time vs hardware clock (RTC; never written but where the setting Hardware clock is UTC)"
  s=$(date +%s); r=$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null)
  echo "system UTC $(date -u '+%F %T'), local $(date '+%F %T %Z'); RTC $(cat /sys/class/rtc/rtc0/date /sys/class/rtc/rtc0/time 2>&1 | tr '\n' ' ')"
  echo "system_minus_rtc=$((s - ${r:-0})) s (a clock kept in local time: the time zone, once NTP has synchronized)"
  clock_setting
  echo "this start began at $(date -u -d @$((s - $(cut -d. -f1 /proc/uptime))) '+%F %T' 2>&1) UTC by the clock as it is now ($( { [ -e /run/connman/ntp-synchronized ] || [ -e /run/systemd/timesync/synchronized ]; } && echo 'set from the network' || echo 'not yet set from the network'); start.txt has it as the clock was then)"
  echo "adjtime: $(tr '\n' ' ' < /etc/adjtime 2>&1)| timezone: $(cat /storage/.cache/timezone 2>&1)"
  ls -la /run/connman/ntp-synchronized 2>&1
  grep -h 'RTC .*: not writing it' /storage/.kodi/temp/kodi.log 2>/dev/null | tail -1 | clean
  journalctl -b -t hades-clock -o short-iso --no-pager 2>/dev/null | tail -4
  clock_source
  echo "== os-release"; cat /etc/os-release
  echo "== uname"; uname -a
  echo "== cmdline"; cat /proc/cmdline
  echo "== kernel lockdown and module signatures (Run and Live: lockdown=integrity; internal: none, or integrity from the init while internal disks are kept away)"
  echo "lockdown: $(cat /sys/kernel/security/lockdown 2>&1)"
  echo "tainted: $(cat /proc/sys/kernel/tainted 2>&1)"
  for m in nvidia nvidia_modeset nvidia_drm nvidia_uvm snd_hda_intel; do
    [ -d /sys/module/$m ] && echo "$m: taint=$(cat /sys/module/$m/taint 2>/dev/null) signer=$(modinfo -F signer $m 2>&1 | head -1) sig_hashalgo=$(modinfo -F sig_hashalgo $m 2>&1 | head -1) sig_key=$(modinfo -F sig_key $m 2>&1 | head -1)"
  done
  dmesg | grep -iE "locked down|lockdown|Loading of|module verification|Key was rejected|kernel-overlays-setup" | head -40
  lim journalctl -b --no-pager | grep -iE "Key was rejected|Failed to insert|Operation not permitted" | head -20 | clean
  # Hades: what the IOMMU does with the memory accesses of the devices (DMA): the line that the init writes on it
  # (hades-iommu:, where the init writes one), what the kernel said of its default domain and of the IOMMU of AMD
  # (AMD-Vi) or of Intel (DMAR), how many groups of devices are of each type now (DMA-FQ and DMA: translated;
  # identity: passed through), the devices passed through, and the transfers that the IOMMU stopped: one line per
  # transfer (AMD-Vi's IO_PAGE_FAULT, DMAR's [DMA Read] or [DMA Write]; DMAR's "handling fault" line is the status of
  # its fault registers, not a transfer), the first 10 shown. A device that works leaves none.
  echo "== DMA translation (IOMMU)"
  dmesg | grep -E "hades-iommu:|iommu: Default domain type|DMA domain TLB invalidation|AMD-Vi: (Interrupt remapping|Extended features)|DMAR: (IOMMU enabled|Disable GFX|Intel-IOMMU force)|DMAR-IR: Enabled" | head -12
  for io_t in DMA-FQ DMA identity blocked unmanaged; do
    io_n=$(cat /sys/kernel/iommu_groups/*/type 2>/dev/null | grep -c -x -- "$io_t")
    [ "$io_n" -gt 0 ] && echo "groups $io_t: $io_n"
  done
  for io_g in /sys/kernel/iommu_groups/*; do
    [ "$(cat $io_g/type 2>/dev/null)" = identity ] || continue
    for io_d in $io_g/devices/*; do
      [ -e "$io_d" ] && echo "passthrough: ${io_d##*/} $(cat $io_d/vendor 2>/dev/null):$(cat $io_d/device 2>/dev/null) class=$(cat $io_d/class 2>/dev/null)"
    done
  done | head -12
  echo "transfers stopped by the IOMMU: $(dmesg | grep -c -E 'IO_PAGE_FAULT|DMAR: \[DMA (Read|Write)')"
  dmesg | grep -E 'IO_PAGE_FAULT|DMAR: \[DMA (Read|Write)|DMAR: DRHD: handling fault|AMD-Vi: Event logged' | head -10
  echo "== PCI storage controllers (class 01xx) and their drivers (Run and Live from a USB drive: no nvme or ahci driver)"
  for d in /sys/bus/pci/devices/*; do
    case "$(cat $d/class 2>/dev/null)" in
      0x01*) echo "${d##*/} class=$(cat $d/class) vendor=$(cat $d/vendor):$(cat $d/device) driver=$(readlink $d/driver 2>/dev/null | sed 's#.*/##')" ;;
    esac
  done
  ls /sys/bus/pci/drivers | grep -E '^(nvme|ahci)$' || echo "no nvme or ahci PCI driver registered"
  echo "== block devices (Run and Live from a USB drive: the internal disks must be absent)"; ls -1 /sys/block | grep -vE '^(loop|ram|zram|nbd)'
  echo "== mounts"; grep -E ' /storage | /flash | /var/media/' /proc/mounts
  echo "== storage statistics since boot (/sys/block/<dev>/stat: reads rmerged rsectors read_ms writes wmerged wsectors write_ms in_flight busy_ms weighted_ms discards dmerged dsectors discard_ms flushes flush_ms)"
  for b in /sys/block/sd* /sys/block/nvme* /sys/block/mmcblk*; do
    [ -e "$b/stat" ] && echo "${b##*/} [$(cat $b/device/vendor $b/device/model 2>/dev/null | tr -s ' \n' ' ')] scheduler=$(cat $b/queue/scheduler 2>/dev/null) write_cache=$(cat $b/queue/write_cache 2>/dev/null):$(cat $b/stat)"
  done
  echo "== system image (toram copies SYSTEM to /dev/SYSTEM)"; grep -E " / |squashfs" /proc/mounts; ls -la /dev/SYSTEM 2>&1; losetup -a 2>&1 | head -5; free -m | head -2
  echo "== machine (as its firmware names it; no serial number is read)"
  for f in sys_vendor product_name board_name bios_version bios_date; do printf '%s: %s\n' $f "$(cat /sys/class/dmi/id/$f 2>/dev/null)"; done
  # Hades: where the power comes from (a laptop on its charger or its battery); the type, status and charge of each
  # supply, never its model or serial number
  echo "== power supplies: type, status (a battery) or online (mains, USB), charge"
  for p in /sys/class/power_supply/*; do
    [ -r $p/type ] || continue
    echo "${p##*/}: type=$(cat $p/type 2>/dev/null) status=$(cat $p/status 2>/dev/null) online=$(cat $p/online 2>/dev/null) capacity=$(cat $p/capacity 2>/dev/null)"
  done
  echo "== gpus: drm card, pci address, driver, boot_vga, and whether it is awake (a card that sleeps sees no hot-plug)"
  for c in /sys/class/drm/card*; do
    case ${c##*/} in *-*) continue ;; esac
    dev=$(readlink -f $c/device)
    echo "${c##*/}: ${dev##*/} driver=$(basename "$(readlink $c/device/driver)") boot_vga=$(cat $c/device/boot_vga 2>/dev/null) runtime_status=$(cat $c/device/power/runtime_status 2>/dev/null) power_state=$(cat $c/device/power_state 2>/dev/null) control=$(cat $c/device/power/control 2>/dev/null)"
    amd_memory $c/device
  done
  ls -l /dev/dri /dev/dri/by-path 2>&1
  echo "== framebuffers"; cat /proc/fb
  echo "== nvidia"; cat /proc/driver/nvidia/version 2>&1
  # Hades: which of the two NVIDIA drivers the boot chose (kernel-overlays-setup, stack-select)
  echo "driver chosen at boot: $(cat /run/nvidia/driver 2>&1)"
  echo "nvidia_drm modeset=$(cat /sys/module/nvidia_drm/parameters/modeset 2>&1) fbdev=$(cat /sys/module/nvidia_drm/parameters/fbdev 2>&1)"
  echo "amdgpu dvbridge=$(cat /sys/module/amdgpu/parameters/dvbridge 2>&1)"
  # Hades: sleep (issue 32): the kinds the kernel can use on this firmware (the one in brackets is used; "deep" only where
  # the firmware offers S3), how the last ones went, what woke the PC, what may wake it, and what the NVIDIA driver keeps
  echo "== sleep: the kinds the kernel can use ([the one used]), how the last ones went, what may wake the PC"
  echo "mem_sleep: $(cat /sys/power/mem_sleep 2>&1); state: $(cat /sys/power/state 2>&1); pm_wakeup_irq: $(cat /sys/power/pm_wakeup_irq 2>/dev/null)"
  for f in success fail failed_freeze failed_prepare failed_suspend failed_suspend_late failed_suspend_noirq failed_resume \
           failed_resume_early failed_resume_noirq last_failed_dev last_failed_errno last_failed_step last_hw_sleep \
           total_hw_sleep; do
    [ -r /sys/power/suspend_stats/$f ] && printf '%s=%s ' $f "$(cat /sys/power/suspend_stats/$f | tr '\n' ' ' | sed 's/ $//')"
  done
  echo
  echo "amd_pmc (s2idle of AMD processors): $([ -d /sys/module/amd_pmc ] && echo loaded || echo 'not loaded')"
  # (the kernel's lines of each sleep of this start, with the lines that sleep.sh leaves before and after each one:
  # what the firmware offers, what may wake the PC, how the sleep went and what woke it)
  dmesg | grep -E 'ACPI: PM: \(supports|ACPI: \(supports|PM: suspend (entry|exit)|PM: (Some devices failed|Device .* failed)|amd_pmc|PM: Triggering wakeup|PM: Wakeup|hades-sleep: ' | tail -60
  # (now: whether the firmware offers deep or a low-power S0 idle, the devices armed to wake the PC and the USB input
  # devices that could wake it and are not armed)
  sh /usr/lib/hades-diag/sleep.sh state 2>&1 | sed 's/^hades-sleep: //'
  echo "ACPI devices that may wake the PC (/proc/acpi/wakeup, enabled ones; through them the devices armed above):"
  grep -w enabled /proc/acpi/wakeup 2>/dev/null | sed 's/^/    /'
  echo "NVIDIA across a sleep: $(grep -E 'PreserveVideoMemoryAllocations|UseKernelSuspendNotifiers|EnableS0ixPowerManagement|TemporaryFilePath' /proc/driver/nvidia/params 2>/dev/null | tr -s ' \n' ' '); /proc/driver/nvidia/suspend $([ -e /proc/driver/nvidia/suspend ] && echo there || echo 'not there'); hook $([ -x /usr/lib/systemd/system-sleep/hades-nvidia ] && echo there || echo 'not there'); $(cat /run/modprobe.d/hades-nvidia-sleep.conf 2>/dev/null || echo 'no option of the legacy driver')"
  for p in /proc/driver/nvidia/gpus/*/power; do [ -r "$p" ] && { echo "  ${p#/proc/driver/nvidia/gpus/}:"; sed 's/^/    /' "$p"; }; done
  echo "kernel display messages: drm.debug=$(cat $G 2>&1)"
  echo "== drm connectors"
  for c in /sys/class/drm/card*-*; do
    n=${c##*/}; echo "$n: status=$(cat $c/status 2>/dev/null) enabled=$(cat $c/enabled 2>/dev/null) dpms=$(cat $c/dpms 2>/dev/null)"
    # sysfs reports size 0 for the EDID attribute, so read it and keep only non-empty copies
    cat $c/edid > $S/edid/$n.bin 2>/dev/null; [ -s $S/edid/$n.bin ] && echo "    edid $(wc -c < $S/edid/$n.bin) bytes" || rm -f $S/edid/$n.bin
    # Hades: whether that EDID comes from the display or through a sound bar, an AV receiver or a switch (its HDMI
    # address), with its Dolby Vision block and sound formats, which such a device changes (edid-path.awk)
    [ -s $S/edid/$n.bin ] && od -An -v -tu1 $S/edid/$n.bin | awk -f /usr/lib/hades-diag/edid-path.awk | sed 's/^/    /'
    head -8 $c/modes 2>/dev/null | sed 's/^/    mode /'
  done
  echo "== usb devices (bus-port: vendor:product, speed, name, whether it may sleep and whether it does) and the driver of each interface (a drive: uas or usb-storage)"
  for d in /sys/bus/usb/devices/*; do
    [ -r $d/idVendor ] || continue
    echo "${d##*/}: $(cat $d/idVendor):$(cat $d/idProduct) speed=$(cat $d/speed 2>/dev/null) [$(cat $d/manufacturer $d/product 2>/dev/null | tr -s ' \n' ' ')] power/control=$(cat $d/power/control 2>/dev/null) runtime_status=$(cat $d/power/runtime_status 2>/dev/null)"
    for i in $d/*:*; do
      [ -e "$i/bInterfaceClass" ] && echo "    ${i##*/}: class=$(cat $i/bInterfaceClass) driver=$(basename "$(readlink $i/driver 2>/dev/null)")"
    done
  done
  # Hades: the USB controllers and the USB4 and Thunderbolt host routers, with their power: a controller or a hub
  # that sleeps, or one that never may (power/control "on"), is what a device that keeps losing its connection or
  # its range, or a drive that is not seen, can come from. And for USB4 and Thunderbolt, who sets the tunnels up:
  # the kernel uses its own connection manager when the firmware gave it control of USB4 (the line "USB4 _OSC:
  # OS controls" with a "+"; tb_acpi_is_native, nhi_select_cm), and otherwise tries the firmware's first. No
  # identifier of a device is read (unique_id, boot_acl).
  echo "== usb controllers (PCI class 0c03: UHCI, OHCI, EHCI, xHCI, USB4) and Thunderbolt host routers: driver and power (power/control: on, never sleeps; auto, may sleep while idle), and their root hubs"
  for d in /sys/bus/pci/devices/*; do
    c=$(cat $d/class 2>/dev/null)
    drv=$(basename "$(readlink $d/driver 2>/dev/null)")
    case "$c" in 0x0c03*) ;; *) [ "$drv" = thunderbolt ] || continue ;; esac
    echo "${d##*/} class=$c vendor=$(cat $d/vendor 2>/dev/null):$(cat $d/device 2>/dev/null) driver=${drv:-none} power/control=$(cat $d/power/control 2>/dev/null) runtime_status=$(cat $d/power/runtime_status 2>/dev/null) d3cold_allowed=$(cat $d/d3cold_allowed 2>/dev/null)"
    for h in $d/usb[0-9]*; do
      [ -r $h/speed ] && echo "    root hub ${h##*/}: speed=$(cat $h/speed) ports=$(cat $h/maxchild 2>/dev/null) power/control=$(cat $h/power/control 2>/dev/null) runtime_status=$(cat $h/power/runtime_status 2>/dev/null)"
    done
  done
  echo "external USB hubs (the SuperSpeed side of each sleeps when nothing is on it, by the udev rule 50-hades-usb3-hub-autosuspend; a USB 2.0 side is left awake):"
  for h in /sys/bus/usb/devices/[0-9]*-[0-9]*; do
    [ -r $h/bDeviceClass ] && [ "$(cat $h/bDeviceClass)" = 09 ] || continue
    echo "    hub ${h##*/}: speed=$(cat $h/speed 2>/dev/null) ports=$(cat $h/maxchild 2>/dev/null) power/control=$(cat $h/power/control 2>/dev/null) runtime_status=$(cat $h/power/runtime_status 2>/dev/null) autosuspend_delay_ms=$(cat $h/power/autosuspend_delay_ms 2>/dev/null)"
  done
  echo "usbcore autosuspend=$(cat /sys/module/usbcore/parameters/autosuspend 2>/dev/null) (seconds; -1: no USB device is made to sleep by itself, as the kernel command line of the build has it; the udev rule 50-hades-usb3-hub-autosuspend sets 2000 ms on the SuperSpeed side of each external hub)"
  echo "== USB4 and Thunderbolt: the agreement with the firmware, who runs the connection manager, and the security level of each domain"
  dmesg | grep -E 'USB4 _OSC|platform _OSC|thunderbolt' | tail -40
  o=$(dmesg | grep 'USB4 _OSC: OS controls' | tail -n 1)
  case "$o" in
    *+*) echo "connection manager: the kernel's own (software): the firmware gave the system control of USB4 (${o##*controls })" ;;
    "") echo "connection manager: no USB4 agreement with the firmware (a computer from before USB4, or one whose firmware does not offer it): the kernel tries the firmware's connection manager first, then its own" ;;
    *) echo "connection manager: the firmware kept control of USB4 (${o##*controls }): the kernel tries the firmware's connection manager first, then its own" ;;
  esac
  echo "thunderbolt driver: $([ -d /sys/module/thunderbolt ] && echo loaded || echo 'not loaded')"
  for t in /sys/bus/thunderbolt/devices/*; do
    [ -e "$t" ] || continue
    case ${t##*/} in
      *:*) continue ;;
      domain*) echo "${t##*/}: security=$(cat $t/security 2>/dev/null) iommu_dma_protection=$(cat $t/iommu_dma_protection 2>/dev/null) deauthorization=$(cat $t/deauthorization 2>/dev/null)" ;;
      [0-9]*-[0-9]*) echo "${t##*/}: [$(cat $t/vendor_name $t/device_name 2>/dev/null | tr -s ' \n' ' ')] generation=$(cat $t/generation 2>/dev/null) authorized=$(cat $t/authorized 2>/dev/null) lanes and speed: rx=$(cat $t/rx_lanes 2>/dev/null)x$(cat $t/rx_speed 2>/dev/null) tx=$(cat $t/tx_lanes 2>/dev/null)x$(cat $t/tx_speed 2>/dev/null)" ;;
    esac
  done
  echo "== kodi"; echo "kodi.bin pid: $(pidof kodi.bin)"
  lim systemctl status kodi --no-pager -l | head -30 | clean
  lim journalctl -b -u kodi --no-pager | tail -80 | clean
  echo "== services"; for u in iptables sshd smbd bluetooth avahi-daemon wait-time-sync kodi-autostart hades-diag kodi; do echo "$u: $(lim systemctl is-active $u)"; done
  lim systemctl --failed --no-pager
  # Hades: what iptables.service is given (each line with "$" at its end, a line end of Windows as "\r"), and
  # whether a firewall is loaded (the policy of INPUT)
  echo "== firewall"
  for f in /storage/.cache/services/iptables.conf /storage/.cache/services/iptables.disabled; do
    [ -e $f ] && { echo "${f##*/}: $(wc -c < $f) bytes"; head -n 5 $f | cut -c1-120 | awk '{ gsub(/\r/, "\\\\r"); gsub(/\t/, "\\\\t"); print $0 "$" }'; }
  done
  echo "INPUT: $(lim iptables -S INPUT | head -n 1) | $(lim ip6tables -S INPUT | head -n 1)"
  # Hades: the network access in effect (Settings > LibreELEC > Services > Network access): Recommended, As in
  # LibreELEC, or own settings, and what it is made of
  echo "== network access"
  lim sh /usr/lib/hades-diag/net-access.sh
  echo "== sockets (the Varlink system installer of systemd 262 must not listen)"
  echo "systemd-sysinstall.socket: enabled=$(lim systemctl is-enabled systemd-sysinstall.socket) active=$(lim systemctl is-active systemd-sysinstall.socket)"
  lim systemctl list-sockets --all --no-pager | grep -iE "sysinstall|listen" | head -20
  echo "== network"; ip addr 2>&1; ip route 2>&1; netstat -tulpn 2>&1
  echo "== kodi: Dolby Vision verdicts and output handling"
  grep -hE "DVBridge|native Dolby Vision|DV FEL|DV: |\[DV\]|Display resolution ADJUST|turning off .* output|turned off .* output|outputs stay on|/dev/dri/card|Using connector|transactional test commit|DRMDisplayWatch|look [0-9]* at the outputs" \
    /storage/.kodi/temp/kodi.log 2>/dev/null | tail -80 | clean
  echo "== output check (System > Check this setup, \"Check the Dolby Vision output\"): the last report, if one was asked for"
  oc=/storage/.cache/hades-output-check/result.txt
  if [ -s $oc ]; then echo "written: $(date -u -r $oc '+%F %T') UTC"; cat $oc; else echo "none"; fi
  # Hades: the last result of "Check this setup" (the settings write summary.txt, in English, at the end of each check;
  # last.json beside it holds the same results in the parts of their texts, for the settings to say them again)
  echo "== check of this setup (System > Submit Log, \"Check this setup\"): the last result, if a check was made"
  hc=/storage/.cache/hades-check/summary.txt
  if [ -s $hc ]; then echo "written: $(date -u -r $hc '+%F %T') UTC"; head -c 65536 $hc; echo; else echo "none"; fi
  # Hades: every answer of the A/B comparison of native Dolby Vision of that check (the last 50), each with the
  # name of the TV from its EDID (no serial number): the data for settings per TV
  echo "== A/B comparisons of native Dolby Vision (Check this setup): the answers kept, with the TV of each"
  ab=/storage/.cache/hades-check/ab-results.json
  if [ -s $ab ]; then head -c 65536 $ab; echo; else echo "none"; fi
  # Hades: the line of "Share my results" of the last check (names of hardware and results only: the settings write it)
  echo "== check of this setup: the line to share (Share my results)"
  sh=/storage/.cache/hades-check/share.txt
  if [ -s $sh ]; then head -c 4096 $sh; else echo "none"; fi
  echo "== kodi: VA-API"
  grep -hiE "vaapi|va-api|libva|nvidia_drv_video" /storage/.kodi/temp/kodi.log 2>/dev/null | head -40 | clean
  echo "== time sync (journal)"; lim journalctl -b --no-pager | grep -E 'RTC configured|ntp:|imeserver' | head -20 | clean
  # Hades: the network at the start, in seconds since the kernel started: the link, connman's states, each step of its
  # DHCP client (with the diagnostics on: hades_diag_dhcp in the init), and what ended the wait for the network
  # (network-wait: the time from the internet, or the network alone where the clock is known to be right)
  echo "== network at the start (journal, seconds since the kernel started)"
  lim journalctl -b -o short-monotonic --no-pager -t kernel -t connmand -t hades-network-wait 2>/dev/null |
    grep -E 'NIC Link is|Link is Up|state is|DHCP index|online check|ntp: adjust|hades-network-wait' | head -60 | clean
  echo "clock for the wait: $(/usr/lib/libreelec/network-wait check 2>&1)"
  echo "== alsa"; aplay -l 2>&1; for e in /proc/asound/card*/eld#*; do [ -r "$e" ] && { echo "--- $e"; cat "$e"; }; done
  # Hades: the digital (HDMI, DisplayPort) pins of each codec with their control, and the state of each playback
  # stream: a pin whose output is off ("Pin-ctls" without OUT) while its stream runs plays nothing (linux-9893)
  echo "== alsa: digital pins and playback streams"
  for c in /proc/asound/card*/codec#*; do
    [ -r "$c" ] || continue
    echo "--- $c: $(sed -n 's/^Codec: //p' "$c" | head -n 1)"
    awk '/^Node /{ node = $0; digital = ($0 ~ /Digital/) } digital && /Pin-ctls:/{ print node; print }' "$c"
  done
  for s in /proc/asound/card*/pcm*p/sub*/status; do [ -r "$s" ] && echo "--- $s: $(head -n 1 "$s")"; done
  echo "== journal warnings and errors"; lim journalctl -b -p warning --no-pager | tail -300 | clean
  } > $S/results.txt 2>&1
  [ -d /sys/class/bluetooth ] && bluetooth > $S/bluetooth.txt 2>&1
  interrupts > $S/interrupts.txt 2>&1
  [ -r /sys/kernel/debug/gpio ] && lim cat /sys/kernel/debug/gpio > $S/gpio.txt
  if [ "$1" = 1-minute ] || [ "$1" = now ]; then
    # The firmware's description of the machine: its ACPI tables DSDT and SSDT, also the ones it loads while
    # it runs (how its sockets, buttons and power are wired and switched; much the same for every machine of
    # a model), and the FADT (FACP: the kinds of sleep and of idle that the firmware declares, such as a
    # low-power S0 idle). Not the other tables: one of them holds the licence key of Windows.
    ( cd /sys/firmware/acpi/tables 2>/dev/null &&
        l=$(ls | grep -E '^(DSDT|SSDT[0-9]*|FACP)$'; ls dynamic 2>/dev/null | grep -E '^SSDT[0-9]*$' | sed 's|^|dynamic/|') &&
        [ -n "$l" ] && tar -cf - $l 2>/dev/null | gzip > $S/acpi-tables.tar.gz )
    [ -s $S/acpi-tables.tar.gz ] || rm -f $S/acpi-tables.tar.gz
  fi
  # Hades: how long the start took (boot-time.txt, boot_time()): in the first snapshot, and in one taken at once (now).
  # Where the start had not finished at the first snapshot, the second one asks again.
  case $1 in
    1-minute)
      boot_time
      grep -q 'Bootup is not yet finished' $S/boot-time.txt && : > $T/boot-unfinished
      ;;
    now) boot_time ;;
    4-minutes) [ -e $T/boot-unfinished ] && { rm -f $T/boot-unfinished; boot_time; } ;;
  esac
  to_flash $1
  # GPU queries: background, time-limited, not waited for; each is copied once it has ended (file_to_flash()),
  # and whatever they wrote after 30 s is copied with the snapshot.
  ( for f in /sys/kernel/debug/dri/*/state; do [ -r "$f" ] && { echo "--- $f"; cat "$f"; }; done > $S/drm-state.txt 2>&1; file_to_flash $1 drm-state.txt ) &
  ( nvidia-smi -q > $S/nvidia-smi.txt 2>&1; file_to_flash $1 nvidia-smi.txt ) &
  ( for r in /dev/dri/renderD*; do echo "--- $r"; vainfo --display drm --device $r 2>&1; done > $S/vainfo.txt; file_to_flash $1 vainfo.txt ) &
  # (each by itself: an output that takes long to answer the one does not keep the other from being written.
  # What listen() says goes to the same file; it then stays for minutes, and writes nothing more.)
  [ "$1" = 1-minute ] && ( redetect > $S/redetect.txt 2>&1; file_to_flash $1 redetect.txt; listen >> $S/redetect.txt 2>&1; file_to_flash $1 redetect.txt ) &
  ( modetest_all > $S/modetest-connectors.txt 2>&1; file_to_flash $1 modetest-connectors.txt ) &
  # (the report made on demand waits for the queries, 30 s at most, and keeps what they wrote: report.sh filters it)
  if [ -n "$REPORT" ]; then settle 30; return; fi
  sleep 30
  to_flash $1
  # (it is on the boot partition now: not kept in RAM as well)
  rm -rf $S $S.filtered
}

# The start before this one, as the journal kept it in the storage (hades_diag_journal in the init, or the
# setting of LibreELEC; the machine ID is kept in the storage, so "-b -1" is that start, and journalctl does not
# take --merge with -b): previous-boot/journal.txt.gz, the last 8 MiB of it, and journal-kernel.txt.gz, the
# kernel's lines of it, through the filter for logs. It tells how a start that hung or lost its power ended,
# which nothing else here can: that start saved nothing. Written once, before the first snapshot. Where the journal
# has no start before this one, previous-boot/NONE.txt says why, as far as this system can tell it
# (previous_boot_none), with the starts that the journal has (journalctl --list-boots: their numbers, identifiers
# and times, through the filter): that a start left nothing is then told apart from a start before it that kept
# nothing. Either way the folder of this start is there from now on, not only from the first snapshot. One line
# in the kernel log says which of the two it was ("hades-diag: previous-boot: ...").
# With initcall_debug (the switch was on in that start), previous-boot/not-returned.txt lists the drivers that the
# journal shows starting and not returned by its last line (not_returned).
previous_boot() {
  local P=$T/previous-boot why= list
  rm -rf $P $P.filtered; mkdir -p $P
  echo "$B" > $P/build.txt
  if [ -e /run/hades-diag-journal ] || [ -f /storage/.cache/journald.conf.d/00_settings.conf ]; then
    if list=$(lim journalctl --list-boots --no-pager 2>&1) && echo "$list" | grep -q '^ *-1 '; then
      lim journalctl -b -1 --no-pager > $T/previous.txt
      last 8388608 $T/previous.txt | clean > $T/previous.clean
      gzip -c $T/previous.clean > $P/journal.txt.gz
      not_returned "the journal of that start" $T/previous.clean $P/not-returned.txt
      rm -f $T/previous.clean
      lim journalctl -b -1 -k --no-pager > $T/previous.txt
      last 8388608 $T/previous.txt | clean | gzip > $P/journal-kernel.txt.gz
      rm -f $T/previous.txt
    elif ! echo "$list" | grep -q '^ *0 '; then
      why="journalctl could not list the starts of the journal (what it said is below)"
    else
      why=$(previous_boot_none kept "$(cat /run/hades-diag-journal 2>/dev/null)")
    fi
  else
    list=$(lim journalctl --list-boots --no-pager 2>&1)
    why=$(previous_boot_none "not kept")
  fi
  if [ -n "$why" ]; then
    {
      echo "No journal of the start before this one is saved here."
      echo
      echo "Why: $why"
      echo
      echo "The journal of a start that hung, or lost its power, is saved in the folder of the start after it (previous-boot"
      echo "there), as long as the diagnostics were on in both. The starts that the journal has now (journalctl --list-boots;"
      echo "0 is this one, -1 the one before it):"
      echo "$list" | clean
    } > $P/NONE.txt
    echo "hades-diag: previous-boot: no journal of the start before this one: $why" > /dev/kmsg 2>/dev/null
  else
    echo "hades-diag: previous-boot: the journal of the start before this one is saved" > /dev/kmsg 2>/dev/null
  fi
  early_log $P
  to_flash previous-boot
  # (the report made on demand keeps it: report.sh takes it)
  [ -n "$REPORT" ] || rm -rf $P $P.filtered
}
# The kernel log of a start that ended before its journal was in the storage, as that start copied it there every second
# (the init: hades_diag_early_log; early-log.sh): previous-boot/early-kernel-log.txt.gz, through the filter for logs (the
# newest, where more than one start left one). Its head says which start it was (its boot ID, as journalctl --list-boots
# in NONE.txt names the starts, both through the same filter) and where it was at its last copy; NONE.txt says it too,
# or EARLY.txt where the journal of the start before was saved. A start that ended before its journal was in the
# storage is not in the journal at all, so "-1" of the journal is then the start before that one: where the boot ID of
# the copy is not among the starts of the journal, EARLY.txt says that the journal saved here is of an earlier start
# (QEMU, 2026-11-18: a start held before the flush of its journal had the journal of the start before it saved here,
# and nothing said that it was another start's). The copies of starts other than this one are deleted then, not by the
# report made on demand. A start whose journal got to the storage deleted its own copy. With initcall_debug,
# previous-boot/early-not-returned.txt lists the drivers that the copy shows starting and not returned (not_returned).
early_log() {
  local d=/storage/.cache/hades-diag/early-log me f n=0 newest= where note id
  [ -d $d ] || return 0
  me=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
  for f in $(ls -t $d/*.txt 2>/dev/null); do
    [ "${f##*/}" = "$me.txt" ] && continue
    n=$((n + 1))
    [ -n "$newest" ] || newest=$f
  done
  if [ -n "$newest" ]; then
    clean < $newest > $T/early.clean
    gzip -c $T/early.clean > $1/early-kernel-log.txt.gz
    not_returned "the kernel log of that start" $T/early.clean $1/early-not-returned.txt
    rm -f $T/early.clean
    where=$(sed -n '3s/^copy [0-9]*, //p' $newest | clean)
    note="The kernel log of a start that ended before its journal was in the storage is saved here (early-kernel-log.txt.gz): that start copied it to the storage every second, and its last copy was made $where."
    [ $n -gt 1 ] && note="$note $n starts left such a copy; this is the newest."
    id=$(sed -n '2s/^start: boot ID \([0-9a-f-]*\) .*/\1/p' $newest | tr -d -)
    if [ -f $1/journal.txt.gz ] && [ -n "$id" ] && ! lim journalctl --list-boots --no-pager 2>/dev/null | grep -q "$id"; then
      note="$note That start left no journal: journal.txt.gz and journal-kernel.txt.gz here are of the start before it, the last one whose journal got to the storage."
    fi
    if [ -f $1/NONE.txt ]; then
      { echo; echo "$note"; } >> $1/NONE.txt
    else
      echo "$note" > $1/EARLY.txt
    fi
    echo "hades-diag: previous-boot: the kernel log of a start that ended before its journal was in the storage is saved (early-kernel-log.txt.gz): its last copy $where" > /dev/kmsg 2>/dev/null
  fi
  [ -n "$REPORT" ] && return 0
  for f in $(ls -A $d 2>/dev/null); do
    case "$f" in "$me.txt"|"$me.txt.new") ;; *) rm -f "$d/$f" ;; esac
  done
}
# The drivers that a log of the start before shows starting and not returned by its last line (not-returned.awk:
# the lines of initcall_debug, "calling" and "initcall ... returned"), which is where a start that hung while drivers
# started was (issue 14). $1: what the log is, for the summary; $2: the log, through the filter already; $3: the file
# written, whose first line, the summary, also goes to the kernel log. Nothing is written where the log has no line of
# initcall_debug (the switch was off in that start).
not_returned() {
  lim awk -v what="$1" -f /usr/lib/hades-diag/not-returned.awk "$2" > "$3"
  if [ -s "$3" ]; then
    echo "hades-diag: previous-boot: $(head -n 1 "$3")" > /dev/kmsg 2>/dev/null
  else
    rm -f "$3"
  fi
}
# Why the journal has no start before this one, in a sentence. $1: "kept", this start keeps its journal in the storage,
# and $2 is what the init wrote to /run/hades-diag-journal (whether the start before this one kept its own: kept, new,
# new-storage; empty with the setting of LibreELEC, which keeps the journal of every start); "not kept", it does not.
previous_boot_none() {
  if [ "$1" = kept ]; then
    case "$2" in
      kept) echo "the start before this one kept its journal in the storage, as this one does, but the journal has no line of it: it ended before its journal was written to the storage (a hang or a loss of power in its first seconds, before systemd-journal-flush), or its lines were removed." ;;
      new) echo "the start before this one kept no journal in the storage: the diagnostics were off then (while they are off, the journal of a start is not kept, and what they kept is deleted), or the init could not keep it." ;;
      new-storage) echo "the diagnostics have not run on this storage before: this is the first start that keeps its journal after a new installation (its very first start only makes the storage as large as its partition, and keeps nothing) or after the storage was made anew." ;;
      *) echo "the journal of the storage has no start before this one." ;;
    esac
  elif dmesg 2>/dev/null | grep -q 'hades-diag: the journal of this start could not be kept'; then
    echo "this start does not keep its journal in the storage: the init could not keep it (its line \"hades-diag:\" in the kernel log)."
  elif grep -qw installer /proc/cmdline 2>/dev/null; then
    echo "this is a start of the installer, whose storage is in RAM: it keeps no journal in the storage."
  elif switch_on; then
    echo "the diagnostics were turned on during this start: the journal of a start is kept in the storage from the start after that on."
  else
    echo "the diagnostics are off: the journal of a start is kept in the storage only while they are on (or while the setting of LibreELEC that keeps the journal is on)."
  fi
}

# Hades: the background jobs of this shell (the queries of the graphics cards of a snapshot), waited for at most $1
# seconds: a query that hangs (a graphics card that does not answer) is left, and what it wrote by then is taken.
# BusyBox's "jobs -p" in a shell that is not interactive goes on listing a job that has ended, so a job counts while
# its process is there and is not a zombie (its state, the field after the name in brackets, which can hold spaces)
settle() {
  local k=0 p r s
  while [ $k -lt $1 ]; do
    jobs -p > $T/.jobs 2>/dev/null
    r=
    for p in $(cat $T/.jobs 2>/dev/null); do
      s=$(cat /proc/$p/stat 2>/dev/null) || continue
      s=${s##*") "}
      case "${s%% *}" in ''|Z|X) ;; *) r=1 ;; esac
    done
    [ -n "$r" ] || break
    sleep 1; k=$((k + 1))
  done
  rm -f $T/.jobs
}

# Hades: the report made on demand (report.sh): the snapshot "now"; the journal of this start (its last 8 MiB,
# journal.txt.gz) and of the start before it where the storage kept it (previous-boot, as previous_boot() takes it);
# the newest crash log of Kodi (its last 8 MiB); and the storage timeline of watch-dv.sh, while it runs (the
# diagnostics are on). Everything through the filter for logs where it is taken, as the snapshots have it; report.sh
# puts every file through the filter again, the one that fits it.
report() {
  local c
  mkdir -p $T
  snapshot now
  lim journalctl -b --no-pager > $T/journal.raw 2>/dev/null
  last 8388608 $T/journal.raw | clean | gzip > $T/journal.txt.gz
  rm -f $T/journal.raw
  previous_boot
  c=$(ls -t /storage/.kodi/temp/kodi_crashlog_*.log 2>/dev/null | head -n 1)
  [ -f "$c" ] && last 8388608 "$c" | clean | gzip > "$T/${c##*/}.gz"
  [ -f /run/hades-io.log ] && cp /run/hades-io.log $T/io-timeline.txt
}

# (the report made on demand: that and nothing else)
if [ -n "$REPORT" ]; then report; exit 0; fi
# (what a run of this that was killed left turned up)
debug_down
refilter
mkdir -p $T; previous_boot
sleep 60; snapshot 1-minute
sleep 150; snapshot 4-minutes
) >/dev/null 2>&1 &
# (the report made on demand is waited for: report.sh filters what it collected once it has ended)
[ "$1" != report ] || wait $!
