#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of not-returned.awk (packages/sysutils/hades-diag/scripts) and of not_returned() of collect.sh, which writes
# previous-boot/not-returned.txt: the drivers that a journal, or a copy of the kernel log, shows starting under
# initcall_debug and not returned by its last line. The logs here are made up after the journal of issue 14 (a start
# that hung while snd_virtuoso, snd_usb_audio, nvidia and cfg80211 started), in the two forms the diagnostics read: the
# journal ("<time> <host> kernel: ...") and busybox dmesg ("[    5.983000] ..."). The busybox of a system runs both.
#
# Usage: not_returned_test.sh <busybox> <not-returned.awk> <collect.sh>
# Exit 0 when every check passes, 1 when one fails, 2 when the test could not run.
set -u
BBI=${1:-}; AWK=${2:-}; C=${3:-}
[ -x "$BBI" ] && [ -f "$AWK" ] && [ -f "$C" ] || { echo "usage: $0 <busybox> <not-returned.awk> <collect.sh>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
ok=0; bad=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
run() { env -i PATH="$T/bb" "$T/bb/awk" -v what="$1" -f "$AWK" "$2" 2>&1; }

# 1. a journal of a start that hung, as in issue 14
J=$T/journal.txt
k() { echo "Nov 20 14:30:$1 [hostname] kernel: $2"; }
{
  k 26 "Linux version 7.3.5 (builder@hades) #1 SMP"
  k 26 "calling  con_init+0x0/0x230 @ 0"
  k 26 "initcall con_init+0x0/0x230 returned 0 after 0 usecs"
  k 26 "calling  [redacted-long]/0x20 @ 1"
  k 26 "initcall [redacted-long]/0x20 returned 0 after 0 usecs"
  k 26 "pci 0000:01:00.0: calling  quirk_nvidia_hda+0x0/0x70 @ 1"
  k 26 "pci 0000:05:00.0: calling  quirk_disable_aspm_l0s_l1+0x0/0x20 @ 1"
  k 26 "pci 0000:05:00.0: ASPM: Link Capabilities L0s L1 treated as unsupported to avoid device defect"
  k 27 "calling  hid_init+0x0/0x60 [hid] @ 700"
  k 27 "hid: raw HID events driver (C) Jiri Kosina"
  k 27 "initcall hid_init+0x0/0x60 [hid] returned 0 after 120 usecs"
  k 28 "calling  xonar_driver_init+0x0/0xff0 [snd_virtuoso] @ 769"
  k 28 "snd_virtuoso 0000:06:04.0: enabling device (0000 -> 0001)"
  k 28 "calling  usb_audio_driver_init+0x0/0xff0 [snd_usb_audio] @ 779"
  k 28 "probe of 1-3.3.1.5 with driver usb returned 0 after 34271 usecs"
  k 28 "calling  snd_hda_intel_init+0x0/0xff0 [snd_hda_intel] @ 790"
  k 28 "snd_hda_intel 0000:01:00.1: enabling device (0000 -> 0002)"
  k 28 "initcall snd_hda_intel_init+0x0/0xff0 [snd_hda_intel] returned 0 after 900 usecs"
  k 28 "calling  nvidia_init_module+0x0/0x7b0 [nvidia] @ 777"
  k 28 "nvidia: loading out-of-tree module taints kernel."
  k 28 "nvidia 0000:01:00.0: vgaarb: VGA decodes changed: olddecodes=io+mem,decodes=none:owns=none"
  k 28 "calling  cfg80211_init+0x0/0xe0 [cfg80211] @ 1057"
  echo "Nov 20 14:30:28 [hostname] nvidia-drm-wait: waiting for the DRM device of 0000:01:00.0: nvidia being started"
} > "$J"
run "the journal of that start" "$J" > "$T/out1"
check "the summary names the four drivers in the order they started, and not the PCI fixups" "$(head -n 1 "$T/out1")" \
  "4 of the 8 initcalls in the journal of that start started and had not returned by its last line (line 23): xonar_driver_init [snd_virtuoso], usb_audio_driver_init [snd_usb_audio], nvidia_init_module [nvidia], cfg80211_init [cfg80211]."
check "each one has its calling line" "$(grep -c '^  called at line ' "$T/out1")" 4
check "how far snd_virtuoso got: its last line" \
  "$(grep -A2 '^xonar_driver_init \[snd_virtuoso\]$' "$T/out1" | sed -n 3p)" \
  "  the last line of snd_virtuoso after it, line 13: Nov 20 14:30:28 [hostname] kernel: snd_virtuoso 0000:06:04.0: enabling device (0000 -> 0001)"
check "how far nvidia got: its last kernel line, not the line of nvidia-drm-wait" \
  "$(grep -A2 '^nvidia_init_module \[nvidia\]$' "$T/out1" | sed -n 3p)" \
  "  the last line of nvidia after it, line 21: Nov 20 14:30:28 [hostname] kernel: nvidia 0000:01:00.0: vgaarb: VGA decodes changed: olddecodes=io+mem,decodes=none:owns=none"
check "a driver that wrote nothing after it says so" "$(grep -A2 '^cfg80211_init \[cfg80211\]$' "$T/out1" | sed -n 3p)" "  no line of cfg80211 after it"
check "a driver that returned is not listed" "$(grep -c 'snd_hda_intel\|hid_init\|con_init' "$T/out1")" 0
check "the last line of the log" "$(tail -n 1 "$T/out1" | cut -c1-58)" "The last line, 23: Nov 20 14:30:28 [hostname] nvidia-drm-w"

# 2. a copy of the kernel log (dmesg) of a start where everything returned
D=$T/dmesg.txt
{
  echo "start: boot ID 0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d (made up)"
  echo "[    0.010000] calling  con_init+0x0/0x230 @ 0"
  echo "[    0.010001] initcall con_init+0x0/0x230 returned 0 after 0 usecs"
  echo "[    0.352485] pci 0000:00:18.7: calling  quirk_mmio_always_on+0x0/0x20 @ 1"
  echo "[    5.983000] calling  xonar_driver_init+0x0/0xff0 [snd_virtuoso] @ 769"
  echo "[    5.983100] snd_virtuoso 0000:06:04.0: enabling device (0000 -> 0001)"
  echo "[    7.291000] probe of 0000:06:04.0 with driver snd_virtuoso returned 0 after 1307390 usecs"
  echo "[    7.291010] initcall xonar_driver_init+0x0/0xff0 [snd_virtuoso] returned 0 after 1307400 usecs"
} > "$D"
check "a start where every driver returned says so in one line" "$(run "the kernel log of that start" "$D")" \
  "Every driver that the kernel log of that start shows starting had returned by its last line (2 initcalls, 8 lines)."

# 3. a log without initcall_debug: nothing
printf '%s\n' "Nov 20 14:30:26 [hostname] kernel: Linux version 7.3.5" "Nov 20 14:30:26 [hostname] kernel: pci 0000:00:00.0: [1022:14d8] type 00" > "$T/plain.txt"
check "without initcall_debug nothing is written" "$(run x "$T/plain.txt" | wc -c)" 0

# 4. a start called twice: once returned, once not (a module loaded again) is listed once, from its second call
{
  k 30 "calling  foo_init+0x0/0x10 [foo] @ 10"
  k 30 "initcall foo_init+0x0/0x10 [foo] returned -19 after 3 usecs"
  k 31 "calling  foo_init+0x0/0x10 [foo] @ 11"
  k 31 "foo 0000:02:00.0: waiting for its firmware"
} > "$T/twice.txt"
run "the journal of that start" "$T/twice.txt" > "$T/out4"
check "a start called again and not returned: listed once, at its second call" \
  "$(head -n 1 "$T/out4")/$(grep -c '^foo_init \[foo\]$' "$T/out4")/$(grep '^  called at line' "$T/out4" | cut -c1-19)" \
  "1 of the 2 initcalls in the journal of that start started and had not returned by its last line (line 4): foo_init [foo]./1/  called at line 3:"

# 5. more than six: the summary names six and counts the rest
for n in 1 2 3 4 5 6 7 8; do k 40 "calling  m${n}_init+0x0/0x10 [m$n] @ $n"; done > "$T/many.txt"
check "the summary names six and counts the rest" "$(run "the journal of that start" "$T/many.txt" | head -n 1 | sed 's/.*: //')" \
  "m1_init [m1], m2_init [m2], m3_init [m3], m4_init [m4], m5_init [m5], m6_init [m6], and 2 more."

# 6. not_returned() of collect.sh: the file, its first line to the kernel log, no file without initcall_debug
F=$T/f.sh
sed -n '/^not_returned() {$/,/^}$/p' "$C" > "$F"
check "collect.sh has not_returned()" "$(grep -c '^not_returned() {$' "$F")" 1
check "collect.sh writes previous-boot/not-returned.txt from the journal and early-not-returned.txt from the copy" \
  "$(grep -c 'not_returned "the journal of that start" \$T/previous.clean \$P/not-returned.txt' "$C")/$(grep -c 'not_returned "the kernel log of that start" \$T/early.clean \$1/early-not-returned.txt' "$C")" "1/1"
sed -i -e "s#/usr/lib/hades-diag/not-returned.awk#$AWK#" -e "s#/dev/kmsg#$T/kmsg#" "$F"
{
  echo 'lim() { "$@" 2>&1; }'
  cat "$F"
  echo "not_returned \"the journal of that start\" $J $T/nr.txt"
  echo "not_returned x $T/plain.txt $T/none.txt"
} > "$T/s.sh"
env -i PATH="$T/bb" "$T/bb/sh" "$T/s.sh" > "$T/sh.out" 2>&1
check "the file is the summary and the drivers" "$(head -n 1 "$T/nr.txt" 2>/dev/null | cut -c1-40)" "4 of the 8 initcalls in the journal of t"
check "its first line goes to the kernel log" "$(cat "$T/kmsg" 2>/dev/null)" "hades-diag: previous-boot: $(head -n 1 "$T/out1")"
check "without initcall_debug no file" "$([ -e "$T/none.txt" ] && echo there || echo none)" none
check "the shell said nothing else" "$(cat "$T/sh.out")" ""

echo "not-returned: $ok checks passed, $bad failed"
[ $bad = 0 ]
