#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Hades diagnostics: the filter that every log and every file of the diagnostics goes through before it is
# written to the boot partition, and that "Submit Log" uses. Nothing comes out of it unfiltered: where a
# part of it cannot be run or fails, what it prints is cut short and its exit status is not 0.
#   redact.sh                  a log, from standard input to standard output: the identifiers of the
#                              machine, its network and its people (redact-ids.awk: addresses, serial
#                              numbers, UUIDs, SSH keys, network names, the name of this system, the names
#                              of Bluetooth devices), then the credentials and the paths of addresses
#                              (redact.sed)
#   redact.sh ids              what the system says of itself (the kernel log, the network, the outputs and
#                              their properties): the identifiers only. redact.sed would take an EDID or a
#                              key ID printed in hex for a token, and a line of them for a blob
#   redact.sh edid <in> <out>  an EDID, without its serial numbers
#   redact.sh tree <in> <out>  a folder of the diagnostics into another, each file through what fits it:
#                              an EDID (edid/*.bin) as above, the archive of the ACPI tables as it is (the
#                              firmware's description of the machine: no text of the system), Kodi's logs and
#                              the journal through the whole filter (once more where they were made through
#                              it: a second pass changes nothing; the first one, for what an earlier version
#                              left), every other file through the identifiers. A file of <out> that is
#                              newer than its file of <in> is kept as it is
#   redact.sh btname <file>    the name and the alias of a Bluetooth device in a file of BlueZ, as the
#                              markers that the filter puts where they stand: "Name: [bt-name-...]"
# The names of the Bluetooth devices are those that BlueZ keeps in /storage/.cache/bluetooth: of each paired
# device (<adapter>/<device>/info), and of the devices that it saw when it looked for some, in its cache of names
# (<adapter>/cache/<device>; the 64 files of it written last). Where they cannot be read, the rest of the filter
# works as it does without them: BlueZ connects a paired device from that same file, and the kernel, logind and
# Kodi name only the input devices that are connected.
# A marker is the same for the same value on this system: the secret it is made with is kept in
# /storage/.cache/hades-diag/key, made at the first use, readable by root only, and never copied into the
# diagnostics. Where it cannot be kept (no storage), a secret of this run only is used, so that the markers
# are the same within what this run filters.
L=${0%/*}
K=${HADES_REDACT_KEY:-/storage/.cache/hades-diag/key}
export LC_ALL=C
umask 077
W=$(mktemp -d /run/hades-redact.XXXXXX 2>/dev/null || mktemp -d) || exit 1
trap 'rm -rf "$W"' EXIT
trap 'exit 1' INT TERM HUP

secret() { head -c 32 /dev/urandom | od -An -v -tx1 | tr -d ' \n'; }
# (made once and never replaced: of two filters that make it at the same time, the second keeps the first's)
if [ ! -s "$K" ] && mkdir -p "${K%/*}" 2>/dev/null; then
  secret > "$K.$$" 2>/dev/null && [ -s "$K.$$" ] && ln "$K.$$" "$K" 2>/dev/null
  rm -f "$K.$$"
fi
key=$K
[ -s "$key" ] || { key=$W/key; secret > "$key"; }
host=${HADES_REDACT_HOST-$(cat /proc/sys/kernel/hostname 2>/dev/null)}
BTS=${HADES_REDACT_BT:-/storage/.cache/bluetooth}
# The names of the Bluetooth devices, one a line, into $W/names (readable by root only, and gone with $W). The
# files of BlueZ are named by their addresses: no space in them. They are read one by one, each after a line of a
# part of its own, so that one that cannot be read leaves out its names only.
btnames() {
  set --
  for f in "$BTS"/*/*/info; do [ -f "$f" ] && set -- "$@" "$f"; done
  for f in $(ls -td "$BTS"/*/cache/* 2>/dev/null | head -n 64); do [ -f "$f" ] && set -- "$@" "$f"; done
  [ $# -gt 0 ] || return 0
  for f in "$@"; do echo '[hades-file]'; cat "$f" 2>/dev/null; echo; done | awk -v MODE=btlist -f "$L/redact-ids.awk" > "$W/names" 2>/dev/null
  return 0
}

# (a line end and a random word after the input, so that a last line that had no line end gets none: see
# emit() in redact-ids.awk)
last=hades-end-$(head -c 12 /dev/urandom | od -An -v -tx1 | tr -d ' \n')
ids() { { cat; printf '\n%s\n' "$last"; } | awk -v KEYFILE="$key" -v TMP="$W/h" -v HOST="$host" -v NAMES="$W/names" -v MODE="$1" -v LAST="$last" -f "$L/redact-ids.awk"; }
edid() {
  e=$(set -o pipefail; od -An -v -tx1 < "$1" | awk -v MODE=edid -f "$L/redact-ids.awk") || return 1
  printf "$e" > "$2"
}
tree() {
  rc=0
  mkdir -p "$2" || return 1
  for f in $(cd "$1" && find . -type f | sed 's|^\./||'); do
    # (a copy that this made after the file last changed is still the one: a snapshot is copied twice, and
    # most of its files do not change between the two. The times are told to the second: a file that
    # changed in the second of its copy is filtered again)
    [ "$2/$f" -nt "$1/$f" ] && continue
    case $f in */*) mkdir -p "$2/${f%/*}" ;; esac
    case $f in
      edid/*.bin) edid "$1/$f" "$2/$f" ;;
      *.tar.gz) cp "$1/$f" "$2/$f" ;;
      kodi*.log.gz|*/kodi*.log.gz|journal*.gz|*/journal*.gz) (set -o pipefail; gunzip -c "$1/$f" | ids log | sed -E -f "$L/redact.sed" | gzip > "$2/$f") ;;
      kodi-tail*|*/kodi-tail*) (set -o pipefail; ids log < "$1/$f" | sed -E -f "$L/redact.sed" > "$2/$f") ;;
      *.gz) (set -o pipefail; gunzip -c "$1/$f" | ids | gzip > "$2/$f") ;;
      *) ids < "$1/$f" > "$2/$f" ;;
    esac || { rm -f "$2/$f"; rc=1; }
  done
  return $rc
}

case "$1" in ''|ids|tree) btnames ;; esac
case "$1" in
  '') set -o pipefail; ids log | sed -E -f "$L/redact.sed" ;;
  ids) ids ;;
  edid) edid "$2" "$3" ;;
  tree) tree "$2" "$3" ;;
  btname) [ -r "$2" ] && awk -v KEYFILE="$key" -v TMP="$W/h" -v MODE=btmark -f "$L/redact-ids.awk" "$2" < /dev/null ;;
  *) echo "usage: $0 [ids | edid <in> <out> | tree <in> <out> | btname <file>]" >&2; exit 2 ;;
esac
