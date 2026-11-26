#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-nvkms.sh: the host tests of Hades' patches to NVIDIA's open kernel modules (packages/graphics/
# nvidia-open-kernel-modules), against the source of the recipe at REGRESS_LE_REF (its archive checked against
# PKG_SHA256) with every patch of the recipe applied with -F0, as scripts/unpack applies them:
#   vrrrm   nvHadesVrrHeadForRm() (vrr_rm_test.c, nvkms-hades.h): whether NVKMS tells RM that a head can extend its
#           vblank. And the other way: on the same source with the patches before the one that adds that function
#           (found by its content, not by its number; the later ones may build on it), the test must not build.
#   frlkeep nvHadesFrlKeepDecide() and nvHadesFrlKeepWhyNot() (frl_keep_test.c, nvkms-hades.h): whether an HDMI head
#           that runs over FRL stays on FRL for a mode that TMDS would carry (NV_HDMI_FRL_KEEP). The other way as for
#           vrrrm: without the patch that adds nvHadesFrlKeepDecide(), found by its content, the test must not build.
#   contenttype nvHadesAviContent(), nvHadesContentTypeOf() and nvHadesContentTypeOverride() (content_type_test.c,
#           nvkms-hades.h): the content type of the AVI InfoFrame ("content type" of nvidia-drm), with the AVI
#           InfoFrames built by the timing code of the tree for the CTA modes of an LG C5, in the open modules and
#           in nvidia-legacy-open (its recipe and archive, the same way). The other way as for vrrrm: without the
#           patch that adds nvHadesAviContent(), found by its content, the test must not build.
#   stereocta nvHadesHalfLayoutVsif(), nvHadesEdidRead3D(), nvHadesEdidDeclares3D() and nvHadesStereoAviCtaVic()
#           (stereo_cta_vic_test.c, nvkms-hades.h): the CTA VIC in the AVI InfoFrame next to a client's half 3D
#           layout in a mode that NVKMS names by its HDMI_VIC, to a display whose EDID declares it, as Linux reads the
#           EDID (whatever its HDMI version); the vendor and AVI InfoFrames byte for byte, the latter built by the
#           timing code of the tree; on two real EDIDs (inputs-nvkms.tsv) and synthetic ones, in the open modules and
#           in nvidia-legacy-open (its recipe and archive, the same way). Each rule changed by a mutant
#           (stereo_mutants.py) must fail with its count of failed checks, in both; and the other way as for vrrrm.
# usage: run-nvkms.sh all | verdict [<log>] | <stage>...       (settings: lib/regress.sh)
# Needs REGRESS_SOURCES (nvidia-open-kernel-modules/nvidia-open-kernel-modules-<version>.tar.gz, and for contenttype
# nvidia-legacy-open/nvidia-legacy-open-<version>.tar.gz), and for stereocta REGRESS_INPUTS. Writes only under
# $REGRESS_WORK/nvkms.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init nvkms
P=packages/graphics/nvidia-open-kernel-modules
# (the package of P, as its sources are named; contenttype sets both for nvidia-legacy-open)
PKG=nvidia-open-kernel-modules
NV=$RG_W/nv

# nvtree <dest> <ERE of patch names left out, or ^$> [stop]: <dest>, the source of the recipe with its patches but
# those whose names match; with "stop", none after the first that is left out either (a later patch may change the
# same lines); an "NVTREE" line says what was applied and left out
nvtree() {
  local D=$1 skip=$2 stop=${3:-} ver sha tb T p n=0 out="" cut=""
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "NV: no version or hash of $P at $REGRESS_LE_REF"; return 1; }
  tb=$(rg_source $PKG "$PKG-$ver.tar.gz") ||
    { echo "NV: no $PKG-$ver.tar.gz in REGRESS_SOURCES"; return 1; }
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "NV: sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  T=$D.le
  rg_le_tree "$T" $P/patches || { echo "NV: the patches of $REGRESS_LE_REF"; return 1; }
  rg_fresh "$D"
  tar -xzf "$tb" --strip-components=1 -C "$D" || { echo "NV: unpack $tb"; return 1; }
  for p in $(ls "$T/$P/patches"/*.patch | sort); do
    if [ -n "$cut" ]; then cut="$cut $(basename "$p")"; continue; fi
    if basename "$p" | grep -E -q -- "$skip"; then out="$out $(basename "$p")"; [ -z "$stop" ] || cut=" after it:"; continue; fi
    patch -d "$D" -p1 -F0 -s --no-backup-if-mismatch < "$p" || { echo "NV: PATCH FAILED $(basename "$p")"; return 1; }
    n=$((n + 1))
  done
  rg_rm "$T"
  echo "NVTREE $PKG $ver, $n patches of $REGRESS_LE_REF applied with -F0; left out:${out:- none}${cut}"
}
# build_rm <tree> <binary> [<test source>]: vrr_rm_test.c (or another test of nvkms-hades.h) against a tree, as its
# head says
build_rm() {
  gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I "$1/src/nvidia-modeset/include" \
    -I "$1/src/common/sdk/nvidia/inc" -I "$1/src/common/inc" -I "$1/kernel-open/common/inc" -o "$2" \
    "$HERE/${3:-vrr_rm_test.c}"
}

stage_vrrrm() {
  local L v the
  L=$RG_W/logs/tree.log
  if ! rg_log "$L" nvtree "$NV/all" '^$'; then
    rg_item vrr_rm FAILED "no source with the patches: $(rg_last "$L")"
    rg_item vrr_rm.without FAILED "no source with the patches"
    return 1
  fi
  echo "$(grep -a '^NVTREE' "$L")"
  L=$RG_W/logs/vrr_rm.log
  if rg_log "$RG_W/logs/vrr_rm.build.log" build_rm "$NV/all" "$RG_W/vrr_rm"; then
    rg_log "$L" "$RG_W/vrr_rm"
    v=$(rg_pass "$L" '^PASSED$')
    rg_item vrr_rm "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 -E '^[0-9]+ checks' "$L")"
  else
    rg_item vrr_rm FAILED "not built: $(grep -a -m1 'error' "$RG_W/logs/vrr_rm.build.log" | cut -c1-180)"
  fi
  # the other way: the patch that adds nvHadesVrrHeadForRm(), found by its content, left out
  rg_le_tree "$RG_W/le-list" $P/patches
  the=$(grep -l -F 'nvHadesVrrHeadForRm' "$RG_W/le-list/$P/patches"/*.patch | head -n 1)
  rg_rm "$RG_W/le-list"
  if [ -z "$the" ]; then rg_item vrr_rm.without FAILED "no patch of $P adds nvHadesVrrHeadForRm()"; return 1; fi
  the=$(basename "$the")
  L=$RG_W/logs/tree-without.log
  if ! rg_log "$L" nvtree "$NV/without" "^$(printf '%s' "$the" | sed 's/[.]/[.]/g')\$" stop; then
    rg_item vrr_rm.without FAILED "no source without $the: $(rg_last "$L")"; return 1
  fi
  if build_rm "$NV/without" "$RG_W/vrr_rm-without" > "$RG_W/logs/vrr_rm-without.build.log" 2>&1; then
    rg_item vrr_rm.without FAILED "it builds without $the"
  elif grep -a -q 'nvHadesVrrHeadForRm' "$RG_W/logs/vrr_rm-without.build.log"; then
    rg_item vrr_rm.without PASSED "it does not build without $the, as it must: $(grep -a -m1 'error' "$RG_W/logs/vrr_rm-without.build.log" | sed 's/.*error: //' | cut -c1-120)"
  else
    rg_item vrr_rm.without FAILED "it does not build without $the, for another reason: $(grep -a -m1 'error' "$RG_W/logs/vrr_rm-without.build.log" | cut -c1-160)"
  fi
  rg_rm "$NV/all" "$NV/without"
}

stage_frlkeep() {
  local L v the
  L=$RG_W/logs/tree-frlkeep.log
  if ! rg_log "$L" nvtree "$NV/all-frlkeep" '^$'; then
    rg_item frl_keep FAILED "no source with the patches: $(rg_last "$L")"
    rg_item frl_keep.without FAILED "no source with the patches"
    return 1
  fi
  echo "$(grep -a '^NVTREE' "$L")"
  L=$RG_W/logs/frl_keep.log
  if rg_log "$RG_W/logs/frl_keep.build.log" build_rm "$NV/all-frlkeep" "$RG_W/frl_keep" frl_keep_test.c; then
    rg_log "$L" "$RG_W/frl_keep"
    v=$(rg_pass "$L" '^PASSED$')
    rg_item frl_keep "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 -E '^[0-9]+ checks' "$L")"
  else
    rg_item frl_keep FAILED "not built: $(grep -a -m1 'error' "$RG_W/logs/frl_keep.build.log" | cut -c1-180)"
  fi
  # the other way: the patch that adds nvHadesFrlKeepDecide(), found by its content, left out
  rg_le_tree "$RG_W/le-list" $P/patches
  the=$(grep -l -F 'nvHadesFrlKeepDecide' "$RG_W/le-list/$P/patches"/*.patch | head -n 1)
  rg_rm "$RG_W/le-list"
  if [ -z "$the" ]; then rg_item frl_keep.without FAILED "no patch of $P adds nvHadesFrlKeepDecide()"; return 1; fi
  the=$(basename "$the")
  L=$RG_W/logs/tree-frlkeep-without.log
  if ! rg_log "$L" nvtree "$NV/without-frlkeep" "^$(printf '%s' "$the" | sed 's/[.]/[.]/g')\$" stop; then
    rg_item frl_keep.without FAILED "no source without $the: $(rg_last "$L")"; return 1
  fi
  if build_rm "$NV/without-frlkeep" "$RG_W/frl_keep-without" frl_keep_test.c \
    > "$RG_W/logs/frl_keep-without.build.log" 2>&1; then
    rg_item frl_keep.without FAILED "it builds without $the"
  elif grep -a -q 'nvHadesFrlKeep' "$RG_W/logs/frl_keep-without.build.log"; then
    rg_item frl_keep.without PASSED "it does not build without $the, as it must: $(grep -a -m1 'error' "$RG_W/logs/frl_keep-without.build.log" | sed 's/.*error: //' | cut -c1-120)"
  else
    rg_item frl_keep.without FAILED "it does not build without $the, for another reason: $(grep -a -m1 'error' "$RG_W/logs/frl_keep-without.build.log" | cut -c1-160)"
  fi
  rg_rm "$NV/all-frlkeep" "$NV/without-frlkeep"
}

# build_ct <tree> <binary>: content_type_test.c with the timing code of the tree (src/common/modeset/timing, NVIDIA's
# code as it is: compiled without -Werror, the test with it)
build_ct() {
  local T=$1 O=$2.obj f
  rg_fresh "$O"
  for f in "$T"/src/common/modeset/timing/*.c; do
    gcc -std=gnu11 -c -w -I "$T/src/common/inc" -I "$T/src/common/modeset/timing" -I "$T/src/common/sdk/nvidia/inc" \
      -I "$T/src/common/shared/inc" "$f" -o "$O/$(basename "$f" .c).o" || return 1
  done
  gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I "$T/src/nvidia-modeset/include" \
    -I "$T/src/common/sdk/nvidia/inc" -I "$T/src/common/inc" -I "$T/src/common/modeset/timing" \
    -I "$T/kernel-open/common/inc" -I "$T/src/nvidia-modeset/interface" -I "$T/src/common/unix/common/inc" \
    -o "$2" "$HERE/content_type_test.c" "$O"/*.o
}
# ct_one <package folder> <package> <item>: content_type_test.c on the source of that recipe with its patches, and
# without the patch that adds nvHadesAviContent()
ct_one() {
  local P=$1 PKG=$2 it=$3 L v the
  L=$RG_W/logs/tree-$it.log
  if ! rg_log "$L" nvtree "$NV/all-$it" '^$'; then
    rg_item $it FAILED "no source with the patches: $(rg_last "$L")"
    rg_item $it.without FAILED "no source with the patches"
    return 1
  fi
  echo "$(grep -a '^NVTREE' "$L")"
  L=$RG_W/logs/$it.log
  if rg_log "$RG_W/logs/$it.build.log" build_ct "$NV/all-$it" "$RG_W/$it"; then
    rg_log "$L" "$RG_W/$it"
    v=$(rg_pass "$L" '^PASSED$')
    rg_item $it "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 -E '^[0-9]+ checks' "$L")"
  else
    rg_item $it FAILED "not built: $(grep -a -m1 'error' "$RG_W/logs/$it.build.log" | cut -c1-180)"
  fi
  # the other way: the patch that adds nvHadesAviContent(), found by its content, left out
  rg_le_tree "$RG_W/le-list" $P/patches
  the=$(grep -l -F 'nvHadesAviContent' "$RG_W/le-list/$P/patches"/*.patch | head -n 1)
  rg_rm "$RG_W/le-list"
  if [ -z "$the" ]; then rg_item $it.without FAILED "no patch of $P adds nvHadesAviContent()"; return 1; fi
  the=$(basename "$the")
  L=$RG_W/logs/tree-$it-without.log
  if ! rg_log "$L" nvtree "$NV/without-$it" "^$(printf '%s' "$the" | sed 's/[.]/[.]/g')\$" stop; then
    rg_item $it.without FAILED "no source without $the: $(rg_last "$L")"; return 1
  fi
  if build_ct "$NV/without-$it" "$RG_W/$it-without" > "$RG_W/logs/$it-without.build.log" 2>&1; then
    rg_item $it.without FAILED "it builds without $the"
  elif grep -a -q 'nvHadesAviContent\|NV_HADES_CONTENT_TYPE' "$RG_W/logs/$it-without.build.log"; then
    rg_item $it.without PASSED "it does not build without $the, as it must: $(grep -a -m1 'error' "$RG_W/logs/$it-without.build.log" | sed 's/.*error: //' | cut -c1-120)"
  else
    rg_item $it.without FAILED "it does not build without $the, for another reason: $(grep -a -m1 'error' "$RG_W/logs/$it-without.build.log" | cut -c1-160)"
  fi
  rg_rm "$NV/all-$it" "$NV/without-$it" "$RG_W/$it.obj" "$RG_W/$it-without.obj"
}

stage_contenttype() {
  ct_one packages/graphics/nvidia-open-kernel-modules nvidia-open-kernel-modules content_type
  ct_one packages/graphics/nvidia-legacy-open nvidia-legacy-open content_type.legacy
}

# build_st <tree> <timing objects> <binary> [<folder of a mutated nvkms-hades.h>]: stereo_cta_vic_test.c with the
# timing code of the tree compiled once into <timing objects> as NVKMS compiles it (-DNVT_USE_NVKMS, which 580 needs
# for the order of the HDMI_VIC timings: prioritizeEdidHDMIExtTiming()), the header of the mutant first where one is
# given
build_st() {
  local T=$1 O=$2 f
  if [ ! -f "$O/.done" ]; then
    rg_fresh "$O"
    for f in "$T"/src/common/modeset/timing/*.c; do
      # (the include folders of src/nvidia-modeset/Makefile that the timing code reaches)
      gcc -std=gnu11 -c -w -DNVT_USE_NVKMS -I "$T/src/common/inc" -I "$T/src/common/modeset/timing" \
        -I "$T/src/common/sdk/nvidia/inc" -I "$T/src/common/sdk/nvidia/inc/hw" -I "$T/src/common/shared/inc" \
        -I "$T/src/common/unix/common/utils/interface" -I "$T/src/common/unix/common/inc" -I "$T/src/common/modeset" \
        -I "$T/src/nvidia-modeset/os-interface/include" -I "$T/src/nvidia-modeset/kapi/interface" \
        -I "$T/src/nvidia/arch/nvalloc/unix/include" -I "$T/src/nvidia-modeset/interface" \
        -I "$T/src/nvidia-modeset/include" "$f" -o "$O/$(basename "$f" .c).o" || return 1
    done
    touch "$O/.done"
  fi
  gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function ${4:+-I "$4"} -I "$T/src/nvidia-modeset/include" \
    -I "$T/src/common/sdk/nvidia/inc" -I "$T/src/common/inc" -I "$T/src/common/modeset/timing" \
    -I "$T/kernel-open/common/inc" -I "$T/src/nvidia-modeset/interface" -I "$T/src/common/unix/common/inc" \
    -o "$3" "$HERE/stereo_cta_vic_test.c" "$O"/*.o
}

# st_one <package folder> <package> <item>: stereo_cta_vic_test.c on the source of that recipe with its patches, its
# mutants, and the test without the patch that adds nvHadesStereoAviCtaVic()
st_one() {
  local P=$1 PKG=$2 it=$3 L v the IN=$RG_W/in-stereo O=$RG_W/st-obj-$3 M=$RG_W/mut-$3 name want
  L=$RG_W/logs/tree-$it.log
  if ! rg_log "$L" nvtree "$NV/all-$it" '^$'; then
    rg_item $it FAILED "no source with the patches: $(rg_last "$L")"
    rg_item $it.without FAILED "no source with the patches"
    rg_item $it.mutants FAILED "no source with the patches"
    return 1
  fi
  echo "$(grep -a '^NVTREE' "$L")"
  if ! rg_inputs "$HERE/inputs-nvkms.tsv" "$IN"; then
    rg_item $it FAILED "the inputs of inputs-nvkms.tsv"
  elif rg_log "$RG_W/logs/$it.build.log" build_st "$NV/all-$it" "$O" "$RG_W/$it"; then
    L=$RG_W/logs/$it.log
    rg_log "$L" "$RG_W/$it" "$IN"
    v=$(rg_pass "$L" '^PASSED$')
    rg_item $it "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 -E '^[0-9]+ checks' "$L")"
    # the other way: each rule of the patch changed in a copy of nvkms-hades.h, with its count of failed checks
    rg_fresh "$M"
    local good=0 bad=0 detail=""
    while read -r name want; do
      mkdir -p "$M/$name"
      cp "$NV/all-$it/src/nvidia-modeset/include/nvkms-hades.h" "$M/$name/"
      if ! $RG_PY "$HERE/stereo_mutants.py" "$name" "$M/$name/e.py" ||
         ! rg_edit "$M/$name/nvkms-hades.h" "$M/$name/e.py" > "$M/$name/edit.log" 2>&1; then
        bad=$((bad + 1)); detail="$detail $name: the mutant no longer applies;"; continue
      fi
      if ! build_st "$NV/all-$it" "$O" "$M/$name/t" "$M/$name" > "$M/$name/build.log" 2>&1; then
        bad=$((bad + 1)); detail="$detail $name: not built ($(grep -a -m1 'error' "$M/$name/build.log" | cut -c1-100));"
        continue
      fi
      rg_log "$M/$name/run.log" "$M/$name/t" "$IN"
      v=$(rg_fails "$M/$name/run.log" "$want")
      echo "mutant $it $name: $v"
      if [ "$(rg_say "$v")" = PASSED ]; then good=$((good + 1)); else bad=$((bad + 1)); detail="$detail $name: $(rg_rest "$v");"; fi
    done < <($RG_PY "$HERE/stereo_mutants.py" list)
    if [ $bad = 0 ] && [ $good -gt 0 ]; then
      rg_item $it.mutants PASSED "all $good mutants of the rules caught, each with its count of failed checks"
    else
      rg_item $it.mutants FAILED "$bad of $((good + bad)) mutants not as they must be:$detail"
    fi
  else
    rg_item $it FAILED "not built: $(grep -a -m1 'error' "$RG_W/logs/$it.build.log" | cut -c1-180)"
    rg_item $it.mutants FAILED "the test was not built"
  fi
  # the other way: the patch that adds nvHadesStereoAviCtaVic(), found by its content, left out
  rg_le_tree "$RG_W/le-list" $P/patches
  the=$(grep -l -F 'nvHadesStereoAviCtaVic' "$RG_W/le-list/$P/patches"/*.patch | head -n 1)
  rg_rm "$RG_W/le-list"
  if [ -z "$the" ]; then rg_item $it.without FAILED "no patch of $P adds nvHadesStereoAviCtaVic()"; return 1; fi
  the=$(basename "$the")
  L=$RG_W/logs/tree-$it-without.log
  if ! rg_log "$L" nvtree "$NV/without-$it" "^$(printf '%s' "$the" | sed 's/[.]/[.]/g')\$" stop; then
    rg_item $it.without FAILED "no source without $the: $(rg_last "$L")"; return 1
  fi
  if build_st "$NV/without-$it" "$RG_W/st-obj-$it-without" "$RG_W/$it-without" \
    > "$RG_W/logs/$it-without.build.log" 2>&1; then
    rg_item $it.without FAILED "it builds without $the"
  elif grep -a -q 'nvHadesStereoAviCtaVic\|nvHadesEdidRead3D\|nvHadesHalfLayoutVsif\|NvHadesEdid3D\|NV_HADES_HALF_' \
    "$RG_W/logs/$it-without.build.log"; then
    rg_item $it.without PASSED "it does not build without $the, as it must: $(grep -a -m1 'error' "$RG_W/logs/$it-without.build.log" | sed 's/.*error: //' | cut -c1-120)"
  else
    rg_item $it.without FAILED "it does not build without $the, for another reason: $(grep -a -m1 'error' "$RG_W/logs/$it-without.build.log" | cut -c1-160)"
  fi
  rg_rm "$NV/all-$it" "$NV/without-$it" "$O" "$RG_W/st-obj-$it-without" "$M"
}

stage_stereocta() {
  st_one packages/graphics/nvidia-open-kernel-modules nvidia-open-kernel-modules stereo_cta_vic
  st_one packages/graphics/nvidia-legacy-open nvidia-legacy-open stereo_cta_vic.legacy
  rg_rm "$RG_W/in-stereo"
}

rg_main "vrrrm frlkeep contenttype stereocta" "$@"
