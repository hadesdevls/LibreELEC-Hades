#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-bdj.sh: host tests of Hades' patches to the Java side of libbluray (BD-J), against the jar of the recipe at
# REGRESS_LE_REF: libbluray's archive (checked against PKG_SHA256 of packages/multimedia/libbluray) with every patch
# of the recipe applied with -F0, built as packages/multimedia/libbluray-bdj builds it (meson, -Dbdj_jar=enabled,
# the JDK 8 of the build host, ant), and the test compiled and run with that JDK:
#   havi    HaviBestTest.java: HGraphicsDevice, HVideoDevice (of a 3D player: the 3D mode of the template chosen) and
#           HBackgroundDevice give the configuration that matches a template
#           best, the first on a tie, and PREFERRED and PREFERRED_NOT count (libbluray-9107). And the other way: the
#           jar without the patch that does it (found by its content, not by its number) must fail the test.
# usage: run-bdj.sh all | verdict [<log>] | <stage>...       (settings: lib/regress.sh)
# Needs REGRESS_SOURCES (libbluray/libbluray-<version>.tar.xz) and /usr/lib/jvm/java-8-openjdk-amd64 with ant.
# Writes only under $REGRESS_WORK/bdj.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init bdj
P=packages/multimedia/libbluray
J8=/usr/lib/jvm/java-8-openjdk-amd64
B=$RG_W/b

# jartree <dest> <ERE of patch names left out, or ^$>: <dest>, libbluray's source with the patches of the recipe but
# those whose names match, its jar built in <dest>.build; a "JARTREE" line says what was applied and left out
jartree() {
  local D=$1 skip=$2 ver sha tb T p n=0 out=""
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "BDJ: no version or hash of $P at $REGRESS_LE_REF"; return 1; }
  tb=$(rg_source libbluray "libbluray-$ver.tar.xz") || { echo "BDJ: no libbluray-$ver.tar.xz in REGRESS_SOURCES"; return 1; }
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "BDJ: sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  [ -x $J8/bin/javac ] && command -v ant > /dev/null || { echo "BDJ: no JDK 8 at $J8 or no ant"; return 1; }
  T=$D.le
  rg_le_tree "$T" $P/patches || { echo "BDJ: the patches of $REGRESS_LE_REF"; return 1; }
  rg_fresh "$D"
  tar -xJf "$tb" --strip-components=1 -C "$D" || { echo "BDJ: unpack $tb"; return 1; }
  for p in $(ls "$T/$P/patches"/*.patch | sort); do
    if basename "$p" | grep -E -q -- "$skip"; then out="$out $(basename "$p")"; continue; fi
    patch -d "$D" -p1 -F0 -s --no-backup-if-mismatch < "$p" || { echo "BDJ: PATCH FAILED $(basename "$p")"; return 1; }
    n=$((n + 1))
  done
  rg_rm "$T"
  echo "JARTREE libbluray $ver, $n patches of $REGRESS_LE_REF applied with -F0; left out:${out:- none}"
  rg_fresh "$D.build"
  JAVA_HOME=$J8 PATH=$J8/bin:$PATH meson setup "$D.build" "$D" -Dbdj_jar=enabled -Djdk_home=$J8 \
    -Dfreetype=disabled -Dlibxml2=disabled -Dfontconfig=disabled -Dhades_bdj_host=disabled -Denable_tools=false \
    -Denable_docs=false > "$D.setup.log" 2>&1 || { tail -n 20 "$D.setup.log"; return 1; }
  JAVA_HOME=$J8 PATH=$J8/bin:$PATH ninja -C "$D.build" -j "${REGRESS_JOBS:-8}" > "$D.ninja.log" 2>&1 ||
    { tail -n 30 "$D.ninja.log"; return 1; }
  ls "$D.build"/src/libbluray/bdj/libbluray-j2se-*.jar
}
# havi_run <tree> <log>: HaviBestTest against the jar of <tree>
havi_run() {
  local jar C
  jar=$(ls "$1.build"/src/libbluray/bdj/libbluray-j2se-*.jar | head -n 1)
  C=$1.classes
  rg_fresh "$C"
  $J8/bin/javac -nowarn -cp "$jar" -d "$C" "$HERE/HaviBestTest.java" || return 1
  rg_log "$2" timeout 120 $J8/bin/java -Djava.awt.headless=true -cp "$C:$jar" org.havi.ui.HaviBestTest
}

stage_havi() {
  local L v the n
  L=$RG_W/logs/tree.log
  if ! rg_log "$L" jartree "$B/all" '^$'; then
    rg_item havi FAILED "no jar with the patches: $(rg_last "$L")"
    rg_item havi.without FAILED "no jar with the patches"
    return 1
  fi
  echo "$(grep -a '^JARTREE' "$L")"
  L=$RG_W/logs/havi.log
  if havi_run "$B/all" "$L" > "$RG_W/logs/havi.build.log" 2>&1; then
    v=$(rg_pass "$L" '^ALL PASSED$' '^FAIL')
    rg_item havi "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 '^HAVI-BEST' "$L")"
  else
    rg_item havi FAILED "$(grep -a -m1 -E 'error|^FAIL' "$RG_W/logs/havi.build.log" "$L" 2> /dev/null | cut -c1-180)"
  fi
  # the other way: the patch that scores the preferences, found by its content, left out (it has to fail 9 checks)
  rg_le_tree "$RG_W/le-list" $P/patches
  the=$(grep -l -F 'a preference is a score, never a refusal' "$RG_W/le-list/$P/patches"/*.patch | head -n 1)
  rg_rm "$RG_W/le-list"
  if [ -z "$the" ]; then rg_item havi.without FAILED "no patch of $P scores the HAVi preferences"; return 1; fi
  the=$(basename "$the")
  L=$RG_W/logs/tree-without.log
  if ! rg_log "$L" jartree "$B/without" "^$(printf '%s' "$the" | sed 's/[.]/[.]/g')\$"; then
    rg_item havi.without FAILED "no jar without $the: $(rg_last "$L")"; return 1
  fi
  L=$RG_W/logs/havi-without.log
  havi_run "$B/without" "$L" > "$RG_W/logs/havi-without.build.log" 2>&1
  n=$(grep -a -c '^FAIL' "$L")
  [ "$n" = 9 ] && rg_item havi.without PASSED "without $the the test fails, as it must: $n checks" ||
    rg_item havi.without FAILED "without $the: $n failed checks, not 9"
  rg_rm "$B"
}

rg_main "havi" "$@"
