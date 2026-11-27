#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-ffmpeg.sh: the FFmpeg patches of the image that change what FFmpeg reads, checked on the host with FFmpeg of the
# recipe (lib-ffmpeg.sh):
#   fate      FATE, the test suite of FFmpeg, on what the patches change: ac3/eac3 (ffmpeg-9945, 9946), IMF (9934),
#             Matroska (9932-01, 9940, 9950, 9953), FLV (9932-02), Ogg (9932-03 to 05), the coded bitstreams and LCEVC
#             (9932-06 to 08), CDXL (9933-01), G2M (9933-02), Sun raster (9933-03, 06), PCX (9933-04), HuffYUV
#             (9933-05); every test of each that can run with the samples of inputs-ffmpeg.tsv passes, and those that
#             cannot are named. The two references of ffmpeg-9946 must give the new output (and the old _v2 ones a
#             difference), and the reference of the IMF unit test of before ffmpeg-9934 must fail.
#   hls       ffmpeg-9972 (the HLS demuxer loads one variant of a master playlist with load_all_variants=0, the one
#             that max_bandwidth selects): tools/hls_variants.sh with FFmpeg built with AddressSanitizer, with all the
#             patches, and without ffmpeg-9972, where it must fail (HLS_FAILS_WITHOUT checks)
#   playback  ffmpeg-9930 (an enhancement track of Dolby Vision in an 'hvc1'/'hev1' sample entry of MP4 grouped) and
#             ffmpeg-9931 (an HEVC picture whose parameter sets came in a PES without a time takes the time of the PES
#             of its first slice), each against FFmpeg without it: what changes, and that nothing else does; and
#             Kodi's join of two tracks (DVLayerMerge.cpp) with the module of the layer on the 'hvc1'/'hev1' files.
# Host test only: it builds and runs everything under $REGRESS_WORK/ffmpeg.
#   mvcmkv    ffmpeg-9953 (the second view of H.264 MVC declared by a block addition mapping or only by the
#             stream of a Matroska file), against FFmpeg without it, and what Kodi's MultiviewChoice.h decides
#             of what it gives (the function mvcmkv() says what is checked)
#   slkeep    ffmpeg-9977 (the hardware decoder kept when the SPS of the base layer comes again changed only in its
#             scaling lists): FFmpeg with VA-API, with and without the patch, on a VA-API driver that decodes nothing
#             and writes what each picture is given (tools/fakeva.c, tools/slkeep.c), and the mutants of the patch
# usage: run-ffmpeg.sh all | verdict [<log>] | <stage>...       stages: inputs build fate hls playback mvcmkv slkeep
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
matroska:9932-01+9940+9950+9953:^fate-(lavf-mkv|matroska|mkv)
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
  # (each with the fuzz that FATE gives it: ac3.mak of FFmpeg 9.1 gives fate-ac3-2.0 FUZZ = 2)
  x=$(grep -c -E ': make exit [1-9]; MAXDIFF: \|[1-9][0-9]* - 0\| >= [1-9][0-9]*$' "$RG_W/logs/ac3-old-refs.log")
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
      # only the group may differ: the "after" listing is the "before" one with the group section in it, which with
      # FFmpeg 9.1's ffprobe (groups list their entries) is its stream_group line and one line for each of its two
      # streams, the same lines as streams 0 and 1 of the listing
      if ! grep -q '^stream_group' "$L/$n.before" && [ "$(grep -c '^stream_group' "$L/$n.after")" = 1 ] &&
         cmp -s "$L/$n.before" <(sed '/^stream_group/,+2d' "$L/$n.after") &&
         [ "$(sed -n '/^stream_group/,+2p' "$L/$n.after" | sed -n 2p)" = "$(grep -m1 '^stream|index=0|' "$L/$n.before")" ] &&
         [ "$(sed -n '/^stream_group/,+2p' "$L/$n.after" | sed -n 3p)" = "$(grep -m1 '^stream|index=1|' "$L/$n.before")" ] &&
         [ -n "$(grep -m1 '^stream|index=1|' "$L/$n.before")" ]; then
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

# ffmpeg-9972: the FFmpeg of the test, with and without the patch (no other patch left out)
HLS_FF="--disable-everything --enable-avdevice --enable-avfilter --enable-swscale --enable-swresample
  --enable-indev=lavfi --enable-filter=testsrc,sine,scale,format,aformat,aresample,null,anull,buffer,buffersink,abuffer,abuffersink
  --enable-encoder=mpeg2video,mp2 --enable-decoder=mpeg2video,mp2,wrapped_avframe,pcm_s16le --enable-parser=mpegvideo,mpegaudio
  --enable-demuxer=hls,mpegts --enable-muxer=mpegts --enable-protocol=file,pipe --enable-ffmpeg --enable-ffprobe
  --extra-cflags=-fsanitize=address --extra-ldflags=-fsanitize=address"
# the checks of hls_variants.sh that fail without ffmpeg-9972 (measured when it was written: ffprobe does not know
# load_all_variants and max_bandwidth)
HLS_FAILS_WITHOUT=14
stage_hls() {
  local L=$RG_W/logs b v
  for b in after before; do
    # shellcheck disable=SC2086
    if [ $b = before ]; then RG_FF_LEAVE_OUT='^ffmpeg-9972-' rg_log "$L/build-hls-$b.log" rg_ffmpeg "$RG_W/hls-$b" $HLS_FF
    else rg_log "$L/build-hls-$b.log" rg_ffmpeg "$RG_W/hls-$b" $HLS_FF; fi
    v=$(rg_pass "$L/build-hls-$b.log" '^FF: (built in|.*already built)' '^FF: (PATCH FAILED|BUILD FAILED|sha256|no )')
    rg_item "build-hls-$b" "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 '^FFSRC' "$L/build-hls-$b.log" | cut -c7-)"
  done
  export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
  if [ -x "$RG_W/hls-after/prefix/bin/ffprobe" ]; then
    rg_log "$L/hls.log" bash "$HERE/tools/hls_variants.sh" "$RG_W/hls-after/prefix/bin" "$RG_W/hls-run"
    v=$(rg_pass "$L/hls.log" '^ALL PASSED$' '^FAIL')
    rg_item hls-one-variant "$(rg_say "$v")" "ffmpeg-9972: $(grep -c '^ok:' "$L/hls.log") ok; $(rg_rest "$v")"
  else rg_item hls-one-variant FAILED "no FFmpeg built with ffmpeg-9972"; fi
  if [ -x "$RG_W/hls-before/prefix/bin/ffprobe" ]; then
    rg_log "$L/hls-before.log" bash "$HERE/tools/hls_variants.sh" "$RG_W/hls-before/prefix/bin" "$RG_W/hls-run-before"
    v=$(rg_fails "$L/hls-before.log" "$HLS_FAILS_WITHOUT" '^FAIL')
    rg_item hls-one-variant.without-9972 "$(rg_say "$v")" "$(rg_rest "$v")"
  else rg_item hls-one-variant.without-9972 FAILED "no FFmpeg built without ffmpeg-9972"; fi
  rg_rm "$RG_W/hls-run" "$RG_W/hls-run-before"
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

# ------------------------------------------------------------------------------------------------- MVC in Matroska
# mvcmkv: ffmpeg-9953. Matroska files of one H.264 MVC stream (the 1920x1080 two-view stream of the reference encoder,
# which tools/regress/mvc decodes against JM) in each form that declares its second view, and a 2D one
# (tools/mkv-mvc.py), read by ffprobe with and without the patch (tools/ff-extradata.py: the private data after
# avformat_find_stream_info(), and the tag mvc_configuration), and what Kodi decides of that private data with its own
# header, MultiviewChoice.h (tools/mvc_declared.cpp). With the patch: every form is decoded as two views; the mapping
# forms give the bytes of the legacy form; the stream form opens the decoder of both views with the same record as the
# legacy form; the legacy form, both forms together and the 2D file keep their bytes. Without it: the mapping and the
# stream forms are plain H.264. And every packet of the six files is the same with and without it.
mvcmkv() {
  local Nb Ab K D=$RG_W/mvcmkv fail=0 n v line
  Nb=$(ffbin nomvc); Ab=$(ffbin after2)
  rg_fresh "$D"
  mkdir -p "$D/x"
  $RG_PY "$HERE/tools/mkv-mvc.py" "$SM/mvc-bd1080.264" "$D" || { echo "FAIL: mkv-mvc.py"; return 1; }
  K=$(rg_kodi) || { echo "FAIL: no Kodi tree"; return 1; }
  g++ -std=c++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$K/xbmc/cores/VideoPlayer" -o "$D/mvc_declared" "$HERE/tools/mvc_declared.cpp" > "$D/build.log" 2>&1 \
    || { echo "FAIL: mvc_declared does not build: $(head -n 3 "$D/build.log" | tr '\n' ' ')"; return 1; }
  for n in legacy mapping maponly both stream plain; do
    for v in nomvc after2; do
      line=$($RG_PY "$HERE/tools/ff-extradata.py" "$(ffbin $v)/ffprobe" "$D/$n.mkv" "$D/x/$n.$v.bin") \
        || { echo "FAIL: $n ($v): ffprobe"; fail=1; continue; }
      echo "$n $v: private data of ${line%% *} bytes, mvc_configuration ${line#* }"
      echo "${line#* }" > "$D/x/$n.$v.tag"
    done
  done
  "$D/mvc_declared" "$D"/x/*.bin "$D/expected-legacy.bin" "$D/expected-base.bin" > "$D/decided.txt" 2>&1 \
    || { echo "FAIL: mvc_declared: $(tail -n 2 "$D/decided.txt" | tr '\n' ' ')"; fail=1; }
  sed "s#$D/##" "$D/decided.txt"
  dec() { grep "/$1 " "$D/decided.txt" | sed 's/^[^ ]* //'; }
  rec() { dec "$1" | sed 's/.*record=//'; }
  want() { # what, got, wanted
    if [ "$2" = "$3" ]; then echo "ok: $1: $2"; else echo "FAIL: $1: $2 (want $3)"; fail=1; fi
  }
  same() { # what, file, file
    if cmp -s "$2" "$3"; then echo "ok: $1"; else echo "FAIL: $1: the bytes differ"; fail=1; fi
  }
  # the premises: the legacy form is the record of the base view with the box, which Kodi decodes as two views
  want "the legacy form, as written" "$(dec expected-legacy.bin | cut -d' ' -f1-2)" "declared=1 twoviews=1"
  want "the record of the base view alone" "$(dec expected-base.bin | cut -d' ' -f1-2)" "declared=0 twoviews=0"
  # without the patch
  for n in legacy both; do
    want "$n without ffmpeg-9953" "$(dec $n.nomvc.bin | cut -d' ' -f1-2)" "declared=1 twoviews=1"
  done
  for n in mapping maponly stream plain; do
    want "$n without ffmpeg-9953 (plain H.264)" "$(dec $n.nomvc.bin | cut -d' ' -f1-2)" "declared=0 twoviews=0"
  done
  # with it
  for n in legacy mapping maponly both stream; do
    want "$n with ffmpeg-9953" "$(dec $n.after2.bin | cut -d' ' -f1-2)" "declared=1 twoviews=1"
  done
  want "plain with ffmpeg-9953 (plain H.264)" "$(dec plain.after2.bin | cut -d' ' -f1-2)" "declared=0 twoviews=0"
  for n in mapping maponly both; do
    same "$n with ffmpeg-9953: the bytes of the legacy form" "$D/x/$n.after2.bin" "$D/expected-legacy.bin"
  done
  for n in legacy both plain; do
    same "$n: the same bytes with and without ffmpeg-9953" "$D/x/$n.nomvc.bin" "$D/x/$n.after2.bin"
  done
  same "stream without ffmpeg-9953: the record of the base view" "$D/x/stream.nomvc.bin" "$D/expected-base.bin"
  want "stream with ffmpeg-9953: the decoder of both views opened with the record of the legacy form" \
    "$(rec stream.after2.bin)" "$(rec legacy.after2.bin)"
  want "legacy: the tag" "$(cat "$D/x/legacy.after2.tag")" "codec private"
  want "both: the tag" "$(cat "$D/x/both.after2.tag")" "codec private"
  want "mapping: the tag" "$(cat "$D/x/mapping.after2.tag")" "block addition mapping"
  want "maponly: the tag" "$(cat "$D/x/maponly.after2.tag")" "block addition mapping"
  want "stream: the tag" "$(cat "$D/x/stream.after2.tag")" "stream"
  want "plain: no tag" "$(cat "$D/x/plain.after2.tag")" "-"
  # every packet, with and without the patch
  for n in legacy mapping maponly both stream plain; do
    for v in nomvc after2; do
      "$(ffbin $v)/ffprobe" -hide_banner -v error -show_packets -show_data_hash MD5 -of compact "$D/$n.mkv" \
        < /dev/null > "$D/x/$n.$v.packets" 2>&1
    done
    if [ "$(grep -c '^packet' "$D/x/$n.after2.packets")" != 25 ]; then
      echo "FAIL: $n: $(grep -c '^packet' "$D/x/$n.after2.packets") packets read (want 25)"; fail=1
    else same "$n: the 25 packets the same with and without ffmpeg-9953" "$D/x/$n.nomvc.packets" "$D/x/$n.after2.packets"; fi
  done
  [ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
}

stage_mvcmkv() {
  local v L=$RG_W/logs
  # shellcheck disable=SC2086
  rg_log "$L/build-nomvc.log" ff_variant "$V/nomvc" '^ffmpeg-9953-' $RG_FF_BRIDGE
  v=$(rg_pass "$L/build-nomvc.log" '^(FF: |/)' '^FF: (PATCH FAILED|BUILD FAILED|sha256|no )')
  rg_item build-nomvc "$(rg_say "$v")" "$(rg_rest "$v")$(grep -a -m1 '^FFSRC' "$L/build-nomvc.log" | sed 's/^FFSRC/;/')"
  rg_log "$L/build-after2-mvc.log" rg_ff_bridge
  if [ -x "$(ffbin nomvc)/ffprobe" ] && [ -x "$(ffbin after2)/ffprobe" ]; then
    rg_log "$L/mvcmkv.log" mvcmkv
    v=$(rg_pass "$L/mvcmkv.log" '^ALL PASSED$' '^FAIL')
    rg_item mvc-matroska "$(rg_say "$v")" "ffmpeg-9953: $(grep -c '^ok:' "$L/mvcmkv.log") ok; $(rg_rest "$v")"
  else rg_item mvc-matroska FAILED "no FFmpeg built without ffmpeg-9953, or with all"; fi
}

# ------------------------------------------------------------------------------------------------- slkeep
# ffmpeg-9977: the hardware decoder is kept when the SPS of the base layer comes again changed only in its scaling
# lists. FFmpeg of the recipe is built with VA-API against libva of the recipe (its core, without a display backend)
# and decodes on the display of tools/slkeep.c, whose driver (tools/fakeva.c) decodes nothing and writes what each
# picture was given; the inputs are x265 streams of segments whose SPS changes at each joint (tools/make-slswitch.sh).
# With the patch and without it ("before"), with 1 and 4 frame threads: how many times get_format() is called and the
# hardware decoder is made (vaCreateContext()), the buffers of every picture the same with and without the patch (its
# picture parameters, scaling lists, slice parameters and slice data), the scaling lists of each segment its own, and
# the pictures of the software decoder the same with and without the patch, MD5 for MD5. Mutants of the patch
# (SL_MUTANTS, mutants/slkeep-*.py) must make the checks fail.
SL=$RG_W/slkeep
SL_FF="--disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect --disable-x86asm
  --enable-static --disable-shared --enable-decoder=hevc --enable-parser=hevc --enable-demuxer=hevc --enable-protocol=file
  --enable-vaapi --enable-hwaccel=hevc_vaapi"
# stream : pictures : the scaling lists of its segments : get_format() and hardware decoders with the patch : without
# it : the lines of the decoder that say the hardware decoder is kept, with the patch
SL_STREAMS="slswitch8:40:ab-ca:1:5:4
slswitch10:24:aba:1:3:2
slother8:48:aaaaba:6:6:0"
# mutant : the failed checks measured when they were written : what it takes out
SL_MUTANTS="always:2:the comparison of the SPS (every change keeps the hardware decoder)
never:4:the comparison of the SPS (no change keeps it)
pixfmt:4:the format of the hardware decoder given back to the context after export_stream_params()"

# sl_libva <dest>: libva of the recipe (archive checked against PKG_SHA256), its core built by hand into <dest>/prefix
# (its meson build wants libdrm even without the DRM backend, which slkeep.c does not use), and fakeva.c into
# <dest>/drivers/hadesfake_drv_video.so
sl_libva() {
  local D=$1 ver sha tb maj min mic
  ver=$(rg_pkgvar packages/multimedia/libva PKG_VERSION); sha=$(rg_pkgvar packages/multimedia/libva PKG_SHA256)
  tb=$(rg_source libva "libva-$ver.tar.gz") || { echo "VA: no libva-$ver.tar.gz in REGRESS_SOURCES"; return 1; }
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "VA: sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  rg_fresh "$D"
  mkdir -p "$D/src" "$D/prefix/include/va" "$D/prefix/lib/pkgconfig" "$D/gen/va" "$D/drivers"
  tar --no-same-owner -xzf "$tb" --strip-components=1 -C "$D/src" || { echo "VA: unpack $tb"; return 1; }
  maj=$(sed -n 's/^va_api_major_version = \([0-9]*\)$/\1/p' "$D/src/meson.build")
  min=$(sed -n 's/^va_api_minor_version = \([0-9]*\)$/\1/p' "$D/src/meson.build")
  mic=$(sed -n 's/^va_api_micro_version = \([0-9]*\)$/\1/p' "$D/src/meson.build")
  [ -n "$maj" ] && [ -n "$min" ] && [ -n "$mic" ] || { echo "VA: no VA-API version in meson.build"; return 1; }
  sed -e "s/@VA_API_MAJOR_VERSION@/$maj/" -e "s/@VA_API_MINOR_VERSION@/$min/" -e "s/@VA_API_MICRO_VERSION@/$mic/" \
    -e "s/@VA_API_VERSION@/$maj.$min.$mic/" "$D/src/va/va_version.h.in" > "$D/gen/va/va_version.h"
  ( cd "$D/src/va" && cp va.h va_backend.h va_backend_prot.h va_backend_vpp.h va_compat.h va_dec_*.h va_drmcommon.h \
      va_egl.h va_enc_*.h va_fei*.h va_str.h va_tpi.h va_prot.h va_vpp.h "$D/gen/va/va_version.h" "$D/prefix/include/va/" ) \
    || { echo "VA: headers"; return 1; }
  gcc -shared -fPIC -O1 -g -I "$D/gen" -I "$D/src" -I "$D/src/va" -D_GNU_SOURCE -DHAVE_SECURE_GETENV \
    -DSYSCONFDIR='"/nonexistent"' -DVA_DRIVERS_PATH='"/nonexistent"' -Wl,-version-script,"$D/src/va/libva.syms" \
    -Wl,-soname,libva.so.2 -o "$D/prefix/lib/libva.so.2" \
    "$D/src/va/va.c" "$D/src/va/va_compat.c" "$D/src/va/va_str.c" "$D/src/va/va_trace.c" -ldl -lpthread \
    > "$D/build.log" 2>&1 || { tail -n 20 "$D/build.log"; echo "VA: BUILD FAILED"; return 1; }
  ln -sfn libva.so.2 "$D/prefix/lib/libva.so"
  # shellcheck disable=SC2016
  printf '%s\n' "prefix=$D/prefix" 'libdir=${prefix}/lib' 'includedir=${prefix}/include' 'Name: libva' \
    "Description: libva $ver of the recipe, its core only" "Version: $maj.$min.$mic" \
    'Libs: -L${libdir} -lva -Wl,-rpath,${libdir}' 'Cflags: -I${includedir}' > "$D/prefix/lib/pkgconfig/libva.pc"
  gcc -shared -fPIC -O1 -g -Wall -Wextra -Werror -I "$D/prefix/include" -o "$D/drivers/hadesfake_drv_video.so" \
    "$HERE/tools/fakeva.c" >> "$D/build.log" 2>&1 || { tail -n 20 "$D/build.log"; echo "VA: fakeva.c does not build"; return 1; }
  rg_rm "$D/src"
  echo "VA: libva $ver (VA-API $maj.$min.$mic) and fakeva.c built in $D"
}

# sl_ff <dest> <ERE of patch names left out, or -> <edit file, or ->: that FFmpeg with VA-API (SL_FF) into
# <dest>/prefix, and slkeep.c linked with it into <dest>/slkeep. The edit file defines FILE (a path in FFmpeg's source)
# and PAIRS (rg_edit). Made again only when the archive, the patches, the edit, slkeep.c or libva changed.
sl_ff() {
  local D=$1 skip=$2 edit=$3 rc f stamp
  stamp="slkeep $SL_FF $(cat "$HERE/tools/slkeep.c" "$SL/va/prefix/lib/pkgconfig/libva.pc" | sha256sum | cut -c1-64)"
  [ "$edit" = - ] || stamp="$stamp $(sha256sum < "$edit" | cut -c1-64)"
  rg_ff_source "$D" "$skip" "$stamp"
  rc=$?
  [ $rc = 2 ] && { echo "FF: $D already built from the same archive, patches, edit and options"; return 0; }
  [ $rc = 0 ] || return 1
  if [ "$edit" != - ]; then
    f=$($RG_PY -c 'import runpy, sys; print(runpy.run_path(sys.argv[1])["FILE"])' "$edit") || { echo "FF: no FILE in $edit"; return 1; }
    rg_edit "$D/src/$f" "$edit" || { echo "FF: EDIT NOT APPLIED $edit"; return 1; }
  fi
  # shellcheck disable=SC2086
  ( cd "$D/src" && PKG_CONFIG_PATH=$SL/va/prefix/lib/pkgconfig nice -n 19 ./configure --prefix="$D/prefix" $SL_FF \
      > "$D/configure.log" 2>&1 < /dev/null &&
    nice -n 19 make -j"$RG_JOBS" > "$D/make.log" 2>&1 < /dev/null && make install > "$D/install.log" 2>&1 < /dev/null ) \
    || { tail -n 30 "$D/configure.log" "$D/make.log" 2> /dev/null; echo "FF: BUILD FAILED $D"; return 1; }
  # shellcheck disable=SC2046
  gcc -O1 -g -Wall -Wextra -Werror -o "$D/slkeep" "$HERE/tools/slkeep.c" \
    $(PKG_CONFIG_PATH="$D/prefix/lib/pkgconfig:$SL/va/prefix/lib/pkgconfig" pkg-config --cflags --libs --static \
      libavformat libavcodec libavutil libva) > "$D/slkeep.log" 2>&1 \
    || { cat "$D/slkeep.log"; echo "FF: slkeep.c does not build with $D"; return 1; }
  rg_rm "$D/src"
  : > "$D/built"
  echo "FF: built in $D, and slkeep.c with it"
}

# sl_run <variant> <stream> <threads>: slkeep.c of a variant on a stream, on fakeva.c and in software, into $SL/out
sl_run() {
  local o=$SL/out/$1-$2-t$3
  rm -f "$o.va"
  LIBVA_DRIVERS_PATH=$SL/va/drivers LIBVA_MESSAGING_LEVEL=1 HADES_FAKEVA_LOG=$o.va \
    "$SL/$1/slkeep" vaapi "$IN/slkeep/$2.hevc" "$3" > "$o.hw" 2>&1 || echo "FAIL: $1 $2 (frame threads: $3): slkeep vaapi, exit $?"
  "$SL/$1/slkeep" sw "$IN/slkeep/$2.hevc" "$3" > "$o.sw" 2>&1 || echo "FAIL: $1 $2 (frame threads: $3): slkeep sw, exit $?"
}
# the get_format() calls, pictures and lines of a kept decoder of a run; the hardware decoders made
sl_sum() { sed -n 's/^SUMMARY get_format \([0-9]*\) frames \([0-9]*\) kept \([0-9]*\)$/\1 \2 \3/p' "$1.hw"; }
sl_ctx() { grep -c '^CONTEXT ' "$1.va"; }
# the buffers of each picture, without its number and context (a context is new where the decoder is made again)
sl_bufs() { grep -E '^(PIC|IQ) ' "$1.va" | sed -e 's/ context [0-9]*//' -e 's/^PIC [0-9]*/PIC/' -e 's/^IQ [0-9]*/IQ/'; }
# the scaling lists of each segment of 8 pictures, letters in the order they come (- for none), or "mixed"
sl_lists() {
  awk '/^PIC / {print $8}' "$1.va" | awk '
    { g = int((NR - 1) / 8); if ((g in s) && s[g] != $1) bad = 1; s[g] = $1; n = g }
    END { if (bad || NR == 0) { print "mixed"; exit }
          for (i = 0; i <= n; i++) { k = s[i]; if (k == "-") { o = o "-"; continue }
            if (!(k in l)) l[k] = sprintf("%c", 97 + c++); o = o l[k] } print o }'
}

# slcheck <variant>: the checks of a variant against "before" ("before" itself is checked as a premise)
slcheck() {
  local v=$1 s fr lists gfa gfb kept t gf fc kp ctx o b
  while IFS=: read -r s fr lists gfa gfb kept; do
    for t in 1 4; do
      sl_run "$v" "$s" "$t"
      [ -f "$SL/out/before-$s-t$t.va" ] || sl_run before "$s" "$t"
      o=$SL/out/$v-$s-t$t; b=$SL/out/before-$s-t$t
      read -r gf fc kp <<< "$(sl_sum "$o")"; ctx=$(sl_ctx "$o")
      if [ "$gf $ctx $kp $fc" = "$gfa $gfa $kept $fr" ]; then
        echo "ok: $v $s (frame threads: $t): get_format() $gf, hardware decoders $ctx, kept $kp times, $fc pictures"
      else echo "FAIL: $v $s (frame threads: $t): get_format() $gf, hardware decoders $ctx, kept $kp times, $fc pictures (want $gfa, $gfa, $kept, $fr)"; fi
      read -r gf fc kp <<< "$(sl_sum "$b")"; ctx=$(sl_ctx "$b")
      if [ "$gf $ctx $kp $fc" = "$gfb $gfb 0 $fr" ]; then
        echo "ok: before $s (frame threads: $t): get_format() $gf, hardware decoders $ctx, $fc pictures"
      else echo "FAIL: before $s (frame threads: $t): get_format() $gf, hardware decoders $ctx, kept $kp times, $fc pictures (want $gfb, $gfb, 0, $fr)"; fi
      if [ "$(sl_bufs "$o" | grep -c '^PIC')" = "$fr" ] && cmp -s <(sl_bufs "$o") <(sl_bufs "$b") &&
         [ "$(sl_lists "$o")" = "$lists" ]; then
        echo "ok: $v $s (frame threads: $t): the buffers of the $fr pictures are those without the patch; scaling lists $lists"
      else echo "FAIL: $v $s (frame threads: $t): $(sl_bufs "$o" | grep -c '^PIC') pictures, buffers $(cmp -s <(sl_bufs "$o") <(sl_bufs "$b") && echo same || echo different) from those without the patch, scaling lists $(sl_lists "$o") (want $fr, the same, $lists)"; fi
      if [ "$(grep -c '^FRAME .* md5 ' "$o.sw")" = "$fr" ] && cmp -s <(grep '^FRAME' "$o.sw") <(grep '^FRAME' "$SL/out/before-$s-t1.sw"); then
        echo "ok: $v $s (frame threads: $t): the $fr pictures of the software decoder are those without the patch with one thread"
      else echo "FAIL: $v $s (frame threads: $t): the pictures of the software decoder differ from those without the patch with one thread"; fi
    done
  done < <(printf '%s\n' "$SL_STREAMS")
  echo "$(grep -c . <<< "$SL_STREAMS") streams, 1 and 4 threads"
}

stage_slkeep() {
  local L=$RG_W/logs v m n what P
  mkdir -p "$SL"
  rg_fresh "$SL/out"
  rg_log "$L/build-slkeep-va.log" sl_libva "$SL/va"
  v=$(rg_pass "$L/build-slkeep-va.log" '^VA: libva .* built' '^VA: (no |sha256|unpack|headers|BUILD|fakeva)')
  rg_item build-slkeep-va "$(rg_say "$v")" "$(rg_rest "$v")"
  P=$(git -C "$REGRESS_LE_GIT" ls-tree --name-only "$REGRESS_LE_REF" projects/Generic/patches/ffmpeg/ | grep -m1 '/ffmpeg-9977-')
  [ -n "$P" ] || { rg_item slkeep FAILED "no ffmpeg-9977 in the tree"; return 1; }
  rg_log "$L/build-slkeep-after.log" sl_ff "$SL/after" - -
  rg_log "$L/build-slkeep-before.log" sl_ff "$SL/before" '^ffmpeg-9977-' -
  for n in after before; do
    v=$(rg_pass "$L/build-slkeep-$n.log" '^FF: (built in|.*already built)' '^FF: (PATCH FAILED|BUILD FAILED|EDIT|sha256|no |slkeep.c does not)')
    rg_item "build-slkeep-$n" "$(rg_say "$v")" "$(rg_rest "$v")$(grep -a -m1 '^FFSRC' "$L/build-slkeep-$n.log" | sed 's/^FFSRC/;/')"
  done
  if [ -x "$SL/after/slkeep" ] && [ -x "$SL/before/slkeep" ] && [ -f "$SL/va/drivers/hadesfake_drv_video.so" ]; then
    rg_log "$L/slkeep.log" slcheck after
    v=$(rg_pass "$L/slkeep.log" '^[0-9]+ streams, 1 and 4 threads$' '^FAIL')
    rg_item slkeep "$(rg_say "$v")" "ffmpeg-9977: $(grep -c '^ok:' "$L/slkeep.log") ok; $(rg_rest "$v")"
  else rg_item slkeep FAILED "no libva and fakeva.c, or no FFmpeg with and without ffmpeg-9977"; fi
  while IFS=: read -r m n what; do
    rg_log "$L/build-slkeep-mutant-$m.log" sl_ff "$SL/mutant-$m" - "$HERE/mutants/slkeep-$m.py"
    if [ -x "$SL/mutant-$m/slkeep" ]; then
      rg_log "$L/slkeep-mutant-$m.log" slcheck "mutant-$m"
      v=$(rg_fails "$L/slkeep-mutant-$m.log" "$n" '^FAIL')
      rg_item "slkeep.mutant-$m" "$(rg_say "$v")" "$what: $(rg_rest "$v")"
    else
      rg_item "slkeep.mutant-$m" FAILED "the mutant does not build or its edit is no longer in the patch: $(grep -a -m1 -E '^FF: |EDIT NOT' "$L/build-slkeep-mutant-$m.log")"
    fi
  done < <(printf '%s\n' "$SL_MUTANTS")
}

rg_main "inputs build fate hls playback mvcmkv slkeep" "$@"
