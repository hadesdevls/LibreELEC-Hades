#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: the kernel line of the boot menus follows their switch. While the switch is on (a file whose name
# starts with DIAG-ON at the top of the boot partition), every entry of the boot menus starts the kernel with
# initcall_debug=1; with it off, initcall_debug=0, which is the kernel's own default (scripts/mkimage writes 0). With 1
# the kernel writes a line when it calls the start of each driver, built in or loaded later by udev, and one when that
# returns ("calling <driver> @ <pid>", "initcall <driver> returned <status> after <time> usecs"), at the level of
# debugging messages: they go to the journal and not to the screen. A start that hangs while drivers start then leaves,
# in the journal that the diagnostics keep for the next start (previous-boot), the driver that was called and did not
# return. It has to be on the kernel line: this kernel has tracepoints, and it reads the parameter once, early in its
# start (init/main.c, initcall_debug_enable()); setting it later does nothing. With it goes log_buf_len=1M (off:
# log_buf_len=0M, which leaves the kernel its own size), so that those lines, about 120 KB measured in a virtual machine,
# do not push the first lines of the start out of the kernel's buffer (128 KiB by default) before the journal has them.
#
# The change is made by internal-disks ("option initcall_debug on|off"): in both menus or in none, under the lock of the
# writers of the boot partition, each menu written beside the old one, through to the device and read back before it
# takes its place; a menu that this system does not make is never written to. It holds from the next start.
#
# Usage: kernel-line.sh        run by switch.sh after it has turned the switch, and by hades-diag-kernel-line.service
#                              at every start (a switch turned by hand, in Windows for example, has its kernel line
#                              from the start after that one)
# What it did, or why not, goes to the kernel log and to its standard error, on lines that start with
# "hades-diag: the kernel line"; nothing is written where the menus say what the switch says already.
# Exit status: 0 the menus say what the switch says (or said so already); otherwise the status of internal-disks (10
# no menu, 11 a menu that this system does not make, 14 the partition was busy, 20 to 22 the change could not be made).

PATH=/usr/bin:/usr/sbin
export PATH
LC_ALL=C
export LC_ALL
FLASH=/flash
ID=/usr/lib/libreelec/internal-disks
OPTION=initcall_debug
KMSG=/dev/kmsg

say() {
  echo "hades-diag: the kernel line of the boot menus: $1" >&2
  { echo "hades-diag: the kernel line of the boot menus: $1" > $KMSG; } 2>/dev/null
}

want=off
ls $FLASH 2>/dev/null | grep -qi '^diag-on' && want=on
[ -x $ID ] || { say "internal-disks is not there: initcall_debug is left as the menus have it"; exit 1; }
now=$($ID option $OPTION 2>/dev/null)
case "$now" in
  "$want")
    exit 0 ;;
  unavailable*)
    say "it cannot be told from the menus (${now#unavailable }): initcall_debug is left as they have it"
    exit 11 ;;
esac
out=$($ID option $OPTION $want 2>&1)
st=$?
if [ $st -eq 0 ]; then
  if [ $want = on ]; then
    say "the diagnostics are on: from the next start the kernel notes each driver that it starts (initcall_debug=1 log_buf_len=1M)"
  else
    say "the diagnostics are off: from the next start the kernel does not note each driver that it starts (initcall_debug=0 log_buf_len=0M)"
  fi
else
  say "initcall_debug could not be set to $([ $want = on ] && echo 1 || echo 0) (internal-disks: status $st, $(echo "$out" | tr '\n' ' ')): the next start tries again"
fi
exit $st
