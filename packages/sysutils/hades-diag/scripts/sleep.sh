#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# Each sleep of the system, said in the kernel log on lines that start with "hades-sleep:": the journal of the start
# keeps them, and with the diagnostics on, the journal of a start that never came back from a sleep is saved by the
# next start (previous-boot). systemd-sleep runs this before and after each sleep (/usr/lib/systemd/system-sleep/
# hades-sleep: $1 pre or post, $2 the kind of sleep); the diagnostics run it with "state" (collect.sh).
# Why (issue 32 of LibreELEC Hades): on a PC whose firmware offers no suspend to RAM (no _S3 in its ACPI tables) the
# kernel suspends to idle (s2idle). Without a device of the platform for a low-power S0 idle (LPS0) nothing there
# lowers the power of the PC much below its idle, and only the power button and the devices armed for it wake the PC:
# an IR receiver or a keyboard that is not armed does nothing, which looks like a PC that hangs. The report could say
# what had woken a PC only while the start of that sleep still ran, and it did not say which input devices were armed.
#
# pre:   the kind of sleep, what the firmware offers (deep, suspend to RAM; a low-power S0 idle declared in its FADT;
#        an LPS0 device), what may wake the PC (the power button, each USB device and network card armed for it), the
#        USB input devices that could wake it and are not armed, and a note of the kernel's wakeup counts in
#        /run/hades-sleep for the line after the sleep. With hades-wake, the setting "Wake from sleep", the kind of each
#        input device (why it may wake the PC or not), and the remotes' receivers that the setting would arm and that
#        cannot wake a PC (no remote wakeup in their USB configuration); the paired Bluetooth input devices that may
#        wake it at this sleep (allowed, and their adapter armed) and those that may not, with why.
# post:  whether the sleep was carried out or failed (at which step, on which device), how long the system was away,
#        the interrupt that woke it (pm_wakeup_irq, with its name in /proc/interrupts), each source of wakeups of the
#        kernel whose count of wakeups grew while it slept (a USB controller, a USB device, a network card, the power
#        button), and how long the hardware stayed in a low-power state where the platform measures it.
# state: the lines of pre that describe the firmware and the devices, on its standard output; nothing is noted.
# Only reads, and the note in /run; a sleep never fails because of it (exit 0).
#
# HADES_SYSFS, HADES_PROC, HADES_RUN, HADES_KMSG and HADES_WAKE stand for /sys, /proc, /run, /dev/kmsg and
# /usr/lib/hades-wake/wake.sh in a test.

SYSFS="${HADES_SYSFS:-/sys}"
PROC="${HADES_PROC:-/proc}"
RUN="${HADES_RUN:-/run}"
KMSG="${HADES_KMSG:-/dev/kmsg}"
WAKE="${HADES_WAKE:-/usr/lib/hades-wake/wake.sh}"
NOTE="${RUN}/hades-sleep"
OUT=kmsg

say() {
  if [ "${OUT}" = stdout ]; then
    echo "hades-sleep: $*"
  else
    echo "hades-sleep: $*" >> "${KMSG}" 2>/dev/null || echo "hades-sleep: $*" >&2
  fi
}
# the first line of a file, or nothing
one() { head -n 1 "$1" 2>/dev/null; }
# seconds since the start, in tenths (from /proc/uptime, which counts the time asleep)
tenths() {
  local u
  read -r u _ < "${PROC}/uptime" 2>/dev/null || { echo 0; return; }
  case "${u}" in
    *.*) echo $(( ${u%.*} * 10 + $(echo "${u#*.}0" | cut -c1) )) ;;
    *) echo $(( u * 10 )) ;;
  esac
}
s() { echo "$(($1 / 10)).$(($1 % 10)) s"; }

usb_name() {
  local d="${SYSFS}/bus/usb/devices/$1" m p
  m=$(one "$d/manufacturer"); p=$(one "$d/product")
  echo "USB $1 $(one "$d/idVendor"):$(one "$d/idProduct")${m:+ $m}${p:+ $p}" | tr -s ' ' | sed 's/ $//'
}
# a source of wakeups of the kernel (its name: a device), said for a person
describe() {
  local n="$1" a c h p
  if [ -r "${SYSFS}/bus/usb/devices/${n}/idVendor" ]; then
    usb_name "${n}"; return
  fi
  if [ -r "${SYSFS}/bus/pci/devices/${n}/class" ]; then
    c=$(one "${SYSFS}/bus/pci/devices/${n}/class")
    case "${c}" in
      0x0c03*) echo "${n} (USB controller)" ;;
      0x02*) echo "${n} (network card)" ;;
      0x0604*) echo "${n} (PCIe port)" ;;
      0x03*) echo "${n} (graphics card)" ;;
      0x0403*) echo "${n} (sound)" ;;
      *) echo "${n} (PCI class ${c})" ;;
    esac
    return
  fi
  a="${SYSFS}/bus/acpi/devices/${n}"
  if [ -d "${a}" ]; then
    h=$(one "${a}/modalias")
    case "${h}" in
      *:PNP0C0C:*|*:LNXPWRBN:*) echo "${n} (the power button)"; return ;;
      *:PNP0C0E:*|*:LNXSLPBN:*) echo "${n} (the sleep button)"; return ;;
      *:PNP0C0D:*) echo "${n} (the lid)"; return ;;
    esac
    p=$(readlink "${a}/physical_node" 2>/dev/null)
    if [ -n "${p}" ]; then
      echo "${n} (ACPI, for $(describe "${p##*/}"))"; return
    fi
    echo "${n} (ACPI)"; return
  fi
  case "${n}" in
    alarmtimer*|rtc*) echo "${n} (an alarm of the clock of the board)" ;;
    *) echo "${n}" ;;
  esac
}

firmware() {
  local kinds deep fadt f flags lps0 pmc d m
  kinds=$(one "${SYSFS}/power/mem_sleep")
  if [ -z "${kinds}" ]; then
    say "the kernel offers no sleep to RAM here (no /sys/power/mem_sleep)"
    return
  fi
  case " ${kinds} " in
    *" deep "*|*" [deep] "*) deep=1 ;;
    *) deep=0 ;;
  esac
  # (the flags of the FADT: 4 bytes at offset 112; bit 21, low-power S0 idle)
  fadt="not known (no FADT to read)"
  f="${SYSFS}/firmware/acpi/tables/FACP"
  if [ -r "${f}" ]; then
    flags=$(od -An -tu4 -j112 -N4 "${f}" 2>/dev/null | tr -d ' \n')
    case "${flags}" in
      ''|*[!0-9]*) ;;
      *) if [ $(( flags / 2097152 % 2 )) = 1 ]; then fadt="declared in the FADT"; else fadt="not declared in the FADT"; fi ;;
    esac
  fi
  lps0=""; pmc=""
  for d in "${SYSFS}"/bus/acpi/devices/*; do
    m=$(one "${d}/modalias")
    case "${m}" in
      *:PNP0D80:*|*:AMDI0005:*|*:AMDI0006:*) lps0="${lps0}${lps0:+ }${d##*/}" ;;
    esac
    case "${m}" in
      *:AMDI000[5-9A-C]:*) pmc="${pmc}${pmc:+ }${d##*/}" ;;
    esac
  done
  if [ ${deep} = 1 ]; then
    say "kinds of sleep: ${kinds} (deep: suspend to RAM, which the firmware offers)"
  else
    say "kinds of sleep: ${kinds} (no deep: the firmware offers no suspend to RAM, S3, in its ACPI tables)"
  fi
  say "low-power S0 idle: ${fadt}; a device for it (LPS0): ${lps0:-none}; an AMD power management controller (amd_pmc): ${pmc:-none}"
  if [ ${deep} = 0 ] && [ -z "${lps0}" ]; then
    say "so a sleep here is s2idle with no low-power state of the platform: the power supply, the fans and the chipset stay on and the PC can draw nearly as much as when idle; only the power button and the devices armed for it wake it"
  fi
}

# the kind of a USB input device that hades-wake told ("<port>|<power/wakeup>|<class>|<why>" lines in $R), as
# " (<why>)", or nothing
why() {
  [ -n "${R}" ] || return 0
  echo "${R}" | awk -F'|' -v p="$1" '$1 == p { print " (" $4 ")"; exit }'
}

devices() {
  local d c w armed="" could="" cannot="" R="" bta btn
  # (hades-wake: the setting, and the kind of each input device)
  if [ -r "${WAKE}" ]; then
    R=$(sh "${WAKE}" reasons 2>/dev/null)
    say "wake from sleep with: $(sh "${WAKE}" words 2>/dev/null) (the setting Wake from sleep, LibreELEC settings > System > Sleep)"
  fi
  for d in "${SYSFS}"/bus/acpi/devices/*; do
    case "$(one "${d}/modalias")" in
      *:PNP0C0C:*|*:LNXPWRBN:*) armed="the power button"; break ;;
    esac
  done
  for d in "${SYSFS}"/bus/usb/devices/*; do
    [ -r "${d}/idVendor" ] || continue
    c=$(one "${d}/bDeviceClass")
    [ "${c}" = 09 ] && continue
    w=$(one "${d}/power/wakeup")
    case "${w}" in
      enabled) armed="${armed}${armed:+; }$(usb_name "${d##*/}")$(why "${d##*/}")" ;;
      disabled)
        # (an input device: a HID interface that the kernel made an input of; a keyboard, a remote's receiver, a mouse)
        ls -d "${d}"/*:*/0003:*/input > /dev/null 2>&1 && could="${could}${could:+; }$(usb_name "${d##*/}")$(why "${d##*/}")"
        ;;
      *)
        # (a remote's receiver that cannot wake a PC: hades-wake says which)
        echo "${R}" | grep -q "^${d##*/}||remote|" && cannot="${cannot}${cannot:+; }$(usb_name "${d##*/}")$(why "${d##*/}")"
        ;;
    esac
  done
  for d in "${SYSFS}"/class/net/*; do
    [ "$(one "${d}/device/power/wakeup")" = enabled ] && armed="${armed}${armed:+; }network ${d##*/} (Wake-on-LAN)"
  done
  # (the paired Bluetooth input devices, as hades-wake told them: "bt <address>|enabled|<class>|<name>: <why>" for one
  # that may wake the PC at this sleep, disabled with why not)
  bta=$(echo "${R}" | awk -F'|' '$1 ~ /^bt / && $2 == "enabled" { printf "%s%s", (n++ ? "; " : ""), $4 }')
  btn=$(echo "${R}" | awk -F'|' '$1 ~ /^bt / && $2 != "enabled" { printf "%s%s", (n++ ? "; " : ""), $4 }')
  [ -n "${bta}" ] && armed="${armed}${armed:+; }${bta}"
  say "may wake the PC: ${armed:-nothing that this can see}"
  [ -n "${btn}" ] && say "Bluetooth input devices that may not wake it: ${btn}"
  if [ -n "${could}" ]; then
    say "could wake it but are not armed: ${could} (the setting Wake from sleep chooses which kinds may, and its list which devices; echo enabled > /sys/bus/usb/devices/<port>/power/wakeup arms one until the next start)"
  else
    say "no USB input device that could wake the PC is left unarmed"
  fi
  [ -n "${cannot}" ] && say "remotes' receivers that cannot wake a PC (no remote wakeup in their USB configuration): ${cannot}"
  return 0
}

# the kernel's wakeup counts: "<count> <name>" for each source of wakeups
counts() {
  local w
  for w in "${SYSFS}"/class/wakeup/wakeup*; do
    [ -r "${w}/wakeup_count" ] || continue
    echo "$(one "${w}/wakeup_count") $(one "${w}/name")"
  done
}
st() { one "${SYSFS}/power/suspend_stats/$1"; }
used() { one "${SYSFS}/power/mem_sleep" | sed -n 's/.*\[\([a-z0-9]*\)\].*/\1/p'; }

pre() {
  say "before ${1} ($(used)):"
  firmware
  devices
  mkdir -p "${NOTE}" 2>/dev/null || return 0
  counts > "${NOTE}/counts" 2>/dev/null
  echo "$(tenths) $(st success) $(st fail)" > "${NOTE}/before" 2>/dev/null
}

post() {
  local t0 s0 f0 t1 s1 f1 away irq name grew c n old hw
  t1=$(tenths); s1=$(st success); f1=$(st fail)
  if read -r t0 s0 f0 < "${NOTE}/before" 2>/dev/null; then
    away=" after $(s $(( t1 - t0 )))"
  else
    t0=""; s0=""; f0=""; away=""
  fi
  if [ -n "${f0}" ] && [ -n "${f1}" ] && [ "${f1}" -gt "${f0}" ] 2>/dev/null; then
    say "back from ${1}${away}: the sleep failed at $(st last_failed_step) ($(st last_failed_dev), error $(st last_failed_errno))"
  elif [ -n "${s0}" ] && [ -n "${s1}" ] && [ "${s1}" -gt "${s0}" ] 2>/dev/null; then
    say "back from ${1} ($(used))${away}"
  elif [ -n "${s0}" ]; then
    say "back from ${1}${away}: the kernel counted no sleep (it was refused or given up before the system slept)"
  else
    say "back from ${1} (nothing was noted before it: not said how long, nor what grew)"
  fi
  irq=$(one "${SYSFS}/power/pm_wakeup_irq")
  if [ -n "${irq}" ]; then
    name=$(awk -v n="${irq}:" '$1 == n { for (i = 2; i <= NF; i++) if ($i ~ /-(edge|level|fasteoi)$/) { s = ""; for (j = i + 1; j <= NF; j++) s = s (s == "" ? "" : " ") $j; print s; exit } }' "${PROC}/interrupts" 2>/dev/null)
    say "woken by interrupt ${irq}${name:+ (${name})}"
  fi
  if [ -r "${NOTE}/counts" ]; then
    grew=""
    counts | while read -r c n; do
      old=$(awk -v n="${n}" '{ c = $1; $1 = ""; sub(/^ /, ""); if ($0 == n) { print c; exit } }' "${NOTE}/counts")
      [ -n "${c}" ] && [ "${c}" -gt "${old:-0}" ] 2>/dev/null && echo "$(describe "${n}")"
    done > "${NOTE}/grew" 2>/dev/null
    grew=$(sed ':a;N;$!ba;s/\n/; /g' "${NOTE}/grew" 2>/dev/null)
    say "sources of wakeups that counted one while it slept: ${grew:-none}"
  fi
  hw=$(st last_hw_sleep)
  if [ -n "${hw}" ] && [ "${hw}" -gt 0 ] 2>/dev/null; then
    say "the hardware stayed in a low-power state for $(s $(( hw / 100000 )))"
  fi
  rm -rf "${NOTE}"
}

case "$1" in
  pre) pre "${2:-sleep}" ;;
  post) post "${2:-sleep}" ;;
  state) OUT=stdout; firmware; devices ;;
esac
exit 0
