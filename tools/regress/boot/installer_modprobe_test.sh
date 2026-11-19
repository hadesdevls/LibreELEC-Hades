#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of what the init of the initramfs (packages/sysutils/busybox/scripts/init) leaves for modprobe in a start
# of the boot entry "Installer": /run/modprobe.d/hades-installer.conf. The block that writes it (from the line
# 'if [ "$HADES_INSTALLER" = "yes" ]; then' to its 'fi') is taken from the init and run by the busybox of an
# initramfs, with /sysroot moved into a folder, once as the installer and once as any other entry. The Bluetooth
# drivers that it has to leave out are read from the modules of an image: each module of kernel/drivers/bluetooth
# that modules.alias names for a bus (usb, sdio, pci, acpi, of...). Then the modprobe of this host (kmod), with the
# modules of the image, is asked what it would load for the Bluetooth controller of an MT7927 (USB 0489:e13a) with
# the blacklist applied, as udev asks it; and, to know that the question is a real one, without the file.
#
# Usage: installer_modprobe_test.sh <busybox> <init> <modules root> <kernel version>
#   <busybox>       the busybox of an initramfs (build.*/initramfs/usr/bin/busybox)
#   <init>          packages/sysutils/busybox/scripts/init
#   <modules root>  the folder that holds lib/modules/<kernel version> of an image (usr/lib/kernel-overlays/base of
#                   its system)
# With the init of before (NVIDIA only) 3 checks fail. Exit 0 when every check passes, 1 when one fails, 2 when the
# test could not run.
set -u
export LC_ALL=C
BBI=${1:-}; INIT=${2:-}; MR=${3:-}; KV=${4:-}
MD=$MR/lib/modules/$KV
[ -x "$BBI" ] && [ -f "$INIT" ] && [ -f "$MD/modules.dep" ] && [ -f "$MD/modules.alias" ] ||
  { echo "usage: $0 <busybox> <init> <modules root> <kernel version>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb" "$T/empty"
for a in $("$BBI" --list); do ln -s "$BBI" "$T/bb/$a"; done
# the block of the init, with /sysroot in the folder
awk '/^if \[ "\$HADES_INSTALLER" = "yes" \]; then$/ {on = 1} on {print} on && /^fi$/ {exit}' "$INIT" > "$T/block.raw"
[ "$(tail -n 1 "$T/block.raw")" = "fi" ] || { echo "the block of the installer is not in $INIT"; exit 2; }
sed -e "s#/usr/bin/busybox #$BBI #g" -e "s#/sysroot/#$T/sysroot/#g" "$T/block.raw" > "$T/block.sh"
ok=0; bad=0; notrun=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
run() {
  rm -rf -- "$T/sysroot"; mkdir -p "$T/sysroot/dev"; : > "$T/sysroot/dev/kmsg"
  env -i PATH="$T/bb" HADES_INSTALLER="$1" "$T/bb/sh" "$T/block.sh" > "$T/out" 2>&1
}
CONF=$T/sysroot/run/modprobe.d/hades-installer.conf
# the Bluetooth drivers of the image that load by an alias of a bus
grep -o '^kernel/drivers/bluetooth/[^:]*' "$MD/modules.dep" | sed -e 's#.*/##' -e 's#\.ko.*##' -e 's#-#_#g' | sort -u > "$T/bt"
awk '$1 == "alias" && $2 ~ /^(usb|sdio|pci|of|platform|i2c|spi|serio|acpi[*]?):/ {print $NF}' "$MD/modules.alias" | sort -u > "$T/bus"
comm -12 "$T/bt" "$T/bus" > "$T/bt.bus"
[ -s "$T/bt.bus" ] || { echo "no Bluetooth driver with a bus alias in $MD"; exit 2; }
echo "Bluetooth drivers of the image with a bus alias: $(paste -sd' ' "$T/bt.bus")"

run yes
check "the installer start writes the file" "$([ -f "$CONF" ] && echo yes || echo no)" yes
check "it still leaves out the four modules of NVIDIA" \
  "$(grep -c -x -E 'blacklist (nvidia|nvidia_modeset|nvidia_drm|nvidia_uvm)' "$CONF" 2>/dev/null)" 4
missing=$(while read -r m; do grep -q -x "blacklist $m" "$CONF" 2>/dev/null || echo "$m"; done < "$T/bt.bus" | paste -sd' ' -)
check "it leaves out each Bluetooth driver of the image that loads by a bus alias ($(wc -l < "$T/bt.bus"))" "${missing:-none missing}" "none missing"
check "the kernel log says it" "$(grep -c '^hades-installer: no NVIDIA and no Bluetooth driver loads by its alias in the installer' "$T/sysroot/dev/kmsg")" 1
check "the block says nothing else" "$(cat "$T/out")" ""
MP=$(command -v modprobe || command -v /usr/sbin/modprobe || command -v /sbin/modprobe || true)
if [ -n "$MP" ] && "$MP" --version 2>/dev/null | grep -q '^kmod'; then
  MA=usb:v0489pE13Ad0100dcE0dsc01dp01icE0isc01ip01in00
  mkdir -p "$T/conf.d"; cp "$CONF" "$T/conf.d/" 2>/dev/null
  with=$("$MP" -b -n -v -d "$MR" -S "$KV" -C "$T/conf.d" "$MA" 2>&1)
  without=$("$MP" -b -n -v -d "$MR" -S "$KV" -C "$T/empty" "$MA" 2>&1)
  # (kmod lists the modules once for each alias that the device matches: twice for this one)
  check "with the file, modprobe as udev asks it loads no btusb for an MT7927 controller" \
    "$(echo "$with" | grep -q '^insmod .*/btusb\.ko' && echo loads || echo no)" no
  check "without it, the same question loads btusb (so the question is a real one)" \
    "$(echo "$without" | grep -q '^insmod .*/btusb\.ko' && echo loads || echo no)" loads
else
  notrun=$((notrun + 2)); echo "not run: the two checks of modprobe (no kmod modprobe on this host)"
fi

run ""
check "another entry writes no file" "$([ -e "$T/sysroot/run/modprobe.d" ] && echo yes || echo no)" no
check "and says nothing in the kernel log" "$(wc -c < "$T/sysroot/dev/kmsg")" 0

echo "passed: $ok, failed: $bad, not run: $notrun"
[ "$bad" -eq 0 ] || exit 1
[ "$notrun" -eq 0 ] || echo "PASSED, BUT $notrun CHECK(S) NOT RUN"
exit 0
