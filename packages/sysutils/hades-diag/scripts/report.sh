#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: a report made on demand, and the log of the share Logfiles, each as one zip file in
# /storage/logfiles, with nothing in it that has not been through the filter of the diagnostics (redact.sh):
#   report.sh now            the report of the settings (System > Submit Log > "Create a diagnostic report"):
#                            what the diagnostics collect, now (collect.sh report: the snapshot "now", the journal of
#                            this start and of the start before it, the newest crash log of Kodi, the storage
#                            timeline), every file through the filter that fits it (redact.sh tree), and beside it
#                            what the diagnostics saved on the boot partition at the last starts (hades-diag/start-N,
#                            through the filter already when they were saved; the newest first, 16 MiB at most):
#                            hades-report-<date>-<time>.zip
#   report.sh share [FAILED] what LibreELEC's createlog collects, each file through the filter for logs (the
#                            filter of Submit Log): filtered-log-<date>-<time>[-FAILED].zip. Run by
#                            hades-share-log.service when the share Logfiles is opened, and by kodi.sh, with FAILED,
#                            where Kodi failed again and again (LibreELEC left createlog's zip there, unfiltered).
#                            While the one of the share is made, a note in the folder says so, and since when.
#   report.sh open           Samba's "root preexec" of the share Logfiles: starts hades-share-log.service and waits for
#                            it 10 s at most, so that the folder has the new log when it opens on a PC that is quick
#                            enough, and the note otherwise. It never keeps the share from opening.
# createlog by itself, over SSH, still leaves its zip unfiltered (log-<date>.zip), as in LibreELEC; the share does not
# show those (its "veto files").
# The files as the system wrote them are collected into a folder in RAM that is this run's, readable by root only,
# filtered into another one, and only that one is packed: a file that the filter fails on is left out, and the note
# in the zip says so; without the filter, nothing is collected. The zip is made in a folder of the storage that is this
# run's, and given its name in /storage/logfiles only when it is whole (a rename on the same file system). Prints
# "<path><tab><files><tab><bytes>" once it is there.
# Exit status: 0 made; 2 wrong usage; 10 the filter is not on this system (nothing collected); 11 nothing came through
# the filter (nothing written); 12 not enough room in the storage; 13 not enough free memory; 14 it could not be
# written; 15 another one is being made.
L=/usr/lib/hades-diag
R=$L/redact.sh
LOGS=/storage/logfiles
WORKDIR=/storage/.cache/hades-diag
TMP=/tmp
RUNDIR=/run
FLASH=/flash
D=$FLASH/hades-diag
CREATELOG=/usr/bin/createlog
ZIP=/usr/bin/simple_zip.py
# (what a report takes of the folders of the starts that the diagnostics saved, in KiB, the newest first)
SAVED_MAX=16384
# (the filter that a folder of a start went through: FILTERED of folder.sh)
FILTERED=4
UNIT=hades-share-log.service
NOTE=filtered-log-in-progress.txt

umask 077
export LC_ALL=C

S= O= N= Z= NOTED= MINE= FL=
finish() {
  [ -n "$S" ] && rm -rf "$S"
  [ -n "$O" ] && rm -rf "$O"
  [ -n "$N" ] && rm -f "$N"
  [ -n "$Z" ] && rm -rf "$Z"
  [ -n "$NOTED" ] && rm -f "$LOGS/$NOTE"
  [ -n "$FL" ] && rmdir "$FL" 2>/dev/null
  [ -n "$MINE" ] && rm -rf "$MINE"
}

# KiB free on the file system of $1
free_kib() { df -P -k "$1" 2>/dev/null | awk 'NR == 2 { print $4 + 0 }'; }

# The lock of this script $1 (a directory in RAM with the number of the process that has it), waited for at most $2
# seconds; one whose process is gone (stopped with KILL) is taken over
own_lock() {
  local n=0 p
  while ! mkdir "$1" 2>/dev/null; do
    p=$(cat "$1/pid" 2>/dev/null)
    if [ -n "$p" ] && ! kill -0 "$p" 2>/dev/null; then rm -rf "$1"; continue; fi
    n=$((n + 1)); [ $n -le $2 ] || return 1; sleep 1
  done
  MINE=$1
  echo $$ > "$1/pid"
}

# The lock of the writers of the boot partition (collect.sh, watch-dv.sh, shutdown.sh, internal-disks), waited for at
# most a minute
flash_lock() {
  local n=0
  while ! mkdir "$RUNDIR/hades-flash.lock" 2>/dev/null; do
    n=$((n + 1)); [ $n -le 60 ] || return 1; sleep 1
  done
  FL=$RUNDIR/hades-flash.lock
}

# The filter is there, all of it: without it nothing is collected
filter_there() { [ -f "$R" ] && [ -f "$L/redact-ids.awk" ] && [ -f "$L/redact.sed" ]; }

# The folders in RAM: S (the files as the system wrote them) and O (what the filter made of them, in O/<name>)
work() {
  [ "$(free_kib "$TMP")" -gt 98304 ] || exit 13
  S=$(mktemp -d "$TMP/hades-report.XXXXXX") || exit 13
  O=$(mktemp -d "$TMP/hades-report.XXXXXX") || exit 13
  N=$O.note
}

# The files of the folder $1 that are not in the folder $2, one a line: what the filter left out
left_out() {
  (cd "$1" && find . -type f | sed 's|^\./||' | sort) | while IFS= read -r f; do
    [ -f "$2/$f" ] || echo "$f"
  done
}

# O/$1 packed into $LOGS/$1.zip, its files under a folder $1 of the zip. The zip is made in a folder of the storage
# that is this run's, and renamed into $LOGS once it is whole and on the drive (with a number after the name where
# that name is taken).
pack() {
  local name="$1" need t n
  need=$(du -sk "$O" 2>/dev/null | awk '{ print $1 + 0 }')
  mkdir -p "$LOGS" "$WORKDIR" 2>/dev/null
  [ "$(free_kib "$LOGS")" -gt $((need + 16384)) ] || exit 12
  Z=$(mktemp -d "$WORKDIR/report.XXXXXX") || exit 14
  "$ZIP" -j -f "$Z/$name.zip" -i "$O" > /dev/null 2>&1 && [ -s "$Z/$name.zip" ] || exit 14
  chmod 0644 "$Z/$name.zip"
  t="$LOGS/$name.zip"; n=1
  while [ -e "$t" ]; do n=$((n + 1)); t="$LOGS/$name-$n.zip"; done
  sync "$Z/$name.zip" 2>/dev/null || exit 14
  mv -n "$Z/$name.zip" "$t" && [ -f "$t" ] && [ ! -e "$Z/$name.zip" ] || exit 14
  sync "$LOGS" 2>/dev/null
  printf '%s\t%s\t%s\n' "$t" "$(find "$O/$name" -type f | wc -l)" "$(wc -c < "$t")"
}

# The folders of the starts that the diagnostics saved (folder.sh), the newest first, into $1, as they are: the ones
# that went through the filter of this version ($FILTERED), read while their writers write nothing, until they take
# SAVED_MAX KiB. What is left out of them is said in the note.
saved() {
  local into="$1" took=0 k f
  if [ ! -d "$D" ]; then
    echo "saved/: nothing. The diagnostics have saved nothing on the boot partition: their switch is off, or was not on long enough." >> "$N"
    return 0
  fi
  if ! flash_lock; then
    echo "saved/: nothing. The writers of the boot partition were busy for more than a minute." >> "$N"
    return 0
  fi
  mkdir -p "$into"
  for f in README.txt LEEME.txt NO-ROOM.txt; do [ -f "$D/$f" ] && cp "$D/$f" "$into/"; done
  for f in $(ls "$D" 2>/dev/null | sed -n 's/^[Ss][Tt][Aa][Rr][Tt]-0*\([0-9]\{1,9\}\)$/\1 &/p' | sort -rn | awk '{ print $2 }'); do
    [ -d "$D/$f" ] || continue
    if [ "$(cat "$D/$f/.filtered" 2>/dev/null)" != "$FILTERED" ]; then
      echo "Left out: saved/$f (saved by an earlier version, and not yet put through the filter of this one)." >> "$N"
      continue
    fi
    k=$(du -sk "$D/$f" 2>/dev/null | awk '{ print $1 + 0 }')
    if [ $((took + k)) -gt $SAVED_MAX ]; then
      echo "Left out: saved/$f ($k KiB: the report takes $SAVED_MAX KiB of the saved starts at most, the newest first)." >> "$N"
      continue
    fi
    if cp -r "$D/$f" "$into/" 2>/dev/null; then took=$((took + k)); else rm -rf "$into/$f"; echo "Left out: saved/$f (it could not be read)." >> "$N"; fi
  done
  rmdir "$FL" 2>/dev/null; FL=
}

report_now() {
  local name
  filter_there || exit 10
  own_lock "$RUNDIR/hades-report.lock" 0 || exit 15
  trap finish EXIT
  trap 'exit 1' INT TERM HUP
  work
  name="hades-report-$(date +%Y-%m-%d-%H.%M.%S)"
  # what the system is doing now, as it writes it (S is this run's, in RAM)
  sh "$L/collect.sh" report "$S" < /dev/null > /dev/null 2>&1
  [ -n "$(find "$S" -type f 2>/dev/null | head -n 1)" ] || exit 11
  # every file through the filter that fits it; one that it fails on is not in O
  sh "$R" tree "$S" "$O/$name" < /dev/null > /dev/null 2>&1
  [ -n "$(find "$O/$name" -type f ! -name filter.txt 2>/dev/null | head -n 1)" ] || exit 11
  {
    echo "LibreELEC Hades: a diagnostic report"
    echo
    echo "build: $(sed -n 's/^VERSION="*\([^"]*\)"*$/\1/p' /etc/os-release 2>/dev/null)"
    echo "made: $(date '+%F %T %Z') ($(date -u '+%F %T') UTC), $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s after the start of the system"
    echo
    echo "now/: what the system was doing when the report was made: results.txt first (the graphics and the displays,"
    echo "the services, the network, the sound, the USB devices and the warnings of the system), the end of Kodi's logs,"
    echo "the EDID of each display, how long the start took (boot-time.txt), the firmware's ACPI tables."
    echo "journal.txt.gz: the journal of this start. previous-boot/: the journal of the start before it, where it was kept."
    echo "kodi_crashlog_*.log.gz: the newest crash log of Kodi, where there is one."
    echo "io-timeline.txt: the storage and the graphics second by second, while the diagnostics are on."
    echo "saved/: what the diagnostics saved at each start while their switch was on (hades-diag on the boot partition)."
    echo
    echo "Every file went through the filter of the diagnostics: addresses, serial numbers, the names of networks, of"
    echo "drives, of films and of this system are markers, and what looks like a key or a password is taken out."
    echo "No filter is perfect: read it before you give it to anyone."
    echo
    left_out "$S" "$O/$name" | sed -e 's/^/Left out: /' -e 's/$/ (the filter failed on it)./'
  } > "$N"
  rm -rf "$S"; S=
  saved "$O/$name/saved"
  # (the note through the filter too: everything in the report is)
  sh "$R" ids < "$N" > "$O/$name/report.txt" 2>/dev/null || rm -f "$O/$name/report.txt"
  pack "$name"
  exit 0
}

# What createlog collects, each file through the filter for logs. $1: FAILED or nothing.
share() {
  local name f
  filter_there || exit 10
  [ -x "$CREATELOG" ] || exit 11
  own_lock "$RUNDIR/hades-share-log.lock" 300 || exit 15
  trap finish EXIT
  trap 'exit 1' INT TERM HUP
  work
  name="filtered-log-$(date +%Y-%m-%d-%H.%M.%S)${1:+-$1}"
  if [ -z "$1" ]; then
    mkdir -p "$LOGS" 2>/dev/null
    {
      echo "A new filtered log of LibreELEC Hades is being made, since $(date '+%F %T'): $name.zip."
      echo "It takes from a few seconds to a few minutes. Open this folder again then."
      echo
      echo "Se está haciendo un registro filtrado nuevo de LibreELEC Hades, desde el $(date '+%F a las %T'): $name.zip."
      echo "Tarda de unos segundos a unos minutos. Vuelve a abrir esta carpeta entonces."
    } > "$LOGS/$NOTE" 2>/dev/null && chmod 0644 "$LOGS/$NOTE" && NOTED=1
  fi
  # (createlog with a folder leaves its files there, as it wrote them, and packs nothing; the folder that it makes for
  # itself in TMPDIR, and leaves behind, is in this run's)
  mkdir "$S/files" "$S/tmp" || exit 13
  TMPDIR="$S/tmp" "$CREATELOG" "$S/files" < /dev/null > /dev/null 2>&1
  [ -n "$(find "$S/files" -type f 2>/dev/null | head -n 1)" ] || exit 11
  mkdir -p "$O/$name"
  for f in $(cd "$S/files" && find . -type f | sed 's|^\./||'); do
    case $f in */*) mkdir -p "$O/$name/${f%/*}" ;; esac
    sh "$R" < "$S/files/$f" > "$O/$name/$f" 2>/dev/null || rm -f "$O/$name/$f"
  done
  [ -n "$(find "$O/$name" -type f | head -n 1)" ] || exit 11
  sh "$R" hostnote "$O/$name" > "$O/$name/filter.txt" 2>/dev/null || rm -f "$O/$name/filter.txt"
  {
    echo "LibreELEC Hades: the log of the share Logfiles (what LibreELEC's createlog collects), $(date '+%F %T %Z')."
    echo "Every file went through the filter of the diagnostics for logs. No filter is perfect: read it before you"
    echo "give it to anyone."
    left_out "$S/files" "$O/$name" | sed -e 's/^/Left out: /' -e 's/$/ (the filter failed on it)./'
  } > "$N"
  sh "$R" ids < "$N" > "$O/$name/README.txt" 2>/dev/null || rm -f "$O/$name/README.txt"
  rm -rf "$S"; S=
  pack "$name"
  exit 0
}

# Samba's root preexec: the log made by its service, waited for 10 s at most (a oneshot unit is "activating" while it
# runs); whatever happens, the share opens
open_share() {
  local n=0
  systemctl --no-block start "$UNIT" > /dev/null 2>&1 || exit 0
  while [ $n -lt 10 ]; do
    case "$(systemctl is-active "$UNIT" 2>/dev/null)" in activating) ;; *) [ $n -gt 0 ] && break ;; esac
    sleep 1; n=$((n + 1))
  done
  exit 0
}

case "$1" in
  now) [ $# -eq 1 ] || exit 2; report_now ;;
  share) { [ $# -eq 1 ] || { [ $# -eq 2 ] && [ "$2" = FAILED ]; }; } || exit 2; share "$2" ;;
  open) [ $# -eq 1 ] || exit 2; open_share ;;
esac
echo "usage: report.sh now | share [FAILED] | open" >&2
exit 2
