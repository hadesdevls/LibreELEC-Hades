#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-patches.sh: what the patches of the tree at REGRESS_LE_REF say of themselves:
#   identity  every patch of Hades (packages/ and projects/) is signed by Hades' own identity, Hades
#             <15710513+hadesdevls@users.noreply.github.com> (identity_check.py): a patch signed by a misspelt account
#             (hadesdvls, three kernel patches of 1.2 before review) or by the owner's account would be published so.
#             And the other way: a copy of one patch of Hades with its From: line misspelt must be found.
# usage: run-patches.sh all | verdict [<log>] | <stage>...       (settings: lib/regress.sh)
# Writes only under $REGRESS_WORK/patches.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init patches

stage_identity() {
  local T=$RG_W/tree M=$RG_W/mutant L one
  rg_le_tree "$T" packages projects || { rg_item identity FAILED "no tree of $REGRESS_LE_REF"; return 1; }
  L=$RG_W/logs/identity.log
  rg_log "$L" $RG_PY "$HERE/identity_check.py" "$T"
  if [ "$(rg_exit "$L")" = 0 ]; then
    rg_item identity PASSED "$(grep -a -m1 -E '^[0-9]+ patches' "$L")"
  else
    rg_item identity FAILED "$(grep -a -m1 -E '^[0-9]+ patches' "$L"); $(grep -a -m1 '^WRONG' "$L" | cut -c1-200)"
  fi
  # the other way: one patch of Hades, its From: line misspelt as it was found in 1.2 before review
  one=$(grep -r -l -m1 -F 'From: Hades <15710513+hadesdevls@users.noreply.github.com>' --include='*.patch' "$T" | sort | head -n 1)
  if [ -z "$one" ]; then
    rg_item identity.mutant FAILED "no patch of Hades in the tree to make a mutant of"
  else
    rg_fresh "$M"
    sed '0,/^From: Hades <15710513+hadesdevls@/s//From: Hades <15710513+hadesdvls@/' "$one" > "$M/mutant.patch"
    L=$RG_W/logs/identity-mutant.log
    rg_log "$L" $RG_PY "$HERE/identity_check.py" "$M"
    if [ "$(rg_exit "$L")" != 0 ] && grep -a -q '^WRONG mutant.patch: From: Hades <15710513+hadesdvls@' "$L"; then
      rg_item identity.mutant PASSED "a copy of ${one#$T/} signed hadesdvls is found: $(grep -a -m1 -E '^[0-9]+ patches' "$L")"
    else
      rg_item identity.mutant FAILED "a copy of ${one#$T/} signed hadesdvls is not found: $(rg_last "$L")"
    fi
  fi
  rg_rm "$T" "$M"
}

rg_main "identity" "$@"
