#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of the theme of the GRUB menu in an update in place (projects/Generic/filesystem/usr/share/bootloader/update.sh,
# theme_update). update.sh runs whole, as the init of an update runs it: through the busybox of an initramfs, with the
# global sync, mount and (for one case) df as shell functions. The boot partition is a folder with the two menus and
# the theme of before; the system of the update has the theme of the tree (or none).
#
# Usage: theme_update_test.sh <busybox> <update.sh> <theme of the system> <theme of before> <grub.cfg> <syslinux.cfg>
#   <busybox>          the busybox of an initramfs (build.*/initramfs/usr/bin/busybox)
#   <theme ...>        folders as packages/tools/grub/theme/hades (of the tree, and of the version updated from)
#   <grub.cfg>, <syslinux.cfg>  the menus of an image (scripts/mkimage writes them; mcopy them from its first partition)
# With the update.sh of before 1.0 + 1 (no theme_update) 7 checks fail. Exit 0 when every check passes, 1 when one
# fails, 2 when the test could not run.
set -u
export LC_ALL=C
BBI=${1:-}; UPD=${2:-}; NEW=${3:-}; OLD=${4:-}; GRUBCFG=${5:-}; SYSCFG=${6:-}
[ -x "$BBI" ] && [ -f "$UPD" ] && [ -f "$NEW/theme.cfg" ] && [ -f "$OLD/theme.cfg" ] && [ -f "$GRUBCFG" ] && [ -f "$SYSCFG" ] ||
  { echo "usage: $0 <busybox> <update.sh> <theme of the system> <theme of before> <grub.cfg> <syslinux.cfg>"; exit 2; }
T=$(mktemp -d)
trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $("$BBI" --list); do ln -s "$BBI" "$T/bb/$a"; done
cat > "$T/shim.sh" <<EOF
sync() { if [ \$# -eq 0 ]; then echo "global sync (simulated)" >> "\$HT/sync.log"; return 0; fi; $BBI sync "\$@"; }
mount() { echo "mount \$*" >> "\$HT/mount.log"; return 0; }
df() { if [ -n "\$HT_DF" ]; then printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n/dev/sda1 524288 524000 %s 99%% /flash\n' "\$HT_DF"; else $BBI df "\$@"; fi; }
EOF
ok=0; bad=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
CASE=""
setup() { # name, theme on the boot partition (folder or "none"), theme of the system (folder or "none"), GRUB menu (yes/no)
  CASE=$T/c/$1; rm -rf "$CASE"; mkdir -p "$CASE/boot/EFI/BOOT" "$CASE/sys/usr/share/grub" "$CASE/sys/etc" "$CASE/work"
  cp "$SYSCFG" "$CASE/boot/syslinux.cfg"
  [ "$4" = yes ] && cp "$GRUBCFG" "$CASE/boot/EFI/BOOT/grub.cfg"
  [ "$2" != none ] && cp -r "$2" "$CASE/boot/EFI/BOOT/hades"
  [ "$3" != none ] && cp -r "$3" "$CASE/sys/usr/share/grub/hades"
  printf 'NAME="LibreELEC"\nVERSION="13.0-devel"\n' > "$CASE/sys/etc/os-release"
  # (boot= of this start: the one that the menu names)
  echo "BOOT_IMAGE=/KERNEL $(grep -o 'boot=UUID=[0-9A-Fa-f-]*' "$GRUBCFG" | head -1) quiet" > "$CASE/cmdline"
  : > "$CASE/mounts"
}
update() { # [free KiB that df says]
  sed -e "s#^CMDLINE=/proc/cmdline#CMDLINE=$CASE/cmdline#" -e "s#^MOUNTS=/proc/mounts#MOUNTS=$CASE/mounts#" \
      -e "s#^KMSG=/dev/kmsg#KMSG=$CASE/kmsg#" -e "s#^WORK=/tmp/hades-update-menu#WORK=$CASE/work/u#" -e "1a . $T/shim.sh" \
      -e 's/ >\$KMSG / >>$KMSG /' "$UPD" > "$CASE/update.sh"
  timeout 120 env -i PATH="$T/bb" HT="$CASE" HT_DF="${1:-}" BOOT_ROOT="$CASE/boot" SYSTEM_ROOT="$CASE/sys" \
    "$T/bb/sh" "$CASE/update.sh" > "$CASE/update.out" 2>&1
  echo "exit $?" >> "$CASE/update.out"
}
said() { grep -c "$1" "$CASE/update.out"; }
as_theme() { # every file of the theme $1 is on the boot partition with the same bytes: "same", or the first that is not
  local f
  for f in $(cd "$1" && find . -type f | sort); do
    cmp -s "$1/$f" "$CASE/boot/EFI/BOOT/hades/$f" || { echo "differs: $f"; return; }
  done
  echo same
}
nfiles() { (cd "$CASE/boot/EFI/BOOT/hades" 2>/dev/null && find . -type f | wc -l) || echo 0; }
leftovers() { (cd "$CASE/boot/EFI/BOOT" && find . -name "*.new" | wc -l); }
# (the update gives the menus the block of one start, once_update: taken out, they are as they were)
noonce() { awk '/^# hades-once-begin/ { skip = 1; next } /^# hades-once-end/ { skip = 0; next } !skip' "$1"; }
changed=$(cd "$NEW" && for f in $(find . -type f | sort); do cmp -s "$f" "$OLD/$f" || echo x; done | wc -l)
all=$(cd "$NEW" && find . -type f | wc -l)
echo "files of the theme: $all; new or changed against the theme of before: $changed"

setup first "$OLD" "$NEW" yes; update
check "an update over the theme of before ends with 0" "$(tail -1 "$CASE/update.out")" "exit 0"
check "it writes the files that are new or changed, and says so" "$(said "the theme of the boot menu is now the one of this system ($changed file(s) written)")" 1
check "the theme on the boot partition is the new one, file by file" "$(as_theme "$NEW")" same
check "no .new file is left" "$(leftovers)" 0
check "the menus are as they were but for the block of one start (grub.cfg, syslinux.cfg)" \
  "$(noonce "$CASE/boot/EFI/BOOT/grub.cfg" | cmp -s - "$GRUBCFG" && noonce "$CASE/boot/syslinux.cfg" | cmp -s - "$SYSCFG" && echo same)" same
update
check "a second update writes nothing and says nothing of the theme" "$(said "theme of the boot menu")/$(tail -1 "$CASE/update.out")/$(as_theme "$NEW")" "0/exit 0/same"

setup nosystheme "$OLD" none yes; update
check "an update to a system without the theme leaves the theme of before and says nothing of it" \
  "$(said "theme of the boot menu")/$(tail -1 "$CASE/update.out")/$(as_theme "$OLD")" "0/exit 0/same"

setup nomenu "$OLD" "$NEW" no; update
check "a boot partition without the GRUB menu (BIOS only) gets no theme" "$(said "theme of the boot menu")/$(tail -1 "$CASE/update.out")/$(as_theme "$OLD")" "0/exit 0/same"

setup notheme none "$NEW" yes; update
check "a boot partition without a theme (installed before it) gets the whole theme" \
  "$(tail -1 "$CASE/update.out")/$(as_theme "$NEW")/$(nfiles)" "exit 0/same/$all"

setup noroom "$OLD" "$NEW" yes; update 100
check "with no room on the partition nothing is written, and that is said" \
  "$(said "has no room for the new one")/$(tail -1 "$CASE/update.out")/$(as_theme "$OLD")/$(leftovers)" "1/exit 0/same/0"

setup readonly "$OLD" "$NEW" yes; chmod a-w "$CASE/boot/EFI/BOOT/hades/1920x1080"; update
check "a file that cannot be written keeps the theme.cfg of before, and that is said" \
  "$(said "keeps its theme.cfg of before")/$(tail -1 "$CASE/update.out")/$(cmp -s "$OLD/theme.cfg" "$CASE/boot/EFI/BOOT/hades/theme.cfg" && echo old)/$(leftovers)" "1/exit 0/old/0"
chmod u+w "$CASE/boot/EFI/BOOT/hades/1920x1080"; update
check "the next update writes what was missing" "$(said "is now the one of this system")/$(as_theme "$NEW")" "1/same"

echo "theme-update: $ok checks passed, $bad failed"
[ $bad = 0 ]
