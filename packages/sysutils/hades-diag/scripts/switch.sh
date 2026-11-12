#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: their switch, for the setting "Save diagnostics at every start" (LibreELEC settings, System).
#
# The switch is a file at the top of the boot partition whose name starts with DIAG-ON, in any letter case and with
# any ending (start.sh, collect.sh, watch-dv.sh, shutdown.sh, installer.sh and the init look for it so). Every image
# carries it off, as DIAG-OFF.TXT; the installer for Windows renames that file, and so can anybody with the drive
# in Windows. This renames it in the same way, and nothing else: no other file keeps the setting, so what is shown
# is what the partition says.
#
# Usage: switch.sh status   print "on" or "off", then the KiB that the folder hades-diag takes on the partition and
#                           the KiB free there; or "unavailable" and why
#        switch.sh on       DIAG-OFF.TXT becomes DIAG-ON.TXT; where the partition has no DIAG-OFF.TXT, DIAG-ON.TXT is
#                           made from the copy that the system has of it
#        switch.sh off      a file whose name starts with DIAG-ON becomes DIAG-OFF.TXT, and any other such file goes
#                           (each of them would keep the switch on); where DIAG-OFF.TXT is there already, they all go
# What the diagnostics do once it is on begins with the next start (start.sh is run before Kodi, and the init keeps
# the journal of a start in the storage while it is on). Turned off, the diagnostics that are running write nothing
# more (they look at the switch before each write), and what they saved stays.
#
# Exit status:
#    0  status: told. on, off: the partition says so, looked at again after the change (or did so already)
#    1  wrong usage
#   10  the boot partition is not mounted
#   14  the boot partition is not mounted read-only, or something else is writing to it
#   20  the change could not be made, and the partition is as it was
#   21  the change could not be made, and the partition is not as it was ("status" tells how it is)
#   22  the boot partition could not be made read-only again
# With 10 and 14 nothing was written.
#
# The boot partition is writable only for the change, and is made read-only again when this ends, also when a
# signal ends it (HUP, INT, QUIT, TERM, USR1, USR2, ALRM; a reader of its output that goes away does not end it).
# The lock is the one of the writers of that partition (/run/hades-flash.lock: the diagnostics, internal-disks), so
# a change never meets one of them.

# Run from Kodi, this has its environment, in which add-ons put folders of their own into PATH and LD_LIBRARY_PATH,
# and mount is linked to libmount: only what the system has is run, in the C locale, without the variables that the
# loader, the C library, libmount and libblkid take from the environment (as internal-disks).
PATH=/usr/bin:/usr/sbin
export PATH
LC_ALL=C
export LC_ALL
unset GCONV_PATH GETCONF_DIR GLIBC_TUNABLES HOSTALIASES LD_AUDIT LD_BIND_NOT LD_BIND_NOW LD_DEBUG \
  LD_DEBUG_OUTPUT LD_DYNAMIC_WEAK LD_LIBRARY_PATH LD_ORIGIN_PATH LD_PRELOAD LD_PROFILE LD_PROFILE_OUTPUT \
  LD_SHOW_AUXV LD_VERBOSE LD_WARN LOCALDOMAIN LOCPATH MALLOC_ARENA_MAX MALLOC_ARENA_TEST MALLOC_MMAP_MAX_ \
  MALLOC_MMAP_THRESHOLD_ MALLOC_PERTURB_ MALLOC_TOP_PAD_ MALLOC_TRACE MALLOC_TRIM_THRESHOLD_ NIS_PATH NLSPATH \
  RESOLV_HOST_CONF RES_OPTIONS TMPDIR TZDIR \
  LIBMOUNT_DEBUG LIBMOUNT_FORCE_MOUNT2 LIBMOUNT_FSTAB LIBMOUNT_KERNEL_CMDLINE LIBMOUNT_MTAB LIBMOUNT_SWAPS \
  LIBMOUNT_UTAB LIBBLKID_DEBUG BLKID_CONF BLKID_FILE LOCK_BLOCK_DEVICE
trap '' PIPE

FLASH=/flash
MOUNTS=/proc/mounts
RUNDIR=/run
LOCK=$RUNDIR/hades-flash.lock
# (the switch as an image carries it, kept by the system: packages/sysutils/hades-diag/boot/DIAG-OFF.TXT)
OFF_COPY=/usr/lib/hades-diag/DIAG-OFF.TXT
OFF=DIAG-OFF.TXT
ON=DIAG-ON.TXT

say() {
  echo "switch: $1" >&2
}

# How the boot partition is mounted: ro, rw or not-mounted
flash_is() {
  awk -v m="$FLASH" '$2 == m { o = "," $4 "," } END { if (o == "") print "not-mounted"; else if (o ~ /,ro,/) print "ro"; else print "rw" }' "$MOUNTS" 2>/dev/null
}

# The names at the top of the partition that turn the switch on, one a line (a name with a line end in it is no
# name that anybody gives this file: it is not looked at, as the other scripts do not see it either)
ons() {
  ls -A "$FLASH" 2>/dev/null | grep -i '^diag-on'
}
# The name of DIAG-OFF.TXT as the partition has it (FAT does not tell the letter cases apart), or nothing
off_name() {
  ls -A "$FLASH" 2>/dev/null | grep -i -x 'diag-off\.txt' | head -n 1
}
state() {
  if [ -n "$(ons)" ]; then echo on; else echo off; fi
}

# RW: yes from just before this asks for the partition to be made writable, until it is read-only again; failed
#   when it could not be made so
# LOCKED: this has the lock
# CHANGED: a name may have been changed
# HOLD, PENDING: a signal that comes while something is done whose outcome must be known first is only noted, and
#   acted on when the outcome is known
RW=no
LOCKED=no
CHANGED=no
HOLD=no
PENDING=no

read_only() {
  local n=0
  [ "$RW" = yes ] || return 0
  # (what is waiting to be written there: the names that were changed)
  sync -f "$FLASH" 2>/dev/null
  while ! mount -o remount,ro "$FLASH" 2>/dev/null; do
    n=$((n + 1))
    if [ $n -ge 5 ]; then
      RW=failed
      say "$FLASH could not be made read-only again"
      return 1
    fi
    sleep 1
  done
  RW=no
}

# When this ends, by itself or by a signal that can be caught: the partition read-only again, and the lock given back
leave() {
  trap '' HUP INT QUIT TERM USR1 USR2 ALRM
  read_only
  if [ "$LOCKED" = yes ]; then
    rmdir "$LOCK" 2>/dev/null
    LOCKED=no
  fi
  [ "$RW" = no ]
}

# Stopped by a signal: before a name could have been changed, nothing was; after that, the partition is for
# "status" to tell
stopped() {
  if [ "$HOLD" = yes ]; then
    PENDING=yes
    return
  fi
  say "stopped"
  [ "$CHANGED" = no ] && exit 20
  exit 21
}

lock() {
  local n=0
  while :; do
    HOLD=yes
    mkdir "$LOCK" 2>/dev/null && LOCKED=yes
    HOLD=no
    [ "$PENDING" = no ] || stopped
    [ "$LOCKED" = yes ] && return 0
    n=$((n + 1))
    [ $n -le 20 ] || return 1
    sleep 1
  done
}

do_status() {
  local used free
  if [ "$(flash_is)" = not-mounted ]; then
    say "$FLASH is not mounted"
    echo "unavailable not-mounted"
    exit 10
  fi
  used=$(du -sk "$FLASH/hades-diag" 2>/dev/null | awk '{ print $1 + 0 }')
  free=$(df -P -k "$FLASH" 2>/dev/null | awk 'NR == 2 { print $4 + 0 }')
  echo "$(state) ${used:-0} ${free:-0}"
  exit 0
}

# The renames that turn the switch to $1, with the partition writable. Returns 1 at the first one that fails.
turn() {
  local first=yes n
  if [ "$1" = on ]; then
    n=$(off_name)
    if [ -n "$n" ]; then
      mv "$FLASH/$n" "$FLASH/$ON" 2>/dev/null || return 1
    else
      # (a file of the partition that is cut short still turns the switch on: its name is the switch)
      cp "$OFF_COPY" "$FLASH/$ON" 2>/dev/null || { rm -f "$FLASH/$ON" 2>/dev/null; return 1; }
    fi
    sync "$FLASH/$ON" 2>/dev/null
  else
    ons > "$WORK" || return 1
    while IFS= read -r n; do
      if [ "$first" = yes ] && [ -z "$(off_name)" ]; then
        mv "$FLASH/$n" "$FLASH/$OFF" 2>/dev/null || return 1
        sync "$FLASH/$OFF" 2>/dev/null
      else
        rm -f "$FLASH/$n" 2>/dev/null || return 1
      fi
      first=no
    done < "$WORK"
  fi
  # (the folder, where the names are)
  sync "$FLASH" 2>/dev/null
  return 0
}

# The names that are not files among those that turn the switch on (a folder named so is not removed)
not_files() {
  ons | while IFS= read -r n; do
    { [ -f "$FLASH/$n" ] && [ ! -L "$FLASH/$n" ]; } || echo "$n"
  done
}

# $1: on or off
change() {
  local want=$1 was before

  if [ "$(flash_is)" = not-mounted ]; then
    say "$FLASH is not mounted"
    exit 10
  fi
  if [ "$(state)" = "$want" ]; then
    echo "$want"
    exit 0
  fi
  if [ "$want" = off ] && [ -n "$(not_files)" ]; then
    say "$(not_files | head -n 1) is not a file: it is left as it is, and the switch stays on"
    exit 20
  fi

  trap 'leave || exit 22' EXIT
  trap 'stopped' HUP INT QUIT TERM USR1 USR2 ALRM
  if ! lock; then
    say "something else is writing to $FLASH"
    exit 14
  fi
  if [ "$(flash_is)" != ro ]; then
    say "$FLASH is not mounted read-only"
    exit 14
  fi
  # (with the lock: it may have been changed meanwhile)
  was=$(state)
  if [ "$was" = "$want" ]; then
    echo "$want"
    exit 0
  fi
  if [ "$want" = off ] && [ -n "$(not_files)" ]; then
    say "$(not_files | head -n 1) is not a file: it is left as it is, and the switch stays on"
    exit 20
  fi
  # (the names at the top of the partition before the change: what "as it was" is)
  before=$(ls -A "$FLASH" 2>/dev/null)
  WORK=$RUNDIR/hades-diag-switch.$$
  rm -f "$WORK"

  # (told before it is asked for: a signal that comes while the partition is being made writable finds it to be
  # made read-only again)
  RW=yes
  if ! mount -o remount,rw "$FLASH" 2>/dev/null; then
    RW=no
    say "$FLASH could not be made writable"
    exit 20
  fi
  CHANGED=yes
  turn "$want" || say "a name on $FLASH could not be changed"
  rm -f "$WORK"
  read_only
  if [ "$(state)" = "$want" ]; then
    [ "$RW" = no ] || exit 22
    echo "$want"
    exit 0
  fi
  [ "$RW" = no ] || exit 22
  if [ "$(ls -A "$FLASH" 2>/dev/null)" = "$before" ]; then
    say "the switch could not be turned $want: the partition is as it was"
    exit 20
  fi
  say "the switch could not be turned $want, and the partition is not as it was"
  exit 21
}

if [ $# -eq 1 ]; then
  case "$1" in
    status) do_status ;;
    on|off) change "$1" ;;
  esac
fi
echo "usage: switch.sh status|on|off" >&2
exit 1
