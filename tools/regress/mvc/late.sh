#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# (builder, no network) finding 7: the second view reaching the decoder late , with the
# decoder of the given twin tree: one thread, 2 and 8 frame threads, 8 slice threads, against the decode of the
# whole stream. Usage: late.sh <twin> [copies] [lost]
set -uo pipefail
export LC_ALL=C
TW=${1:-ship}
. "$(dirname "$0")/env.sh"
T=$W/ffmpeg/$TW/ffmpeg-9.0.2
O=$W/ffmpeg/late-$TW; rm -rf $O; mkdir -p $O
cp $W/streams/sd540-wrap.264 $O/
COPIES=${2:-3}
LOST=${3:-25}
cd $O
python3 -I $TOOLS/late.py sd540-wrap.264 $COPIES 0 full.264 < /dev/null
python3 -I $TOOLS/late.py sd540-wrap.264 $COPIES $LOST late.264 < /dev/null
dec() { # stream, threads, type, view, out
  timeout -s KILL 300 $T/ffmpeg -nostdin -nostats -hide_banner -v warning -noauto_conversion_filters -threads $2 -thread_type $3 -c:v h264_mvc -i $1 \
    -map 0:vidx:$4 -f rawvideo -pix_fmt yuv420p -y $5 < /dev/null 2> $5.err
  echo "  $(basename $5): $(( $(stat -c %s $5 2> /dev/null || echo 0) / 777600 )) pictures; said: $(grep -v 'NALU (type 14)\|Last message repeated\|probesize' $5.err | sed 's/^\[[^]]*\] //' | sort | uniq -c | head -n 3 | tr -s ' ' | tr '\n' ';' | cut -c1-200)"
}
echo "--- the whole stream"
dec full.264 1 frame 0 full-b.yuv
dec full.264 1 frame 1 full-d.yuv
echo "--- the second view late"
for m in 1:frame 2:frame 8:frame 8:slice; do
  dec late.264 ${m%%:*} ${m##*:} 1 late-${m%%:*}${m##*:}-d.yuv
done
dec late.264 8 frame 0 late-8frame-b.yuv
python3 -I - $LOST <<'PY'
import hashlib, sys
n = 960 * 540 * 3 // 2
lost = int(sys.argv[1])
def pics(name):
    try:
        b = open(name, "rb").read()
    except OSError:
        return []
    return [hashlib.md5(b[i:i + n]).hexdigest() for i in range(0, len(b) - n + 1, n)]
full_d, full_b = pics("full-d.yuv"), pics("full-b.yuv")
p = pics("late-8frame-b.yuv")
print("base view, 8 frame threads, against the whole decode: %d pictures, %d differ" % (len(p), sum(1 for a, b in zip(p, full_b) if a != b) + abs(len(p) - len(full_b))))
verdict = []
for name in ("late-1frame-d.yuv", "late-2frame-d.yuv", "late-8frame-d.yuv", "late-8slice-d.yuv"):
    p = pics(name)
    # from unit `lost` on, the second view must be the whole decode's, unit for unit (the last pictures out)
    tail_ref = full_d[lost:]
    tail = p[-len(tail_ref):] if len(p) >= len(tail_ref) else p
    bad = sum(1 for a, b in zip(tail, tail_ref) if a != b) + abs(len(tail) - len(tail_ref))
    verdict.append(bad)
    print("%s: %d pictures of the second view; from unit %d on, %d of %d are not the whole decode's" % (name, len(p), lost, bad, len(tail_ref)))
print("LATE %s" % ("PASSED" if not any(verdict) else "FAILED"))
PY
rm -f *.yuv
