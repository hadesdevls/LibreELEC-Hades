#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades

# Hades: what an update in place changes on the boot partition besides KERNEL and SYSTEM. The init of the
# system that applies the update runs this from the system of the update (update_bootloader in its init, with
# BOOT_ROOT, the boot partition, and SYSTEM_ROOT, the new system, mounted), right after it has written KERNEL
# and SYSTEM, and at no other time: never at an ordinary start. That init made the boot partition writable
# only while it wrote each file (update_file), so this makes it writable for its own write in the same way,
# and read-only again afterwards (unless it was writable before: a system in a file, whose boot partition
# stays writable).
#
# The GRUB menu (UEFI). Before 1.0 RC7 each entry started the first /KERNEL on any disk ("search --set -f
# /KERNEL"), and with a second drive that has another build on it GRUB could start that drive's kernel with
# the SYSTEM of this one, which has no modules for it (issue #14). scripts/mkimage writes two lines in its
# place since then, which start the kernel of the partition that the entry's boot= names. An update in place
# does not write the menus, so a menu of before gets those two lines here, once:
#   - only a menu that is, line by line, one of Hades of before: its first line "# hades-features:", the
#     entries Run, "Run (internal disks read-only)", Live and Installer, each with one line
#     "<tab>search --set -f /KERNEL" and after it one line "<tab>linux /KERNEL ..." with one boot=UUID= of a
#     FAT file system, the same in the four and the same as boot= of this start; every line of a kind that
#     scripts/mkimage writes, every line ending the same way (LF, or CR LF), and a line end at the end. With
#     any doubt the menu is left as it is, and that is said;
#   - in each entry that line becomes the two lines of scripts/mkimage, with the UUID of that entry's boot=,
#     and the line end of the file; every other byte stays as it is (the default, and with it the setting of
#     the internal disks, the comments, the line ends);
#   - what comes out is checked before it is written: the four pairs, none of the line of before, and, with
#     each pair put back as the line of before, the menu that was there, byte for byte;
#   - the menu as it was is kept as EFI/BOOT/grub.cfg.old. The new one is written as EFI/BOOT/grub.cfg.new,
#     written through to the device (fsync) and read back, and then renamed over grub.cfg: a power cut leaves
#     the menu of before or the new one;
#   - a menu that has the new lines already is left as it is: a second update does nothing;
#   - syslinux.cfg (BIOS) is not touched: syslinux loads the kernel from its own partition.
# Then the entries that lock the kernel down get the words that ask for DMA translation, in both menus (dma_update
# below), as scripts/mkimage writes them since 1.0.
# Last, the menus (GRUB and syslinux) are given for the one start that follows the update a block that makes them
# start the system at once, so that the update does not show the boot menu twice (once_update below); the
# system takes the block out again at its next start (usr/lib/libreelec/hades-menu-once).
# Before that last step, menus of before 1.0 get their wait written as 1.0 writes it, once: the 10 s that they
# had by themselves become the 5 s of 1.0, and any other wait is kept (wait_update below).
# What was done is said on the console of the update and in the kernel log, on lines that start with
# "hades-update:". The exit status is always 0: nothing here stops the update.
# (Run by the busybox of the initramfs of the system that applies the update: no cmp, wc, cut or chmod.)

[ -z "$BOOT_ROOT" ] && BOOT_ROOT="/flash"
MENU="$BOOT_ROOT/EFI/BOOT/grub.cfg"
MOUNTS=/proc/mounts
CMDLINE=/proc/cmdline
KMSG=/dev/kmsg
WORK=/tmp/hades-update-menu

say() {
  echo "hades-update: $1"
  echo "hades-update: $1" >$KMSG 2>/dev/null
}

# the two lines of scripts/mkimage that take the place of the line of before
NEW_SEARCH='	search --no-floppy --fs-uuid --set=root --hint="${root}" '
NEW_FALLBACK='	if [ ! -f /KERNEL ]; then search --set -f /KERNEL; fi'
OLD_SEARCH='	search --set -f /KERNEL'

# What the menu $1 is: "old <uuid> <cr>" (a menu of before that can be given the new lines: the UUID of its
# boot=, and 1 when its lines end with CR LF), "new" (it has them already), or "unknown: <why>"
LOOK='
function bad(why) { if (verdict == "") verdict = "unknown: " why }
{
  line = $0
  c = sub(/\r$/, "", line)
  if (NR == 1) cr = c
  else if (c != cr) bad("its lines do not all end the same way")
  if (index(line, "\r")) bad("a carriage return inside line " NR)
  if (index(line, "fs-uuid") || index(line, "--hint")) has_new = 1
  if (NR == 1) { if (line !~ /^# hades-features:/) bad("it is not a menu of Hades (its first line)"); next }
  if (line ~ /^#/ || line ~ /^[ \t]*$/ || line ~ /^set timeout="[0-9]+"$/ || line ~ /^set default="[^"]+"$/) next
  if (cur == "" && line == "if [ -f \"${prefix}/hades/theme.cfg\" ]; then source \"${prefix}/hades/theme.cfg\"; fi") next
  if (cur == "" && line ~ /^menuentry "[^"]+"( --class [a-z0-9-]+)? [{]$/) {
    cur = line; sub(/^menuentry "/, "", cur); sub(/".*$/, "", cur)
    n[cur]++; entries++; s[cur] = 0; k[cur] = 0
    next
  }
  if (cur != "" && line == "\tterminal_output console") next
  if (cur != "" && line == "\tsearch --set -f /KERNEL") {
    if (s[cur] || k[cur]) bad("an entry looks for its kernel twice, or after it starts it")
    s[cur] = 1; old_seen = 1; next
  }
  if (cur != "" && line ~ /^\tlinux \/KERNEL [^ \t]/) {
    if (!s[cur] || k[cur]) bad("an entry starts a kernel that it did not look for, or two")
    k[cur] = 1
    m = split(line, w, " ")
    nb = 0
    for (i = 1; i <= m; i++) if (w[i] ~ /^boot=/) { nb++; b = w[i] }
    if (nb != 1) bad("an entry does not name one boot partition")
    else if (b !~ /^boot=UUID=[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]$/) bad("an entry names its boot partition in a way that is not known")
    else if (uuid == "") uuid = substr(b, 11)
    else if (toupper(uuid) != toupper(substr(b, 11))) bad("the entries name different boot partitions")
    next
  }
  if (cur != "" && line == "}") {
    if (!s[cur] || !k[cur]) bad("an entry without the line of before or without its kernel")
    cur = ""; next
  }
  if (!has_new) bad("line " NR " is of no known kind")
}
END {
  if (NR == 0) bad("it is empty")
  if (has_new) { print (old_seen ? "unknown: it has the line of before and the new ones" : "new"); exit }
  if (cur != "") bad("an entry does not end")
  if (entries != 4 || n["Run"] != 1 || n["Run (internal disks read-only)"] != 1 || n["Live"] != 1 || n["Installer"] != 1) bad("its entries are not the four of Hades")
  if (verdict == "" && uuid == "") bad("no boot partition")
  if (verdict != "") print verdict
  else print "old " uuid " " cr
}
'

# The new menu ($1: the menu; uuid, cr): each line of before in the place of the two new lines; every other
# record as it is
MAKE='
{
  line = $0
  sub(/\r$/, "", line)
  if (line == "\tsearch --set -f /KERNEL") { print s uuid e; print f e; next }
  print
}
'
# ... and back: each pair of new lines in the place of the line of before
BACK='
{
  line = $0
  sub(/\r$/, "", line)
  if (line == s uuid) {
    if ((getline nl) <= 0) { print; next }
    t = nl
    sub(/\r$/, "", t)
    if (t == f) { print o e; next }
    print
    print nl
    next
  }
  print
}
'

same() {
  local a b
  a=$(md5sum <"$1" 2>/dev/null) || return 1
  b=$(md5sum <"$2" 2>/dev/null) || return 1
  [ -n "$a" ] && [ "$a" = "$b" ]
}

# How the boot partition is mounted: ro, rw or none
flash_is() {
  awk -v m="$BOOT_ROOT" '$2 == m { o = "," $4 "," } END { if (o == "") print "none"; else if (o ~ /,ro,/) print "ro"; else print "rw" }' "$MOUNTS" 2>/dev/null
}

# The programs of GRUB (UEFI), EFI/BOOT/bootx64.efi and bootia32.efi. An update in place did not write them
# before 1.0 RC8, so a drive installed before 1.0 RC6 kept a GRUB without the command "[" (its module test),
# which the menu uses since 1.0 RC7: each start said "can't find command '['" and waited for a key (issue
# #17). Each one that is on the boot partition and is not the one of the new system becomes that one, as the
# menu does: written as <name>.new, written through to the device and read back, and then renamed over it, so
# a power cut leaves the one of before or the new one. One that is not there stays away; nothing else on the
# partition is touched. This runs before the menu is changed, so that the menu finds the GRUB it needs.
efi_update() {
  local dir="$BOOT_ROOT/EFI/BOOT" src="$SYSTEM_ROOT/usr/share/grub" name todo="" was
  for name in bootx64.efi bootia32.efi; do
    if [ -f "$dir/$name" ] && [ -f "$src/$name" ] && ! same "$src/$name" "$dir/$name"; then
      todo="$todo $name"
    fi
  done
  [ -z "$todo" ] && return 0

  was=$(flash_is)
  if [ "$was" = ro ] && ! mount -o remount,rw "$BOOT_ROOT"; then
    say "GRUB is left as it is: the boot partition could not be made writable"
    return 0
  fi
  for name in $todo; do
    rm -f "$dir/$name.new"
    if cp "$src/$name" "$dir/$name.new" && sync "$dir/$name.new" && same "$dir/$name.new" "$src/$name" &&
       mv -f "$dir/$name.new" "$dir/$name" && sync "$dir/$name" "$dir" && same "$dir/$name" "$src/$name"; then
      say "GRUB: EFI/BOOT/$name is now the one of this system"
    elif same "$dir/$name" "$src/$name"; then
      say "GRUB: EFI/BOOT/$name is now the one of this system, but it could not all be written through"
    else
      rm -f "$dir/$name.new"
      say "GRUB: EFI/BOOT/$name could not be replaced: it is the one of before"
    fi
  done
  sync
  if [ "$was" = ro ] && ! mount -o remount,ro "$BOOT_ROOT"; then
    say "the boot partition could not be made read-only again"
  fi
  return 0
}

menu_update() {
  local look uuid cr e this was

  if [ ! -f "$MENU" ]; then
    say "no GRUB menu at ${MENU#$BOOT_ROOT/}: nothing to change"
    return 0
  fi
  rm -rf "$WORK"
  mkdir -p "$WORK" || { say "the GRUB menu is left as it is: no room to work in"; return 0; }
  # (a byte that is no text: what reads the menu line by line may end at it)
  if ! tr -d '\000' <"$MENU" >"$WORK/nonul" || ! same "$MENU" "$WORK/nonul"; then
    say "the GRUB menu is left as it is: it is not one of Hades of before (it has a NUL byte)"
    return 0
  fi
  if [ -n "$(tail -c 1 "$MENU" 2>/dev/null)" ]; then
    say "the GRUB menu is left as it is: it is not one of Hades of before (no line end at its end)"
    return 0
  fi
  look=$(awk "$LOOK" "$MENU" 2>/dev/null) || look="unknown: it could not be read"
  case "$look" in
    new)
      say "the GRUB menu starts the kernel of its own partition already: nothing to change"
      return 0
      ;;
    old\ *) ;;
    *)
      say "the GRUB menu is left as it is: it is not one of Hades of before (${look#unknown: })"
      return 0
      ;;
  esac
  set -- $look
  uuid=$2
  cr=$3
  e=""
  [ "$cr" = 1 ] && e=$(printf '\r')
  # the partition of this start is the one that the menu names
  this=$(tr ' ' '\n' <"$CMDLINE" 2>/dev/null | sed -n 's/^boot=UUID=//p' | head -n 1)
  if [ -z "$this" ] || [ "$(echo "$this" | tr a-f A-F)" != "$(echo "$uuid" | tr a-f A-F)" ]; then
    say "the GRUB menu is left as it is: it names the boot partition $uuid, and this system was started from ${this:-another one}"
    return 0
  fi

  awk -v s="$NEW_SEARCH" -v f="$NEW_FALLBACK" -v uuid="$uuid" -v e="$e" "$MAKE" "$MENU" >"$WORK/new" 2>/dev/null &&
    awk -v s="$NEW_SEARCH" -v f="$NEW_FALLBACK" -v o="$OLD_SEARCH" -v uuid="$uuid" -v e="$e" "$BACK" "$WORK/new" >"$WORK/back" 2>/dev/null ||
    { say "the GRUB menu is left as it is: the new one could not be made"; return 0; }
  if [ "$(grep -c -F -x -e "$NEW_SEARCH$uuid$e" "$WORK/new")" != 4 ] ||
     [ "$(grep -c -F -x -e "$NEW_FALLBACK$e" "$WORK/new")" != 4 ] ||
     grep -q -F -x -e "$OLD_SEARCH$e" "$WORK/new" || ! same "$WORK/back" "$MENU"; then
    say "the GRUB menu is left as it is: the new one is not what it should be"
    return 0
  fi

  was=$(flash_is)
  if [ "$was" = ro ] && ! mount -o remount,rw "$BOOT_ROOT"; then
    say "the GRUB menu is left as it is: the boot partition could not be made writable"
    return 0
  fi
  # the menu of before, kept; then the new one next to it, written through and read back, and the rename
  rm -f "$MENU.old.new" "$MENU.new"
  if cp "$MENU" "$MENU.old.new" && sync "$MENU.old.new" && same "$MENU.old.new" "$MENU" &&
     mv -f "$MENU.old.new" "$MENU.old" && sync "$MENU.old" &&
     cp "$WORK/new" "$MENU.new" && sync "$MENU.new" && same "$MENU.new" "$WORK/new" &&
     mv -f "$MENU.new" "$MENU" && sync "$MENU" "${MENU%/*}" && same "$MENU" "$WORK/new"; then
    say "the GRUB menu now starts the kernel of its own partition ($uuid) in its four entries; the one of before is EFI/BOOT/grub.cfg.old"
  elif same "$MENU" "$WORK/new"; then
    say "the GRUB menu now starts the kernel of its own partition ($uuid), but it could not all be written through: look at EFI/BOOT/grub.cfg"
  else
    rm -f "$MENU.old.new" "$MENU.new"
    say "the GRUB menu could not be changed: it is the one of before"
  fi
  sync
  if [ "$was" = ro ] && ! mount -o remount,ro "$BOOT_ROOT"; then
    say "the boot partition could not be made read-only again"
  fi
  rm -rf "$WORK"
  return 0
}

# DMA translation (1.0). The entries that lock the kernel down (Run and Live) ask for it since 1.0 too
# (scripts/mkimage: iommu.passthrough=0 intel_iommu=on,igfx_off), so that a device reaches only the memory that
# the kernel maps for its transfers. An update in place does not write the menus, so the menus of before get
# those words here, once, in GRUB (UEFI) and in syslinux (BIOS) alike:
#   - only a menu of Hades (first line "# hades-features:"), with no NUL byte, a line end at its end and every
#     line ending the same way (LF, or CR LF); with any doubt the menu is left as it is, and that is said;
#   - only the lines that start a kernel (APPEND in syslinux.cfg, linux in grub.cfg) and lock it down
#     (lockdown=integrity): each gets the words at its end, before its line end. Every other byte stays as it
#     is (the default, and with it the setting of the internal disks, the wait, the comments, the line ends);
#   - a menu with such a line that names the IOMMU already (iommu.*, iommu=, intel_iommu=, amd_iommu=), or
#     with some of them asking for it and others not, is left as it is: someone chose for it. A menu whose
#     lines all have the words is left as it is too: a second update does nothing;
#   - what comes out is checked before it is written: the words at the end of as many lines as were found,
#     and, with them taken off again, the menu that was there, byte for byte;
#   - written as the other changes of a menu: next to it, written through to the device and read back, then
#     renamed over it, so a power cut leaves the menu of before or the new one.
# The entry "Run (internal disks read-only)" and the installer start the kernel as LibreELEC does and are not
# touched: the first one is also the way to start a computer whose firmware does not let a device work with
# DMA translation.
DMA_WORDS='iommu.passthrough=0 intel_iommu=on,igfx_off'
# What the menu is (kind: syslinux or grub): "add <n>" (n lines lock the kernel down, and none names the
# IOMMU), "has" (all of them have the words), "chosen" (one of them names the IOMMU in another way), "mixed"
# (some have the words and some do not), "none" (no line locks the kernel down), or "unknown: <why>"
DMA_LOOK='
function bad(why) { if (verdict == "") verdict = "unknown: " why }
function kernel(l) {
  if (kind == "syslinux") return l ~ /^[ \t]+APPEND[ \t]/
  return l ~ /^[ \t]+linux[ \t]/
}
{
  l = $0
  c = sub(/\r$/, "", l)
  if (NR == 1) {
    cr = c
    if (l !~ /^# hades-features:/) bad("it is not a menu of Hades (its first line)")
  } else if (c != cr) bad("its lines do not all end the same way")
  if (index(l, "\r")) bad("a carriage return inside line " NR)
  if (!kernel(l)) next
  n = split(l, w, /[ \t]+/)
  lock = 0
  iommu = 0
  for (i = 1; i <= n; i++) {
    if (w[i] == "lockdown=integrity") lock = 1
    if (w[i] ~ /^(iommu[.=]|intel_iommu=|amd_iommu=)/) iommu = 1
  }
  if (!lock) next
  locks++
  if (substr(l, length(l) - length(words)) == " " words) has++
  else if (iommu) chosen++
}
END {
  if (NR == 0) bad("it is empty")
  if (verdict != "") print verdict
  else if (locks == 0) print "none"
  else if (has == locks) print "has"
  else if (chosen) print "chosen"
  else if (has) print "mixed"
  else print "add " locks
}
'
# The new menu: the words at the end of each line that starts a kernel and locks it down
DMA_ADD='
function kernel(l) {
  if (kind == "syslinux") return l ~ /^[ \t]+APPEND[ \t]/
  return l ~ /^[ \t]+linux[ \t]/
}
{
  l = $0
  c = sub(/\r$/, "", l)
  if (kernel(l)) {
    n = split(l, w, /[ \t]+/)
    for (i = 1; i <= n; i++) if (w[i] == "lockdown=integrity") { print l " " words (c ? "\r" : ""); next }
  }
  print
}
'
# ... and back: the words taken off the end of each line that ends with them
DMA_BACK='
{
  l = $0
  c = sub(/\r$/, "", l)
  if (substr(l, length(l) - length(words)) == " " words) { print substr(l, 1, length(l) - length(words) - 1) (c ? "\r" : ""); next }
  print
}
'

# dma_menu <file> <syslinux or grub>
dma_menu() {
  local f="$1" kind="$2" look n was
  [ -f "$f" ] && [ ! -L "$f" ] || return 0
  rm -rf "$WORK"
  mkdir -p "$WORK" || { say "${f#$BOOT_ROOT/} is left without the words of DMA translation: no room to work in"; return 0; }
  if ! tr -d '\000' <"$f" >"$WORK/nonul" || ! same "$f" "$WORK/nonul" || [ -n "$(tail -c 1 "$f" 2>/dev/null)" ]; then
    say "${f#$BOOT_ROOT/} is left without the words of DMA translation: it is not a menu of Hades (a NUL byte, or no line end at its end)"
    rm -rf "$WORK"
    return 0
  fi
  look=$(awk -v kind="$kind" -v words="$DMA_WORDS" "$DMA_LOOK" "$f" 2>/dev/null) || look="unknown: it could not be read"
  case "$look" in
    has) say "${f#$BOOT_ROOT/}: the entries that lock the kernel down ask for DMA translation already: nothing to change" ;;
    none) say "${f#$BOOT_ROOT/} has no entry that locks the kernel down: no DMA translation to ask for" ;;
    chosen) say "${f#$BOOT_ROOT/} is left without the words of DMA translation: an entry that locks the kernel down names the IOMMU already, in another way" ;;
    mixed) say "${f#$BOOT_ROOT/} is left without the words of DMA translation: only some of the entries that lock the kernel down ask for it" ;;
    add\ *) ;;
    *) say "${f#$BOOT_ROOT/} is left without the words of DMA translation: ${look#unknown: }" ;;
  esac
  case "$look" in
    add\ *) n=${look#add } ;;
    *) rm -rf "$WORK"; return 0 ;;
  esac
  awk -v kind="$kind" -v words="$DMA_WORDS" "$DMA_ADD" "$f" >"$WORK/new" 2>/dev/null &&
    awk -v words="$DMA_WORDS" "$DMA_BACK" "$WORK/new" >"$WORK/back" 2>/dev/null ||
    { say "${f#$BOOT_ROOT/} is left without the words of DMA translation: the new one could not be made"; rm -rf "$WORK"; return 0; }
  if ! same "$WORK/back" "$f" || [ "$(grep -c -F -e " $DMA_WORDS" "$WORK/new")" != "$n" ] ||
     [ "$(awk -v kind="$kind" -v words="$DMA_WORDS" "$DMA_LOOK" "$WORK/new" 2>/dev/null)" != has ]; then
    say "${f#$BOOT_ROOT/} is left without the words of DMA translation: the new one is not what it should be"
    rm -rf "$WORK"
    return 0
  fi
  was=$(flash_is)
  if [ "$was" = ro ] && ! mount -o remount,rw "$BOOT_ROOT"; then
    say "${f#$BOOT_ROOT/} is left without the words of DMA translation: the boot partition could not be made writable"
    rm -rf "$WORK"
    return 0
  fi
  rm -f "$f.new"
  if cp "$WORK/new" "$f.new" && sync "$f.new" && same "$f.new" "$WORK/new" && mv -f "$f.new" "$f" &&
     sync "$f" "${f%/*}" && same "$f" "$WORK/new"; then
    say "${f#$BOOT_ROOT/}: the $n entries that lock the kernel down now ask for DMA translation ($DMA_WORDS)"
  elif same "$f" "$WORK/new"; then
    say "${f#$BOOT_ROOT/}: the $n entries that lock the kernel down now ask for DMA translation, but it could not all be written through"
  else
    rm -f "$f.new"
    say "${f#$BOOT_ROOT/} could not be given the words of DMA translation: it is the one of before"
  fi
  sync
  if [ "$was" = ro ] && ! mount -o remount,ro "$BOOT_ROOT"; then
    say "the boot partition could not be made read-only again"
  fi
  rm -rf "$WORK"
  return 0
}

dma_update() {
  dma_menu "$MENU" grub
  dma_menu "$BOOT_ROOT/syslinux.cfg" syslinux
  return 0
}

# The boot menus, for the start after this update. The update restarts the system by itself, and with the
# menus as they are the PC shows the boot menu, with its wait, before the update is applied and once more right
# after the restart that ends it: twice for one update, the second time for no reason, which to anyone who
# did not ask for a menu looks like an installer that came back. So each menu gets, for that one start, a
# block that makes it start the system at once:
#   - GRUB: timeout_style "hidden" and a timeout of one second (the menu is not drawn; Escape shows it);
#   - syslinux (BIOS): "MENU SHIFTKEY": without Shift held when the menu starts, it starts the default entry at
#     once, without its wait; with Shift held, it is drawn and waits as it always does, and a key stops the wait
#     (com32/menu/menumain.c of syslinux 6.03, run_menu). No timeout of its own: the menu's timeout counts with
#     Shift held as well, and Shift is no key that stops it, so with "TIMEOUT 1" the menu that Shift shows was
#     gone after a tenth of a second.
# The block is between the lines "# hades-once-begin" and "# hades-once-end", put after the line that sets the
# wait of that menu, so that the rest of the menu is as it was; usr/lib/libreelec/hades-menu-once takes exactly
# it out at the next start of the system (hades-menu-once.service), and the menu is then, byte for byte, the
# one that was here. The entries, their order, the default and the wait (10 s, or what the setting System >
# Boot menu says) are not changed. Only a menu of Hades (first line "# hades-features:") whose lines are all of
# one kind, with exactly one line that sets the wait and no block already, is given the block; anything else
# stays as it is, and that is said. Written like the other changes of a menu: next to it, written through to the
# device, read back, renamed over it; and checked before: taking the block out of what is written gives the menu
# that was there, byte for byte.
ONCE_BEGIN='# hades-once-begin'
ONCE_END='# hades-once-end'
ONCE_ADD='
{ print }
!done { l = $0; sub(/\r$/, "", l) }
!done && l ~ pat { print b e; n = split(lines, w, "|"); for (i = 1; i <= n; i++) print w[i] e; print x e; done = 1 }
'
ONCE_DROP='
{ l = $0; sub(/\r$/, "", l) }
!skip && l == b { skip = 1; next }
skip && l == x { skip = 0; next }
!skip { print }
'

# once_menu <file> <regex of the line that sets the wait> <lines of the block, separated by |>
once_menu() {
  local f="$1" pat="$2" lines="$3" cr e was n

  [ -f "$f" ] && [ ! -L "$f" ] || return 0
  rm -rf "$WORK"
  mkdir -p "$WORK" || { say "${f#$BOOT_ROOT/} is left as it is: no room to work in"; return 0; }
  if ! tr -d '\000' <"$f" >"$WORK/nonul" || ! same "$f" "$WORK/nonul" || [ -n "$(tail -c 1 "$f" 2>/dev/null)" ]; then
    say "${f#$BOOT_ROOT/} is left as it is: it is not a menu of Hades (a NUL byte, or no line end at its end)"
    return 0
  fi
  # (the busybox of the initramfs has no cut)
  case "$(head -n 1 "$f" | tr -d '\r')" in
    "# hades-features:"*) ;;
    *)
      say "${f#$BOOT_ROOT/} is left as it is: it is not a menu of Hades (its first line)"
      return 0
      ;;
  esac
  if grep -q -F -e "$ONCE_BEGIN" "$f"; then
    say "${f#$BOOT_ROOT/} has the lines of an update already: nothing to add"
    return 0
  fi
  n=$(tr -d '\r' <"$f" | grep -c -E -e "$pat")
  if [ "$n" != 1 ]; then
    say "${f#$BOOT_ROOT/} is left as it is: it has $n lines that set its wait, not one"
    return 0
  fi
  e=""
  [ -n "$(head -n 1 "$f" | tr -d '\n' | tr -cd '\r')" ] && e=$(printf '\r')
  awk -v pat="$pat" -v b="$ONCE_BEGIN" -v x="$ONCE_END" -v lines="$lines" -v e="$e" "$ONCE_ADD" "$f" >"$WORK/new" 2>/dev/null &&
    awk -v b="$ONCE_BEGIN" -v x="$ONCE_END" "$ONCE_DROP" "$WORK/new" >"$WORK/back" 2>/dev/null ||
    { say "${f#$BOOT_ROOT/} is left as it is: the new one could not be made"; return 0; }
  if ! same "$WORK/back" "$f" || [ "$(grep -c -F -e "$ONCE_BEGIN" "$WORK/new")" != 1 ]; then
    say "${f#$BOOT_ROOT/} is left as it is: the new one is not what it should be"
    return 0
  fi
  was=$(flash_is)
  if [ "$was" = ro ] && ! mount -o remount,rw "$BOOT_ROOT"; then
    say "${f#$BOOT_ROOT/} is left as it is: the boot partition could not be made writable"
    return 0
  fi
  rm -f "$f.new"
  if cp "$WORK/new" "$f.new" && sync "$f.new" && same "$f.new" "$WORK/new" && mv -f "$f.new" "$f" &&
     sync "$f" "${f%/*}" && same "$f" "$WORK/new"; then
    say "${f#$BOOT_ROOT/} starts the system at once, for the one start that follows this update"
  else
    rm -f "$f.new"
    say "${f#$BOOT_ROOT/} could not be changed: it is the one of before"
  fi
  sync
  if [ "$was" = ro ] && ! mount -o remount,ro "$BOOT_ROOT"; then
    say "the boot partition could not be made read-only again"
  fi
  rm -rf "$WORK"
  return 0
}

once_update() {
  local version
  version=$(sed -n 's/^VERSION="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$SYSTEM_ROOT/etc/os-release" 2>/dev/null | head -n 1)
  echo ""
  echo "Updating LibreELEC Hades to ${version:-the new version}."
  echo "The system restarts by itself and starts straight away, without the boot menu."
  echo ""
  once_menu "$MENU" '^set timeout="[0-9]+"$' 'set timeout_style="hidden"|set timeout="1"'
  once_menu "$BOOT_ROOT/syslinux.cfg" '^TIMEOUT [0-9]+$' 'MENU SHIFTKEY'
  return 0
}

# The wait of the boot menus (1.0). The menus of 1.0 RC6 to 1.0 RC8 waited 10 s by themselves (5 s before), and the
# setting System > Boot menu of 1.0 RC8 offered 10, 3 and 1 s. Since 1.0 they wait 5 s, the setting offers 10, 5, 3
# and 1, and the menus say "boot-choices" in their first line (scripts/mkimage): the wait that they have is then a
# choice, which every update keeps, in place or with the installer for Windows (tools/hades-installer, BootMenus,
# which follows the same rule). An update in place does not write the menus, so the menus of before get here, once,
# in GRUB (UEFI) and in syslinux (BIOS) together:
#   - the wait of 1.0 where they wait 10 s, the default of before, which nearly no one chose; any other wait (5 s,
#     the default before 1.0 RC6, 3 or 1 s from the setting, or another whole number of seconds from 1 to 30 put in
#     by hand) is kept;
#   - the forms of scripts/mkimage of 1.0: TIMEOUT with three digits, the seconds of GRUB with two, and the line of
#     the BIOS text menu that says the wait with its number (before 1.0 it always said 10), padded with blanks to 13
#     characters; and "boot-choices" at the end of the first line. Every other byte stays as it is (the default, the
#     entries, the comments, the line ends);
#   - only menus of Hades (first line "# hades-features:") with no NUL byte, a line end at their end, every line
#     ending the same way (LF, or CR LF), exactly one line that sets the wait, in the form of before (no leading 0:
#     GRUB reads a number that starts with 0 as octal), and no block of one start; both, where both are there, and
#     waiting the same. With any doubt both are left as they are, and that is said;
#   - what comes out is checked before it is written: line by line the menu that was there, but for the end of the
#     first line, the line that sets the wait and the lines that say it, which say the new wait;
#   - written as the other changes of a menu (next to it, written through to the device, read back, renamed over
#     it), and both or none: when the second cannot be written, the first is written back as it was;
#   - menus that say "boot-choices" are left as they are: a second update does nothing, and a wait chosen in 1.0 or
#     later, 10 s as well, is kept for good (internal-disks, which changes the wait for the setting, writes the word
#     too).
WAIT_FEATURE=boot-choices
WAIT_BEFORE=10
WAIT_NOW=5
WAIT_SAY=' mode to start automatically in '
# What the menu is (kind: syslinux or grub): "marked" (its first line says boot-choices), "wait <seconds> <cr>" (a
# menu of before that can be given the forms of 1.0: its wait, and 1 when its lines end with CR LF), or
# "unknown: <why>". A line that sets the wait is read as loosely as the boot loader may read it (syslinux takes its
# keywords in any case; GRUB takes a variable set with or without "set"), and must be in the form of before.
WAIT_LOOK='
function bad(why) { if (verdict == "") verdict = "unknown: " why }
{
  l = $0
  c = sub(/\r$/, "", l)
  if (NR == 1) {
    cr = c
    if (l !~ /^# hades-features:/) bad("it is not a menu of Hades (its first line)")
    else {
      n = split(l, w, " ")
      for (i = 2; i <= n; i++) if (w[i] == feature) marked = 1
    }
  } else if (c != cr) bad("its lines do not all end the same way")
  if (index(l, "\r")) bad("a carriage return inside line " NR)
  if (l == "# hades-once-begin" || l == "# hades-once-end") bad("it has the block of one start after an update")
  if (kind == "syslinux" && l ~ /^[ \t]*[Tt][Ii][Mm][Ee][Oo][Uu][Tt]([ \t]|$)/) {
    waits++
    s = -1
    if (l ~ /^TIMEOUT [1-9][0-9]*$/ && length(l) <= 12 && substr(l, 9) % 10 == 0) s = substr(l, 9) / 10
  }
  if (kind == "grub" && l ~ /^[ \t]*(set[ \t]+)?timeout=/) {
    waits++
    s = -1
    if (l ~ /^set timeout="[1-9][0-9]*"$/ && length(l) <= 18) { s = substr(l, 14); sub(/"$/, "", s); s += 0 }
  }
}
END {
  if (NR == 0) bad("it is empty")
  if (marked) print "marked"
  else if (verdict != "") print verdict
  else if (waits != 1) print "unknown: it has " (waits + 0) " lines that set its wait, not one"
  else if (s < 1 || s > 30) print "unknown: its wait is not a whole number of seconds from 1 to 30, written as before 1.0"
  else print "wait " s " " cr
}
'
# The head of a line of the BIOS text menu that says the wait, up to the last WAIT_SAY in it; "" for any other line
WAIT_SAYHEAD='
function sayhead(l,    j, k) {
  if (index(l, "SAY Wait for ") != 1) return ""
  k = 0
  while ((j = index(substr(l, k + 1), tail)) > 0) k += j
  if (k < 14) return ""
  return substr(l, 1, k - 1 + length(tail))
}
'
# The new menu (form: the line that sets the wait; words: the end of the lines that say it): the word at the end of
# the first line, the line that sets the wait in the form of 1.0, the lines that say it with the new number, each
# with its line end; every other line as it is
WAIT_MAKE="$WAIT_SAYHEAD"'
{
  l = $0
  c = sub(/\r$/, "", l)
  e = c ? "\r" : ""
  if (NR == 1) { print l " " feature e; next }
  if ((kind == "syslinux" && l ~ /^TIMEOUT [0-9]+$/) || (kind == "grub" && l ~ /^set timeout="[0-9]+"$/)) { print form e; next }
  if (kind == "syslinux" && (h = sayhead(l)) != "") { print h words e; next }
  print
}
'
# Whether the new menu (the second file) is the old one (the first) line by line, but for the word at the end of the
# first line, the line that sets the wait (one, which is form) and the lines that say it (each of them ending with
# words): "ok", or why not
WAIT_SAME="$WAIT_SAYHEAD"'
FNR == NR { o[FNR] = $0; n = FNR; next }
{
  m = FNR
  a = o[FNR]
  b = $0
  ca = sub(/\r$/, "", a)
  cb = sub(/\r$/, "", b)
  if (ca != cb && why == "") why = "line " FNR " ends in another way"
  if ((kind == "syslinux" && b ~ /^[ \t]*[Tt][Ii][Mm][Ee][Oo][Uu][Tt]([ \t]|$)/) || (kind == "grub" && b ~ /^[ \t]*(set[ \t]+)?timeout=/)) {
    waits++
    if (b == form) forms++
  }
  if (kind == "syslinux" && (h = sayhead(b)) != "") {
    said++
    if (b == h words) right++
  }
  if (a == b) next
  if (FNR == 1 && b == a " " feature) { first++; next }
  if (b == form && ((kind == "syslinux" && a ~ /^TIMEOUT [0-9]+$/) || (kind == "grub" && a ~ /^set timeout="[0-9]+"$/))) next
  if (kind == "syslinux" && (h = sayhead(a)) != "" && b == h words) next
  if (why == "") why = "line " FNR " is not the one that was there"
}
END {
  if (why == "" && m != n) why = "it has " (m + 0) " lines, and the menu had " (n + 0)
  if (why == "" && first != 1) why = "its first line does not end with " feature
  if (why == "" && (waits != 1 || forms != 1)) why = "it does not have one line that sets the wait, as " form
  if (why == "" && said != right) why = "a line of the text menu does not say the new wait"
  print (why == "" ? "ok" : why)
}
'

wait_file() {
  if [ "$1" = grub ]; then echo "$MENU"; else echo "$BOOT_ROOT/syslinux.cfg"; fi
}

# wait_put <from> <menu>: the menu becomes the file from: written next to it, written through to the device and read
# back, renamed over it, written through again and read back
wait_put() {
  rm -f "$2.new"
  if cp "$1" "$2.new" && sync "$2.new" && same "$2.new" "$1" && mv -f "$2.new" "$2" && sync "$2" "${2%/*}" &&
     same "$2" "$1"; then
    return 0
  fi
  rm -f "$2.new"
  return 1
}

wait_update() {
  local kind f look s="" todo="" why="" marked="" n form words was written="" failed="" now old
  rm -rf "$WORK"
  mkdir -p "$WORK" || { say "the wait of the boot menus is left as it is: no room to work in"; return 0; }
  for kind in grub syslinux; do
    f=$(wait_file "$kind")
    [ -e "$f" ] || [ -L "$f" ] || continue
    if [ ! -f "$f" ] || [ -L "$f" ]; then
      [ -z "$why" ] && why="${f#$BOOT_ROOT/} is not a file"
      continue
    fi
    if ! tr -d '\000' <"$f" >"$WORK/nonul" || ! same "$f" "$WORK/nonul" || [ -n "$(tail -c 1 "$f" 2>/dev/null)" ]; then
      [ -z "$why" ] && why="${f#$BOOT_ROOT/} is not a menu of Hades (a NUL byte, or no line end at its end)"
      continue
    fi
    look=$(awk -v kind="$kind" -v feature="$WAIT_FEATURE" "$WAIT_LOOK" "$f" 2>/dev/null) || look="unknown: it could not be read"
    case "$look" in
      marked)
        marked="$marked ${f#$BOOT_ROOT/}"
        ;;
      wait\ *)
        set -- $look
        if [ -n "$s" ] && [ "$s" != "$2" ]; then
          [ -z "$why" ] && why="the two menus wait differently ($s and $2 seconds)"
        fi
        s=$2
        todo="$todo $kind"
        ;;
      *)
        [ -z "$why" ] && why="${f#$BOOT_ROOT/}: ${look#unknown: }"
        ;;
    esac
  done
  if [ -n "$marked" ]; then
    say "the wait of the boot menus is one chosen since 1.0 ($WAIT_FEATURE in the first line of${marked}): kept as it is"
    rm -rf "$WORK"
    return 0
  fi
  if [ -n "$why" ]; then
    say "the wait of the boot menus is left as it is: $why"
    rm -rf "$WORK"
    return 0
  fi
  if [ -z "$todo" ]; then
    say "no boot menu: no wait to change"
    rm -rf "$WORK"
    return 0
  fi

  n=$s
  [ "$s" = "$WAIT_BEFORE" ] && n=$WAIT_NOW
  if [ "$n" -eq 1 ]; then words=$(printf '%-13s' "1 second..."); else words=$(printf '%-13s' "$n seconds..."); fi
  for kind in $todo; do
    f=$(wait_file "$kind")
    if [ "$kind" = grub ]; then form="set timeout=\"$(printf '%02d' "$n")\""; else form="TIMEOUT $(printf '%03d' $((n * 10)))"; fi
    if ! cp "$f" "$WORK/$kind.old" || ! same "$WORK/$kind.old" "$f" ||
       ! awk -v kind="$kind" -v feature="$WAIT_FEATURE" -v form="$form" -v words="$words" -v tail="$WAIT_SAY" "$WAIT_MAKE" "$WORK/$kind.old" >"$WORK/$kind.new" 2>/dev/null; then
      say "the wait of the boot menus is left as it is: the new ${f#$BOOT_ROOT/} could not be made"
      rm -rf "$WORK"
      return 0
    fi
    look=$(awk -v kind="$kind" -v feature="$WAIT_FEATURE" -v form="$form" -v words="$words" -v tail="$WAIT_SAY" "$WAIT_SAME" "$WORK/$kind.old" "$WORK/$kind.new" 2>/dev/null)
    if [ "$look" != ok ] || [ "$(awk -v kind="$kind" -v feature="$WAIT_FEATURE" "$WAIT_LOOK" "$WORK/$kind.new" 2>/dev/null)" != marked ]; then
      say "the wait of the boot menus is left as it is: the new ${f#$BOOT_ROOT/} is not what it should be (${look:-it could not be checked})"
      rm -rf "$WORK"
      return 0
    fi
  done

  was=$(flash_is)
  if [ "$was" = ro ] && ! mount -o remount,rw "$BOOT_ROOT"; then
    say "the wait of the boot menus is left as it is: the boot partition could not be made writable"
    rm -rf "$WORK"
    return 0
  fi
  for kind in $todo; do
    f=$(wait_file "$kind")
    if wait_put "$WORK/$kind.new" "$f"; then
      written="$written $kind"
    else
      failed=$kind
      break
    fi
  done
  # (both or none: what was written goes back as it was)
  if [ -n "$failed" ]; then
    for kind in $failed $written; do
      f=$(wait_file "$kind")
      same "$WORK/$kind.old" "$f" || wait_put "$WORK/$kind.old" "$f"
    done
  fi
  now=0
  old=0
  for kind in $todo; do
    f=$(wait_file "$kind")
    same "$WORK/$kind.new" "$f" && now=$((now + 1))
    same "$WORK/$kind.old" "$f" && old=$((old + 1))
  done
  set -- $todo
  if [ -z "$failed" ] && [ "$now" = $# ]; then
    if [ "$n" != "$s" ]; then
      say "the wait of the boot menu is now $n s (it was $s s, the default before 1.0); it can be changed in Settings > LibreELEC > System > Boot menu"
    else
      say "the boot menus keep their wait of $n s, now written as 1.0 writes it ($WAIT_FEATURE)"
    fi
  elif [ "$old" = $# ]; then
    say "the wait of the boot menus could not be changed: the menus are as they were"
  else
    say "the wait of the boot menus could not be changed, and the menus are not as they were: look at syslinux.cfg and EFI/BOOT/grub.cfg"
  fi
  sync
  if [ "$was" = ro ] && ! mount -o remount,ro "$BOOT_ROOT"; then
    say "the boot partition could not be made read-only again"
  fi
  rm -rf "$WORK"
  return 0
}

efi_update
menu_update
dma_update
wait_update
once_update
exit 0
