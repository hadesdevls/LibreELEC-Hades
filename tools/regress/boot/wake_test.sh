#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of what may wake the PC from sleep (packages/sysutils/hades-wake: scripts/wake.sh, which udev, the sleep
# hook and the setting "Wake from sleep" of the LibreELEC settings run) and of what each sleep then says of it
# (packages/sysutils/hades-diag/scripts/sleep.sh). A /sys of files stands for a PC with the USB devices of issue 32 (a
# receiver of a remote with an air mouse that is a boot keyboard, a flirc, a Logitech Unifying receiver with a keyboard
# and a touchpad, an Xbox 360 receiver, a UPS, a Bluetooth adapter, a hub, a sound card with volume keys) and others: a
# Media Center IR receiver (mceusb), a mouse, a game pad with media keys, the receiver of a remote with media keys
# given in the extended form, a TV tuner with an IR receiver, a receiver that offers no remote wakeup, and an IR receiver
# on the board (nuvoton-cir). Each HID interface has a report descriptor of bytes; the busybox of the image runs the
# scripts, as udev and systemd-sleep do.
#
# Usage: wake_test.sh <busybox> <wake.sh> <sleep.sh> <70-hades-wake.rules> <sleep hook>
# Then each of its mutations (a copy of wake.sh or sleep.sh with one rule taken out) must fail at least one check:
# "mutation Mn (...) caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test
# could not run.
set -u
BBI=${1:-}; WS=${2:-}; SS=${3:-}; RULES=${4:-}; HOOK=${5:-}
[ -x "$BBI" ] && [ -f "$WS" ] && [ -f "$SS" ] && [ -f "$RULES" ] && [ -f "$HOOK" ] ||
  { echo "usage: $0 <busybox> <wake.sh> <sleep.sh> <70-hades-wake.rules> <sleep hook>"; exit 2; }
T=$(mktemp -d); trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done

FAILS=0
check() {
  if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi
}
# wk <wake.sh> <args...>: as udev or the settings run it; status in RC, standard output in $T/out, the kernel log in
# $T/kmsg (appended)
wk() {
  local s=$1; shift
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" HADES_WAKE_CHOICE="$T/choice" \
    /usr/bin/timeout 20 "$T/bb/sh" "$s" "$@" > "$T/out" 2> "$T/err"
  RC=$?
}
w() { mkdir -p "$(dirname "$1")"; printf '%s\n' "$2" > "$1"; }
rd() { mkdir -p "$(dirname "$1")"; python3 -I -c 'import sys; open(sys.argv[1], "wb").write(bytes.fromhex(sys.argv[2]))' "$1" "$2"; }
# report descriptors (top-level applications): a boot keyboard, a mouse (with a Pointer in a physical collection), System
# Control, Consumer Control, Consumer Control in the extended form (the page in the usage), a game pad, a vendor's, a UPS
KBD=05010906a101050719e029e715002501750195088102c0
MOUSE=05010902a1010901a100050919012903150025019503750181027505950181010501093009311581257f750895028106c0c0
SYSC=05010980a1018503198129831500250175019503810295058101c0
CONS=050c0901a101850419002a3c021500263c02950175108100c0
CONSX=0b01000c00a101850519002a3c021500263c02950175108100c0
PAD=05010905a10105091901291015002501750195108102c0
VEND=0600ff0901a101150026ff007508954009018102c0
UPS=05840904a1010924a102851f096515002501750195018102c0c0
DEV=$T/sys/devices/pci0000:00
# usb <port> <vid> <pid> <class> <wakeup|-> <product> <parent folder>: a USB device and its link in bus/usb/devices
usb() {
  local d=$7/$1
  mkdir -p "$d" "$T/sys/bus/usb/devices"
  w "$d/idVendor" "$2"; w "$d/idProduct" "$3"; w "$d/bDeviceClass" "$4"; w "$d/product" "$6"
  [ "$5" = - ] || w "$d/power/wakeup" "$5"
  ln -sfn "$d" "$T/sys/bus/usb/devices/$1"
  echo "$d"
}
# intf <device folder> <n> <class> <subclass> <protocol> [<HID descriptor hex>]: an interface, and its HID device
intf() {
  local i=$1/${1##*/}:1.$2
  mkdir -p "$i"
  w "$i/bInterfaceClass" "$3"; w "$i/bInterfaceSubClass" "$4"; w "$i/bInterfaceProtocol" "$5"
  ln -sfn "$i" "$T/sys/bus/usb/devices/${1##*/}:1.$2"
  if [ -n "${6:-}" ]; then
    rd "$i/0003:$(cat "$1/idVendor"):$(cat "$1/idProduct").000$2/report_descriptor" "$6"
    # (the kernel makes an input of what is no vendor's and no UPS)
    case "$6" in "$VEND"|"$UPS") ;; *) mkdir -p "$i/0003:$(cat "$1/idVendor"):$(cat "$1/idProduct").000$2/input/input$2" ;; esac
  fi
  echo "$i"
}
wake_of() { cat "$T/sys/bus/usb/devices/$1/power/wakeup" 2>/dev/null || echo none; }
pc() {
  chmod -R u+w "$T/sys" "$T/run" 2>/dev/null; rm -rf "$T/sys" "$T/run" "$T/kmsg" "$T/out" "$T/err" "$T/choice"
  mkdir -p "$T/run"
  local X=$DEV/0000:00:08.1/0000:07:00.3 E=$DEV/0000:02:00.0 u h1 h2 h3 i p
  u=$(usb usb5 1d6b 0002 09 disabled "xHCI Host Controller" "$X")
  h1=$(usb 5-2 0bda 5411 09 disabled "USB2.1 Hub" "$u"); intf "$h1" 0 09 00 00 > /dev/null
  h2=$(usb 5-2.4 1a86 8091 09 disabled "USB HUB" "$h1")
  h3=$(usb 5-2.4.2 0b95 6804 09 disabled "AX68004" "$h2")
  # the receiver that was armed (a boot keyboard: the kernel armed it), an air mouse
  p=$(usb 5-2.4.2.6 6c38 3901 00 enabled "AX68004" "$h3")
  intf "$p" 0 03 01 01 $KBD > /dev/null; intf "$p" 1 03 00 00 $MOUSE$SYSC$CONS > /dev/null; intf "$p" 2 03 00 00 $MOUSE > /dev/null
  # the flirc: a plain keyboard, not of the boot protocol, and two of the vendor's
  p=$(usb 5-2.4.1 20a0 0006 00 disabled "flirc" "$h2")
  intf "$p" 0 03 00 00 $VEND > /dev/null; intf "$p" 1 03 00 00 $KBD > /dev/null; intf "$p" 2 03 00 00 $VEND > /dev/null
  # Logitech Unifying: a keyboard, a mouse with media keys, and the receiver's own, with the K830 under it
  p=$(usb 5-2.4.2.1 046d c52b 00 disabled "USB Receiver" "$h3")
  intf "$p" 0 03 01 01 $KBD > /dev/null; intf "$p" 1 03 01 02 $MOUSE$CONS$SYSC > /dev/null
  i=$(intf "$p" 2 03 00 00 $VEND); rd "$i/0003:046d:c52b.0002/0003:046D:4032.0009/report_descriptor" $KBD$CONS$MOUSE
  p=$(usb 5-2.3 2357 0604 e0 enabled "Bluetooth USB Adapter" "$h1"); intf "$p" 0 e0 01 01 > /dev/null
  # a sound card with volume keys
  p=$(usb 5-2.4.4 1b3f 2008 00 disabled "USB Audio Device" "$h2")
  intf "$p" 0 01 01 00 > /dev/null; intf "$p" 1 01 02 00 > /dev/null; intf "$p" 3 03 00 00 $CONS > /dev/null
  u=$(usb usb1 1d6b 0002 09 disabled "xHCI Host Controller" "$E")
  p=$(usb 1-8 045e 0719 ff disabled "Xbox 360 Wireless Receiver" "$u")
  intf "$p" 0 ff 5d 81 > /dev/null; intf "$p" 1 ff 5d 82 > /dev/null
  p=$(usb 1-7 051d 0002 00 disabled "Back-UPS" "$u"); intf "$p" 0 03 00 00 $UPS > /dev/null
  u=$(usb usb3 1d6b 0002 09 disabled "xHCI Host Controller" "$DEV/0000:05:00.2")
  # a Media Center IR receiver (mceusb, rc-core)
  p=$(usb 3-1 0471 0815 00 disabled "eHome Infrared Transceiver" "$u")
  i=$(intf "$p" 0 ff ff ff); w "$i/rc/rc0/uevent" "NAME=rc-rc6-mce"$'\n'"DRV_NAME=mceusb"$'\n'"DEV_NAME=eHome"
  # (mceusb arms its interface, which does not count for a wakeup of the PC: it is never touched)
  w "$i/power/wakeup" enabled
  p=$(usb 3-2 046d c077 00 disabled "USB Optical Mouse" "$u"); intf "$p" 0 03 01 02 $MOUSE > /dev/null
  # a game pad with media keys
  p=$(usb 3-3 054c 05c4 00 disabled "Wireless Controller" "$u"); intf "$p" 0 03 00 00 $PAD$CONS > /dev/null
  # the receiver of a remote with a keyboard of its own and media keys in the extended form
  p=$(usb 3-4 1d57 ad02 00 disabled "RF Remote" "$u"); intf "$p" 0 03 00 00 $KBD$CONSX > /dev/null
  # a TV tuner with an IR receiver
  p=$(usb 3-5 2040 8265 00 disabled "DVB-T stick" "$u")
  i=$(intf "$p" 0 ff 00 00); w "$i/rc/rc1/uevent" "DRV_NAME=dvb_usb_dib0700"; mkdir -p "$i/dvb/dvb0.frontend0"
  # the receiver of a remote that offers no remote wakeup
  p=$(usb 3-6 1234 5678 00 - "Plain Remote" "$u"); intf "$p" 0 03 00 00 $CONS > /dev/null
  # an IR receiver on the board, which its driver armed
  mkdir -p "$T/sys/devices/pnp0/00:05/rc/rc2" "$T/sys/class/rc"
  w "$T/sys/devices/pnp0/00:05/power/wakeup" enabled
  w "$T/sys/devices/pnp0/00:05/rc/rc2/uevent" "DRV_NAME=nuvoton-cir"
  ln -s "$T/sys/devices/pnp0/00:05" "$T/sys/devices/pnp0/00:05/rc/rc2/device"
  ln -s "$T/sys/devices/pnp0/00:05/rc/rc2" "$T/sys/class/rc/rc2"
  ln -s "$(readlink -f "$T/sys/bus/usb/devices/3-1:1.0")/rc/rc0" "$T/sys/class/rc/rc0"
  ln -s "$(readlink -f "$T/sys/bus/usb/devices/3-1:1.0")" "$T/sys/class/rc/rc0/device"
}
# the states of the devices, one word each, in this order
ORDER="5-2.4.2.6 5-2.4.1 5-2.4.2.1 5-2.3 5-2.4.4 1-8 1-7 3-1 3-2 3-3 3-4 3-5 3-6 5-2 usb5"
states() {
  local p o=""
  for p in $ORDER; do o="$o $p=$(wake_of "$p")"; done
  echo "${o# } ir=$(cat "$T/sys/devices/pnp0/00:05/power/wakeup") mceif=$(cat "$T/sys/bus/usb/devices/3-1:1.0/power/wakeup")"
}
expect() { # expect <name> <states>
  local s; s=$(states)
  [ "$s" = "$2" ] && check "$1" 0 || { check "$1" 1; echo "       got:  $s"; echo "       want: $2"; }
}
has() { grep -q -F -- "$1" "$2"; }
hasnt() { ! grep -q -F -- "$1" "$2"; }
devpath() { readlink -f "$T/sys/bus/usb/devices/$1" | sed "s|^$T/sys||"; }

AS_LINUX="5-2.4.2.6=enabled 5-2.4.1=disabled 5-2.4.2.1=disabled 5-2.3=enabled 5-2.4.4=disabled 1-8=disabled 1-7=disabled 3-1=disabled 3-2=disabled 3-3=disabled 3-4=disabled 3-5=disabled 3-6=none 5-2=disabled usb5=disabled ir=enabled mceif=enabled"
REMOTES="5-2.4.2.6=enabled 5-2.4.1=enabled 5-2.4.2.1=disabled 5-2.3=enabled 5-2.4.4=disabled 1-8=disabled 1-7=disabled 3-1=enabled 3-2=disabled 3-3=disabled 3-4=enabled 3-5=disabled 3-6=none 5-2=disabled usb5=disabled ir=enabled mceif=enabled"
KEYBOARDS="5-2.4.2.6=enabled 5-2.4.1=enabled 5-2.4.2.1=enabled 5-2.3=enabled 5-2.4.4=disabled 1-8=disabled 1-7=disabled 3-1=enabled 3-2=disabled 3-3=disabled 3-4=enabled 3-5=disabled 3-6=none 5-2=disabled usb5=disabled ir=enabled mceif=enabled"
BUTTON="5-2.4.2.6=disabled 5-2.4.1=disabled 5-2.4.2.1=disabled 5-2.3=enabled 5-2.4.4=disabled 1-8=disabled 1-7=disabled 3-1=disabled 3-2=disabled 3-3=disabled 3-4=disabled 3-5=disabled 3-6=none 5-2=disabled usb5=disabled ir=disabled mceif=enabled"

cases() {
  local S=$1 SL=$2
  echo "1. the default (no file of the choice): the receivers of remotes are armed (flirc, mceusb, media keys); nothing else changes"
  pc; expect "1a the PC as Linux leaves it" "$AS_LINUX"
  wk "$S" apply
  expect "1b remotes" "$REMOTES"
  [ $RC = 0 ] && has "hades-wake: USB 5-2.4.1 20a0:0006 flirc: may wake the PC now (wake from sleep with: remotes; an IR receiver known by its ID (flirc))" "$T/kmsg" &&
    has "USB 3-1 0471:0815 eHome Infrared Transceiver: may wake the PC now (wake from sleep with: remotes; an IR receiver (rc-core, mceusb))" "$T/kmsg" &&
    has "USB 3-4 1d57:ad02 RF Remote: may wake the PC now (wake from sleep with: remotes; a remote's receiver: media or system keys" "$T/kmsg" &&
    has "USB 3-6 1234:5678 Plain Remote: cannot wake a PC: it does not offer remote wakeup in its USB configuration (a remote's receiver" "$T/kmsg"
  check "1c each change said, with why, and a receiver that cannot wake a PC" $?
  grep '^wake from sleep with: remotes; USB input devices that may wake the PC: ' "$T/out" > "$T/line"
  has "USB 5-2.4.2.6 6c38:3901 AX68004 (a keyboard with a mouse or touchpad on one receiver; a boot keyboard, which the kernel arms by itself)" "$T/line" &&
    has "USB 5-2.4.1 20a0:0006 flirc (an IR receiver known by its ID (flirc))" "$T/line" && has "USB 3-1 0471:0815" "$T/line" && has "USB 3-4 1d57:ad02" "$T/line" &&
    has "chosen, but cannot wake a PC (no remote wakeup in their USB configuration): USB 3-6 1234:5678 Plain Remote" "$T/out" &&
    has "USB 3-3 054c:05c4 Wireless Controller (a game controller)" "$T/out" && has "USB 3-5 2040:8265 DVB-T stick (a TV tuner with an IR receiver (dvb_usb_dib0700))" "$T/out" &&
    has "USB 5-2.4.4 1b3f:2008 USB Audio Device (keys of a device that is not only an input device" "$T/out" &&
    has "USB 3-2 046d:c077 USB Optical Mouse (a mouse or another pointer)" "$T/out" &&
    [ "$(tail -n 1 "$T/out")" = "armed 4" ] && hasnt "Back-UPS" "$T/out" && hasnt "Xbox" "$T/out" && hasnt "Bluetooth" "$T/out" && hasnt "Hub" "$T/out"
  check "1d the summary for the settings: what may wake it and why, what may not, what cannot, and how many" $?
  [ ! -e "$T/sys/bus/usb/devices/3-6/power/wakeup" ]; check "1e nothing written to a device that cannot wake a PC" $?
  : > "$T/kmsg"; wk "$S" apply quiet
  [ $RC = 0 ] && [ ! -s "$T/out" ] && [ ! -s "$T/kmsg" ]; check "1f applied again: nothing to change, nothing said" $?

  echo "2. remotes and keyboards: the Logitech receiver too (a keyboard and a touchpad); mice and game pads never"
  w "$T/choice" keyboards; wk "$S" apply
  expect "2a keyboards" "$KEYBOARDS"
  has "USB 5-2.4.2.1 046d:c52b USB Receiver: may wake the PC now (wake from sleep with: remotes and keyboards; a keyboard with a mouse or touchpad on one receiver" "$T/kmsg"
  check "2b said" $?

  echo "3. only the power button: every input device disarmed, the one the kernel armed and the IR receiver of the board too; Bluetooth, the UPS, hubs untouched"
  w "$T/choice" button; wk "$S" apply
  expect "3a button" "$BUTTON"
  has "USB 5-2.4.2.6 6c38:3901 AX68004: may no longer wake the PC (wake from sleep with: only the power button;" "$T/kmsg" &&
    has "IR 00:05: may no longer wake the PC" "$T/kmsg" && [ "$(tail -n 1 "$T/out")" = "armed 0" ]
  check "3b said" $?

  echo "4. back to the default: what this set is put back as it was (the kernel's boot keyboard, the board's IR receiver), the remotes armed"
  rm -f "$T/choice"; wk "$S" apply
  expect "4a remotes again" "$REMOTES"
  has "USB 5-2.4.2.6 6c38:3901 AX68004: power/wakeup back to enabled, as it was before this setting" "$T/kmsg" &&
    has "IR 00:05: power/wakeup back to enabled, as it was before this setting" "$T/kmsg" &&
    hasnt "USB 5-2.4.2.1 046d:c52b USB Receiver: power/wakeup back" "$T/kmsg"; check "4b said (and nothing for one that is as it was)" $?
  [ "$(ls "$T/run/hades-wake/orig/" | sort | tr '\n' ' ')" = "usb-3-1 usb-3-4 usb-5-2.4.1 " ]
  check "4c what is noted is what this armed" $?
  w "$T/choice" "something else"; : > "$T/kmsg"; wk "$S" apply quiet
  expect "4d a file of the choice that says something else: the default" "$REMOTES"

  echo "5. plugged in (udev: a HID interface bound): set at once; a device seen before all its interfaces are, set again by the last"
  pc; wk "$S" device "$(devpath 5-2.4.1:1.1)/0003:20a0:0006.0001"
  [ $RC = 0 ] && [ "$(wake_of 5-2.4.1)" = enabled ] && [ "$(wake_of 5-2.4.2.1)" = disabled ]; check "5a the flirc, alone" $?
  wk "$S" device "$(devpath 3-2:1.0)/0003:046d:c077.0000"; [ "$(wake_of 3-2)" = disabled ]; check "5b a mouse: not armed" $?
  wk "$S" device "$(devpath 3-1:1.0)/rc/rc0"; [ "$(wake_of 3-1)" = enabled ]; check "5c an IR receiver of rc-core (udev: rc)" $?
  # an air mouse that is not a boot keyboard: its keys come first, its mouse after
  local u=$(readlink -f "$T/sys/bus/usb/devices/usb3") p
  p=$(usb 3-7 2222 3333 00 disabled "Air Mouse" "$u"); intf "$p" 0 03 00 00 $KBD$CONS > /dev/null
  wk "$S" device "$(devpath 3-7:1.0)/0003:2222:3333.0000"; a=$(wake_of 3-7)
  intf "$p" 1 03 00 00 $MOUSE > /dev/null
  wk "$S" device "$(devpath 3-7:1.1)/0003:2222:3333.0001"
  [ "$a" = enabled ] && [ "$(wake_of 3-7)" = disabled ] && has "USB 3-7 2222:3333 Air Mouse: power/wakeup back to disabled" "$T/kmsg"
  check "5d keys first (a remote), then a mouse: not armed in the end" $?
  wk "$S" device "/devices/pnp0/00:05/rc/rc2"; [ "$(cat "$T/sys/devices/pnp0/00:05/power/wakeup")" = enabled ]
  check "5e the IR receiver of the board, with the default: kept as its driver armed it" $?
  w "$T/choice" button; wk "$S" device "/devices/pnp0/00:05/rc/rc2"; [ "$(cat "$T/sys/devices/pnp0/00:05/power/wakeup")" = disabled ]
  check "5f the IR receiver of the board, with button: disarmed" $?

  echo "6. after a sleep: a device that came back as a new one (power/wakeup as the kernel sets a new device) is set again"
  pc; wk "$S" apply quiet; w "$T/sys/bus/usb/devices/5-2.4.1/power/wakeup" disabled; : > "$T/kmsg"
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" HADES_WAKE_CHOICE="$T/choice" \
    /usr/bin/timeout 20 "$T/bb/sh" -c "sed 's|/usr/lib/hades-wake/wake.sh|sh $S|' '$HOOK' > '$T/hook'; sh '$T/hook' post suspend" > "$T/out" 2>&1
  [ "$(wake_of 5-2.4.1)" = enabled ] && [ ! -s "$T/out" ] && has "USB 5-2.4.1 20a0:0006 flirc: may wake the PC now" "$T/kmsg"
  check "6a the hook after a sleep" $?
  sed 's|/usr/lib/hades-wake/wake.sh|true|' "$HOOK" > "$T/hook2"; "$T/bb/sh" "$T/hook2" pre suspend; check "6b the hook before a sleep does nothing, exit 0" $?

  echo "7. another device on the port of one this disarmed: not given the other's value"
  pc; w "$T/choice" button; wk "$S" apply quiet
  chmod -R u+w "$T/sys/bus/usb/devices/5-2.4.2.6/"; rm -rf "$(readlink -f "$T/sys/bus/usb/devices/5-2.4.2.6")"
  p=$(usb 5-2.4.2.6 046d c077 00 disabled "USB Optical Mouse" "$(readlink -f "$T/sys/bus/usb/devices/5-2.4.2")")
  intf "$p" 0 03 01 02 $MOUSE > /dev/null
  rm -f "$T/choice"; wk "$S" apply quiet
  [ "$(wake_of 5-2.4.2.6)" = disabled ]; check "7 a mouse where the boot keyboard was: stays disabled" $?

  echo "8. each sleep says the setting and why each device may wake the PC or not (sleep.sh with hades-wake)"
  pc; wk "$S" apply quiet
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_PROC="$T/proc" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" HADES_WAKE="$S" HADES_WAKE_CHOICE="$T/choice" \
    /usr/bin/timeout 20 "$T/bb/sh" "$SL" state > "$T/out" 2> "$T/err"
  grep '^hades-sleep: may wake the PC: ' "$T/out" > "$T/line"
  has "hades-sleep: wake from sleep with: remotes (the setting Wake from sleep, LibreELEC settings > System > Sleep)" "$T/out" &&
    has "USB 5-2.4.2.6 6c38:3901 AX68004 (a keyboard with a mouse or touchpad on one receiver; a boot keyboard, which the kernel arms by itself)" "$T/line" &&
    has "USB 5-2.4.1 20a0:0006 flirc (an IR receiver known by its ID (flirc))" "$T/line" &&
    grep '^hades-sleep: could wake it but are not armed: ' "$T/out" | grep -q -F "USB 5-2.4.2.1 046d:c52b USB Receiver (a keyboard with a mouse or touchpad on one receiver" &&
    has "remotes' receivers that cannot wake a PC (no remote wakeup in their USB configuration): USB 3-6 1234:5678 Plain Remote (a remote's receiver" "$T/out"
  r=$?; check "8 sleep.sh state" $r; [ $r = 0 ] || sed 's/^/       | /' "$T/out" "$T/err"
  wk "$S" reasons
  has "5-2.4.1|enabled|remote|an IR receiver known by its ID (flirc)" "$T/out" && has "00:05|enabled|remote|an IR receiver on the board (rc-core, nuvoton-cir)" "$T/out" &&
    hasnt "5-2|" "$T/out" && hasnt "1-7|" "$T/out" && hasnt "1-8|" "$T/out"
  check "9 reasons: input devices only" $?
}

echo "wake.sh: $WS; sleep.sh: $SS"
echo "0. the rule of udev runs wake.sh on a bound HID device and on an IR receiver, where the package installs it"
grep -q '^ACTION=="add|bind", SUBSYSTEM=="hid", RUN+="/usr/lib/hades-wake/wake.sh device %p"$' "$RULES" &&
  grep -q '^ACTION=="add", SUBSYSTEM=="rc", RUN+="/usr/lib/hades-wake/wake.sh device %p"$' "$RULES" &&
  grep -q '/usr/lib/hades-wake/wake.sh apply quiet' "$HOOK"; check "0 the rule and the hook" $?
cases "$WS" "$SS"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

MUT_MISSED=0
# mutation <n> <what> <w|s> <sed program>: one rule of wake.sh (w) or sleep.sh (s) taken out must fail a check
mutation() {
  local src m=$T/mutant-$1
  [ "$3" = w ] && src=$WS || src=$SS
  sed -e "$4" "$src" > "$m"
  if cmp -s "$m" "$src"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  if [ "$3" = w ]; then cases "$m" "$SS" > "$T/mutant-$1.log" 2>&1; else cases "$WS" "$m" > "$T/mutant-$1.log" 2>&1; fi
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "the flirc not known by its ID" w 's/^    20a0:0006) CLASS=remote;.*$/    20a0:0006) ;;/'
mutation 2 "media and system keys not counted" w 's/^      1:128|12:1) ctl=1 ;;$/      :;;/'
mutation 3 "a mouse not counted" w 's/^      1:1|1:2|13:\*) ptr=1 ;;$/      :;;/'
mutation 4 "a game controller not counted" w 's/^      1:4|1:5|1:8) pad=1 ;;$/      :;;/'
mutation 5 "the page of an extended usage not read" w 's|if (us == 4) print int(uv / 65536) ":" (uv % 65536)|if (0) print 0|'
mutation 6 "a TV tuner taken for an IR receiver" w 's/\[ -e "\${f}" \] \&\& tuner=1;/:;/'
mutation 7 "a device that is not only HID taken for one" w 's/^    \[ "\${c}" = 03 \] || hid=0$/    :/'
mutation 8 "nothing put back" w 's/^    \[ -n "\${rorig}" \] || return 0$/    return 0/'
mutation 9 "button disarms nothing" w 's/^    button:\*) if \[ \${INPUT} = 1 \]; then echo disabled; else echo keep; fi ;;$/    button:*) echo keep ;;/'
mutation 10 "the note of another device on the port taken as this one's" w 's/^    \[ "\${rid}" = "\${ids}" \] || { rm -f "\${key}"; rorig=""; }$/    :/'
mutation 11 "a device that cannot wake a PC written to" w 's/^    enabled|disabled) ;;$/    *) ;;/'
mutation 12 "the board's IR receiver taken for a USB device" w 's/^    usb_of "\${p}" > \/dev\/null \&\& continue$/    :/'
mutation 13 "sleep.sh does not say why" s 's/^  \[ -n "\${R}" \] || return 0$/  return 0/'
mutation 14 "only the first application of a descriptor read" w 's/^            if (depth == 0 \&\& v == 1 \&\& nu) {$/            if (depth == 0 \&\& v == 1 \&\& nu \&\& !seen++) {/'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "WAKE PASSED"; exit 0; }
echo "WAKE FAILED: $TREE_FAILS check(s) failed, $MUT_MISSED mutation(s) not caught"
exit 1
