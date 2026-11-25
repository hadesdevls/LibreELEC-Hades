#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: edid-path.awk of hades-diag, with the image's busybox (od and awk): what the report says under each connector
# of where its EDID comes from, on the EDIDs of an LG C5 read direct (4.0.0.0, Dolby Vision v2, no DTS) and through a
# TCL sound bar (2.2.0.0, v1, DTS and DTS-HD), of an AV receiver and a TV of the linuxhw database; that collect.sh runs
# it for each EDID it keeps and that package.mk installs it.
# usage: edid_path_diag_test.sh <busybox> <hades-diag folder> <c5 direct> <c5 sound bar> <receiver> <tv>
BB=$1 D=$2
fails=0
say() { $BB od -An -v -tu1 "$1" | $BB awk -f $D/scripts/edid-path.awk; }
expect() { # <what> <file> <text it must have>...
  local what=$1 f=$2 out t; shift 2
  out=$(say "$f")
  echo "$what: $out"
  for t in "$@"; do
    case "$out" in *"$t"*) ;; *) echo "FAIL: $what: no '$t'"; fails=$((fails + 1)) ;; esac
  done
}
expect "c5 direct" "$3" "hdmi address 4.0.0.0: on an input of the display itself" "dolby vision block: v2" \
  "sound: TrueHD, DD+, AC-3, PCM 8 ch"
expect "c5 through the bar" "$4" "hdmi address 2.2.0.0: through 1 device (a sound bar" "dolby vision block: v1" \
  "sound: TrueHD, DD+, DTS-HD, DTS, AC-3, PCM 8 ch"
expect "receiver" "$5" ": through 1 device"
expect "tv" "$6" ": on an input of the display itself"
case "$(say "$3")" in *DTS*) echo "FAIL: the C5 direct declares no DTS"; fails=$((fails + 1)) ;; esac
# an EDID without an extension: nothing said; one whose address is F.F.F.F or 2.0.1.0: said to say nothing
T=$(mktemp -d)
head -c 128 "$3" > $T/base.bin
[ -z "$(say $T/base.bin)" ] || { echo "FAIL: a base block alone says something"; fails=$((fails + 1)); }
for a in "255 255:hdmi address none" "32 16:says nothing of where the PC is" "33 64:through 2 devices"; do
  bytes=${a%%:*} want=${a#*:}
  { $BB od -An -v -tu1 "$3" | tr -s ' \n' '\n' | grep -v '^$'; } > $T/b.txt
  # the HDMI VSDB of the C5's EDID: its address replaced (the bytes after 3 12 0)
  python3 - "$T/b.txt" "$bytes" > $T/m.txt <<'PY'
import sys
b = [int(x) for x in open(sys.argv[1]).read().split()]
hi, lo = (int(x) for x in sys.argv[2].split())
for i in range(len(b) - 5):
    if b[i:i + 3] == [3, 12, 0]:
        b[i + 3], b[i + 4] = hi, lo
        break
print(' '.join(map(str, b)))
PY
  out=$($BB awk -f $D/scripts/edid-path.awk < $T/m.txt)
  case "$out" in *"$want"*) echo "address $bytes: $out" ;; *) echo "FAIL: address $bytes: $out"; fails=$((fails + 1)) ;; esac
done
rm -rf $T
grep -q 'awk -f /usr/lib/hades-diag/edid-path.awk' $D/scripts/collect.sh || { echo "FAIL: collect.sh does not run it"; fails=$((fails + 1)); }
grep -q 'scripts/edid-path.awk' $D/package.mk || { echo "FAIL: package.mk does not install it"; fails=$((fails + 1)); }
grep -q 'hades-check/share.txt' $D/scripts/collect.sh || { echo "FAIL: collect.sh does not take the line to share"; fails=$((fails + 1)); }
[ $fails = 0 ] && echo "EDID-PATH-DIAG PASSED" || { echo "EDID-PATH-DIAG FAILED: $fails"; exit 1; }
