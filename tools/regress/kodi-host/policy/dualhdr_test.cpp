// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of what a Dolby Vision video with HDR10+ metadata of its own is sent with
// (DVPlaybackPolicy.h: DualVerdictOf(), Hdr10PlusWins(), DualVerdictText(), as
// DVBRIDGE::DualHdr10PlusFound() takes them), above all on a display that takes HDR10+ and not native
// Dolby Vision: a video of profile 8.1 goes out with its own HDR10+ there, sent as the HDR10+ it also is
// where the settings would rebuild it (ConversionTargetOf(), "Send it as HDR10+ where the display takes
// it"), and as before (its base layer with its own HDR10+) where they do not.
// Build: g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined
//        -I <tree>/xbmc/cores/VideoPlayer/DVDCodecs/Video dualhdr_test.cpp
#include "DVPlaybackPolicy.h"

#include <cstdio>
#include <initializer_list>
#include <string>

using namespace DVBRIDGE;

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

static constexpr int DV = static_cast<int>(DualHdr::DOLBY_VISION);
static constexpr int PLUS = static_cast<int>(DualHdr::HDR10PLUS);

// usable where a constant is
static_assert(DualVerdictOf(DV, OutputChoice::AUTOMATIC, 8, 1, true, true) == DualVerdict::DOLBY_VISION);

// what the demuxer asks, as DualHdr10PlusFound() makes its input: rebuilt for output as HDR10 by the
// settings, with the output chosen as they say, for a stream of profile 8.1 (no enhancement layer)
static bool Rebuilt(bool convert, bool alsoEnhancement, bool mapped, bool dvPlusPossible)
{
  const ConversionSetting conversion = ConversionSettingOf(convert, alsoEnhancement, mapped);
  return ConversionTargetOf(true, false, conversion, false, OutputChoice::AUTOMATIC, 8, 1, false, false,
                            false, false,
                            conversion != ConversionSetting::EVERY && dvPlusPossible) ==
         ConversionTarget::HDR10;
}

int main()
{
  // ---- a display that takes both: as before
  {
    Check("both: the setting says Dolby Vision (the default): Dolby Vision, not won by HDR10+",
          DualVerdictOf(DV, OutputChoice::AUTOMATIC, 8, 1, true, true) == DualVerdict::DOLBY_VISION &&
              !Hdr10PlusWins(DualVerdictOf(DV, OutputChoice::AUTOMATIC, 8, 1, true, true)));
    Check("both: the setting says HDR10+: HDR10+, won (profile 8.1 and profile 7 of a disc)",
          DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 8, 1, true, true) == DualVerdict::HDR10PLUS &&
              DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 7, 6, true, true) == DualVerdict::HDR10PLUS &&
              Hdr10PlusWins(DualVerdict::HDR10PLUS));
    Check("both: the choice of the video stands over the setting",
          DualVerdictOf(DV, OutputChoice::HDR10PLUS, 8, 1, true, true) == DualVerdict::HDR10PLUS_CHOSEN &&
              DualVerdictOf(PLUS, OutputChoice::DOLBY_VISION, 8, 1, true, true) ==
                  DualVerdict::DOLBY_VISION_CHOSEN);
    bool same = true;
    for (int s : {DV, PLUS})
      for (OutputChoice c : {OutputChoice::AUTOMATIC, OutputChoice::DOLBY_VISION, OutputChoice::HDR10PLUS})
        for (int p : {7, 8})
          same &= DualVerdictOf(s, c, p, p == 7 ? 6 : 1, true, true, true, true) ==
                  DualVerdictOf(s, c, p, p == 7 ? 6 : 1, true, true);
    Check("both: whether it would be rebuilt or mapped where native output is not had changes nothing",
          same);
    Check("native Dolby Vision without HDR10+ that can be sent: as before (DISPLAY), whatever the setting",
          DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 8, 1, true, false) == DualVerdict::DISPLAY &&
              DualVerdictOf(PLUS, OutputChoice::HDR10PLUS, 8, 1, true, false) == DualVerdict::DISPLAY);
  }
  // ---- a display that takes HDR10+ and not native Dolby Vision
  {
    bool ok = true;
    for (int s : {DV, PLUS})
      ok &= DualVerdictOf(s, OutputChoice::AUTOMATIC, 8, 1, false, true, true, false) ==
                DualVerdict::HDR10PLUS_DISPLAY &&
            Hdr10PlusWins(DualVerdictOf(s, OutputChoice::AUTOMATIC, 8, 1, false, true, true, false));
    Check("HDR10+ without Dolby Vision, profile 8.1 that the settings would rebuild: sent as the HDR10+ it "
          "also is (won), whatever videoscreen.hadesdualhdr says",
          ok);
    ok = true;
    for (int s : {DV, PLUS})
      ok &= DualVerdictOf(s, OutputChoice::AUTOMATIC, 8, 1, false, true, false, false) ==
                DualVerdict::BASE_LAYER_HDR10PLUS &&
            !Hdr10PlusWins(DualVerdictOf(s, OutputChoice::AUTOMATIC, 8, 1, false, true, false, false));
    Check("... that is not rebuilt (the defaults): as before, its base layer with its own HDR10+, not "
          "opened again",
          ok);
    Check("... mapped to the display: as before (rebuilt and mapped), not won",
          DualVerdictOf(DV, OutputChoice::AUTOMATIC, 8, 1, false, true, true, true) == DualVerdict::MAPPED &&
              DualVerdictOf(DV, OutputChoice::HDR10PLUS, 8, 1, false, true, true, true) ==
                  DualVerdict::MAPPED &&
              !Hdr10PlusWins(DualVerdict::MAPPED));
    Check("... its HDR10+ chosen for the video (a choice kept for the file): HDR10+, won",
          DualVerdictOf(DV, OutputChoice::HDR10PLUS, 8, 1, false, true, false, false) ==
                  DualVerdict::HDR10PLUS_CHOSEN &&
              DualVerdictOf(DV, OutputChoice::HDR10PLUS, 8, 1, false, true, true, false) ==
                  DualVerdict::HDR10PLUS_CHOSEN);
    Check("... its Dolby Vision chosen for the video: as before (DISPLAY), not won",
          DualVerdictOf(PLUS, OutputChoice::DOLBY_VISION, 8, 1, false, true, true, false) ==
                  DualVerdict::DISPLAY &&
              !Hdr10PlusWins(DualVerdictOf(PLUS, OutputChoice::DOLBY_VISION, 8, 1, false, true, true, false)));
    ok = true;
    for (OutputChoice c : {OutputChoice::REBUILT_HDR10PLUS, OutputChoice::REBUILT_HDR10, OutputChoice::HDR10,
                           OutputChoice::KODI, OutputChoice::MEASURED, OutputChoice::SDR})
      ok &= DualVerdictOf(DV, c, 8, 1, false, true, true, false) == DualVerdict::OTHER_CHOICE;
    Check("... another output chosen for the video (\"Dolby Vision as HDR10+\" among them): it stands", ok);
    ok = true;
    for (bool rebuilt : {false, true})
      for (bool mapped : {false, true})
        ok &= DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 7, 6, false, true, rebuilt, mapped) ==
                  DualVerdict::DISPLAY &&
              DualVerdictOf(PLUS, OutputChoice::HDR10PLUS, 7, 6, false, true, rebuilt, mapped) ==
                  DualVerdict::DISPLAY;
    Check("... profile 7 of a disc: as before (DISPLAY: its enhancement layer is not given up)", ok);
  }
  // ---- a display that takes neither, or HDR10 only; base layers that are not HDR10
  {
    bool ok = true;
    for (OutputChoice c : {OutputChoice::AUTOMATIC, OutputChoice::DOLBY_VISION, OutputChoice::HDR10PLUS})
      for (bool rebuilt : {false, true})
        ok &= DualVerdictOf(PLUS, c, 8, 1, false, false, rebuilt, false) == DualVerdict::DISPLAY;
    Check("no native Dolby Vision and no HDR10+ that can be sent: as before (DISPLAY)", ok);
    ok = true;
    for (int c : {0, 2, 4, 6})
      ok &= DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 8, c, false, true, true, false) ==
            DualVerdict::BASE_LAYER;
    Check("profile 5, 8.2, 8.4 and the like: their base layer is not HDR10 (BASE_LAYER)",
          ok && DualVerdictOf(PLUS, OutputChoice::AUTOMATIC, 5, 0, false, true, true, false) ==
                    DualVerdict::BASE_LAYER);
  }
  // ---- the input that DualHdr10PlusFound() makes: rebuilt for output as HDR10 by the settings
  {
    Check("the defaults (Dolby Vision as HDR10 when needed, its enhancement layer, \"Send it as HDR10+\" "
          "off): profile 8.1 is not rebuilt",
          !Rebuilt(true, true, false, false) && !Rebuilt(true, false, false, false));
    Check("\"Send it as HDR10+ where the display takes it\" on, on a display with HDR10+: rebuilt",
          Rebuilt(true, true, false, true) && Rebuilt(true, false, false, true));
    Check("mapped to the display: rebuilt (EVERY), and the verdict is MAPPED",
          Rebuilt(true, true, true, false) &&
              DualVerdictOf(DV, OutputChoice::AUTOMATIC, 8, 1, false, true, Rebuilt(true, true, true, false),
                            true) == DualVerdict::MAPPED);
    Check("the conversion off: profile 8.1 is not rebuilt, even with HDR10+ possible",
          !Rebuilt(false, false, false, true));
  }
  // ---- what the log says
  {
    const std::string won = DualVerdictText(DualVerdict::HDR10PLUS_DISPLAY, 8, 1, false, true);
    const std::string kept = DualVerdictText(DualVerdict::BASE_LAYER_HDR10PLUS, 8, 1, false, true);
    const std::string mapped = DualVerdictText(DualVerdict::MAPPED, 8, 1, false, true);
    Check("the log of each new verdict says what goes out and why",
          won.find("sent as HDR10+") != std::string::npos &&
              won.find("not native Dolby Vision") != std::string::npos &&
              kept.find("own HDR10+ metadata") != std::string::npos &&
              kept.find("not rebuilt") != std::string::npos &&
              mapped.find("hadesdvhdr10map") != std::string::npos);
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
