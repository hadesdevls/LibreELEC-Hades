#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-oracle.sh: the composer oracle of ETSI GS CCM 001 clause 5.4 (ccm001_oracle.py) against libplacebo's
# composer as Kodi's Dolby Vision bridge runs it (fidbench, on zink/lavapipe), on real profile 7 FEL pictures.
# Host test only: it builds and runs everything in its work folder and never writes into the trees it reads.
#
# usage: run-oracle.sh <stage> [...]
#   build    libplacebo (verified tarball + the patch series of a LibreELEC ref), Kodi's tools/dvbridge
#            (git archive of a ref), ref_dump, fidbench, rpu_sets, lutcheck
#   scan     the RPU of every picture of each clip (rpu_sets), grouped into distinct composer sets
#   select   the pictures to draw (spread pictures, one per mapping shape, the extremes of the NLQ sets)
#   dump     their decoded layers and RPU (ref_dump)
#   bench    fidbench on them (the composer of CCM 001, source transport: its composed planes)
#   compare  the oracle against fidbench, picture by picture (ORACLE-ITEM lines)
#   luts     libplacebo's tables (pl_dovi_ccm_prepare) against the oracle for every distinct RPU
#   mutate   each mutation of the oracle must make the comparison fail (proof of the harness)
#   negative the identity mapping without EL, a set whose result is known
#   verdict  ORACLE-ITEM lines of the log counted line by line; ORACLE-DONE
#   all      every stage in order, into $WORK/logs/run.log, then verdict
# Settings (environment): see the block below; every path can be changed.
set -uo pipefail
export LC_ALL=C TZ=UTC

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${ORACLE_WORK:-${HADES_REGRESS:-$HOME/hades-regress}/oracle}
KODI_GIT=${ORACLE_KODI_GIT:?set ORACLE_KODI_GIT to the Kodi repository}
KODI_REF=${ORACLE_KODI_REF:-HEAD}
LE_GIT=${ORACLE_LE_GIT:-$(cd "$HERE/../../.." && pwd)}
LE_REF=${ORACLE_LE_REF:-HEAD}
LP_TARBALL_DIRS=${ORACLE_LP_TARBALL_DIRS:?set ORACLE_LP_TARBALL_DIRS to the folders (separated by spaces) that may hold the libplacebo tarball of the recipe}
# FFmpeg 9.0.2 with the DV patches of the image (only the HEVC decoder and Matroska): the RPU scan
FF=${ORACLE_FFMPEG:?set ORACLE_FFMPEG to the FFmpeg prefix with the DV patches}
# FFmpeg 9.0.2 for the host bridge (has the dovi_split filter that tools/dvbridge/dvbridge_fel.c needs): ref_dump and
# fidbench; the stage dump checks that the RPU it parses for each picture is the one of the scan
FFB=${ORACLE_FFMPEG_BENCH:?set ORACLE_FFMPEG_BENCH to the host FFmpeg prefix of the bridge}
GLW=${ORACLE_GLW:?set ORACLE_GLW to the folder with the GL headers (inc/) and mkshim.py}
VASTUB=${ORACLE_VASTUB:?set ORACLE_VASTUB to the folder with the stub of va/va.h}
FIDTOOLS=${ORACLE_FIDTOOLS:?set ORACLE_FIDTOOLS to the folder of ref_dump.c and the layer tools}
FIDBENCH_C=${ORACLE_FIDBENCH:?set ORACLE_FIDBENCH to fidbench.c}
LE_BUILD=${ORACLE_LE_BUILD:?set ORACLE_LE_BUILD to a build folder of LibreELEC}
SAMPLES=${ORACLE_SAMPLES:?set ORACLE_SAMPLES to the folder of the sample videos}
CLIPS=${ORACLE_CLIPS:-"tf tron"}
GPU_ENV="MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_ALWAYS_SOFTWARE=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json"
MEM=${ORACLE_MEM:-3G}
LPDIR=packages/addons/addon-depends/multimedia-tools-depends/libplacebo
B=$WORK/build
PY="python3 -I"

clip_file() {
  case $1 in
    tf) echo "$SAMPLES/Transformers DV FEL.mkv" ;;
    tron) echo "$SAMPLES/Tron Legacy DV FEL.mkv" ;;
    *) echo "$SAMPLES/$1.mkv" ;;
  esac
}
clip_sha() {
  case $1 in
    tf) echo 850fcc8e5716446be4079cc3e0c1f386ab065bc5d92163f93665c394206417d1 ;;
    tron) echo 21dd84a0c9c7809a322d58eb10a8d1445cbff0d0ac43713e49d0af62140643f6 ;;
    *) echo "${ORACLE_SHA_1:-unknown}" ;;
  esac
}
fail() { echo "FAIL: $*"; exit 1; }
item() { echo "ORACLE-ITEM $1: $2 | $3"; }
cap() {
  # one memory scope per heavy run
  if systemd-run --user --scope -q -p MemoryMax=$MEM -p MemorySwapMax=0 true 2> /dev/null; then
    systemd-run --user --scope -q -p MemoryMax=$MEM -p MemorySwapMax=0 "$@"
  else
    ( ulimit -v $((4 * 1024 * 1024)); "$@" )
  fi
}

# ------------------------------------------------------------------------------------------------- build
stage_build() {
  mkdir -p $B $WORK/logs
  : > $B/trees.txt
  # host python's glad and the Vulkan headers, copied once from LibreELEC's build folder (read only)
  if [ ! -d $B/py/glad ]; then
    local sp; sp=$(ls -d $LE_BUILD/toolchain/lib/python3*/site-packages 2> /dev/null | head -1)
    [ -n "$sp" ] && [ -d $sp/glad ] || fail "no glad in LibreELEC's toolchain ($LE_BUILD)"
    mkdir -p $B/py && cp -a $sp/glad $sp/glad2-*.dist-info $B/py/ || fail "copy glad"
  fi
  if [ ! -d $B/vkinc/vulkan ]; then
    local vk; vk=$(ls -d $LE_BUILD/build/vulkan-headers-*/include 2> /dev/null | head -1)
    [ -n "$vk" ] || fail "no Vulkan headers in $LE_BUILD"
    cp -a $vk $B/vkinc || fail "copy Vulkan headers"
  fi
  # libplacebo: the version, hash and patches of $LE_REF
  local ver sha tb="" d
  ver=$(git -C $LE_GIT show $LE_REF:$LPDIR/package.mk | sed -n 's/^PKG_VERSION="\(.*\)"$/\1/p')
  sha=$(git -C $LE_GIT show $LE_REF:$LPDIR/package.mk | sed -n 's/^PKG_SHA256="\(.*\)"$/\1/p')
  [ -n "$ver" ] && [ -n "$sha" ] || fail "libplacebo version or hash of $LE_REF"
  for d in $LP_TARBALL_DIRS; do [ -f $d/libplacebo-$ver.tar.gz ] && { tb=$d/libplacebo-$ver.tar.gz; break; }; done
  [ -n "$tb" ] || fail "no libplacebo-$ver.tar.gz in $LP_TARBALL_DIRS"
  [ "$(sha256sum < $tb | cut -c1-64)" = "$sha" ] || fail "sha256 of $tb is not $sha"
  echo "libplacebo $ver: $tb, sha256 $sha (PKG_SHA256 of $LE_REF)" | tee -a $B/trees.txt
  local D=$B/lp stamp
  stamp=$( { echo "$ver $sha"; for f in $(git -C $LE_GIT ls-tree --name-only $LE_REF $LPDIR/patches/); do
             git -C $LE_GIT show $LE_REF:$f; done; } | sha256sum | cut -c1-64)
  if [ -f $D/prefix/lib/libplacebo.a ] && [ "$(cat $D/stamp 2> /dev/null)" = "$stamp" ]; then
    echo "libplacebo: already built from the same tarball and patches (stamp $stamp)"
  else
  rm -rf $D && mkdir -p $D/src $D/patches
  local f
  for f in $(git -C $LE_GIT ls-tree --name-only $LE_REF $LPDIR/patches/); do
    git -C $LE_GIT show $LE_REF:$f > $D/patches/$(basename $f)
  done
  tar -xzf $tb --strip-components=1 -C $D/src || fail "unpack $tb"
  local p out
  for p in $(ls $D/patches/*.patch | sort); do
    out=$(patch -d $D/src -p1 -F0 --no-backup-if-mismatch < $p 2>&1) || { echo "$out" | tail -5; fail "$(basename $p)"; }
    echo "$out" | grep -q -i offset && echo "  $(basename $p): $(echo "$out" | grep -c -i offset) hunk(s) with an offset"
  done
  ( cd $D/patches && sha256sum *.patch ) > $D/patches.sha256
  ( export PYTHONPATH=$B/py CC=gcc CXX=g++ CFLAGS="-O2 -g0 -I$GLW/inc -I$B/vkinc" CXXFLAGS="-O2 -g0 -I$GLW/inc -I$B/vkinc"
    meson setup $D/build $D/src --prefix=$D/prefix --libdir=lib --buildtype=plain --auto-features=disabled \
      -Ddefault_library=static -Dvulkan=disabled -Dvk-proc-addr=disabled -Dd3d11=disabled -Dglslang=disabled \
      -Dshaderc=disabled -Dlcms=disabled -Ddovi=enabled -Dlibdovi=disabled -Ddemos=false -Dtests=false \
      -Dbench=false -Dopengl=enabled -Dgl-proc-addr=enabled > $D/setup.log 2>&1 < /dev/null &&
    nice -n 19 ninja -j8 -C $D/build install > $D/build.log 2>&1 < /dev/null ) || { tail -20 $D/setup.log $D/build.log; fail "libplacebo build"; }
  [ -f $D/prefix/lib/libplacebo.a ] || fail "libplacebo.a"
  echo "$stamp" > $D/stamp
  fi
  echo "libplacebo patches: $(ls $D/patches | wc -l) of $LE_REF ($(git -C $LE_GIT rev-parse $LE_REF)), applied with -F0" | tee -a $B/trees.txt
  # Kodi's bridge: git archive of $KODI_REF
  rm -rf $B/ktree && mkdir -p $B/ktree
  git -C $KODI_GIT archive $KODI_REF tools/dvbridge | tar -x -C $B/ktree || fail "archive $KODI_REF"
  echo "kodi $KODI_REF $(git -C $KODI_GIT rev-parse $KODI_REF) (tools/dvbridge, git archive)" | tee -a $B/trees.txt
  local S=$B/ktree/tools/dvbridge LP=$D/prefix
  # ref_dump, its decoder threads lowered to 8 for this shared machine
  sed 's/thread_count = 16;/thread_count = 8;/' $FIDTOOLS/ref_dump.c > $B/ref_dump.c
  CPATH=$VASTUB gcc -std=gnu11 -O2 -g -Wall -Wno-unused-function -I$FFB/include -I$S -o $B/ref_dump $B/ref_dump.c \
    -L$FFB/lib -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl > $B/ref_dump.warn 2>&1 || { cat $B/ref_dump.warn; fail "ref_dump"; }
  # fidbench with the bridge of $KODI_REF and this libplacebo
  local O=$B/fb.obj
  rm -rf $O && mkdir -p $O
  $PY $HERE/pack_sources.py $S $O || fail "pack_sources.h"
  local CF="-std=gnu11 -O1 -g -Wall -Wno-unused-function -I$LP/include -I$FFB/include -I$GLW/inc -I$B/vkinc -I$S -I$O"
  local bname
  for f in $S/*.c; do
    bname=$(basename $f .c)
    case $bname in dvbridge_fel|dvbridge_dmabuf) continue ;; esac
    CPATH=$VASTUB:$FIDTOOLS gcc $CF -c $f -o $O/$bname.o 2> $O/$bname.warn || { cat $O/$bname.warn; fail "$bname.c"; }
  done
  # glFinish() after each picture (this copy only), so that the software renderer holds one picture at a time
  # and FB_MMRT=1: the MMR mapping with libplacebo's table (dvbridge_render_use_mmr_table(), off by default)
  awk '{ if ($0 == "    ++drawn;") print "    glFinish();"; print;
         if (index($0, "dvbridge_render_set_step(g.renderer, envi(\"FB_STEP\", 0));"))
           print "  if (envi(\"FB_MMRT\", 0))\n    dvbridge_render_use_mmr_table(g.renderer, true);\n  printf(\"mmr table: %d\\n\", (int)dvbridge_render_mmr_table(g.renderer));" }' \
    $FIDBENCH_C > $O/fidbench.c
  [ "$(grep -c "^    glFinish();$" $O/fidbench.c)" = 1 ] || fail "glFinish() not added to fidbench.c"
  [ "$(grep -c "dvbridge_render_use_mmr_table(g.renderer, true);" $O/fidbench.c)" = 1 ] || fail "FB_MMRT not added"
  CPATH=$VASTUB:$FIDTOOLS gcc $CF -c $O/fidbench.c -o $O/fidbench.o 2> $O/fidbench.warn || { cat $O/fidbench.warn; fail "fidbench.c"; }
  nm -u $O/*.o | awk '$1=="U" && $2 ~ /^(__real_)?gl[A-Z]/ {sub(/^__real_/, "", $2); print $2}' | sort -u > $O/gl-symbols.txt
  python3 $GLW/mkshim.py $GLW/inc/GLES3/gl32.h $GLW/inc/GLES2/gl2ext.h < $O/gl-symbols.txt > $O/gles_shim.c || fail "GL shim"
  gcc $CF -c $O/gles_shim.c -o $O/gles_shim.o || fail "GL shim"
  g++ -o $B/fidbench $O/*.o $LP/lib/libplacebo.a -L$FFB/lib -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl \
    /usr/lib/x86_64-linux-gnu/libEGL.so.1 > $O/link.log 2>&1 || { cat $O/link.log; fail "fidbench link"; }
  echo "fidbench: $(sha256sum < $FIDBENCH_C | cut -c1-16) ($FIDBENCH_C), warnings $(cat $O/*.warn | grep -c 'warning:')" | tee -a $B/trees.txt
  # rpu_sets and lutcheck
  gcc -O2 -Wall -Wextra -o $B/rpu_sets $HERE/rpu_sets.c -I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil \
    -pthread -lm -latomic || fail "rpu_sets"
  if [ -f $HERE/lutcheck.c ]; then
    local inc="-I$D/build -I$D/src/src/include -I$D/build/src/include -I$D/src/src -I$D/build/src -I$FF/include"
    gcc -std=gnu11 -O1 -g -Wall -Wno-unused-function -c $HERE/lutcheck.c -o $B/lutcheck.o $inc 2> $B/lutcheck.warn &&
    gcc -o $B/lutcheck $B/lutcheck.o $LP/lib/libplacebo.a -L$FF/lib -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl -lstdc++ 2>> $B/lutcheck.warn ||
      { cat $B/lutcheck.warn; fail "lutcheck"; }
  fi
  # meta_edit (pictures made on purpose)
  gcc -O2 -Wall -Wextra -o $B/meta_edit $HERE/meta_edit.c -I$FFB/include || fail "meta_edit"
  echo "STAGE-BUILD-DONE"
}

# ------------------------------------------------------------------------------------------------- scan, select
stage_scan() {
  mkdir -p $WORK/sets
  local c f
  for c in $CLIPS; do
    f=$(clip_file $c)
    [ "$(sha256sum < "$f" | cut -c1-64)" = "$(clip_sha $c)" ] || fail "sha256 of $f"
    nice -n 19 $B/rpu_sets "$f" 2> $WORK/sets/$c-scan.err | gzip -1 > $WORK/sets/$c-rpu.jsonl.gz
    [ "${PIPESTATUS[0]}" = 0 ] || fail "rpu_sets $c"
    $PY $HERE/rpu_groups.py $WORK/sets/$c-rpu.jsonl.gz $WORK/sets/$c > /dev/null || fail "rpu_groups $c"
    echo "$c ($f): $(tail -1 $WORK/sets/$c-scan.err)"
    cat $WORK/sets/$c-summary.txt
  done
  echo "STAGE-SCAN-DONE"
}

stage_select() {
  local c
  for c in $CLIPS; do
    $PY $HERE/oracle_select.py $WORK/sets/$c ${ORACLE_SPREAD:-12} || fail "select $c"
  done
  echo "STAGE-SELECT-DONE"
}

# ------------------------------------------------------------------------------------------------- pictures
JOBS=${ORACLE_JOBS:-3}
job() { # <output file> <command...>: in the background, at most $JOBS at once
  local out=$1; shift
  while [ "$(jobs -r | wc -l)" -ge $JOBS ]; do wait -n; done
  ( "$@" > $out 2>&1 < /dev/null ) &
}
run_bench() { # <data dir> <out dir> <tag> <extra environment or -> <pictures...>
  local data=$1 out=$2 tag=$3 extra=$4
  shift 4
  [ "$extra" = - ] && extra=""
  mkdir -p $out
  cap env $GPU_ENV DV_MEM_LIMIT_MB=2500 FB_CCM=1 FB_SOURCE=1 FB_STEP=0 $extra nice -n 19 timeout 3000 \
    $B/fidbench $data $out $tag "$@" > $out/$tag.log 2> $out/$tag.err < /dev/null
  local rc=$?
  echo "fidbench $tag ${extra:-(EL, composer of CCM 001, source transport, step 0)}: exit $rc, $(grep -c 'composer ccm$' $out/$tag.log) pictures composed by the composer of CCM 001; $(tail -1 $out/$tag.log)"
  return $rc
}

# Batches of 64 pictures (ref_dump's limit): their layers and RPU, the RPU of each checked against the scan,
# drawn by fidbench (the sub-list also without the EL and with the MMR table), each compared with the oracle.
# The layers and outputs of a batch are deleted after it (ORACLE_KEEP=1 keeps them).
stage_pictures() {
  local c
  for c in $CLIPS; do
    local pics sub n i=0 bn=0 mism=0 total=0
    read -r -a pics < $WORK/sets/$c-pics.txt
    sub=" $(cat $WORK/sets/$c-sub.txt) "
    n=${#pics[@]}
    echo "$c: $n pictures, sub-list $(echo $sub | wc -w)"
    while [ $i -lt $n ]; do
      local batch=("${pics[@]:$i:64}") subb=() p D O
      i=$((i + 64)); bn=$((bn + 1))
      D=$WORK/data/$c-b$bn; O=$WORK/out/$c-b$bn
      rm -rf $D $O; mkdir -p $D $O
      local s0=$(date +%s)
      nice -n 19 $B/ref_dump "$(clip_file $c)" $D "${batch[@]}" > $O/dump.log 2>&1 < /dev/null
      if [ $? != 0 ] || ! grep -q '^DONE' $O/dump.log; then
        item $c-batch$bn FAILED "ref_dump: $(tail -1 $O/dump.log)"
        continue
      fi
      for p in "${batch[@]}"; do
        total=$((total + 1))
        if ! $PY $HERE/oracle_compare.py scanmatch $D/p$p $WORK/sets/$c-frames.txt $p >> $O/match.log 2>&1; then
          mism=$((mism + 1))
          item $c-p$p-rpu FAILED "$(tail -1 $O/match.log)"
        fi
        [[ "$sub" == *" $p "* ]] && subb+=($p)
      done
      run_bench $D $O o - "${batch[@]}"
      if [ ${#subb[@]} -gt 0 ]; then
        run_bench $D $O n FB_NOEL=1 "${subb[@]}"
        run_bench $D $O t FB_MMRT=1 "${subb[@]}"
      fi
      for p in "${batch[@]}"; do
        job $O/cmp-o-$p.txt $PY $HERE/oracle_compare.py picture $D/p$p $O o $c-p$p
      done
      for p in "${subb[@]}"; do
        job $O/cmp-n-$p.txt $PY $HERE/oracle_compare.py picture $D/p$p $O n $c-p$p-noel --noel
        job $O/cmp-t-$p.txt $PY $HERE/oracle_compare.py picture $D/p$p $O t $c-p$p-mmrtable
      done
      wait
      for p in "${batch[@]}"; do
        cat $O/cmp-o-$p.txt
        [ -f $O/cmp-n-$p.txt ] && cat $O/cmp-n-$p.txt $O/cmp-t-$p.txt
      done
      echo "$c batch $bn: ${#batch[@]} pictures (${batch[0]}..${batch[-1]}), sub-list ${#subb[@]}, $(( $(date +%s) - s0 )) s"
      if [ -z "${ORACLE_KEEP:-}" ]; then
        rm -rf $D
        rm -f $O/*.u16 $O/*.rgba $O/*.rgb16
      fi
    done
    item rpu-$c $([ $mism = 0 ] && [ $total = $n ] && echo PASSED || echo FAILED) \
      "$total of $n pictures dumped, the RPU parsed for $((total - mism)) of them is the scan's (FFmpeg of the image)"
  done
  echo "STAGE-PICTURES-DONE"
}

# ------------------------------------------------------------------------------------------------- luts
stage_luts() {
  local c m
  for c in $CLIPS; do
    $PY $HERE/oracle_luts.py $B/lutcheck $WORK/sets/$c-sets.jsonl luts-$c
  done
  # the same comparison sees each kind of fault (the first 50 RPUs of the first clip)
  c=${CLIPS%% *}
  for m in top-first poly-shift nlq-nosign nlq-shift nlq-noclamp; do
    $PY $HERE/oracle_luts.py $B/lutcheck $WORK/sets/$c-sets.jsonl luts-proof-$m --limit 50 --mutate $m --expect differ
  done
  echo "STAGE-LUTS-DONE"
}

# ------------------------------------------------------------------------------------------------- mutate, negative
# Real pictures (the first picture of each mapping shape of each clip) and pictures made on purpose from the first
# picture of the first clip (oracle_synth.py), kept in $WORK/mdata. Every mutation of the oracle must make the
# comparison with fidbench report a difference on at least one of them; each synthetic case must compare equal
# unmutated and must detect the mutations it was made for.
stage_mutate() {
  local M=$WORK/mdata c p base=""
  rm -rf $M; mkdir -p $M/real $M/out
  local sets=()
  for c in $CLIPS; do
    local shp
    shp=$($PY - $WORK/sets/$c <<'PY'
import json, sys
first, shapes = {}, {}
for line in open(sys.argv[1] + '-frames.txt'):
    f, k = line.split()
    first.setdefault(k, int(f))
sys.path.insert(0, '.')
for line in open(sys.argv[1] + '-sets.jsonl'):
    s = json.loads(line)
    parts = []
    for c in s['map']['curves']:
        parts.append(' '.join(map(str, c['pivots'])) + '|' + ','.join(
            ('mmr%d' % c['mmr_order'][k]) if c['mapping_idc'][k] else ('poly%d' % c['poly_order'][k])
            for k in range(c['num_pivots'] - 1)))
    shp = ' '.join(parts)
    shapes[shp] = min(shapes.get(shp, first[s['key']]), first[s['key']])
print(' '.join(str(f) for f in sorted(set(shapes.values()))[:8]))
PY
)
    mkdir -p $M/real/$c
    nice -n 19 $B/ref_dump "$(clip_file $c)" $M/real/$c $shp > $M/real/$c.dump.log 2>&1 < /dev/null || fail "ref_dump $c"
    run_bench $M/real/$c $M/out/real-$c o - $shp || fail "fidbench $c"
    for p in $shp; do sets+=("real-$c-p$p $M/real/$c/p$p $M/out/real-$c"); done
    # the base of the pictures made on purpose: the first picture of the first clip
    [ -z "$base" ] && base=$M/real/$c/p${shp%% *}
  done
  # pictures made on purpose
  $PY $HERE/oracle_synth.py $B/meta_edit $base $M/synth > $M/synth.txt || fail "oracle_synth"
  local case dir want pn
  while read -r case dir want; do
    pn=$(basename $dir); pn=${pn#p}
    run_bench $(dirname $dir) $M/out/synth-$case o - $pn
    $PY $HERE/oracle_compare.py picture $dir $M/out/synth-$case o synth-$case
    sets+=("synth-$case $dir $M/out/synth-$case")
  done < $M/synth.txt
  # the negative control: the identity without a residual (the EL picture is there)
  dir=$(awk '$1 == "identity" {print $2}' $M/synth.txt)
  $PY $HERE/oracle_compare.py formula $dir negative-identity-formula
  # every mutation on every set
  local m s name d o found
  for m in $($PY $HERE/ccm001_oracle.py list | awk '$1 == "mutation" {sub(":", "", $2); print $2}'); do
    for s in "${sets[@]}"; do
      read -r name d o <<< "$s"
      job $M/out/mut-$m-$name.txt $PY $HERE/oracle_compare.py picture $d $o o mut-$m-$name --mutate $m --expect differ
    done
    wait
    found=$(grep -l '^ORACLE-ITEM .*: PASSED' $M/out/mut-$m-*.txt 2> /dev/null | sed "s|.*/mut-$m-||; s|\.txt$||" | tr '\n' ' ')
    item mutation-$m $([ -n "$found" ] && echo PASSED || echo FAILED) \
      "$($PY $HERE/ccm001_oracle.py list | awk -v m="$m" '$1 == "mutation" && $2 == m":" {$1 = $2 = ""; print substr($0, 3)}'); detected on: ${found:-nothing}"
  done
  # each synthetic case detects what it was made for
  while read -r case dir want; do
    [ "$want" = - ] && continue
    for m in ${want//,/ }; do
      if grep -q '^ORACLE-ITEM .*: PASSED' $M/out/mut-$m-synth-$case.txt; then
        item synth-$case-sees-$m PASSED "$(grep -o 'y: max [0-9]*, [0-9]* of [0-9]* compared differ\|cb: max [0-9]*, [0-9]* of [0-9]* compared differ\|cr: max [0-9]*, [0-9]* of [0-9]* compared differ' $M/out/mut-$m-synth-$case.txt | tr '\n' ' ')"
      else
        item synth-$case-sees-$m FAILED "the comparison did not see mutation $m on this picture"
      fi
    done
  done < $M/synth.txt
  # the readings with an alternative: the real pictures decide between them
  for r in ds-axes=i-is-row ds-round=joint; do
    for s in "${sets[@]}"; do
      read -r name d o <<< "$s"
      job $M/out/rd-$r-$name.txt $PY $HERE/oracle_compare.py picture $d $o o rd-$r-$name --reading $r --expect differ
    done
    wait
    found=$(grep -l '^ORACLE-ITEM .*: PASSED' $M/out/rd-$r-*.txt 2> /dev/null | wc -l)
    item reading-$r $([ $found -gt 0 ] && echo PASSED || echo FAILED) \
      "the other reading differs from the bridge on $found of ${#sets[@]} pictures (the bridge composes as the default reading)"
  done
  echo "STAGE-MUTATE-DONE"
}

# ------------------------------------------------------------------------------------------------- lpmut (optional)
# A fault put on purpose into a copy of libplacebo (5.4.2.2 with <= in pivot_index() of src/dovi_ccm.c), built into a
# prefix of its own: the comparisons must see it (picture "pivots" made on purpose, and its tables).
stage_lpmut() {
  local D=$B/lp-mut M=$WORK/mdata
  [ -f $M/synth.txt ] || fail "mutate first"
  rm -rf $D; mkdir -p $D
  cp -a $B/lp/src $D/src
  grep -c 'if (s < cv->pivots\[idx + 1\])' $D/src/src/dovi_ccm.c | grep -qx 1 || fail "pivot_index() line not found"
  sed -i 's/if (s < cv->pivots\[idx + 1\])/if (s <= cv->pivots[idx + 1])/' $D/src/src/dovi_ccm.c
  ( export PYTHONPATH=$B/py CC=gcc CXX=g++ CFLAGS="-O2 -g0 -I$GLW/inc -I$B/vkinc" CXXFLAGS="-O2 -g0 -I$GLW/inc -I$B/vkinc"
    meson setup $D/build $D/src --prefix=$D/prefix --libdir=lib --buildtype=plain --auto-features=disabled \
      -Ddefault_library=static -Dvulkan=disabled -Dvk-proc-addr=disabled -Dd3d11=disabled -Dglslang=disabled \
      -Dshaderc=disabled -Dlcms=disabled -Ddovi=enabled -Dlibdovi=disabled -Ddemos=false -Dtests=false \
      -Dbench=false -Dopengl=enabled -Dgl-proc-addr=enabled > $D/setup.log 2>&1 < /dev/null &&
    nice -n 19 ninja -j8 -C $D/build install > $D/build.log 2>&1 < /dev/null ) || fail "libplacebo (mutated) build"
  local O=$B/fb.obj
  g++ -o $B/fidbench-mut $O/*.o $D/prefix/lib/libplacebo.a -L$FFB/lib -lavformat -lavcodec -lavutil -lm -latomic \
    -pthread -ldl /usr/lib/x86_64-linux-gnu/libEGL.so.1 || fail "fidbench-mut link"
  gcc -o $B/lutcheck-mut $B/lutcheck.o $D/prefix/lib/libplacebo.a -L$FF/lib -lavformat -lavcodec -lavutil -lm -latomic \
    -pthread -ldl -lstdc++ || fail "lutcheck-mut link"
  local dir pn
  dir=$(awk '$1 == "pivots" {print $2}' $M/synth.txt); pn=$(basename $dir); pn=${pn#p}
  mkdir -p $M/out/lpmut
  cap env $GPU_ENV DV_MEM_LIMIT_MB=2500 FB_CCM=1 FB_SOURCE=1 FB_STEP=0 nice -n 19 timeout 3000 \
    $B/fidbench-mut $(dirname $dir) $M/out/lpmut o $pn > $M/out/lpmut/o.log 2>&1 < /dev/null
  $PY $HERE/oracle_compare.py picture $dir $M/out/lpmut o lpmut-picture-pivots --expect differ
  $PY $HERE/oracle_compare.py metaset $dir > $M/out/lpmut/set.jsonl
  $PY $HERE/oracle_luts.py $B/lutcheck-mut $M/out/lpmut/set.jsonl lpmut-luts-pivots --expect differ
  $PY $HERE/oracle_luts.py $B/lutcheck $M/out/lpmut/set.jsonl luts-synth-pivots
  echo "STAGE-LPMUT-DONE"
}

# ------------------------------------------------------------------------------------------------- selftests, verdict
stage_selftest() {
  local out
  out=$($PY $HERE/ccm001_oracle.py selftest --sets $WORK/sets/*-shapes.jsonl 2>&1 | tail -1)
  item oracle-selftest $(echo "$out" | grep -q ' 0 failed$' && echo PASSED || echo FAILED) "$out (scalar and vectorised forms, hand-computed values, identity)"
  out=$($PY $HERE/oracle_verdict.py --selftest 2>&1 | tail -1)
  item verdict-selftest $(echo "$out" | grep -q ', 0 wrong$' && echo PASSED || echo FAILED) "$out"
  echo "STAGE-SELFTEST-DONE"
}

STAGES_ALL="build scan select selftest luts pictures mutate lpmut"
case "${1:-}" in
  build) stage_build ;;
  scan) stage_scan ;;
  select) stage_select ;;
  selftest) stage_selftest ;;
  pictures) stage_pictures ;;
  luts) stage_luts ;;
  mutate) stage_mutate ;;
  lpmut) stage_lpmut ;;
  verdict) $PY $HERE/oracle_verdict.py ${2:-$WORK/logs/run.log} $STAGES_ALL ;;
  all)
    mkdir -p $WORK/logs
    LOG=$WORK/logs/run.log
    : > $LOG
    echo "run-oracle.sh all, $(date -u +%FT%TZ), work $WORK" | tee -a $LOG
    for s in $STAGES_ALL; do
      echo "ORACLE-STAGE $s START" | tee -a $LOG
      bash $0 $s 2>&1 | tee -a $LOG
      [ "${PIPESTATUS[0]}" = 0 ] && echo "ORACLE-STAGE $s END" | tee -a $LOG
    done
    $PY $HERE/oracle_verdict.py $LOG $STAGES_ALL | tee $WORK/logs/verdict.txt
    exit "${PIPESTATUS[0]}"
    ;;
  *) echo "usage: run-oracle.sh build|scan|select|selftest|luts|pictures|mutate|lpmut|verdict|all"; exit 2 ;;
esac
