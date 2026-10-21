#!/bin/sh
# Hades diagnostics: for up to 4 hours after boot, capture the output state about 15 s after each time Kodi logs
# "DVBridge: native Dolby Vision output started", into during-dv-K/ (K = 1 to 6) of the folder of this start
# (hades-diag/start-N; N is the argument, from start.sh): the HDMI
# connector properties with the Dolby VSIF blob (modetest, only on the devices that Kodi is the master of, so it never
# becomes DRM master), the atomic state (debugfs), the connectors and the end of Kodi's log (redacted). Read-only.
# Also after each "DVBridge: native Dolby Vision output ended", into after-dv-K/ (K = 1 to 6) of that folder,
# 5 s and 30 s later: the kernel log (NVIDIA Xid and display errors), the connectors, the connector
# properties, the atomic state, the GPU state and the end of Kodi's log, each copied to the boot partition at
# once, so that a forced power-off (the display lost after leaving Dolby Vision) does not lose them.
# Also keeps a storage timeline in RAM, copied by shutdown.sh: every second for up to 4 hours, the uptime, the
# writes completed, ms spent writing, requests in flight, ms busy, flushes and ms flushing of the device that holds
# /storage (/sys/block/<dev>/stat), and the Kodi threads in uninterruptible sleep (state D) at that moment with
# the kernel function they wait in (wchan: a vblank wait during Dolby Vision is not storage I/O). Every 10 s it
# also records the GPU performance state, clocks and load (nvidia-smi, when present).
# The same for an AMD GPU (amdgpu: load and current core clock, every 10 s in the timeline), and during
# Dolby Vision a file gpu-amd.txt with five readings a second apart of its load, clocks, temperature, power
# and power policy.
# Every file of a capture goes through the filter of the diagnostics (redact.sh tree: addresses, serial
# numbers, UUIDs, SSH keys, network names and the name of the system become markers) before it is copied,
# and the end of Kodi's log through the filter for logs where it is taken; without the filter nothing is
# copied.
(
dev=$(awk '$2 == "/storage" { print $1 }' /proc/mounts)
part=${dev#/dev/}
if [ -e "/sys/block/$part" ]; then blk=$part; else blk=$(basename "$(readlink -f "/sys/class/block/$part/.." 2>/dev/null)"); fi
st=/sys/block/$blk/stat
L=/run/hades-io.log
[ -r "$st" ] || exit 0
echo "# uptime writes write_ms in_flight busy_ms flushes flush_ms kodi_D_threads(name:wchan) [gpu=pstate,MHz,memMHz,load%,dec%] [amd=load%,coreMHz] (device $blk)" > $L
t=0
while [ $t -lt 14400 ]; do
  p=$(pidof kodi.bin)
  d=""
  if [ -n "$p" ]; then
    # the state follows the last ") " of the stat line (the thread name may contain parentheses)
    for tid in $(awk '{ i = match($0, /\) [A-Za-z] [^)]*$/); if (i && substr($0, i + 2, 1) == "D") print $1 }' /proc/$p/task/*/stat 2>/dev/null); do
      d="$d$(cat /proc/$p/task/$tid/comm 2>/dev/null):$(cat /proc/$p/task/$tid/wchan 2>/dev/null),"
    done
  fi
  g=""
  if [ $((t % 10)) -eq 0 ] && command -v nvidia-smi >/dev/null; then
    g=" gpu=$(nvidia-smi --query-gpu=pstate,clocks.gr,clocks.mem,utilization.gpu,utilization.decoder --format=csv,noheader,nounits 2>/dev/null | tr -d ' ' | head -1)"
  fi
  if [ $((t % 10)) -eq 0 ]; then
    for c in /sys/class/drm/card[0-9]; do
      [ -r $c/device/gpu_busy_percent ] || continue
      g="$g amd=$(cat $c/device/gpu_busy_percent 2>/dev/null)%,$(sed -n 's/^[0-9]*: *\([0-9]*\)Mhz \*.*/\1/p' $c/device/pp_dpm_sclk 2>/dev/null | head -n 1)MHz"
    done
  fi
  echo "$(cut -d' ' -f1 /proc/uptime) $(awk '{ print $5, $8, $9, $10, $16, $17 }' $st) $d$g" >> $L
  sleep 1
  t=$((t + 1))
done
) >/dev/null 2>&1 &
(
D=/flash/hades-diag
# (the folder of this start)
H=start-${1:-0}
K=/storage/.kodi/temp/kodi.log
R=/usr/lib/hades-diag/redact.sh
# Run a command for at most 20 s (the image has no timeout command).
lim() {
  "$@" 2>&1 &
  lp=$!
  ( sleep 20; kill $lp 2>/dev/null ) >/dev/null 2>&1 &
  lw=$!
  wait $lp
  kill $lw 2>/dev/null
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
# One writer of the boot partition at a time (the captures run in the background): a lock directory in RAM,
# waited for at most 60 s.
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
# Lines of a log go through the filter; without it nothing of them is copied.
clean() {
  if [ -f $R ]; then sh $R; else cat >/dev/null; fi
}
# With no room, a note on the partition says so, once: nothing else there would tell why nothing was saved.
no_room() { [ -e $D/NO-ROOM.txt ] || { mkdir -p $D && cp /usr/lib/hades-diag/NO-ROOM.txt $D/; }; }
# Copy the files of $1 into $2 of the folder of this start on the boot partition, each through the filter
# that fits it first, into $1.filtered in RAM (a file that the filter fails on is not copied; without the
# filter, nothing is). The second copy of a capture filters only its new files.
to_flash() {
  switch_on || return 1
  [ -f $R ] || return 1
  sh $R tree $1 $1.filtered
  flash_lock || return 1
  if mount -o remount,rw /flash; then
    if room_left; then
      rm -f $D/NO-ROOM.txt
      mkdir -p $D/$H/$2 && cp $1.filtered/* $D/$H/$2/ && : > $D/$H/.filtered
    else
      no_room
    fi
    mount -o remount,ro /flash
  fi
  flash_unlock
}
output_state() {
  echo "== connectors"
  for c in /sys/class/drm/card*-*; do
    echo "${c##*/}: status=$(cat $c/status 2>/dev/null) enabled=$(cat $c/enabled 2>/dev/null) dpms=$(cat $c/dpms 2>/dev/null)"
  done
}
# The captures after leaving Dolby Vision ($1 = slot): 5 s and 30 s later.
after_dv() {
  S=/tmp/hades-after-dv-$1
  rm -rf $S $S.filtered; mkdir -p $S
  for w in 5 30; do
    if [ $w = 5 ]; then sleep 5; else sleep 25; fi
    {
      echo "== $(date -u '+%F %T') UTC, uptime $(cut -d' ' -f1 /proc/uptime) s, about $w s after the Dolby Vision output ended"
      echo "== last DVBridge lines of kodi.log"; grep -h 'DVBridge' $K | tail -8 | clean
      output_state
      echo "== kodi.bin pid: $(pidof kodi.bin)"
    } > $S/state-${w}s.txt 2>&1
    dmesg 2>&1 | tail -n 400 > $S/dmesg-${w}s.txt
    modetest_all > $S/modetest-connectors-${w}s.txt
    for f in /sys/kernel/debug/dri/*/state; do
      [ -r "$f" ] && { echo "--- $f"; cat "$f"; }
    done > $S/drm-state-${w}s.txt 2>&1
    command -v nvidia-smi >/dev/null && lim nvidia-smi -q > $S/nvidia-smi-${w}s.txt
    [ -f $R ] && tail -n 200 $K | sh $R > $S/kodi-tail-${w}s.txt
    to_flash $S after-dv-$1
  done
  rm -rf $S $S.filtered
}
# The capture about 15 s into Dolby Vision ($1 = slot).
during_dv() {
  sleep 15
  S=/tmp/hades-dv-$1
  rm -rf $S $S.filtered; mkdir -p $S
  {
    echo "== $(date -u '+%F %T') UTC, uptime $(cut -d' ' -f1 /proc/uptime) s"
    echo "== last DVBridge lines of kodi.log"; grep -h 'DVBridge' $K | tail -5 | clean
    output_state
  } > $S/state.txt 2>&1
  modetest_all > $S/modetest-connectors.txt
  for f in /sys/kernel/debug/dri/*/state; do
    [ -r "$f" ] && { echo "--- $f"; cat "$f"; }
  done > $S/drm-state.txt 2>&1
  [ -f $R ] && tail -n 400 $K | sh $R > $S/kodi-tail.txt
  # what an AMD GPU is doing while it draws Dolby Vision
  for c in /sys/class/drm/card[0-9]; do
    [ -r $c/device/gpu_busy_percent ] || continue
    {
      echo "=== ${c##*/} $(grep -m 1 PCI_ID $c/device/uevent 2>/dev/null)"
      echo "power_dpm_force_performance_level: $(cat $c/device/power_dpm_force_performance_level 2>&1)"
      echo "pp_power_profile_mode:"
      head -n 12 $c/device/pp_power_profile_mode 2>&1
      q=0
      while [ $q -lt 5 ]; do
        echo "-- reading $q at uptime $(cut -d' ' -f1 /proc/uptime) s: load $(cat $c/device/gpu_busy_percent 2>&1) %"
        for f in pp_dpm_sclk pp_dpm_fclk pp_dpm_mclk pp_dpm_socclk; do
          [ -r $c/device/$f ] && echo "   $f: $(cat $c/device/$f 2>/dev/null | tr '\n' ' ')"
        done
        for h in $c/device/hwmon/hwmon*; do
          [ -d $h ] && echo "   hwmon: core clock $(cat $h/freq1_input 2>/dev/null) Hz, temperature $(cat $h/temp1_input 2>/dev/null) mC, power $(cat $h/power1_average $h/power1_input 2>/dev/null | head -n 1) uW"
        done
        sleep 1
        q=$((q + 1))
      done
    } >> $S/gpu-amd.txt 2>&1
  done
  to_flash $S during-dv-$1
  rm -rf $S $S.filtered
}
# The first free slot of $1 (during-dv or after-dv) in this start, taken at once with a directory in RAM, so
# that a capture still running (not yet on the boot partition) keeps its slot. Prints nothing when all 6 are
# used.
take_slot() {
  k=1
  while [ $k -le 6 ]; do
    if [ ! -e $D/$H/$1-$k ] && mkdir /run/hades-$1-$k 2>/dev/null; then
      echo $k
      return
    fi
    k=$((k + 1))
  done
}
# What the log has when this starts is of the session before (Kodi starts after this, and with a new log):
# only what comes after that is counted.
seen=$(grep -c 'DVBridge: native Dolby Vision output started' $K 2>/dev/null)
seen=${seen:-0}
ended=$(grep -c 'DVBridge: native Dolby Vision output ended' $K 2>/dev/null)
ended=${ended:-0}
i=0
while [ $i -lt 2880 ]; do
  sleep 5
  i=$((i + 1))
  n=$(grep -c 'DVBridge: native Dolby Vision output started' $K 2>/dev/null)
  n=${n:-0}
  e=$(grep -c 'DVBridge: native Dolby Vision output ended' $K 2>/dev/null)
  e=${e:-0}
  # a new kodi.log after a Kodi restart starts counting again
  [ "$n" -lt "$seen" ] && seen=0
  [ "$e" -lt "$ended" ] && ended=0
  if [ "$e" -gt "$ended" ]; then
    ended=$e
    k=$(take_slot after-dv)
    [ -n "$k" ] && after_dv $k &
  fi
  if [ "$n" -gt "$seen" ]; then
    seen=$n
    k=$(take_slot during-dv)
    [ -n "$k" ] && during_dv $k &
  fi
done
) >/dev/null 2>&1 &
