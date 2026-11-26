#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# What may wake the PC from sleep (s2idle or suspend to RAM), by the setting "Wake from sleep" of the LibreELEC
# settings (System > Sleep), kept in /storage/.cache/hades-wake as one word:
#   remotes    (the default, also with no file) the receivers of remote controls may wake the PC, besides what the
#              kernel arms by itself (the keyboards of the boot protocol, hid-core.c of usbhid);
#   keyboards  also every keyboard, and a receiver that a keyboard shares with a mouse or a touchpad;
#   button     only the power button: no keyboard, remote or other input device (Wake-on-LAN is not changed here).
# Mice and game controllers are never armed by this on their own; nor is anything else, which keeps what the kernel,
# LibreELEC or a rule of the user set.
#
# Device by device (System > Sleep > "Devices that may wake it"), in /storage/.cache/hades-wake-devices, one line each:
#   on|off usb <vendor>:<product> [serial=<serial>|port=<port>]   a USB device (the serial number or the port only
#                                                                 where another input device has the same IDs)
#   on|off bt <address>                                           a paired Bluetooth input device
# "on" lets that device wake the PC, a mouse or a game controller too; "off" keeps it from it, a keyboard that the
# kernel arms by itself too. With "button" no input device may, whatever the list says.
#
# Why (issue 32 of LibreELEC Hades): Linux arms no USB device for wakeup but the keyboards of the boot protocol
# (drivers/usb/core/hub.c, usb_new_device: device_init_wakeup(dev, 0); drivers/hid/usbhid/hid-core.c, usbhid_start),
# so the IR receiver a home cinema PC is used with (a flirc, a Media Center receiver: mceusb arms its interface, which
# does not count, drivers/usb/core/driver.c, choose_wakeup) could not wake it, and a PC that does not wake looks hung.
#
# A remote's receiver is told by what it is, not by a list alone: a USB device made only of HID interfaces whose report
# descriptors have a top-level application of media or system keys (Consumer Control, Generic Desktop System Control)
# and none of a mouse, another pointer, a digitizer or a game controller; an IR receiver of rc-core (mceusb, imon,
# streamzap...) on a device that is no TV tuner; and the flirc (20a0:0006), which shows itself as a plain keyboard.
# Only the device's own power/wakeup is set: the kernel then relays its wakeup through the hubs above it (hub.c,
# usb_port_suspend: the suspend feature of each port above an armed device), and the USB controller is armed by the
# kernel (drivers/usb/core/hcd-pci.c, device_wakeup_enable); arming the hubs too would make plugging a device in wake
# the PC. A device without power/wakeup cannot wake a PC (it does not offer remote wakeup in its USB configuration):
# nothing is done to it. IR receivers on the board (nuvoton-cir, fintek-cir, ene_ir, winbond-cir), which the kernel
# arms itself with their wake filters, are left as they are but with "button".
#
# Bluetooth: before a sleep the kernel drops every connection and, where the adapter may wake the PC (its USB
# device's power/wakeup: btusb's hdev->wakeup), lets only the devices flagged for remote wakeup connect again (an event
# filter of the controller for a device of Bluetooth Classic, the accept list of the passive scan for one of Bluetooth
# LE: net/bluetooth/hci_sync.c, hci_suspend_sync). That flag is BlueZ's WakeAllowed of the device, which BlueZ turns on
# by itself for every paired input device (src/device.c, device_set_wake_support). So: WakeAllowed of each paired
# input device as the setting says (BlueZ's own value for a keyboard or a remote, off for a mouse or a game controller,
# what the list says for one in it, off with "button"), and the adapter armed only while at least one device in the
# list may wake the PC; otherwise the adapter is left as the kernel set it. A Bluetooth device then wakes the PC only
# if it connects by itself when a button is pressed, which its firmware decides.
#
# Before this version (up to 1.2.1), Hades did not set Bluetooth at all: where the firmware or Linux armed the adapter,
# every paired input device could wake the PC, mice and game controllers too (BlueZ's WakeAllowed). Nothing that could
# wake the PC stops at the update: the Bluetooth devices paired before it are noted when the first adapter appears
# (udev, "adapter": the folders of BlueZ's storage, /storage/.cache/bluetooth/<adapter>/<device>/info, in
# /storage/.cache/hades-wake-bt-before), and at the first run that sets Bluetooth, each of them that is a mouse or a
# game controller, may wake the PC (WakeAllowed on) through an adapter that the firmware or Linux armed, and has no line
# in the list, gets the line "on bt <address>": the user's choice from then on, which the setting shows ticked and the
# user can untick. Keyboards, remotes and other input devices keep BlueZ's value anyway. With "button" nothing is kept
# (none may wake the PC, as the user chose). A new installation has nothing paired when its first adapter appears, so
# nothing is kept; and it is done once.
#
# What this set is noted in /run/hades-wake/orig (the device's port, its IDs and the value it had), so that another
# choice puts it back as it was; each change is said in the kernel log, on a line that starts with "hades-wake:".
#
# usage: wake.sh device <devpath>   (udev, 70-hades-wake.rules: a HID device that got its driver, or an IR receiver)
#        wake.sh apply [quiet]       (the setting: every device, USB and Bluetooth; the summary on the standard
#                                    output and in the kernel log, unless quiet)
#        wake.sh usb quiet           (after each sleep: the USB devices and the IR receivers on the board)
#        wake.sh bluetooth quiet     (before each sleep: the Bluetooth devices and their adapters)
#        wake.sh adapter <devpath>   (udev: a Bluetooth adapter appeared; the devices paired before this version noted,
#                                    once)
#        wake.sh reasons             (port|power/wakeup|class|why for each device that is an input device; for a
#                                    Bluetooth device "bt <address>|may|class|why", may "enabled" where it may wake
#                                    the PC at the next sleep; nothing set)
#        wake.sh list                (for the setting: kind|key|state|class|name|why|choice of the list, one line each
#                                    input device; nothing set)
#        wake.sh choice              (the choice in force: remotes, keyboards or button)
#        wake.sh words               (the choice in force, in words)
# HADES_SYSFS, HADES_RUN, HADES_KMSG, HADES_WAKE_CHOICE, HADES_WAKE_DEVICES, HADES_BUSCTL, HADES_BT_STORAGE and
# HADES_WAKE_BT_BEFORE stand for /sys, /run, /dev/kmsg, the file of the choice, the file of the list, busctl, BlueZ's
# storage and the note of the devices paired before this version in a test.

SYSFS="${HADES_SYSFS:-/sys}"
RUN="${HADES_RUN:-/run}"
KMSG="${HADES_KMSG:-/dev/kmsg}"
CHOICE_FILE="${HADES_WAKE_CHOICE:-/storage/.cache/hades-wake}"
DEVICES_FILE="${HADES_WAKE_DEVICES:-/storage/.cache/hades-wake-devices}"
BUSCTL="${HADES_BUSCTL:-busctl}"
BT_STORE="${HADES_BT_STORAGE:-/storage/.cache/bluetooth}"
BT_BEFORE="${HADES_WAKE_BT_BEFORE:-/storage/.cache/hades-wake-bt-before}"
STATE="${RUN}/hades-wake"
LOUD=0

say() {
  echo "hades-wake: $*" >> "${KMSG}" 2>/dev/null || echo "hades-wake: $*" >&2
  [ ${LOUD} = 1 ] && echo "$*"
  return 0
}
# the first line of a file, or nothing
one() { head -n 1 "$1" 2>/dev/null; }
# a value that goes into a line of the list: only letters, digits, ".", "_" and "-"
clean() { printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '_'; }
# a name for a line of the list: no "|" and no line end
plain() { printf '%s' "$1" | tr '|\n\r\t' '/   '; }

choice() {
  case "$(one "${CHOICE_FILE}")" in
    keyboards) echo keyboards ;;
    button) echo button ;;
    *) echo remotes ;;
  esac
}
choice_words() {
  case "$1" in
    keyboards) echo "remotes and keyboards" ;;
    button) echo "only the power button" ;;
    *) echo "remotes" ;;
  esac
}

# the top-level applications of a HID report descriptor, one "<usage page>:<usage>" (decimal) a line
hid_apps() {
  od -An -v -tu1 "$1" 2>/dev/null | awk '
    { for (i = 1; i <= NF; i++) b[n++] = $i + 0 }
    END {
      i = 0; depth = 0; page = 0; sp = 0; nu = 0
      while (i < n) {
        p = b[i]
        # a long item: its size is in the byte after it
        if (p == 254) { if (i + 1 >= n) break; i += 3 + b[i + 1]; continue }
        sz = p % 4; if (sz == 3) sz = 4
        type = int(p / 4) % 4; tag = int(p / 16)
        if (i + sz >= n) break
        v = 0; for (k = sz; k >= 1; k--) v = v * 256 + b[i + k]
        i += 1 + sz
        if (type == 1) {
          # global: Usage Page, Push, Pop
          if (tag == 0) page = v
          else if (tag == 10) { stack[sp] = page; sp++ }
          else if (tag == 11 && sp > 0) { sp--; page = stack[sp] }
        } else if (type == 2) {
          # local: the first Usage before a main item (4 bytes: its page in the upper half)
          if (tag == 0 && nu == 0) { uv = v; us = sz; nu = 1 }
        } else if (type == 0) {
          # main: Collection (Application = 1) at the top level, End Collection
          if (tag == 10) {
            if (depth == 0 && v == 1 && nu) {
              if (us == 4) print int(uv / 65536) ":" (uv % 65536)
              else print page ":" uv
            }
            depth++
          } else if (tag == 12 && depth > 0) depth--
          nu = 0
        }
      }
    }'
}

# classify <usb device folder>: CLASS (remote, keyboard, kbmouse, pointer, gamepad, other, none), INPUT (1: an input
# device, which "button" disarms) and WHY, in words
classify() {
  local d="$1" i c f a apps="" hid=1 any=0 boot=0 kb=0 ptr=0 pad=0 ctl=0 ir="" tuner=0 vin=""
  CLASS=none; INPUT=0; WHY=""
  [ "$(one "${d}/bDeviceClass")" = 09 ] && return
  for i in "${d}"/*:*; do
    [ -r "${i}/bInterfaceClass" ] || continue
    any=1
    c=$(one "${i}/bInterfaceClass")
    [ "${c}" = 03 ] || hid=0
    [ "${c}" = 03 ] && [ "$(one "${i}/bInterfaceSubClass")" = 01 ] && [ "$(one "${i}/bInterfaceProtocol")" = 01 ] && boot=1
    for f in "${i}"/rc/rc*; do
      [ -d "${f}" ] || continue
      a=$(sed -n 's/^DRV_NAME=//p' "${f}/uevent" 2>/dev/null | head -n 1)
      ir="${a:-rc-core}"
    done
    for f in "${i}"/dvb/* "${i}"/video4linux/*; do [ -e "${f}" ] && tuner=1; done
    # (an input device right under an interface that is not HID: made by the interface's own driver)
    if [ "${c}" != 03 ]; then
      for f in "${i}"/input/input*; do
        [ -d "${f}" ] && { a=$(readlink "${i}/driver" 2>/dev/null); vin="${a##*/}"; vin="${vin:-unknown}"; }
      done
    fi
    for f in "${i}"/0003:*/report_descriptor "${i}"/0003:*/0003:*/report_descriptor; do
      [ -r "${f}" ] && apps="${apps} $(hid_apps "${f}" | tr '\n' ' ')"
    done
  done
  [ ${any} = 1 ] || return
  for a in ${apps}; do
    case "${a}" in
      1:1|1:2|13:*) ptr=1 ;;
      1:4|1:5|1:8) pad=1 ;;
      1:6|1:7) kb=1 ;;
      1:128|12:1) ctl=1 ;;
    esac
  done
  if [ -n "${ir}" ]; then
    INPUT=1
    if [ ${tuner} = 0 ]; then CLASS=remote; WHY="an IR receiver (rc-core, ${ir})"
    else CLASS=other; WHY="a TV tuner with an IR receiver (${ir})"; fi
    return
  fi
  case "$(one "${d}/idVendor"):$(one "${d}/idProduct")" in
    20a0:0006) CLASS=remote; INPUT=1; WHY="an IR receiver known by its ID (flirc)"; return ;;
    # (the receivers of Xbox 360 game controllers: interfaces of the vendor, driven by xpad, with an input device only
    # while a controller is on)
    045e:0719|045e:0291) CLASS=gamepad; INPUT=1; WHY="a receiver of Xbox 360 game controllers (xpad), known by its ID"; return ;;
  esac
  # (an input device that the driver of a vendor's interface made: xpad for a wired Xbox controller)
  if [ $((kb + ptr + pad + ctl)) = 0 ] && [ -n "${vin}" ]; then
    INPUT=1
    case "${vin}" in
      xpad) CLASS=gamepad; WHY="a game controller (xpad)" ;;
      *) CLASS=other; WHY="an input device of a driver of its own (${vin})" ;;
    esac
    return
  fi
  [ $((kb + ptr + pad + ctl)) = 0 ] && return
  INPUT=1
  if [ ${hid} = 0 ]; then
    CLASS=other; WHY="keys of a device that is not only an input device (sound, video or other interfaces)"
  elif [ ${pad} = 1 ]; then
    CLASS=gamepad; WHY="a game controller"
  elif [ ${ptr} = 1 ] && [ ${kb} = 1 ]; then
    CLASS=kbmouse; WHY="a keyboard with a mouse or touchpad on one receiver"
  elif [ ${ptr} = 1 ]; then
    CLASS=pointer; WHY="a mouse or another pointer"
  elif [ ${ctl} = 1 ]; then
    CLASS=remote; WHY="a remote's receiver: media or system keys (HID Consumer or System Control), no mouse, no game controller"
  else
    CLASS=keyboard; WHY="a keyboard"
  fi
  [ ${boot} = 1 ] && WHY="${WHY}; a boot keyboard, which the kernel arms by itself"
  return 0
}

# The choice of the list for a USB device ($1, its folder): OV on, off or nothing. A line names the device by its IDs,
# and by its serial number or its port where it has one of them; the last line that fits it counts.
usb_override() {
  local d="$1" ids serial port a ty id q
  OV=""
  [ -r "${DEVICES_FILE}" ] || return 0
  ids="$(one "${d}/idVendor"):$(one "${d}/idProduct")"
  serial=$(clean "$(one "${d}/serial")"); port=${d##*/}
  while read -r a ty id q; do
    [ "${ty}" = usb ] && [ "${id}" = "${ids}" ] || continue
    case "${q}" in
      '') ;;
      serial=*) [ -n "${serial}" ] && [ "${q#serial=}" = "${serial}" ] || continue ;;
      port=*) [ "${q#port=}" = "${port}" ] || continue ;;
      *) continue ;;
    esac
    case "${a}" in on|off) OV=${a} ;; esac
  done < "${DEVICES_FILE}"
  return 0
}
# the choice of the list for a Bluetooth device ($1, its address): OV on, off or nothing
bt_override() {
  local a ty id q
  OV=""
  [ -r "${DEVICES_FILE}" ] || return 0
  while read -r a ty id q; do
    [ "${ty}" = bt ] && [ "${id}" = "$1" ] || continue
    case "${a}" in on|off) OV=${a} ;; esac
  done < "${DEVICES_FILE}"
  return 0
}
# WHY with what the list says of the device
why_override() {
  case "${OV}" in
    on) WHY="${WHY}; allowed in the list of devices" ;;
    off) WHY="${WHY}; kept from it in the list of devices" ;;
  esac
}

# what the choice asks of a device of that class: enabled, disabled, or keep (as the kernel or anything else set it)
target() {
  case "$1:${OV}" in
    button:*) if [ ${INPUT} = 1 ]; then echo disabled; else echo keep; fi; return ;;
    *:on) echo enabled; return ;;
    *:off) echo disabled; return ;;
  esac
  case "$1:${CLASS}" in
    remotes:remote|keyboards:remote|keyboards:keyboard|keyboards:kbmouse) echo enabled ;;
    *) echo keep ;;
  esac
}

usb_name() {
  local d="$1" m p
  m=$(one "${d}/manufacturer"); p=$(one "${d}/product")
  echo "USB ${d##*/} $(one "${d}/idVendor"):$(one "${d}/idProduct")${m:+ ${m}}${p:+ ${p}}" | tr -s ' ' | sed 's/ $//'
}

# set <device folder> <key> <ids> <name> <target>: power/wakeup as the target says, noted so that it can be put back
set_wakeup() {
  local d="$1" key="${STATE}/orig/$2" ids="$3" name="$4" t="$5" w rid rorig
  w=$(one "${d}/power/wakeup")
  if [ -r "${key}" ]; then
    read -r rid rorig < "${key}"
    # (a note of another device on that port is not this one's)
    [ "${rid}" = "${ids}" ] || { rm -f "${key}"; rorig=""; }
  else
    rorig=""
  fi
  if [ "${t}" = keep ]; then
    [ -n "${rorig}" ] || return 0
    rm -f "${key}"
    [ "${w}" = "${rorig}" ] && return 0
    if echo "${rorig}" > "${d}/power/wakeup" 2>/dev/null; then
      say "${name}: power/wakeup back to ${rorig}, as it was before this setting (wake from sleep with: $(choice_words "${CH}"))"
    else
      say "${name}: power/wakeup could not be put back to ${rorig}"
    fi
    return 0
  fi
  [ "${w}" = "${t}" ] && return 0
  [ -n "${rorig}" ] || echo "${ids} ${w}" > "${key}" 2>/dev/null
  if echo "${t}" > "${d}/power/wakeup" 2>/dev/null; then
    if [ "${t}" = enabled ]; then
      say "${name}: may wake the PC now (wake from sleep with: $(choice_words "${CH}"); ${WHY})"
    else
      say "${name}: may no longer wake the PC (wake from sleep with: $(choice_words "${CH}"); ${WHY})"
    fi
  else
    say "${name}: power/wakeup could not be set to ${t}"
  fi
}

do_usb() {
  local d="$1" w ids name said
  classify "${d}"
  [ "${CLASS}" = none ] && return 0
  usb_override "${d}"; why_override
  w=$(one "${d}/power/wakeup")
  ids="$(one "${d}/idVendor"):$(one "${d}/idProduct")"
  name=$(usb_name "${d}")
  case "${w}" in
    enabled|disabled) ;;
    *)
      # (said once a start, for a device that the choice would arm)
      if [ "$(target "${CH}")" = enabled ]; then
        said="${STATE}/said-${d##*/}-${ids}"
        [ -e "${said}" ] || { : > "${said}" 2>/dev/null; say "${name}: cannot wake a PC: it does not offer remote wakeup in its USB configuration (${WHY})"; }
      fi
      return 0
      ;;
  esac
  set_wakeup "${d}" "usb-${d##*/}" "${ids}" "${name}" "$(target "${CH}")"
}

# an IR receiver on the board (its rc device's parent is no USB device): kept as the kernel arms it, disarmed with
# "button"
do_board_ir() {
  local p="$1" rc="$2" drv w t
  w=$(one "${p}/power/wakeup")
  case "${w}" in enabled|disabled) ;; *) return 0 ;; esac
  drv=$(sed -n 's/^DRV_NAME=//p' "${rc}/uevent" 2>/dev/null | head -n 1)
  CLASS=remote; INPUT=1; OV=""; WHY="an IR receiver on the board (rc-core, ${drv:-rc-core})"
  if [ "${CH}" = button ]; then t=disabled; else t=keep; fi
  set_wakeup "${p}" "dev-${p##*/}" "${drv:-rc}" "IR ${p##*/}" "${t}"
}

# the USB device of a folder of sysfs (itself, an interface, a HID device or an rc device under it), or nothing
usb_of() {
  local d="$1"
  while [ -n "${d}" ] && [ "${d}" != "${SYSFS}" ] && [ "${d}" != / ]; do
    [ -r "${d}/idVendor" ] && [ -r "${d}/bDeviceClass" ] && { echo "${d}"; return 0; }
    d="${d%/*}"
  done
  return 1
}

# one run at a time: the HID interfaces of one device get their drivers at once, and each run reads the whole device
# as it is when it holds the lock, so the last one sees all of it
lock() {
  mkdir -p "${STATE}/orig" 2>/dev/null
  [ -d "${STATE}" ] && [ -w "${STATE}" ] || return 0
  exec 9> "${STATE}/lock"
  flock 9 2>/dev/null
  return 0
}

all_usb() {
  local l
  for l in "${SYSFS}"/bus/usb/devices/*; do
    case "${l##*/}" in *:*) continue ;; esac
    [ -r "${l}/idVendor" ] || continue
    readlink -f "${l}"
  done
}

# ---- Bluetooth (BlueZ over D-Bus, with busctl, which gives up after 5 s by itself)
bt_get() { ${BUSCTL} --timeout=5 get-property org.bluez "$1" org.bluez.Device1 "$2" 2>/dev/null; }
# the value of a property: "b true" gives true, 's "Name"' gives Name; exit status 1 where it is not there
bt_val() {
  local v
  v=$(bt_get "$1" "$2") || return 1
  [ -n "${v}" ] || return 1
  v=${v#* }
  case "${v}" in \"*\") v=${v#\"}; v=${v%\"} ;; esac
  printf '%s\n' "${v}"
}
# the paired input devices that BlueZ knows ("<path>" a line): WakeAllowed is there only for a device with an input
# profile (HID or HID over GATT: profiles/input/device.c, profiles/input/hog.c, device_set_wake_support)
bt_devices() {
  local p
  for p in $(${BUSCTL} --timeout=5 --list tree org.bluez 2>/dev/null | grep -E '^/org/bluez/hci[0-9]+/dev_[0-9A-F]{2}(_[0-9A-F]{2}){5}$'); do
    [ "$(bt_val "${p}" Paired)" = true ] || continue
    bt_val "${p}" WakeAllowed > /dev/null || continue
    echo "${p}"
  done
}
bt_addr() { echo "${1##*/dev_}" | tr _ :; }
# bt_classify <path>: CLASS (gamepad, pointer, keyboard, other), WHY, NAME, ADDR, from the icon that BlueZ gives the
# device (src/dbus-common.c: from the class of a Bluetooth Classic device, from the appearance of a Bluetooth LE one)
bt_classify() {
  local icon
  ADDR=$(bt_addr "$1")
  NAME=$(bt_val "$1" Alias) || NAME=""
  icon=$(bt_val "$1" Icon) || icon=""
  CLASS=other; WHY="a Bluetooth input device"; INPUT=1
  case "${icon}" in
    input-gaming) CLASS=gamepad; WHY="a Bluetooth game controller" ;;
    input-mouse|input-tablet) CLASS=pointer; WHY="a Bluetooth mouse or another pointer" ;;
    input-keyboard) CLASS=keyboard; WHY="a Bluetooth keyboard or remote" ;;
  esac
}
# what the choice asks of WakeAllowed: true or false. BlueZ's own value is true for every paired input device: kept
# for a keyboard, a remote and another input device, false for a mouse and a game controller (never by themselves),
# as the list says for a device in it, and false with "button"
bt_target() {
  case "$1:${OV}" in
    button:*) echo false ;;
    *:on) echo true ;;
    *:off) echo false ;;
    *) case "${CLASS}" in gamepad|pointer) echo false ;; *) echo true ;; esac ;;
  esac
}
bt_name() { echo "Bluetooth ${ADDR}${NAME:+ ${NAME}}"; }
# the USB device of an adapter ($1: /sys/class/bluetooth/hciN), or nothing
bt_adapter_usb() {
  local i
  [ -e "$1/device" ] || return 1
  i=$(readlink -f "$1/device")
  usb_of "${i}"
}
# how many devices of the list may wake the PC through the adapter hciN ($1), with the choice in force
bt_allowed_on() {
  local p n=0
  [ "${CH}" = button ] && { echo 0; return; }
  for p in ${BTDEV}; do
    case "${p}" in "/org/bluez/$1/"*) ;; *) continue ;; esac
    bt_override "$(bt_addr "${p}")"
    [ "${OV}" = on ] && n=$((n + 1))
  done
  echo ${n}
}
# The Bluetooth devices paired before this version, noted once, when the first adapter appears: "pending" and their
# addresses, one a line (bt_keep_before() keeps them), or "done" where none is paired (a new installation)
bt_snapshot() {
  local f a l="" n=0
  [ -e "${BT_BEFORE}" ] && return 0
  for f in "${BT_STORE}"/*/*/info; do
    [ -f "${f}" ] || continue
    a=${f%/info}; a=${a##*/}
    printf '%s\n' "${a}" | grep -q -E '^[0-9A-F]{2}(:[0-9A-F]{2}){5}$' || continue
    l="${l}${a}
"; n=$((n + 1))
  done
  if [ ${n} -gt 0 ]; then printf 'pending\n%s' "${l}"; else echo done; fi > "${BT_BEFORE}.new" 2>/dev/null &&
    mv -f "${BT_BEFORE}.new" "${BT_BEFORE}" || { rm -f "${BT_BEFORE}.new"; return 0; }
  [ ${n} -gt 0 ] && say "Bluetooth: ${n} device(s) paired before this version of Hades noted, so that a mouse or a game controller among them that could wake the PC still may"
  return 0
}
# what the adapter of the device $1 (its path) was as the firmware or Linux set it: its power/wakeup, or the value
# that a note of this script says it had before it was armed for the list
bt_adapter_before() {
  local h u key rid rorig
  h="${1#/org/bluez/}"; h="${h%%/*}"
  u=$(bt_adapter_usb "${SYSFS}/class/bluetooth/${h}") || { echo none; return; }
  key="${STATE}/orig/bta-${u##*/}"
  if [ -r "${key}" ]; then
    read -r rid rorig < "${key}"
    [ "${rid}" = "$(one "${u}/idVendor"):$(one "${u}/idProduct")" ] && [ -n "${rorig}" ] && { echo "${rorig}"; return; }
  fi
  one "${u}/power/wakeup"
}
# Once, at the first run that sets Bluetooth with BlueZ answering: a mouse or a game controller paired before this
# version that could wake the PC then (WakeAllowed on, its adapter armed by the firmware or Linux), with no line in the
# list, gets "on bt <address>" in it, as the user's choice. Nothing with "button".
bt_keep_before() {
  local p
  [ "$(one "${BT_BEFORE}")" = pending ] || return 0
  # (BlueZ not running, or not yet with these devices: at the next run)
  [ -n "${BTDEV}" ] || return 0
  if [ "${CH}" != button ]; then
    for p in ${BTDEV}; do
      bt_classify "${p}"
      case "${CLASS}" in gamepad|pointer) ;; *) continue ;; esac
      grep -q -x -F "${ADDR}" "${BT_BEFORE}" || continue
      bt_override "${ADDR}"
      [ -z "${OV}" ] || continue
      [ "$(bt_val "${p}" WakeAllowed)" = true ] || continue
      [ "$(bt_adapter_before "${p}")" = enabled ] || continue
      { [ -r "${DEVICES_FILE}" ] && awk 1 "${DEVICES_FILE}"; printf 'on bt %s\n' "${ADDR}"; } > "${DEVICES_FILE}.new" 2>/dev/null &&
        mv -f "${DEVICES_FILE}.new" "${DEVICES_FILE}" || { rm -f "${DEVICES_FILE}.new"; say "$(bt_name): could not be kept in the list of devices"; continue; }
      say "$(bt_name): may still wake the PC, as before this version of Hades (paired before it; ${WHY}; its adapter armed by the firmware or Linux): kept in the list of devices as your choice (System > Sleep > Devices that may wake it)"
    done
  fi
  sed -i '1s/^pending$/done/' "${BT_BEFORE}" 2>/dev/null
  return 0
}
do_bluetooth() {
  local p t v h u n ids name said
  BTDEV=$(bt_devices)
  bt_keep_before
  for p in ${BTDEV}; do
    bt_classify "${p}"; bt_override "${ADDR}"; why_override
    t=$(bt_target "${CH}")
    v=$(bt_val "${p}" WakeAllowed) || continue
    [ "${v}" = "${t}" ] && continue
    if ${BUSCTL} --timeout=5 set-property org.bluez "${p}" org.bluez.Device1 WakeAllowed b "${t}" > /dev/null 2>&1; then
      if [ "${t}" = true ]; then
        say "$(bt_name): may wake the PC when its adapter may (WakeAllowed on; wake from sleep with: $(choice_words "${CH}"); ${WHY})"
      else
        say "$(bt_name): may no longer wake the PC (WakeAllowed off; wake from sleep with: $(choice_words "${CH}"); ${WHY})"
      fi
    else
      say "$(bt_name): WakeAllowed could not be set to ${t}"
    fi
  done
  # the adapters: armed while a device of the list may wake the PC through it, otherwise as the kernel set them
  for h in "${SYSFS}"/class/bluetooth/hci*; do
    case "${h##*/}" in *:*) continue ;; esac
    u=$(bt_adapter_usb "${h}") || continue
    n=$(bt_allowed_on "${h##*/}")
    ids="$(one "${u}/idVendor"):$(one "${u}/idProduct")"
    name="Bluetooth adapter ${h##*/} ($(usb_name "${u}"))"
    INPUT=0; OV=""
    WHY="the adapter of ${n} Bluetooth device(s) allowed in the list of devices"
    case "$(one "${u}/power/wakeup")" in
      enabled|disabled) ;;
      *)
        if [ "${n}" -gt 0 ]; then
          said="${STATE}/said-bt-${u##*/}-${ids}"
          [ -e "${said}" ] || { : > "${said}" 2>/dev/null; say "${name}: cannot wake a PC: it does not offer remote wakeup in its USB configuration (the Bluetooth devices allowed in the list cannot wake it through it)"; }
        fi
        continue
        ;;
    esac
    if [ "${n}" -gt 0 ]; then t=enabled; else t=keep; fi
    set_wakeup "${u}" "bta-${u##*/}" "${ids}" "${name}" "${t}"
  done
}
# whether the adapter of the device $1 (its path) may wake the PC: yes, or no
bt_adapter_may() {
  local h u
  h="${1#/org/bluez/}"; h="${h%%/*}"
  u=$(bt_adapter_usb "${SYSFS}/class/bluetooth/${h}") || { echo no; return; }
  [ "$(one "${u}/power/wakeup")" = enabled ] && { echo yes; return; }
  # (armed before the next sleep, where a device of the list may wake the PC through it)
  [ "$(one "${u}/power/wakeup")" = disabled ] && [ "$(bt_allowed_on "${h}")" -gt 0 ] && { echo yes; return; }
  echo no
}

apply() {
  local d r p armed="" n=0 not="" cannot="" bta="" btn=""
  for d in $(all_usb); do do_usb "${d}"; done
  for r in "${SYSFS}"/class/rc/rc*; do
    [ -e "${r}/device" ] || continue
    p=$(readlink -f "${r}/device")
    usb_of "${p}" > /dev/null && continue
    do_board_ir "${p}" "$(readlink -f "${r}")"
  done
  [ "$1" = usb ] && return 0
  do_bluetooth
  [ "$1" = quiet ] && return 0
  for d in $(all_usb); do
    classify "${d}"
    [ "${CLASS}" = none ] && continue
    usb_override "${d}"; why_override
    case "$(one "${d}/power/wakeup")" in
      enabled) armed="${armed}${armed:+; }$(usb_name "${d}") (${WHY})"; n=$((n + 1)) ;;
      disabled) not="${not}${not:+; }$(usb_name "${d}") (${WHY})" ;;
      *) [ "$(target "${CH}")" = enabled ] && cannot="${cannot}${cannot:+; }$(usb_name "${d}") (${WHY})" ;;
    esac
  done
  for p in ${BTDEV}; do
    bt_classify "${p}"; bt_override "${ADDR}"; why_override
    if [ "$(bt_val "${p}" WakeAllowed)" = true ] && [ "$(bt_adapter_may "${p}")" = yes ]; then
      bta="${bta}${bta:+; }$(bt_name) (${WHY})"; n=$((n + 1))
    else
      btn="${btn}${btn:+; }$(bt_name) (${WHY})"
    fi
  done
  say "wake from sleep with: $(choice_words "${CH}"); USB input devices that may wake the PC: ${armed:-none}"
  [ -n "${not}" ] && say "USB input devices that may not: ${not}"
  [ -n "${cannot}" ] && say "chosen, but cannot wake a PC (no remote wakeup in their USB configuration): ${cannot}"
  [ -n "${bta}" ] && say "Bluetooth devices that may wake the PC: ${bta}"
  [ -n "${btn}" ] && say "Bluetooth devices that may not (not allowed, or their adapter may not wake the PC): ${btn}"
  echo "armed ${n}"
}

reasons() {
  local d r p
  for d in $(all_usb); do
    classify "${d}"
    [ "${CLASS}" = none ] && continue
    usb_override "${d}"; why_override
    echo "${d##*/}|$(one "${d}/power/wakeup")|${CLASS}|${WHY}"
  done
  for r in "${SYSFS}"/class/rc/rc*; do
    [ -e "${r}/device" ] || continue
    p=$(readlink -f "${r}/device")
    usb_of "${p}" > /dev/null && continue
    echo "${p##*/}|$(one "${p}/power/wakeup")|remote|an IR receiver on the board (rc-core, $(sed -n 's/^DRV_NAME=//p' "${r}/uevent" 2>/dev/null | head -n 1))"
  done
  BTDEV=$(bt_devices)
  for p in ${BTDEV}; do
    bt_classify "${p}"; bt_override "${ADDR}"; why_override
    # (what the next sleep holds: the choice, and the adapter as it will be then)
    if [ "$(bt_target "${CH}")" = true ] && [ "$(bt_adapter_may "${p}")" = yes ]; then
      echo "bt ${ADDR}|enabled|${CLASS}|$(plain "$(bt_name)"): ${WHY}"
    else
      echo "bt ${ADDR}|disabled|${CLASS}|$(plain "$(bt_name)"): ${WHY}; $( [ "$(bt_target "${CH}")" = true ] && echo "its adapter may not wake the PC" || echo "WakeAllowed off")"
    fi
  done
}

# For the setting: every input device that could be in the list. "usb|<key>|<power/wakeup or none>|<class>|<name>|
# <why>|<on, off or ->" and "bt|<address>|<enabled or disabled>|<class>|<name>|<why>|<on, off or ->". The key of a USB
# device is its IDs, with its serial number where another input device has the same IDs and their serial numbers
# differ, or else its port. A Bluetooth device's state is what the next sleep holds (bt_target and its adapter).
list() {
  local d ids all="" k s n
  for d in $(all_usb); do
    classify "${d}"
    [ "${CLASS}" = none ] && continue
    all="${all} ${d}"
  done
  for d in ${all}; do
    classify "${d}"
    ids="$(one "${d}/idVendor"):$(one "${d}/idProduct")"
    s=$(clean "$(one "${d}/serial")")
    k="${ids}"
    # (another input device with the same IDs: the serial number tells them apart where both have one that differs)
    for n in ${all}; do
      [ "${n}" != "${d}" ] && [ "$(one "${n}/idVendor"):$(one "${n}/idProduct")" = "${ids}" ] || continue
      if [ -n "${s}" ] && [ "$(clean "$(one "${n}/serial")")" != "${s}" ]; then k="${ids} serial=${s}"; else k="${ids} port=${d##*/}"; fi
      break
    done
    usb_override "${d}"
    echo "usb|${k}|$(one "${d}/power/wakeup")|${CLASS}|$(plain "$(usb_name "${d}")")|$(plain "${WHY}")|${OV:--}"
  done
  BTDEV=$(bt_devices)
  for d in ${BTDEV}; do
    bt_classify "${d}"; bt_override "${ADDR}"
    if [ "$(bt_target "${CH}")" = true ] && [ "$(bt_adapter_may "${d}")" = yes ]; then s=enabled; else s=disabled; fi
    echo "bt|${ADDR}|${s}|${CLASS}|$(plain "$(bt_name)")|$(plain "${WHY}")|${OV:--}"
  done
}

CH=$(choice)
case "$1" in
  device)
    lock
    p="${SYSFS}${2}"
    if u=$(usb_of "${p}"); then
      do_usb "${u}"
    else
      case "${p}" in
        */rc/rc*) do_board_ir "${p%/rc/rc*}" "${p}" ;;
      esac
    fi
    ;;
  apply)
    lock
    [ "$2" = quiet ] || LOUD=1
    apply "$2"
    ;;
  usb) lock; apply usb ;;
  bluetooth) lock; do_bluetooth ;;
  adapter)
    lock
    case "${2##*/}" in hci[0-9]*:*|'') ;; hci[0-9]*) bt_snapshot ;; esac
    ;;
  reasons) reasons ;;
  list) list ;;
  choice) echo "${CH}" ;;
  words) choice_words "${CH}" ;;
esac
exit 0
