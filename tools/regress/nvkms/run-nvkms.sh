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
# usage: run-nvkms.sh all | verdict [<log>] | <stage>...       (settings: lib/regress.sh)
# Needs REGRESS_SOURCES (nvidia-open-kernel-modules/nvidia-open-kernel-modules-<version>.tar.gz). Writes only under
# $REGRESS_WORK/nvkms.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init nvkms
P=packages/graphics/nvidia-open-kernel-modules
NV=$RG_W/nv

# nvtree <dest> <ERE of patch names left out, or ^$> [stop]: <dest>, the source of the recipe with its patches but
# those whose names match; with "stop", none after the first that is left out either (a later patch may change the
# same lines); an "NVTREE" line says what was applied and left out
nvtree() {
  local D=$1 skip=$2 stop=${3:-} ver sha tb T p n=0 out="" cut=""
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "NV: no version or hash of $P at $REGRESS_LE_REF"; return 1; }
  tb=$(rg_source nvidia-open-kernel-modules "nvidia-open-kernel-modules-$ver.tar.gz") ||
    { echo "NV: no nvidia-open-kernel-modules-$ver.tar.gz in REGRESS_SOURCES"; return 1; }
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
  echo "NVTREE nvidia-open-kernel-modules $ver, $n patches of $REGRESS_LE_REF applied with -F0; left out:${out:- none}${cut}"
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

rg_main "vrrrm frlkeep" "$@"
