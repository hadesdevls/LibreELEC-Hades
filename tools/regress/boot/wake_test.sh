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
# scripts, as udev and systemd-sleep do. The list of devices (on and off, by IDs, serial number or port) and Bluetooth:
# two adapters (the one of issue 32, armed as Linux left it, and an Intel one that is not) with paired input devices,
# and a stand-in for busctl that answers for BlueZ from a folder per object path (Paired, WakeAllowed, Alias, Icon).
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
    HADES_WAKE_DEVICES="$T/devices" HADES_BUSCTL="sh $T/busctl.sh" HADES_BT_STORAGE="$T/btstore" \
    HADES_WAKE_BT_BEFORE="$T/btbefore" /usr/bin/timeout 20 "$T/bb/sh" "$s" "$@" > "$T/out" 2> "$T/err"
  RC=$?
}
# a stand-in for busctl: what BlueZ answers, from $T/bz (a folder for each object path, a file for each property with
# the text that busctl prints for it); $T/bz.down: BlueZ is not running; a file "readonly" in a device's folder: BlueZ
# refuses to set its WakeAllowed. Each call is noted in $T/bz.log.
cat > "$T/busctl.sh" <<'BUSCTL'
BZ=${0%/*}/bz
echo "$*" >> "$BZ.log"
[ -d "$BZ" ] && [ ! -e "$BZ.down" ] || { echo "Failed to connect to bus" >&2; exit 1; }
while [ $# -gt 0 ]; do case "$1" in --timeout=*|--list) shift ;; *) break ;; esac; done
case "$1" in
  tree) [ "$2" = org.bluez ] || exit 1; (cd "$BZ" && find . -type d | sed 's|^\.||' | grep . | sort) ;;
  get-property) [ "$4" = org.bluez.Device1 ] && [ -f "$BZ$3/$5" ] || exit 1; cat "$BZ$3/$5" ;;
  set-property) [ "$4" = org.bluez.Device1 ] && [ "$5" = WakeAllowed ] && [ "$6" = b ] && [ -f "$BZ$3/$5" ] || exit 1
    [ -e "$BZ$3/readonly" ] && exit 1; echo "b $7" > "$BZ$3/$5" ;;
  *) exit 1 ;;
esac
BUSCTL
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
  chmod -R u+w "$T/sys" "$T/run" 2>/dev/null
  rm -rf "$T/sys" "$T/run" "$T/kmsg" "$T/out" "$T/err" "$T/choice" "$T/devices" "$T/bz" "$T/bz.down" "$T/bz.log"
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
  # a wired Xbox controller: an interface of the vendor, driven by xpad, which makes an input device under it
  p=$(usb 3-10 045e 0b12 ff disabled "Controller" "$u"); i=$(intf "$p" 0 ff 47 d0)
  mkdir -p "$i/input/input7" "$T/sys/bus/usb/drivers/xpad"; ln -s "$T/sys/bus/usb/drivers/xpad" "$i/driver"
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
# Bluetooth: hci0 on the adapter of issue 32 (5-2.3, armed as Linux left it), hci1 on an Intel adapter that is not (3-8);
# paired input devices: a keyboard on hci0; a game controller (Xbox), a mouse, a remote with no icon on hci1; a paired
# headset (no input: no WakeAllowed) and an input device that is not paired
btdev() { # btdev <hci> <address> <icon or -> <alias> <paired> [<WakeAllowed>]
  local d=$T/bz/org/bluez/$1/dev_$(echo "$2" | tr : _)
  mkdir -p "$d"; printf 'b %s\n' "$5" > "$d/Paired"; printf 's "%s"\n' "$4" > "$d/Alias"
  [ "$3" = - ] || printf 's "%s"\n' "$3" > "$d/Icon"
  [ -n "${6:-}" ] && printf 'b %s\n' "$6" > "$d/WakeAllowed"
  return 0
}
bt_pc() {
  local u i p
  mkdir -p "$T/sys/class/bluetooth/hci0"
  ln -sfn "$(readlink -f "$T/sys/bus/usb/devices/5-2.3:1.0")" "$T/sys/class/bluetooth/hci0/device"
  u=$(readlink -f "$T/sys/bus/usb/devices/usb3")
  p=$(usb 3-8 8087 0029 e0 disabled "AX200 Bluetooth" "$u"); i=$(intf "$p" 0 e0 01 01)
  mkdir -p "$T/sys/class/bluetooth/hci1"; ln -sfn "$i" "$T/sys/class/bluetooth/hci1/device"
  mkdir -p "$T/bz/org/bluez/hci0" "$T/bz/org/bluez/hci1"
  btdev hci0 11:22:33:44:55:01 input-keyboard "Living Room Keyboard" true true
  btdev hci1 11:22:33:44:55:02 input-gaming "Xbox Wireless Controller" true true
  btdev hci1 11:22:33:44:55:03 input-mouse "BT Mouse" true true
  btdev hci1 11:22:33:44:55:04 - "TV Remote" true true
  btdev hci1 11:22:33:44:55:05 audio-headset "Headset" true
  btdev hci1 11:22:33:44:55:06 input-gaming "Neighbour Pad" false true
}
wa() { sed 's/^b //' "$T/bz/org/bluez/$1/dev_$(echo "$2" | tr : _)/WakeAllowed" 2>/dev/null || echo none; }
btstates() { echo "kbd=$(wa hci0 11:22:33:44:55:01) pad=$(wa hci1 11:22:33:44:55:02) mouse=$(wa hci1 11:22:33:44:55:03) remote=$(wa hci1 11:22:33:44:55:04) unpaired=$(wa hci1 11:22:33:44:55:06) hci0=$(wake_of 5-2.3) hci1=$(wake_of 3-8)"; }
btexpect() { local s; s=$(btstates); [ "$s" = "$2" ] && check "$1" 0 || { check "$1" 1; echo "       got:  $s"; echo "       want: $2"; }; }

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
    [ "$(tail -n 1 "$T/out")" = "armed 4" ] && hasnt "Back-UPS" "$T/out" && hasnt "Bluetooth" "$T/out" && hasnt "Hub" "$T/out" &&
    grep '^USB input devices that may not: ' "$T/out" | grep -q -F "USB 1-8 045e:0719 Xbox 360 Wireless Receiver (a receiver of Xbox 360 game controllers (xpad), known by its ID)" &&
    grep '^USB input devices that may not: ' "$T/out" | grep -q -F "USB 3-10 045e:0b12 Controller (a game controller (xpad))"
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
    hasnt "5-2|" "$T/out" && hasnt "1-7|" "$T/out" && has "1-8|disabled|gamepad|" "$T/out" && has "3-10|disabled|gamepad|a game controller (xpad)" "$T/out"
  check "9 reasons: input devices only" $?

  echo "10. the list of devices: a mouse allowed, a boot keyboard kept from it; back to the kinds when the lines go; with button, none"
  pc; printf 'on usb 046d:c077\noff usb 6c38:3901\n' > "$T/devices"; wk "$S" apply
  [ "$(wake_of 3-2)" = enabled ] && [ "$(wake_of 5-2.4.2.6)" = disabled ] && [ "$(wake_of 5-2.4.1)" = enabled ] &&
    has "USB 3-2 046d:c077 USB Optical Mouse: may wake the PC now (wake from sleep with: remotes; a mouse or another pointer; allowed in the list of devices)" "$T/kmsg" &&
    has "USB 5-2.4.2.6 6c38:3901 AX68004: may no longer wake the PC (wake from sleep with: remotes; a keyboard with a mouse or touchpad on one receiver; a boot keyboard, which the kernel arms by itself; kept from it in the list of devices)" "$T/kmsg"
  check "10a on and off in the list, said with why" $?
  w "$T/choice" button; wk "$S" apply quiet
  [ "$(wake_of 3-2)" = disabled ]; check "10b only the power button: a device of the list may not either" $?
  rm -f "$T/choice"; echo "on usb 045e:0b12" > "$T/devices"; wk "$S" apply quiet
  [ "$(wake_of 3-10)" = enabled ]; check "10b2 a wired Xbox controller (xpad) in the list: armed" $?
  rm -f "$T/choice" "$T/devices"; wk "$S" apply quiet; expect "10c the lines gone: as the kinds say, the boot keyboard put back" "$REMOTES"
  # two mice with the same IDs: by serial number where they differ, by port where they do not
  local u p
  u=$(readlink -f "$T/sys/bus/usb/devices/usb3")
  p=$(usb 3-9 046d c077 00 disabled "USB Optical Mouse" "$u"); intf "$p" 0 03 01 02 $MOUSE > /dev/null
  wk "$S" list
  has 'usb|046d:c077 port=3-2|disabled|pointer|USB 3-2 046d:c077 USB Optical Mouse|a mouse or another pointer|-' "$T/out" &&
    has 'usb|046d:c077 port=3-9|disabled|pointer|' "$T/out" && has 'usb|20a0:0006|enabled|remote|USB 5-2.4.1 20a0:0006 flirc|' "$T/out" &&
    has 'usb|1234:5678||remote|' "$T/out" && hasnt '|USB 1-7 ' "$T/out" && hasnt 'Hub' "$T/out"
  check "10d list: one line each input device, the key with the port where two have the same IDs and no serial number" $?
  w "$T/sys/bus/usb/devices/3-2/serial" "AB12 x"; w "$T/sys/bus/usb/devices/3-9/serial" "CD34"
  wk "$S" list
  has 'usb|046d:c077 serial=AB12_x|' "$T/out" && has 'usb|046d:c077 serial=CD34|' "$T/out"
  check "10e ... and with the serial number where they have one each" $?
  echo "on usb 046d:c077 serial=CD34" > "$T/devices"; wk "$S" apply quiet
  [ "$(wake_of 3-9)" = enabled ] && [ "$(wake_of 3-2)" = disabled ]; check "10f only the device of that serial number" $?
  echo "on usb 046d:c077 port=3-2" > "$T/devices"; wk "$S" apply quiet
  [ "$(wake_of 3-2)" = enabled ] && [ "$(wake_of 3-9)" = disabled ]; check "10g only the device on that port" $?

  echo "11. Bluetooth: WakeAllowed by kind (BlueZ's own for a keyboard and a remote, off for a mouse and a game controller), the adapters as Linux left them"
  pc; bt_pc; wk "$S" apply
  btexpect "11a the default" "kbd=true pad=false mouse=false remote=true unpaired=true hci0=enabled hci1=disabled"
  has "hades-wake: Bluetooth 11:22:33:44:55:02 Xbox Wireless Controller: may no longer wake the PC (WakeAllowed off; wake from sleep with: remotes; a Bluetooth game controller)" "$T/kmsg" &&
    has "Bluetooth devices that may wake the PC: Bluetooth 11:22:33:44:55:01 Living Room Keyboard (a Bluetooth keyboard or remote)" "$T/out" &&
    grep '^Bluetooth devices that may not' "$T/out" | grep -q -F "Bluetooth 11:22:33:44:55:04 TV Remote (a Bluetooth input device)" &&
    hasnt "Headset" "$T/out" && hasnt "Neighbour" "$T/out" && [ "$(tail -n 1 "$T/out")" = "armed 5" ]
  check "11b said; the remote may not (its adapter is not armed); no headset, nothing unpaired; counted" $?
  echo "on bt 11:22:33:44:55:02" > "$T/devices"; : > "$T/kmsg"; wk "$S" apply
  btexpect "11c the game controller in the list: allowed, and its adapter armed" "kbd=true pad=true mouse=false remote=true unpaired=true hci0=enabled hci1=enabled"
  has "Bluetooth adapter hci1 (USB 3-8 8087:0029 AX200 Bluetooth): may wake the PC now (wake from sleep with: remotes; the adapter of 1 Bluetooth device(s) allowed in the list of devices)" "$T/kmsg" &&
    has "Bluetooth 11:22:33:44:55:02 Xbox Wireless Controller (a Bluetooth game controller; allowed in the list of devices)" "$T/out"
  check "11d said" $?
  rm -f "$T/devices"; wk "$S" apply quiet
  btexpect "11e out of the list again: off, and the adapter put back" "kbd=true pad=false mouse=false remote=true unpaired=true hci0=enabled hci1=disabled"
  printf 'off bt 11:22:33:44:55:01\n' > "$T/devices"; wk "$S" apply quiet
  btexpect "11f the keyboard kept from it in the list" "kbd=false pad=false mouse=false remote=true unpaired=true hci0=enabled hci1=disabled"
  printf 'on bt 11:22:33:44:55:03\n' > "$T/devices"; w "$T/choice" button; wk "$S" apply quiet
  btexpect "11g only the power button: none, whatever the list says; the adapters as Linux left them" "kbd=false pad=false mouse=false remote=false unpaired=true hci0=enabled hci1=disabled"
  rm -f "$T/choice" "$T/devices"; wk "$S" apply quiet
  # a device paired since: BlueZ turns WakeAllowed on by itself; the hook before a sleep sets it
  btdev hci1 11:22:33:44:55:07 input-gaming "New Pad" true true; : > "$T/kmsg"
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" HADES_WAKE_CHOICE="$T/choice" HADES_WAKE_DEVICES="$T/devices" HADES_BUSCTL="sh $T/busctl.sh" \
    HADES_BT_STORAGE="$T/btstore" HADES_WAKE_BT_BEFORE="$T/btbefore" /usr/bin/timeout 20 "$T/bb/sh" -c "sed 's|/usr/lib/hades-wake/wake.sh|sh $S|' '$HOOK' > '$T/hook'; sh '$T/hook' pre suspend" > "$T/out" 2>&1
  [ "$(wa hci1 11:22:33:44:55:07)" = false ] && [ ! -s "$T/out" ] && has "Bluetooth 11:22:33:44:55:07 New Pad: may no longer wake the PC" "$T/kmsg"
  check "11h the hook before a sleep: a game controller paired since is kept from it" $?
  touch "$T/bz/org/bluez/hci1/dev_11_22_33_44_55_07/readonly"; w "$T/bz/org/bluez/hci1/dev_11_22_33_44_55_07/WakeAllowed" "b true"; : > "$T/kmsg"
  wk "$S" apply quiet; has "Bluetooth 11:22:33:44:55:07 New Pad: WakeAllowed could not be set to false" "$T/kmsg"; check "11i BlueZ refuses: said" $?
  touch "$T/bz.down"; : > "$T/kmsg"; wk "$S" apply
  [ $RC = 0 ] && [ "$(tail -n 1 "$T/out")" = "armed 4" ] && hasnt "Bluetooth" "$T/out" && [ "$(wake_of 5-2.4.1)" = enabled ]
  check "11j BlueZ not running: the USB devices as before, nothing of Bluetooth" $?
  rm -f "$T/bz.down" "$T/bz/org/bluez/hci1/dev_11_22_33_44_55_07/readonly"
  echo "on bt 11:22:33:44:55:04" > "$T/devices"; wk "$S" apply quiet; wk "$S" list
  has 'bt|11:22:33:44:55:04|enabled|other|Bluetooth 11:22:33:44:55:04 TV Remote|a Bluetooth input device|on' "$T/out" &&
    has 'bt|11:22:33:44:55:02|disabled|gamepad|Bluetooth 11:22:33:44:55:02 Xbox Wireless Controller|a Bluetooth game controller|-' "$T/out" &&
    hasnt 'Headset' "$T/out" && hasnt 'Neighbour' "$T/out"
  check "11k list: the paired input devices, what the next sleep holds, the choice of the list" $?
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_PROC="$T/proc" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" HADES_WAKE="$S" HADES_WAKE_CHOICE="$T/choice" \
    HADES_WAKE_DEVICES="$T/devices" HADES_BUSCTL="sh $T/busctl.sh" /usr/bin/timeout 20 "$T/bb/sh" "$SL" state > "$T/out" 2> "$T/err"
  grep '^hades-sleep: may wake the PC: ' "$T/out" | grep -q -F "Bluetooth 11:22:33:44:55:04 TV Remote: a Bluetooth input device; allowed in the list of devices" &&
    grep '^hades-sleep: Bluetooth input devices that may not wake it: ' "$T/out" | grep -q -F "Bluetooth 11:22:33:44:55:02 Xbox Wireless Controller: a Bluetooth game controller; WakeAllowed off"
  r=$?; check "11l sleep.sh says which Bluetooth devices may wake the PC and which may not, with why" $r; [ $r = 0 ] || sed 's/^/       | /' "$T/out" "$T/err"

  echo "12. the update: a Bluetooth mouse or game controller paired before it that could wake the PC still may, kept in the list as the user's choice"
  # paired before the update, in BlueZ's storage: on hci0 (armed as Linux left it) a mouse, a game controller and the
  # keyboard; on hci1 (not armed) the Xbox controller
  btprep() {
    local a
    pc; bt_pc; rm -rf "$T/btstore" "$T/btbefore" "$T/devices" "$T/choice"
    btdev hci0 11:22:33:44:55:08 input-mouse "Old Mouse" true true
    btdev hci0 11:22:33:44:55:09 input-gaming "Old Pad" true true
    for a in 11:22:33:44:55:01 11:22:33:44:55:08 11:22:33:44:55:09 11:22:33:44:55:02; do w "$T/btstore/AA:BB:CC:DD:EE:00/$a/info" "[General]"; done
    w "$T/btstore/AA:BB:CC:DD:EE:00/settings" "[General]"; mkdir -p "$T/btstore/AA:BB:CC:DD:EE:00/cache"
  }
  wa0() { wa hci0 "$1"; }
  btprep; : > "$T/kmsg"; wk "$S" adapter /devices/pci0000:00/usb5/5-2/5-2.3/5-2.3:1.0/bluetooth/hci0
  [ "$(head -n 1 "$T/btbefore")" = pending ] && [ "$(grep -c ':' "$T/btbefore")" = 4 ] && [ ! -e "$T/devices" ] &&
    has "hades-wake: Bluetooth: 4 device(s) paired before this version of Hades noted" "$T/kmsg"
  check "12a the first adapter: the devices paired before noted, nothing set" $?
  # paired since the update, before the first sleep: not kept
  btdev hci0 11:22:33:44:55:0A input-gaming "Pad Paired Since" true true
  w "$T/btstore/AA:BB:CC:DD:EE:00/11:22:33:44:55:0A/info" "[General]"
  wk "$S" adapter /devices/pci0000:00/usb3/3-8/3-8:1.0/bluetooth/hci1
  [ "$(grep -c ':' "$T/btbefore")" = 4 ]; check "12b another adapter later: noted once" $?
  : > "$T/kmsg"; wk "$S" bluetooth quiet
  [ "$(sort "$T/devices" | tr '\n' ' ')" = "on bt 11:22:33:44:55:08 on bt 11:22:33:44:55:09 " ] &&
    [ "$(wa0 11:22:33:44:55:08) $(wa0 11:22:33:44:55:09) $(wa0 11:22:33:44:55:0A) $(wa hci1 11:22:33:44:55:02) $(wa0 11:22:33:44:55:01)" = "true true false false true" ] &&
    [ "$(wake_of 5-2.3)" = enabled ] && [ "$(head -n 1 "$T/btbefore")" = done ] &&
    has "Bluetooth 11:22:33:44:55:08 Old Mouse: may still wake the PC, as before this version of Hades (paired before it; a Bluetooth mouse or another pointer; its adapter armed by the firmware or Linux): kept in the list of devices as your choice" "$T/kmsg"
  r=$?; check "12c the first sleep: the mouse and the game controller of the armed adapter kept, as the user's choice; not the one paired since, not the one of the adapter Linux did not arm" $r
  [ $r = 0 ] || { echo "       devices: $(tr '\n' ' ' < "$T/devices" 2>/dev/null)"; sed 's/^/       | /' "$T/kmsg"; }
  wk "$S" list
  has 'bt|11:22:33:44:55:08|enabled|pointer|Bluetooth 11:22:33:44:55:08 Old Mouse|a Bluetooth mouse or another pointer|on' "$T/out"
  check "12d the setting shows it ticked" $?
  echo "off bt 11:22:33:44:55:08" >> "$T/devices"; wk "$S" bluetooth quiet
  [ "$(grep -c '' "$T/devices")" = 3 ] && [ "$(wa0 11:22:33:44:55:08)" = false ]
  check "12e done once: untick it and it stays so" $?
  # with the user's own line before the first sleep, or with "button", nothing kept
  btprep; wk "$S" adapter /devices/x/bluetooth/hci0; echo "off bt 11:22:33:44:55:08" > "$T/devices"; wk "$S" bluetooth quiet
  [ "$(sort "$T/devices" | tr '\n' ' ')" = "off bt 11:22:33:44:55:08 on bt 11:22:33:44:55:09 " ] && [ "$(wa0 11:22:33:44:55:08)" = false ]
  check "12f a line of the user's own is not changed" $?
  btprep; wk "$S" adapter /devices/x/bluetooth/hci0; w "$T/choice" button; wk "$S" bluetooth quiet
  [ ! -e "$T/devices" ] && [ "$(head -n 1 "$T/btbefore")" = done ] && [ "$(wa0 11:22:33:44:55:09)" = false ]
  check "12g only the power button: nothing kept" $?
  # BlueZ not running at the first sleep: kept at the next one
  btprep; wk "$S" adapter /devices/x/bluetooth/hci0; touch "$T/bz.down"; wk "$S" bluetooth quiet
  [ ! -e "$T/devices" ] && [ "$(head -n 1 "$T/btbefore")" = pending ]; r=$?
  rm -f "$T/bz.down"; wk "$S" bluetooth quiet
  [ $r = 0 ] && [ "$(sort "$T/devices" | tr '\n' ' ')" = "on bt 11:22:33:44:55:08 on bt 11:22:33:44:55:09 " ]
  check "12h BlueZ not running at the first sleep: kept at the next" $?
  # the adapter armed by this script for a device of the list: what the firmware or Linux had counts
  btprep; wk "$S" adapter /devices/x/bluetooth/hci0
  mkdir -p "$T/run/hades-wake/orig"; echo "$(cat "$T/sys/bus/usb/devices/5-2.3/idVendor"):$(cat "$T/sys/bus/usb/devices/5-2.3/idProduct") disabled" > "$T/run/hades-wake/orig/bta-5-2.3"
  echo "on bt 11:22:33:44:55:01" > "$T/devices"; wk "$S" bluetooth quiet
  [ "$(grep -c '' "$T/devices")" = 1 ] && [ "$(wa0 11:22:33:44:55:09)" = false ]
  check "12i an adapter that only this script armed: nothing kept" $?
  rm -rf "$T/run/hades-wake/orig/bta-5-2.3"
  # a new installation: nothing paired when the first adapter appears; nothing noted, nothing kept later
  btprep; rm -rf "$T/btstore"; wk "$S" adapter /devices/x/bluetooth/hci0
  [ "$(cat "$T/btbefore")" = done ]; r=$?
  wk "$S" bluetooth quiet
  [ $r = 0 ] && [ ! -e "$T/devices" ] && [ "$(wa0 11:22:33:44:55:08)" = false ]
  check "12j a new installation: nothing kept" $?
  # no note at all (no adapter seen by udev): nothing kept
  btprep; wk "$S" bluetooth quiet
  [ ! -e "$T/devices" ] && [ ! -e "$T/btbefore" ] && [ "$(wa0 11:22:33:44:55:08)" = false ]
  check "12k no note: nothing kept, nothing noted by a sleep" $?
  # a connection's device (hci0:256) is no adapter
  btprep; wk "$S" adapter /devices/x/bluetooth/hci0/hci0:256
  [ ! -e "$T/btbefore" ]; check "12l a connection is no adapter" $?
  rm -rf "$T/btstore" "$T/btbefore" "$T/devices" "$T/choice"
}

echo "wake.sh: $WS; sleep.sh: $SS"
echo "0. the rule of udev runs wake.sh on a bound HID device and on an IR receiver, where the package installs it"
grep -q '^ACTION=="add|bind", SUBSYSTEM=="hid", RUN+="/usr/lib/hades-wake/wake.sh device %p"$' "$RULES" &&
  grep -q '^ACTION=="add", SUBSYSTEM=="rc", RUN+="/usr/lib/hades-wake/wake.sh device %p"$' "$RULES" &&
  grep -q '^ACTION=="add", SUBSYSTEM=="bluetooth", KERNEL=="hci\[0-9\]\*", RUN+="/usr/lib/hades-wake/wake.sh adapter %p"$' "$RULES" &&
  grep -q '^  pre) /usr/lib/hades-wake/wake.sh bluetooth quiet ;;$' "$HOOK" && grep -q '^  post) /usr/lib/hades-wake/wake.sh usb quiet ;;$' "$HOOK"
check "0 the rules and the hook" $?
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
mutation 9 "button disarms nothing" w 's/^    button:\*) if \[ \${INPUT} = 1 \]; then echo disabled; else echo keep; fi; return ;;$/    button:*) echo keep; return ;;/'
mutation 10 "the note of another device on the port taken as this one's" w 's/^    \[ "\${rid}" = "\${ids}" \] || { rm -f "\${key}"; rorig=""; }$/    :/'
mutation 11 "a device that cannot wake a PC written to" w 's/^    enabled|disabled) ;;$/    *) ;;/'
mutation 12 "the board's IR receiver taken for a USB device" w 's/^    usb_of "\${p}" > \/dev\/null \&\& continue$/    :/'
mutation 13 "sleep.sh does not say why" s 's/^  \[ -n "\${R}" \] || return 0$/  return 0/'
mutation 14 "only the first application of a descriptor read" w 's/^            if (depth == 0 \&\& v == 1 \&\& nu) {$/            if (depth == 0 \&\& v == 1 \&\& nu \&\& !seen++) {/'
mutation 15 "the list of devices not read" w 's/^    case "\${a}" in on|off) OV=\${a} ;; esac$/    :/'
mutation 16 "a serial number of the list not compared" w 's/^      serial=\*) \[ -n "\${serial}" \] \&\& \[ "\${q#serial=}" = "\${serial}" \] || continue ;;$/      serial=*) ;;/'
mutation 17 "button lets a device of the list wake the PC" w 's/^    button:\*) if \[/    button:*) [ "${OV}" = on ] \&\& { echo enabled; return; }; if [/'
mutation 18 "a Bluetooth game controller or mouse left as BlueZ allows it" w 's/^    \*) case "\${CLASS}" in gamepad|pointer) echo false ;; \*) echo true ;; esac ;;$/    *) echo true ;;/'
mutation 19 "a Bluetooth adapter armed with no device of the list" w 's/^    if \[ "\${n}" -gt 0 \]; then t=enabled; else t=keep; fi$/    t=enabled/'
mutation 20 "the address of a Bluetooth line not compared" w 's/^    \[ "\${ty}" = bt \] \&\& \[ "\${id}" = "\$1" \] || continue$/    [ "${ty}" = bt ] || continue/'
mutation 21 "button leaves Bluetooth devices allowed" w 's/^    button:\*) echo false ;;$/    button:*) echo true ;;/'
mutation 22 "an input device that is not paired taken" w 's/^    \[ "\$(bt_val "\${p}" Paired)" = true \] || continue$/    :/'
mutation 23 "sleep.sh says nothing of the Bluetooth devices that may not" s 's/^  \[ -n "\${btn}" \] \&\& say .*$/  :/'
mutation 25 "an input device of a vendor's driver (xpad) not seen" w 's/^        \[ -d "\${f}" \] \&\& { a=.*$/        :/'
mutation 24 "two devices with the same IDs never told apart by serial number" w 's,^      if \[ -n "\${s}" \] \&\& .*$,      k="${ids} port=${d##*/}",'
mutation 26 "a device paired since the update kept as one of before" w 's/^      grep -q -x -F "\${ADDR}" "\${BT_BEFORE}" || continue$/      :/'
mutation 27 "a device of an adapter that the firmware did not arm kept" w 's/^      \[ "\$(bt_adapter_before "\${p}")" = enabled \] || continue$/      :/'
mutation 28 "the user's own line overwritten" w 's/^      \[ -z "\${OV}" \] || continue$/      :/'
mutation 29 "kept with only the power button" w 's/^  if \[ "\${CH}" != button \]; then$/  if true; then/'
mutation 30 "done while BlueZ does not answer" w 's/^  \[ -n "\${BTDEV}" \] || return 0$/  :/'
mutation 31 "kept again at every sleep" w "s/^  sed -i '1s\/\^pending\$\/done\/' .*$/  :/"
mutation 32 "the note of this script's own arming not read" w 's/^    \[ "\${rid}" = "\$(one "\${u}\/idVendor"):\$(one "\${u}\/idProduct")" \] \&\& \[ -n "\${rorig}" \] \&\& { echo "\${rorig}"; return; }$/    :/'
mutation 33 "the devices paired at any time noted" w 's/^  \[ -e "\${BT_BEFORE}" \] \&\& return 0$/  rm -f "${BT_BEFORE}"/'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "WAKE PASSED"; exit 0; }
echo "WAKE FAILED: $TREE_FAILS check(s) failed, $MUT_MISSED mutation(s) not caught"
exit 1
