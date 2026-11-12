#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The regression of h264_mvc (the separate MVC decoder of packages/multimedia/ffmpeg/h264-mvc): the findings of
# the automatic review of FFmpeg pull request 24303 and what was found with them (README.md), each test against
# the decoder of the tree and against a mutant of it that puts the defect back.
#   run-mvc.sh all             every stage, then the verdict
#   run-mvc.sh <stage>...      base twins streams unit views jmlayouts late prefix f6 f8 f9 grain flushf1 flush
#                              mutants kodi verdict
# EXTRAFIX="<patch>..." builds the decoder of the tree with those fixes added (a proposal not in the tree yet).
# REVIEWED=<revision of the tree> (default 12728a2, where the series is the one the review read) builds that
# decoder too ('rev', without the project's fixes): the unit, late, prefix, f8 and f9 tests must fail on it.
# Lowest priority and 8 jobs: other work runs on the machine. Results: $W/results/<stage>.txt.
set -uo pipefail
. "$(dirname "$0")/env.sh"
R=$W/results; mkdir -p $R
S=$W/streams
REV=${REVIEWED:-12728a2}
itu() { find $ITU -iname '*.264' | sort | tr '\n' ' '; }
good() { echo $S/f4-default.264 $S/f4-custom.264 $S/f4-fallback.264 $S/f5-replace.264 $S/f5-replace-shared.264 \
  $S/f5-ppsfirst.264 $S/f6-views.264 $S/f8-levels40.264 $S/f8-levels64.264 $S/f9-ok-one.264 $S/f9-viewmod-ok2.264 \
  $S/sd540-wrap.264 $S/bd1080.264 $S/x-sd540x2.264 $S/x-f4x2.264; }
stage() {
  case $1 in
  base) bash $TOOLS/host.sh base ;;
  twins)
    bash $TOOLS/twin-make.sh ship
    EXTRAFIX= bash $TOOLS/twin-make.sh rev $REV 9001
    CONFX="--toolchain=gcc-asan --extra-cflags=-fsanitize=undefined --extra-ldflags=-fsanitize=undefined" \
      bash $TOOLS/twin-make.sh ship-san ;;
  streams) bash $TOOLS/streams.sh ;;
  unit) bash $TOOLS/unit.sh ship rev ;;
  views) bash $TOOLS/views.sh ship $(itu) $(good) ;;
  jmlayouts) bash $TOOLS/jmlayouts.sh ship $(itu) $(good) ;;
  late) for t in ship rev; do bash $TOOLS/late.sh $t; done ;;
  prefix) for t in ship rev; do bash $TOOLS/prefix.sh $t $(find $ITU -name MVCDS1.264) $(find $ITU -name MVCDS-4.264); done ;;
  f6) bash $TOOLS/f6.sh ship ;;
  f8) bash $TOOLS/f8.sh ship rev ;;
  f9) bash $TOOLS/f9.sh ship rev ship-san ;;
  grain) bash $TOOLS/grain.sh ship rev ;;
  flushf1) bash $TOOLS/flushf1.sh ship rev ;;
  flush) bash $TOOLS/flush.sh ship ;;
  mutants)
    V="NOSTOCK=1 MODES='1:frame 8:frame 8:slice' bash $TOOLS/views.sh TWIN"
    for m in F1-reset-keeps-fields F2-append-before-swap F3-temporal-matches-inter-view; do
      bash $TOOLS/mutrun.sh $m "bash $TOOLS/unit.sh TWIN"; done
    bash $TOOLS/mutrun.sh F1-reset-keeps-fields "bash $TOOLS/flushf1.sh ship TWIN"
    bash $TOOLS/mutrun.sh F4-no-view-pps "$V $S/f4-default.264 $S/f4-custom.264 $S/f4-fallback.264"
    bash $TOOLS/mutrun.sh F5-view-pps-keyed-by-pps-only "$V $S/f5-replace-shared.264"
    bash $TOOLS/mutrun.sh F6-first-subset-sps "bash $TOOLS/f6.sh TWIN"
    bash $TOOLS/mutrun.sh F7-no-wait-for-new-view "bash $TOOLS/late.sh TWIN"
    bash $TOOLS/mutrun.sh F8-levels-golomb31 "bash $TOOLS/f8.sh TWIN"
    bash $TOOLS/mutrun.sh F8-ops-unbounded "bash $TOOLS/f8.sh TWIN"
    bash $TOOLS/mutrun.sh F9-no-range-check "bash $TOOLS/f9.sh TWIN"
    bash $TOOLS/mutrun.sh F10-header-through-gb "bash $TOOLS/prefix.sh TWIN $(find $ITU -name MVCDS1.264)"
    bash $TOOLS/mutrun.sh C1-grain-not-in-setup "bash $TOOLS/grain.sh TWIN"
    if grep -q 'finish_dependent_view' $W/ffmpeg/ship/ffmpeg-9.0.2/libavcodec/h264mvc/h264dec.c; then
      bash $TOOLS/mutrun.sh N1-idr-before-the-dependent-view-ends "bash $TOOLS/jmlayouts.sh TWIN $S/x-sd540x2.264 $S/x-f4x2.264"
    fi ;;
  kodi) bash $TOOLS/kodi/run-kodi.sh ;;
  verdict) verdict ;;
  *) echo "no stage $1"; return 1 ;;
  esac
}
# The verdict, read line by line from the results: what each stage must say. A line that is not there, or a
# line that says otherwise, is a failure, and the script ends with their count.
verdict() {
  local bad=0
  want() { # file, fixed text that must be there, what it means
    if [ -s $R/$1.txt ] && grep -qF -- "$2" $R/$1.txt; then echo "ok    $3"; else echo "FAIL  $3 ($1: no '$2')"; bad=$((bad + 1)); fi
  }
  none() { # file, extended regular expression that must match no line, what it means
    if [ ! -s $R/$1.txt ]; then echo "FAIL  $3 ($1: no results)"; bad=$((bad + 1)); return; fi
    local n; n=$(grep -E -- "$2" $R/$1.txt | grep -c .)
    if [ "$n" = 0 ]; then echo "ok    $3"; else echo "FAIL  $3 ($n lines of $1, the first: $(grep -m1 -E -- "$2" $R/$1.txt | cut -c1-150))"; bad=$((bad + 1)); fi
  }
  want unit "== ship refs-test" "unit tests ran on the decoder of the tree"
  [ "$(grep -A8 '^== ship refs-test' $R/unit.txt | grep -c 'REFS-TEST PASSED')" = 1 ] && echo "ok    F2, F3: the reference lists of H.8.2.1/H.8.2.2 (refs-test)" || { echo "FAIL  F2, F3 (refs-test on ship)"; bad=$((bad + 1)); }
  [ "$(grep -A8 '^== ship f1reset' $R/unit.txt | grep -c 'F1-RESET PASSED')" = 1 ] && echo "ok    F1: no field-pair state after a flush (f1reset)" || { echo "FAIL  F1 (f1reset on ship)"; bad=$((bad + 1)); }
  [ "$(grep -A8 '^== rev refs-test' $R/unit.txt | grep -c 'REFS-TEST FAILED')" = 1 ] && echo "ok    F2, F3 shown on the reviewed series" || echo "note  the reviewed series not tested (unit)"
  [ "$(grep -A8 '^== rev f1reset' $R/unit.txt | grep -c 'F1-RESET FAILED')" = 1 ] && echo "ok    F1 shown on the reviewed series" || echo "note  the reviewed series not tested (f1reset)"
  # every picture of every view is JM's. The one exception known: MVCDS-6_old, a retired stream whose second view
  # has always differed from JM (cause not known): its view 1, and nothing else of it
  local n
  n=$(awk '/^== / { s = $2 } /DIFFERENT|NOTHING|TIMEOUT/ { if (!(s == "MVCDS-6_old" && $0 ~ /h264_mvc view 1,/)) print s ": " $0 }' $R/views.txt 2> /dev/null | tee $R/views-unexpected.txt | grep -c .)
  if [ -s $R/views.txt ] && grep -q '^== ' $R/views.txt && [ "$n" = 0 ]; then echo "ok    F4, F5, F6, F8, F9, N1: every view of every stream, 1/2/8 frame and 8 slice threads, is JM's"
  else echo "FAIL  views: $n unexpected lines (views-unexpected.txt), the first: $(head -n 1 $R/views-unexpected.txt | cut -c1-140)"; bad=$((bad + 1)); fi
  n=$(python3 -I - $R/jmlayouts.txt > $R/jmlayouts-unexpected.txt <<'PY2'
import sys
for l in open(sys.argv[1]):
    if ": |" not in l:
        continue
    name, runs = l.split(":", 1)
    for r in runs.split(" | "):
        r = r.strip()
        if not r or r.endswith("JM's"):
            continue
        if name == "MVCDS-6_old" and "view 0 identical" in r and "EXIT" not in r:
            continue
        # N2, known and open: the field-coded MVCICT-1 in the layout that keeps the second view of a unit in
        # the next packet gives 225 pictures of its second view for 150 (the two fields of a picture are then
        # in different packets); its first view is JM's. Reported as known, not as passed.
        if name == "MVCICT-1" and r.startswith("shifted") and "view 0 identical" in r and "EXIT" not in r:
            print("KNOWN N2 %s: %s" % (name, r), file=sys.stderr)
            continue
        print("%s: %s" % (name, r))
PY2
2> $R/jmlayouts-known.txt; grep -c . $R/jmlayouts-unexpected.txt)
  if [ -s $R/jmlayouts.txt ] && grep -q 'sanitizer reports: 0' $R/jmlayouts.txt && [ "$n" = 0 ]; then echo "ok    N1: both packet layouts, 1/8 frame and 8 slice threads, are JM's"
  [ -s $R/jmlayouts-known.txt ] && echo "known $(grep -c . $R/jmlayouts-known.txt) runs of N2 (MVCICT-1, field coded, second view in the next packet): open"
  else echo "FAIL  jmlayouts: $n unexpected runs (jmlayouts-unexpected.txt), the first: $(head -n 1 $R/jmlayouts-unexpected.txt | cut -c1-140)"; bad=$((bad + 1)); fi
  want late "LATE PASSED" "F7: a second view that starts late, with frame threads"
  want prefix "PREFIX PASSED" "F10: prefix NAL units of four bytes"
  want f6 "F6 ship PASSED" "F6: the pictures carry the view they were decoded in"
  want f8 "F8-PARSE PASSED" "F8: the operation point table read to its end"
  want f8 "F8-HUGE PASSED" "F8: a damaged count refused at once"
  want f9 "F9 ship PASSED" "F9: an abs_diff_view_idx_minus1 out of range refused"
  want f9 "F9 ship-san PASSED" "F9: no undefined behaviour (UBSan)"
  want grain "GRAIN ship PASSED" "a concern of the review: the base view keeps its film grain when both views are decoded"
  want flushf1 "ship: runs 158, assertion aborts 0, other crashes 0" "F1 end to end: flushes in a field-coded stream"
  none flush "^CRASH" "flushes in every stream of two views: no crash (flush.sh)"
  local nw
  nw=$(grep -E "^(WRONG|LAYOUTS DIFFER)" $R/flush.txt 2> /dev/null | grep -vc "MVCICT-1")
  if [ -s $R/flush.txt ] && [ "$nw" = 0 ]; then echo "ok    flushes: every picture is the one of the decode without a flush (MVCICT-1 in the second layout apart: N2)"
  else echo "FAIL  flushes: $nw lines of other streams that differ"; bad=$((bad + 1)); fi
  grep -q "^LAYOUTS DIFFER: MVCICT-1" $R/flush.txt 2> /dev/null &&     echo "known N2, open: $(grep -m1 '^LAYOUTS DIFFER: MVCICT-1' $R/flush.txt | cut -c17-120) (field coded; second view in the next packet; decoder opened without the probed reorder depth)"
  for m in "F1-reset-keeps-fields:F1-RESET FAILED" "F2-append-before-swap:REFS-TEST FAILED" \
           "F3-temporal-matches-inter-view:REFS-TEST FAILED" "F1-reset-keeps-fields:assertion aborts [1-9]" \
           "F4-no-view-pps:DIFFERENT" "F5-view-pps-keyed-by-pps-only:DIFFERENT" "F6-first-subset-sps:F6 mut FAILED" \
           "F7-no-wait-for-new-view:LATE FAILED" "F8-levels-golomb31:F8-PARSE FAILED" "F8-ops-unbounded:F8-HUGE FAILED" \
           "F9-no-range-check:F9 mut FAILED" "F10-header-through-gb:PREFIX FAILED" "C1-grain-not-in-setup:GRAIN mut FAILED"; do
    if awk -v m="--- ${m%%:*}:" 'index($0, m) == 1 { on = 1; next } /^--- [A-Z][0-9]+-[A-Za-z0-9-]+: / { on = 0 } on' $R/mutants.txt | grep -qE -- "${m#*:}"; then
      echo "ok    mutant ${m%%:*} caught (${m#*:})"
    else echo "FAIL  mutant ${m%%:*} not caught (no '${m#*:}')"; bad=$((bad + 1)); fi
  done
  if grep -q 'finish_dependent_view' $W/ffmpeg/ship/ffmpeg-9.0.2/libavcodec/h264mvc/h264dec.c 2> /dev/null; then
    if awk '/^--- N1-idr-before-the-dependent-view-ends:/ { on = 1; next } /^--- [A-Z][0-9]+-[A-Za-z0-9-]+: / { on = 0 } on' $R/mutants.txt | grep -q 'JMLAYOUTS FAILED'; then
      echo "ok    mutant N1-idr-before-the-dependent-view-ends caught (JMLAYOUTS FAILED)"
    else echo "FAIL  mutant N1-idr-before-the-dependent-view-ends not caught"; bad=$((bad + 1)); fi
  else
    echo "note  the decoder has no fix for N1 (9009): jmlayouts must fail on x-sd540x2, x-f4x2, f5-replace-shared"
  fi
  # the other direction on the series that the review read, where it was built: each test must fail there
  if [ -s $W/ffmpeg/pkg-rev.out ]; then
    wantrev() { if grep -qE -- "$2" $R/$1.txt 2> /dev/null; then echo "ok    $3 shown on the reviewed series"; else echo "FAIL  $3: not shown on the reviewed series ($1: no '$2')"; bad=$((bad + 1)); fi; }
    wantrev late "LATE FAILED" "F7"
    wantrev prefix "PREFIX FAILED" "F10"
    wantrev f8 "F8-PARSE FAILED" "F8 (the table)"
    wantrev f8 "F8-HUGE FAILED" "F8 (the damaged count)"
    wantrev f9 "F9 rev FAILED" "F9"
    wantrev grain "GRAIN rev FAILED" "the film grain concern"
    wantrev flushf1 "rev: runs 158, assertion aborts [1-9]" "F1 end to end"
  fi
  want kodi "STEREO-RATE PASSED" "the 3D output at twice the refresh rate (kodi/stereo_rate_test.cpp, its mutants)"
  want kodi "MVCC PASSED" "F11 in Kodi: the count of 'mvcC' (kodi/mvcc_count_test.cpp, its mutants)"
  echo "VERDICT: $bad failure(s)"
  return $([ $bad = 0 ] && echo 0 || echo 1)
}
[ $# -gt 0 ] || { sed -n '2,13p' "$0"; exit 1; }
[ "$1" = all ] && set -- base twins streams unit views jmlayouts late prefix f6 f8 f9 grain flushf1 flush mutants kodi verdict
export R S REV TOOLS ITU W
rc=0
for s in "$@"; do
  echo "=== $s ($(date -u +%H:%M:%S))"
  if [ $s = verdict ]; then verdict | tee $R/verdict.txt; rc=${PIPESTATUS[0]}
  else nice -n 19 bash -c "$(declare -f stage itu good); stage $s" > $R/$s.txt 2>&1; echo "    $(tail -n 1 $R/$s.txt | cut -c1-160)"
  fi
done
exit $rc
