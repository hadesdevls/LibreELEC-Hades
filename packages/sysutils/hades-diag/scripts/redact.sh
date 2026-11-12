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
#                              an EDID (edid/*.bin, also in a folder below <in>: a folder of a start
#                              holds its snapshots, 1-minute/edid/...) as above, never as text (that turned each
#                              byte 0 into a line end, and left the serial number in it); a copy that an earlier
#                              version damaged so (its header 0a ff ff ff ff ff ff 0a) cannot be mended and is left
#                              out, with a note in its place. The archive of the ACPI tables as it is (the
#                              firmware's description of the machine: no text of the system), Kodi's logs and
#                              the journal (also those of a start of the installer, installer/) through the whole filter (once more where they were made through
#                              it: a second pass changes nothing; the first one, for what an earlier version
#                              left), every other file through the identifiers. A file of <out> that is
#                              newer than its file of <in> is kept as it is. Then <out>/filter.txt: what
#                              the filter left of the name of this system in all of <out> (hostnote)
#   redact.sh btname <file>    the name and the alias of a Bluetooth device in a file of BlueZ, as the
#                              markers that the filter puts where they stand: "Name: [bt-name-...]"
#   redact.sh hostnote <dir>   what the filter left of the name of this system in the files of a folder that
#                              went through it: the times it stands as a word of its own (none, where it is a
#                              name of its own: the last pass of redact-ids.awk takes it wherever it stands
#                              so) and inside longer words. Never the name itself
# With HADES_REDACT_DROP_UNENDED set, a last line of the input with no line end is left out (the input was cut
# while it was written, shutdown.sh); with HADES_REDACT_STATS set, what the filter did goes to standard error.
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
# (the digests that one file of a run made are kept in $W/dg for the files after it: see digest() in redact-ids.awk)
ids() { { cat; printf '\n%s\n' "$last"; } | awk -v KEYFILE="$key" -v TMP="$W/h" -v DGFILE="$W/dg" -v HOST="$host" -v NAMES="$W/names" -v MODE="$1" -v LAST="$last" -v DROPCUT="${HADES_REDACT_DROP_UNENDED:-}" -v STATS="${HADES_REDACT_STATS:-}" -f "$L/redact-ids.awk"; }
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
      edid/*.bin|*/edid/*.bin)
        edid "$1/$f" "$2/$f" && if [ ! -s "$2/$f" ]; then
          rm -f "$2/$f"
          echo "An earlier version of the diagnostics damaged this copy of the EDID (each byte 0 became a line end): it cannot be mended and is left out." > "$2/$f.left-out.txt"
        fi ;;
      *.tar.gz) cp "$1/$f" "$2/$f" ;;
      kodi*.log.gz|*/kodi*.log.gz|journal*.gz|*/journal*.gz) (set -o pipefail; gunzip -c "$1/$f" | ids log | sed -E -f "$L/redact.sed" | gzip > "$2/$f") ;;
      kodi-tail*|*/kodi-tail*|installer/journal.txt|*/installer/journal.txt|installer/install.txt|*/installer/install.txt) (set -o pipefail; ids log < "$1/$f" | sed -E -f "$L/redact.sed" > "$2/$f") ;;
      *.gz) (set -o pipefail; gunzip -c "$1/$f" | ids | gzip > "$2/$f") ;;
      *) ids < "$1/$f" > "$2/$f" ;;
    esac || { rm -f "$2/$f"; rc=1; }
  done
  # (Hades: what the filter left of the name of this system in all of <out>, for whoever reads the folder)
  hostnote "$2" > "$2/filter.txt" 2>/dev/null || rm -f "$2/filter.txt"
  return $rc
}
# Hades: what the filter left of the name of this system in the files of the folder $1: "<kind> <as a word of its
# own> <inside longer words>", the kind "word" (a name that the last pass takes wherever it stands as a word),
# "plain" (one that only the lines that name it as such lose) or "none" (no name to take). Each file whole, a log
# packed with gzip unpacked, with a line end after it; not the archive of the ACPI tables, the EDIDs (no text of
# the system) nor a note of this.
hostleft() {
  for f in $(cd "$1" && find . -type f | sed 's|^\./||'); do
    case $f in
      filter.txt|*/filter.txt|*.tar.gz|edid/*.bin|*/edid/*.bin) ;;
      *.gz) gunzip -c "$1/$f"; echo ;;
      *) cat "$1/$f"; echo ;;
    esac
  done | awk -v HOST="$host" -v MODE=hostleft -f "$L/redact-ids.awk"
}
hostnote() {
  set -- $(hostleft "$1")
  case "$1" in
    word) echo "The name of this system: $2 times left as a word of its own in the files of this folder (none is expected: the last pass of the filter takes it wherever it stands so), and $3 times inside a longer word (left as it is: such a word may be another one)." ;;
    plain) echo "The name of this system: a word that the logs use themselves, or a very short one, so it is taken only where a line names it as the name of the system (not counted here)." ;;
    none) echo "The name of this system: none to take (none is set, or it is the one that LibreELEC gives)." ;;
    *) return 1 ;;
  esac
}

case "$1" in ''|ids|tree) btnames ;; esac
case "$1" in
  '') set -o pipefail; ids log | sed -E -f "$L/redact.sed" ;;
  ids) ids ;;
  edid) edid "$2" "$3" ;;
  tree) tree "$2" "$3" ;;
  hostnote) hostnote "$2" ;;
  btname) [ -r "$2" ] && awk -v KEYFILE="$key" -v TMP="$W/h" -v MODE=btmark -f "$L/redact-ids.awk" "$2" < /dev/null ;;
  *) echo "usage: $0 [ids | edid <in> <out> | tree <in> <out> | btname <file> | hostnote <dir>]" >&2; exit 2 ;;
esac
