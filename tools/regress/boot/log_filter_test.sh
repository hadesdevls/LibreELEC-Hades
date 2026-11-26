#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of the filter of the diagnostics for a log in a file (packages/sysutils/hades-diag/scripts/redact.sh file
# <in> <out>), which the share Logfiles uses (report.sh share): a big log is cut into parts that go through the filter
# at once, and what comes out must be, byte for byte, what the filter makes of the same log from standard input. A log
# of Kodi made up here has what the filter takes out (addresses of the network and MAC addresses, UUIDs, paths of a
# drive, an address with a token, the name of the system, a password) and what it holds across lines: rows of hex of
# an EDID with its serial number (hexdump's form, as pastekodi prints one), a section of Samba's configuration with its
# path, lines of the journal, a line with NUL bytes in it. The busybox of the image runs it, with the parts forced
# (HADES_REDACT_PARTS, HADES_REDACT_PART_MIN) so that a log of 2 MB is cut as one of hundreds of MB is.
#
# Usage: log_filter_test.sh <busybox> <the folder of redact.sh, redact-ids.awk and redact.sed>
# Then each of its mutations (a copy of redact.sh with one rule taken out) must fail at least one check: "mutation Mn
# (...) caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when it could not run.
set -u
BBI=${1:-}; SD=${2:-}
[ -x "$BBI" ] && [ -f "$SD/redact.sh" ] && [ -f "$SD/redact-ids.awk" ] && [ -f "$SD/redact.sed" ] ||
  { echo "usage: $0 <busybox> <folder of redact.sh>"; exit 2; }
T=$(mktemp -d); trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
mkdir -p "$T/bb" "$T/bt"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
FAILS=0
check() { if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi; }

# the log: blocks of a line of Kodi, the rows of an EDID (128 bytes, serial number 0x01020304 and the string
# SERIAL12345), lines with addresses, a section of Samba with its path, lines of the journal
python3 -I - "$T/kodi.log" <<'PY'
import sys
edid = bytearray(128)
edid[0:8] = b'\x00\xff\xff\xff\xff\xff\xff\x00'
edid[8:10] = b'\x1e\x6d'; edid[10:12] = b'\x01\x00'; edid[12:16] = b'\x04\x03\x02\x01'
edid[16] = 1; edid[17] = 33; edid[18] = 1; edid[19] = 3
d = 54
edid[d:d + 18] = bytes([0, 0, 0, 0xff, 0]) + b'SERIAL12345\n' + b' '
edid[127] = (-sum(edid[:127])) % 256
rows = [' '.join(f'{b:02x}' for b in edid[i:i + 16]) for i in range(0, 128, 16)]
out = []
for n in range(9000):
    t = f'2026-11-25 19:{(n // 60) % 60:02d}:{n % 60:02d}.{n % 1000:03d} T:{1600 + n % 7}'
    out.append(f'{t}    debug <general>: ffmpeg[0x7f]: [mov] stream 0, sample {n}, dts {n * 1001}')
    k = n % 9
    if k == 0:
        out.append(f'{t}     info <general>: EDID of the display:')
        out += rows
    elif k == 1:
        out.append(f'{t}     info <general>: connected to 192.168.1.{n % 250 + 2}, gateway 10.0.{n % 200}.1, mac 00:1a:2b:{n % 256:02x}:4d:5e')
    elif k == 2:
        out.append(f'{t}     info <general>: VideoPlayer::OpenFile: /var/media/Juan Disk/Films/Some Film {n}/film.mkv')
    elif k == 3:
        out.append(f'{t}     info <general>: CCurlFile::Open https://api.example.com/v1/stream?token=abcdef{n:06d}&user=x')
    elif k == 4:
        out.append('[Juan Disk]')
        out.append('  path = /media/Juan Disk')
    elif k == 5:
        out.append(f'Nov 25 19:{(n // 60) % 60:02d}:{n % 60:02d} myhtpc kernel: usb 1-{n % 9}: SerialNumber: ABC{n:08d}')
    elif k == 6:
        out.append(f'{t}     info <general>: partition UUID 1b4e28ba-2fa1-11d2-883f-{n:012d}, host myhtpc, password=hunter{n}')
    elif k == 7:
        out.append(f'{t}     info <general>: a line with a NUL \x00 byte in it {n}')
    else:
        out.append(f'{t}  warning <general>: Skipped 1 duplicate messages..')
open(sys.argv[1], 'w', encoding='latin-1', newline='\n').write('\n'.join(out) + '\n')
PY
grep -a -c '' "$T/kodi.log" > /dev/null || { echo "the log could not be made"; exit 2; }
printf 'no line here starts with a time\n%.0s' $(seq 1 30000) > "$T/plain.log"

# rf <folder> <args...>: redact.sh of that folder, as root runs it, with a key of its own and the name of the system
rf() {
  local sd=$1; shift
  PATH="$T/bb" LC_ALL=C HADES_REDACT_KEY="$T/key" HADES_REDACT_HOST=myhtpc HADES_REDACT_BT="$T/bt" \
    HADES_REDACT_PART_MIN=65536 /usr/bin/timeout 300 "$T/bb/sh" "$sd/redact.sh" "$@"
}
printf '%s\n' 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef > "$T/key"

cases() {
  local sd=$1 n rc
  rm -f "$T/whole" "$T/parts" "$T/err"
  echo "1. a log of Kodi in four parts: the same bytes as from standard input"
  rf "$sd" < "$T/kodi.log" > "$T/whole"; check "1a from standard input" $?
  HADES_REDACT_PARTS=4 HADES_REDACT_STATS=1 rf "$sd" file "$T/kodi.log" "$T/parts" 2> "$T/err"; rc=$?
  n=$(grep -c '^redact-ids: ' "$T/err")
  [ $rc = 0 ] && [ "$n" = 4 ]; check "1b in a file: exit 0, four parts (runs of awk: $n)" $?
  cmp -s "$T/whole" "$T/parts"; check "1c byte for byte the same" $?
  ! grep -a -q -e '53 45 52 49 41 4c 31' -e '04 03 02 01' -e '192\.168\.1\.' -e 'Juan Disk' -e 'hunter' -e 'abcdef0' -e myhtpc "$T/parts" &&
    [ "$(grep -a -c '^00 ff ff ff ff ff ff 00' "$T/parts")" = "$(grep -a -c '^00 ff ff ff ff ff ff 00' "$T/kodi.log")" ]
  check "1d nothing of what is taken out is left, every EDID kept as one" $?
  [ -z "$(ls "$T" | grep -F 'parts.part.')" ]; check "1e no part left beside it" $?
  echo "2. a log with no line that starts with a time: one part, the same bytes"
  rf "$sd" < "$T/plain.log" > "$T/whole2"
  HADES_REDACT_PARTS=4 HADES_REDACT_STATS=1 rf "$sd" file "$T/plain.log" "$T/parts2" 2> "$T/err"; rc=$?
  [ $rc = 0 ] && [ "$(grep -c '^redact-ids: ' "$T/err")" = 1 ] && cmp -s "$T/whole2" "$T/parts2"
  check "2 one part" $?
  echo "3. parts that cannot be written: no file, exit status not 0, nothing left"
  mkdir -p "$T/ro"; chmod 0555 "$T/ro"
  HADES_REDACT_PARTS=4 rf "$sd" file "$T/kodi.log" "$T/ro/out" 2> /dev/null; rc=$?
  [ $rc != 0 ] && [ -z "$(ls -A "$T/ro")" ]; check "3 exit $rc, nothing in the folder" $?
  chmod 0755 "$T/ro"
  echo "4. a part of the filter that fails (redact.sed not readable after the cuts are made): no file, exit status not 0"
  cp -r "$sd" "$T/sdfail"; chmod 0755 "$T/sdfail"
  sed -i 's|^  wait$|  chmod 000 "$L/redact.sed"; wait|' "$T/sdfail/redact.sh"
  # (as root, chmod 000 would not keep it from being read: the test runs as a user)
  HADES_REDACT_PARTS=4 rf "$T/sdfail" file "$T/kodi.log" "$T/out4" 2> /dev/null; rc=$?
  [ $rc != 0 ] && [ ! -e "$T/out4" ] && [ -z "$(ls "$T" | grep -F 'out4.part.')" ]; check "4 exit $rc, no file" $?
  chmod -R u+rwX "$T/sdfail"; rm -rf "$T/sdfail" "$T/out4"
}

echo "redact.sh: $SD"
cases "$SD"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

MUT_MISSED=0
mutation() {
  local m=$T/mut-$1
  rm -rf "$m"; cp -r "$SD" "$m"
  sed -i -e "$3" "$m/redact.sh"
  if cmp -s "$m/redact.sh" "$SD/redact.sh"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  cases "$m" > "$T/mut-$1.log" 2>&1
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
  rm -rf "$m"
}
mutation 1 "a cut before any line: the awk takes any" 's,^      /^\[0-9\]\[0-9\]\[0-9\]\[0-9\]-.* ||$,      1 ||,'
mutation 2 "a cut counted one byte short" 's|^      { off += length($0) + 1 }.)$|      { off += length($0) }'"'"')|'
mutation 3 "the parts joined in another order" 's|^      cat "$out.part.$i" >> "$out" .. rc=1$|      cat "$out.part.$((c - i + 1))" >> "$out" \|\| rc=1|'
mutation 4 "a part that failed not looked at" 's|^  while \[ "$i" -le "$c" \]; do \[ "$(cat "$W/rc.$i" 2>/dev/null)" = 0 \] .. rc=1; i=$((i + 1)); done$|  :|'
mutation 5 "always one part" 's|^  \[ "$n" -gt 8 \] \&\& n=8$|  n=1|'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "LOGFILTER PASSED"; exit 0; }
echo "LOGFILTER FAILED: $TREE_FAILS check(s) failed, $MUT_MISSED mutation(s) not caught"
exit 1
