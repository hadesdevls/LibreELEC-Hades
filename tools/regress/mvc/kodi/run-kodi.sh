#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The host tests of Kodi's side (read only, from $MVC_KODI): stereo_rate_test.cpp (the 3D output at twice
# the refresh rate, xbmc/windowing/gbm/drm/DRMStereo.h) and mvcc_count_test.cpp (finding 11 as Kodi reads
# 'mvcC', xbmc/cores/VideoPlayer/MultiviewChoice.h). Each against the header as it is (must pass) and against
# copies of it with one rule changed (each must fail). MUTATE=1 also runs mutate_range.py on every mutation site
# of the choice (lines 316-478 of DRMStereo.h) and of what it reads of the modes (147-263): survivors are listed
# for the reader. Prints STEREO-RATE PASSED / MVCC PASSED, or FAILED.
set -u
. "$(dirname "$0")/../env.sh"
K=$KODI
D=$W/kodi; rm -rf $D; mkdir -p $D
H=$K/xbmc/windowing/gbm/drm/DRMStereo.h
M=$K/xbmc/cores/VideoPlayer/MultiviewChoice.h
build() { # include folder, source, program
  nice -n 19 g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I "$1" -o "$3" "$2" > "$3.log" 2>&1
}
sem() { # test source, header, name, sed expression: the test must fail on the changed copy
  mkdir -p $D/sem/$3
  sed -e "$4" "$2" > $D/sem/$3/$(basename "$2")
  if cmp -s "$2" $D/sem/$3/$(basename "$2"); then echo "$3: THE CHANGE CHANGED NOTHING"; fails=$((fails + 1)); return; fi
  if ! build $D/sem/$3 "$1" $D/sem/$3/t; then echo "$3: DOES NOT COMPILE"; fails=$((fails + 1)); return; fi
  $D/sem/$3/t > $D/sem/$3/out 2>&1
  if [ $? = 0 ]; then echo "$3: NOT CAUGHT"; fails=$((fails + 1));
  else echo "$3: caught, $(grep -c '^FAIL' $D/sem/$3/out) checks fail, first: $(grep -m1 '^FAIL' $D/sem/$3/out | cut -c1-110)"; fi
}
echo "DRMStereo.h: $(git -C $K log -1 --format=%h -- xbmc/windowing/gbm/drm/DRMStereo.h) $(sha256sum $H | cut -c1-16); MultiviewChoice.h: $(git -C $K log -1 --format=%h -- xbmc/cores/VideoPlayer/MultiviewChoice.h) $(sha256sum $M | cut -c1-16)"
T=$TOOLS/kodi/stereo_rate_test.cpp
fails=0
build $K/xbmc/windowing/gbm/drm $T $D/t || { cat $D/t.log | head -20; fails=1; }
$D/t > $D/out.txt 2>&1 || fails=$((fails + 1))
echo "stereo_rate_test: $(tail -n 2 $D/out.txt | tr '\n' ' ')"
sem $T $H never-double 's/  return DETAIL::ChooseAtRate(offers, own, 2.0 \* own.refresh, arrangement, allowed);/  return {};/'
sem $T $H picture-rate-ignored 's/  if (!allowed.doubleRate || std::fabs(allowed.pictureRate - own.refresh) >= 0.01)/  if (!allowed.doubleRate)/'
sem $T $H permission-ignored 's/  if (!allowed.doubleRate || std::fabs(allowed.pictureRate - own.refresh) >= 0.01)/  if (std::fabs(allowed.pictureRate - own.refresh) >= 0.01)/'
sem $T $H double-first 's/DETAIL::ChooseAtRate(offers, own, own.refresh, arrangement, allowed);/DETAIL::ChooseAtRate(offers, own, 2.0 * own.refresh, arrangement, allowed);/'
sem $T $H loose-rate 's/std::fabs(offer.refresh - rate) < 0.01; };/std::fabs(offer.refresh - rate) < 0.1; };/'
sem $T $H whitelist-ignored-at-twice 's/^          otherSize(offer))$/          true)/'
sem $T $H 1080-line-rule-off 's/    return offer.height >= (own.height < FILM_LINES ? own.height : FILM_LINES);/    return offer.height >= 0 * (own.height < FILM_LINES ? own.height : FILM_LINES);/'
sem $T $H triple-not-double 's/2.0 \* own.refresh/3.0 * own.refresh/'
[ $fails = 0 ] && echo "STEREO-RATE PASSED" || echo "STEREO-RATE FAILED ($fails)"
T=$TOOLS/kodi/mvcc_count_test.cpp
fails=0
build $K/xbmc/cores/VideoPlayer $T $D/m || { cat $D/m.log | head -20; fails=1; }
$D/m > $D/mout.txt 2>&1 || fails=$((fails + 1))
echo "mvcc_count_test: $(tail -n 2 $D/mout.txt | tr '\n' ' ')"
sem $T $M five-bits-declares 's/  const unsigned int count = (record\[5\] & 0xe0) == 0xe0 ? record\[5\] & 0x1f : record\[5\] & 0x7f;/  const unsigned int count = record[5] \& 0x1f;/'
sem $T $M five-bits-sets 's/  unsigned int count = (record\[5\] & 0xe0) == 0xe0 ? record\[5\] & 0x1f : record\[5\] & 0x7f;/  unsigned int count = record[5] \& 0x1f;/'
sem $T $M merge-of-32 's/if (baseSps.size() + moreSps.size() > 31 ||/if (baseSps.size() + moreSps.size() > 32 ||/'
[ $fails = 0 ] && echo "MVCC PASSED" || echo "MVCC FAILED ($fails)"
if [ "${MUTATE:-}" = 1 ]; then
  export MVC_KODI=$K MVC_WORK=$D
  python3 -I $TOOLS/kodi/mutate_range.py $TOOLS/kodi/stereo_rate_test.cpp xbmc/windowing/gbm/drm/DRMStereo.h 316 478
  python3 -I $TOOLS/kodi/mutate_range.py $TOOLS/kodi/stereo_rate_test.cpp xbmc/windowing/gbm/drm/DRMStereo.h 147 263
fi
rm -rf $D/sem
