// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: why an HLG stream stays HLG (DVBRIDGE::HlgDolbyVisionIneligibility(), DVPlaybackPolicy.h) says
// nothing exactly where HLG as Dolby Vision takes the stream (DVBRIDGE::HlgDolbyVisionEligible()), and names the
// first thing that keeps it out elsewhere: every coding of an HLG stream (BT.2020 primaries, ARIB STD-B67), its
// depth, chroma subsampling, matrix, range and scan, against the rule; and nothing while the pixel format is unknown.
// 8 bits is taken since the bridge takes an 8-bit picture as the same picture in 10 bits (dvbridge_gl_widen.h).
#include "cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int g_checks = 0, g_failures = 0;
void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}
bool Says(const char* why, const char* what)
{
  return why && std::strcmp(why, what) == 0;
}
} // namespace

int main()
{
  using DVBRIDGE::HlgDolbyVisionEligible;
  using DVBRIDGE::HlgDolbyVisionIneligibility;
  int disagree = 0, cases = 0;
  for (int depth : {8, 9, 10, 12, 16})
    for (int cw = 0; cw <= 1; ++cw)
      for (int ch = 0; ch <= 1; ++ch)
        for (int ncl = 0; ncl <= 1; ++ncl)
          for (int full = 0; full <= 1; ++full)
            for (int prog = 0; prog <= 1; ++prog)
            {
              ++cases;
              const bool eligible =
                  HlgDolbyVisionEligible(true, true, ncl, depth, cw == 1 && ch == 1, full, prog);
              const char* why = HlgDolbyVisionIneligibility(ncl, depth, cw, ch, full, prog);
              if (eligible != (why == nullptr))
              {
                if (++disagree <= 3)
                  std::printf("  disagree: depth %d chroma %d/%d ncl %d full %d progressive %d: %s\n", depth,
                              cw, ch, ncl, full, prog, why ? why : "(nothing)");
              }
            }
  Check(disagree == 0, "a reason exactly where the rule refuses, over " + std::to_string(cases) +
                           " codings (" + std::to_string(disagree) + " disagree)");
  Check(HlgDolbyVisionIneligibility(true, 0, 1, 1, false, true) == nullptr &&
            HlgDolbyVisionIneligibility(false, 0, 0, 0, true, false) == nullptr,
        "nothing while the pixel format is not known");
  Check(HlgDolbyVisionIneligibility(true, 8, 1, 1, false, true) == nullptr &&
            HlgDolbyVisionEligible(true, true, true, 8, true, false, true),
        "8-bit 4:2:0: eligible, nothing to say (its picture goes to the bridge widened to 10 bits)");
  Check(Says(HlgDolbyVisionIneligibility(true, 12, 1, 1, false, true),
             "its samples are neither of 8 nor of 10 bits"),
        "12-bit: its samples are neither of 8 nor of 10 bits");
  Check(Says(HlgDolbyVisionIneligibility(true, 10, 1, 0, false, true), "its chroma is 4:2:2"),
        "10-bit 4:2:2 (a camera's XAVC S-I): its chroma is 4:2:2");
  Check(Says(HlgDolbyVisionIneligibility(true, 10, 1, 1, true, true), "it is in full range"),
        "full range: it is in full range");
  Check(Says(HlgDolbyVisionIneligibility(true, 10, 1, 1, false, false), "it is interlaced"),
        "interlaced: it is interlaced");
  std::printf("HLG-REASON %d checks, %d failed\n", g_checks, g_failures);
  if (!g_failures)
    std::printf("ALL PASSED\n");
  return g_failures ? 1 : 0;
}
