#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-fel.sh: the host tests of the profile 7 enhancement layer in Kodi, on real dual-layer Dolby Vision streams decoded
# in software, built with the sanitizers of address and undefined behaviour (tests/):
#   fel_rpu_test    Kodi's enhancement-layer module (tools/dvbridge/dvbridge_fel.c, the file itself) on the one-track
#                   FEL test stream, its pictures paired as Kodi pairs them: whole, with start codes, an RPU taken out of
#                   one access unit in seven, an RPU that FFmpeg refuses, a fixed pool of 12 surfaces, a seek, two later
#                   starts, a switch to another display, the file with its layers swapped (whole and from packet 20);
#   mel_test        the rule of the MEL in the module (dvbridge_mel.h): five schedules of FEL and MEL, a seek, and the
#                   two-track transport stream;
#   fel_tracks_test the module on streams of two tracks joined as Kodi joins them (DVLayerMerge.cpp), and every start
#                   at a random-access picture;
#   dvmerge_test    the join of two tracks (CDVLayerMerge): the transport stream, its disc form, Matroska, MP4 and the
#                   variants made of them, the file of issue 1, packets made here;
#   fel_drain_test  a drain in the middle of a stream (CDrainRestart, DrainRestart.h): ten cases, and its units.
# Every picture, MEL and dump file of the runs is compared byte for byte with the reference (ref/outputs.sha256), and
# each mutant of the module, of the join and of CDrainRestart (mutants/) must make its test fail as it is told.
# Host test only: it builds and runs everything under $REGRESS_WORK/fel and reads the Kodi tree of $REGRESS_KODI_REF.
# usage: run-fel.sh all | verdict [<log>] | <stage>...        stages: inputs build suite outputs mutants drain
# Settings (environment), besides those of lib/regress.sh: REGRESS_FEL_PAR, how many runs at once (default 3; a run
# takes about 1 GB with the sanitizers).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init fel
IN=$RG_W/in
B=$RG_W/bin
R=$RG_W/runs
M=$RG_W/mut
TESTS=$HERE/tests
PAR=${REGRESS_FEL_PAR:-3}
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
DVL=xbmc/cores/VideoPlayer/DVDDemuxers
DRH=xbmc/cores/VideoPlayer/DVDCodecs/Video
FMT='^(ok|FAIL)'

# ------------------------------------------------------------------------------------------------- building
# build <test> <tree> <binary>: one test of tests/ against the module, the join and CDrainRestart of <tree> (a Kodi
# tree, or a copy of the parts of it that the tests read); its compiler's output in <binary>.log
build() {
  local what=$1 TR=$2 OUT=$3 FF VA S J H O LIBS rc
  FF=$(rg_ff_bridge) || { echo "no FFmpeg for the host" > "$OUT.log"; return 1; }
  VA=$(rg_vastub)
  S=$TR/tools/dvbridge J=$TR/$DVL H=$TR/$DRH O=$OUT.obj
  LIBS="-L$FF/lib -lavformat -lavcodec -lavutil -lm -latomic -pthread"
  rg_fresh "$O"
  {
    case $what in
      rpu)
        gcc -std=gnu11 $SAN -Wall -Wextra -Wno-unused-function -I"$FF/include" -I"$S" -I"$TESTS" -I"$VA" \
          -c "$TESTS/fel_rpu_test.c" -o "$O/t.o" && g++ $SAN -o "$OUT" "$O/t.o" $LIBS ;;
      tracks|mel)
        local src=fel_tracks_test.c
        [ "$what" = mel ] && src=mel_test.c
        g++ -std=c++20 $SAN -Wall -Wextra -I"$FF/include" -I"$J" -I"$TESTS" -c "$TESTS/merge_shim.cpp" -o "$O/shim.o" &&
          g++ -std=c++20 $SAN -Wall -Wextra -I"$FF/include" -I"$J" -I"$S" -c "$J/DVLayerMerge.cpp" -o "$O/merge.o" &&
          gcc -std=gnu11 $SAN -Wall -Wextra -I"$FF/include" -I"$S" -c "$S/dvbridge_probe.c" -o "$O/probe.o" &&
          gcc -std=gnu11 $SAN -Wall -Wextra -Wno-unused-function -I"$FF/include" -I"$S" -I"$TESTS" -I"$VA" \
            -c "$TESTS/$src" -o "$O/t.o" &&
          g++ $SAN -o "$OUT" "$O/t.o" "$O/shim.o" "$O/merge.o" "$O/probe.o" $LIBS ;;
      merge)
        # (as Kodi's demuxer runs it: an error of undefined behaviour stops the run through UBSAN_OPTIONS)
        gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -I"$FF/include" -I"$S" \
          -c "$S/dvbridge_probe.c" -o "$O/probe.o" &&
          g++ -std=c++20 -O1 -g -Wall -Wextra -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer -I"$J" -I"$S" \
            -I"$FF/include" -o "$OUT" "$TESTS/dvmerge_test.cpp" "$J/DVLayerMerge.cpp" "$O/probe.o" $LIBS ;;
      drain)
        gcc -std=gnu11 $SAN -Wall -Wextra -Wno-unused-function -I"$FF/include" -I"$S" -I"$VA" -c "$TESTS/fel_shim.c" \
          -o "$O/shim.o" &&
          g++ -std=c++20 $SAN -Wall -Wextra -I"$H" -I"$FF/include" -I"$S" -I"$TESTS" -c "$TESTS/fel_drain_test.cpp" \
            -o "$O/t.o" &&
          g++ $SAN -o "$OUT" "$O/t.o" "$O/shim.o" $LIBS ;;
      *) echo "no test $what"; false ;;
    esac
  } > "$OUT.log" 2>&1 < /dev/null
  rc=$?
  rg_rm "$O"
  return $rc
}
# mutant_tree <name>: $M/<name>, the parts of the Kodi tree that the tests read, with the edit of mutants/<name>.py
# (status 3: the edit no longer applies)
mutant_tree() {
  local n=$1 K f
  K=$(rg_kodi) || return 1
  rg_fresh "$M/$n/$DVL" "$M/$n/$DRH" "$M/$n/tools"
  cp -a "$K/tools/dvbridge" "$M/$n/tools/" && cp -a "$K/$DVL/DVLayerMerge.cpp" "$K/$DVL/DVLayerMerge.h" "$M/$n/$DVL/" &&
    cp -a "$K/$DRH/DrainRestart.h" "$M/$n/$DRH/" || return 1
  f=$($RG_PY -c 'import runpy, sys; print(runpy.run_path(sys.argv[1])["FILE"])' "$HERE/mutants/$n.py") || return 1
  rg_edit "$M/$n/$f" "$HERE/mutants/$n.py"
}
# built_item <item> <binary>...: PASSED when each one was built with no warning
built_item() {
  local item=$1 b bad="" w=0 n=0
  shift
  for b in "$@"; do
    n=$((n + 1))
    if [ ! -x "$b" ]; then bad="$bad $(basename "$b") ($(grep -a -m1 -E 'error|EDIT|no ' "$b.log" 2> /dev/null | cut -c1-120))"
    else w=$((w + $(grep -a -c 'warning:' "$b.log"))); fi
  done
  if [ -n "$bad" ]; then rg_item "$item" FAILED "not built:$bad"
  elif [ $w -gt 0 ]; then rg_item "$item" FAILED "$w warning lines: $(grep -a -h -m1 'warning:' "$@" 2> /dev/null | head -n 1 | cut -c1-160)"
  else rg_item "$item" PASSED "$n built with the sanitizers, no warning"; fi
}

stage_inputs() {
  if rg_inputs "$HERE/inputs-fel.tsv" "$IN" > "$RG_W/logs/inputs.log" 2>&1; then
    rg_item inputs PASSED "$(tail -n 1 "$RG_W/logs/inputs.log")"
  else
    rg_item inputs FAILED "$(grep -v '^INPUTS ' "$RG_W/logs/inputs.log" | head -n 3 | tr '\n' ' ')"
    return 1
  fi
}

stage_build() {
  local K
  K=$(rg_kodi) || { rg_item build FAILED "no Kodi tree of $REGRESS_KODI_REF"; return 1; }
  rg_ff_bridge > /dev/null || { rg_item build FAILED "the FFmpeg of the recipe for the host was not built: $RG_SHARED/ff-bridge.log"; return 1; }
  rg_fresh "$B"
  echo "Kodi $REGRESS_KODI_REF $(rg_kodi_commit): $(sha256sum "$K/tools/dvbridge/dvbridge_fel.c" "$K/tools/dvbridge/dvbridge_mel.h" \
    "$K/$DVL/DVLayerMerge.cpp" "$K/$DRH/DrainRestart.h" | cut -c1-12 | tr '\n' ' ')"
  local t
  for t in rpu tracks mel merge drain; do
    while [ "$(jobs -r | wc -l)" -ge "$RG_JOBS" ]; do wait -n; done
    build $t "$K" "$B/$t" &
  done
  wait
  built_item build "$B/rpu" "$B/tracks" "$B/mel" "$B/merge" "$B/drain"
  [ -x "$B/rpu" ] && [ -x "$B/tracks" ] && [ -x "$B/mel" ] && [ -x "$B/merge" ] && [ -x "$B/drain" ]
}

# ------------------------------------------------------------------------------------------------- runs
# job <name> <binary> <environment, or -> <arguments...>: a run into $R/<name>.log (its pictures into <name>.pics), then
# "== exit <status> after <s> s"
job() {
  local n=$1 bin=$2 envs=$3 t0 rc
  shift 3
  [ "$envs" = - ] && envs=""
  t0=$(date +%s)
  # shellcheck disable=SC2086
  env ASAN_OPTIONS=detect_leaks=1 $envs FEL_PICTURES="$R/$n.pics" nice -n 19 timeout 3600 "$bin" "$@" > "$R/$n.log" 2>&1 < /dev/null
  rc=$?
  echo "== exit $rc after $(( $(date +%s) - t0 )) s" >> "$R/$n.log"
}
# runs <file of lines "name|binary|environment|arguments">: the runs, $PAR at once
runs() {
  local n b e a
  while IFS='|' read -r n b e a; do
    [ -n "$n" ] || continue
    while [ "$(jobs -r | wc -l)" -ge "$PAR" ]; do wait -n; done
    # shellcheck disable=SC2086
    job "$n" "$b" "$e" $a &
  done < "$1"
  wait
}
# the runs of the suite: name | binary | environment | arguments | ok lines | FAIL lines (the slowest first)
suite_table() {
  local I=$IN/samples T=$IN/truth E N
  E="FEL_TRUTH=$T/truth-mkv.txt FEL_WHOLE=$T/whole.pics FEL_HOLD=8"
  N="ASAN_OPTIONS=detect_leaks=0 $E"
  cat << EOF
swap|$B/rpu|ASAN_OPTIONS=detect_leaks=0 FEL_RPU_IN_BASE=1 FEL_TRUTH=$T/truth-swap-own.txt FEL_HOLD=8|$I/one_track_swap.mkv kodi|24|0
swap20|$B/rpu|ASAN_OPTIONS=detect_leaks=0 FEL_RPU_IN_BASE=1 FEL_TRUTH=$T/truth-swap-own.txt FEL_HOLD=8|$I/one_track_swap.mkv kodi start=20|14|0
tracks-mkv|$B/tracks|FEL_DUMP=$R/dump-mkv.tsv|$I/FEL_test_dual.mkv quick|18|0
tracks-starts|$B/tracks|FEL_DUMP=$R/dump-starts.tsv|$I/FEL_test_for_AVS.m2ts quick starts|20|0
tracks-ts|$B/tracks|FEL_DUMP=$R/dump-ts.tsv|$I/FEL_test_for_AVS.m2ts quick|18|0
whole|$B/rpu|$E|$I/BL_EL.mkv kodi|25|0
annexb|$B/rpu|$E|$I/BL_EL.mkv kodi annexb|26|0
norpu7|$B/rpu|$E|$I/BL_EL.mkv kodi elnorpu=7|26|0
badterm|$B/rpu|$E|$I/BL_EL.mkv kodi badterm=1000|23|4
p12e|$B/rpu|$E FEL_POOL=12 FEL_EXPECT=enough|$I/BL_EL.mkv kodi|26|0
device|$B/rpu|$N|$I/BL_EL.mkv kodi device=600:other|16|0
seek|$B/rpu|$N|$I/BL_EL.mkv kodi seek=600:1500|15|0
start457|$B/rpu|$N|$I/BL_EL.mkv kodi start=457|15|0
start1273|$B/rpu|$N|$I/BL_EL.mkv kodi start=1273|15|0
mel-fel|$B/mel|MEL_OUT=$R/mel-fel.out|$I/BL_EL.mkv fel|1|0
mel-mel|$B/mel|MEL_OUT=$R/mel-mel.out|$I/BL_EL.mkv mel|1|0
mel-aligned|$B/mel|MEL_OUT=$R/mel-aligned.out|$I/BL_EL.mkv aligned|1|0
mel-midgop|$B/mel|MEL_OUT=$R/mel-midgop.out|$I/BL_EL.mkv midgop|1|0
mel-aligned2|$B/mel|MEL_OUT=$R/mel-aligned2.out|$I/BL_EL.mkv aligned2|1|0
mel-seek|$B/mel|MEL_OUT=$R/mel-seek.out|$I/BL_EL.mkv mel seek=600:1500|1|0
mel-ts|$B/mel|MEL_OUT=$R/mel-ts.out|$I/FEL_test_for_AVS.m2ts mel tracks|1|0
merge|$B/merge|UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1|$I/FEL_test_for_AVS.m2ts $I/BL_EL.mkv $I/FEL_test_as_disc.m2ts $I|158|0
EOF
}

stage_suite() {
  rg_fresh "$R"
  suite_table | cut -d'|' -f1-4 > "$RG_W/logs/suite-jobs.txt"
  local t0=$(date +%s)
  runs "$RG_W/logs/suite-jobs.txt"
  echo "the runs of the suite: $(( $(date +%s) - t0 )) s, $PAR at once"
  local n b e a ok fl L gok gfl san x v
  while IFS='|' read -r n b e a ok fl; do
    L=$R/$n.log
    gok=$(grep -a -c '^ok:' "$L" 2> /dev/null); gfl=$(grep -a -c '^FAIL' "$L" 2> /dev/null); san=$(rg_san "$L"); x=$(rg_exit "$L")
    if [ "$n" = badterm ]; then
      # by design: the picture whose RPU FFmpeg refuses has no readable RPU, with the module as before it
      if [ "$gok" = "$ok" ] && [ "${san:-0}" = 0 ] && [ -n "$x" ] && grep -a '^FAIL' "$L" | cmp -s - "$HERE/ref/badterm.fail"; then
        v="PASSED $gok ok, and the $gfl FAIL lines of ref/badterm.fail (the picture without a readable RPU)"
      else v="FAILED exit ${x:-none}, $gok ok (want $ok), $gfl FAIL lines ($(grep -a '^FAIL' "$L" | cmp -s - "$HERE/ref/badterm.fail" && echo 'those' || echo 'NOT those') of ref/badterm.fail), ${san:-0} sanitizer lines"; fi
    elif [ "$x" = 0 ] && [ "$gok" = "$ok" ] && [ "$gfl" = 0 ] && [ "${san:-0}" = 0 ]; then
      v="PASSED $gok ok, 0 FAIL, $(grep -a -o 'after [0-9]* s' "$L" | tail -n 1)"
    else
      v="FAILED exit ${x:-none}, $gok ok (want $ok), $gfl FAIL, ${san:-0} sanitizer lines; $(grep -a -m1 '^FAIL' "$L" | cut -c1-140)"
    fi
    rg_item "suite-$n" "$(rg_say "$v")" "$(rg_rest "$v")"
  done < <(suite_table)
}

stage_outputs() {
  local sha f got same=0 n=0 differ=""
  while read -r sha f; do
    case "$sha" in ''|'#'*) continue ;; esac
    n=$((n + 1))
    got=$(sha256sum < "$R/$f" 2> /dev/null | cut -c1-64)
    if [ "$got" = "$sha" ]; then same=$((same + 1)); else differ="$differ $f"; fi
  done < "$HERE/ref/outputs.sha256"
  if [ $n -gt 0 ] && [ $same = $n ]; then rg_item outputs PASSED "$same of $n pictures, MEL and dump files byte for byte the reference"
  else rg_item outputs FAILED "$same of $n byte for byte the reference; differ or missing:$differ"; fi
}

# ------------------------------------------------------------------------------------------------- mutants
# mutated <name> <test> <binary>: the test built against the mutant <name>; FAILED item mutant-<name> when it is not
mutated() {
  local n=$1 t=$2 out=$3 rc
  mutant_tree "$n" > "$M/$n.edit.log" 2>&1
  rc=$?
  if [ $rc = 3 ]; then rg_item "mutant-$n" FAILED "the mutant no longer applies: $(tail -n 1 "$M/$n.edit.log" | cut -c1-160)"; return 1; fi
  [ $rc = 0 ] || { rg_item "mutant-$n" FAILED "the copy of the tree was not made: $(tail -n 1 "$M/$n.edit.log")"; return 1; }
  build "$t" "$M/$n" "$out" || { rg_item "mutant-$n" FAILED "not built: $(grep -a -m1 error "$out.log" | cut -c1-160)"; return 1; }
}

stage_mutants() {
  local I=$IN/samples T=$IN/truth E
  rg_fresh "$M"
  mkdir -p "$R"
  E="FEL_TRUTH=$T/truth-mkv.txt FEL_WHOLE=$T/whole.pics FEL_HOLD=8"
  local ok_nowait=0 ok_nopeak=0 ok_rev=0
  mutated fel-nowait rpu "$B/m-nowait" && ok_nowait=1
  mutated fel-nopeak rpu "$B/m-nopeak" && ok_nopeak=1
  mutated merge-reversed merge "$B/m-reversed" && ok_rev=1
  : > "$RG_W/logs/mutant-jobs.txt"
  [ $ok_nowait = 1 ] && {
    echo "m-nowait-enough|$B/m-nowait|ASAN_OPTIONS=detect_leaks=0 $E FEL_POOL=12 FEL_EXPECT=enough|$I/BL_EL.mkv kodi"
    echo "m-nowait-named|$B/m-nowait|ASAN_OPTIONS=detect_leaks=0 $E FEL_POOL=12 FEL_EXPECT=named|$I/BL_EL.mkv kodi"
  } >> "$RG_W/logs/mutant-jobs.txt"
  [ $ok_nopeak = 1 ] && echo "m-nopeak|$B/m-nopeak|ASAN_OPTIONS=detect_leaks=0 $E|$I/BL_EL.mkv kodi" >> "$RG_W/logs/mutant-jobs.txt"
  [ $ok_rev = 1 ] && echo "m-reversed|$B/m-reversed|ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1|$I/FEL_test_for_AVS.m2ts $I/BL_EL.mkv $I/FEL_test_as_disc.m2ts $I" >> "$RG_W/logs/mutant-jobs.txt"
  runs "$RG_W/logs/mutant-jobs.txt"
  local v
  if [ $ok_nowait = 1 ]; then
    # without the wait, a fixed pool of 12 runs out where the layer needs 16: the layer stops 86 times, so the check
    # of the pool and four of what follows from it fail (pictures not paired, and so with other RPUs)
    v=$(rg_fails "$R/m-nowait-enough.log" 5 '^FAIL:')
    rg_item mutant-fel-nowait "$(rg_say "$v")" "$(rg_rest "$v") (no wait for a free surface, a fixed pool of 12: $(grep -a -m1 '^FAIL:' "$R/m-nowait-enough.log" | cut -c7-120))"
    # ... and the module says so, every time, with its count of the surfaces right (what the wait is built on)
    if grep -a -q -x 'ok:   the module counts the surfaces of its layer as they are held, at most and now' "$R/m-nowait-named.log" &&
       grep -a -q -x 'ok:   the pool runs out, and every time the layer stops for it the module says so (not a decoder error)' "$R/m-nowait-named.log" &&
       [ "$(rg_san "$R/m-nowait-named.log")" = 0 ]; then
      rg_item surfaces-named PASSED "without the wait the pool of 12 runs out ($(grep -a -m1 -o '[0-9]* times none was free' "$R/m-nowait-named.log")), and each time the layer stops the module says why, with its count of the surfaces as they were held"
    else
      rg_item surfaces-named FAILED "the exhaustion of the pool is not named, or the count is wrong: $(grep -a -E '^FAIL: .*(surfaces|pool)' "$R/m-nowait-named.log" | head -n 2 | tr '\n' ' ' | cut -c1-200)"
    fi
  else
    rg_item surfaces-named FAILED "not run: the mutant fel-nowait was not built"
  fi
  if [ $ok_nopeak = 1 ]; then
    v=$(rg_fails "$R/m-nopeak.log" 1 '^FAIL:')
    rg_item mutant-fel-nopeak "$(rg_say "$v")" "$(rg_rest "$v") (the most surfaces held not counted: $(grep -a -m1 '^FAIL:' "$R/m-nopeak.log" | cut -c7-120))"
  fi
  if [ $ok_rev = 1 ]; then
    # as before that rule: the group of FEL_test_dual_blrecord.mkv and of BL_EL_plus_1080.mkv refused for their size
    v=$(rg_fails "$R/m-reversed.log" 3 '^FAIL:')
    rg_item mutant-merge-reversed "$(rg_say "$v")" "$(rg_rest "$v") (a Matroska group the wrong way round refused for its size)"
  fi
}

# ------------------------------------------------------------------------------------------------- drain
# the cases: name | arguments (the run without the drain, the same otherwise, is each one's reference)
drain_cases() {
  local I=$IN/samples
  cat << EOF
a1-midgop|$I/BL_EL.mkv from=97 count=40 dry=14
a2-midgop-partial|$I/BL_EL.mkv from=97 count=40 dry=14 pull=2
a3-seek-then-dry|$I/BL_EL.mkv from=1 seek=8:600 count=40 dry=3 pull=1
a4-annexb-partial|$I/BL_EL.mkv annexb from=97 count=40 dry=14 pull=2
a5-at-the-access-point|$I/BL_EL.mkv from=97 count=40 dry=1 pull=2
a6-nopipeline-stall|$I/BL_EL.mkv from=97 count=40 dry=14 hw=1 pull=0 stall=3
a7-swap-reorders|$I/one_track_swap.mkv from=20 count=40 dry=10 pull=2
a8-one-track|$I/one_track.mkv from=20 count=40 dry=10 pull=2
a9-layer-started|$I/BL_EL.mkv from=1 count=40 dry=20 pull=2
a10-times-go-back|$I/BL_EL.mkv from=1 count=60 dry=20 pull=2 jump=1000000
EOF
}
# the mutants of CDrainRestart: name | <case>:<FAIL lines> the cases it runs and the checks of each that must fail
# (units: the checks of CDrainRestart alone). Measured on the tree: nowait fails a3, a7, a9 and a10 of the cases, and
# fromlast all but a5 (one packet kept either way); the others fail every case.
DRAIN_MUTANTS="drain-nomark|units:2 a10-times-go-back:1
drain-bytime|units:1
drain-nodrop|units:2 a10-times-go-back:1
drain-nowait|units:2 a3-seek-then-dry:2 a10-times-go-back:3
drain-fromlast|units:1 a2-midgop-partial:2 a3-seek-then-dry:2
drain-norestart|units:4 a2-midgop-partial:2 a3-seek-then-dry:2"

stage_drain() {
  local K n a c m cases want v built=""
  K=$(rg_kodi) || return 1
  mkdir -p "$R" "$M"
  : > "$RG_W/logs/drain-jobs.txt"
  while IFS='|' read -r n a; do
    echo "$n|$B/drain|FEL_DRAIN_LOSS=2|$a mode=after" >> "$RG_W/logs/drain-jobs.txt"
  done < <(drain_cases)
  echo "units|$B/drain|-|- units" >> "$RG_W/logs/drain-jobs.txt"
  while IFS='|' read -r m cases; do
    mutated "$m" drain "$B/m-$m" || continue
    built="$built $m"
    for c in $cases; do
      c=${c%%:*}
      if [ "$c" = units ]; then a="- units"
      else a="$(drain_cases | awk -F'|' -v c="$c" '$1 == c {print $2}') mode=after"; fi
      echo "m-$m-$c|$B/m-$m|ASAN_OPTIONS=detect_leaks=0 FEL_DRAIN_LOSS=2|$a" >> "$RG_W/logs/drain-jobs.txt"
    done
  done <<< "$DRAIN_MUTANTS"
  runs "$RG_W/logs/drain-jobs.txt"
  while IFS='|' read -r n a; do
    v=$(rg_pass "$R/$n.log" '^ALL PASSED$' '^FAIL')
    rg_item "drain-$n" "$(rg_say "$v")" "$(rg_rest "$v")"
  done < <(drain_cases)
  v=$(rg_pass "$R/units.log" '^ALL PASSED$' '^FAIL')
  rg_item drain-units "$(rg_say "$v")" "$(rg_rest "$v")"
  while IFS='|' read -r m cases; do
    case " $built " in *" $m "*) ;; *) continue ;; esac
    local det="" bad=""
    for c in $cases; do
      want=${c#*:}; c=${c%%:*}
      v=$(rg_fails "$R/m-$m-$c.log" "$want" '^FAIL:')
      case $v in PASSED*) det="$det $c: $want ($(grep -a -m1 '^FAIL:' "$R/m-$m-$c.log" | cut -c7-80));" ;; *) bad="$bad $c: $(rg_rest "$v");" ;; esac
    done
    if [ -z "$bad" ]; then rg_item "mutant-$m" PASSED "it fails as it must:$det"
    else rg_item "mutant-$m" FAILED "$bad"; fi
  done <<< "$DRAIN_MUTANTS"
}

rg_main "inputs build suite outputs mutants drain" "$@"
