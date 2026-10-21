#!/bin/sh
# Hades diagnostics: the diagnostics of a start on real hardware, written to the boot partition (FAT,
# readable in Windows) as hades-diag/start-N/1-minute/ and hades-diag/start-N/4-minutes/, about 1 and 4
# minutes after Kodi is started, at every start. Each snapshot is copied to the FAT as soon as it is taken,
# so an early power-off still leaves the first one. Commands that talk to the GPU run in the background and
# are never waited for, so a wedged GPU cannot block the copy. Kodi's logs (the last 8 MiB of each, packed)
# and the journal pass through the filter for logs (redact.sh) where they are taken, and every file of a
# snapshot through the filter that fits it (redact.sh tree: addresses, serial numbers, UUIDs, SSH keys,
# network names and the name of the system become markers) before it is copied; without the filter nothing
# is copied. Nothing is written once the switch is off, or with little room left on the partition. Nothing
# is powered off or reconfigured.
# Arguments: the build (VERSION of os-release), written to build.txt in each snapshot, and the number of
# this start (start.sh).
# All of it only reads what the system is doing, but for what the first snapshot does for a display that is
# plugged in and not seen (redetect.txt; see redetect() and listen() for what each can change):
# - it asks once more the HDMI and DisplayPort outputs that have nothing connected and drive nothing, with
#   the kernel's display messages turned up for those seconds;
# - it lists the PCI devices then (lspci.txt), which wakes a graphics card that sleeps;
# - where no TV or monitor is seen on any socket, the kernel's display messages stay turned up until the
#   second snapshot has the kernel log, or a video is opened.
# It also keeps the firmware's description of the machine (acpi-tables.tar.gz).
(
D=/flash/hades-diag
T=/tmp/hades-diag
R=/usr/lib/hades-diag/redact.sh
B=$1
H=start-${2:-0}
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
  switch_on || return 0
  [ -f $R ] || return 0
  sh $R tree $T/$1 $T/$1.filtered
  flash_lock || return 0
  if mount -o remount,rw /flash; then
    # (without room for a new copy, the one that is there stays)
    if room_left; then
      rm -rf $D/$H/$1 $D/NO-ROOM.txt
      mkdir -p $D/$H/$1 && cp -r $T/$1.filtered/. $D/$H/$1/ && : > $D/$H/.filtered
    else
      no_room
    fi
    mount -o remount,ro /flash
  fi
  flash_unlock
}

# What versions of the diagnostics before 15 left on the partition went there without the filter of
# identifiers: each of their folders (of a start, or of the layout before the folders of starts) is put
# through the filter once, file by file, in RAM, and written back with an empty file .filtered in it, which
# this version puts in every folder of a start that it writes to. A file that the filter fails on is not
# written back. The folder is written beside the old one first, and the old one goes only when it is there.
# (Run in the background before the first snapshot: start.sh is waited for by Kodi.)
refilter() {
  [ -f $R ] || return 0
  for d in $D/*/; do
    d=${d%/}
    [ -d "$d" ] && [ ! -e "$d/.filtered" ] && [ "${d##*/}" != "$H" ] || continue
    case ${d##*/} in *.part) continue ;; esac
    rm -rf $T.refilter
    sh $R tree "$d" $T.refilter
    mkdir -p $T.refilter && : > $T.refilter/.filtered
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
# The kernel's display messages go back to what they were before this turned them up (whoever gets here
# first does it: the second snapshot, listen() at its end, or the next start of this after one that was
# killed).
debug_down() { [ -f $W ] && { cat $W > $G; rm -f $W; }; }
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
  dmesg | awk -F'[][]' -v t="$t0" '$2 + 0 > t + 0'
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

snapshot() {
  S=$T/$1; rm -rf $S $S.filtered; mkdir -p $S/edid
  echo "$B" > $S/build.txt
  dmesg > $S/dmesg.txt 2>&1
  # (the second snapshot has the kernel log now: see listen())
  [ "$1" = 4-minutes ] && debug_down
  for f in /storage/.kodi/temp/kodi*.log; do [ -f "$f" ] && last 8388608 "$f" | clean | gzip > $S/${f##*/}.gz; done
  {
  echo "== date (UTC) and uptime"; date -u; cat /proc/uptime
  echo "== clock: system time vs hardware clock (RTC, read-only; Windows keeps it in local time)"
  s=$(date +%s); r=$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null)
  echo "system UTC $(date -u '+%F %T'), local $(date '+%F %T %Z'); RTC $(cat /sys/class/rtc/rtc0/date /sys/class/rtc/rtc0/time 2>&1 | tr '\n' ' ')"
  echo "system_minus_rtc=$((s - ${r:-0})) s (a clock kept in local time: the time zone, once NTP has synchronized)"
  echo "adjtime: $(tr '\n' ' ' < /etc/adjtime 2>&1)| timezone: $(cat /storage/.cache/timezone 2>&1)"
  ls -la /run/connman/ntp-synchronized 2>&1
  grep -h 'RTC runs in local time' /storage/.kodi/temp/kodi.log 2>/dev/null | tail -1 | clean
  echo "== os-release"; cat /etc/os-release
  echo "== uname"; uname -a
  echo "== cmdline"; cat /proc/cmdline
  echo "== kernel lockdown and module signatures (Run and Live: lockdown=integrity; internal: none)"
  echo "lockdown: $(cat /sys/kernel/security/lockdown 2>&1)"
  echo "tainted: $(cat /proc/sys/kernel/tainted 2>&1)"
  for m in nvidia nvidia_modeset nvidia_drm nvidia_uvm snd_hda_intel; do
    [ -d /sys/module/$m ] && echo "$m: taint=$(cat /sys/module/$m/taint 2>/dev/null) signer=$(modinfo -F signer $m 2>&1 | head -1) sig_hashalgo=$(modinfo -F sig_hashalgo $m 2>&1 | head -1) sig_key=$(modinfo -F sig_key $m 2>&1 | head -1)"
  done
  dmesg | grep -iE "locked down|lockdown|Loading of|module verification|Key was rejected|kernel-overlays-setup" | head -40
  lim journalctl -b --no-pager | grep -iE "Key was rejected|Failed to insert|Operation not permitted" | head -20 | clean
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
  echo "== gpus: drm card, pci address, driver, boot_vga, and whether it is awake (a card that sleeps sees no hot-plug)"
  for c in /sys/class/drm/card*; do
    case ${c##*/} in *-*) continue ;; esac
    dev=$(readlink -f $c/device)
    echo "${c##*/}: ${dev##*/} driver=$(basename "$(readlink $c/device/driver)") boot_vga=$(cat $c/device/boot_vga 2>/dev/null) runtime_status=$(cat $c/device/power/runtime_status 2>/dev/null) power_state=$(cat $c/device/power_state 2>/dev/null) control=$(cat $c/device/power/control 2>/dev/null)"
  done
  ls -l /dev/dri /dev/dri/by-path 2>&1
  echo "== framebuffers"; cat /proc/fb
  echo "== nvidia"; cat /proc/driver/nvidia/version 2>&1
  echo "nvidia_drm modeset=$(cat /sys/module/nvidia_drm/parameters/modeset 2>&1) fbdev=$(cat /sys/module/nvidia_drm/parameters/fbdev 2>&1)"
  echo "amdgpu dvbridge=$(cat /sys/module/amdgpu/parameters/dvbridge 2>&1)"
  echo "kernel display messages: drm.debug=$(cat $G 2>&1)"
  echo "== drm connectors"
  for c in /sys/class/drm/card*-*; do
    n=${c##*/}; echo "$n: status=$(cat $c/status 2>/dev/null) enabled=$(cat $c/enabled 2>/dev/null) dpms=$(cat $c/dpms 2>/dev/null)"
    # sysfs reports size 0 for the EDID attribute, so read it and keep only non-empty copies
    cat $c/edid > $S/edid/$n.bin 2>/dev/null; [ -s $S/edid/$n.bin ] && echo "    edid $(wc -c < $S/edid/$n.bin) bytes" || rm -f $S/edid/$n.bin
    head -8 $c/modes 2>/dev/null | sed 's/^/    mode /'
  done
  echo "== usb devices (bus-port: vendor:product, speed, name) and the driver of each interface (a drive: uas or usb-storage)"
  for d in /sys/bus/usb/devices/*; do
    [ -r $d/idVendor ] || continue
    echo "${d##*/}: $(cat $d/idVendor):$(cat $d/idProduct) speed=$(cat $d/speed 2>/dev/null) [$(cat $d/manufacturer $d/product 2>/dev/null | tr -s ' \n' ' ')]"
    for i in $d/*:*; do
      [ -e "$i/bInterfaceClass" ] && echo "    ${i##*/}: class=$(cat $i/bInterfaceClass) driver=$(basename "$(readlink $i/driver 2>/dev/null)")"
    done
  done
  echo "== kodi"; echo "kodi.bin pid: $(pidof kodi.bin)"
  lim systemctl status kodi --no-pager -l | head -30 | clean
  lim journalctl -b -u kodi --no-pager | tail -80 | clean
  echo "== services"; for u in iptables sshd smbd bluetooth avahi-daemon wait-time-sync kodi-autostart hades-diag kodi; do echo "$u: $(lim systemctl is-active $u)"; done
  lim systemctl --failed --no-pager
  echo "== sockets (the Varlink system installer of systemd 262 must not listen)"
  echo "systemd-sysinstall.socket: enabled=$(lim systemctl is-enabled systemd-sysinstall.socket) active=$(lim systemctl is-active systemd-sysinstall.socket)"
  lim systemctl list-sockets --all --no-pager | grep -iE "sysinstall|listen" | head -20
  echo "== network"; ip addr 2>&1; ip route 2>&1; netstat -tulpn 2>&1
  echo "== kodi: Dolby Vision verdicts and output handling"
  grep -hE "DVBridge|native Dolby Vision|DV FEL|DV: |\[DV\]|Display resolution ADJUST|turning off .* output|turned off .* output|outputs stay on|/dev/dri/card|Using connector|transactional test commit|DRMDisplayWatch|look [0-9]* at the outputs" \
    /storage/.kodi/temp/kodi.log 2>/dev/null | tail -80 | clean
  echo "== kodi: VA-API"
  grep -hiE "vaapi|va-api|libva|nvidia_drv_video" /storage/.kodi/temp/kodi.log 2>/dev/null | head -40 | clean
  echo "== time sync (journal)"; lim journalctl -b --no-pager | grep -E 'RTC configured|ntp:|imeserver' | head -20 | clean
  echo "== alsa"; aplay -l 2>&1; for e in /proc/asound/card*/eld#*; do [ -r "$e" ] && { echo "--- $e"; cat "$e"; }; done
  echo "== journal warnings and errors"; lim journalctl -b -p warning --no-pager | tail -300 | clean
  } > $S/results.txt 2>&1
  [ -r /sys/kernel/debug/gpio ] && lim cat /sys/kernel/debug/gpio > $S/gpio.txt
  if [ "$1" = 1-minute ]; then
    # The firmware's description of the machine: its ACPI tables DSDT and SSDT, also the ones it loads while
    # it runs (how its sockets, buttons and power are wired and switched; much the same for every machine of
    # a model). Not the other tables: one of them holds the licence key of Windows.
    ( cd /sys/firmware/acpi/tables 2>/dev/null &&
        l=$(ls | grep -E '^(DSDT|SSDT[0-9]*)$'; ls dynamic 2>/dev/null | grep -E '^SSDT[0-9]*$' | sed 's|^|dynamic/|') &&
        [ -n "$l" ] && tar -cf - $l 2>/dev/null | gzip > $S/acpi-tables.tar.gz )
    [ -s $S/acpi-tables.tar.gz ] || rm -f $S/acpi-tables.tar.gz
  fi
  to_flash $1
  # GPU queries: background, time-limited, not waited for; whatever they wrote after 30 s is copied.
  ( for f in /sys/kernel/debug/dri/*/state; do [ -r "$f" ] && { echo "--- $f"; cat "$f"; }; done > $S/drm-state.txt 2>&1 ) &
  ( nvidia-smi -q > $S/nvidia-smi.txt 2>&1 ) &
  ( for r in /dev/dri/renderD*; do echo "--- $r"; vainfo --display drm --device $r 2>&1; done > $S/vainfo.txt ) &
  # (each by itself: an output that takes long to answer the one does not keep the other from being written.
  # What listen() says goes to the same file; it then stays for minutes, and writes nothing more.)
  [ "$1" = 1-minute ] && ( redetect > $S/redetect.txt 2>&1; listen >> $S/redetect.txt 2>&1 ) &
  ( modetest_all > $S/modetest-connectors.txt 2>&1 ) &
  sleep 30
  to_flash $1
  # (it is on the boot partition now: not kept in RAM as well)
  rm -rf $S $S.filtered
}

# (what a run of this that was killed left turned up)
debug_down
refilter
sleep 60; snapshot 1-minute
sleep 150; snapshot 4-minutes
) >/dev/null 2>&1 &
