// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test (the setting off is NO_PICTURE_ONLY since issue #2): the pure part of the choice of output of a Dolby Vision video
// (DVPlaybackPolicy.h): which streams are reconstructed and sent as HDR10 with the settings, the
// mapping to the display and the choice of the user for the video; and what native output the
// choice allows. Compiled against the header of a Kodi tree: g++ -std=c++20 -I<tree>/xbmc/cores/VideoPlayer/DVDCodecs/Video
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

// profile, compatibility, enhancement layer
struct Stream
{
  int profile, compatibility;
  bool el;
  const char* name;
};
static const Stream STREAMS[] = {
    {5, 0, false, "profile 5"},           {7, 6, true, "profile 7 with its layer"},
    {7, 6, false, "profile 7 without"},   {8, 1, false, "profile 8.1"},
    {8, 4, false, "profile 8.4"},         {10, 0, false, "profile 10.0"},
    {10, 1, false, "profile 10.1"},       {4, 2, true, "profile 4"},
};

int main()
{
  // the settings as one: unchanged where the mapping is off
  Check(ConversionSettingOf(true, false, false) == ConversionSetting::NEEDED, "default: NEEDED");
  Check(ConversionSettingOf(true, true, false) == ConversionSetting::ALSO_ENHANCEMENT, "FEL: ALSO");
  Check(ConversionSettingOf(false, false, false) == ConversionSetting::NO_PICTURE_ONLY, "off: NO_PICTURE_ONLY");
  Check(ConversionSettingOf(false, true, false) == ConversionSetting::NO_PICTURE_ONLY, "off with FEL: NO_PICTURE_ONLY");
  Check(ConversionSettingOf(false, false, true) == ConversionSetting::NO_PICTURE_ONLY, "off with the mapping: NO_PICTURE_ONLY");
  Check(ConversionSettingOf(true, false, true) == ConversionSetting::EVERY, "mapped: EVERY");
  Check(ConversionSettingOf(true, true, true) == ConversionSetting::EVERY, "mapped with FEL: EVERY");
  // the choice of the user
  for (const auto s : {ConversionSetting::NEEDED, ConversionSetting::ALSO_ENHANCEMENT, ConversionSetting::NEVER,
                       ConversionSetting::EVERY})
  {
    Check(ConversionSettingFor(s, OutputChoice::AUTOMATIC) == s, "automatic: the settings");
    Check(ConversionSettingFor(s, OutputChoice::HDR10) == ConversionSetting::EVERY, "HDR10: every stream");
    Check(ConversionSettingFor(s, OutputChoice::KODI) == ConversionSetting::NEVER, "Kodi: none");
  }
  Check(NativeAllowed(OutputChoice::AUTOMATIC), "automatic allows native output");
  Check(!NativeAllowed(OutputChoice::HDR10), "HDR10 does not");
  Check(!NativeAllowed(OutputChoice::KODI), "Kodi does not");
  // the choices offered for a video: "as Kodi plays it" not where the base layer is no picture by
  // itself (profile 5, profile 10 compatible with nothing), every other choice everywhere
  for (const auto& st : STREAMS)
  {
    const bool noPicture = st.profile == 5 || (st.profile == 10 && st.compatibility == 0);
    Check(OutputChoiceOffered(OutputChoice::AUTOMATIC, st.profile, st.compatibility), st.name);
    Check(OutputChoiceOffered(OutputChoice::HDR10, st.profile, st.compatibility), st.name);
    Check(OutputChoiceOffered(OutputChoice::KODI, st.profile, st.compatibility) == !noPicture, st.name);
  }
  Check(!OutputChoiceOffered(OutputChoice::KODI, 5, 0), "profile 5: not as Kodi plays it");
  Check(!OutputChoiceOffered(OutputChoice::KODI, 5, 6), "profile 5 whatever its record says: not either");
  Check(!OutputChoiceOffered(OutputChoice::KODI, 10, 0), "profile 10.0: not as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 10, 1), "profile 10.1: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 10, 2), "profile 10.2: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 10, 4), "profile 10.4: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 7, 6), "profile 7: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 8, 1), "profile 8.1: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 8, 4), "profile 8.4: as Kodi plays it");
  Check(OutputChoiceOffered(OutputChoice::KODI, 0, 0), "not Dolby Vision: as Kodi plays it");
  // the streams: as before where nothing new is set
  for (const auto& st : STREAMS)
  {
    const bool before5 = st.profile == 5 || (st.profile == 10 && st.compatibility == 0);
    const bool beforeFel = before5 || (st.profile == 7 && st.el);
    Check(ConversionEligible(ConversionSetting::NEEDED, st.profile, st.compatibility, st.el, false, false, false) ==
              before5,
          st.name);
    Check(ConversionEligible(ConversionSetting::ALSO_ENHANCEMENT, st.profile, st.compatibility, st.el, false,
                             false, false) == beforeFel,
          st.name);
    Check(!ConversionEligible(ConversionSetting::NEVER, st.profile, st.compatibility, st.el, false, false, false),
          st.name);
    // (the setting off still rebuilds a base layer that is no picture by itself)
    Check(ConversionEligible(ConversionSetting::NO_PICTURE_ONLY, st.profile, st.compatibility, st.el, false, false,
                             false) == before5,
          st.name);
    // every stream with EVERY, but never what the renderer keeps from Dolby Vision anyway
    Check(ConversionEligible(ConversionSetting::EVERY, st.profile, st.compatibility, st.el, false, false, false),
          st.name);
    Check(!ConversionEligible(ConversionSetting::EVERY, st.profile, st.compatibility, st.el, true, false, false),
          "EVERY: not a software probe");
    Check(!ConversionEligible(ConversionSetting::EVERY, st.profile, st.compatibility, st.el, false, true, false),
          "EVERY: not interlaced");
    Check(!ConversionEligible(ConversionSetting::EVERY, st.profile, st.compatibility, st.el, false, false, true),
          "EVERY: not stereoscopic");
  }
  std::printf("policy: %d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
