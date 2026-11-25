#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-dvbridge-meta.sh: host tests of what Kodi's Dolby Vision bridge (tools/dvbridge) sends and measures, and of
# the Kodi side of HDR10+, on the Kodi tree of REGRESS_KODI_REF; the GL tests on a software GPU (zink on lavapipe).
# Every check has its other direction: a mutant of the tree (tests/meta/mut_*.py, made by tests/meta/mutants.py) that
# takes out what the check protects must make it fail, with the number of failed checks measured when it was written.
#   inputs      the inputs of inputs-dvbridge-meta.tsv, and every mutant's texts found once in the tree
#   serializer  the metadata serializer (serializer_test.c): its checks, and the payload of every frame against
#               ref/meta/serializer-bytes.txt
#   hdr10plus   the HDR10+ InfoFrame of a picture, the pictures without metadata, what the display is sent
#               (hdr10plus_vsif_test.cpp, hdr10plus_test.cpp, hdr10plus_gap_test.cpp)
#   rows        pass 2 of the source transport against the integer filter on the CPU (rows_test.c)
#   pack        the pack keeps the GL state and writes the bytes of ref/meta/pack.hashes (pack_state_test.c)
#   cover       frames of a profile 7 FEL stream drawn with things over them and packed, and the direct way into the
#               window: their bytes against ref/meta/cover-*.hashes (cover_test_direct.c)
# usage: run-dvbridge-meta.sh all | verdict [<log>] | <stage>...      (settings: lib/regress.sh)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=../lib/regress.sh
. "$HERE/../lib/regress.sh"
rg_init dvbridge-meta
T=$HERE/tests/meta
SRC=$HERE/src
REF=$HERE/ref/meta
IN=$RG_W/in
MUT="$RG_PY $T/mutants.py"
EGL=/usr/lib/x86_64-linux-gnu/libEGL.so.1
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}

# the trees and libraries every stage takes (each stage runs on its own)
setup() {
  K=$(rg_kodi) || rg_fail "no Kodi tree of $REGRESS_KODI_REF"
  LP=$(rg_lp_shared) || rg_fail "libplacebo of the recipe"
  FF=$(rg_ff_bridge) || rg_fail "FFmpeg of the recipe"
  GLINC=$(rg_glinc) || rg_fail "the GL headers of the build folder"
  VK=$(rg_vkinc)
  [ -n "$VK" ] || rg_fail "the Vulkan headers of the build folder"
}
# need <input name>...: the inputs a test reads are there (stage inputs links them)
need() {
  local n
  for n in "$@"; do [ -e "$IN/$n" ] || { echo "BUILD FAILED: no input $n (stage inputs)"; return 1; }; done
}
# mutants <spec> <test>: the names of the mutants of a test; wanted <spec> <name>: its expected failures
mutants() { $MUT list "$1" "$2" | cut -d' ' -f1; }
wanted() { $MUT list "$1" | awk -v m="$2" '$1 == m {print $3}'; }
# mitem <item> <log> <expected>: the verdict of a run on a mutant (made by rg_log); "nobuild": it must not build
mitem() {
  local name=$1 L=$2 want=$3 v
  if grep -a -q '^MUTANT NOT APPLIED' "$L"; then
    v="FAILED the mutant no longer applies to the tree ($(grep -a -m1 '^MUTANT NOT APPLIED' "$L" | cut -c1-160)): write it again for the code of today"
  elif [ "$want" = nobuild ]; then
    if grep -a -q '^BUILD FAILED' "$L"; then v="PASSED the test does not build against the mutant, as it must"
    else v="FAILED the test builds against the mutant"; fi
  elif grep -a -q '^BUILD FAILED' "$L"; then
    v="FAILED the test does not build against the mutant: $(grep -a -m1 -E 'error|BUILD FAILED' "$L" | cut -c1-160)"
  else v=$(rg_fails "$L" "$want" '^FAIL'); fi
  rg_item "$name" "$(rg_say "$v")" "$(rg_rest "$v") [$($MUT what "$SPEC" "${name#mutant-}" 2> /dev/null)]"
}
glrun() { local bin=$1 secs=$2; shift 2; env $RG_GPU_ENV nice -n 19 timeout "$secs" "$bin" "$@"; }

# --------------------------------------------------------------------------------------------------- inputs
stage_inputs() {
  local v
  v=$(rg_inputs "$HERE/inputs-dvbridge-meta.tsv" "$IN" 2>&1); echo "$v"
  rg_item inputs "$(echo "$v" | grep -q -E '^INPUT (MISSING|WRONG|BAD)' && echo FAILED || echo PASSED)" "$(echo "$v" | tail -n 1)"
  setup
  echo "Kodi $REGRESS_KODI_REF = $(rg_kodi_commit); libplacebo $LP; FFmpeg $FF"
  local s n
  # (the mutants of the kernel's headers are checked against the kernel's files, in the stage kernel)
  for s in "$T"/mut_*.py; do
    n=$(basename "$s" .py)
    [ "$n" = mut_kernel ] && continue
    if $MUT check "$s" "$K" > "$RG_W/logs/texts-$n.log" 2>&1; then
      rg_item "texts-$n" PASSED "$(tail -n 1 "$RG_W/logs/texts-$n.log") ($(basename "$s"))"
    else
      rg_item "texts-$n" FAILED "$(tail -n 1 "$RG_W/logs/texts-$n.log"): $(grep -v 'once each' "$RG_W/logs/texts-$n.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"
    fi
  done
}

# --------------------------------------------------------------------------------------------------- serializer
# bser <tree root> <output binary>: serializer_test.c with dvbridge_core.c and dvbridge_placebo.c of that tree
bser() {
  local S=$1/tools/dvbridge O=$2.obj CF
  rg_fresh "$O"
  CF="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -I$LP/include -I$FF/include -I$GLINC -I$VK -I$S"
  # shellcheck disable=SC2086
  gcc $CF -c "$S/dvbridge_core.c" -o "$O/core.o" && gcc $CF -c "$S/dvbridge_placebo.c" -o "$O/placebo.o" &&
    gcc $CF -c "$T/serializer_test.c" -o "$O/test.o" &&
    gcc -o "$2" "$O"/*.o "$LP/lib/libplacebo.a" -L"$FF/lib" -lavutil -lm -lpthread -ldl || { echo "BUILD FAILED"; rg_rm "$O"; return 1; }
  rg_rm "$O"
}
ser_run() { bser "$1" "$2" && "$2"; }
ser_mut() { $MUT make "$SPEC" "$1" "$K" "$2/m" && ser_run "$2/m" "$2/ser-m"; }
stage_serializer() {
  setup
  local O=$RG_W/ser L n d v m
  SPEC=$T/mut_meta.py
  rg_fresh "$O"
  L=$RG_W/logs/serializer.log
  rg_log "$L" ser_run "$K" "$O/ser"
  grep -a '^BYTES' "$L" > "$O/bytes.txt"
  v=$(rg_pass "$L" '^[0-9]+ checks, 0 failed')
  rg_item serializer "$(rg_say "$v")" "$(rg_rest "$v")"
  n=$(wc -l < "$O/bytes.txt"); d=$(diff "$REF/serializer-bytes.txt" "$O/bytes.txt" | grep -c '^>')
  if [ "$d" = 0 ] && [ "$n" = "$(wc -l < "$REF/serializer-bytes.txt")" ]; then
    rg_item serializer-bytes PASSED "the payloads of the $n frames prepared are those of ref/meta/serializer-bytes.txt"
  else
    # how they differ: the change of size of each payload that differs from the reference
    m=$($RG_PY - "$REF/serializer-bytes.txt" "$O/bytes.txt" <<'PY'
import sys
a = [l.split() for l in open(sys.argv[1])]
b = [l.split() for l in open(sys.argv[2])]
deltas = {}
for x, y in zip(a, b):
    if x != y:
        k = '%+d' % (int(y[3]) - int(x[3])) if len(x) > 3 and len(y) > 3 and x[2] == y[2] == 'size' else 'other'
        deltas[k] = deltas.get(k, 0) + 1
print('; '.join('%d of them %s bytes' % (v, k) for k, v in sorted(deltas.items())) or 'none in the lines both have')
PY
)
    rg_item serializer-bytes FAILED "$d of $n payloads differ from ref/meta/serializer-bytes.txt ($(wc -l < "$REF/serializer-bytes.txt") lines): $m"
  fi
  for m in $(mutants "$SPEC" serializer); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" ser_mut "$m" "$O"
    mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- hdr10plus
# bcxx <test .cpp> <output> <tree root> [<mutant tree root>]: a host test of the Kodi side of HDR10+
bcxx() {
  local inc="-I$3/xbmc -I$3/xbmc/windowing/gbm"
  [ -n "${4:-}" ] && inc="-I$4/xbmc -I$4/xbmc/windowing/gbm $inc"
  # shellcheck disable=SC2086
  g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -I"$FF/include" $inc "$1" "$FF/lib/libavutil.a" -lm -lpthread -o "$2" ||
    { echo "BUILD FAILED"; return 1; }
}
cxx_run() { bcxx "$1" "$2" "$K" && "$2"; }
cxx_mut() { $MUT make "$SPEC" "$1" "$K" "$3/m" && bcxx "$2" "$3/m-bin" "$K" "$3/m" && "$3/m-bin"; }
stage_hdr10plus() {
  setup
  local O=$RG_W/h10 t L v m
  SPEC=$T/mut_meta.py
  rg_fresh "$O"
  for t in vsif:hdr10plus_vsif_test h10:hdr10plus_test gap:hdr10plus_gap_test; do
    L=$RG_W/logs/${t%%:*}.log
    rg_log "$L" cxx_run "$T/${t#*:}.cpp" "$O/${t%%:*}"
    v=$(rg_pass "$L" '^[0-9]+ checks, 0 failed')
    rg_item "${t%%:*}" "$(rg_say "$v")" "$(rg_rest "$v")"
    for m in $(mutants "$SPEC" "${t%%:*}"); do
      L=$RG_W/logs/mut-$m.log
      rg_log "$L" cxx_mut "$m" "$T/${t#*:}.cpp" "$O"
      mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
    done
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- rows, pack
# bgl <test .c> <output> <tree root> [pack]: a GL test with dvbridge_gl_pack.c of that tree (and pack_sources.h)
bgl() {
  local S=$3/tools/dvbridge O=$2.obj CF
  rg_fresh "$O"
  if [ "${4:-}" = pack ]; then $RG_PY "$RG_REGRESS/oracle/pack_sources.py" "$S" "$O" || { echo "BUILD FAILED pack_sources.h"; return 1; }; fi
  CF="-std=gnu11 -O2 -g -Wall -Wno-unused-function -I$FF/include -I$GLINC -I$S -I$O"
  # shellcheck disable=SC2086
  gcc $CF -c "$S/dvbridge_gl_pack.c" -o "$O/pack.o" && gcc $CF -c "$1" -o "$O/test.o" && rg_shim "$O" &&
    gcc -o "$2" "$O/pack.o" "$O/test.o" "$O/gles_shim.o" -L"$FF/lib" -lavutil -lm -ldl -pthread "$EGL" ||
    { echo "BUILD FAILED"; rg_rm "$O"; return 1; }
  rg_rm "$O"
}
gl_run() { bgl "$1" "$2" "$K" "${3:-}" && glrun "$2" 3000; }
gl_mut() { $MUT make "$SPEC" "$1" "$K" "$3/m" && bgl "$2" "$3/m-bin" "$3/m" "${4:-}" && glrun "$3/m-bin" 3000; }
stage_rows() {
  setup
  local O=$RG_W/rows L v m
  SPEC=$T/mut_meta.py
  rg_fresh "$O"
  L=$RG_W/logs/rows.log
  rg_log "$L" gl_run "$T/rows_test.c" "$O/rows"
  v=$(rg_pass "$L" '^[0-9]+ checks, 0 failed')
  rg_item rows "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 '^codes compared' "$L" | cut -c1-120)"
  for m in $(mutants "$SPEC" rows); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" gl_mut "$m" "$T/rows_test.c" "$O"
    mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
  done
  rg_rm "$O/m"
}
stage_pack() {
  setup
  local O=$RG_W/pack L v m d
  SPEC=$T/mut_meta.py
  rg_fresh "$O"
  L=$RG_W/logs/pack.log
  rg_log "$L" gl_run "$T/pack_state_test.c" "$O/pack" pack
  v=$(rg_pass "$L" '^[0-9]+ checks, 0 failed')
  rg_item pack "$(rg_say "$v")" "$(rg_rest "$v")"
  grep -a '^pack:' "$L" > "$O/pack.hashes"
  d=$(diff "$REF/pack.hashes" "$O/pack.hashes" | grep -c '^[<>]')
  rg_item pack-bytes "$([ "$d" = 0 ] && [ -s "$O/pack.hashes" ] && echo PASSED || echo FAILED)" \
    "$(wc -l < "$O/pack.hashes") packs, $d lines differ from ref/meta/pack.hashes"
  for m in $(mutants "$SPEC" pack); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" gl_mut "$m" "$T/pack_state_test.c" "$O" pack
    mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
  done
  # the mutants of the bytes: the checks of the pack still pass; the bytes are what differ from the reference
  for m in $(mutants "$SPEC" packbytes); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" gl_mut "$m" "$T/pack_state_test.c" "$O" pack
    grep -a '^pack:' "$L" > "$O/pack-m.hashes"
    d=$(diff "$REF/pack.hashes" "$O/pack-m.hashes" | grep -c '^>')
    if grep -a -q -E '^(MUTANT NOT APPLIED|BUILD FAILED)' "$L"; then mitem "mutant-$m" "$L" -
    else rg_item "mutant-$m" "$([ "$d" -gt 0 ] && echo PASSED || echo FAILED)" \
      "the bytes of $d of $(wc -l < "$O/pack-m.hashes") packs differ from the reference [$($MUT what "$SPEC" "$m")]"; fi
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- cover
# the GL calls that the cover test counts go through its own wrappers
COVER_WRAPS="-Wl,--wrap=glCompileShader -Wl,--wrap=glLinkProgram -Wl,--wrap=glGenTextures -Wl,--wrap=glTexStorage2D -Wl,--wrap=glGenFramebuffers"
# bbridge <tree root> <test .c> <output>: a test of the whole bridge (rg_bridge), "BUILD FAILED" when it does not build
bbridge() {
  rg_bridge "$1" "$LP" "$FF" "$2" "$3" || { echo "BUILD FAILED: $(grep -a -m1 -E 'FAIL|error' "$3.build.log")"; tail -n 20 "$3.build.log"; return 1; }
  echo "compiler warnings: $(grep -a -c 'warning:' "$3.build.log")"
}
cover_run() { need BL_EL.mkv && LDEXTRA=$COVER_WRAPS bbridge "$1" "$T/cover_test_direct.c" "$2" && glrun "$2" 3000 "$IN/BL_EL.mkv" "$3" 2; }
cover_mut() { $MUT make "$SPEC" "$1" "$K" "$2/m" && cover_run "$2/m" "$2/cover-m" "$3"; }
stage_cover() {
  setup
  local O=$RG_W/cover L v m mode d
  SPEC=$T/mut_meta.py
  rg_fresh "$O"
  for mode in frames window; do
    L=$RG_W/logs/cover-$mode.log
    rg_log "$L" cover_run "$K" "$O/cover" "$mode"
    v=$(rg_pass "$L" '^ALL PASSED')
    rg_item "cover-$mode" "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -c '^ok:' "$L") checks ok"
    # the bytes of every frame (the level 5 block that each sends is checked by the test itself)
    grep -a -E '^(frame|direct):' "$L" | sed 's/, L5 sent.*//' > "$O/$mode.hashes"
    d=$(diff "$REF/cover-$mode.hashes" "$O/$mode.hashes" | grep -c '^[<>]')
    rg_item "cover-$mode-bytes" "$([ "$d" = 0 ] && [ -s "$O/$mode.hashes" ] && echo PASSED || echo FAILED)" \
      "$(wc -l < "$O/$mode.hashes") lines of bytes, $d lines differ from ref/meta/cover-$mode.hashes"
  done
  for m in $(mutants "$SPEC" cover); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" cover_mut "$m" "$O" frames
    mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
  done
  rg_rm "$O/m" "$O/cover-m"
}

# --------------------------------------------------------------------------------------------------- passtimes
# the arithmetic of the GPU time of each pass, without a GPU (dvbridge_passtime.h, DVPassTimes.h)
bpass() { g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -I"$2/tools/dvbridge" -I"$K/xbmc/windowing/gbm" "$T/passtimes_test.cpp" -o "$1" || { echo "BUILD FAILED"; return 1; }; }
pass_run() { bpass "$1" "$K" && "$1"; }
pass_mut() { $MUT make "$SPEC" "$1" "$K" "$2/m" && bpass "$2/m-bin" "$2/m" && "$2/m-bin"; }
stage_passtimes() {
  setup
  local O=$RG_W/passtimes L v m
  SPEC=$T/mut_measure.py
  rg_fresh "$O"
  L=$RG_W/logs/passtimes.log
  rg_log "$L" pass_run "$O/passtimes"
  v=$(rg_pass "$L" '^PASSTIMES PASSED: [0-9]+ checks, 0 failed')
  rg_item passtimes "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in $(mutants "$SPEC" passtimes); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" pass_mut "$m" "$O"
    mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- render, measured
# The render test with the CCM 001 reader (src/dvrender_oracle.c: pictures of the profile 7 FEL stream drawn the two
# ways Kodi draws them) and the measured test (src/measured_oracle.c: HDR10 measured as Dolby Vision), each with the
# GPU time of each pass wired in (passtime_wire.py): off, they are the plain tests.
#   render full (3 24 6), off and on; half (3 24 6 half); reshaping (DV_CCM=0); measured (12 20), off and on
# brender <tree root> <output folder>: render and measured, built against that tree
brender() {
  $RG_PY "$T/passtime_wire.py" dvrender "$SRC/dvrender_oracle.c" "$2/dvrender_pt.c" &&
    $RG_PY "$T/passtime_wire.py" measured "$SRC/measured_oracle.c" "$2/measured_pt.c" || { echo "BUILD FAILED: passtime_wire.py"; return 1; }
  rg_bridge "$1" "$LP" "$FF" "$2/dvrender_pt.c" "$2/render" "$SRC" || { echo "BUILD FAILED render: $(grep -a -m1 -E 'FAIL|error' "$2/render.build.log")"; return 1; }
  rg_bridge "$1" "$LP" "$FF" "$2/measured_pt.c" "$2/measured" "$SRC" || { echo "BUILD FAILED measured: $(grep -a -m1 -E 'FAIL|error' "$2/measured.build.log")"; return 1; }
  echo "built: render, measured; compiler warnings $(cat "$2/render.build.log" "$2/measured.build.log" | grep -a -c 'warning:')"
}
# rrun <log> <0: as it is | 1: the pass times on | 8: on, one render in 8 timed | resh: the composer by libplacebo's
# reshaping> <binary> <arguments...>:
# a run of a GL test, its whole output in its log
rrun() {
  local L=$1 how=$2 bin=$3 e=DV_GL_TEST=1
  shift 3
  case $how in 1) e=DV_PASSTIMES=1 ;; 8) e=DV_PASSTIMES=8 ;; resh) e=DV_CCM=0 ;; esac
  rg_log "$L" env "$e" $RG_GPU_ENV nice -n 19 timeout 2400 "$bin" "$@"
}
reader() { grep -a -m1 '^CCM 001 reader:' "$1" | cut -c1-140; }
stage_render() {
  setup
  need BL_EL.mkv hdr10_240.mkv || rg_fail "inputs"
  local O=$RG_W/render L v a b n reread
  rg_fresh "$O"
  L=$RG_W/logs/render-build.log
  rg_log "$L" brender "$K" "$O"
  if ! [ -x "$O/render" ] || ! [ -x "$O/measured" ]; then
    for v in oracle-render-full oracle-render-half oracle-measured k24 passtime-render passtime-measured passtime-sampled; do
      rg_item "$v" FAILED "not built: $(grep -a -m1 'BUILD FAILED' "$L" | cut -c1-160)"; done
    return 0
  fi
  rrun "$RG_W/logs/render-off.log" 0 "$O/render" "$IN/BL_EL.mkv" 3 24 6
  rrun "$RG_W/logs/render-on.log" 1 "$O/render" "$IN/BL_EL.mkv" 3 24 6
  rrun "$RG_W/logs/render-half.log" 0 "$O/render" "$IN/BL_EL.mkv" 3 24 6 half
  rrun "$RG_W/logs/render-resh.log" resh "$O/render" "$IN/BL_EL.mkv" 3 24 6
  rrun "$RG_W/logs/measured-off.log" 0 "$O/measured" "$IN/hdr10_240.mkv" 12 20
  rrun "$RG_W/logs/measured-on.log" 1 "$O/measured" "$IN/hdr10_240.mkv" 12 20
  rrun "$RG_W/logs/render-sampled.log" 8 "$O/render" "$IN/BL_EL.mkv" 3 24 6
  rrun "$RG_W/logs/measured-sampled.log" 8 "$O/measured" "$IN/hdr10_240.mkv" 12 20
  # the CCM 001 reader: every transport of every way read as the clause says
  for v in render-full:render-off render-half:render-half measured:measured-off; do
    L=$RG_W/logs/${v#*:}.log
    reread=$(reader "$L")
    a=$(rg_pass "$L" '^ALL PASSED')
    if [ "$(rg_say "$a")" = PASSED ] && echo "$reread" | grep -q -E '^CCM 001 reader: [1-9][0-9]* transports read, 0 not as the clause says'; then
      rg_item "oracle-${v%%:*}" PASSED "ALL PASSED; $reread"
    else rg_item "oracle-${v%%:*}" FAILED "$(rg_rest "$a"); ${reread:-no line of the reader}"; fi
  done
  # K24: the colour of the float picture within [0, 1] and its alpha PICTURE_ALPHA, full, half and with the reshaping
  n=0; b=""
  for v in render-off render-half render-resh; do
    L=$RG_W/logs/$v.log
    a=$(rg_pass "$L" '^ALL PASSED')
    if [ "$(rg_say "$a")" = PASSED ] && grep -a -q '^ok: *the colour of the float picture holds only values within \[0, 1\]' "$L" &&
       grep -a -q '^ok: *its alpha is 2 (PICTURE_ALPHA) on every pixel' "$L"; then n=$((n + 1)); else b="$b $v: $(rg_rest "$a");"; fi
  done
  rg_item k24 "$([ $n = 3 ] && echo PASSED || echo FAILED)" "the float picture: colour within [0, 1] and alpha 2, ALL PASSED in $n of 3 (full, half, reshaping)$b"
  # the pass times: off and on, the same drawings; on, the passes timed
  grep -a -E '^picture [0-9]+.*: margins' "$RG_W/logs/render-off.log" > "$O/render-off.draw"
  grep -a -E '^picture [0-9]+.*: margins' "$RG_W/logs/render-on.log" > "$O/render-on.draw"
  a=$(rg_pass "$RG_W/logs/render-on.log" '^ALL PASSED'); b=$(rg_pass "$RG_W/logs/render-off.log" '^ALL PASSED')
  if [ "$(rg_say "$a")" = PASSED ] && [ "$(rg_say "$b")" = PASSED ] && [ -s "$O/render-on.draw" ] && cmp -s "$O/render-off.draw" "$O/render-on.draw" &&
     grep -a -q '^ok: .*passes are timed' "$RG_W/logs/render-on.log"; then
    rg_item passtime-render PASSED "off and on ALL PASSED, the same $(wc -l < "$O/render-on.draw") drawing lines; on: $(grep -a -m1 '^pass times:' "$RG_W/logs/render-on.log" | cut -c1-110)"
  else rg_item passtime-render FAILED "off: $(rg_rest "$b"); on: $(rg_rest "$a"); drawing lines $(wc -l < "$O/render-off.draw") and $(wc -l < "$O/render-on.draw"), $(cmp -s "$O/render-off.draw" "$O/render-on.draw" && echo same || echo DIFFER)"; fi
  grep -a -E '^picture [0-9]+: ' "$RG_W/logs/measured-off.log" > "$O/measured-off.draw"
  grep -a -E '^picture [0-9]+: ' "$RG_W/logs/measured-on.log" > "$O/measured-on.draw"
  a=$(rg_pass "$RG_W/logs/measured-on.log" '^ALL PASSED'); b=$(rg_pass "$RG_W/logs/measured-off.log" '^ALL PASSED')
  if [ "$(rg_say "$a")" = PASSED ] && [ "$(rg_say "$b")" = PASSED ] && [ -s "$O/measured-on.draw" ] && cmp -s "$O/measured-off.draw" "$O/measured-on.draw" &&
     grep -a -q '^ok: .*packing has the time' "$RG_W/logs/measured-on.log"; then
    rg_item passtime-measured PASSED "off and on ALL PASSED, the same $(wc -l < "$O/measured-on.draw") drawing lines; on: $(grep -a -m1 '^pass times:' "$RG_W/logs/measured-on.log" | cut -c1-110)"
  else rg_item passtime-measured FAILED "off: $(rg_rest "$b"); on: $(rg_rest "$a"); drawing lines $(wc -l < "$O/measured-off.draw") and $(wc -l < "$O/measured-on.draw"), $(cmp -s "$O/measured-off.draw" "$O/measured-on.draw" && echo same || echo DIFFER)"; fi
  sampled "$O" "$RG_W/logs/render-sampled.log" "$RG_W/logs/measured-sampled.log" passtime-sampled
}

# sampled <folder> <render log> <measured log> <item>: one render in 8 timed (Kodi's default, DV_PASSTIMES=8): the same
# drawings as without the timing, ALL PASSED (the measured test: the packing timed in the renders timed alone), and
# libplacebo's passes of the render test with at most a quarter of the times of the run that times every render
sampled() {
  local O=$1 R=$2 M=$3 item=$4 a b on sm
  grep -a -E '^picture [0-9]+.*: margins' "$R" > "$O/render-sampled.draw"
  grep -a -E '^picture [0-9]+: ' "$M" > "$O/measured-sampled.draw"
  a=$(rg_pass "$R" '^ALL PASSED'); b=$(rg_pass "$M" '^ALL PASSED')
  on=$(grep -a -m1 '^pass times:' "$RG_W/logs/render-on.log" | sed -n "s/.* passes, \([0-9]*\) times of libplacebo's.*/\1/p")
  sm=$(grep -a -m1 '^pass times:' "$R" | sed -n "s/.* passes, \([0-9]*\) times of libplacebo's.*/\1/p")
  if [ "$(rg_say "$a")" = PASSED ] && [ "$(rg_say "$b")" = PASSED ] && cmp -s "$O/render-off.draw" "$O/render-sampled.draw" &&
     cmp -s "$O/measured-off.draw" "$O/measured-sampled.draw" && [ -s "$O/render-sampled.draw" ] &&
     grep -a -q '^ok: .*timed one render in n' "$M" && [ -n "$on" ] && [ -n "$sm" ] && [ "$sm" -gt 0 ] && [ $((sm * 4)) -le "$on" ]; then
    rg_item "$item" PASSED "one render in 8 timed: ALL PASSED, the same drawings; libplacebo's times $sm of $on; measured: $(grep -a -m1 '^pass times:' "$M" | cut -c1-120)"
  else rg_item "$item" FAILED "render: $(rg_rest "$a"); measured: $(rg_rest "$b"); libplacebo's times ${sm:-?} sampled, ${on:-?} every render; drawings $(cmp -s "$O/render-off.draw" "$O/render-sampled.draw" && echo same || echo DIFFER)/$(cmp -s "$O/measured-off.draw" "$O/measured-sampled.draw" && echo same || echo DIFFER); $(grep -a -m1 '^FAIL' "$M" | cut -c1-100)"; fi
}

# The mutants of the render and measured tests: the defects of the transport that only the reader sees, the pass
# times taken out, the clamp of K24 taken out. Each is built and run on its own (render 3 24 2, measured 12 20).
mrender() { $MUT make "$SPEC" "$1" "$K" "$2/m" && brender "$2/m" "$2"; }
stage_mutants() {
  setup
  need BL_EL.mkv hdr10_240.mkv || rg_fail "inputs"
  local O=$RG_W/rmut m test want L B n f
  SPEC=$T/mut_measure.py
  rg_fresh "$O"
  for m in $($MUT list "$SPEC" | awk '$2 ~ /^(oracle-render|oracle-measured|passtime|passtime-measured|passtime-sampled|k24)$/ {print $1}'); do
    test=$($MUT list "$SPEC" | awk -v m="$m" '$1 == m {print $2}'); want=$(wanted "$SPEC" "$m")
    B=$RG_W/logs/mut-$m-build.log
    rg_rm "$O/render" "$O/measured"
    rg_log "$B" mrender "$m" "$O"
    if grep -a -q -E '^(MUTANT NOT APPLIED|BUILD FAILED)' "$B"; then mitem "mutant-$m" "$B" "$want"; continue; fi
    L=$RG_W/logs/mut-$m.log
    case $test in
      oracle-render) rrun "$L" 0 "$O/render" "$IN/BL_EL.mkv" 3 24 2 ;;
      oracle-measured) rrun "$L" 0 "$O/measured" "$IN/hdr10_240.mkv" 12 20 ;;
      k24) rrun "$L" 0 "$O/render" "$IN/BL_EL.mkv" 3 24 6 half ;;
      passtime) rrun "$L" 1 "$O/render" "$IN/BL_EL.mkv" 3 24 2; rrun "$L.measured" 1 "$O/measured" "$IN/hdr10_240.mkv" 12 20 ;;
      passtime-measured) rrun "$L" 1 "$O/measured" "$IN/hdr10_240.mkv" 12 20 ;;
      passtime-sampled) rrun "$L" 8 "$O/measured" "$IN/hdr10_240.mkv" 12 20 ;;
    esac
    case $want in
      reader)
        n=$(grep -a -E '^FAIL' "$L" | grep -a -v -c -E 'every transport reads as ETSI GS CCM 001|^FAILED$')
        if grep -a -q '^FAIL: every transport reads as ETSI GS CCM 001' "$L"; then
          rg_item "mutant-$m" PASSED "the reader of CCM 001 sees it ($(reader "$L" | cut -c17-80)); other checks of the test that fail: $n [$($MUT what "$SPEC" "$m")]"
        else rg_item "mutant-$m" FAILED "the reader of CCM 001 does not see it: exit $(rg_exit "$L"); other checks that fail: $n [$($MUT what "$SPEC" "$m")]"; fi ;;
      bridge)
        n=$(cat "$L" "$L.measured" | grep -a -c '^FAIL: the bridge times the passes')
        rg_item "mutant-$m" "$([ "$n" = 2 ] && echo PASSED || echo FAILED)" "'the bridge times the passes' fails in $n of 2 tests (render, measured) [$($MUT what "$SPEC" "$m")]" ;;
      placebo)
        f=$(grep -a -c '^FAIL: libplacebo draws none of these pictures' "$L")
        rg_item "mutant-$m" "$([ "$f" = 1 ] && echo PASSED || echo FAILED)" "measured, timed: 'libplacebo draws none of these pictures' fails: $f; $(grep -a -m1 '^pass times:' "$L" | cut -c1-90) [$($MUT what "$SPEC" "$m")]" ;;
      sampled)
        f=$(grep -a -c '^FAIL: timed one render in n' "$L")
        rg_item "mutant-$m" "$([ "$f" = 1 ] && echo PASSED || echo FAILED)" "measured, one render in 8 timed: 'the bridge's packing has the time of the renders timed alone' fails: $f; $(grep -a -m1 '^pass times:' "$L" | cut -c1-100) [$($MUT what "$SPEC" "$m")]" ;;
      clamp)
        f=$(grep -a -c '^FAIL: the colour of the float picture holds only values within \[0, 1\]' "$L")
        rg_item "mutant-$m" "$([ "$f" = 1 ] && echo PASSED || echo FAILED)" "half picture: 'the colour of the float picture holds only values within [0, 1]' fails: $f; $(grep -a -c '^FAIL' "$L") FAIL lines in all [$($MUT what "$SPEC" "$m")]" ;;
    esac
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- dvplus
# Dolby Vision sent as HDR10+ and the Kodi side of HDR10+ that needs no kernel: the module's decisions (dvplus_test.c),
# the bytes of its InfoFrame (dvplus_bytes_test.cpp), its GPU samples (dvplus_gl_test.c, 8 pictures of the FEL
# stream), the policy of the streams converted for it (dvplus_policy_test.cpp), its clock (dvplus_clock_test.cpp), the
# native path with four shapes of metadata (native_shapes_test.cpp), Kodi's reading of NVKMS's HDMI VIC rule
# (nvkms_vic_cross_test.cpp against the rule that NVKMS 615 printed for 593 EDIDs, input nvkms-vicrule-615.txt) and the
# vblank (dvplus_vblank_test.cpp). Each with the mutants of mut_dvplus.py that name its test.
# dvp_build <test> <output> <tree root> [<mutant tree root>]: one of them against that tree (a mutant tree first)
dvp_build() {
  local t=$1 out=$2 R=$3 M=${4:-} inc S CXX="g++ -std=c++20 -O1 -g -Wall -Wextra" FFI="-I$FF/include"
  S=${M:-$R}/tools/dvbridge
  inc="-I$R/xbmc"; [ -n "$M" ] && inc="-I$M/xbmc $inc"
  # shellcheck disable=SC2086
  case $t in
    unit) gcc -O2 -g -Wall -Wextra -I"$S" $FFI -o "$out" "$T/dvplus_test.c" "$S/dvbridge_dvplus.c" "$FF/lib/libavutil.a" -lm -lpthread ;;
    bytes|clock)
      gcc -c -O1 -Wall -Wextra -I"$S" $FFI -o "$out.dvplus.o" "$S/dvbridge_dvplus.c" &&
        $CXX $inc -I"$S" $FFI -o "$out" "$T/$([ $t = bytes ] && echo dvplus_bytes_test || echo dvplus_clock_test).cpp" "$out.dvplus.o" "$FF/lib/libavutil.a" -lm -lpthread ;;
    policy) $CXX -I"${M:-$R}/xbmc/cores/VideoPlayer/DVDCodecs/Video" -o "$out" "$T/dvplus_policy_test.cpp" ;;
    native) $CXX $inc $FFI -o "$out" "$T/native_shapes_test.cpp" "$FF/lib/libavutil.a" -lm -lpthread ;;
    nvvic) $CXX $inc -o "$out" "$T/nvkms_vic_cross_test.cpp" ;;
    vblank) $CXX $inc -o "$out" "$T/dvplus_vblank_test.cpp" ;;
    gl) bbridge "${M:-$R}" "$T/dvplus_gl_test.c" "$out" ;;
  esac || { echo "BUILD FAILED $t"; return 1; }
}
# dvp_run <test> <binary>
dvp_run() {
  case $1 in
    gl) need BL_EL.mkv && glrun "$2" 1800 "$IN/BL_EL.mkv" 8 ;;
    nvvic) need nvkms-vicrule-615.txt edid/oldtv edid/crafted && vicrule "$IN/nvkms-vicrule-615.txt" "$2.rule" && "$2" "$2.rule" ;;
    *) "$2" ;;
  esac
}
# vicrule <rule file> <out>: the rule file with each EDID that a line names (by the path it was printed with) found
# among the inputs edid/ by its file name
vicrule() {
  $RG_PY - "$1" "$2" "$IN/edid" <<'PY'
import os, re, sys
src, out, root = sys.argv[1:4]
by_name = {}
for d, dirs, files in os.walk(root, followlinks=True):
    for f in files:
        by_name.setdefault(f, []).append(os.path.join(d, f))
n = bad = 0
with open(out, 'w') as o:
    for line in open(src):
        m = re.match(r'MODE (\S+) ', line)
        if m:
            found = by_name.get(os.path.basename(m.group(1)), [])
            if len(found) != 1:
                bad += 1
                continue
            line = line[:m.start(1)] + found[0] + line[m.end(1):]
            n += 1
        o.write(line)
print('vicrule: %d lines with their EDID among the inputs, %d without one (or with two)' % (n, bad))
sys.exit(1 if bad or not n else 0)
PY
}
dvp_one() { dvp_build "$1" "$2" "$K" && dvp_run "$1" "$2"; }
dvp_mut() { $MUT make "$SPEC" "$1" "$K" "$3/m" && dvp_build "$2" "$3/m-bin" "$K" "$3/m" && dvp_run "$2" "$3/m-bin"; }
stage_dvplus() {
  setup
  local O=$RG_W/dvplus L v m t re
  SPEC=$T/mut_dvplus.py
  rg_fresh "$O"
  for t in unit bytes gl policy clock native nvvic vblank; do
    L=$RG_W/logs/dvplus-$t.log
    rg_log "$L" dvp_one "$t" "$O/$t"
    case $t in
      unit|gl|policy|clock) re='^ALL PASSED: ([0-9]+) of \1$' ;;
      *) re='^PASSED: [0-9]+ (checks|mismatches|tests)?|^PASSED' ;;
    esac
    v=$(rg_pass "$L" "$re")
    rg_item "dvplus-$t" "$(rg_say "$v")" "$(rg_rest "$v")"
    for m in $(mutants "$SPEC" "$t"); do
      L=$RG_W/logs/mut-$m.log
      rg_log "$L" dvp_mut "$m" "$t" "$O"
      mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
    done
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- kernel
# The HDR10+ headers of the image's kernel (drm_hdr10plus.h, amdgpu_dm_hdr10plus.h, intel_hdr10plus.h: linux-9952,
# linux-9953, linux-9954 and linux-9972 of projects/Generic/patches/linux) and what Kodi decides with them:
#   khost   their framing, byte by byte (khost_test.c), with stand-ins for the kernel headers they include (tests/meta/
#           kstub), each checked against the kernel first
#   kodi    the decisions of Kodi (version gate, mode start, VRR) with the model of i915 built from intel_hdr10plus.h
#           (hdr10plus_kodi_test.cpp, i915_model.c)
#   cross   Kodi and i915 read the same HDR10+ application version of every EDID of the corpus (edid_cross_test.cpp,
#           kedid.c; inputs edid/...)
# The files come from the archive of the kernel of the recipe (checked against its PKG_SHA256) with every section of
# the recipe's patches that changes them (kernel_files.py; the Generic project, LINUX=default: the patches of
# packages/linux/patches/default, then those of projects/Generic/patches/linux, as scripts/unpack takes them).
KFILES="include/linux/hdmi.h include/drm/drm_hdr10plus.h drivers/gpu/drm/amd/display/dc/dc_types.h
  drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_hdr10plus.h drivers/gpu/drm/i915/display/intel_hdr10plus.h"
kfiles() { # <dest>
  local D=$1 pk ver sha name tb P
  pk=$(git -C "$REGRESS_LE_GIT" show "$REGRESS_LE_REF:packages/linux/package.mk") || return 1
  ver=$(echo "$pk" | awk '/^  \*\)/ {f = 1} f && /PKG_VERSION=/ {gsub(/^[^"]*"|".*$/, ""); print; exit}')
  sha=$(echo "$pk" | awk '/^  \*\)/ {f = 1} f && /PKG_SHA256=/ {gsub(/^[^"]*"|".*$/, ""); print; exit}')
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "no version or PKG_SHA256 of the kernel (LINUX=default) at $REGRESS_LE_REF"; return 1; }
  # the archive's name: PKG_SOURCE_NAME where the recipe sets one (an archive of a commit, as for a release candidate:
  # linux-${LINUX}-${PKG_VERSION}.tar.gz), else kernel.org's linux-<version>.tar.xz
  name=$(echo "$pk" | awk '/^  \*\)/ {f = 1} f && /PKG_SOURCE_NAME=/ {gsub(/^[^"]*"|".*$/, ""); print; exit}')
  name=${name:-linux-$ver.tar.xz}
  name=${name//\$\{LINUX\}/default}
  name=${name//\$\{PKG_VERSION\}/$ver}
  case $name in *'$'*) echo "a kernel archive name that is not understood: $name"; return 1;; esac
  tb=$(rg_source linux "$name") || { echo "no $name in REGRESS_SOURCES"; return 1; }
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "the sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  echo "kernel $ver: $tb, sha256 $sha (PKG_SHA256 of $REGRESS_LE_REF)"
  P=$D.patches
  rg_le_tree "$P" packages/linux/patches/default projects/Generic/patches/linux || return 1
  rg_fresh "$D"
  # shellcheck disable=SC2046,SC2086
  $RG_PY "$T/kernel_files.py" "$tb" "$D" $(ls "$P"/packages/linux/patches/default/*.patch | sort) \
    $(ls "$P"/projects/Generic/patches/linux/*.patch | sort) -- $KFILES
}
# kstub_check <kernel root>: the stand-ins of tests/meta/kstub say what the kernel says
kstub_check() {
  local h=$1/include/linux/hdmi.h d=$1/drivers/gpu/drm/amd/display/dc/dc_types.h n
  n=$(grep -A20 '^struct hdmi_avi_infoframe {' "$h" | sed '/^};/q' | grep -c '^\s*unsigned char video_code;')
  [ "$n" = 1 ] || { echo "STUB-MISMATCH hdmi_avi_infoframe (video_code)"; return 1; }
  [ "$(grep -c -E 'HDMI_3D_STRUCTURE_INVALID = -1|HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF = 8|define HDMI_IEEE_OUI 0x000c03' "$h")" = 3 ] ||
    { echo "STUB-MISMATCH hdmi.h"; return 1; }
  n=$(grep -A7 'struct dc_info_packet {' "$d" | tr -d ' \t\n' | grep -c 'boolvalid;uint8_thb0;uint8_thb1;uint8_thb2;uint8_thb3;uint8_tsb\[32\];')
  [ "$n" = 1 ] || { echo "STUB-MISMATCH dc_info_packet"; return 1; }
  echo "the stand-ins say what the kernel says"
}
# kinc <kernel root> <dest>: the three headers as the tests include them
kinc() {
  rg_fresh "$2/drm"
  cp "$1/include/drm/drm_hdr10plus.h" "$2/drm/" && cp "$1/drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_hdr10plus.h" \
    "$1/drivers/gpu/drm/i915/display/intel_hdr10plus.h" "$2/"
}
KCF="-std=gnu11 -Wall -Wextra -Werror"
k_build() { # <test: khost|kodi|cross> <kernel root> <output> [<Kodi mutant root>]
  local inc="-I$K/xbmc"
  [ -n "${4:-}" ] && inc="-I$4/xbmc $inc"
  kstub_check "$2" > /dev/null || { echo "BUILD FAILED: $(kstub_check "$2")"; return 1; }
  kinc "$2" "$3.inc" || { echo "BUILD FAILED: headers"; return 1; }
  # shellcheck disable=SC2086
  case $1 in
    khost) gcc $KCF -I"$T/kstub" -I"$3.inc" -o "$3" "$T/khost_test.c" ;;
    kodi) gcc $KCF -I"$T/kstub" -I"$3.inc" -c -o "$3.i915.o" "$T/i915_model.c" &&
          g++ -std=c++20 -O1 -Wall -Wextra -I"$T" $inc -I"$FF/include" -o "$3" "$T/hdr10plus_kodi_test.cpp" "$3.i915.o" ;;
    cross) gcc $KCF -I"$T/kstub" -I"$3.inc" -c -o "$3.kedid.o" "$T/kedid.c" &&
           g++ -std=c++20 -O1 -Wall -Wextra $inc -o "$3" "$T/edid_cross_test.cpp" "$3.kedid.o" ;;
  esac || { echo "BUILD FAILED $1"; return 1; }
}
k_run() { case $1 in cross) need edid/oldtv edid/crafted && "$2" "$IN/edid/oldtv" "$IN/edid/crafted" ;; *) "$2" ;; esac; }
k_one() { k_build "$1" "$2" "$3" && k_run "$1" "$3"; }
# a mutant of the kernel's headers: khost, and for "kernel-kodi" the decisions of Kodi too; caught when one fails
k_mut() { # <mutant> <kernel root> <work>
  local t
  t=$($MUT list "$T/mut_kernel.py" | awk -v m="$1" '$1 == m {print $2}')
  $MUT make "$T/mut_kernel.py" "$1" "$2" "$3/km" || return 0
  if k_one khost "$3/km" "$3/km-khost"; then
    echo "khost: passes"
    if [ "$t" = kernel-kodi ]; then
      if k_one kodi "$3/km" "$3/km-kodi"; then echo "kodi: passes"; else echo "FAIL: the decisions of Kodi with the model of the mutant"; fi
    fi
  else echo "FAIL: khost_test.c on the mutant"; fi
}
d_kmut() { $MUT make "$SPEC" "$1" "$K" "$4/m" && k_build "$2" "$3" "$4/m-bin" "$4/m" && k_run "$2" "$4/m-bin"; }
stage_kernel() {
  setup
  local O=$RG_W/kernel KR L v m t n
  KR=$O/tree
  rg_fresh "$O"
  L=$RG_W/logs/kernel-files.log
  rg_log "$L" kfiles "$KR"
  if ! grep -a -q '^KERNEL-FILES-DONE' "$L"; then
    rg_item kernel-files FAILED "$(grep -a -m1 -E 'FAILED|no |sha256|NOT THERE' "$L" | cut -c1-160)"
    return 0
  fi
  rg_item kernel-files PASSED "$(grep -a -m1 '^kernel ' "$L" | cut -d, -f1); $(grep -a -c -E '^linux-[0-9]+-.*: ' "$L") patches change the $(echo $KFILES | wc -w) files"
  # the stand-ins, and their check in its other direction: a kernel whose struct hdmi_avi_infoframe has no video_code
  v=$(kstub_check "$KR")
  rg_rm "$O/nostub"; mkdir -p "$O/nostub"; cp -a "$KR/." "$O/nostub/"
  sed -i 's/^\tunsigned char video_code;/\tunsigned char video_id_code;/' "$O/nostub/include/linux/hdmi.h"
  n=$(kstub_check "$O/nostub")
  if [ "$v" = "the stand-ins say what the kernel says" ] && echo "$n" | grep -q 'STUB-MISMATCH hdmi_avi_infoframe'; then
    rg_item kernel-stubs PASSED "$v; a kernel without video_code: $n"
  else rg_item kernel-stubs FAILED "$v; a kernel without video_code: $n"; fi
  rg_rm "$O/nostub"
  for t in khost kodi cross; do
    L=$RG_W/logs/kernel-$t.log
    rg_log "$L" k_one "$t" "$KR" "$O/$t"
    v=$(rg_pass "$L" '^PASSED: [0-9]+ checks, 0 failed')
    rg_item "kernel-$t" "$(rg_say "$v")" "$(rg_rest "$v")$([ $t = cross ] && echo "; $(grep -a -m1 '^EDIDs read' "$L" | cut -c1-100)")"
  done
  # the mutants of the headers: their texts in the kernel's files, then each
  if $MUT check "$T/mut_kernel.py" "$KR" > "$RG_W/logs/texts-mut_kernel.log" 2>&1; then
    rg_item texts-mut_kernel PASSED "$(tail -n 1 "$RG_W/logs/texts-mut_kernel.log") (mut_kernel.py)"
  else rg_item texts-mut_kernel FAILED "$(tail -n 1 "$RG_W/logs/texts-mut_kernel.log"): $(grep -v 'once each' "$RG_W/logs/texts-mut_kernel.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"; fi
  for m in $($MUT list "$T/mut_kernel.py" | cut -d' ' -f1); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" k_mut "$m" "$KR" "$O"
    SPEC=$T/mut_kernel.py
    mitem "mutant-$m" "$L" -
  done
  # the mutants of Kodi's side that these tests catch (mut_dvplus.py: kodi, cross)
  SPEC=$T/mut_dvplus.py
  for t in kodi cross; do
    for m in $(mutants "$SPEC" "$t"); do
      L=$RG_W/logs/mut-$m.log
      rg_log "$L" d_kmut "$m" "$t" "$KR" "$O"
      mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
    done
  done
  rg_rm "$O/m" "$O/km"
}

# --------------------------------------------------------------------------------------------------- menus
# The menus in Dolby Vision: the metadata of a frame of graphics alone, field by field (graphics_meta_test.c), the
# decisions of the menus and of the move of native output into another mode (dvmenus_test.cpp), a frame of the menus
# drawn as Kodi draws it, with Kodi's GUI composite shader of the tree, and packed (dvmenus_render_test.c), and that
# shader against the exact conversion in double precision (guipq_test.c: also the shader with its exact conversion
# taken out, which must be off by more than 2 codes, and its own mutations of the shader).
mn_build() { # <test> <output> <tree root> [<mutant tree root>]
  local t=$1 out=$2 R=$3 M=${4:-} S CF f inc
  S=${M:-$R}/tools/dvbridge
  inc="-I$R/xbmc"; [ -n "$M" ] && inc="-I$M/xbmc $inc"
  # shellcheck disable=SC2086
  case $t in
    meta)
      rg_fresh "$out.obj"
      CF="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare -I$S -I$FF/include -I$LP/include -I$GLINC -I$VK"
      for f in dvbridge_core dvbridge_graphics dvbridge_hdr10plus dvbridge_probe; do
        gcc $CF -c "$S/$f.c" -o "$out.obj/$f.o" || { echo "BUILD FAILED $f.c"; return 1; }
      done
      gcc $CF -c "$T/graphics_meta_test.c" -o "$out.obj/t.o" && gcc -o "$out" "$out.obj"/*.o "$FF/lib/libavutil.a" -lm -pthread ||
        { echo "BUILD FAILED meta"; return 1; }
      rg_rm "$out.obj" ;;
    unit) g++ -std=c++20 -O1 -Wall -Wextra $inc -o "$out" "$T/dvmenus_test.cpp" || { echo "BUILD FAILED unit"; return 1; } ;;
    render) bbridge "${M:-$R}" "$T/dvmenus_render_test.c" "$out" || return 1 ;;
    guipq)
      rg_fresh "$out.obj"
      gcc -std=gnu11 -O1 -g -Wall -I"$GLINC" -c "$T/guipq_test.c" -o "$out.obj/t.o" && rg_shim "$out.obj" &&
        gcc -o "$out" "$out.obj/t.o" "$out.obj/gles_shim.o" -lm "$EGL" || { echo "BUILD FAILED guipq"; return 1; }
      rg_rm "$out.obj" ;;
  esac
}
mn_run() { # <test> <binary> <tree root>
  local sh=$3/system/shaders/GLES/2.0
  case $1 in
    render) glrun "$2" 1200 "$3" ;;
    guipq)
      # the shader without its exact conversion: the branch of GL_FRAGMENT_PRECISION_HIGH taken out (the GUI made PQ
      # or HLG by the LUTs alone, as the shader of 1.0 RC4 did)
      sed 's/^#if defined(GL_FRAGMENT_PRECISION_HIGH)$/#if 0/' "$sh/gles_gui_composite.frag" > "$2.lut.frag"
      [ "$(grep -c '^#if 0$' "$2.lut.frag")" = 1 ] || { echo "FAIL: the exact conversion of the shader is not under #if defined(GL_FRAGMENT_PRECISION_HIGH)"; return 1; }
      glrun "$2" 900 "$2.lut.frag" "$sh/gles_gui_composite.frag" "$sh/gles_gui_composite.vert" ;;
    *) "$2" ;;
  esac
}
mn_one() { mn_build "$1" "$2" "$K" && mn_run "$1" "$2" "$K"; }
mn_mut() { $MUT make "$SPEC" "$1" "$K" "$3/m" && mn_build "$2" "$3/m-bin" "$K" "$3/m" && mn_run "$2" "$3/m-bin" "$3/m"; }
stage_menus() {
  setup
  local O=$RG_W/menus L v m t
  SPEC=$T/mut_menus.py
  rg_fresh "$O"
  for t in meta unit render guipq; do
    L=$RG_W/logs/menus-$t.log
    rg_log "$L" mn_one "$t" "$O/$t"
    v=$(rg_pass "$L" '^ALL PASSED: [0-9]+ checks, 0 failed|^ALL PASSED: [0-9]+ checks')
    rg_item "menus-$t" "$(rg_say "$v")" "$(rg_rest "$v")"
    for m in $(mutants "$SPEC" "$t"); do
      L=$RG_W/logs/mut-$m.log
      rg_log "$L" mn_mut "$m" "$t" "$O"
      mitem "mutant-$m" "$L" "$(wanted "$SPEC" "$m")"
    done
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- mapping
# The default content mapping v4.0 part of the serializer (cm40_test.c on the RPU of a profile 5 picture, input
# p5-51.meta): switched off, the payload byte for byte that of a bridge without it (ref/meta/cm40-off.txt); and the
# average floor of HDR10 measured as Dolby Vision (hades_dump.c over the measurements of two streams, inputs
# measure/*.txt): by default what the bridge sent with its floor (inputs measured/*-all.txt), with the floor at 0 what
# it sent without it (measured/*-noavg.txt).
mp_build() { # <test> <output> <tree root>
  local S=$3/tools/dvbridge
  case $1 in
    cm40) gcc -O1 -Wall -Wextra -I"$S" -I"$FF/include" -o "$2" "$T/cm40_test.c" "$FF/lib/libavutil.a" -lm -lpthread ;;
    avgfloor) gcc -O2 -Wall -I"$S" -I"$FF/include" -I"$LP/include" -o "$2" "$T/hades_dump.c" "$S/dvbridge_measured.c" \
                "$S/dvbridge_hdr10plus.c" "$FF/lib/libavutil.a" -lm -lpthread ;;
  esac || { echo "BUILD FAILED $1"; return 1; }
}
mp_run() { # <test> <binary>: cm40 prints its payload switched off (OFF) and its checks; avgfloor, what it sends for
  # each stream, with the floor and without, against the references
  local s d
  case $1 in
    cm40) need p5-51.meta && "$2" "$IN/p5-51.meta" ;;
    avgfloor)
      need measure/hdr10-a.txt measure/hdr10-b.txt || return 1
      for s in hdr10-a hdr10-b; do
        "$2" "$IN/measure/$s.txt" 1 > "$2.$s-floor.txt" 2> /dev/null < /dev/null
        EM_AVG_FLOOR=0 "$2" "$IN/measure/$s.txt" 1 > "$2.$s-none.txt" 2> /dev/null < /dev/null
        d=$(grep -c '^H' "$2.$s-floor.txt")
        if cmp -s "$2.$s-floor.txt" "$IN/measured/$s-all.txt" && [ "$d" -gt 0 ]; then echo "ok:   $s: the default as sent with the floor ($d pictures)"
        else echo "FAIL: $s: the default differs from what was sent with the floor ($d pictures)"; fi
        if cmp -s "$2.$s-none.txt" "$IN/measured/$s-noavg.txt"; then echo "ok:   $s: the floor at 0 as sent without it ($(awk '$1 == "H" && $6 < 819' "$2.$s-none.txt" | wc -l) averages under 819; with the floor $(awk '$1 == "H" && $6 < 819' "$2.$s-floor.txt" | wc -l))"
        else echo "FAIL: $s: the floor at 0 differs from what was sent without it"; fi
      done
      echo "AVGFLOOR-DONE" ;;
  esac
}
mp_one() { mp_build "$1" "$2" "$K" && mp_run "$1" "$2"; }
# Level 3 in map_to_display() (dvbridge_level1_effective()): pictures 1680 and 2640 of the FEL stream (decoded again by
# src/ref_dump.c, checked against ref/meta/level3-pictures.sha256) and picture 51 of a profile 5 stream, each mapped to
# HDR10 displays of 600 cd/m2 (BT.2390 and spline) and 1000 cd/m2, to SDR, and not mapped (mbe.c), with metadata changed
# for it: a neutral level 3 draws what no level 3 draws; one under the floors of content mapping v2.9 draws what those
# floors alone draw; one that ST 2094-10 6.1.9 refuses (a black shot) draws the level 1 block alone; and the bridge
# notes each where it maps (none where it does not).
L3PICS="p1680 p2640 p5-51"
L3MODES="off:MB_MAP=0 h600:MB_MAP=1,MB_PEAK=600,MB_CURVE=1,MB_GAMUT=1 s600:MB_MAP=1,MB_PEAK=600,MB_CURVE=0,MB_GAMUT=1 h1000:MB_MAP=1,MB_PEAK=1000,MB_CURVE=1,MB_GAMUT=1 sdr:MB_MAP=2"
L3FLOORS="MB_L1MIN=0 MB_L1MAX=2081 MB_L1AVG=1229"
l3_pictures() { # <bin folder> <data folder>: ref_dump and the decoded pictures
  sed 's/thread_count = 16;/thread_count = 8;/' "$SRC/ref_dump.c" > "$1/ref_dump.c"
  CPATH=$(rg_vastub) gcc -std=gnu11 -O2 -g -Wall -Wno-unused-function -I"$FF/include" -I"$K/tools/dvbridge" -o "$1/ref_dump" \
    "$1/ref_dump.c" -L"$FF/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl || { echo "BUILD FAILED ref_dump"; return 1; }
  rg_fresh "$2"
  nice -n 19 "$1/ref_dump" "$IN/BL_EL.mkv" "$2" 1680 2640 > "$1/ref_dump.log" 2>&1 < /dev/null || { tail -n 3 "$1/ref_dump.log"; echo "FAIL: ref_dump"; return 1; }
  ( cd "$2" && sha256sum -c --quiet "$REF/level3-pictures.sha256" ) || { echo "FAIL: the decoded pictures are not those of ref/meta/level3-pictures.sha256"; return 1; }
  echo "pictures 1680 and 2640 decoded and paired: as ref/meta/level3-pictures.sha256"
}
l3_draw() { # <bin> <picture> <mode settings> <out prefix> [env...]: one picture drawn; its .f32 hashed and removed
  local bin=$1 pic=$2 mode=$3 out=$4 spec
  shift 4
  case $pic in
    p5-51) spec="raw:$IN/p5-51.meta:$IN/p5-51-bl.yuv:3840:2160:1" ;;
    *) spec="fid:$L3DATA/$pic" ;;
  esac
  # shellcheck disable=SC2086
  env ${mode//,/ } "$@" $RG_GPU_ENV nice -n 19 timeout 900 "$bin" "$out" "$spec" > "$out.log" 2>&1 < /dev/null ||
    { echo "FAIL: draw $(basename "$out"): exit $?, $(tail -n 1 "$out.log")"; return 1; }
  sha256sum < "$out.f32" | cut -c1-64 > "$out.sha"
  rg_rm "$out.f32"
}
l3_case() { # <bin> <picture> <mode name:settings> <work>: the four comparisons and the notes of one picture and mode
  local bin=$1 pic=$2 name=${3%%:*} mode=${3#*:} W=$4 k b a f u r
  k=$pic-$name
  l3_draw "$bin" "$pic" "$mode" "$W/$k-base" || return 0
  l3_draw "$bin" "$pic" "$mode" "$W/$k-l3n" MB_L3=2048,2048,2048 || return 0
  # shellcheck disable=SC2086
  l3_draw "$bin" "$pic" "$mode" "$W/$k-fl" $L3FLOORS || return 0
  # shellcheck disable=SC2086
  l3_draw "$bin" "$pic" "$mode" "$W/$k-flL3" $L3FLOORS MB_L3=2048,2048,1832 || return 0
  # shellcheck disable=SC2086
  l3_draw "$bin" "$pic" "$mode" "$W/$k-blk" $L3FLOORS MB_L3=2048,1007,1434 || return 0
  same() { cmp -s "$W/$1.sha" "$W/$2.sha" && echo same || echo DIFFERENT; }
  b=$(same "$k-base" "$k-l3n"); u=$(same "$k-fl" "$k-flL3"); f=$(same "$k-fl" "$k-blk")
  [ "$b" = same ] || echo "FAIL: $k: a neutral level 3 draws otherwise than no level 3"
  [ "$u" = same ] || echo "FAIL: $k: a level 3 under the floors of v2.9 draws otherwise than the floors alone"
  [ "$f" = same ] || echo "FAIL: $k: a level 3 refused by 6.1.9 (a black shot) draws otherwise than the level 1 block alone"
  [ "$(grep -c '^NOTE' "$W/$k-base.log")" = 0 ] || echo "FAIL: $k: a note of level 3 without level 3"
  if [ "$name" != off ]; then
    a=$(grep -c '^NOTE level3 added' "$W/$k-l3n.log"); r=$(grep -c '^NOTE level3 refused' "$W/$k-blk.log")
    [ "$a" -ge 1 ] || echo "FAIL: $k: no note of the neutral level 3"
    [ "$r" -ge 1 ] || echo "FAIL: $k: no note of the refused black shot"
  else
    [ "$(cat "$W/$k-l3n.log" "$W/$k-blk.log" | grep -c '^NOTE')" = 0 ] || echo "FAIL: $k: a note of level 3 without the mapping"
  fi
  echo "$k: neutral L3 $b | under the floors $u | black shot refused $f"
}
l3_one() { # <tree root> <work>: build, then every picture and mode
  local p m
  rbuild_l3 "$1" "$2/mbe" || return 1
  for p in $L3PICS; do for m in $L3MODES; do l3_case "$2/mbe" "$p" "$m" "$2"; done; done
  echo "LEVEL3-DONE"
}
rbuild_l3() { rg_bridge "$1" "$LP" "$FF" "$T/mbe.c" "$2" "$SRC" || { echo "BUILD FAILED mbe: $(grep -a -m1 -E 'FAIL|error' "$2.build.log")"; return 1; }; }
l3_mut() { # <mutant> <work>: the first mapped mode of the first picture is enough to see it
  $MUT make "$SPEC" "$1" "$K" "$2/m" && rbuild_l3 "$2/m" "$2/mbe-m" && l3_case "$2/mbe-m" p1680 "${L3MODES#* }" "$2"
}
mp_mut() { $MUT make "$SPEC" "$1" "$K" "$3/m" && mp_build "$2" "$3/m-bin" "$3/m" && mp_run "$2" "$3/m-bin"; }
stage_mapping() {
  setup
  local O=$RG_W/mapping L v m n
  SPEC=$T/mut_mapping.py
  rg_fresh "$O"
  L=$RG_W/logs/mapping-cm40.log
  rg_log "$L" mp_one cm40 "$O/cm40"
  v=$(rg_pass "$L" '^CM40-TEST PASSED')
  rg_item cm40 "$(rg_say "$v")" "$(rg_rest "$v")"
  n=$(grep -a -m1 '^OFF ' "$L")
  rg_item cm40-off "$([ -n "$n" ] && [ "$n" = "$(head -n 1 "$REF/cm40-off.txt")" ] && echo PASSED || echo FAILED)" \
    "switched off, the payload ($(( (${#n} - 4) / 2 )) bytes) $([ "$n" = "$(head -n 1 "$REF/cm40-off.txt")" ] && echo "is" || echo "is NOT") that of a bridge without the part (ref/meta/cm40-off.txt)"
  L=$RG_W/logs/mapping-avgfloor.log
  rg_log "$L" mp_one avgfloor "$O/avgfloor"
  v=$(rg_pass "$L" '^AVGFLOOR-DONE')
  rg_item avgfloor "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a '^ok:' "$L" | cut -c7-80 | tr '\n' ';')"
  # level 3
  L3DATA=$O/data
  L=$RG_W/logs/mapping-level3.log
  if need BL_EL.mkv p5-51.meta p5-51-bl.yuv > "$L" 2>&1 && l3_pictures "$O" "$L3DATA" >> "$L" 2>&1; then
    rg_log "$L.run" l3_one "$K" "$O"
    cat "$L.run" >> "$L"
    n=$(grep -a -c -E '^p[0-9a-z-]+-(off|h600|s600|h1000|sdr): neutral L3 same \| under the floors same \| black shot refused same' "$L")
    v=$(rg_pass "$L.run" '^LEVEL3-DONE')
    if [ "$(rg_say "$v")" = PASSED ] && [ "$n" = 15 ]; then rg_item level3 PASSED "15 of 15 pictures and mappings: a neutral level 3, one under the floors and one refused draw what they must, with their notes"
    else rg_item level3 FAILED "$n of 15 cases all the same; $(rg_rest "$v")"; fi
    for m in $(mutants "$SPEC" level3); do
      rg_log "$RG_W/logs/mut-$m.log" l3_mut "$m" "$O"
      mitem "mutant-$m" "$RG_W/logs/mut-$m.log" -
    done
  else rg_item level3 FAILED "the pictures could not be made: $(grep -a -m1 -E 'FAIL|no input|BUILD' "$L" | cut -c1-160)"; fi
  rg_rm "$L3DATA"
  for m in $(mutants "$SPEC" cm40) $(mutants "$SPEC" avgfloor); do
    L=$RG_W/logs/mut-$m.log
    rg_log "$L" mp_mut "$m" "$($MUT list "$SPEC" | awk -v m="$m" '$1 == m {print $2}')" "$O"
    if [ "$m" = cm40-always ]; then
      n=$(grep -a -m1 '^OFF ' "$L")
      if grep -a -q -E '^(MUTANT NOT APPLIED|BUILD FAILED)' "$L"; then mitem "mutant-$m" "$L" -
      else rg_item "mutant-$m" "$([ "$n" != "$(head -n 1 "$REF/cm40-off.txt")" ] && [ "$(grep -a -c '^FAIL' "$L")" -gt 0 ] && echo PASSED || echo FAILED)" \
        "switched off, the payload $([ "$n" != "$(head -n 1 "$REF/cm40-off.txt")" ] && echo "differs from" || echo "is still") the reference; $(grep -a -c '^FAIL' "$L") checks of the test fail [$($MUT what "$SPEC" "$m")]"; fi
    else mitem "mutant-$m" "$L" -; fi
  done
  rg_rm "$O/m"
}

# --------------------------------------------------------------------------------------------------- the whole run
rg_main "inputs serializer hdr10plus rows pack cover passtimes render mutants dvplus kernel menus mapping" "$@"
