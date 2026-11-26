#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# What may wake the PC from sleep (s2idle or suspend to RAM), by the setting "Wake from sleep" of the LibreELEC
# settings (System > Sleep), kept in /storage/.cache/hades-wake as one word:
#   remotes    (the default, also with no file) the receivers of remote controls may wake the PC, besides what the
#              kernel arms by itself (the keyboards of the boot protocol, hid-core.c of usbhid);
#   keyboards  also every keyboard, and a receiver that a keyboard shares with a mouse or a touchpad;
#   button     only the power button: no keyboard, remote or other input device (Wake-on-LAN is not changed here).
# Mice and game controllers are never armed by this; nor is anything else, which keeps what the kernel, LibreELEC or
# a rule of the user set.
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
# What this set is noted in /run/hades-wake/orig (the device's port, its IDs and the value it had), so that another
# choice puts it back as it was; each change is said in the kernel log, on a line that starts with "hades-wake:".
#
# usage: wake.sh device <devpath>   (udev, 70-hades-wake.rules: a HID device that got its driver, or an IR receiver)
#        wake.sh apply [quiet]       (the setting, and after each sleep: every device; the summary on the standard
#                                    output and in the kernel log, unless quiet)
#        wake.sh reasons             (port|power/wakeup|class|why for each device that is an input device; nothing set)
#        wake.sh choice              (the choice in force: remotes, keyboards or button)
#        wake.sh words               (the choice in force, in words)
# HADES_SYSFS, HADES_RUN, HADES_KMSG and HADES_WAKE_CHOICE stand for /sys, /run, /dev/kmsg and the file of the choice
# in a test.

SYSFS="${HADES_SYSFS:-/sys}"
RUN="${HADES_RUN:-/run}"
KMSG="${HADES_KMSG:-/dev/kmsg}"
CHOICE_FILE="${HADES_WAKE_CHOICE:-/storage/.cache/hades-wake}"
STATE="${RUN}/hades-wake"
LOUD=0

say() {
  echo "hades-wake: $*" >> "${KMSG}" 2>/dev/null || echo "hades-wake: $*" >&2
  [ ${LOUD} = 1 ] && echo "$*"
  return 0
}
# the first line of a file, or nothing
one() { head -n 1 "$1" 2>/dev/null; }

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
  local d="$1" i c f a apps="" hid=1 any=0 boot=0 kb=0 ptr=0 pad=0 ctl=0 ir="" tuner=0
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
  esac
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

# what the choice asks of a device of that class: enabled, disabled, or keep (as the kernel or anything else set it)
target() {
  case "$1:${CLASS}" in
    remotes:remote|keyboards:remote|keyboards:keyboard|keyboards:kbmouse) echo enabled ;;
    button:*) if [ ${INPUT} = 1 ]; then echo disabled; else echo keep; fi ;;
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
  CLASS=remote; INPUT=1; WHY="an IR receiver on the board (rc-core, ${drv:-rc-core})"
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

apply() {
  local d r p armed="" n=0 not="" cannot=""
  for d in $(all_usb); do do_usb "${d}"; done
  for r in "${SYSFS}"/class/rc/rc*; do
    [ -e "${r}/device" ] || continue
    p=$(readlink -f "${r}/device")
    usb_of "${p}" > /dev/null && continue
    do_board_ir "${p}" "$(readlink -f "${r}")"
  done
  [ "$1" = quiet ] && return 0
  for d in $(all_usb); do
    classify "${d}"
    [ "${CLASS}" = none ] && continue
    case "$(one "${d}/power/wakeup")" in
      enabled) armed="${armed}${armed:+; }$(usb_name "${d}") (${WHY})"; n=$((n + 1)) ;;
      disabled) not="${not}${not:+; }$(usb_name "${d}") (${WHY})" ;;
      *) [ "$(target "${CH}")" = enabled ] && cannot="${cannot}${cannot:+; }$(usb_name "${d}") (${WHY})" ;;
    esac
  done
  say "wake from sleep with: $(choice_words "${CH}"); USB input devices that may wake the PC: ${armed:-none}"
  [ -n "${not}" ] && say "USB input devices that may not: ${not}"
  [ -n "${cannot}" ] && say "chosen, but cannot wake a PC (no remote wakeup in their USB configuration): ${cannot}"
  echo "armed ${n}"
}

reasons() {
  local d r p
  for d in $(all_usb); do
    classify "${d}"
    [ "${CLASS}" = none ] && continue
    echo "${d##*/}|$(one "${d}/power/wakeup")|${CLASS}|${WHY}"
  done
  for r in "${SYSFS}"/class/rc/rc*; do
    [ -e "${r}/device" ] || continue
    p=$(readlink -f "${r}/device")
    usb_of "${p}" > /dev/null && continue
    echo "${p##*/}|$(one "${p}/power/wakeup")|remote|an IR receiver on the board (rc-core, $(sed -n 's/^DRV_NAME=//p' "${r}/uevent" 2>/dev/null | head -n 1))"
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
  reasons) reasons ;;
  choice) echo "${CH}" ;;
  words) choice_words "${CH}" ;;
esac
exit 0
