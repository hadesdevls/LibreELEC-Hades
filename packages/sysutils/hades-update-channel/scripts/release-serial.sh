#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: the serial of a version of LibreELEC Hades in its update channel, a whole number that grows with the
# version: MAJOR * 1000000 + MINOR * 10000 + PATCH * 100 + (the number of a release candidate, 1 to 98, or
# 99 for the version itself). 1.0 RC8 = 1000008, 1.0 = 1000099, 1.0.1 = 1000199, 1.1 RC1 = 1010001,
# 1.1 = 1010099. The channel offers a version only when its serial is higher than the system's.
# The publication tool computes it the same way and compares it with the image's.
# Usage: release-serial.sh <BUILDER_VERSION>   (prints the serial; exit 1 for a version of another form)
set -u
v=${1:-}
if [[ ! $v =~ ^(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]?)(\.(0|[1-9][0-9]?))?( RC([1-9][0-9]?))?$ ]]; then
  echo "release-serial: \"$v\" is not MAJOR.MINOR[.PATCH][ RCn]" >&2
  exit 1
fi
major=${BASH_REMATCH[1]}
minor=${BASH_REMATCH[2]}
patch=${BASH_REMATCH[4]:-0}
rc=${BASH_REMATCH[6]:-99}
if [ "$rc" -gt 98 ] && [ -n "${BASH_REMATCH[6]}" ]; then
  echo "release-serial: \"$v\": a release candidate is numbered 1 to 98" >&2
  exit 1
fi
echo $((major * 1000000 + minor * 10000 + patch * 100 + rc))
