#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: the folders of the starts, hades-diag/start-N on the boot partition: how they are numbered
# and whose they are. Read by start.sh, collect.sh, watch-dv.sh and shutdown.sh (D is /flash/hades-diag in all of
# them). Nothing of it runs by itself.
# The number of a start goes on from the start before it, also where the folders of the starts are gone: an update
# of the system that did not keep them, a new image written to the drive, folders deleted by hand. The last number
# is kept in /storage/.cache/hades-diag/last-start, which an update keeps, and a start is one more than it or than
# the highest folder start-N here, whichever is higher. So a name start-N is never given to two starts of one
# system, not even in a copy of the folder that was taken before an update. (The boot partition is FAT, which does
# not tell start-3 from START-3: a name in any letter case counts.)
# Each folder of a start has a note, start.txt: the number of the start, the build, when the system started, and
# a mark of that start (a random word, made once in /run, which every start begins empty). Before anything of a
# start is written into its folder (by collect.sh, watch-dv.sh and shutdown.sh, with the boot partition writable
# and the lock of its writers held), own_start makes sure that the folder is of this start: a folder of that name
# that is of another start, or of a version before this one (which wrote no note), is emptied first, and the
# note says what was there. So what is in a folder start-N is always of one start.
FC=/storage/.cache/hades-diag/last-start
FM=/run/hades-diag-mark
# What the file .filtered of a folder holds: the filter that the folder went through. 2: with the names of the
# Bluetooth devices. An empty file is of the filter before them: collect.sh puts such a folder through the filter
# again, and to-usb.sh leaves it out until then (it has FILTERED of its own: it does not read this file).
FILTERED=2

# The folders of the starts that are here, "<number> <name>" a line, by their numbers (start-07 is start 7)
start_folders() { ls $D 2>/dev/null | sed -n 's/^[Ss][Tt][Aa][Rr][Tt]-0*\([0-9]\{1,9\}\)$/\1 &/p' | sort -n; }

# The number of a new start, kept in storage at once (a start that writes nothing has its number all the same)
next_start() {
  local f c n
  f=$(start_folders | tail -n 1)
  c=$(sed -n '1s/^0*\([0-9]\{1,9\}\)$/\1/p' $FC 2>/dev/null)
  n=${f%% *}
  [ "${c:-0}" -gt "${n:-0}" ] && n=$c
  n=$((${n:-0} + 1))
  ( umask 077; mkdir -p ${FC%/*} && echo $n > $FC.$$ && mv -f $FC.$$ $FC ) 2>/dev/null || rm -f $FC.$$ 2>/dev/null
  echo $n
}

# The build, as os-release names it
this_build() { sed -n 's/^VERSION="*\([^"]*\)"*$/\1/p' /etc/os-release 2>/dev/null; }

# The mark of this start: made at random the first time that it is asked for, then the same for every script of
# the diagnostics until the system stops. Empty where it cannot be made: then nothing of the start is written.
start_mark() {
  [ -s $FM ] || {
    head -c 8 /dev/urandom 2>/dev/null | od -An -v -tx1 | tr -d ' \n' > $FM.$$ 2>/dev/null &&
      [ -s $FM.$$ ] && ln $FM.$$ $FM 2>/dev/null
    rm -f $FM.$$
  }
  cat $FM 2>/dev/null
}

# Whether $1 is the name of a folder of a start, start-N (and nothing else: no path)
start_name() {
  case "$1" in start-*) ;; *) return 1 ;; esac
  case "${1#start-}" in ''|*[!0-9]*) return 1 ;; esac
}

# Whether the folder $1 (start-N) is of this start
mine() {
  local m
  start_name "$1" || return 1
  m=$(start_mark)
  [ -n "$m" ] && [ "$(sed -n 's/^mark: //p' $D/$1/start.txt 2>/dev/null)" = "$m" ]
}

# The folder $1 (start-N) made this start's, before anything of the start goes into it: a folder of that name
# that is of another start is emptied first. Fails, and nothing is to be written, where there is no mark of this
# start, or the folder cannot be emptied or made.
own_start() {
  local m was up
  start_name "$1" || return 1
  m=$(start_mark)
  [ -n "$m" ] || return 1
  mine $1 && return 0
  was=
  if [ -e $D/$1 ]; then
    was=$(grep -E '^(start|build|started): ' $D/$1/start.txt 2>/dev/null | tr '\n' ';' | sed -e 's/;$//' -e 's/;/, /g')
    was=${was:-with no note: of a version before this one, or not of a start}
    rm -rf $D/$1
    [ -e $D/$1 ] && return 1
  fi
  up=$(cut -d. -f1 /proc/uptime 2>/dev/null)
  mkdir -p $D/$1 && {
    echo "start: ${1#start-}"
    echo "build: $(this_build)"
    echo "started: $(date -u -d @$(($(date +%s) - ${up:-0})) '+%F %T' 2>/dev/null) UTC"
    echo "mark: $m"
    if [ -n "$was" ]; then echo "emptied first: a folder $1 of another start was here ($was)"; fi
  } > $D/$1/start.txt && echo $FILTERED > $D/$1/.filtered
}
