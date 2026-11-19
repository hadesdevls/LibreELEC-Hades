#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-regress.sh: the host regression of tools/regress, each group once: tools/regress/<group>/run-<group>.sh all,
# several groups side by side, each one in a memory scope of its own, at the lowest priority.
# usage: run-regress.sh [<group>...]       (default: every group of REGRESS_GROUPS below, the longest first)
#        run-regress.sh summary             (the summary of the last run)
# Settings (environment), besides those of lib/regress.sh that the groups read:
#   REGRESS_LANES   how many groups run at once (default 3)
#   REGRESS_MEM     the memory scope of each group (default 5G): systemd-run --user --scope with MemoryMax and no
#                   swap. Without a user systemd the groups run without that cap, and the summary says so.
# Output: $REGRESS_WORK/<group>/logs/run.log and verdict.txt for each group; $REGRESS_WORK/summary.txt, one line per
# group (verdict, items, seconds, peak memory of its scope), and "REGRESS-ALL PASSED|FAILED", which is PASSED only
# when every group passed. Exit 0 only then.
set -uo pipefail
export LC_ALL=C TZ=UTC
HERE=$(cd "$(dirname "$0")" && pwd)
: "${REGRESS_WORK:?set REGRESS_WORK to a work folder}"
# the groups, the longest first (measured: see README.md)
REGRESS_GROUPS=${REGRESS_GROUPS:-"dvbridge-render dvbridge-meta fel kodi-host ffmpeg boot playerinfo nvkms"}
LANES=${REGRESS_LANES:-3}
MEM=${REGRESS_MEM:-5G}
S=$REGRESS_WORK/summary.txt

summary() {
  local g v
  for g in $GROUPS_RUN; do cat "$REGRESS_WORK/$g/logs/lane.txt" 2> /dev/null || echo "$g: no result"; done
  v=PASSED
  for g in $GROUPS_RUN; do grep -q '^REGRESS-VERDICT PASSED$' "$REGRESS_WORK/$g/logs/verdict.txt" 2> /dev/null || v=FAILED; done
  echo "REGRESS-ALL $v ($(echo $GROUPS_RUN | wc -w) groups)"
}

if [ "${1:-}" = summary ]; then GROUPS_RUN=${REGRESS_LAST_GROUPS:-$REGRESS_GROUPS}; cat "$S" 2> /dev/null || summary; exit 0; fi
GROUPS_RUN=${*:-$REGRESS_GROUPS}
# a group's runner: <folder>/run-<group>.sh (a folder may hold the runners of several groups)
runner() { ls "$HERE"/*/run-"$1".sh 2> /dev/null | head -n 1; }
for g in $GROUPS_RUN; do
  [ -n "$(runner "$g")" ] || { echo "no group $g (no $HERE/*/run-$g.sh)"; exit 2; }
done
mkdir -p "$REGRESS_WORK"
CAP=1
systemd-run --user --scope -q -p MemoryMax="$MEM" -p MemorySwapMax=0 true 2> /dev/null || CAP=0
[ $CAP = 1 ] || echo "NOTE: no user systemd: the groups run without a memory cap"
echo "run-regress.sh: $(echo $GROUPS_RUN | wc -w) groups, $LANES at once, memory scope $([ $CAP = 1 ] && echo "$MEM each" || echo none), $(date -u +%FT%TZ)"

lane() { # <group>: its runner in its scope; then its line
  local g=$1 t0 rc
  t0=$(date +%s)
  mkdir -p "$REGRESS_WORK/$g/logs"
  if [ $CAP = 1 ]; then
    systemd-run --user --scope -q -p MemoryMax="$MEM" -p MemorySwapMax=0 \
      nice -n 19 bash "$HERE/lib/lane.sh" "$(runner "$g")" "$REGRESS_WORK/$g/logs" < /dev/null
  else
    nice -n 19 bash "$HERE/lib/lane.sh" "$(runner "$g")" "$REGRESS_WORK/$g/logs" < /dev/null
  fi
  rc=$?
  echo "$g: $(grep -m1 '^REGRESS-VERDICT' "$REGRESS_WORK/$g/logs/verdict.txt" 2> /dev/null || echo 'no verdict'); $(grep -m1 '^REGRESS-DONE' "$REGRESS_WORK/$g/logs/verdict.txt" 2> /dev/null | cut -c14-); exit $rc after $(( $(date +%s) - t0 )) s; peak memory $(cat "$REGRESS_WORK/$g/logs/memory-peak.txt" 2> /dev/null || echo 'not measured')" \
    > "$REGRESS_WORK/$g/logs/lane.txt"
  cat "$REGRESS_WORK/$g/logs/lane.txt"
}
for g in $GROUPS_RUN; do
  while [ "$(jobs -r | wc -l)" -ge "$LANES" ]; do wait -n; done
  echo "group $g: started $(date -u +%T)"
  lane "$g" &
done
wait
summary | tee "$S"
grep -q '^REGRESS-ALL PASSED' "$S"
