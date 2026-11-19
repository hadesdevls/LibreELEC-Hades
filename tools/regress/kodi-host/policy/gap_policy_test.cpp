// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of what is shown for a Dolby Vision picture that is reconstructed
// and sent as HDR10 when it cannot be reconstructed (DVPlaybackPolicy.h: BaseLayerIsKnownPicture(),
// ConversionShowsBase(), ConversionRepeats(), as CLinuxRendererGLES::Render() and
// CWinSystemGbmGLESContext::RenderDVConversion() take them).
// Build: g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined
//        -I <tree>/xbmc/cores/VideoPlayer/DVDCodecs/Video gap_policy_test.cpp
#include "DVPlaybackPolicy.h"

#include <cstdio>

using namespace DVBRIDGE;

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

// usable where a constant is
static_assert(BaseLayerIsKnownPicture(7, 6) && !BaseLayerIsKnownPicture(5, 0));
static_assert(ConversionShowsBase(true, true, true, false) && ConversionRepeats(true, true));

int main()
{
  // ---- which base layers are pictures by themselves, known by the record of the stream
  {
    bool ok = true;
    for (int c : {0, 1, 2, 4, 6})
      ok &= !BaseLayerIsKnownPicture(5, c);
    Check("profile 5: never (its base layer is in IPTPQc2), whatever its record says it is compatible with", ok);
    Check("AV1 profile 10 compatible with nothing: no; compatible with HDR10, SDR or HLG: yes",
          !BaseLayerIsKnownPicture(10, 0) && BaseLayerIsKnownPicture(10, 1) && BaseLayerIsKnownPicture(10, 2) &&
              BaseLayerIsKnownPicture(10, 4));
    Check("profiles 4, 7, 8 and 9: yes",
          BaseLayerIsKnownPicture(7, 6) && BaseLayerIsKnownPicture(8, 1) && BaseLayerIsKnownPicture(8, 2) &&
              BaseLayerIsKnownPicture(8, 4) && BaseLayerIsKnownPicture(4, 2) && BaseLayerIsKnownPicture(9, 2));
    ok = true;
    for (int c : {0, 1, 2, 4, 6})
      ok &= !BaseLayerIsKnownPicture(0, c) && !BaseLayerIsKnownPicture(-1, c);
    Check("a stream whose profile is not known: no (it may be profile 5)", ok);
  }
  // ---- whose base layer is shown where nothing reconstructed can be
  {
    int shown = 0;
    for (int picture = 0; picture < 2; ++picture)
      for (int pq = 0; pq < 2; ++pq)
        for (int bt2020 = 0; bt2020 < 2; ++bt2020)
          for (int mapped = 0; mapped < 2; ++mapped)
            shown += ConversionShowsBase(picture, pq, bt2020, mapped);
    Check("only a base layer that is a picture by itself, in PQ and BT.2020 (what the HDR10 output is set up for), "
          "where the reconstruction is not mapped to the display: 1 of the 16 cases",
          shown == 1 && ConversionShowsBase(true, true, true, false));
    Check("not a base layer of profile 5, of HLG or SDR (profile 8.4, 8.2), or under a mapping to the display",
          !ConversionShowsBase(BaseLayerIsKnownPicture(5, 6), true, true, false) &&
              !ConversionShowsBase(true, false, true, false) && !ConversionShowsBase(true, true, false, false) &&
              !ConversionShowsBase(true, true, true, true));
  }
  // ---- when the last picture reconstructed stands in for one that cannot be
  {
    Check("in the same run of the stream: always (a picture or a few without metadata: a still, as native output "
          "makes)",
          ConversionRepeats(true, true) && ConversionRepeats(true, false));
    Check("after a seek: not where the base layer can be shown instead (another place of the film), yes where it "
          "cannot (profile 5: better than nothing)",
          !ConversionRepeats(false, true) && ConversionRepeats(false, false));
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
