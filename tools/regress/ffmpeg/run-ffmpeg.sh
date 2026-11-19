#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-ffmpeg.sh: the FFmpeg patches of the image that change what FFmpeg reads, checked on the host with FFmpeg of the
# recipe (lib-ffmpeg.sh):
#   fate      FATE, the test suite of FFmpeg, on what the patches change: ac3/eac3 (ffmpeg-9945, 9946), IMF (9934),
#             Matroska (9932-01, 9940, 9950), FLV (9932-02), Ogg (9932-03 to 05), the coded bitstreams and LCEVC
#             (9932-06 to 08), CDXL (9933-01), G2M (9933-02), Sun raster (9933-03, 06), PCX (9933-04), HuffYUV
#             (9933-05); every test of each that can run with the samples of inputs-ffmpeg.tsv passes, and those that
#             cannot are named. The two references of ffmpeg-9946 must give the new output (and the old _v2 ones a
#             difference), and the reference of the IMF unit test of before ffmpeg-9934 must fail.
#   playback  ffmpeg-9930 (an enhancement track of Dolby Vision in an 'hvc1'/'hev1' sample entry of MP4 grouped) and
#             ffmpeg-9931 (an HEVC picture whose parameter sets came in a PES without a time takes the time of the PES
#             of its first slice), each against FFmpeg without it: what changes, and that nothing else does; and
#             Kodi's join of two tracks (DVLayerMerge.cpp) with the module of the layer on the 'hvc1'/'hev1' files.
# Host test only: it builds and runs everything under $REGRESS_WORK/ffmpeg.
# usage: run-ffmpeg.sh all | verdict [<log>] | <stage>...       stages: inputs build fate playback
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
. "$HERE/lib-ffmpeg.sh"
rg_init ffmpeg
IN=$RG_W/in
FS=$IN/fate-suite
SM=$IN/samples
F=$RG_W/fate
V=$RG_W/ff
P=$RG_W/playback
# the FATE tests of each group: name : the patches whose files they exercise : ERE of the test names
FGROUPS="ac3:9945+9946:^fate-(ac3|eac3)-
imf:9934:^fate-imf
matroska:9932-01+9940+9950:^fate-(lavf-mkv|matroska|mkv)
flv:9932-02:^fate-(lavf-flv|flv)
ogg:9932-03+04+05:^fate-(lavf-ogg|ogg)
cbs-lcevc:9932-06+07+08:^fate-(cbs-(av1|h264|hevc|h265|h266|vvc|mpeg2|lcevc)-|lcevc)
cdxl:9933-01:^fate-cdxl
g2meet:9933-02:^fate-g2m
sunrast:9933-03+06:^fate-(lavf-sun|lavf-sunrast|sunraster)
pcx:9933-04:^fate-(lavf-pcx|pcx)
huffyuv:9933-05:(huffyuv|ffvhuff)"

stage_inputs() {
  if rg_inputs "$HERE/inputs-ffmpeg.tsv" "$IN" > "$RG_W/logs/inputs.log" 2>&1; then
    rg_item inputs PASSED "$(tail -n 1 "$RG_W/logs/inputs.log")"
  else
    rg_item inputs FAILED "$(grep -v '^INPUTS ' "$RG_W/logs/inputs.log" | head -n 3 | tr '\n' ' ')"
    return 1
  fi
}

# the builds: FFmpeg for FATE, and FFmpeg for the host as the bridge's (RG_FF_BRIDGE) without ffmpeg-9930 and 9931
# ("before"), without 9931 ("after") and with all ("after2": rg_ff_bridge itself)
stage_build() {
  local L=$RG_W/logs n v
  rg_log "$L/build-fate.log" ff_fate "$F" "$FS"
  v=$(rg_pass "$L/build-fate.log" '^FF: (built for FATE|.*already built)' '^FF: (PATCH FAILED|CONFIGURE FAILED|BUILD FAILED|sha256|no )')
  rg_item build-fate "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 '^FFSRC' "$L/build-fate.log" | cut -c7-)"
  n=$(find "$F/src" -newer "$F/src.mark" 2> /dev/null | wc -l)
  if [ ! -f "$F/src.mark" ]; then rg_item source-untouched FAILED "no mark of the start of the build"
  elif [ "$n" = 0 ]; then rg_item source-untouched PASSED "nothing in the source is newer than the start of the build (built out of its tree)"
  else rg_item source-untouched FAILED "$n paths of the source written by the build: $(find "$F/src" -newer "$F/src.mark" | head -n 3 | tr '\n' ' ')"; fi
  # shellcheck disable=SC2086
  rg_log "$L/build-before.log" ff_variant "$V/before" '^ffmpeg-993[01]-' $RG_FF_BRIDGE
  # shellcheck disable=SC2086
  rg_log "$L/build-after.log" ff_variant "$V/after" '^ffmpeg-9931-' $RG_FF_BRIDGE
  rg_log "$L/build-after2.log" rg_ff_bridge
  for n in before after after2; do
    v=$(rg_pass "$L/build-$n.log" '^(FF: |/)' '^FF: (PATCH FAILED|BUILD FAILED|sha256|no )')
    rg_item "build-$n" "$(rg_say "$v")" "$(rg_rest "$v")$(grep -a -m1 '^FFSRC' "$L/build-$n.log" | sed 's/^FFSRC/;/')"
  done
}
ffbin() { case $1 in after2) echo "$RG_SHARED/ff-bridge/prefix/bin" ;; *) echo "$V/$1/prefix/bin" ;; esac; }

# ------------------------------------------------------------------------------------------------- FATE
stage_fate() {
  local B=$F/build W=$RG_W/fate-run g p re ts t r pass fail nos nor tot v
  [ -x "$B/ffmpeg" ] || { for g in $(printf '%s\n' "$FGROUPS" | cut -d: -f1) ac3-new-refs ac3-old-refs-fail imf-old-ref-fails; do
    rg_item "$g" FAILED "no FFmpeg built for FATE"; done; return 1; }
  rg_fresh "$W"
  make -C "$B" fate-list < /dev/null 2> /dev/null | grep '^fate-' | sort -u > "$W/list-all.txt"
  make -C "$B" fate-list SAMPLES= < /dev/null 2> /dev/null | grep '^fate-' | sort -u > "$W/list-nosamples.txt"
  printf '%s\n' "$FGROUPS" | while IFS=: read -r g p re; do grep -E "$re" "$W/list-all.txt" | sed "s/^/$g /"; done > "$W/selected.txt"
  awk '{print $2}' "$W/selected.txt" | sort -u > "$W/tests.txt"
  echo "FATE: $(wc -l < "$W/list-all.txt") tests with the samples, $(wc -l < "$W/list-nosamples.txt") without; $(wc -l < "$W/tests.txt") selected"
  # shellcheck disable=SC2046
  rg_log "$RG_W/logs/fate.log" nice -n 19 make -C "$B" -k -j"$RG_JOBS" $(cat "$W/tests.txt")
  # each test: its .rep (field 2: 0 = passed). One that did not pass is NOSAMPLE only when FATE itself says it needs
  # samples and a sample is not there: one that its recipe names, or one that FFmpeg itself named in the test's error
  # output as a file it could not open (the files an IMF composition playlist leads to: its ASSETMAP.xml and track files,
  # which the recipe does not name); otherwise FAIL (it ran and differs) or NORUN (no .rep)
  missing() {
    { make -C "$B" -n "$1" < /dev/null 2> /dev/null
      cat "$B/tests/data/fate/${1#fate-}.err" 2> /dev/null; } | grep -o "$FS/[^ '\",;)]*" | sed 's/[:.]*$//' | sort -u | while read -r f; do
      case $f in *%*) ls "$(dirname "$f")"/* > /dev/null 2>&1 || echo "${f#"$FS"/}" ;; *) [ -e "$f" ] || echo "${f#"$FS"/}" ;; esac
    done
  }
  res() {
    local rep=$B/tests/data/fate/${1#fate-}.rep
    if [ -f "$rep" ] && [ "$(cut -d: -f2 "$rep")" = 0 ]; then echo PASS; return; fi
    if ! grep -q -x "$1" "$W/list-nosamples.txt" && [ -n "$(missing "$1")" ]; then echo NOSAMPLE; return; fi
    [ -f "$rep" ] && echo FAIL || echo NORUN
  }
  while read -r t; do echo "$t $(res "$t")"; done < "$W/tests.txt" > "$W/results.txt"
  awk '$2 == "NOSAMPLE" {print $1}' "$W/results.txt" | while read -r t; do echo "$t: $(missing "$t" | tr '\n' ' ')"; done \
    > "$RG_W/logs/missing-samples.txt"
  cp "$W/results.txt" "$RG_W/logs/fate-results.txt"
  echo "FATE results: $(awk '{print $2}' "$W/results.txt" | sort | uniq -c | tr '\n' ' '); missing samples: $RG_W/logs/missing-samples.txt"
  while IFS=: read -r g p re; do
    ts=$(awk -v g="$g" '$1 == g {print $2}' "$W/selected.txt")
    [ -n "$ts" ] || { rg_item "$g" FAILED "no FATE test of the build matches /$re/ (the tests of what $p changes)"; continue; }
    pass=0; fail=""; nos=""; nor=""
    for t in $ts; do
      r=$(awk -v t="$t" '$1 == t {print $2}' "$W/results.txt")
      case $r in PASS) pass=$((pass + 1)) ;; FAIL) fail="$fail ${t#fate-}" ;; NOSAMPLE) nos="$nos ${t#fate-}" ;; *) nor="$nor ${t#fate-}" ;; esac
    done
    tot=$(echo $ts | wc -w)
    if [ -n "$fail" ] || [ -n "$nor" ]; then
      rg_item "$g" FAILED "$pass of $tot passed;${fail:+ failed:$fail;}${nor:+ did not run:$nor;}${nos:+ no sample:$nos} ($p)"
    elif [ $pass = 0 ]; then
      rg_item "$g" NOT-RUN "all $tot need samples that inputs-ffmpeg.tsv does not have ($p): $(echo $nos | cut -c1-200)"
    else
      rg_item "$g" PASSED "$pass of $tot passed ($p)${nos:+; for want of a sample, not run:$(echo $nos | wc -w) (logs/missing-samples.txt)}"
    fi
  done < <(printf '%s\n' "$FGROUPS")
  # ffmpeg-9946: the new references (_v3, which the tree's ac3.mak names) pass; with the old ones (_v2) the same tests
  # differ in their output (FATE's MAXDIFF), so the references tell the two apart
  local nr x
  nr=""
  for t in fate-ac3-2.0 fate-ac3-4.0 fate-ac3-fixed-2.0; do nr="$nr ${t#fate-}:$(awk -v t="$t" '$1 == t {print $2}' "$W/results.txt")"; done
  case "$nr" in *FAIL*|*NO*|*:\ *) v=FAILED ;; *) v=PASSED ;; esac
  [ "$(echo "$nr" | grep -o ':PASS' | wc -l)" = 3 ] || v=FAILED
  rg_item ac3-new-refs $v "$nr (with the _v3 references that the tree's tests/fate/ac3.mak names)"
  old() {
    local x
    for x in "fate-ac3-2.0 monsters_inc_2.0_192_small_v2.pcm" "fate-ac3-4.0 millers_crossing_4.0_v2.pcm" "fate-ac3-fixed-2.0 monsters_inc_2.0_192_small_v2.pcm"; do
      set -- $x
      [ -s "$FS/ac3/$2" ] || { echo "$1: the old reference $2 is not there"; continue; }
      make -C "$B" "$1" REF="$FS/ac3/$2" < /dev/null > "$W/old-$1.log" 2>&1
      echo "$1 with the old $2: make exit $?; $(grep -m1 '^MAXDIFF' "$W/old-$1.log")"
    done
  }
  rg_log "$RG_W/logs/ac3-old-refs.log" old
  x=$(grep -c -E ': make exit [1-9]; MAXDIFF: \|[1-9][0-9]* - 0\| >= 1' "$RG_W/logs/ac3-old-refs.log")
  if [ "$x" = 3 ]; then rg_item ac3-old-refs-fail PASSED "the three differ from the _v2 references: $(grep -o -E '^fate-[^ ]+|MAXDIFF: \|[0-9]+' "$RG_W/logs/ac3-old-refs.log" | paste -sd' ' -)"
  else rg_item ac3-old-refs-fail FAILED "$x of 3 differ from the _v2 references by their output: $(grep -E 'make exit|not there' "$RG_W/logs/ac3-old-refs.log" | tr '\n' ';' | cut -c1-200)"; fi
  # ffmpeg-9934: the reference of the IMF unit test before it (its hunk of tests/ref/fate/imf reversed on a copy) fails,
  # on the lines the patch adds
  local P9934 T=$W/imf-old
  P9934=$(git -C "$REGRESS_LE_GIT" ls-tree --name-only "$REGRESS_LE_REF" projects/Generic/patches/ffmpeg/ | grep -m1 '/ffmpeg-9934-')
  imfold() {
    mkdir -p "$T/tests/ref/fate"
    cp "$F/src/tests/ref/fate/imf" "$T/tests/ref/fate/imf" || return 1
    git -C "$REGRESS_LE_GIT" show "$REGRESS_LE_REF:$P9934" | sed -n '/^diff --git a\/tests\/ref\/fate\/imf b\/tests\/ref\/fate\/imf$/,$p' > "$T/ref.patch"
    [ -s "$T/ref.patch" ] || { echo "ffmpeg-9934 has no hunk of tests/ref/fate/imf"; return 1; }
    ( cd "$T" && patch -R -p1 -F0 --no-backup-if-mismatch < ref.patch ) || { echo "the hunk of ffmpeg-9934 does not reverse"; return 1; }
    echo "the reference of before ffmpeg-9934: $(wc -l < "$T/tests/ref/fate/imf") lines; the tree's: $(wc -l < "$F/src/tests/ref/fate/imf")"
    make -C "$B" fate-imf REF="$T/tests/ref/fate/imf" < /dev/null > "$W/old-imf.log" 2>&1
    echo "fate-imf with the reference of before ffmpeg-9934: make exit $?"
    cut -d: -f3 "$B/tests/data/fate/imf.rep" 2> /dev/null | base64 -d 2> /dev/null | grep -E '^[-+](Parse asset map|	Kept)' | head -n 3
    # (the build's own run of fate-imf is left as FATE leaves it: run again with the tree's reference)
    make -C "$B" fate-imf < /dev/null > "$W/imf-again.log" 2>&1; echo "fate-imf with the tree's reference again: make exit $?"
  }
  if [ "$(awk '$1 == "fate-imf" {print $2}' "$W/results.txt")" = PASS ] && [ -n "$P9934" ]; then
    rg_log "$RG_W/logs/imf-old-ref.log" imfold
    if grep -q -E '^fate-imf with the reference of before ffmpeg-9934: make exit [1-9]' "$RG_W/logs/imf-old-ref.log" &&
       grep -q '^+Parse asset map with paths that leave the package' "$RG_W/logs/imf-old-ref.log" &&
       grep -q -E "^fate-imf with the tree's reference again: make exit 0" "$RG_W/logs/imf-old-ref.log"; then
      rg_item imf-old-ref-fails PASSED "fate-imf passes with the tree's reference and fails with the one of before ffmpeg-9934, on the lines the patch adds"
    else rg_item imf-old-ref-fails FAILED "$(grep -E 'make exit|does not|no hunk' "$RG_W/logs/imf-old-ref.log" | tr '\n' ';' | cut -c1-200)"; fi
  else
    rg_item imf-old-ref-fails FAILED "fate-imf did not pass ($(awk '$1 == "fate-imf" {print $2}' "$W/results.txt")), or no ffmpeg-9934 in the tree"
  fi
}

# ------------------------------------------------------------------------------------------------- playback
# mov: ffmpeg-9930. The 'hvc1'/'hev1' files are made from the 'dvh1'/'dvhe' ones (tools/mp4-entry.py), and the negative
# ones from them (tools/mp4-edit2.py): which files get a Dolby Vision stream group before and after, and every stream,
# group and packet (with an MD5 of its data) of every MP4 file the same before and after but the group of the two
# 'hvc1'/'hev1' files.
mov() {
  local Bb Ab M=$P/samples L=$P/mov fail=0 f n
  Bb=$(ffbin before); Ab=$(ffbin after)
  mkdir -p "$M" "$L"
  mk() { [ -f "$M/$1" ] || $RG_PY "$HERE/tools/$2" "${@:3}" > /dev/null || { echo "FAIL: cannot make $1"; return 1; }; }
  mk FEL_test_dual_hvc1.mp4 mp4-entry.py "$SM/FEL_test_dual_dvh1.mp4" "$M/FEL_test_dual_hvc1.mp4" 2 hvc1 &&
    mk FEL_test_dual_hev1.mp4 mp4-entry.py "$SM/FEL_test_dual.mp4" "$M/FEL_test_dual_hev1.mp4" 2 hev1 &&
    mk neg_hvc1_novdep.mp4 mp4-entry.py "$SM/FEL_test_novdep.mp4" "$M/neg_hvc1_novdep.mp4" 2 hvc1 &&
    mk neg_hvc1_vdep2.mp4 mp4-edit2.py "$M/FEL_test_dual_hvc1.mp4" "$M/neg_hvc1_vdep2.mp4" vdep2 7 &&
    mk neg_hvc1_vdep2same.mp4 mp4-edit2.py "$M/FEL_test_dual_hvc1.mp4" "$M/neg_hvc1_vdep2same.mp4" vdep2 1 &&
    mk neg_hvc1_p8.mp4 mp4-edit2.py "$M/FEL_test_dual_hvc1.mp4" "$M/neg_hvc1_p8.mp4" profile 8 &&
    mk neg_hvc1_bl.mp4 mp4-edit2.py "$M/FEL_test_dual_hvc1.mp4" "$M/neg_hvc1_bl.mp4" bl &&
    mk dvh1_vdep2.mp4 mp4-edit2.py "$SM/FEL_test_dual_dvh1.mp4" "$M/dvh1_vdep2.mp4" vdep2 7 || return 1
  groups() { "$1/ffprobe" -hide_banner -v error -show_stream_groups -of compact "$2" 2> /dev/null < /dev/null | grep -c '^stream_group'; }
  expect() { # file before after
    local b a; b=$(groups "$Bb" "$1"); a=$(groups "$Ab" "$1")
    if [ "$b" = "$2" ] && [ "$a" = "$3" ]; then echo "ok: $(basename "$1"): groups before $b, after $a"
    else echo "FAIL: $(basename "$1"): groups before $b (want $2), after $a (want $3)"; fail=1; fi
  }
  expect "$M/FEL_test_dual_hvc1.mp4" 0 1
  expect "$M/FEL_test_dual_hev1.mp4" 0 1
  expect "$M/neg_hvc1_novdep.mp4" 0 0
  expect "$M/neg_hvc1_vdep2.mp4" 0 0
  expect "$M/neg_hvc1_vdep2same.mp4" 0 0
  expect "$M/neg_hvc1_p8.mp4" 0 0
  expect "$M/neg_hvc1_bl.mp4" 0 0
  expect "$M/dvh1_vdep2.mp4" 1 1
  expect "$SM/FEL_test_dual.mp4" 1 1
  expect "$SM/FEL_test_dual_dvh1.mp4" 1 1
  expect "$SM/FEL_test_novdep.mp4" 0 0
  "$Ab/ffprobe" -hide_banner -v warning "$M/neg_hvc1_vdep2.mp4" < /dev/null > "$L/warn.txt" 2>&1
  grep -q "HEVC sample entry with a 'vdep' reference to more than one track" "$L/warn.txt" \
    && echo "ok: neg_hvc1_vdep2.mp4: warned" || { echo "FAIL: neg_hvc1_vdep2.mp4: no warning"; fail=1; }
  dump() { "$1/ffprobe" -hide_banner -v error -show_streams -show_stream_groups -show_packets -show_data_hash MD5 -of compact "$2" < /dev/null 2>&1; }
  for f in "$SM/FEL_test_dual.mp4" "$SM/FEL_test_dual_dvh1.mp4" "$SM/FEL_test_novdep.mp4" "$SM/issue1-sample.mp4" \
           "$SM/open-movie_2k24p_HDR_P3PQ.mp4" "$M"/*.mp4; do
    n=$(basename "$f")
    [ -e "$f" ] || { echo "FAIL: $n: not there"; fail=1; continue; }
    dump "$Bb" "$f" > "$L/$n.before"
    dump "$Ab" "$f" > "$L/$n.after"
    if [ "$(grep -c '^packet' "$L/$n.after")" -lt 100 ]; then echo "FAIL: $n: $(grep -c '^packet' "$L/$n.after") packets read"; fail=1
    elif cmp -s "$L/$n.before" "$L/$n.after"; then echo "ok: $n: the same before and after ($(grep -c '^packet' "$L/$n.after") packets)"
    elif [ "$n" = FEL_test_dual_hvc1.mp4 ] || [ "$n" = FEL_test_dual_hev1.mp4 ]; then
      # only the group may differ: the "after" listing is the "before" one with the group section in it (its
      # stream_group line, which also holds its first stream, the line of its second stream and a blank line)
      if ! grep -q '^stream_group' "$L/$n.before" && cmp -s "$L/$n.before" <(sed '/^stream_group/,/^$/d' "$L/$n.after") &&
         [ "$(sed -n '/^stream_group/,/^$/p' "$L/$n.after" | grep -c '^stream')" = 2 ] &&
         [ "$(sed -n '/^stream_group/,/^$/p' "$L/$n.after" | grep -c '^packet')" = 0 ]; then
        echo "ok: $n: only the stream group differs ($(grep -c '^packet' "$L/$n.after") packets the same)"
      else echo "FAIL: $n: more than the group differs"; fail=1; fi
    else echo "FAIL: $n: differs before and after"; fail=1; fi
    rg_rm "$L/$n.before" "$L/$n.after"
  done
  [ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
}

# clock: ffmpeg-9931. Transport streams made from one HEVC stream (tools/ts-split.py): the control, the first picture
# split, every other picture split, the conforming case; then every transport stream, Matroska and MP4 sample: every
# packet the same before and after.
clock() {
  local Bb Ab C=$P/clock L=$P/clock-logs fail=0 f n nb
  Bb=$(ffbin before); Ab=$(ffbin after2)
  rg_fresh "$C" "$L"
  "$Bb/ffmpeg" -hide_banner -v error -y -i "$SM/hdr10_240.mkv" -map 0:v:0 -c copy -bsf:v hevc_mp4toannexb -f mpegts "$C/base.ts" < /dev/null \
    || { echo "FAIL: the transport stream of hdr10_240.mkv was not made"; return 1; }
  $RG_PY "$HERE/tools/ts-split.py" "$C/base.ts" "$C/none.ts" none 0 &&
    $RG_PY "$HERE/tools/ts-split.py" "$C/base.ts" "$C/headers.ts" headers 0 &&
    $RG_PY "$HERE/tools/ts-split.py" "$C/base.ts" "$C/next.ts" next 0 &&
    $RG_PY "$HERE/tools/ts-split.py" "$C/base.ts" "$C/headers_all.ts" headers "$(seq -s, 0 2 239)" || { echo "FAIL: ts-split.py"; return 1; }
  # every video packet: times, duration, size, flags and the MD5 of its data (not its position: the split PES packets
  # add PES headers, which move the later packets in the file)
  pk() { "$1/ffprobe" -hide_banner -v error -select_streams v:0 -show_entries packet=pts,dts,duration,size,flags,data_hash -show_data_hash MD5 -of csv=p=0 "$2" < /dev/null 2>&1; }
  for f in none headers headers_all next; do pk "$Bb" "$C/$f.ts" > "$L/$f.before"; pk "$Ab" "$C/$f.ts" > "$L/$f.after"; done
  first() { head -n 1 "$1" | cut -d, -f1,2; }
  [ "$(grep -c . "$L/none.after")" = 240 ] && echo "ok: none.ts: 240 pictures" || { echo "FAIL: none.ts: $(grep -c . "$L/none.after") pictures"; fail=1; }
  [ "$(first "$L/headers.before")" = "N/A,N/A" ] && echo "ok: headers.ts before: the first picture has no time" || { echo "FAIL: headers.ts before: $(first "$L/headers.before")"; fail=1; }
  cmp -s "$L/none.before" "$L/none.after" && echo "ok: none.ts: the same before and after" || { echo "FAIL: none.ts differs"; fail=1; }
  cmp -s "$L/headers.after" "$L/none.after" && echo "ok: headers.ts after: every packet as in the control ($(first "$L/headers.after") first)" || { echo "FAIL: headers.ts after differs from the control"; fail=1; }
  nb=$(grep -c '^N/A,N/A' "$L/headers_all.before")
  [ "$nb" = 120 ] && echo "ok: headers_all.ts before: 120 pictures without a time" || { echo "FAIL: headers_all.ts before: $nb pictures without a time"; fail=1; }
  cmp -s "$L/headers_all.after" "$L/none.after" && echo "ok: headers_all.ts after: every packet as in the control" || { echo "FAIL: headers_all.ts after differs from the control"; fail=1; }
  cmp -s "$L/next.before" "$L/next.after" && echo "ok: next.ts: the same before and after ($(first "$L/next.after"), then $(sed -n 2p "$L/next.after" | cut -d, -f1,2): the time stays with the access unit that begins in its PES)" || { echo "FAIL: next.ts differs"; fail=1; }
  # decoded: the first three pictures of headers.ts with the times the decoder gives them (as Kodi takes them)
  fm() { "$1/ffprobe" -hide_banner -v error -select_streams v:0 -read_intervals '%+#3' -show_entries frame=pts,pkt_dts,best_effort_timestamp -of csv=p=0 "$2" < /dev/null 2>&1; }
  fm "$Bb" "$C/headers.ts" > "$L/headers.frames.before"; fm "$Ab" "$C/headers.ts" > "$L/headers.frames.after"; fm "$Ab" "$C/none.ts" > "$L/none.frames.after"
  [ "$(grep -c . "$L/none.frames.after")" = 3 ] && cmp -s "$L/headers.frames.after" "$L/none.frames.after" && echo "ok: headers.ts after: the first three decoded pictures have the times of the control" || { echo "FAIL: decoded pictures differ"; fail=1; }
  echo "   first decoded picture (pts,pkt_dts,best_effort): before $(head -n 1 "$L/headers.frames.before"), after $(head -n 1 "$L/headers.frames.after"), control $(head -n 1 "$L/none.frames.after")"
  dump() { "$1/ffprobe" -hide_banner -v error -show_streams -show_packets -show_data_hash MD5 -of compact "$2" < /dev/null 2>&1; }
  for n in FEL_test_188.ts FEL_test_as_disc_188.ts issue1_dual.ts issue1_otherpids.ts null_cbr.ts FEL_test_private.m2ts \
           issue1_disc.m2ts FEL_test_as_disc.m2ts FEL_test_for_AVS.m2ts hdr10plus_240.ts hdr10_240.mkv hdr10plus_240.mkv \
           FEL_test_dual.mkv issue1_dual.mkv BL_EL.mkv FEL_test_dual.mp4 open-movie_2k24p_HDR_P3PQ.mp4; do
    f=$SM/$n
    [ -e "$f" ] || { echo "FAIL: $n: not there"; fail=1; continue; }
    dump "$Bb" "$f" > "$L/$n.before"
    dump "$Ab" "$f" > "$L/$n.after"
    if [ "$(grep -c '^packet' "$L/$n.after")" -lt 100 ]; then echo "FAIL: $n: $(grep -c '^packet' "$L/$n.after") packets read"; fail=1
    elif cmp -s "$L/$n.before" "$L/$n.after"; then echo "ok: $n: the same before and after ($(grep -c '^packet' "$L/$n.after") packets)"
    else echo "FAIL: $n: differs ($(diff "$L/$n.before" "$L/$n.after" | grep -c '^>') lines)"; fail=1; fi
    rg_rm "$L/$n.before" "$L/$n.after"
  done
  [ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
}

# merge: Kodi's join of two tracks (DVLayerMerge.cpp) and the module of the layer (dvbridge_fel.c) of the Kodi tree on
# the 'hvc1'/'hev1' files, with FFmpeg with ffmpeg-9930 ("after") and without it ("before"): fel_tracks_test of the group
# fel, quick. With the patch the 'hvc1' file gives the same pictures, pairs and RPUs as the 'dvh1' file it was made of
# (the same bytes but four), and the 'hev1' file the same as the 'dvhe' one; without it the 'hvc1' file is not joined.
merge() {
  local K O=$P/merge L=$P/merge-logs fail=0 n SAN VA T=$RG_REGRESS/fel/tests
  K=$(rg_kodi) || { echo "FAIL: no Kodi tree"; return 1; }
  VA=$(rg_vastub)
  rg_fresh "$O" "$L"
  SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
  local DV=$K/tools/dvbridge J=$K/xbmc/cores/VideoPlayer/DVDDemuxers
  bld() { # <name> <FFmpeg prefix>
    local PF=$2 Ob=$O/obj-$1
    mkdir -p "$Ob"
    g++ -std=c++20 $SAN -Wall -Wextra -I"$PF/include" -I"$J" -I"$T" -c "$T/merge_shim.cpp" -o "$Ob/shim.o" &&
      g++ -std=c++20 $SAN -Wall -Wextra -I"$PF/include" -I"$J" -I"$DV" -c "$J/DVLayerMerge.cpp" -o "$Ob/merge.o" &&
      gcc -std=gnu11 $SAN -Wall -Wextra -I"$PF/include" -I"$DV" -c "$DV/dvbridge_probe.c" -o "$Ob/probe.o" &&
      gcc -std=gnu11 $SAN -Wall -Wextra -Wno-unused-function -I"$PF/include" -I"$DV" -I"$T" -I"$VA" -c "$T/fel_tracks_test.c" -o "$Ob/t.o" &&
      g++ $SAN -o "$O/tracks-$1" "$Ob/t.o" "$Ob/shim.o" "$Ob/merge.o" "$Ob/probe.o" -L"$PF/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread
  }
  bld after "$V/after/prefix" > "$O/build-after.log" 2>&1 < /dev/null || { echo "FAIL: build with ffmpeg-9930: $(grep -m1 error "$O/build-after.log")"; return 1; }
  bld before "$V/before/prefix" > "$O/build-before.log" 2>&1 < /dev/null || { echo "FAIL: build without ffmpeg-9930: $(grep -m1 error "$O/build-before.log")"; return 1; }
  echo "built with and without ffmpeg-9930: $(cat "$O"/build-*.log | grep -c 'warning:') warning lines"
  one() { # <program> <file> <name>
    FEL_DUMP=$L/dump-$3.tsv FEL_PICTURES=$L/$3.pics ASAN_OPTIONS=detect_leaks=1 nice -n 19 timeout 3600 "$O/tracks-$1" "$2" quick < /dev/null > "$L/$3.log" 2>&1
    echo "$3: exit $?, $(grep -c '^FAIL' "$L/$3.log") FAIL, $(grep -c '^ok:' "$L/$3.log") ok, $(rg_san "$L/$3.log") sanitizer lines; last: $(tail -n 1 "$L/$3.log" | cut -c1-100)"
  }
  local M=$P/samples
  one after "$M/FEL_test_dual_hvc1.mp4" hvc1-after & one after "$SM/FEL_test_dual_dvh1.mp4" dvh1-after &
  one after "$M/FEL_test_dual_hev1.mp4" hev1-after & wait
  one after "$SM/FEL_test_dual.mp4" dvhe-after & one before "$M/FEL_test_dual_hvc1.mp4" hvc1-before &
  one before "$SM/FEL_test_dual_dvh1.mp4" dvh1-before & wait
  for n in hvc1-after dvh1-after hev1-after dvhe-after hvc1-before dvh1-before; do
    [ "$(rg_san "$L/$n.log")" = 0 ] || { echo "FAIL: $n: sanitizer reports"; fail=1; }
  done
  [ -s "$L/dump-dvh1-after.tsv" ] && cmp -s "$L/dump-hvc1-after.tsv" "$L/dump-dvh1-after.tsv" && echo "ok: hvc1 (with ffmpeg-9930) gives what dvh1 gives: $(wc -l < "$L/dump-hvc1-after.tsv") lines" || { echo "FAIL: hvc1 differs from dvh1"; fail=1; }
  [ -s "$L/dump-dvhe-after.tsv" ] && cmp -s "$L/dump-hev1-after.tsv" "$L/dump-dvhe-after.tsv" && echo "ok: hev1 (with ffmpeg-9930) gives what dvhe gives: $(wc -l < "$L/dump-hev1-after.tsv") lines" || { echo "FAIL: hev1 differs from dvhe"; fail=1; }
  [ -s "$L/dump-dvh1-after.tsv" ] && cmp -s "$L/dump-dvh1-after.tsv" "$L/dump-dvh1-before.tsv" && echo "ok: dvh1: the same with and without ffmpeg-9930" || { echo "FAIL: dvh1 differs with ffmpeg-9930"; fail=1; }
  [ "$(grep -c '^FAIL' "$L/hvc1-before.log")" -gt 0 ] && echo "ok: hvc1 without ffmpeg-9930: not joined ($(grep -m1 '^FAIL' "$L/hvc1-before.log" | cut -c1-120))" || { echo "FAIL: hvc1 joined without the patch?"; fail=1; }
  # The 'dvhe' and 'hev1' runs pass whole. The 'dvh1' and 'hvc1' files have the parameter sets of the enhancement track
  # only in its record, and one check of fel_tracks_test runs the module "without parameter sets from outside the
  # stream" (after a seek), which can then pair nothing: it fails for 'dvh1' without ffmpeg-9930 too (a premise of the
  # test, not of the player, which hands the record on to the decoder of the layer: doviELConfig). It must be that one
  # failure, and no other, for 'hvc1' with ffmpeg-9930 and for 'dvh1' with and without it.
  for n in hev1-after dvhe-after; do
    grep -q '^FAIL' "$L/$n.log" && { echo "FAIL: $n has failures: $(grep -m1 '^FAIL' "$L/$n.log" | cut -c1-140)"; fail=1; }
  done
  local known
  known=$(grep '^FAIL:' "$L/dvh1-before.log")
  if [ "$(grep -c '^FAIL:' "$L/dvh1-before.log")" = 1 ] && case "$known" in *"without parameter sets"*|*"cannot be read yet do not start it"*) true ;; *) false ;; esac; then
    echo "ok: dvh1 without ffmpeg-9930: the one failure of the premise (${known:6:100}...)"
  else echo "FAIL: dvh1 without ffmpeg-9930: not the one failure of the premise: $(echo "$known" | head -n 2 | tr '\n' ' ' | cut -c1-160)"; fail=1; fi
  for n in hvc1-after dvh1-after; do
    if [ "$(grep '^FAIL:' "$L/$n.log")" = "$known" ] && [ "$(grep -c '^FAIL:' "$L/$n.log")" = 1 ]; then
      echo "ok: $n: only the failure that 'dvh1' has without ffmpeg-9930 too"
    else echo "FAIL: $n: failures other than that of 'dvh1' without ffmpeg-9930: $(grep -m1 '^FAIL:' "$L/$n.log" | cut -c1-140)"; fail=1; fi
  done
  [ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
}

stage_playback() {
  local v
  rg_fresh "$P"
  if [ -x "$(ffbin before)/ffprobe" ] && [ -x "$(ffbin after)/ffprobe" ]; then
    rg_log "$RG_W/logs/mov.log" mov
    v=$(rg_pass "$RG_W/logs/mov.log" '^ALL PASSED$' '^FAIL')
    rg_item mov-dv-group "$(rg_say "$v")" "ffmpeg-9930: $(grep -c '^ok:' "$RG_W/logs/mov.log") ok; $(rg_rest "$v")"
  else rg_item mov-dv-group FAILED "no FFmpeg built without ffmpeg-9930 and 9931, or without 9931"; fi
  if [ -x "$(ffbin before)/ffprobe" ] && [ -x "$(ffbin after2)/ffprobe" ]; then
    rg_log "$RG_W/logs/clock.log" clock
    v=$(rg_pass "$RG_W/logs/clock.log" '^ALL PASSED$' '^FAIL')
    rg_item hevc-pes-time "$(rg_say "$v")" "ffmpeg-9931: $(grep -c '^ok:' "$RG_W/logs/clock.log") ok; $(rg_rest "$v")"
  else rg_item hevc-pes-time FAILED "no FFmpeg built without ffmpeg-9930 and 9931, or with all"; fi
  if [ -f "$P/samples/FEL_test_dual_hvc1.mp4" ] && [ -f "$V/after/prefix/lib/libavcodec.a" ] && [ -f "$V/before/prefix/lib/libavcodec.a" ]; then
    rg_log "$RG_W/logs/merge.log" merge
    v=$(rg_pass "$RG_W/logs/merge.log" '^ALL PASSED$' '^FAIL')
    rg_item merge-hvc1 "$(rg_say "$v")" "ffmpeg-9930 and DVLayerMerge.cpp: $(grep -c '^ok:' "$RG_W/logs/merge.log") ok; $(rg_rest "$v")"
  else rg_item merge-hvc1 FAILED "no 'hvc1' sample made (mov-dv-group), or no FFmpeg with and without ffmpeg-9930"; fi
}

rg_main "inputs build fate playback" "$@"
