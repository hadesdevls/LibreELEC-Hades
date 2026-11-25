#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-dvbridge-render.sh: what Kodi's Dolby Vision bridge (tools/dvbridge) draws, byte for byte, on a software
# OpenGL ES (zink on lavapipe), with the libplacebo and the FFmpeg of the LibreELEC recipes: the fidelity bench in
# its configurations against the reference sums, the measured test with the CCM 001 reader, the render test with
# its checks and the mutations it must catch, the step-down of a scaled picture, the gamut table, the GUI step, the
# geometry of a view, the composed planes in 16 bits, the composer with a base layer in BT.1886, HDR mapped to SDR in
# BT.2020 and DCI-P3, the step-down levels of Dolby Vision rebuilt as HDR10 and SDR, the 3D LUT of a display (in the
# bridge, and in the video shaders of Kodi's own renderer: stage lutsdr), a 3D picture drawn a view at a time (stage
# stereo3d), an 8-bit HLG picture sent as Dolby Vision the same bytes as the same picture in 10 bits (stage hlg8).
# Host test only.
#
# usage: run-dvbridge-render.sh all | verdict [<log>] | <stage>...
# Inputs (inputs-dvbridge-render.tsv, by SHA256): the FEL stream the pictures are taken from, an HDR10 stream, the
# profile 5 and PQ/HLG charts, an SDR clip of H.264. The pictures of the fidelity bench are decoded again from the
# stream by ref_dump (src/ref_dump.c) and checked against ref/render/fidelity-data.sha256 before anything draws them.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
. "$HERE/lib-dvbridge-render.sh"
rg_init dvbridge-render
SRC=$HERE/src
TS=$HERE/tests/render
REF=$HERE/ref/render
LIST=$HERE/inputs-dvbridge-render.tsv
IN=$RG_W/in
BIN=$RG_W/bin
DATA=$RG_W/data
export LP_NUM_THREADS=${LP_NUM_THREADS:-4}
# the pictures of the stream that the fidelity bench draws
PICS="3 27 51 240 480 720 960 1200 1440 1680 1920 2160 2400 2640"
STAGES="build data fid measured rt r56 rscale gamut sdrgamut convstep gui geom r8 p82 fidx mmr hdr10area startsignal padding wextra p84 hostpol ipt p5gui p84gpu lut3d lutsdr stereo3d hlg8"

# --------------------------------------------------------------------------------------------------- helpers
lp() { rg_lp_shared; }
ff() { rg_ff_bridge; }
gpu() { env $RG_GPU_ENV "$@"; }
# bin <name>: a binary of the stage build, or status 1
bin() { [ -x "$BIN/$1" ] && echo "$BIN/$1"; }
# fid_run <binary> <out folder> <tag> <log> <env...> -- <pictures>: fidbench into <out folder>, then the SHA256 of
# every file it wrote (the info.txt without its line of samples not on a whole code, which counts floats) into
# <log>.sums, and the outputs deleted
fid_run() {
  local b=$1 O=$2 tag=$3 L=$4 f
  shift 4
  local envs=()
  while [ $# -gt 0 ] && [ "$1" != -- ]; do envs+=("$1"); shift; done
  shift
  rg_fresh "$O"
  rg_log "$L" gpu "${envs[@]}" nice -n 19 timeout 3000 "$b" "$DATA" "$O" "$tag" "$@"
  for f in "$O"/*info.txt; do [ -e "$f" ] && sed -i '/samples not on a whole code/d' "$f"; done
  ( cd "$O" && sha256sum * 2> /dev/null ) > "$L.sums"
  rg_rm "$O"
}
# same <file> <reference>: "the same as <ref> (N lines)" (status 0), or what differs (status 1)
same() {
  if [ ! -s "$2" ]; then echo "no reference $2"; return 1; fi
  if [ ! -s "$1" ]; then echo "nothing to compare (empty $(basename "$1"))"; return 1; fi
  if cmp -s "$1" "$2"; then echo "the same as ref/render/$(basename "$2") ($(wc -l < "$1") lines)"; return 0; fi
  echo "DIFFERS from ref/render/$(basename "$2"): $(diff "$2" "$1" | grep -c '^>') of $(wc -l < "$1") lines"
  return 1
}
# kodi_copy <dest>: a copy of the tree under test's tools/dvbridge in <dest>/tools/dvbridge (for a mutant)
kodi_copy() {
  local K
  K=$(rg_kodi) || return 1
  rg_fresh "$1/tools"
  cp -a "$K/tools/dvbridge" "$1/tools/"
}
# mutant <name> <python edit file> <file under tools/dvbridge>: a copy of the bridge with that exact edit, in
# $RG_W/mut/<name> (status 3 when the edit does not apply any more: the mutant must be brought up to date)
mutant() {
  kodi_copy "$RG_W/mut/$1" || return 1
  rg_edit "$RG_W/mut/$1/tools/dvbridge/$3" "$2" > "$RG_W/mut/$1.edit.log" 2>&1 || return 3
}

# --------------------------------------------------------------------------------------------------- stages
stage_build() {
  local L K F b bad=0 built=""
  L=$(lp) || rg_fail "libplacebo"
  F=$(ff) || rg_fail "FFmpeg"
  K=$(rg_kodi) || rg_fail "Kodi tree"
  mkdir -p "$BIN"
  echo "libplacebo: $(tail -n 1 "$RG_SHARED/lp.log")"
  echo "FFmpeg: $(tail -n 1 "$RG_SHARED/ff-bridge.log")"
  echo "Kodi: $(rg_kodi_commit) (tools/dvbridge)"
  # ref_dump: the decoder threads lowered to 8 for a shared machine (the pictures do not depend on it)
  sed 's/thread_count = 16;/thread_count = 8;/' "$SRC/ref_dump.c" > "$BIN/ref_dump.c"
  if CPATH=$(rg_vastub) gcc -std=gnu11 -O2 -g -Wall -Wno-unused-function -I"$F/include" -I"$K/tools/dvbridge" \
       -o "$BIN/ref_dump" "$BIN/ref_dump.c" -L"$F/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl \
       > "$BIN/ref_dump.build.log" 2>&1; then built="$built ref_dump"; else cat "$BIN/ref_dump.build.log"; bad=$((bad + 1)); fi
  for b in "fidbench $SRC/fidbench.c $SRC" "measured $SRC/measured_oracle.c $SRC" "render_test $TS/render_test.c -"; do
    set -- $b
    if rg_bridge "$K" "$L" "$F" "$2" "$BIN/$1" "$([ "$3" = - ] || echo "$3")"; then
      built="$built $1"
      grep -q -i 'warning' "$BIN/$1.build.log" && { echo "warnings building $1:"; grep -i -A3 'warning' "$BIN/$1.build.log" | head -n 20; }
    else
      tail -n 20 "$BIN/$1.build.log"; bad=$((bad + 1))
    fi
  done
  rg_item build "$([ $bad = 0 ] && echo PASSED || echo FAILED)" "built:$built; $bad failed"
}

stage_data() {
  local out n bad
  rg_inputs "$LIST" "$IN" || rg_fail "inputs"
  [ -x "$BIN/ref_dump" ] || { rg_item data FAILED "no ref_dump (stage build)"; return 1; }
  rg_fresh "$DATA"
  rg_log "$RG_W/logs/ref_dump.log" nice -n 19 "$BIN/ref_dump" "$IN/bl_el.mkv" "$DATA" $PICS
  out=$( cd "$DATA" && sha256sum -c "$REF/fidelity-data.sha256" 2>&1 )
  n=$(echo "$out" | grep -c ': OK$'); bad=$(echo "$out" | grep -c -v ': OK$')
  echo "$out" | grep -v ': OK$' | head -n 5
  if [ "$(rg_exit "$RG_W/logs/ref_dump.log")" = 0 ] && [ "$bad" = 0 ] && [ "$n" = 112 ]; then
    rg_item data PASSED "the 14 pictures decoded and paired again from the FEL stream: the 112 files (layers, meta.txt, meta.bin) as ref/render/fidelity-data.sha256"
  else
    rg_item data FAILED "ref_dump exit $(rg_exit "$RG_W/logs/ref_dump.log"), $n files as the reference, $bad not: $(rg_last "$RG_W/logs/ref_dump.log")"
  fi
}

# the configurations of the fidelity bench: tag and settings (FB_*), as Kodi's switches set them
FID_CFGS="src0:FB_SOURCE=1,FB_STEP=0 src1:FB_SOURCE=1,FB_STEP=1 src2:FB_SOURCE=1,FB_STEP=2 cnv0:FB_SOURCE=0 gui:FB_SOURCE=1,FB_GUI=1 resh:FB_CCM=0"
stage_fid() {
  local f c tag envs L v
  f=$(bin fidbench) || { rg_item fid FAILED "no fidbench (stage build)"; return 1; }
  [ -f "$DATA/p3/meta.bin" ] || { rg_item fid FAILED "no pictures (stage data)"; return 1; }
  for c in $FID_CFGS; do
    tag=${c%%:*}; envs=${c#*:}; L=$RG_W/logs/fid-$tag
    # shellcheck disable=SC2086
    fid_run "$f" "$RG_W/tmp/fid-$tag" "$tag" "$L.log" ${envs//,/ } -- $PICS
    v=$(same "$L.log.sums" "$REF/f-$tag.sums")
    if grep -q '^FIDBENCH-OK: 14 of 14 pictures drawn, 0 failures' "$L.log" && [ "$(rg_exit "$L.log")" = 0 ] && cmp -s "$L.log.sums" "$REF/f-$tag.sums"; then
      rg_item fid-$tag PASSED "FIDBENCH-OK 14 of 14 (${envs//,/ }); $(wc -l < "$L.log.sums") files, $v"
    else
      rg_item fid-$tag FAILED "$(grep -a -E '^(FIDBENCH|FAIL)' "$L.log" | head -n 2 | tr '\n' ' ' | cut -c1-160); exit $(rg_exit "$L.log"); files: $v"
    fi
  done
}

stage_measured() {
  local m L s ap orc
  m=$(bin measured) || { rg_item measured-oracle FAILED "no measured test (stage build)"; return 1; }
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  L=$RG_W/logs/measured.log
  rg_log "$L" gpu nice -n 19 timeout 1800 "$m" "$IN/hdr10_240.mkv" 12 20
  grep -a -E '^picture [0-9]+: ' "$L" > "$RG_W/logs/m.draw"
  s=$(same "$RG_W/logs/m.draw" "$REF/m.draw"); ap=$(grep -a -c '^ALL PASSED' "$L"); orc=$(grep -a -c 'reads as ETSI' "$L")
  if [ "$ap" -ge 1 ] && [ "$orc" -ge 1 ] && cmp -s "$RG_W/logs/m.draw" "$REF/m.draw"; then
    rg_item measured-oracle PASSED "ALL PASSED, the CCM 001 reader's line $orc time(s), drawings $s"
  else
    rg_item measured-oracle FAILED "$(grep -a -E '^(ALL PASSED|FAILED|FAIL)' "$L" | head -n 3 | tr '\n' ' ' | cut -c1-200); reader lines $orc; drawings $s"
  fi
}

# rt_lines <log> <lines>: the drawing lines of a render test run that do not depend on the run (no times, no checks)
rt_lines() { grep -a -E '^picture' "$1" | grep -a -v -E ': R2 |R1[35]|EARLY|early hook|plain float' > "$2"; }
stage_rt() {
  local rt L v r2 r13 r15 rc ref c n
  rt=$(bin render_test) || { rg_item rt FAILED "no render test (stage build)"; return 1; }
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  for v in "ccm:X=1" "resh:DV_CCM=0"; do
    c=${v%%:*}; L=$RG_W/logs/rt-$c.log; ref=$REF/rt-$c.lines
    rg_log "$L" gpu "${v#*:}" nice -n 19 timeout 2400 "$rt" "$IN/bl_el.mkv" 3 24 6
    rt_lines "$L" "$RG_W/logs/rt-$c.lines"
    r2=$(grep -a -E '^R2:' "$L" | grep -c '248832000 words compared, 0 differ, 0 in a wrong form')
    r13=$(grep -a -E '^R13:' "$L" | grep -c '1 time(s) by the first picture, 0 after two recoveries, 1 after a reset')
    r15=$(grep -a -E '^R15:' "$L" | grep -c '36 composed pictures checked, 0 composed otherwise than asked')
    n=$(same "$RG_W/logs/rt-$c.lines" "$ref")
    if grep -a -q '^ALL PASSED' "$L" && [ "$r2" = 1 ] && [ "$r13" = 1 ] && [ "$r15" = 1 ] && cmp -s "$RG_W/logs/rt-$c.lines" "$ref"; then
      rg_item rt-$c PASSED "ALL PASSED, R2/R13/R15 as expected, drawing lines $n"
    else
      rg_item rt-$c FAILED "$(grep -a -E '^(ALL PASSED|FAILED|FAIL)' "$L" | head -n 3 | tr '\n' ' ' | cut -c1-200); R2 ok $r2, R13 ok $r13, R15 ok $r15; drawing lines $n"
    fi
  done
  # the half raster: every second sample of the layers, a 1920x1080 picture that the renderer scales
  L=$RG_W/logs/rt-half.log
  rg_log "$L" gpu nice -n 19 timeout 2400 "$rt" "$IN/bl_el.mkv" 3 24 6 half
  if grep -a -q '^ALL PASSED' "$L" && grep -a -q '^R2: 30 pictures drawn both ways (0 not), 248832000 words compared, 0 differ, 0 in a wrong form' "$L"; then
    rg_item rt-half PASSED "ALL PASSED; $(grep -a '^R2:' "$L" | cut -c1-110)"
  else
    rg_item rt-half FAILED "$(grep -a -E '^(FAILED|FAIL|R2:)' "$L" | head -n 3 | tr '\n' ' ' | cut -c1-220)"
  fi
  # mutations of the bridge that the render test must catch
  rt_mutant r2-mutation "$TS/mut-r2-truncate.py" dvbridge_render.c X=1
  L=$RG_W/logs/r2-mutation.log; rc=$(cat "$L.rc" 2> /dev/null)
  if [ "$rc" = 3 ]; then rg_item r2-mutation FAILED "the mutant no longer applies to dvbridge_render.c: bring tests/render/mut-r2-truncate.py up to date"
  elif grep -a -q '^FAILED' "$L" && grep -a -E '^R2:' "$L" | grep -q -v ' 0 differ'; then rg_item r2-mutation PASSED "caught: $(grep -a '^R2:' "$L" | cut -c1-120)"
  else rg_item r2-mutation FAILED "not caught: $(grep -a -E '^(ALL PASSED|FAILED|R2:|BUILD)' "$L" | head -n 2 | tr '\n' ' ' | cut -c1-200)"; fi
  rt_mutant r15-mutation "$TS/mut-r15-always-ccm.py" dvbridge_render.c DV_CCM=0
  L=$RG_W/logs/r15-mutation.log; rc=$(cat "$L.rc" 2> /dev/null)
  if [ "$rc" = 3 ]; then rg_item r15-mutation FAILED "the mutant no longer applies to dvbridge_render.c: bring tests/render/mut-r15-always-ccm.py up to date"
  elif grep -a -q '^FAILED' "$L" && grep -a -E '^R15:' "$L" | grep -q -v ', 0 composed otherwise'; then rg_item r15-mutation PASSED "caught: $(grep -a '^R15:' "$L" | cut -c1-110)"
  else rg_item r15-mutation FAILED "not caught: $(grep -a -E '^(ALL PASSED|FAILED|R15:|BUILD)' "$L" | head -n 2 | tr '\n' ' ' | cut -c1-200)"; fi
  # the strict packer is made from the tree's shader: without its line, or with it twice, the test stops (abort)
  for c in none twice; do
    rt_mutant strict-$c "$TS/mut-strict-$c.py" pack_gles300.frag X=1 stdbuf
  done
  v=PASSED; n=""
  for c in none twice; do
    L=$RG_W/logs/strict-$c.log; want=$([ $c = none ] && echo 0 || echo 2)
    if grep -a -q "^FAIL: the pack shader has '            e = clamp(picture_alone(pe), 0.0, 1.0);' $want times, not 1" "$L" && [ "$(rg_exit "$L")" = 134 ]; then
      n="$n $c: stops ($want times, exit 134);"
    else v=FAILED; n="$n $c: exit $(rg_exit "$L"), $(grep -a -m1 -E '^(FAIL|BUILD|ALL)' "$L" | cut -c1-100);"; fi
  done
  rg_item strict-neither $v "the render test's strict packer needs the fallback fetch of pack_gles300.frag exactly once:$n"
}
# rt_mutant <item> <edit file> <file under tools/dvbridge> <env> [stdbuf]: the render test built on a mutant of the
# bridge and run, into logs/<item>.log (and its status of the edit in .rc: 3 when it no longer applies)
rt_mutant() {
  local n=$1 e=$2 f=$3 env=$4 sb=${5:-} L=$RG_W/logs/$1.log K M
  M=$RG_W/mut/$n
  mutant "$n" "$e" "$f"; echo $? > "$L.rc"
  [ "$(cat "$L.rc")" = 0 ] || { echo "== the edit did not apply" > "$L"; cat "$RG_W/mut/$n.edit.log" >> "$L"; return; }
  if ! rg_bridge "$M" "$(lp)" "$(ff)" "$TS/render_test.c" "$M/rt"; then
    { echo "BUILD FAILED"; tail -n 20 "$M/rt.build.log"; } > "$L"; return
  fi
  if [ -n "$sb" ]; then rg_log "$L" gpu "$env" nice -n 19 timeout 600 stdbuf -oL "$M/rt" "$IN/bl_el.mkv" 3 24 6
  else rg_log "$L" gpu "$env" nice -n 19 timeout 2400 "$M/rt" "$IN/bl_el.mkv" 3 24 6; fi
  rg_rm "$M"
}

# --------------------------------------------------------------------------------------------------- libplacebo variants
# lpvar <name> <edit file or -> [<ERE of patches to leave out>]: libplacebo of the recipe changed so, built into
# $RG_W/lp-<name>: echoes its prefix (status 3: the edit no longer applies; else 1 when it does not build)
lpvar() {
  local rc
  rg_libplacebo "$RG_W/lp-$1" "$2" "${3:-^$}" > "$RG_W/logs/lp-$1.log" 2>&1; rc=$?
  [ $rc = 0 ] || { tail -n 5 "$RG_W/logs/lp-$1.log"; return $rc; }
  echo "$RG_W/lp-$1/prefix"
}
# fid_count <sums> <reference>: how many files of a run differ from the reference (by name)
fid_count() { diff "$2" "$1" | grep -c '^>'; }

# R5/R6 (libplacebo-9970, 9971: the rows of the enhancement layer read once, kept in half floats): that they change no
# byte is shown by fid-src0/src1/src2/cnv0/gui, whose references were made before them. Here: what the bench sees
# when they are wrong (two mutants), and the control that tells the half floats from the rest.
stage_r56() {
  local K F n e c lp rc v ok=1 detail="" want
  K=$(rg_kodi) || rg_fail "Kodi tree"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item r56 FAILED "no pictures (stage data)"; return 1; }
  for v in "weight:lp-mut-weight.py:56" "4096:lp-mut-4096.py:65" "float4096:lp-rows-float-4096.py:0"; do
    IFS=: read -r n e want <<< "$v"
    lp=$(lpvar "r56-$n" "$TS/$e"); rc=$?
    if [ $rc != 0 ]; then
      ok=0; detail="$detail $n: $([ $rc = 3 ] && echo "the edit $e no longer applies" || echo "libplacebo did not build");"; continue
    fi
    if ! rg_bridge "$K" "$lp" "$F" "$SRC/fidbench.c" "$RG_W/bin-r56-$n" "$SRC"; then
      ok=0; detail="$detail $n: fidbench did not build;"; continue
    fi
    fid_run "$RG_W/bin-r56-$n" "$RG_W/tmp/r56-$n" src0 "$RG_W/logs/r56-$n.log" FB_SOURCE=1 FB_STEP=0 -- $PICS
    c=$(fid_count "$RG_W/logs/r56-$n.log.sums" "$REF/f-src0.sums")
    grep -q '^FIDBENCH-OK: 14 of 14' "$RG_W/logs/r56-$n.log" || { ok=0; detail="$detail $n: not drawn ($(rg_last "$RG_W/logs/r56-$n.log"));"; continue; }
    [ "$c" = "$want" ] || ok=0
    detail="$detail $n: $c of $(wc -l < "$RG_W/logs/r56-$n.log.sums") files differ from f-src0.sums ($want expected);"
    rg_rm "$RG_W/bin-r56-$n" "$RG_W/lp-r56-$n"
  done
  rg_item r56 "$([ $ok = 1 ] && echo PASSED || echo FAILED)" "libplacebo mutants, fidbench src0:$detail"
}

# the step-down of a picture scaled to the raster (FB_HALF=1: the layers at half size), at every level
stage_rscale() {
  local f O el e ee L eq=0 nc=0 dz=0 fr=0 detail
  f=$(bin fidbench) || { rg_item rscale FAILED "no fidbench (stage build)"; return 1; }
  [ -f "$DATA/p3/meta.bin" ] || { rg_item rscale FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/rscale; rg_fresh "$O"
  fidh() { # <tag of the sums> <env...>: the 4 pictures at half size; short sums of every output but info.txt
    local tag=$1 x
    shift
    rg_fresh "$O/out"
    rg_log "$O/$tag.log" gpu FB_HALF=1 "$@" nice -n 19 timeout 2400 "$f" "$DATA" "$O/out" h 3 51 960 1440
    ( cd "$O/out" && for x in *; do case $x in *info.txt) continue ;; esac; echo "$(sha256sum < "$x" | cut -c1-16) $x"; done ) > "$O/$tag.sums"
  }
  sameh() { cmp -s "$O/$1.sums" "$O/$2.sums" && [ -s "$O/$1.sums" ]; }
  for el in "el X=1" "noel FB_NOEL=1"; do
    set -- $el; e=$1; ee=$2
    fidh "$e-s0" "$ee" FB_SOURCE=1 FB_STEP=0
    cmp -s "$O/$e-s0.sums" "$REF/rscale-$e-s0.sums" && fr=$((fr + 1))
    for L in 1 2 3; do
      fidh "$e-s$L" "$ee" FB_SOURCE=1 FB_STEP=$L; fidh "$e-c$L" "$ee" FB_SOURCE=0 FB_STEP=$L
      sameh "$e-s$L" "$e-c$L" && eq=$((eq + 1))
    done
    fidh "$e-c0" "$ee" FB_SOURCE=0 FB_STEP=0
    sameh "$e-s0" "$e-c0" || dz=$((dz + 1))
    [ "$e" = noel ] && sameh noel-c1 noel-c0 && nc=1
  done
  rg_rm "$O/out"
  detail="levels 1-3 = the converted transport at that level: $eq of 6; without the layer level 1 = converted level 0: $nc of 1; level 0 as ref/render/rscale-{el,noel}-s0.sums: $fr of 2; level 0 not the converted transport: $dz of 2"
  rg_item rscale "$([ $eq = 6 ] && [ $nc = 1 ] && [ $fr = 2 ] && [ $dz = 2 ] && echo PASSED || echo FAILED)" "$detail"
}

# the gamut table of the mapping to a display (dvbridge_render_set_gamut_table()), gamut_half() of libplacebo-9950,
# and the pictures that the gamut changes leave as they were
stage_gamut() {
  local K L F O m rc v n nm=0 good=1 detail="" a b
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/gamut; rg_fresh "$O" "$O/out"
  # --- gamut-table: the test made from hdrsdr_test.c; the bridge as it is, with a finer table it cannot make, mutants
  if ! $RG_PY "$TS/gamut_table_mktest.py" "$TS/hdrsdr_test.c" "$O/gamut_table_test.c" > "$O/mktest.log" 2>&1; then
    rg_item gamut-table FAILED "gamut_table_mktest.py no longer applies to hdrsdr_test.c: $(tail -n 1 "$O/mktest.log")"
  else
    gt() { # <name> <edit file or -> <env...>: the test built on that bridge and run, its verdict line
      local n=$1 e=$2 T=$K
      shift 2
      if [ "$e" != - ]; then
        $RG_PY "$TS/gamut-mutants.py" "$n" "$O/e-$n.py" || return 4
        mutant "gamut-$n" "$O/e-$n.py" "$($RG_PY -c 'import runpy, sys; print(runpy.run_path(sys.argv[1])["FILE"])' "$O/e-$n.py")" || { echo "$n: THE MUTANT NO LONGER APPLIES"; return 3; }
        T=$RG_W/mut/gamut-$n
      fi
      rg_bridge "$T" "$L" "$F" "$O/gamut_table_test.c" "$O/gt-$n" || { echo "$n: BUILD FAILED $(tail -n 2 "$O/gt-$n.build.log" | tr '\n' ' ')"; return 4; }
      rg_log "$O/run-$n.log" gpu "$@" nice -n 19 timeout 1200 "$O/gt-$n" "$IN/chart-pq.yuv" 1 pq 4000 0.005 4000 400 p3 "$O/out/$n-"
      echo "$n: exit $(rg_exit "$O/run-$n.log"), $(grep -a -E '^(PASSED|FAILED)' "$O/run-$n.log" | head -n 1)"
      rg_rm "$O/out" "$RG_W/mut/gamut-$n"; mkdir -p "$O/out"
    }
    { gt new - GT_FINE=256,192,512; gt fail fail GT_FAIL=1
      for m in m1 m2 m3; do gt $m $m GT_FAIL=1; done; gt m4 m4 GT_FINE=256,192,512; gt m0 m0 X=1; } > "$O/gamut-table.log" 2>&1
    cat "$O/gamut-table.log"
    # each mutant with the number of checks it fails
    nm=0
    for m in m1:5 m2:5 m3:2 m4:2 m0:1; do grep -q "^${m%%:*}: exit [1-9], FAILED: ${m#*:} failed\$" "$O/gamut-table.log" && nm=$((nm + 1)); done
    v=$(grep -a -m1 "^values that differ from the standard table's picture: " "$O/run-new.log" 2> /dev/null | grep -o '[0-9]*$')
    if grep -q '^new: exit 0, PASSED' "$O/gamut-table.log" && grep -q '^fail: exit 0, PASSED' "$O/gamut-table.log" && [ "$nm" = 5 ]; then
      rg_item gamut-table PASSED "the standard and the finer table as reported; a finer table that cannot be made: the standard one in its place, fail-closed; 5 of 5 mutants caught with the checks expected ($(grep -E '^m[0-4]:' "$O/gamut-table.log" | sed 's/: exit [0-9]*, FAILED: / /' | tr '\n' ';')); informative: ${v:-?} values differ from the standard table's picture"
    else
      rg_item gamut-table FAILED "$(grep -E '^(new|fail|m[0-4]):' "$O/gamut-table.log" | cut -c1-60 | tr '\n' ';')"
    fi
  fi
  # --- gamut-half: gamut_half() of libplacebo-9950 against _Float16, and the copy is the function of the source
  { gcc -O2 -std=gnu11 -Wall -Wextra "$TS/half.c" -o "$O/half" -lm && nice -n 19 "$O/half"; echo "exit $?"
    $RG_PY - "$TS/half.c" "$RG_SHARED/lp/src/src/shaders/colorspace.c" <<'PY'
import re, sys
def body(path):
    t = open(path).read()
    m = re.search(r'static uint16_t gamut_half\(float f\)\n\{\n(.*?)\n\}\n', t, re.S)
    return None if not m else [l.split('//')[0].strip() for l in m.group(1).split('\n') if l.split('//')[0].strip()]
a, b = body(sys.argv[1]), body(sys.argv[2])
print("gamut_half of the recipe's libplacebo is the one tested: " + ("yes" if a and a == b else "NO"))
PY
  } > "$O/gamut-half.log" 2>&1
  if grep -q '^HALF-OK' "$O/gamut-half.log" && grep -q 'is the one tested: yes' "$O/gamut-half.log"; then
    rg_item gamut-half PASSED "$(grep '^half:' "$O/gamut-half.log" | cut -c1-140); the function is the patched source's"
  else
    rg_item gamut-half FAILED "$(grep -E '^(half|HALF|gamut_half)' "$O/gamut-half.log" | tr '\n' ' ' | cut -c1-200)"
  fi
  # --- gamut-ident: the pictures of hdrsdr_test, p5sdr_test and the fidelity bench's HDR10 ones (as they are, to
  # SDR, to a projector), and the drawing lines of dvrender_test, against the references; A: libplacebo without
  # 9950 and 9951 (where the GPU filters 16-bit unorm), the same pictures
  cp "$SRC/fidbench.c" "$O/fidbench-mapping.c"
  if ! rg_edit "$O/fidbench-mapping.c" "$TS/fidbench-mapping.py" > "$O/fidmap.log" 2>&1; then
    rg_item gamut-ident FAILED "fidbench-mapping.py no longer applies to src/fidbench.c: $(tail -n 1 "$O/fidmap.log")"; return
  fi
  progs() { # <bin folder> <libplacebo prefix>
    local D=$1 P=$2
    mkdir -p "$D"
    rg_bridge "$K" "$P" "$F" "$TS/hdrsdr_test.c" "$D/hs" && rg_bridge "$K" "$P" "$F" "$TS/p5sdr_test.c" "$D/p5" &&
      rg_bridge "$K" "$P" "$F" "$O/fidbench-mapping.c" "$D/fb" "$SRC" && rg_bridge "$K" "$P" "$F" "$TS/dvrender_test.c" "$D/rt"
  }
  pics() { # <bin folder> <out folder>: every picture; their SHA256 into <out folder>.sha256
    local B=$1 D=$2 n k e
    rg_fresh "$D"
    for n in chart 51; do rg_log "$D/p5-$n.log" gpu P5SDR_TIMES=1 nice -n 19 "$B/p5" "$IN/p5-$n.meta" "$IN/p5-$n-bl.yuv" 1 "$D/p5-$n-"; done
    rg_log "$D/hs-pq.log" gpu nice -n 19 "$B/hs" "$IN/chart-pq.yuv" 1 pq 4000 0.005 4000 400 p3 "$D/hs-pq-"
    rg_log "$D/hs-hlg.log" gpu nice -n 19 "$B/hs" "$IN/chart-hlg.yuv" 1 hlg 0 0 0 0 none "$D/hs-hlg-"
    for k in asis sdr proj; do
      mkdir -p "$D/fb-$k"
      case $k in asis) e="FB_HDR10=1" ;; sdr) e="FB_HDR10=1 FB_SDR=1" ;; proj) e="FB_HDR10=1 FB_PROJ=150" ;; esac
      # shellcheck disable=SC2086
      rg_log "$D/fb-$k/run.log" gpu $e nice -n 19 "$B/fb" "$DATA" "$D/fb-$k" "$k" 3 1440
    done
    ( cd "$D" && find . -name '*.f32' | sort | xargs sha256sum ) > "$D.sha256"
    echo "$(basename "$D"): $(wc -l < "$D.sha256") pictures; FAIL lines $(cat "$D"/*.log "$D"/fb-*/run.log | grep -c '^FAIL')"
    rg_rm "$D"
  }
  { if progs "$O/bin" "$L"; then
      pics "$O/bin" "$O/B"
      same "$O/B.sha256" "$REF/gamut-ident-B.sha256"
      rg_log "$O/rt.log" gpu nice -n 19 timeout 2400 "$O/bin/rt" "$IN/bl_el.mkv" 3 24 6
      echo "rt: exit $(rg_exit "$O/rt.log"), $(grep -a -E '^(ALL PASSED|FAILED)' "$O/rt.log")"
      grep -a -E '^picture' "$O/rt.log" | grep -a -v -E ' in [0-9.]+ ?ms' > "$O/rt.lines"
      echo "rt lines: $(same "$O/rt.lines" "$REF/dvrender-test.lines")"
    else echo "B: BUILD FAILED"; fi
    # the mutant: the standard table of another size; the PQ chart mapped to SDR must change, as it is must not
    $RG_PY "$TS/gamut-mutants.py" size "$O/e-size.py" && mutant gamut-size "$O/e-size.py" dvbridge_render.c &&
      rg_bridge "$RG_W/mut/gamut-size" "$L" "$F" "$TS/hdrsdr_test.c" "$O/hs-size" || echo "MUTANT: no longer applies or does not build"
    if [ -x "$O/hs-size" ]; then
      rg_fresh "$O/size"
      rg_log "$O/size/hs.log" gpu nice -n 19 "$O/hs-size" "$IN/chart-pq.yuv" 1 pq 4000 0.005 4000 400 p3 "$O/size/hs-pq-"
      ( cd "$O/size" && sha256sum hs-pq-*.f32 | sed 's|  |  ./|' ) > "$O/size.sha256"
      echo "MUTANT size: $(grep -c -F -x -f "$O/size.sha256" "$REF/gamut-ident-B.sha256") of $(wc -l < "$O/size.sha256") PQ chart pictures as the reference (1 expected: the one drawn as it is)"
      rg_rm "$O/size"
    fi
    rg_rm "$O/bin" "$O/hs-size" "$RG_W/mut/gamut-size"; } > "$O/gamut-ident.log" 2>&1
  cat "$O/gamut-ident.log"
  if cmp -s "$O/B.sha256" "$REF/gamut-ident-B.sha256" && grep -q '^rt: exit 0, ALL PASSED' "$O/gamut-ident.log" &&
     cmp -s "$O/rt.lines" "$REF/dvrender-test.lines" && grep -q '^MUTANT size: 1 of 2 PQ chart pictures' "$O/gamut-ident.log"; then
    rg_item gamut-ident PASSED "the 16 pictures (as they are, to SDR, to a projector) as ref/render/gamut-ident-B.sha256, dvrender_test ALL PASSED and its $(wc -l < "$O/rt.lines") drawing lines as ref/render/dvrender-test.lines; a standard table of another size (mutant) changes the mapped picture and not the other"
  else
    rg_item gamut-ident FAILED "$(grep -E '^(B|rt|the same|DIFFERS|no ref|nothing|MUTANT)' "$O/gamut-ident.log" | cut -c1-90 | tr '\n' ';')"
  fi
}

# HDR mapped to SDR in a wide container (videoscreen.hadessdrhdrgamut: BT.2020 or DCI-P3 (D65), as well as BT.709):
# sdrgamut_test.c on the PQ chart (its mastering display DCI-P3) and the HLG one (BT.2020), and two mutants: mapped into
# BT.709 and put in the container (as a "BT.709 in BT.2020" output), and the signal written in BT.709 whatever the
# container. What BT.709 draws is that of gamut-ident (stage gamut), unchanged.
stage_sdrgamut() {
  local K L F O v t want got n=0 ok=0
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/sdrgamut; rg_fresh "$O"
  # sg <tree> <tag>: the test of that tree on both charts, into $O/<tag>-pq.log and $O/<tag>-hlg.log
  sg() {
    rg_bridge "$1" "$L" "$F" "$TS/sdrgamut_test.c" "$O/t-$2" || { echo "$2: BUILD FAILED"; return 1; }
    rg_log "$O/$2-pq.log" gpu nice -n 19 timeout 1800 "$O/t-$2" "$IN/chart-pq.yuv" 1 pq 4000 0.005 4000 400 p3
    rg_log "$O/$2-hlg.log" gpu nice -n 19 timeout 1800 "$O/t-$2" "$IN/chart-hlg.yuv" 1 hlg 0 0 0 0 none
    rg_rm "$O/t-$2"
  }
  sg "$K" tree
  for t in pq hlg; do
    v=$(rg_pass "$O/tree-$t.log" '^PASSED: 0 failed$')
    rg_item "sdr-gamut-$t" "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -m1 '^inside BT.709, saturation below 0.20' "$O/tree-$t.log" | cut -c1-230); $(grep -a -m1 '^pixels outside' "$O/tree-$t.log" | cut -c1-170)"
  done
  # the mutants, each with the checks it must fail on the PQ chart and on the HLG one (measured when written)
  for t in "into-bt709 mut-sdr-into-bt709.py 2 3" "bt709-signal mut-sdr-bt709-signal.py 2 1"; do
    set -- $t
    n=$((n + 1))
    if mutant "sdrgamut-$1" "$TS/$2" dvbridge_render.c && sg "$RG_W/mut/sdrgamut-$1" "m-$1" > "$O/m-$1.build" 2>&1; then
      got="$(grep -a -c '^FAIL:' "$O/m-$1-pq.log") $(grep -a -c '^FAIL:' "$O/m-$1-hlg.log")"
      if [ "$got" = "$3 $4" ]; then ok=$((ok + 1)); else echo "mutant $1: failed $got checks, $3 $4 expected"; fi
    else echo "mutant $1: no longer applies or does not build"; fi
    rg_rm "$RG_W/mut/sdrgamut-$1"
  done
  rg_item sdr-gamut-mutants "$([ $ok = $n ] && echo PASSED || echo FAILED)" "$ok of $n mutants failed the checks expected (mapped into BT.709 and put in the container: 2 on the PQ chart, 3 on the HLG one; the signal in BT.709 whatever the container: 2 and 1)"
}

# A 3D picture drawn by the bridge a view at a time (KODI::UTILS::STEREO3D in Kodi): two pictures side by side (as Kodi
# puts the two views of MV-HEVC) or one above the other, each view copied out of the textures of Kodi's renderer
# (dvbridge_gl_part_copy()) and drawn into its eye: value for value the same picture in 2D at that place, rebuilt from
# profile 5 as HDR10 (side by side (half) at 2160p, frame packing at 1080p, a cropped picture), mapped to SDR in
# BT.2020, and HDR10 mapped to SDR in BT.709; each eye its own view; and the rule before (a crop of the picture of two),
# which draws the other view's samples at the seam. A mutant of the copy: every part from the corner of the picture (the
# right eyes show the left view).
stage_stereo3d() {
  local K L F O v t got n=0 ok=0
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/stereo3d; rg_fresh "$O"
  s3() { # <tree> <tag>
    rg_bridge "$1" "$L" "$F" "$TS/stereo3d_test.c" "$O/t-$2" || { echo "$2: BUILD FAILED"; tail -n 5 "$O/t-$2.build.log"; return 1; }
    rg_log "$O/$2.log" gpu nice -n 19 timeout 2400 "$O/t-$2" "$IN/p5-51.meta" "$IN/p5-51-bl.yuv" "$IN/p5-96-bl.yuv"       "$IN/chart-pq.yuv" "$IN/hdr10bl-96.yuv"
    rg_rm "$O/t-$2"
  }
  s3 "$K" tree
  v=$(rg_pass "$O/tree.log" '^PASSED: 0 failed$')
  rg_item stereo3d "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.log") checks; $(grep -a -m1 'the rule before' "$O/tree.log" | cut -c7-190)"
  for t in "at-origin mut-part-at-origin.py 5"; do
    set -- $t
    n=$((n + 1))
    if mutant "stereo3d-$1" "$TS/$2" dvbridge_gl_part.c && s3 "$RG_W/mut/stereo3d-$1" "m-$1" > "$O/m-$1.build" 2>&1; then
      got=$(grep -a -c '^FAIL:' "$O/m-$1.log")
      if [ "$got" = "$3" ]; then ok=$((ok + 1)); else echo "mutant $1: failed $got checks, $3 expected"; fi
    else echo "mutant $1: no longer applies or does not build"; fi
    rg_rm "$RG_W/mut/stereo3d-$1"
  done
  rg_item stereo3d-mutants "$([ $ok = $n ] && echo PASSED || echo FAILED)" "$ok of $n mutants failed the checks expected (every part copied from the corner of the picture: 5, the right eyes)"
}

# The fidelity step-down levels of Dolby Vision rebuilt for output as HDR10 and as SDR (CDVConvertStep in Kodi): the
# fidelity bench with fidbench-mapping.py and fidbench-convtime.py on the two pictures of the FEL stream with the most
# of its layer (51) and a late one (1440), at each level: the enhancement layer is drawn at every level, every level
# below 0 changes the picture, level 0 is the picture of gamut-ident (as before), and the GPU time of each level on the
# software renderer (informative: what a level saves on a graphics chip is measured there, by Kodi).
stage_convstep() {
  local K L F O m lvl tag e same0 res diff n=0 bad=0 detail=""
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p51/meta.bin" ] && [ -f "$DATA/p1440/meta.bin" ] || { rg_item conv-step FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/convstep; rg_fresh "$O"
  cp "$SRC/fidbench.c" "$O/fb.c"
  if ! rg_edit "$O/fb.c" "$TS/fidbench-mapping.py" > "$O/edit.log" 2>&1 || ! rg_edit "$O/fb.c" "$TS/fidbench-convtime.py" >> "$O/edit.log" 2>&1; then
    rg_item conv-step FAILED "the edits no longer apply to src/fidbench.c: $(tail -n 1 "$O/edit.log")"; return 1
  fi
  rg_bridge "$K" "$L" "$F" "$O/fb.c" "$O/fb" "$SRC" || { rg_item conv-step FAILED "did not build: $(tail -n 3 "$O/fb.build.log" | tr '\n' ' ')"; return 1; }
  for m in hdr10 sdr; do
    e="FB_HDR10=1"; [ $m = sdr ] && e="FB_HDR10=1 FB_SDR=1"
    for lvl in 0 1 2 3; do
      tag=$m-$lvl
      mkdir -p "$O/$tag"
      # shellcheck disable=SC2086
      rg_log "$O/$tag.log" gpu $e FB_STEP=$lvl FB_TIMES=5 nice -n 19 timeout 2400 "$O/fb" "$DATA" "$O/$tag" "$tag" 51 1440
      ( cd "$O/$tag" && sha256sum *.f32 ) | sed "s/$tag/L/g" > "$O/$tag.sums"
    done
  done
  for m in hdr10 sdr; do
    # the reference of level 0: gamut-ident's pictures of picture 1440 as they are and to SDR
    res=$(grep -c 'residual drawn 1' "$O"/$m-*.log | awk -F: '{s += $2} END {print s}')
    diff=0
    for lvl in 1 2 3; do cmp -s "$O/$m-0.sums" "$O/$m-$lvl.sums" || diff=$((diff + 1)); done
    same0=no
    case $m in hdr10) e=asis ;; sdr) e=sdr ;; esac
    if grep -q " ./fb-$e/$e-p1440-hdr10.f32\$" "$REF/gamut-ident-B.sha256" &&
       [ "$(grep " ./fb-$e/$e-p1440-hdr10.f32\$" "$REF/gamut-ident-B.sha256" | cut -c1-64)" = "$(grep 'L-p1440-hdr10.f32' "$O/$m-0.sums" | cut -c1-64)" ]; then
      same0=yes
    fi
    detail="$detail $m: residual drawn in $res of 8 pictures, levels 1-3 other pictures than level 0 in $diff of 3, level 0 of picture 1440 as gamut-ident: $same0; times $(for lvl in 0 1 2 3; do grep -a -h 'picture 51 tag' "$O/$m-$lvl.log" | grep -o 'median [0-9.]* ms' | sed "s/median/$lvl:/"; done | tr '\n' ' ');"
    [ "$res" = 8 ] && [ $diff = 3 ] && [ $same0 = yes ] || bad=$((bad + 1))
    n=$((n + 1))
  done
  echo "$detail"
  rg_item conv-step "$([ $bad = 0 ] && echo PASSED || echo FAILED)" "the step-down levels of the conversion, pictures 51 and 1440 of the FEL stream:$detail"
  rg_rm "$O"
}

# CDVGuiStep (xbmc/windowing/gbm/DVGuiStep.h): the fidelity step-down of frames with GUI elements, and its mutants
stage_gui() {
  local K O GB names n caught=0 total=0 L
  K=$(rg_kodi) || rg_fail "Kodi tree"
  O=$RG_W/gui; rg_fresh "$O" "$O/mut"; GB=$K/xbmc/windowing/gbm; L=$O/gui-step.log
  { g++ -std=c++17 -O1 -Wall -Wextra -Werror -I"$GB" "$TS/dvguistep_test.cpp" -o "$O/dvguistep_test" 2>&1 && "$O/dvguistep_test" | tail -n 40; echo "exit ${PIPESTATUS[0]}"
    if names=$($RG_PY "$TS/guistep_mut.py" "$GB" "$O/mut"); then
      for n in $names; do
        total=$((total + 1))
        if ! g++ -std=c++17 -O1 -w -I"$O/mut/$n" "$TS/dvguistep_test.cpp" -o "$O/mut/$n/t" 2> "$O/mut/$n/build.err"; then echo "$n: does not build (counts as not caught)"; continue; fi
        if "$O/mut/$n/t" > "$O/mut/$n/out.txt"; then echo "$n: NOT CAUGHT"; else caught=$((caught + 1)); echo "$n: caught ($(grep -c '^FAIL' "$O/mut/$n/out.txt") failing checks)"; fi
      done
      echo "MUTATIONS: $caught of $total caught"
    else echo "$names"; fi; } > "$L" 2>&1
  rg_rm "$O/mut"
  if grep -q '^ALL PASSED: 86180 of 86180 checks' "$L" && grep -q '^MUTATIONS: 14 of 15 caught' "$L" && grep -q '^M1-up-above-base: NOT CAUGHT' "$L"; then
    rg_item gui-step PASSED "86180 of 86180 checks; mutants 14 of 15 caught (M1-up-above-base not: equivalent)"
  else
    rg_item gui-step FAILED "$(grep -E '^(ALL PASSED|FAIL|MUTANTS|MUTATIONS|M[0-9]+-.*NOT CAUGHT)' "$L" | head -n 4 | tr '\n' ' ' | cut -c1-200)"
  fi
}

# the geometry of a part of the picture drawn natively (zoom, shifts): the level 5 margins through the view, Kodi's
# ViewGeometry, and the bridge's transports against the CPU reference and the CCM 001 reader
stage_geom() {
  local K L F O G gc rd wh c1 x
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/geom; rg_fresh "$O" "$O/out"
  # --- geom-margins, and the same without the view's code (mutant): 5 of the 12 cases fail
  { gcc -std=gnu11 -O1 -Wall -Wextra -Wdouble-promotion -I"$K/tools/dvbridge" -I"$F/include" "$TS/margins_test.c" -o "$O/margins" -lm 2>&1 && "$O/margins"; echo "margins: exit $?"
    if mutant margins "$TS/mut-margins-no-view.py" dvbridge_metadata.h; then
      gcc -std=gnu11 -O1 -I"$RG_W/mut/margins/tools/dvbridge" -I"$F/include" "$TS/margins_test.c" -o "$O/margins-mut" -lm &&
        { "$O/margins-mut" > "$O/margins-mut.log"; echo "margins without the view's code: exit $? (must be 1): $(tail -n 1 "$O/margins-mut.log")"; }
    else echo "margins without the view's code: THE MUTANT NO LONGER APPLIES"; fi
    rg_rm "$RG_W/mut/margins"; } > "$O/geom-margins.log" 2>&1
  if grep -q '^ALL PASSED: 0 failed' "$O/geom-margins.log" && grep -q "margins without the view's code: exit 1 (must be 1): FAILED: 5 failed" "$O/geom-margins.log"; then
    rg_item geom-margins PASSED "12 cases of the level 5 area through a view, by hand; without the view's code (mutant) 5 fail"
  else rg_item geom-margins FAILED "$(grep -E '^(ALL|FAIL|margins)' "$O/geom-margins.log" | head -n 4 | tr '\n' ' ' | cut -c1-200)"; fi
  # --- geom-viewgeom: Kodi's DVBRIDGE::ViewGeometry() and SameGeometry()
  { g++ -std=c++17 -O1 -Wall -Wextra -Wdouble-promotion -I"$K/tools/dvbridge" -I"$K/xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender" "$TS/viewgeom_test.cpp" -o "$O/viewgeom" 2>&1 && "$O/viewgeom"; echo "viewgeom: exit $?"; } > "$O/geom-viewgeom.log" 2>&1
  if [ "$(grep -c '^ok:' "$O/geom-viewgeom.log")" = 9 ] && grep -q '^ALL PASSED: 0 failed' "$O/geom-viewgeom.log"; then rg_item geom-viewgeom PASSED "9 of 9 checks of ViewGeometry/SameGeometry"
  else rg_item geom-viewgeom FAILED "$(grep -c '^ok:' "$O/geom-viewgeom.log") ok; $(grep -E '^(FAIL|ALL|viewgeom)' "$O/geom-viewgeom.log" | head -n 3 | tr '\n' ' ')"; fi
  # --- geom-bench: the bridge's two transports against the CPU reference and the CCM 001 reader
  G=$O/gb.log
  { nice -n 19 $RG_PY "$TS/geom_prep.py" "$O/chart-1080.yuv" 1 "$IN/p5-chart-bl.yuv" || echo "geom_prep chart failed"
    nice -n 19 $RG_PY "$TS/geom_prep.py" "$O/f960-1080.yuv" 3 "$DATA/p960" || echo "geom_prep f960 failed"
    EXTRA_CF=-DGB_ORACLE=1 rg_bridge "$K" "$L" "$F" "$SRC/geom_bench.c" "$O/gbo" "$SRC" || { echo "BUILD FAILED"; tail -n 10 "$O/gbo.build.log"; }
    if mutant noview "$TS/mut-geometry-no-view.py" dvbridge_core.h; then
      EXTRA_CF=-DGB_ORACLE=1 rg_bridge "$RG_W/mut/noview" "$L" "$F" "$SRC/geom_bench.c" "$O/gbo-noview" "$SRC" || { echo "BUILD FAILED noview"; tail -n 10 "$O/gbo-noview.build.log"; }
    else echo "noview: THE MUTANT NO LONGER APPLIES"; fi
    rg_rm "$RG_W/mut/noview"
    gb() { # <binary> <tag> <env> <picture>
      env $RG_GPU_ENV $3 nice -n 19 timeout 1200 "$O/$1" "$O/out" "$2" "$4" > "$O/$2.log" 2> "$O/$2.err" < /dev/null
      echo "$2: exit $?; $(grep -a -E 'FAIL|reader: (as|FAIL)|GB_CHECK:|GEOMBENCH' "$O/$2.log" | cut -c1-160 | tr '\n' ' ')"
      case $2 in *whole*) ;; *) for x in "$O/out/$2"-*; do [ -e "$x" ] && rg_rm "$x"; done ;; esac
    }
    CHART="raw:$IN/p5-chart.meta:$O/chart-1080.yuv:1920:1080:1"
    gb gbo chart-scaled "GB_CHECK=1" "$CHART"
    gb gbo f960-scaled "GB_CHECK=1" "raw:$DATA/p960/meta.bin:$O/f960-1080.yuv:1920:1080:3"
    gb gbo chart-zoom "GB_CHECK=1 GB_VIEW=240,135,1680,945" "$CHART"
    gb gbo chart-zoom-off "GB_CHECK=1 GB_VIEW=240.5,135.25,1680.5,945.25" "$CHART"
    gb gbo f3-zoom "GB_CHECK=1 GB_VIEW=480,270,3360,1890" "fid:$DATA/p3"
    gb gbo f3-up "GB_CHECK=1 GB_VIEW=0,540,3840,2160 GB_RECT=0,0,3840,1620" "fid:$DATA/p3"
    gb gbo f3-down "GB_CHECK=1 GB_VIEW=0,0,3840,1620 GB_RECT=0,540,3840,1620" "fid:$DATA/p3"
    gb gbo f960-zoom-half "GB_CHECK=1 GB_STEP=2 GB_VIEW=240,135,1680,945" "fid:$DATA/p960"
    gb gbo f3-zoom-converted "GB_SOURCE=0 GB_VIEW=480,270,3360,1890" "fid:$DATA/p3"
    gb gbo f3-whole-view "GB_VIEW=0,0,3840,2160" "fid:$DATA/p3"
    gb gbo f3-whole "X=1" "fid:$DATA/p3"
    for x in direct.rgba composed.rgba; do cmp -s "$O/out/f3-whole-$x" "$O/out/f3-whole-view-$x" && echo "WHOLE $x: the same bytes as without a view" || echo "WHOLE $x: DIFFERS"; done
    [ -x "$O/gbo-noview" ] && { gb gbo-noview noview-chart-zoom "GB_CHECK=1 GB_VIEW=240,135,1680,945" "$CHART"; gb gbo-noview noview-f3-zoom "GB_CHECK=1 GB_VIEW=480,270,3360,1890" "fid:$DATA/p3"; }
    rg_rm "$O/out" "$O/chart-1080.yuv" "$O/f960-1080.yuv"; } > "$G" 2>&1
  gc=$(grep -E '^(chart-scaled|f960-scaled|chart-zoom|chart-zoom-off|f3-zoom|f3-up|f3-down|f960-zoom-half):' "$G" | grep -c 'exit 0.*both transports are the CPU reference')
  rd=$(grep -E '^(chart-zoom|chart-zoom-off|f3-zoom|f3-up|f3-down|f960-zoom-half|f3-zoom-converted|f3-whole-view|f3-whole):' "$G" | grep -c 'reader: as the clause says.*reader: as the clause says')
  wh=$(grep -c '^WHOLE .*: the same bytes' "$G")
  if [ "$gc" = 8 ] && [ "$rd" = 9 ] && [ "$wh" = 2 ]; then rg_item geom-bench PASSED "both transports the CPU reference in 8 of 8 (scaled, zoomed, off the grid, shifted, at level 2), the CCM 001 reader as the clause in 9 of 9, the whole picture as its view = no view"
  else rg_item geom-bench FAILED "reference $gc of 8, reader $rd of 9, whole view $wh of 2: $(grep -E 'BUILD|failed' "$G" | head -n 2 | tr '\n' ' ')"; fi
  c1=$(grep -E '^noview-(chart|f3)-zoom:' "$G" | grep -c 'exit [1-9].*FAIL: GB_CHECK')
  if [ "$c1" = 2 ]; then rg_item geom-bench-before PASSED "the bridge whose geometries never have a view (mutant) fails the view's reference in 2 of 2"
  else rg_item geom-bench-before FAILED "the mutant failed the reference in $c1 of 2: $(grep -E '^noview|MUTANT' "$G" | cut -c1-100 | tr '\n' ';')"; fi
}

# R8 (libplacebo-9960): the composed planes in R16/RG16 give the same bytes as in R32F/RG32F, in the fidelity bench
# (6 configurations), geom_bench (10 files) and the measured test; a reader that cuts the codes off must see a
# difference on the 16-bit side
stage_r8() {
  local K L F O s side tree n cfg same=0 differ=0 t x
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item r8-planes FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/r8; rg_fresh "$O" "$O/out"
  { mutant r32 "$TS/mut-planes-r32f.py" dvbridge_render.c || echo "r32: THE EDIT NO LONGER APPLIES"
    mutant trunc "$TS/mut-read-truncate.py" dvbridge_source.c || echo "trunc: THE MUTANT NO LONGER APPLIES"
    $RG_PY "$TS/geom_prep.py" "$O/chart-1080.yuv" 1 "$IN/p5-chart-bl.yuv" > /dev/null
    for s in "r16 $K" "r32 $RG_W/mut/r32" "trunc $RG_W/mut/trunc"; do
      set -- $s
      rg_bridge "$2" "$L" "$F" "$SRC/fidbench.c" "$O/fid-$1" "$SRC" || echo "BUILD FAILED fidbench $1"
      [ "$1" = trunc ] && continue
      EXTRA_CF=-DGB_ORACLE=1 rg_bridge "$2" "$L" "$F" "$SRC/geom_bench.c" "$O/gb-$1" "$SRC" || echo "BUILD FAILED geom_bench $1"
      rg_bridge "$2" "$L" "$F" "$SRC/measured_oracle.c" "$O/meas-$1" "$SRC" || echo "BUILD FAILED measured $1"
    done
    rg_rm "$RG_W/mut/r32" "$RG_W/mut/trunc"
    fid8() { # <side> <config> <env...>: the 14 pictures, the sums without the tag
      local side=$1 n=$2
      shift 2
      rg_fresh "$O/out"
      rg_log "$O/fid-$side-$n.log" gpu "$@" nice -n 19 timeout 2400 "$O/fid-$side" "$DATA" "$O/out" "$n" $PICS
      for x in "$O"/out/*info.txt; do [ -e "$x" ] && sed -i '/samples not on a whole code/d' "$x"; done
      ( cd "$O/out" && sha256sum * | sed "s/ \*\?$n-/ /" ) > "$O/fid-$side-$n.sums"
      echo "fid $side $n: exit $(rg_exit "$O/fid-$side-$n.log"); $(wc -l < "$O/fid-$side-$n.sums") files; $(rg_last "$O/fid-$side-$n.log")"
    }
    for cfg in "src0 FB_SOURCE=1 FB_STEP=0" "src1 FB_SOURCE=1 FB_STEP=1" "src2 FB_SOURCE=1 FB_STEP=2" "src3 FB_SOURCE=1 FB_STEP=3" "gui FB_SOURCE=1 FB_GUI=1" "half FB_SOURCE=1 FB_HALF=1"; do
      set -- $cfg; n=$1; shift
      fid8 r16 "$n" "$@"; fid8 r32 "$n" "$@"
      cmp -s "$O/fid-r32-$n.sums" "$O/fid-r16-$n.sums" && [ -s "$O/fid-r16-$n.sums" ] && echo "R8 $n: all $(wc -l < "$O/fid-r16-$n.sums") files the same bytes ($(grep -c ccm_ "$O/fid-r16-$n.sums") composed planes)" || { echo "R8 $n: DIFFER"; diff "$O/fid-r32-$n.sums" "$O/fid-r16-$n.sums" | head -n 4; }
    done
    fid8 trunc src0 FB_SOURCE=1 FB_STEP=0
    echo "MUTANT trunc src0: $(diff "$O/fid-r16-src0.sums" "$O/fid-trunc-src0.sums" | grep -c '^>') of $(wc -l < "$O/fid-trunc-src0.sums") files differ from the 16-bit side"
    rg_fresh "$O/out"
    for s in r32 r16; do
      rg_log "$O/gb-$s-chart.log" gpu GB_CHECK=1 nice -n 19 timeout 1200 "$O/gb-$s" "$O/out" "$s-chart-scaled" "raw:$IN/p5-chart.meta:$O/chart-1080.yuv:1920:1080:1"
      rg_log "$O/gb-$s-f3.log" gpu GB_CHECK=1 GB_VIEW=480,270,3360,1890 nice -n 19 timeout 1200 "$O/gb-$s" "$O/out" "$s-f3-zoom" "fid:$DATA/p3"
      echo "gb $s: chart exit $(rg_exit "$O/gb-$s-chart.log"), f3 exit $(rg_exit "$O/gb-$s-f3.log")"
    done
    for t in chart-scaled f3-zoom; do for x in direct.rgba composed.rgba ccm_y.u16 ccm_cb.u16 ccm_cr.u16; do
      cmp -s "$O/out/r32-$t-$x" "$O/out/r16-$t-$x" && [ -s "$O/out/r16-$t-$x" ] && echo "R8 $t $x: the same bytes" || echo "R8 $t $x: DIFFERS"; done; done
    for s in r32 r16; do
      rg_log "$O/meas-$s.log" gpu nice -n 19 timeout 1800 "$O/meas-$s" "$IN/hdr10_240.mkv" 12 20
      echo "measured $s: exit $(rg_exit "$O/meas-$s.log"); $(grep -a -E '^(ALL PASSED|FAILED)' "$O/meas-$s.log" | head -n 1)"
      grep -a -E '^picture [0-9]+: ' "$O/meas-$s.log" > "$O/meas-$s.draw"
    done
    cmp -s "$O/meas-r32.draw" "$O/meas-r16.draw" && [ -s "$O/meas-r16.draw" ] && echo "R8 measured: IDENTICAL ($(wc -l < "$O/meas-r16.draw") lines)" || echo "R8 measured: DIFFER"
    rg_rm "$O/out" "$O/chart-1080.yuv"; } > "$O/r8.log" 2>&1
  same=$(grep -c -E '^R8 .*(the same bytes|IDENTICAL)' "$O/r8.log"); differ=$(grep -c -E '^R8 .*DIFFER' "$O/r8.log")
  x=$(sed -n 's/^MUTANT trunc src0: \([0-9]*\) of .*/\1/p' "$O/r8.log")
  if [ "$same" = 17 ] && [ "$differ" = 0 ] && [ "${x:-0}" -gt 0 ]; then
    rg_item r8-planes PASSED "R16 = R32F in the 6 fidbench configurations, the 10 planes and pictures of geom_bench, the measured test; a reader cutting the luma codes off (mutant) changes $x files on the 16-bit side"
  else
    rg_item r8-planes FAILED "$same of 17 the same, $differ differ; the mutant changed ${x:-?} files: $(grep -E 'BUILD|NO LONGER' "$O/r8.log" | head -n 2 | tr '\n' ' ')"
  fi
}

# the composer of CCM 001 with a base layer in BT.1886 (libplacebo-9955: out_bit_depth 14 of 5.4.3.3 and the
# conversion of clause 5.5 by its annex C), drawn by libplacebo alone (ccm_gpu_test.c) against the 64-bit reference
# (src/ccm_ref.c made into ccm_ref_1886.c by p82_mkref.py), with "before" a mutant that composes BT.1886 as ST 2084
stage_p82() {
  local F GL VK T n lpb lpa def fail=0 cse d k r e1 dd e0 why rc same c
  F=$(ff) || rg_fail "FFmpeg"; GL=$(rg_glinc) || rg_fail "GL headers"; VK=$(rg_vkinc)
  [ -f "$DATA/p1200/meta.bin" ] || { rg_item p82-run FAILED "no pictures (stage data)"; rg_item p82-run2 FAILED "no pictures"; return 1; }
  T=$RG_W/p82; rg_fresh "$T" "$T/bin" "$T/ref" "$T/gout" "$T/data"
  check() { if [ "$1" = 1 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi; }
  gpu82() { # <name> <lp: base|p82> <dir> <env...>
    local n=$1 l=$2 dd=$3 x
    shift 3
    for x in "$T/gout/$n"-*; do [ -e "$x" ] && rg_rm "$x"; done
    env $RG_GPU_ENV "$@" DV_CCM_DUMP="$T/gout/$n" nice -n 19 timeout 1200 "$T/bin/t-$l" "$T/data/$dd" "$T/gout/$n.rgb16" planar > "$T/gout/$n.log" 2>&1 < /dev/null
    local rc=$?
    echo "$n: exit $rc; $(grep -a -E 'render errors|FAIL|not composed|composed as|not drawn' "$T/gout/$n.log" | sed 's/\x1b\[[0-9;]*m//g' | tr '\n' ' ' | cut -c1-300)"
    return $rc
  }
  cmpd() { $RG_PY "$TS/p82_cmp_dump.py" "$T/gout/$1-0" "$T/ref/$2" "$T/data/$3" "  $4"; }
  within1() { $RG_PY - "$1" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
big = sum(int(a) + int(b) for a, b in re.findall(r'2-15: (\d+), 16\+: (\d+)', t))
print('  largest differences: %s; samples off by more than 1 code: %d' % (re.findall(r'largest (\d+)', t), big))
sys.exit(0 if big == 0 and 'Y ' in t else 1)
PY
  }
  {
    $RG_PY "$TS/p82_mkdata.py" "$DATA/p1200" "$T/data" || echo "FAIL: p82_mkdata.py"
    $RG_PY "$TS/p82_mkref.py" "$SRC/ccm_ref.c" "$T/ccm_ref_1886.c" && gcc -O2 -Wall -o "$T/bin/ccm_ref_1886" "$T/ccm_ref_1886.c" -lm || echo "FAIL: ccm_ref_1886"
    for r in "pq-p1200 p1200 -" "pqn-p1200 p1200 -n" "s-p1200 p1200 -s" "sn-p1200 p1200 -s,-n" "sn-rand rand -s,-n" "sn-ramp ramp -s,-n" "s-rand rand -s"; do
      set -- $r; o=${3//,/ }; [ "$o" = - ] && o=
      # shellcheck disable=SC2086
      "$T/bin/ccm_ref_1886" $o "$T/data/$2" "$T/ref/$1" > "$T/ref/$1.log" 2>&1 || echo "FAIL: reference $1: $(cat "$T/ref/$1.log")"
    done
    gcc -O2 -Wall -o "$T/bin/ccm_ref_orig" "$SRC/ccm_ref.c" && "$T/bin/ccm_ref_orig" "$T/data/p1200" "$T/ref/orig-p1200" > /dev/null
    same=1; for c in y cb cr; do cmp -s "$T/ref/orig-p1200_$c.u16" "$T/ref/pq-p1200_$c.u16" || same=0; done
    check $same "ccm_ref_1886 without -s gives the planes of ccm_ref.c (p1200, with its residual)"
    lpb=$(lpvar p82-base "$TS/lp-dump-hook-no-bt1886.py") || echo "FAIL: libplacebo before (the edit or the build): $lpb"
    lpa=$(lpvar p82-after "$TS/lp-dump-hook.py") || echo "FAIL: libplacebo after (the edit or the build): $lpa"
    for n in base p82; do
      l=$([ $n = base ] && echo "$lpb" || echo "$lpa")
      gcc -std=gnu11 -O1 -g -Wall -DP82_BL_TRC -I"$l/include" -I"$F/include" -I"$GL" -I"$VK" -c -o "$T/bin/t-$n.o" "$TS/ccm_gpu_test.c" &&
        g++ -o "$T/bin/t-$n" "$T/bin/t-$n.o" "$l/lib/libplacebo.a" -L"$F/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl \
          /usr/lib/x86_64-linux-gnu/libEGL.so.1 || echo "FAIL: build of ccm_gpu_test against $n"
    done
    echo "== 1. a base layer in PQ (every stream but 8.2): the same composed planes with and without BT.1886 handled"
    for e in "el:X=1" "noel:DV_NOEL=1"; do
      k=${e%%:*}
      gpu82 "base-pq-$k" base p1200 "${e#*:}"; gpu82 "p82-pq-$k" p82 p1200 "${e#*:}"
      same=1; for c in y c; do cmp -s "$T/gout/base-pq-$k-0_$c.f32" "$T/gout/p82-pq-$k-0_$c.f32" || same=0; done
      check $same "p1200 ($k): the composed planes the same with BT.1886 composed as ST 2084, byte for byte"
      r=pq-p1200; [ $k = noel ] && r=pqn-p1200
      cmpd "p82-pq-$k" $r p1200 "p1200 ($k), against the reference of clause 5.4" > "$T/c.txt"; rc=$?; cat "$T/c.txt"
      check $([ $rc = 0 ] && echo 1 || echo 0) "p1200 ($k): bit-exact to the reference of clause 5.4"
      cmp -s "$T/gout/base-pq-$k.rgb16" "$T/gout/p82-pq-$k.rgb16"; check $([ $? = 0 ] && echo 1 || echo 0) "p1200 ($k): the same picture (12-bit RGB of the render)"
    done
    gpu82 p82-pq-explicit p82 p1200 DV_BL_TRC=pq
    same=1; for c in y c; do cmp -s "$T/gout/base-pq-el-0_$c.f32" "$T/gout/p82-pq-explicit-0_$c.f32" || same=0; done
    check $same "p1200: PL_COLOR_TRC_PQ given explicitly composes as the default"
    echo "== 2. a base layer in BT.1886: with the mutant composed as if ST 2084 (wrong); as it is, clauses 5.4.3.3 (14 bits) and 5.5"
    gpu82 base-1886-noel base p1200 DV_NOEL=1 DV_BL_TRC=bt1886
    cmpd base-1886-noel sn-p1200 p1200 "p1200 BT.1886 (no EL), MUTANT, against the reference of 5.5" > "$T/c.txt"; rc=$?; cat "$T/c.txt"
    check $([ $rc != 0 ] && echo 1 || echo 0) "mutant: the composer does not give the planes of clause 5.5 (the test fails without 9955's code)"
    for cse in "p1200:el:s-p1200:X=1" "p1200:noel:sn-p1200:DV_NOEL=1" "rand:noel:sn-rand:DV_NOEL=1" "rand:el:s-rand:X=1" "ramp:noel:sn-ramp:DV_NOEL=1"; do
      IFS=: read -r d k r e1 <<< "$cse"
      gpu82 "p82-1886-$d-$k" p82 "$d" "$e1" DV_BL_TRC=bt1886 || { check 0 "$d ($k): drawn"; continue; }
      cmpd "p82-1886-$d-$k" "$r" "$d" "$d BT.1886 ($k), against the reference of 5.4.3.3 + 5.5" > "$T/c.txt"; cat "$T/c.txt"
      within1 "$T/c.txt"
      check $([ $? = 0 ] && echo 1 || echo 0) "$d ($k): every sample within 1 code of the reference (32-bit floating point between the filters)"
      grep -q 'clauses 5.4 and 5.5' "$T/gout/p82-1886-$d-$k.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d ($k): libplacebo says it composed with clause 5.5"
    done
    echo "== 3. fail-closed: what clause 5.5 cannot take is not drawn, and the log says why"
    for cse in "rgb709:DV_BL_TRC=bt1886:YCbCr of BT.2020" "nomaster:DV_BL_TRC=bt1886:luminances of the mastering display" "p1200:DV_BL_TRC=hlg:does not declare its HDR picture in SMPTE" "p1200 DV_CCM=0:DV_BL_TRC=bt1886:composer of CCM 001 is off"; do
      IFS=: read -r dd e1 why <<< "$cse"; d=${dd%% *}; e0=${dd#* }; [ "$e0" = "$dd" ] && e0=X=1
      n=p82-refuse-$d-${e1#*=}-${e0%%=*}
      gpu82 "$n" p82 "$d" "$e0" "$e1" DV_NOEL=1; rc=$?
      check $([ $rc != 0 ] && grep -q 'FAIL: render' "$T/gout/$n.log" && echo 1 || echo 0) "$d ($e0 $e1): the render fails (nothing drawn)"
      grep -q "$why" "$T/gout/$n.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d ($e0 $e1): the log says why ($why)"
      grep -q 'composed by the reshaping of libplacebo' "$T/gout/$n.log"; check $([ $? != 0 ] && echo 1 || echo 0) "$d ($e0 $e1): never composed by libplacebo's reshaping"
    done
    gpu82 base-refuse-rgb709 base rgb709 DV_NOEL=1 DV_BL_TRC=bt1886; check $([ $? = 0 ] && echo 1 || echo 0) "mutant: rgb709 is drawn (as if ST 2084): the refusal comes from 9955's code"
    gpu82 p82-rgb709-pq p82 rgb709 DV_NOEL=1; check $([ $? = 0 ] && echo 1 || echo 0) "rgb709 with a base layer in ST 2084: composed (the check is only for BT.1886)"
    echo "fail=$fail"
  } > "$T/p82-run.log" 2>&1
  cat "$T/p82-run.log"
  if [ "$(grep -c '^fail=0$' "$T/p82-run.log")" = 1 ]; then rg_item p82-run PASSED "fail=0, $(grep -c '^ok:' "$T/p82-run.log") checks: ST 2084 planes bit-exact to clause 5.4 and unchanged; BT.1886 within 1 code of 5.4.3.3 + 5.5; refusals with their reasons; the mutant fails what it must"
  else rg_item p82-run FAILED "$(grep -c '^FAIL' "$T/p82-run.log") failing: $(grep '^FAIL' "$T/p82-run.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"; fi
  # p82-run2: rows of the bars left out, hdr_bit_depth 10, 1919x1079
  fail=0
  {
    if [ -x "$T/bin/t-p82" ]; then
      gpu82 p82-1886-rows p82 p1200 DV_NOEL=1 DV_ROWS=1 DV_BL_TRC=bt1886
      same=1; for c in y c; do cmp -s "$T/gout/p82-1886-rows-0_$c.f32" "$T/gout/p82-1886-p1200-noel-0_$c.f32" || same=0; done
      check $same "rows: with the rows of the bars left out, the planes of clause 5.5 are those composed with every row"
      $RG_PY - "$T/data" <<'PY'
import os, sys
import numpy as np
root = sys.argv[1]
lines = open(os.path.join(root, 'rand', 'meta.txt')).read().split('\n')
def write(d, ch):
    out = []
    for l in lines:
        k = l.split(' ', 1)[0]
        out.append(k + ' ' + ch[k] if k in ch else l)
    open(os.path.join(d, 'meta.txt'), 'w').write('\n'.join(out))
d = os.path.join(root, 'rand10'); os.makedirs(d)
for f in os.listdir(os.path.join(root, 'rand')):
    if f.endswith('.u16'):
        os.symlink(os.path.join(root, 'rand', f), os.path.join(d, f))
write(d, {'hdr.vdr_bit_depth': '10'})
rng = np.random.default_rng(2084)
d = os.path.join(root, 'odd'); os.makedirs(d)
w, h = 1919, 1079; wc, hc = (w + 1) // 2, (h + 1) // 2; ew, eh = 960, 540
for n, s in (('bl_y', (h, w)), ('bl_u', (hc, wc)), ('bl_v', (hc, wc)), ('el_y', (eh, ew)), ('el_u', (eh // 2, ew // 2)), ('el_v', (eh // 2, ew // 2))):
    rng.integers(0, 1024, size=s).astype('<u2').tofile(os.path.join(d, n + '.u16'))
write(d, {'bl.width': str(w), 'bl.height': str(h), 'el.width': str(ew), 'el.height': str(eh)})
print('run2 data: rand10 odd')
PY
      for d in rand10 odd; do
        "$T/bin/ccm_ref_1886" -s -n "$T/data/$d" "$T/ref/sn-$d" > "$T/ref/sn-$d.log" 2>&1 || { echo "FAIL: reference $d"; fail=1; continue; }
        gpu82 "p82-1886-$d" p82 "$d" DV_NOEL=1 DV_BL_TRC=bt1886
        cmpd "p82-1886-$d" "sn-$d" "$d" "$d BT.1886, against the reference" > "$T/c2.txt"; cat "$T/c2.txt"
        within1 "$T/c2.txt"
        check $([ $? = 0 ] && echo 1 || echo 0) "$d: every sample within 1 code of the reference"
      done
    else echo "FAIL: no ccm_gpu_test of p82-run"; fail=1; fi
    echo "fail=$fail"
  } > "$T/p82-run2.log" 2>&1
  cat "$T/p82-run2.log"
  if [ "$(grep -c '^fail=0$' "$T/p82-run2.log")" = 1 ]; then rg_item p82-run2 PASSED "fail=0, $(grep -c '^ok:' "$T/p82-run2.log") checks: rows left out = every row; hdr_bit_depth 10 and 1919x1079 within 1 code"
  else rg_item p82-run2 FAILED "$(grep '^FAIL' "$T/p82-run2.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"; fi
  rg_rm "$T/gout" "$T/data" "$T/ref" "$RG_W/lp-p82-base" "$RG_W/lp-p82-after"
}

# the fidelity bench with the GUI cache as Kodi uses it (on, with the composite's key), in the configurations the
# other stages do not draw: against the reference sums of each (the cache must give the bytes of the bridge
# without it), its own check of the copied tiles, and a cache that copies the tiles of the metadata rows (mutant)
FIDX_CFGS="gui:FB_SOURCE=1,FB_GUI=1 noel:FB_SOURCE=1,FB_NOEL=1 src3:FB_SOURCE=1,FB_STEP=3 half:FB_SOURCE=1,FB_HALF=1
  gui1:FB_SOURCE=1,FB_GUI=1,FB_STEP=1 gui2:FB_SOURCE=1,FB_GUI=1,FB_STEP=2 gui3:FB_SOURCE=1,FB_GUI=1,FB_STEP=3
  guis:FB_SOURCE=1,FB_GUI=2 guinoel:FB_SOURCE=1,FB_GUI=1,FB_NOEL=1 guiresh:FB_CCM=0,FB_GUI=1 guicnv:FB_SOURCE=0,FB_GUI=1
  guihalf:FB_SOURCE=1,FB_GUI=1,FB_HALF=1 guioff:FB_SOURCE=1,FB_GUI=1,FB_GCACHE=0 guichk:FB_SOURCE=1,FB_GUI=1,FB_GCHECK=1"
stage_fidx() {
  local K L F O c tag envs Lg v n name
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item fidx FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/fidx; rg_fresh "$O"
  cp "$SRC/fidbench.c" "$O/fidbench-gc.c"
  rg_edit "$O/fidbench-gc.c" "$TS/fidbench-guicache.py" > "$O/edit.log" 2>&1 || { rg_item fidx FAILED "fidbench-guicache.py no longer applies to src/fidbench.c: $(tail -n 1 "$O/edit.log")"; return 1; }
  rg_bridge "$K" "$L" "$F" "$O/fidbench-gc.c" "$O/fb" "$SRC" || { rg_item fidx FAILED "fidbench with the GUI cache did not build: $(tail -n 3 "$O/fb.build.log" | tr '\n' ' ')"; return 1; }
  for c in $FIDX_CFGS; do
    tag=${c%%:*}; envs=${c#*:}; Lg=$O/fid-$tag; name=fid-$tag; [ "$tag" = gui ] && name=fid-gui-cache
    # shellcheck disable=SC2086
    fid_run "$O/fb" "$RG_W/tmp/fidx-$tag" "$tag" "$Lg.log" ${envs//,/ } -- $PICS
    v=$(same "$Lg.log.sums" "$REF/f-$tag.sums")
    n=$(grep -a -c '^gcheck picture' "$Lg.log")
    if grep -q '^FIDBENCH-OK: 14 of 14 pictures drawn, 0 failures' "$Lg.log" && cmp -s "$Lg.log.sums" "$REF/f-$tag.sums" &&
       { [ "$tag" != guichk ] || [ "$n" -gt 0 ]; }; then
      rg_item "$name" PASSED "FIDBENCH-OK 14 of 14 (${envs//,/ }, GUI cache $(grep -a -m1 '^GUI cache:' "$Lg.log" | cut -d' ' -f3)); $(wc -l < "$Lg.log.sums") files, $v$([ "$tag" = guichk ] && echo "; the copied tiles checked in $n pictures, none differing")"
    else
      rg_item "$name" FAILED "$(grep -a -E '^(FIDBENCH|FAIL|gcheck)' "$Lg.log" | head -n 2 | tr '\n' ' ' | cut -c1-160); files: $v"
    fi
  done
  # the mutant: the tiles of the first row copied; the check of the cache must see tiles that differ
  if mutant guicache "$TS/mut-guicache-first-row.py" dvbridge_source.c && rg_bridge "$RG_W/mut/guicache" "$L" "$F" "$O/fidbench-gc.c" "$O/fb-mut" "$SRC"; then
    fid_run "$O/fb-mut" "$RG_W/tmp/fidx-mut" guichk "$O/mut.log" FB_SOURCE=1 FB_GUI=1 FB_GCHECK=1 -- $PICS
    n=$(grep -a 'gcheck picture' "$O/mut.log" | grep -c -v 'differ 0$')
    if [ "$n" -gt 0 ] && grep -q '^FIDBENCH-FAILED' "$O/mut.log"; then rg_item fid-guicache-mutant PASSED "caught: tiles that differ in $n pictures, FIDBENCH-FAILED; $(diff "$REF/f-guichk.sums" "$O/mut.log.sums" | grep -c '^>') files differ from f-guichk.sums"
    else rg_item fid-guicache-mutant FAILED "not caught: $(grep -a -E '^(FIDBENCH|gcheck)' "$O/mut.log" | head -n 3 | tr '\n' ' ' | cut -c1-200)"; fi
  else rg_item fid-guicache-mutant FAILED "the mutant no longer applies or does not build: bring tests/render/mut-guicache-first-row.py up to date"; fi
  rg_rm "$RG_W/mut/guicache" "$O/fb" "$O/fb-mut"
}

# Dolby Vision for output as HDR10 copied into a 1920x1080 window within the area of the frame (Kodi's
# CopyConversion(), dvbridge_gl_copy_area()), in full and limited range; MUT=1 copies at the place instead
stage_hdr10area() {
  local K L F O p ok=0 m
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p1200/meta.bin" ] || { rg_item hdr10-area FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/hdr10area; rg_fresh "$O"
  rg_bridge "$K" "$L" "$F" "$TS/hdr10area.c" "$O/hdr10area" "$SRC" || { rg_item hdr10-area FAILED "did not build: $(tail -n 3 "$O/hdr10area.build.log" | tr '\n' ' ')"; return 1; }
  for p in 1200 240; do
    rg_log "$O/p$p.log" gpu nice -n 19 timeout 900 "$O/hdr10area" "$DATA" "$p"
    [ "$(rg_exit "$O/p$p.log")" = 0 ] && [ "$(grep -a -c '^FAIL' "$O/p$p.log")" = 0 ] && grep -a -q '^HDR10AREA-PASSED' "$O/p$p.log" && ok=$((ok + 1))
  done
  rg_log "$O/mut.log" gpu MUT=1 nice -n 19 timeout 900 "$O/hdr10area" "$DATA" 1200
  m=$(grep -a -c '^FAIL:' "$O/mut.log")
  if [ $ok = 2 ] && [ "$m" -gt 0 ] && grep -a -q '^HDR10AREA-FAILED' "$O/mut.log"; then
    rg_item hdr10-area PASSED "pictures 1200 and 240: nothing outside the area written, inside the words of a renderer of its own, black where a repeated picture does not cover, in full and limited range; copied at the place (MUT=1): $m checks fail"
  else
    rg_item hdr10-area FAILED "$ok of 2 pictures passed ($(grep -a -h -m1 -E '^(FAIL|HDR10AREA)' "$O"/p*.log | tr '\n' ' ' | cut -c1-120)); MUT=1: $m failing"
  fi
}

# the frame that sets the Dolby Vision signal up when a video opens (CDVBridgeGLES::DrawStartSignal()), read by the
# CCM 001 reader; fresh and after a session; and a renderer that does not commit (mutant)
stage_startsignal() {
  local K L F O M v
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  O=$RG_W/startsignal; rg_fresh "$O"
  rg_bridge "$K" "$L" "$F" "$TS/start_signal_test.c" "$O/t" "$SRC" || { rg_item start-signal FAILED "did not build: $(tail -n 3 "$O/t.build.log" | tr '\n' ' ')"; return 1; }
  rg_log "$O/fresh.log" gpu nice -n 19 timeout 600 "$O/t"
  rg_log "$O/session.log" gpu TEST_AFTER_SESSION=1 nice -n 19 timeout 600 "$O/t"
  for v in "start-signal fresh" "start-signal-session session"; do
    set -- $v
    v=$(rg_pass "$O/$2.log" '^START-SIGNAL-OK: [0-9]+ checks, 0 failed$')
    rg_item "$1" "$(rg_say "$v")" "$(rg_rest "$v")$([ "$2" = session ] && echo " (after a native output before it)")"
  done
  if mutant nocommit "$TS/mut-no-commit.py" dvbridge_render.c && rg_bridge "$RG_W/mut/nocommit" "$L" "$F" "$TS/start_signal_test.c" "$O/t-mut" "$SRC"; then
    rg_log "$O/mut.log" gpu TEST_AFTER_SESSION=1 nice -n 19 timeout 600 "$O/t-mut"
    v=$(rg_fails "$O/mut.log" -)
    rg_item start-signal-no-commit "$(rg_say "$v")" "a renderer that does not commit the frame (mutant), after a session: $(rg_rest "$v")"
  else rg_item start-signal-no-commit FAILED "the mutant no longer applies or does not build: bring tests/render/mut-no-commit.py up to date"; fi
  rg_rm "$RG_W/mut/nocommit"
}

# layers in textures larger than their pictures (a decoder's surface): refused, never drawn (fail-closed); the same
# bench unpadded gives the bytes of the plain one; the import that takes them (mutant) draws them
stage_padding() {
  local K L F O c tag envs pad ni=0 un=0 detail="" m
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item padded-layers FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/padding; rg_fresh "$O" "$O/inc"
  $RG_PY "$TS/mkpad.py" "$SRC/fidbench.c" "$SRC/upload.h" "$O/inc" > "$O/mkpad.log" 2>&1 || { rg_item padded-layers FAILED "mkpad.py no longer applies to src/fidbench.c or src/upload.h: $(tail -n 1 "$O/mkpad.log")"; return 1; }
  rg_bridge "$K" "$L" "$F" "$O/inc/fidbench_pad.c" "$O/fp" "$O/inc:$SRC" || { rg_item padded-layers FAILED "did not build: $(tail -n 3 "$O/fp.build.log" | tr '\n' ' ')"; return 1; }
  for c in src0:FB_SOURCE=1 cnv0:FB_SOURCE=0 resh:FB_CCM=0 half:FB_SOURCE=1,FB_HALF=1 halfc:FB_SOURCE=0,FB_HALF=1; do
    tag=${c%%:*}; envs=${c#*:}
    for pad in 0 1; do
      # shellcheck disable=SC2086
      fid_run "$O/fp" "$RG_W/tmp/pad-$tag-$pad" "$tag" "$O/$tag-$pad.log" ${envs//,/ } FB_PADW=$((pad * 64)) FB_PADH=$((pad * 8)) -- 3 240 1200 2160
    done
    # unpadded: the files of the plain bench (src0, cnv0, resh: the lines of the reference sums of these pictures;
    # half, halfc: the plain bench run the same way)
    case $tag in
      half|halfc)
        # shellcheck disable=SC2086
        fid_run "$(bin fidbench)" "$RG_W/tmp/pad-$tag-plain" "$tag" "$O/$tag-plain.log" ${envs//,/ } -- 3 240 1200 2160
        cp "$O/$tag-plain.log.sums" "$O/$tag-want.sums" ;;
      *) grep -E -- "-p(3|240|1200|2160)-" "$REF/f-$tag.sums" > "$O/$tag-want.sums" ;;
    esac
    cmp -s "$O/$tag-0.log.sums" "$O/$tag-want.sums" && [ -s "$O/$tag-want.sums" ] && un=$((un + 1))
    [ "$(grep -a -c 'not imported' "$O/$tag-1.log")" = 4 ] && ni=$((ni + 1))
    detail="$detail $tag: unpadded $(cmp -s "$O/$tag-0.log.sums" "$O/$tag-want.sums" && echo same || echo DIFFERS), padded refused $(grep -a -c 'not imported' "$O/$tag-1.log") of 4;"
  done
  if mutant padded "$TS/mut-padded-import.py" dvbridge_gl_frame.c && rg_bridge "$RG_W/mut/padded" "$L" "$F" "$O/inc/fidbench_pad.c" "$O/fp-mut" "$O/inc:$SRC"; then
    fid_run "$O/fp-mut" "$RG_W/tmp/pad-mut" src0 "$O/mut.log" FB_SOURCE=1 FB_PADW=64 FB_PADH=8 -- 3 240 1200 2160
    m=$(grep -a -c 'not imported' "$O/mut.log")
  else m=mutant-not-applied; fi
  rg_rm "$RG_W/mut/padded" "$O/fp" "$O/fp-mut"
  if [ $un = 5 ] && [ $ni = 5 ] && [ "$m" = 0 ]; then
    rg_item padded-layers PASSED "layers 64x8 samples larger than their pictures refused in the 4 pictures of the 5 configurations, unpadded the plain bench's bytes; an import that takes them (mutant) draws them:$detail"
  else
    rg_item padded-layers FAILED "unpadded the same in $un of 5, padded refused in $ni of 5, the mutant refused $m:$detail"
  fi
}

# every C file of tools/dvbridge compiles without a warning with -Wall -Wextra (as the bridge's own check of new
# code did); an unused variable put in on purpose must be seen
stage_wextra() {
  local K L F O GL VK f n=0 bad=0 w list="" CF
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  GL=$(rg_glinc) || rg_fail "GL headers"; VK=$(rg_vkinc)
  O=$RG_W/wextra; rg_fresh "$O" "$O/inc"
  # dvbridge_dmabuf.c includes <libdrm/drm_fourcc.h>: the build folder's libdrm headers, and only them
  ln -sfn "$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include/libdrm" "$O/inc/libdrm"
  $RG_PY "$RG_REGRESS/oracle/pack_sources.py" "$K/tools/dvbridge" "$O/inc" > /dev/null || rg_fail "pack_sources.h"
  CF="-std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -fsyntax-only"
  cc1() { # <file> <log>
    # shellcheck disable=SC2086
    CPATH=$(rg_vastub) gcc $CF -I"$L/include" -I"$F/include" -I"$GL" -I"$VK" -I"$K/tools/dvbridge" -I"$O/inc" "$1" > "$2" 2>&1
  }
  for f in "$K"/tools/dvbridge/*.c; do
    n=$((n + 1))
    cc1 "$f" "$O/$(basename "$f").log"; rc=$?; w=$(grep -c 'warning:' "$O/$(basename "$f").log")
    if [ $rc != 0 ]; then bad=$((bad + 1)); list="$list $(basename "$f") (errors)"
    elif [ "$w" != 0 ]; then bad=$((bad + 1)); list="$list $(basename "$f") ($w warnings)"; fi
  done
  cp "$K/tools/dvbridge/dvbridge_render.c" "$O/inc/mut_render.c"
  printf '\nvoid dvbridge_test_probe(void);\nvoid dvbridge_test_probe(void)\n{\n  int unused_probe;\n}\n' >> "$O/inc/mut_render.c"
  cc1 "$O/inc/mut_render.c" "$O/mut.log"; w=$(grep -c 'warning:' "$O/mut.log")
  if [ $bad = 0 ] && [ "$w" -ge 1 ]; then rg_item cc-wextra PASSED "$n files of tools/dvbridge without a warning under -Wall -Wextra; an unused variable put in on purpose: $w warning(s)"
  else rg_item cc-wextra FAILED "$bad of $n files with warnings or errors:$list; the probe gave $w warning(s) (see logs in $O)"; fi
}

# profile 8.4 (a base layer in HLG) through the bridge's render test: an RPU that declares ST 2084 composed as the
# render test's own runs; one that declares a gamma signal refused natively and for output as HDR10 (transfer 2),
# never by libplacebo's reshaping; a base layer in BT.1886 composed by clause 5.5; and the bridge that does not
# tell libplacebo the transfer (mutant) composes the gamma one as if ST 2084
P84_GAMMA="39322 15867 228 1383604"
stage_p84() {
  local K L F O fail=0
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/p84; rg_fresh "$O"
  check() { if [ "$1" = 1 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi; }
  yes() { "$@" > /dev/null && echo 1 || echo 0; }
  run84() { # <name> <binary> <first step pictures> <env...>
    local n=$1 b=$2 a=$3
    shift 3
    # shellcheck disable=SC2086
    env $RG_GPU_ENV "$@" nice -n 19 timeout 2400 "$b" "$IN/bl_el.mkv" $a > "$O/$n.log" 2> "$O/$n.err" < /dev/null
    echo "$n: exit $?; $(grep -a -E '^(ALL PASSED|FAILED)' "$O/$n.log" | head -n 1); refused $(grep -a -c 'P84: refused' "$O/$n.log")"
    rt_lines "$O/$n.log" "$O/$n.lines.all"; grep -a -v 'P84' "$O/$n.lines.all" > "$O/$n.lines"
  }
  drawn_none() { ! grep -a -E '^picture [0-9]+[^:]*: margins' "$O/$1.log" | grep -a -v -q 'composed -- [0-9a-f]* early [0-9] | direct -- '; }
  {
    $RG_PY "$TS/p84_mkrt.py" "$TS/render_test.c" "$O/rt84.c" || echo "FAIL: p84_mkrt.py no longer applies to render_test.c"
    rg_bridge "$K" "$L" "$F" "$O/rt84.c" "$O/rt84" || { echo "FAIL: build"; tail -n 10 "$O/rt84.build.log"; }
    if mutant basehlg "$TS/mut-base-hlg-unknown.py" dvbridge_render.c; then
      rg_bridge "$RG_W/mut/basehlg" "$L" "$F" "$O/rt84.c" "$O/rt84-mut" || echo "FAIL: build of the mutant"
    else echo "FAIL: the mutant mut-base-hlg-unknown.py no longer applies"; fi
    rg_rm "$RG_W/mut/basehlg"
    run84 A "$O/rt84" "3 24 6" DV_BASE_TRC=hlg
    check $(yes cmp -s "$O/A.lines" "$REF/rt-ccm.lines") "A: HLG with an RPU that declares PQ: every line as ref/render/rt-ccm.lines ($(wc -l < "$O/A.lines") lines)"
    check $(yes grep -a -q '^ALL PASSED' "$O/A.log") "A: every check of the render test passed"
    check $([ "$(grep -a -c 'P84: refused' "$O/A.log")" = 0 ] && echo 1 || echo 0) "A: nothing refused"
    check $(yes grep -a -q 'a base layer in HLG, whose RPU declares its HDR picture in ST 2084' "$O/A.err") "A: libplacebo says how it composed it"
    run84 B "$O/rt84" "3 24 2" DV_BASE_TRC=hlg DV_SIGNAL_EOTF="$P84_GAMMA"
    check $(yes drawn_none B) "B: HLG, the RPU declaring a gamma signal: no picture drawn natively"
    check $(yes grep -a -q 'P84: the transport refuses the metadata: the colour metadata do not have the signal of the tunnel' "$O/B.log") "B: natively, the transport refuses its metadata"
    check $(yes grep -a -q 'for output as HDR10: not drawn, refused transfer 2' "$O/B.log") "B: for output as HDR10 refused, transfer 2 (HLG)"
    check $(yes grep -a -q 'does not declare its HDR picture in SMPTE ST 2084' "$O/B.err") "B: libplacebo says why"
    check $(yes bash -c "! grep -a -q 'composed by the reshaping of libplacebo' '$O/B.err'") "B: never composed by libplacebo's reshaping"
    run84 C "$O/rt84" "3 24 6" DV_BASE_TRC=hlg DV_CCM=0
    check $(yes cmp -s "$O/C.lines" "$REF/rt-resh.lines") "C: HLG, reshaping, an RPU that declares PQ: every line as ref/render/rt-resh.lines ($(wc -l < "$O/C.lines") lines)"
    check $([ "$(grep -a -c 'P84: refused' "$O/C.log")" = 0 ] && echo 1 || echo 0) "C: nothing refused"
    run84 D "$O/rt84" "3 24 2" DV_BASE_TRC=hlg DV_CCM=0 DV_SIGNAL_EOTF="$P84_GAMMA"
    check $(yes grep -a -q 'for output as HDR10: not drawn, refused transfer 2' "$O/D.log") "D: for output as HDR10 refused (transfer 2)"
    check $(yes grep -a -q 'composer of CCM 001 is off' "$O/D.err") "D: libplacebo says the composer is off"
    run84 E "$O/rt84" "3 24 2" DV_BASE_TRC=sdr
    check $(yes grep -a -q 'clauses 5.4 and 5.5 do' "$O/E.err") "E: SDR (BT.1886): composed with clause 5.5"
    check $([ "$(grep -a -c 'P84: refused' "$O/E.log")" = 0 ] && echo 1 || echo 0) "E: nothing refused"
    if [ -x "$O/rt84-mut" ]; then
      run84 F "$O/rt84-mut" "3 24 2" DV_BASE_TRC=hlg DV_SIGNAL_EOTF="$P84_GAMMA"
      check $(yes grep -a -q 'for output as HDR10: drawn, refused transfer 0, composed 1' "$O/F.log") "F: the bridge that does not tell libplacebo the transfer (mutant): the gamma one drawn for output as HDR10, composed as if PQ, nothing refused (B refuses it)"
    else echo "FAIL: F not run (the mutant did not build)"; fail=1; fi
    echo "fail=$fail"
  } > "$O/p84.log" 2>&1
  cat "$O/p84.log"
  if [ "$(grep -c '^fail=0$' "$O/p84.log")" = 1 ]; then rg_item p84-hlg-render PASSED "fail=0, $(grep -c '^ok:' "$O/p84.log") checks (A, C as the render test's own lines; B, D refused with transfer 2; E composed by clause 5.5; F the mutant composes as if PQ)"
  else rg_item p84-hlg-render FAILED "$(grep -c '^FAIL' "$O/p84.log") failing: $(grep '^FAIL' "$O/p84.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"; fi
  rg_rm "$O/rt84" "$O/rt84-mut"
}

# Kodi's decisions around the bridge, pure host tests with their mutants: a dual-mode DisplayPort connector of
# nvidia-drm (NvidiaDualMode.h, ModeChangeHold.h, DRMCommittedMode.h), the start signal of a video
# (DVStartSignal.h), the rules for a base layer in HLG (DVPlaybackPolicy.h)
stage_hostpol() {
  local K GB O t v
  K=$(rg_kodi) || rg_fail "Kodi tree"; GB=$K/xbmc/windowing/gbm
  O=$RG_W/hostpol; rg_fresh "$O"
  hostrun() { # <item> <std> <test> <ERE of the line that says it passed> <include folders...>
    local n=$1 std=$2 t=$3 re=$4 inc="" d
    shift 4
    for d in "$@"; do inc="$inc -I$d"; done
    # shellcheck disable=SC2086
    rg_log "$O/$n.log" bash -c "g++ -std=$std -Wall -Wextra -Werror $inc '$t' -o '$O/$n' && '$O/$n'"
    v=$(rg_pass "$O/$n.log" "$re" '^(FAIL|BAD)')
    rg_item "$n" "$(rg_say "$v")" "$(rg_rest "$v")"
  }
  mutrun() { # <item> <mutation script> <test>
    rg_log "$O/$1.log" $RG_PY "$2" "$K" "$3" "$O/work-$1"
    local c a
    c=$(grep -a -m1 '^MUTATIONS:' "$O/$1.log")
    a=$(grep -a -c -E '^(MUTATION NOT APPLIED|NOT CAUGHT)' "$O/$1.log")
    if [ "$a" = 0 ] && echo "$c" | grep -q -E '^MUTATIONS: ([0-9]+) of \1 caught$'; then rg_item "$1" PASSED "$c"
    else rg_item "$1" FAILED "${c:-no MUTATIONS line}; $(grep -a -E '^(MUTATION NOT APPLIED|NOT CAUGHT)' "$O/$1.log" | head -n 3 | tr '\n' ';' | cut -c1-200)"; fi
  }
  hostrun dpdual c++20 "$TS/dpdual_test.cpp" '^DPDUAL-OK: [0-9]+ checks, 0 failed$' "$GB" "$GB/drm" "$K/xbmc"
  mutrun dpdual-mutants "$TS/dpdual_mut.py" "$TS/dpdual_test.cpp"
  hostrun dvstartsignal c++17 "$TS/dvstartsignal_test.cpp" '^DVSTARTSIGNAL-OK: [0-9]+ checks, 0 failed$' "$GB" "$GB/drm" "$K/xbmc"
  mutrun dvstartsignal-mutants "$TS/dvstartsignal_mut.py" "$TS/dvstartsignal_test.cpp"
  hostrun policy84 c++20 "$TS/policy84_test.cpp" '^ALL PASSED \(0 failed\)$' "$K/xbmc/cores/VideoPlayer/DVDCodecs/Video" "$K/xbmc"
  mutrun policy84-mutants "$TS/policy84_mut.py" "$TS/policy84_test.cpp"
}

# the IPT tunnel (dvbridge_ipt.h, the IPT pass of dvbridge_source.c): the 14 pictures against an IPT conversion in
# float64 and float32 of the exact composed planes (src/ccm_ref.c; ipt_check.py, ipt_eval.py); the direct and the
# composed way the same bytes; the GUI over it; a profile 5 picture taken from the matrices of its RPU to those of
# the tunnel (a licensed core's), the direct and the composed way the same bytes, and decoded as the picture as
# composed is; two mutants that the oracle must see
# (with the matrices of the licensed core in the tunnel, 2026-11; with those of the RPUs of profile 5 before it was
# "I 0.036% off max 1 | P 0.034% max 1 | T 0.041% max 1": the shares move with the rounding of other matrices, the
# maximum of one code is the target)
IPT_F64="I 0.043% off max 1 | P 0.029% max 1 | T 0.036% max 1"
stage_ipt() {
  local K L F O p v n m d5 bad
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item ipt-oracle FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/ipt; rg_fresh "$O"
  # the exact composed planes of clause 5.4 (ccm_ref.c), checked against ref/render/ccm-ref.sha256
  gcc -O2 -Wall -o "$O/ccm_ref" "$SRC/ccm_ref.c" || rg_fail "ccm_ref"
  for p in $PICS; do
    [ -f "$DATA/p$p/ccmC_cr.u16" ] || "$O/ccm_ref" "$DATA/p$p" "$DATA/p$p/ccmC" > "$O/ccm_ref-p$p.log" 2>&1 || echo "FAIL: ccm_ref p$p"
  done
  v=$( cd "$DATA" && sha256sum -c "$REF/ccm-ref.sha256" 2>&1 | grep -c ': OK$' )
  cp "$SRC/fidbench.c" "$O/fidbench-ipt.c"
  rg_edit "$O/fidbench-ipt.c" "$TS/fidbench-ipt.py" > "$O/edit.log" 2>&1 || { rg_item ipt-oracle FAILED "fidbench-ipt.py no longer applies: $(tail -n 1 "$O/edit.log")"; return 1; }
  rg_bridge "$K" "$L" "$F" "$O/fidbench-ipt.c" "$O/fb" "$SRC" || { rg_item ipt-oracle FAILED "did not build: $(tail -n 3 "$O/fb.build.log" | tr '\n' ' ')"; return 1; }
  ipt_run() { # <tag> <binary> <pictures> <env...>
    local tag=$1 b=$2 pics=$3
    shift 3
    rg_fresh "$O/$tag"
    # shellcheck disable=SC2086
    rg_log "$O/$tag.log" gpu "$@" nice -n 19 timeout 3000 "$b" "$DATA" "$O/$tag" "$tag" $pics
  }
  # --- ipt-oracle and ipt-ways: the 14 pictures
  ipt_run ipt "$O/fb" "$PICS" FB_SOURCE=1 FB_IPT=1
  # shellcheck disable=SC2086
  rg_log "$O/check.log" nice -n 19 $RG_PY "$TS/ipt_check.py" bridge "$DATA" "$O/ipt" ipt $PICS
  n=$(sed -n 's/^ALL vs float64 oracle: //p' "$O/check.log")
  if [ "$v" = 42 ] && grep -a -q '^FIDBENCH-OK: 14 of 14' "$O/ipt.log" && grep -a -q '^BRIDGE-IPT PASSED$' "$O/check.log" && [ "$n" = "$IPT_F64" ]; then
    rg_item ipt-oracle PASSED "14 pictures, the header of the licensed core's IPT; against the float64 oracle: $n; float32: $(sed -n 's/^ALL vs float32 oracle: //p' "$O/check.log"); ideal (reported): $(sed -n 's/^ALL vs ideal: //p' "$O/check.log"); the composed planes of ccm_ref.c as ref/render/ccm-ref.sha256 (42 files)"
  else
    rg_item ipt-oracle FAILED "ccm_ref planes $v of 42 as the reference; $(grep -a -E '^(FIDBENCH|BRIDGE-IPT)' "$O/ipt.log" "$O/check.log" | cut -d: -f2- | tr '\n' ' '); float64: ${n:-none} (want $IPT_F64); $(rg_last "$O/check.log")"
  fi
  # shellcheck disable=SC2086
  bad=$($RG_PY - "$O/ipt" $PICS <<'PY'
import sys
d, pics = sys.argv[1], sys.argv[2:]
bad = 0
for n in pics:
    a = open('%s/ipt-p%s-direct.rgba' % (d, n), 'rb').read()
    b = open('%s/ipt-p%s-composed.rgba' % (d, n), 'rb').read()
    bad += a != b or not a
print(bad)
PY
)
  [ "$bad" = 0 ] && rg_item ipt-ways PASSED "the IPT tunnel of the direct and the composed way the same bytes in all 14 pictures" \
    || rg_item ipt-ways FAILED "${bad:-?} of 14 pictures differ between the direct and the composed way"
  rg_rm "$O/ipt"
  # --- ipt-gui: the GUI over the IPT tunnel; every pixel not drawn over as the direct way
  ipt_run gui "$O/fb" "51 960 2640" FB_SOURCE=1 FB_IPT=1 FB_GUI=2
  rg_log "$O/gui-check.log" nice -n 19 $RG_PY "$TS/ipt_check.py" gui "$O/gui" gui 51 960 2640
  v=$(rg_pass "$O/gui-check.log" '^GUI PASSED$')
  rg_item ipt-gui "$(rg_say "$v")" "the pixels the GUI does not reach as the direct way, pictures 51 960 2640: $(rg_rest "$v")"
  rg_rm "$O/gui"
  # --- ipt-p5: a profile 5 picture (its base layer is IPT-PQ-c2 in the matrices of its RPU): with the tunnel asked,
  # in the matrices of the tunnel, the direct and the composed way the same bytes, and decoded from its header as the
  # picture as composed is decoded from its own
  d5=$O/data5/p1; mkdir -p "$d5"
  $RG_PY - "$IN/p5-chart-bl.yuv" "$d5" <<'PY'
import sys
W, H = 3840, 2160
raw = open(sys.argv[1], 'rb').read()
y, u = raw[:W * H * 2], raw[W * H * 2:W * H * 2 + W * H // 2]
v = raw[W * H * 2 + W * H // 2:]
d = sys.argv[2]
for layer in ('bl', 'el'):
    for name, p in zip('yuv', (y, u, v)):
        open('%s/%s_%s.u16' % (d, layer, name), 'wb').write(p)
open(d + '/meta.txt', 'w').write('bl.width 3840\nbl.height 2160\nel.width 3840\nel.height 2160\n'
                                 'bl.chroma_location 1\nel.chroma_location 1\nbl.pts 0\nel.pts 0\n')
PY
  cp "$IN/p5-chart.meta" "$d5/meta.bin"
  for m in 0 1; do
    rg_fresh "$O/p5-$m"
    rg_log "$O/p5-$m.log" gpu FB_SOURCE=1 FB_NOEL=1 FB_IPT=$m nice -n 19 timeout 1800 "$O/fb" "$O/data5" "$O/p5-$m" p5 1
  done
  n=0; for m in 0 1; do cmp -s "$O/p5-$m/p5-p1-direct.rgba" "$O/p5-$m/p5-p1-composed.rgba" && [ -s "$O/p5-$m/p5-p1-direct.rgba" ] && n=$((n + 1)); done
  rg_log "$O/p5-check.log" nice -n 19 $RG_PY "$TS/ipt_check.py" p5 "$O/p5-0" p5 "$O/p5-1" p5 1
  [ "$n" = 2 ] && grep -a -q '^FIDBENCH-OK' "$O/p5-1.log" && grep -a -q '^P5-IPT PASSED$' "$O/p5-check.log" &&
    rg_item ipt-p5 PASSED "the profile 5 chart: the direct and the composed way the same bytes, asked or not; $(grep -a -m1 '^p1 ' "$O/p5-check.log" | sed 's/^p1 *//')" \
    || rg_item ipt-p5 FAILED "$n of 2 ways the same; $(grep -a -m1 -E '^(p1 |P5-IPT|Traceback)' "$O/p5-check.log"); $(rg_last "$O/p5-1.log")"
  # ... and the conversion of profile 5 through the RGBtoLMS of BT.2020 in place of its RPU's (a mutant): caught
  if mutant ipt-p5lms "$TS/mut-ipt-p5lms.py" dvbridge_source.c &&
     rg_bridge "$RG_W/mut/ipt-p5lms" "$L" "$F" "$O/fidbench-ipt.c" "$O/fb-p5lms" "$SRC"; then
    rg_fresh "$O/p5-m"
    rg_log "$O/p5-m.log" gpu FB_SOURCE=1 FB_NOEL=1 FB_IPT=1 nice -n 19 timeout 1800 "$O/fb-p5lms" "$O/data5" "$O/p5-m" p5 1
    rg_log "$O/p5-m-check.log" nice -n 19 $RG_PY "$TS/ipt_check.py" p5 "$O/p5-0" p5 "$O/p5-m" p5 1
    grep -a -q '^P5-IPT FAILED$' "$O/p5-m-check.log" &&
      rg_item ipt-p5-mutant PASSED "profile 5 converted through the RGBtoLMS of BT.2020: caught, $(grep -a -m1 '^p1 ' "$O/p5-m-check.log" | sed 's/^p1 *//' | cut -c1-150)" ||
      rg_item ipt-p5-mutant FAILED "profile 5 converted through the RGBtoLMS of BT.2020: not caught; $(grep -a -m1 -E '^(p1 |P5-IPT|Traceback)' "$O/p5-m-check.log")"
  else rg_item ipt-p5-mutant FAILED "the mutant no longer applies or does not build"; fi
  rg_rm "$O/p5-0" "$O/p5-1" "$O/p5-m" "$O/data5" "$O/fb-p5lms" "$RG_W/mut/ipt-p5lms"
  # --- ipt-mutants: a coefficient of the matrix, Cb and Cr swapped; caught where the checker fails, or where pictures
  # 51 and 960 are not as far from the float64 oracle as the bridge as it is puts them (the lines of check.log)
  v=""; n=0
  f64() { grep -a -E "^p(51|960) " "$1" | sed 's/.*| vs float64 oracle: \([^|]*\) | vs ideal.*/\1/'; }
  for m in matrix:dvbridge_ipt.h swap:dvbridge_source.c; do
    if mutant "ipt-${m%%:*}" "$TS/mut-ipt-${m%%:*}.py" "${m#*:}" && rg_bridge "$RG_W/mut/ipt-${m%%:*}" "$L" "$F" "$O/fidbench-ipt.c" "$O/fb-${m%%:*}" "$SRC"; then
      ipt_run "mut-${m%%:*}" "$O/fb-${m%%:*}" "51 960" FB_SOURCE=1 FB_IPT=1
      rg_log "$O/mut-${m%%:*}-check.log" nice -n 19 $RG_PY "$TS/ipt_check.py" bridge "$DATA" "$O/mut-${m%%:*}" "mut-${m%%:*}" 51 960
      if grep -a -q '^BRIDGE-IPT FAILED$' "$O/mut-${m%%:*}-check.log" ||
         { [ -n "$(f64 "$O/check.log")" ] && [ "$(f64 "$O/mut-${m%%:*}-check.log")" != "$(f64 "$O/check.log")" ]; }; then n=$((n + 1)); fi
      v="$v ${m%%:*}: $(grep -a -m1 '^ALL vs float64' "$O/mut-${m%%:*}-check.log" | cut -c1-90), $(grep -a -m1 '^BRIDGE-IPT' "$O/mut-${m%%:*}-check.log");"
    else v="$v ${m%%:*}: THE MUTANT NO LONGER APPLIES OR DOES NOT BUILD;"; fi
    rg_rm "$O/mut-${m%%:*}" "$O/fb-${m%%:*}" "$RG_W/mut/ipt-${m%%:*}"
  done
  [ "$n" = 2 ] && rg_item ipt-mutants PASSED "2 of 2 caught by the float64 oracle:$v" || rg_item ipt-mutants FAILED "$n of 2 caught:$v"
  rg_rm "$O/fb"
}

# profile 5, profile 7 FEL and profile 8.1 pictures with Kodi's GUI composited over them (the tree's shaders), packed
# as Kodi packs a frame with GUI elements, in seven modes (p5gui_test.c: its own checks): the frame hashes against
# ref/render/p5gui-<mode>.frames; with the GUI cache on, off, checked and at GUI level 1 the same frames; and a cache
# that copies the tiles of the metadata rows (mutant) caught by its check
P5GUI_MODES="bytes:bytes cache:cache cachemix:cachemix cache:cache-l1:P5GUI_LEVEL=1 cache:cache-l2:P5GUI_LEVEL=2 cache:cache-chk:P5GUI_GCHECK=1 cache:cache-off:P5GUI_GCACHE=0"
stage_p5gui() {
  local K L F O x m t e ok=0 same=0 detail="" n
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/p5gui; rg_fresh "$O"
  rg_bridge "$K" "$L" "$F" "$TS/p5gui_test.c" "$O/p" || { rg_item p5gui FAILED "did not build: $(tail -n 3 "$O/p.build.log" | tr '\n' ' ')"; return 1; }
  p5mode() { # <binary> <mode> <tag> <env or ->
    local e=$4
    [ "$e" = - ] && e=X=1
    rg_log "$O/p5-$3.log" gpu "$e" nice -n 19 timeout 3600 "$1" "$IN/bl_el.mkv" "$2" "$K/system/shaders/GLES/2.0" "$IN" "$IN"
    grep -a -E '^frame:' "$O/p5-$3.log" > "$O/p5-$3.frames"
  }
  for x in $P5GUI_MODES; do
    m=${x%%:*}; t=${x#*:}; e=-; case $t in *:*) e=${t#*:}; t=${t%%:*} ;; esac
    p5mode "$O/p" "$m" "$t" "$e"
    if [ "$(rg_exit "$O/p5-$t.log")" = 0 ] && [ "$(grep -a -c '^FAIL' "$O/p5-$t.log")" = 0 ] && grep -a -q '^ALL PASSED$' "$O/p5-$t.log"; then ok=$((ok + 1)); fi
    cmp -s "$O/p5-$t.frames" "$REF/p5gui-$t.frames" && [ -s "$O/p5-$t.frames" ] && same=$((same + 1))
    detail="$detail $t: $(wc -l < "$O/p5-$t.frames") frames;"
  done
  n=0; for t in cache-off cache-chk; do cmp -s "$O/p5-cache.frames" "$O/p5-$t.frames" && [ -s "$O/p5-$t.frames" ] && n=$((n + 1)); done
  # at GUI level 1 the frames over the profile 8.1 picture (no enhancement layer) go out converted, by design; the
  # others are the frames of level 0
  grep -v ' p8.1-' "$O/p5-cache.frames" > "$O/l0.frames"; grep -v ' p8.1-' "$O/p5-cache-l1.frames" > "$O/l1.frames"
  cmp -s "$O/l0.frames" "$O/l1.frames" && [ -s "$O/l1.frames" ] && n=$((n + 1))
  if [ $ok = 7 ] && [ $same = 7 ] && [ $n = 3 ]; then
    rg_item p5gui PASSED "7 of 7 modes ALL PASSED, 0 FAIL;$detail every frame list as ref/render/p5gui-*.frames; the cache off and checked the frames of the cache on, at GUI level 1 too but over the picture without an enhancement layer (converted by design)"
  else
    rg_item p5gui FAILED "$ok of 7 modes passed, $same of 7 frame lists as the references, $n of 3 invariants with the cache on held:$detail"
  fi
  if mutant p5cache "$TS/mut-guicache-first-row.py" dvbridge_source.c && rg_bridge "$RG_W/mut/p5cache" "$L" "$F" "$TS/p5gui_test.c" "$O/p-mut"; then
    p5mode "$O/p-mut" cache cache-chk-mut P5GUI_GCHECK=1
    n=$(grep -a 'cachecheck:' "$O/p5-cache-chk-mut.log" | grep -c -v 'differ 0$')
    if [ "$n" -gt 0 ] && grep -a -q '^FAILED' "$O/p5-cache-chk-mut.log"; then rg_item p5gui-cache-mutant PASSED "caught by the cache's check: $n frames with tiles that differ"
    else rg_item p5gui-cache-mutant FAILED "not caught: $(grep -a -E '^(ALL PASSED|FAILED|cachecheck)' "$O/p5-cache-chk-mut.log" | head -n 2 | tr '\n' ' ')"; fi
  else rg_item p5gui-cache-mutant FAILED "the mutant no longer applies or does not build"; fi
  rg_rm "$RG_W/mut/p5cache" "$O/p" "$O/p-mut"
}

# profile 8.4 composed by libplacebo alone (p84_gpu.c) on real RPUs of profile 8.4 (dovi_tool's test assets, parsed by
# FFmpeg's own RPU parser: p84_rpuinfo.c) over an HLG chart (p84_mkdata.py): an RPU that declares ST 2084 at 12 bits
# composed byte for byte as clause 5.4 (src/ccm_ref.c); one that declares a gamma signal, or 10 bits, refused with the
# reason, never by libplacebo's reshaping; with libplacebo that refuses HLG outright (mutant, the composer before
# libplacebo-9975) what tells HLG apart fails, and every other base layer is as with it
stage_p84gpu() {
  local F GL VK FS T n l fail=0 d s c
  F=$(ff) || rg_fail "FFmpeg"; GL=$(rg_glinc) || rg_fail "GL headers"; VK=$(rg_vkinc)
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  T=$RG_W/p84gpu; rg_fresh "$T" "$T/bin" "$T/data" "$T/ref" "$T/gout"
  check() { if [ "$1" = 1 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi; }
  run() { # <name> <lp: base|p84> <dir> <env...>
    local n=$1 l=$2 dd=$3
    shift 3
    env $RG_GPU_ENV "$@" nice -n 19 timeout 1200 "$T/bin/g-$l" "$T/data/$dd" > "$T/gout/$n.log" 2>&1 < /dev/null
    echo "$n: exit $?; $(grep -a -E 'render |not composed|composed as|not drawn' "$T/gout/$n.log" | tr '\n' ' ' | cut -c1-300)"
  }
  same_ref() { local s=1 c; for c in y cb cr; do cmp -s "$T/gout/$1_$c.u16" "$T/ref/$2_$c.u16" || s=0; done; echo $s; }
  {
    FS=$(ff_internal) || echo "FAIL: FFmpeg's source tree for its RPU parser"
    gcc -O1 -I"$FS" -o "$T/bin/rpuinfo" "$TS/p84_rpuinfo.c" "$FS/libavcodec/libavcodec.a" "$FS/libavutil/libavutil.a" -lm -lpthread \
      > "$T/bin/rpuinfo.build.log" 2>&1 || { head -n 20 "$T/bin/rpuinfo.build.log"; echo "FAIL: p84_rpuinfo"; }
    "$T/bin/rpuinfo" "$IN/profile84.bin" "$T/data/iphone.meta" 1920 1080 > "$T/data/iphone.rpu.txt" || echo "FAIL: rpuinfo iphone"
    "$T/bin/rpuinfo" "$IN/p84-01-ref.bin" "$T/data/dolby.meta" 1920 1080 > "$T/data/dolby.rpu.txt" || echo "FAIL: rpuinfo dolby"
    for n in iphone dolby; do echo "$n: $(grep -E '^(color.signal_eotf|color.signal_bit_depth|hdr.vdr_bit_depth|hdr.disable_residual_flag) ' "$T/data/$n.meta" | tr '\n' ';')"; done
    for n in iphone dolby; do $RG_PY "$TS/p84_mkdata.py" "$T/data/$n.meta" "$T/data/$n" > /dev/null || echo "FAIL: p84_mkdata.py $n"; done
    $RG_PY "$TS/p84_mkdata.py" "$T/data/iphone.meta" "$T/data/gamma" "39322 15867 228 1383604" > /dev/null || echo "FAIL: p84_mkdata.py gamma"
    $RG_PY "$TS/p84_mkdata.py" "$T/data/iphone.meta" "$T/data/bits" > /dev/null || echo "FAIL: p84_mkdata.py bits"
    sed -i 's/^color.signal_bit_depth .*/color.signal_bit_depth 10/' "$T/data/bits/meta.txt"
    gcc -O2 -Wall -o "$T/bin/ccm_ref" "$SRC/ccm_ref.c" -lm || echo "FAIL: ccm_ref"
    for n in iphone dolby; do "$T/bin/ccm_ref" -n "$T/data/$n" "$T/ref/$n" > "$T/ref/$n.log" 2>&1 || echo "FAIL: reference $n"; done
    for n in base p84; do
      if [ $n = base ]; then l=$(lpvar p84-nohlg "$TS/lp-no-hlg.py") || { echo "FAIL: libplacebo without HLG (the edit or the build)"; continue; }
      else l=$(lp) || { echo "FAIL: libplacebo"; continue; }; fi
      gcc -std=gnu11 -O1 -g -Wall -I"$l/include" -I"$F/include" -I"$GL" -I"$VK" -c -o "$T/bin/g-$n.o" "$TS/p84_gpu.c" &&
        g++ -o "$T/bin/g-$n" "$T/bin/g-$n.o" "$l/lib/libplacebo.a" -L"$F/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl \
          /usr/lib/x86_64-linux-gnu/libEGL.so.1 || echo "FAIL: build of p84_gpu against $n"
    done
    echo "== the mutant (libplacebo that refuses HLG outright), the transfer not given"
    for d in iphone dolby gamma bits; do
      run b-$d base $d DV_ONLY="$T/gout/b-$d"
      grep -q 'composed 1' "$T/gout/b-$d.log"; check $([ $? = 0 ] && echo 1 || echo 0) "mutant, $d: composed (as if its base layer were in PQ, whatever its RPU declares)"
    done
    check $(same_ref b-iphone iphone) "mutant, iphone: the composed planes are ccm_ref's (clause 5.4), byte for byte"
    check $(same_ref b-dolby dolby) "mutant, dolby: the composed planes are ccm_ref's (clause 5.4), byte for byte"
    run b-hlg base iphone DV_ONLY="$T/gout/b-hlg" DV_BL_TRC=hlg
    grep -q 'render FAILED' "$T/gout/b-hlg.log"; check $([ $? = 0 ] && echo 1 || echo 0) "mutant, PL_COLOR_TRC_HLG: refused whatever the RPU says"
    echo "== libplacebo of the recipe (libplacebo-9975), PL_COLOR_TRC_HLG"
    for d in iphone dolby; do
      run a-$d p84 $d DV_ONLY="$T/gout/a-$d" DV_BL_TRC=hlg
      grep -q 'composed 1' "$T/gout/a-$d.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d (its RPU declares PQ at 12 bits): composed"
      check $(same_ref a-$d $d) "$d: the composed planes are ccm_ref's (clause 5.4), byte for byte"
      s=1; for c in y cb cr; do cmp -s "$T/gout/a-${d}_$c.u16" "$T/gout/b-${d}_$c.u16" || s=0; done
      check $s "$d: the same planes as composed as PQ"
      grep -q 'a base layer in HLG, whose RPU declares' "$T/gout/a-$d.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d: libplacebo says how it composed it"
    done
    for d in gamma bits; do
      run a-$d p84 $d DV_ONLY="$T/gout/a-$d" DV_BL_TRC=hlg
      grep -q 'render FAILED' "$T/gout/a-$d.log" && grep -q 'composed 0' "$T/gout/a-$d.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d: refused, nothing drawn"
      grep -q 'does not declare its HDR picture in SMPTE ST 2084' "$T/gout/a-$d.log"; check $([ $? = 0 ] && echo 1 || echo 0) "$d: the log says why"
      grep -q 'composed by the reshaping of libplacebo' "$T/gout/a-$d.log"; check $([ $? != 0 ] && echo 1 || echo 0) "$d: never composed by libplacebo's reshaping"
    done
    echo "== the whole render (the converted transport's), and libplacebo's own reshaping"
    run w-iphone p84 iphone DV_BL_TRC=hlg
    grep -q 'render ok, composed 1' "$T/gout/w-iphone.log"; check $([ $? = 0 ] && echo 1 || echo 0) "iphone, composer on: drawn, composed by the clause"
    run r-iphone p84 iphone DV_BL_TRC=hlg DV_CCM=0
    grep -q 'render ok, composed 0' "$T/gout/r-iphone.log"; check $([ $? = 0 ] && echo 1 || echo 0) "iphone, composer off: drawn by libplacebo's reshaping (its RPU declares PQ, which the reshaping gives)"
    run r-gamma p84 gamma DV_BL_TRC=hlg DV_CCM=0
    grep -q 'render FAILED' "$T/gout/r-gamma.log"; check $([ $? = 0 ] && echo 1 || echo 0) "gamma, composer off: never drawn by libplacebo's reshaping"
    grep -q 'composer of CCM 001 is off' "$T/gout/r-gamma.log"; check $([ $? = 0 ] && echo 1 || echo 0) "gamma, composer off: the log says why"
    echo "== every other base layer as with the mutant"
    for d in iphone gamma; do
      run u-$d p84 $d DV_ONLY="$T/gout/u-$d"
      s=1; for c in y cb cr; do cmp -s "$T/gout/u-${d}_$c.u16" "$T/gout/b-${d}_$c.u16" || s=0; done
      check $s "$d, the transfer not given: the same planes as with the mutant"
    done
    echo "fail=$fail"
  } > "$T/p84gpu.log" 2>&1
  cat "$T/p84gpu.log"
  if [ "$(grep -c '^fail=0$' "$T/p84gpu.log")" = 1 ] && [ "$(grep -c '^FAIL' "$T/p84gpu.log")" = 0 ]; then
    rg_item p84-hlg-compose PASSED "fail=0, $(grep -c '^ok:' "$T/p84gpu.log") checks: real profile 8.4 RPUs composed byte for byte as clause 5.4, the gamma and 10-bit ones refused with the reason, never by the reshaping; the mutant refuses HLG"
  else
    rg_item p84-hlg-compose FAILED "$(grep -c '^FAIL' "$T/p84gpu.log") failing: $(grep '^FAIL' "$T/p84gpu.log" | head -n 3 | cut -c1-80 | tr '\n' ';')"
  fi
  rg_rm "$T/gout" "$T/data" "$T/ref" "$RG_W/lp-p84-nohlg"
}

# the MMR table of the composer (libplacebo-9977, dvbridge_render_use_mmr_table(), off by default): with it on, the
# same bytes as without it (source and converted transports); a table one step off (mutant) changes the composed
# chroma with it on and nothing with it off
stage_mmr() {
  local K L F O c lm n m v=""
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item mmr-table FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/mmr; rg_fresh "$O"
  cp "$SRC/fidbench.c" "$O/fidbench-mmr.c"
  rg_edit "$O/fidbench-mmr.c" "$TS/fidbench-mmr.py" > "$O/edit.log" 2>&1 || { rg_item mmr-table FAILED "fidbench-mmr.py no longer applies: $(tail -n 1 "$O/edit.log")"; return 1; }
  rg_bridge "$K" "$L" "$F" "$O/fidbench-mmr.c" "$O/fb" "$SRC" || { rg_item mmr-table FAILED "did not build: $(tail -n 3 "$O/fb.build.log" | tr '\n' ' ')"; return 1; }
  n=0
  for c in src0:FB_SOURCE=1 cnv0:FB_SOURCE=0; do
    fid_run "$O/fb" "$RG_W/tmp/mmr-${c%%:*}" "${c%%:*}" "$O/on-${c%%:*}.log" "${c#*:}" FB_MMRT=1 -- $PICS
    grep -a -q '^MMR table: on' "$O/on-${c%%:*}.log" && grep -a -q '^FIDBENCH-OK: 14 of 14' "$O/on-${c%%:*}.log" &&
      cmp -s "$O/on-${c%%:*}.log.sums" "$REF/f-${c%%:*}.sums" && n=$((n + 1))
    v="$v ${c%%:*}: $(same "$O/on-${c%%:*}.log.sums" "$REF/f-${c%%:*}.sums");"
  done
  [ "$n" = 2 ] && rg_item mmr-table PASSED "the MMR table on, 14 pictures:$v" || rg_item mmr-table FAILED "$n of 2 as the references with the table on:$v"
  if lm=$(lpvar mmrmut "$TS/lp-mut-mmr-table.py") && rg_bridge "$K" "$lm" "$F" "$O/fidbench-mmr.c" "$O/fb-mut" "$SRC"; then
    fid_run "$O/fb-mut" "$RG_W/tmp/mmr-mut-on" src0 "$O/mut-on.log" FB_SOURCE=1 FB_MMRT=1 -- $PICS
    fid_run "$O/fb-mut" "$RG_W/tmp/mmr-mut-off" src0 "$O/mut-off.log" FB_SOURCE=1 -- $PICS
    m=$(fid_count "$O/mut-on.log.sums" "$REF/f-src0.sums")
    if [ "$m" -gt 0 ] && cmp -s "$O/mut-off.log.sums" "$REF/f-src0.sums"; then
      rg_item mmr-table-mutant PASSED "a table one step off: $m of 98 files of src0 differ with the table on, none with it off"
    else
      rg_item mmr-table-mutant FAILED "with the table on $m files differ; with it off: $(same "$O/mut-off.log.sums" "$REF/f-src0.sums")"
    fi
  else rg_item mmr-table-mutant FAILED "the mutant lp-mut-mmr-table.py no longer applies or does not build"; fi
  rg_rm "$O/fb" "$O/fb-mut" "$RG_W/lp-mmrmut"
}

# The 3D LUT of a display (dvbridge_lut.h, dvbridge_render_set_lut()): lut3d_test.c on the PQ chart (mapped to SDR, to
# a projector with and without its black, as it is, kept as 10-bit codes) and on picture 3 of the FEL stream (native and
# Low-Latency, with the bars written first and not), with the LUT files of lut3d_check.py (.cube, .3dl, madVR's), whose
# check applies each LUT to the picture drawn without it with arithmetic of its own; and the mutants it must catch, each
# with the lines "FAIL..." of the test and of the check that it makes (measured when written): a .3dl in the order of its
# file, the video levels of a LUT's input left out, the bars of Low-Latency left to the LUT, 10-bit codes made before
# the LUT, the LUT applied to HDR over the tunnel, a LUT that fails not drawn again without it.
stage_lut3d() {
  local K L F O v m got n=0 ok=0
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  [ -f "$DATA/p3/meta.bin" ] || { rg_item lut3d FAILED "no pictures (stage data)"; return 1; }
  O=$RG_W/lut3d; rg_fresh "$O" "$O/luts"
  $RG_PY "$TS/lut3d_check.py" make "$O/luts" > "$O/make.log" 2>&1 || { rg_item lut3d FAILED "the LUT files: $(tail -n 1 "$O/make.log")"; return 1; }
  # lt <tree> <tag>: the test of that tree into $O/<tag>.log, checked into $O/<tag>.check
  lt() {
    rg_bridge "$1" "$L" "$F" "$TS/lut3d_test.c" "$O/t-$2" "$SRC" || { echo "$2: BUILD FAILED"; tail -n 5 "$O/t-$2.build.log"; return 1; }
    rg_fresh "$O/out-$2"
    rg_log "$O/$2.log" gpu nice -n 19 timeout 2400 "$O/t-$2" "$IN/chart-pq.yuv" 1 "$O/luts" "$O/out-$2" "$DATA/p3"
    rg_log "$O/$2.check" $RG_PY "$TS/lut3d_check.py" check "$O/luts" "$O/out-$2" "$O/$2.log"
    rg_rm "$O/t-$2" "$O/out-$2"
  }
  lt "$K" tree
  v=$(rg_pass "$O/tree.log" '^PASSED: 0 failed$')
  rg_item lut3d-render "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.log") checks; $(grep -a -m1 '^identity sdr ' "$O/tree.log" | cut -c1-120); $(grep -a -m1 '^identity ll ' "$O/tree.log" | cut -c1-120); $(grep -a -m1 '^time sdr' "$O/tree.log" | cut -c1-120); $(grep -a -m1 '^time ll' "$O/tree.log" | cut -c1-90)"
  v=$(rg_pass "$O/tree.check" '^LUT3D-CHECK PASSED: 0 failed$')
  rg_item lut3d-reference "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.check") checks against the LUT applied in Python; sdr $(grep -a -m1 'sdr (tint)' "$O/tree.check" | grep -o 'at most [^ ]* apart'); ll $(grep -a -m1 '^ok:   ll (tint)' "$O/tree.check" | grep -o 'at most [^ ]* apart')"
  # the mutants: <name> <edit file> <file under tools/dvbridge> <FAIL lines of the test> <of the check>
  for m in "3dl-order mut-lut-3dl-order.py dvbridge_lut.c 0 3" "range mut-lut-range.py dvbridge_lut.c 0 2"            "bars mut-lut-bars.py dvbridge_render.c 2 0" "codes mut-lut-codes.py dvbridge_render.c 0 1"            "tunnel mut-lut-tunnel.py dvbridge_render.c 2 0" "retry mut-lut-retry.py dvbridge_render.c 3 0"; do
    set -- $m
    n=$((n + 1))
    if mutant "lut3d-$1" "$TS/$2" "$3"; then
      lt "$RG_W/mut/lut3d-$1" "m-$1" > "$O/m-$1.build" 2>&1
      rg_rm "$RG_W/mut/lut3d-$1"
    else echo "mutant $1: no longer applies"; continue; fi
    got="$(grep -a -c '^FAIL' "$O/m-$1.log") $(grep -a -c '^FAIL' "$O/m-$1.check")"
    if [ "$got" = "$4 $5" ]; then ok=$((ok + 1)); else echo "mutant $1: $got lines FAIL (test, check), $4 $5 expected"; fi
  done
  rg_item lut3d-mutants "$([ $ok = $n ] && echo PASSED || echo FAILED)" "$ok of $n mutants failed as expected (lines FAIL of the test and of the check: a .3dl in its file's order 0 and 3, video levels left out 0 and 2, the bars of Low-Latency 2 and 0, codes before the LUT 0 and 1, HDR over the tunnel 2 and 0, no retry without a LUT that fails 3 and 0)"
}

# The 3D LUT of a display in the video shaders of Kodi's own renderer, the way SDR videos that Kodi draws itself take it
# (system/shaders/GLES/2.0/gles_lut3d.frag and gles_lut3d_body.frag, put in by VideoShaders/Lut3dGLES.h; the table laid
# out by tools/dvbridge/dvbridge_lut_atlas.c): lutsdr_test.c draws a picture of an SDR clip (bd1080.264, its base view)
# the ways Kodi draws one (the YUV shader of GLES 2.0 in one pass, of a software and of a VAAPI picture, to an output
# of full and of limited range; the filter shader of GLES 3.1; a field; HDR tone mapped by Kodi; two passes with Kodi's
# scalers), without a LUT, with identity LUTs (every byte and 10-bit code of the picture without it) and with the LUT
# files of lut3d_check.py (.cube, .3dl, madVR's), which lutsdr_check.py checks against the LUT applied in Python to the
# picture drawn without it; and the mutants it must catch, each with the lines "FAIL..." of the test and of the check
# that it makes (measured when written).
# lutsdr_copy <dest>: a copy of the tree under test's tools/dvbridge and system/shaders/GLES (for a mutant)
lutsdr_copy() {
  local K
  K=$(rg_kodi) || return 1
  rg_fresh "$1/tools" "$1/system/shaders"
  cp -a "$K/tools/dvbridge" "$1/tools/" && cp -a "$K/system/shaders/GLES" "$1/system/shaders/"
}
stage_lutsdr() {
  local K L F O v m got n=0 ok=0
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/lutsdr; rg_fresh "$O" "$O/luts"
  $RG_PY "$TS/lut3d_check.py" make "$O/luts" > "$O/make.log" 2>&1 || { rg_item lutsdr FAILED "the LUT files: $(tail -n 1 "$O/make.log")"; return 1; }
  # lt <tree> <tag>: the test of that tree into $O/<tag>.log, checked into $O/<tag>.check
  lt() {
    rg_bridge "$1" "$L" "$F" "$TS/lutsdr_test.c" "$O/t-$2" "$SRC" || { echo "$2: BUILD FAILED"; tail -n 5 "$O/t-$2.build.log"; return 1; }
    rg_fresh "$O/out-$2"
    rg_log "$O/$2.log" gpu nice -n 19 timeout 1800 "$O/t-$2" "$1" "$IN/bd1080.264" "$O/luts" "$O/out-$2"
    rg_log "$O/$2.check" $RG_PY "$TS/lutsdr_check.py" "$O/luts" "$O/out-$2" "$O/$2.log"
    rg_rm "$O/t-$2" "$O/out-$2"
  }
  lt "$K" tree
  v=$(rg_pass "$O/tree.log" '^PASSED: 0 failed$')
  rg_item lutsdr-render "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.log") checks; $(grep -a -m1 '^identity basic-yv12 id65' "$O/tree.log" | cut -c1-140); $(grep -a -m1 '^identity conv4 ' "$O/tree.log" | cut -c1-140); $(grep -a -m1 '^time ' "$O/tree.log" | cut -c1-140)"
  v=$(rg_pass "$O/tree.check" '^LUTSDR-CHECK PASSED: 0 failed$')
  rg_item lutsdr-reference "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.check") checks against the LUT applied in Python; $(grep -a -o 'at most [^ ]* apart' "$O/tree.check" | sort -k3 -g | tail -n 1) at worst"
  # the mutants: <name> <edit file> <file under the tree> <FAIL lines of the test> <of the check>
  for m in "range mut-lutsdr-range.py system/shaders/GLES/2.0/gles_lut3d.frag 0 2" \
           "tetra mut-lutsdr-tetra.py system/shaders/GLES/2.0/gles_lut3d.frag 10 11" \
           "atlas mut-lutsdr-atlas.py tools/dvbridge/dvbridge_lut_atlas.c 11 11" \
           "shaper mut-lutsdr-shaper.py system/shaders/GLES/2.0/gles_lut3d.frag 0 1" \
           "apply mut-lutsdr-apply.py system/shaders/GLES/2.0/gles_lut3d.frag 8 0" \
           "beyond mut-lutsdr-beyond.py system/shaders/GLES/2.0/gles_lut3d.frag 3 2"; do
    set -- $m
    n=$((n + 1))
    if lutsdr_copy "$RG_W/mut/lutsdr-$1" && rg_edit "$RG_W/mut/lutsdr-$1/$3" "$TS/$2" > "$RG_W/mut/lutsdr-$1.edit.log" 2>&1; then
      lt "$RG_W/mut/lutsdr-$1" "m-$1" > "$O/m-$1.build" 2>&1
      rg_rm "$RG_W/mut/lutsdr-$1"
    else echo "mutant $1: no longer applies"; continue; fi
    got="$(grep -a -c '^FAIL' "$O/m-$1.log") $(grep -a -c '^FAIL' "$O/m-$1.check")"
    echo "mutant $1: $got lines FAIL (test, check), $4 $5 expected"
    if [ "$got" = "$4 $5" ]; then ok=$((ok + 1)); fi
  done
  rg_item lutsdr-mutants "$([ $ok = $n ] && echo PASSED || echo FAILED)" "$ok of $n mutants failed as expected (lines FAIL of the test and of the check: the input range of a LUT left out 0 and 2, a tetrahedron of other corners 10 and 11, the slices of the table out of place 11 and 11, Nuke's shaper left out 0 and 1, a shader told not to apply the LUT applying it 8 and 0, what is beyond black and white of a limited output lost 3 and 2)"
}

# An 8-bit HLG picture sent as Dolby Vision (dvbridge_gl_widen.c; Kodi's VAAPI renderer, WidenForBridge()): the HLG
# chart brought to 8 bits and a picture with every code in each plane, given as NV12 planes and widened, against the
# same pictures given as P010 planes of (v << 2) << 6: every widened sample, the picture drawn (floats), the transport
# packed with its metadata (bytes) and the measurement, the same; the GL state kept; the planes given as they are
# refused (hlg8_test.c). Three mutants of the pass (the 10 bits at the bottom of 16, the chroma not widened, the GL
# state not put back), and in the source the rule, the probe and the three ways of the renderer (hlg8_src_check.py),
# with a copy of the renderer without the widening on the native output, which it must find.
stage_hlg8() {
  local K L F O v t got n=0 ok=0 C
  K=$(rg_kodi) || rg_fail "Kodi tree"; L=$(lp) || rg_fail "libplacebo"; F=$(ff) || rg_fail "FFmpeg"
  rg_inputs "$LIST" "$IN" > /dev/null || rg_fail "inputs"
  O=$RG_W/hlg8; rg_fresh "$O"
  h8() { # <tree> <tag>
    rg_bridge "$1" "$L" "$F" "$TS/hlg8_test.c" "$O/t-$2" || { echo "$2: BUILD FAILED"; tail -n 5 "$O/t-$2.build.log"; return 1; }
    rg_log "$O/$2.log" gpu nice -n 19 timeout 2400 "$O/t-$2" "$IN/chart-hlg.yuv"
    rg_rm "$O/t-$2"
  }
  h8 "$K" tree
  v=$(rg_pass "$O/tree.log" '^PASSED: 0 failed$')
  rg_item hlg8 "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/tree.log") checks; $(grep -a -m1 'converted to ST 2084 differ' "$O/tree.log" | cut -c7-200)"
  for t in "p010-shift mut-widen-p010-shift.py 8" "luma-only mut-widen-luma-only.py 8" "state mut-widen-state.py 2"; do
    set -- $t
    n=$((n + 1))
    if mutant "hlg8-$1" "$TS/$2" dvbridge_gl_widen.c && h8 "$RG_W/mut/hlg8-$1" "m-$1" > "$O/m-$1.build" 2>&1; then
      got=$(grep -a -c '^FAIL:' "$O/m-$1.log")
      if [ "$got" = "$3" ]; then ok=$((ok + 1)); else echo "mutant $1: failed $got checks, $3 expected"; fi
    else echo "mutant $1: no longer applies or does not build"; fi
    rg_rm "$RG_W/mut/hlg8-$1"
  done
  rg_item hlg8-mutants "$([ $ok = $n ] && echo PASSED || echo FAILED)" "$ok of $n mutants failed the checks expected (the 10 bits at the bottom: 8; the chroma not widened: 8; the GL state not put back: 2)"
  rg_log "$O/src.log" $RG_PY "$TS/hlg8_src_check.py" "$K"
  v=$(rg_pass "$O/src.log" '^PASSED: 0 failed$')
  rg_item hlg8-src "$(rg_say "$v")" "$(rg_rest "$v"): $(grep -a -c '^ok:' "$O/src.log") checks"
  C=$O/src-without; rg_fresh "$C"
  for t in xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h xbmc/cores/VideoPlayer/DVDDemuxers/DVHdr10Plus.cpp \
           xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/RendererVAAPIGLES.cpp; do
    mkdir -p "$C/$(dirname "$t")" && cp "$K/$t" "$C/$t"
  done
  $RG_PY - "$C/xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/RendererVAAPIGLES.cpp" << 'PY'
import sys
p = sys.argv[1]
t = open(p).read()
old = '  if (!WidenForBridge(idx, layer))\n    return DVFrameRejected("hlg-8-bit-widening");\n'
if t.count(old) != 1:
    sys.exit(3)
open(p, 'w').write(t.replace(old, ''))
PY
  rg_log "$O/src-without.log" $RG_PY "$TS/hlg8_src_check.py" "$C"
  got=$(grep -a -c '^FAIL:' "$O/src-without.log")
  [ "$got" = 1 ] && rg_item hlg8-src.without PASSED "the renderer without the widening on the native output: 1 check fails, as it must" ||
    rg_item hlg8-src.without FAILED "the renderer without the widening on the native output: $got checks fail, not 1"
  rg_rm "$C"
}

rg_main "$STAGES" "$@"
