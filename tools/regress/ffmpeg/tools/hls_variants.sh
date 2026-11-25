#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# hls_variants.sh <bin> <work>: ffmpeg-9972 (the HLS demuxer loads one variant of a master playlist with
# load_all_variants=0, the one that max_bandwidth selects) with the ffmpeg and ffprobe of <bin>, built with
# AddressSanitizer. It writes a master playlist of two variants (320x240 at 200 kb/s with the audio group a1, 160x120
# at 100 kb/s with a2), each variant and each audio rendition a Media Playlist of one MPEG-TS segment, and reads it with
# ffprobe: what is loaded (the "Opening ... for reading" lines of the debug log), the programs and the size of the
# video. One line "ok: ..." or "FAIL: ..." per check, then ALL PASSED or SOME FAILED. Without ffmpeg-9972 ffprobe does
# not know the options, and the checks that use them fail.
set -u
B=$1 W=$2
fail=0
ok() { echo "ok: $*"; }
bad() { echo "FAIL: $*"; fail=1; }
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
# san <name> <what>: the debug log of that probe has no report of AddressSanitizer
san() {
  [ -f "$1.log" ] || { bad "$2: no log"; return 1; }
  grep -q 'ERROR: AddressSanitizer\|runtime error:' "$1.log" &&
    { bad "$2: AddressSanitizer: $(grep -m1 'ERROR: AddressSanitizer\|runtime error:' "$1.log")"; return 1; }
  return 0
}

# the media: two video variants and two audio renditions, one segment each
"$B/ffmpeg" -nostdin -v error -f lavfi -i testsrc=size=320x240:rate=25:duration=2 -c:v mpeg2video -b:v 150k -f mpegts v_hi.ts &&
"$B/ffmpeg" -nostdin -v error -f lavfi -i testsrc=size=160x120:rate=25:duration=2 -c:v mpeg2video -b:v 60k -f mpegts v_lo.ts &&
"$B/ffmpeg" -nostdin -v error -f lavfi -i sine=frequency=440:duration=2 -c:a mp2 -b:a 64k -f mpegts a1.ts &&
"$B/ffmpeg" -nostdin -v error -f lavfi -i sine=frequency=880:duration=2 -c:a mp2 -b:a 64k -f mpegts a2.ts ||
  { echo "FAIL: the media were not made"; echo "SOME FAILED"; exit 1; }
for m in v_hi v_lo a1 a2; do
  printf '#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:2.0,\n%s.ts\n#EXT-X-ENDLIST\n' $m > $m.m3u8
done
master() { # <file> <hi playlist> <resolution of hi or ->
  {
    printf '#EXTM3U\n'
    printf '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="a1",NAME="one",DEFAULT=YES,URI="a1.m3u8"\n'
    printf '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="a2",NAME="two",DEFAULT=YES,URI="a2.m3u8"\n'
    if [ "$3" = - ]; then printf '#EXT-X-STREAM-INF:BANDWIDTH=200000,AUDIO="a1"\n%s\n' "$2"
    else printf '#EXT-X-STREAM-INF:BANDWIDTH=200000,RESOLUTION=%s,AUDIO="a1"\n%s\n' "$3" "$2"; fi
    if [ "$3" = - ]; then printf '#EXT-X-STREAM-INF:BANDWIDTH=100000,AUDIO="a2"\nv_lo.m3u8\n'
    else printf '#EXT-X-STREAM-INF:BANDWIDTH=100000,RESOLUTION=160x120,AUDIO="a2"\nv_lo.m3u8\n'; fi
  } > "$1"
}
master master.m3u8 v_hi.m3u8 320x240
master missing.m3u8 v_gone.m3u8 320x240
master nores.m3u8 v_hi.m3u8 -
# a variant of lower resolution and higher bandwidth: the resolution decides first
{ printf '#EXTM3U\n'
  printf '#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=160x120\nv_lo.m3u8\n'
  printf '#EXT-X-STREAM-INF:BANDWIDTH=200000,RESOLUTION=320x240\nv_hi.m3u8\n'; } > reshigh.m3u8

# probe <name> <master> <options...>: ffprobe's debug log in <name>.log, its JSON in <name>.json, its exit in <name>.rc
probe() {
  local n=$1 m=$2; shift 2
  "$B/ffprobe" -v debug "$@" -show_programs -show_streams -of json "$m" > $n.json 2> $n.log
  echo $? > $n.rc
}
opened() { grep -c "Opening '[^']*$2' for reading" "$1.log"; }
programs() { python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(len(d.get("programs", [])))' "$1.json" 2> /dev/null || echo "-"; }
widths() { python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(" ".join(sorted({str(s["width"]) for s in d.get("streams", []) if s.get("codec_type") == "video"})))' "$1.json" 2> /dev/null || echo "-"; }
audios() { python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(sum(1 for s in d.get("streams", []) if s.get("codec_type") == "audio"))' "$1.json" 2> /dev/null || echo "-"; }
check() { # <name> <what> <got> <want>
  [ "$3" = "$4" ] && ok "$1: $2 $3" || bad "$1: $2 $3, $4 expected"
}

# 1. as before: every variant
probe all master.m3u8
if san all "every variant"; then
  check all "exit" "$(cat all.rc)" 0
  check all "programs" "$(programs all)" 2
  check all "v_lo.m3u8 opened" "$(opened all v_lo.m3u8)" 1
  check all "a2.m3u8 opened" "$(opened all a2.m3u8)" 1
fi
# 2. one variant, no limit: the highest resolution
probe one master.m3u8 -load_all_variants 0 -max_bandwidth 0
if san one "one variant"; then
  check one "exit" "$(cat one.rc)" 0
  check one "programs" "$(programs one)" 1
  check one "video width" "$(widths one)" 320
  check one "audio streams" "$(audios one)" 1
  check one "v_hi.m3u8 opened" "$(opened one v_hi.m3u8)" 1
  check one "v_lo.m3u8 opened" "$(opened one v_lo.m3u8)" 0
  check one "a1.m3u8 opened" "$(opened one a1.m3u8)" 1
  check one "a2.m3u8 opened" "$(opened one a2.m3u8)" 0
  check one "v_lo.ts opened" "$(opened one v_lo.ts)" 0
fi
# 3. a limit between the two: the lower one
probe mid master.m3u8 -load_all_variants 0 -max_bandwidth 150000
if san mid "within 150 kb/s"; then
  check mid "programs" "$(programs mid)" 1
  check mid "video width" "$(widths mid)" 160
  check mid "v_hi.m3u8 opened" "$(opened mid v_hi.m3u8)" 0
  check mid "a2.m3u8 opened" "$(opened mid a2.m3u8)" 1
fi
# 4. a limit below both: the one of the lowest bandwidth
probe low master.m3u8 -load_all_variants 0 -max_bandwidth 1000
if san low "within 1 kb/s"; then
  check low "programs" "$(programs low)" 1
  check low "video width" "$(widths low)" 160
fi
# 5. no RESOLUTION: the highest bandwidth within the limit
probe nores nores.m3u8 -load_all_variants 0 -max_bandwidth 0
if san nores "no resolution"; then
  check nores "video width" "$(widths nores)" 320
fi
# 6. the resolution before the bandwidth
probe reshigh reshigh.m3u8 -load_all_variants 0 -max_bandwidth 0
if san reshigh "resolution first"; then
  check reshigh "video width" "$(widths reshigh)" 320
fi
# 7. the variant chosen is missing: the open fails (Kodi opens it again with all variants); with all, it opens
probe gone missing.m3u8 -load_all_variants 0 -max_bandwidth 0
if san gone "the variant chosen missing"; then
  [ "$(cat gone.rc)" != 0 ] && grep -q "Invalid data found" gone.log && ok "gone: the open fails: exit $(cat gone.rc)" ||
    bad "gone: exit $(cat gone.rc), a failed open (Invalid data found) expected"
fi
probe goneall missing.m3u8
if san goneall "the variant missing, all variants"; then
  check goneall "exit" "$(cat goneall.rc)" 0
  check goneall "video width" "$(widths goneall)" 160
fi
[ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
