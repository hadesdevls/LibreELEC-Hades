#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of the lines that each sleep leaves in the kernel log (packages/sysutils/hades-diag/scripts/sleep.sh,
# /usr/lib/systemd/system-sleep/hades-sleep in the image) and of what the diagnostics print with it ("state"). A /sys
# and a /proc of files stand for a PC like the one of issue 32 (a B550 board whose firmware offers s2idle only: no
# deep, no low-power S0 idle in its FADT, no LPS0 device; a receiver armed to wake it, an IR receiver and a keyboard
# receiver that are not, a UPS, a hub and a game controller receiver that are no input devices to arm) and for one
# whose firmware offers deep and an LPS0 device; the busybox of the image runs the script, as systemd-sleep does.
#
# Usage: sleep_report_test.sh <busybox> <sleep.sh>
# Then each of its mutations (a copy of the script with one rule taken out) must fail at least one check: "mutation Mn
# (...) caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test could not run.
set -u
BBI=${1:-}; SC=${2:-}
[ -x "$BBI" ] && [ -f "$SC" ] || { echo "usage: $0 <busybox> <sleep.sh>"; exit 2; }
T=$(mktemp -d); trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done

FAILS=0
check() {
  if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi
}
# run <script> <args...>: as systemd-sleep or collect.sh runs it; status in RC, standard output in $T/out, the kernel
# log in $T/kmsg (appended), standard error in $T/err
run() {
  local s=$1; shift
  PATH="$T/bb" HADES_SYSFS="$T/sys" HADES_PROC="$T/proc" HADES_RUN="$T/run" HADES_KMSG="$T/kmsg" \
    /usr/bin/timeout 10 "$T/bb/sh" "$s" "$@" > "$T/out" 2> "$T/err"
  RC=$?
}
w() { mkdir -p "$(dirname "$1")"; printf '%s\n' "$2" > "$1"; }
# fadt <flags>: a FADT of 276 bytes whose flags (offset 112) are <flags>
fadt() {
  mkdir -p "$T/sys/firmware/acpi/tables"
  python3 -I -c 'import struct,sys; b=bytearray(276); b[0:4]=b"FACP"; struct.pack_into("<I",b,4,276); struct.pack_into("<I",b,112,int(sys.argv[2],0)); open(sys.argv[1],"wb").write(b)' \
    "$T/sys/firmware/acpi/tables/FACP" "$1"
}
usb() { # usb <port> <vid> <pid> <class> <wakeup|-> <product> [hid-input|hid]
  local d=$T/sys/bus/usb/devices/$1
  w "$d/idVendor" "$2"; w "$d/idProduct" "$3"; w "$d/bDeviceClass" "$4"; w "$d/product" "$6"
  [ "$5" = - ] || w "$d/power/wakeup" "$5"
  case "${7:-}" in
    hid-input) mkdir -p "$d/$1:1.0/0003:$2:$3.0001/input/input5" ;;
    hid) mkdir -p "$d/$1:1.0/0003:$2:$3.0002/hidraw" ;;
  esac
}
wsrc() { w "$T/sys/class/wakeup/$1/name" "$2"; w "$T/sys/class/wakeup/$1/wakeup_count" "$3"; }
# the PC of issue 32
mibix() {
  chmod -R u+w "$T/sys" "$T/proc" "$T/run" 2>/dev/null; rm -rf "$T/sys" "$T/proc" "$T/run" "$T/kmsg" "$T/out" "$T/err"
  w "$T/sys/power/mem_sleep" "[s2idle]"
  for f in success fail; do w "$T/sys/power/suspend_stats/$f" 0; done
  w "$T/sys/power/suspend_stats/last_failed_step" ""; w "$T/sys/power/suspend_stats/last_failed_dev" ""
  w "$T/sys/power/suspend_stats/last_failed_errno" 0; w "$T/sys/power/suspend_stats/last_hw_sleep" 0
  fadt 0x000084a5
  w "$T/sys/bus/acpi/devices/PNP0C0C:00/modalias" "acpi:PNP0C0C:"
  w "$T/sys/bus/acpi/devices/LNXPWRBN:00/modalias" "acpi:LNXPWRBN:"
  w "$T/sys/bus/acpi/devices/PNP0A08:00/modalias" "acpi:PNP0A08:PNP0A03:"
  mkdir -p "$T/sys/bus/acpi/devices/device:2b" "$T/sys/bus/pci/devices/0000:07:00.3" "$T/sys/bus/pci/devices/0000:04:00.0/power"
  ln -s "$T/sys/bus/pci/devices/0000:07:00.3" "$T/sys/bus/acpi/devices/device:2b/physical_node"
  w "$T/sys/bus/pci/devices/0000:07:00.3/class" 0x0c0330
  w "$T/sys/bus/pci/devices/0000:04:00.0/class" 0x020000
  w "$T/sys/bus/pci/devices/0000:04:00.0/power/wakeup" enabled
  mkdir -p "$T/sys/class/net/eth0" "$T/sys/class/net/lo"
  ln -s "$T/sys/bus/pci/devices/0000:04:00.0" "$T/sys/class/net/eth0/device"
  usb 5-2.4.2.6 6c38 3901 00 enabled "AX68004" hid-input
  usb 5-2.4.1 20a0 0006 00 disabled "flirc" hid-input
  usb 5-2.4.2.1 046d c52b 00 disabled "USB Receiver" hid-input
  usb 1-7 051d 0002 00 disabled "Back-UPS" hid
  usb 1-8 045e 0719 ff disabled "Xbox 360 Wireless Receiver"
  usb 5-2 0bda 5411 09 disabled "USB2.1 Hub" hid-input
  usb 5-2.4.4 1b3f 2008 00 - "USB Audio Device" hid-input
  usb usb5 1d6b 0002 09 enabled "xHCI Host Controller"
  wsrc wakeup0 "0000:07:00.3" 0; wsrc wakeup1 "device:2b" 0; wsrc wakeup2 "PNP0C0C:00" 0
  wsrc wakeup3 "5-2.4.2.6" 0; wsrc wakeup4 "0000:04:00.0" 0; wsrc wakeup5 "alarmtimer.0.auto" 0
  mkdir -p "$T/proc" "$T/run"
  printf '%s\n' "           CPU0       CPU1" \
    "  8:          0          0  IR-IO-APIC    8-edge      rtc0" \
    "  9:          0          1  IR-IO-APIC    9-fasteoi   acpi" \
    " 33:          1          1  IR-PCI-MSI-0000:00:08.1    0-edge      PCIe PME, aerdrv, PCIe bwctrl (shared)" > "$T/proc/interrupts"
  w "$T/proc/uptime" "100.40 1500.00"
}
# a PC whose firmware offers deep and declares a low-power S0 idle, with an LPS0 device
modern() {
  mibix
  w "$T/sys/power/mem_sleep" "s2idle [deep]"
  fadt 0x002084a5
  w "$T/sys/bus/acpi/devices/AMDI0007:00/modalias" "acpi:AMDI0007:"
  w "$T/sys/bus/acpi/devices/PNP0D80:00/modalias" "acpi:PNP0D80:"
}
has() { grep -q -F -- "$1" "$2"; }
hasnt() { ! grep -q -F -- "$1" "$2"; }

cases() {
  local S=$1
  echo "1. state on the PC of issue 32: no deep, no low-power S0 idle, no LPS0, and what that means"
  mibix; run "$S" state
  [ $RC = 0 ] && [ ! -e "$T/kmsg" ] &&
    has "hades-sleep: kinds of sleep: [s2idle] (no deep: the firmware offers no suspend to RAM, S3, in its ACPI tables)" "$T/out" &&
    has "low-power S0 idle: not declared in the FADT; a device for it (LPS0): none; an AMD power management controller (amd_pmc): none" "$T/out" &&
    has "so a sleep here is s2idle with no low-power state of the platform" "$T/out"; check "1 state, no low-power sleep" $?
  echo "2. state: what may wake it (the power button, the armed receiver, the network card) and nothing else"
  has "may wake the PC: the power button; USB 5-2.4.2.6 6c38:3901 AX68004; network eth0 (Wake-on-LAN)" "$T/out" &&
    hasnt "usb5" "$T/out" && hasnt "network lo" "$T/out"; check "2 armed" $?
  echo "3. state: the input devices that could wake it and are not armed; not the UPS, the hub, the game receiver, a device that cannot"
  has "could wake it but are not armed: USB 5-2.4.1 20a0:0006 flirc; USB 5-2.4.2.1 046d:c52b USB Receiver (the setting Wake from sleep chooses which kinds may, and its list which devices; echo enabled" "$T/out" &&
    hasnt "Back-UPS" "$T/out" && hasnt "Hub" "$T/out" && hasnt "Xbox" "$T/out" && hasnt "Audio" "$T/out"; check "3 could, not armed" $?
  echo "4. state where the firmware offers deep and an LPS0 device: said so, and no note of a sleep without a low-power state"
  modern; run "$S" state
  [ $RC = 0 ] && has "kinds of sleep: s2idle [deep] (deep: suspend to RAM, which the firmware offers)" "$T/out" &&
    has "low-power S0 idle: declared in the FADT; a device for it (LPS0): PNP0D80:00; an AMD power management controller (amd_pmc): AMDI0007:00" "$T/out" &&
    hasnt "so a sleep here" "$T/out"; check "4 state, deep and LPS0" $?
  echo "5. pre: the lines go to the kernel log, none to the standard output, and the counts are noted"
  mibix; run "$S" pre suspend
  [ $RC = 0 ] && [ ! -s "$T/out" ] && has "hades-sleep: before suspend (s2idle):" "$T/kmsg" &&
    has "hades-sleep: may wake the PC: the power button; USB 5-2.4.2.6" "$T/kmsg" &&
    [ -s "$T/run/hades-sleep/counts" ] && [ -s "$T/run/hades-sleep/before" ]; check "5 pre" $?
  echo "6. post after a sleep that went: how long, the interrupt and its name, the sources that counted a wakeup, nothing left in /run"
  w "$T/sys/power/suspend_stats/success" 1; w "$T/proc/uptime" "115.90 1500.00"; w "$T/sys/power/pm_wakeup_irq" 9
  w "$T/sys/class/wakeup/wakeup0/wakeup_count" 1; w "$T/sys/class/wakeup/wakeup1/wakeup_count" 1
  : > "$T/kmsg"; run "$S" post suspend
  [ $RC = 0 ] && has "hades-sleep: back from suspend (s2idle) after 15.5 s" "$T/kmsg" &&
    has "hades-sleep: woken by interrupt 9 (acpi)" "$T/kmsg" &&
    has "hades-sleep: sources of wakeups that counted one while it slept: 0000:07:00.3 (USB controller); device:2b (ACPI, for 0000:07:00.3 (USB controller))" "$T/kmsg" &&
    hasnt "PNP0C0C" "$T/kmsg" && hasnt "low-power state for" "$T/kmsg" && [ ! -e "$T/run/hades-sleep" ]; check "6 post" $?
  echo "7. post after a sleep that failed: the step, the device and the error"
  mibix; run "$S" pre suspend
  w "$T/sys/power/suspend_stats/fail" 1; w "$T/sys/power/suspend_stats/last_failed_step" suspend
  w "$T/sys/power/suspend_stats/last_failed_dev" "0000:05:00.0"; w "$T/sys/power/suspend_stats/last_failed_errno" -16
  : > "$T/kmsg"; run "$S" post suspend
  [ $RC = 0 ] && has "back from suspend after 0.0 s: the sleep failed at suspend (0000:05:00.0, error -16)" "$T/kmsg" &&
    has "sources of wakeups that counted one while it slept: none" "$T/kmsg"; check "7 failed" $?
  echo "8. post where the platform measures its low-power state, and the power button woke it"
  modern; run "$S" pre suspend
  w "$T/sys/power/suspend_stats/success" 1; w "$T/sys/power/suspend_stats/last_hw_sleep" 14234567
  w "$T/sys/class/wakeup/wakeup2/wakeup_count" 3
  : > "$T/kmsg"; run "$S" post suspend
  [ $RC = 0 ] && has "back from suspend (deep)" "$T/kmsg" && has "the hardware stayed in a low-power state for 14.2 s" "$T/kmsg" &&
    has "counted one while it slept: PNP0C0C:00 (the power button)" "$T/kmsg"; check "8 hardware sleep, power button" $?
  echo "9. post with nothing noted before, and a /sys with nothing in it: said, and the sleep is not failed"
  mibix; run "$S" post suspend; a=$RC
  has "back from suspend (nothing was noted before it" "$T/kmsg"; b=$?
  chmod -R u+w "$T/sys"; rm -rf "$T/sys" "$T/kmsg"; mkdir -p "$T/sys"; run "$S" pre suspend; c=$RC; run "$S" post suspend
  [ $a = 0 ] && [ $b = 0 ] && [ $c = 0 ] && [ $RC = 0 ] && has "the kernel offers no sleep to RAM here" "$T/kmsg"; check "9 nothing noted, empty /sys" $?
  echo "10. a sleep that the kernel did not count: said"
  mibix; run "$S" pre suspend; : > "$T/kmsg"; run "$S" post suspend
  [ $RC = 0 ] && has "the kernel counted no sleep" "$T/kmsg"; check "10 not counted" $?
}

echo "sleep.sh: $SC"
cases "$SC"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

MUT_MISSED=0
# mutation <n> <what> <sed program>: one rule taken out must fail a check
mutation() {
  local m=$T/mutant-$1
  sed -e "$3" "$SC" > "$m"
  if cmp -s "$m" "$SC"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  cases "$m" > "$T/mutant-$1.log" 2>&1
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "deep is taken as offered everywhere" 's/^    \*) deep=0 ;;$/    *) deep=1 ;;/'
mutation 2 "the wrong bit of the FADT" 's|flags / 2097152 % 2|flags / 1048576 % 2|'
mutation 3 "hubs are not left out" 's/^    \[ "\${c}" = 09 \] \&\& continue$/    :/'
mutation 4 "every HID device is taken for an input device" 's|ls -d "\${d}"/\*:\*/0003:\*/input > /dev/null 2>\&1 \&\& could=|could=|'
mutation 5 "every source of wakeups is taken as one that counted" 's/\[ "\${c}" -gt "\${old:-0}" \] 2>\/dev\/null \&\& //'
mutation 6 "the note is left in /run" 's/^  rm -rf "\${NOTE}"$/  :/'
mutation 7 "a failed sleep is said as one that went" 's/\[ "\${f1}" -gt "\${f0}" \] 2>\/dev\/null; then$/false; then/'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "SLEEP-REPORT PASSED"; exit 0; }
echo "SLEEP-REPORT FAILED: $TREE_FAILS check(s) of the script failed, $MUT_MISSED mutation(s) not caught"
exit 1
