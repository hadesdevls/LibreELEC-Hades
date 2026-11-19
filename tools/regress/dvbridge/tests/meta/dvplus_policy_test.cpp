// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades 2026-10-27, TEST ONLY: which Dolby Vision streams are rebuilt for Dolby Vision sent as HDR10+
// (DVBRIDGE::ConversionTargetOf() with hdr10Plus, ConversionEligibleForHDR10Plus(), DVPlaybackPolicy.h of the tree
// given with -I), on the host:
//   P1 without HDR10+ (the parameter left out, or false): every combination exactly as before;
//   P2 with it: what was rebuilt still is, and profile 8.1 without an enhancement layer is too, unless the conversion
//      is off (NEVER, or the choice of Kodi's own output), or for software decoding, interlaced or stereoscopic video;
//      nothing else (profile 7 as its setting says, 8.4, 5, 10);
//   P3 the output that takes no HDR10 (SDR) is not touched.
// usage: dvplus_policy_test
#include "DVPlaybackPolicy.h"

#include <cstdio>

using namespace DVBRIDGE;

static int g_failures, g_checks;
static void Check(bool ok, const char* what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

int main()
{
  const ConversionSetting settings[] = {ConversionSetting::NEEDED, ConversionSetting::ALSO_ENHANCEMENT,
                                        ConversionSetting::NEVER, ConversionSetting::EVERY};
  const OutputChoice choices[] = {OutputChoice::AUTOMATIC, OutputChoice::HDR10, OutputChoice::KODI};
  const int profiles[] = {4, 5, 7, 8, 9, 10};
  const int compatibilities[] = {0, 1, 2, 4, 6};
  long combinations = 0, same = 0, added = 0, wrong = 0;
  for (int out = 0; out < 4; ++out)
    for (const auto setting : settings)
      for (const auto choice : choices)
        for (const int profile : profiles)
          for (const int compatibility : compatibilities)
            for (int flags = 0; flags < 32; ++flags)
            {
              const bool hdr10 = out & 1, sdr = out & 2, layer = flags & 1, sw = flags & 2,
                         interlaced = flags & 4, stereo = flags & 8, reference = flags & 16;
              ++combinations;
              const ConversionTarget before =
                  ConversionTargetOf(hdr10, sdr, setting, reference, choice, profile, compatibility, layer, sw,
                                     interlaced, stereo);
              const ConversionTarget off = ConversionTargetOf(hdr10, sdr, setting, reference, choice, profile,
                                                              compatibility, layer, sw, interlaced, stereo, false);
              const ConversionTarget on = ConversionTargetOf(hdr10, sdr, setting, reference, choice, profile,
                                                             compatibility, layer, sw, interlaced, stereo, true);
              same += before == off;
              const ConversionSetting s = ConversionSettingFor(setting, choice);
              const bool extra = hdr10 && s != ConversionSetting::NEVER && !sw && !interlaced && !stereo &&
                                 profile == 8 && compatibility == 1 && !layer;
              const ConversionTarget expected = extra ? ConversionTarget::HDR10 : before;
              if (on != expected)
                ++wrong;
              added += on != before;
            }
  Check(same == combinations, "P1 without HDR10+: every combination as before");
  Check(wrong == 0, "P2 with HDR10+: what was rebuilt still is, and profile 8.1 too where the conversion is on");
  std::printf("     %ld combinations, %ld that HDR10+ adds\n", combinations, added);
  // the cases by name
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::AUTOMATIC, 8, 1, false, false,
                           false, false) == ConversionTarget::NONE,
        "P2 8.1 by default: not rebuilt (its base layer plays as HDR10)");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::AUTOMATIC, 8, 1, false, false,
                           false, false, true) == ConversionTarget::HDR10,
        "P2 8.1 with HDR10+: rebuilt");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::AUTOMATIC, 7, 6, true, false,
                           false, false, true) == ConversionTarget::NONE,
        "P2 7 with its enhancement layer, the setting of the layer off: not rebuilt, with HDR10+ too");
  Check(ConversionTargetOf(true, false, ConversionSetting::ALSO_ENHANCEMENT, false, OutputChoice::AUTOMATIC, 7, 6,
                           true, false, false, false, true) == ConversionTarget::HDR10,
        "P2 7 with its enhancement layer, the setting on: rebuilt");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::KODI, 8, 1, false, false, false,
                           false, true) == ConversionTarget::NONE,
        "P2 Kodi's own output chosen: 8.1 not rebuilt, with HDR10+ too");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEVER, false, OutputChoice::AUTOMATIC, 8, 1, false, false,
                           false, false, true) == ConversionTarget::NONE,
        "P2 the conversion off: nothing, with HDR10+ too");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::AUTOMATIC, 8, 4, false, false,
                           false, false, true) == ConversionTarget::NONE,
        "P2 8.4 (HLG): not rebuilt");
  Check(ConversionTargetOf(true, false, ConversionSetting::NEEDED, false, OutputChoice::AUTOMATIC, 8, 1, false, true,
                           false, false, true) == ConversionTarget::NONE,
        "P2 8.1 decoded in software: not rebuilt");
  Check(ConversionTargetOf(false, true, ConversionSetting::NEEDED, true, OutputChoice::AUTOMATIC, 8, 1, false, false,
                           false, false, true) ==
            ConversionTargetOf(false, true, ConversionSetting::NEEDED, true, OutputChoice::AUTOMATIC, 8, 1, false,
                               false, false, false),
        "P3 an SDR output: as before");
  std::printf("%s: %d of %d\n", g_failures ? "FAILED" : "ALL PASSED", g_checks - g_failures, g_checks);
  return g_failures != 0;
}
