// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test (the setting off is NO_PICTURE_ONLY since issue #2): the pure part of the choice of output of a Dolby Vision video
// (DVPlaybackPolicy.h): which streams are reconstructed and sent as HDR10 with the settings, the
// mapping to the display and the choice of the user for the video; and what native output the
// choice allows. Compiled against the header of a Kodi tree: g++ -std=c++20 -I<tree>/xbmc/cores/VideoPlayer/DVDCodecs/Video
// -I<tree>/xbmc (1.2.3: settings/SettingTakeOver.h, the boolean of "HDR10+ as Dolby Vision" taken over)
#include "DVPlaybackPolicy.h"
#include "settings/SettingTakeOver.h"

#include <cstdio>
#include <cstring>

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
  // Hades: why an HDR10+ stream is not sent as Dolby Vision (Hdr10PlusAsDolbyVisionWhy()): each of the four reasons, in
  // the order it decides, and the verdict of Hdr10PlusAsDolbyVisionFor() for every value of the setting, choice and
  // display. (1.2.3: the setting has three values, Hdr10PlusMode; NO_HDR10PLUS is the "on" of before, and the checks of
  // before are made with it)
  {
    using OC = OutputChoice;
    using HM = Hdr10PlusMode;
    const auto why = [](HM mode, OC choice, bool native, bool sent, const char* want)
    { return std::strcmp(Hdr10PlusAsDolbyVisionWhy(mode, choice, native, sent), want) == 0; };
    Check(why(HM::OFF, OC::AUTOMATIC, true, false, "off"), "HDR10+ as Dolby Vision: the setting off");
    Check(why(HM::NO_HDR10PLUS, OC::HDR10, true, false, "choice"),
          "HDR10+ as Dolby Vision: another output chosen for the video");
    Check(why(HM::NO_HDR10PLUS, OC::AUTOMATIC, false, false, "nodv"), "HDR10+ as Dolby Vision: no native Dolby Vision");
    Check(why(HM::NO_HDR10PLUS, OC::AUTOMATIC, true, true, "hdr10plus"),
          "HDR10+ as Dolby Vision: the display takes HDR10+");
    Check(why(HM::NO_HDR10PLUS, OC::AUTOMATIC, true, false, ""), "HDR10+ as Dolby Vision: sent so");
    Check(why(HM::OFF, OC::DOLBY_VISION, true, true, ""),
          "HDR10+ as Dolby Vision: chosen for the video, over the setting and a display that takes HDR10+");
    Check(why(HM::OFF, OC::DOLBY_VISION, false, false, "nodv"), "HDR10+ as Dolby Vision: chosen, no native Dolby Vision");
    Check(why(HM::OFF, OC::HDR10, false, true, "choice"), "HDR10+ as Dolby Vision: another choice is said first");
    Check(why(HM::OFF, OC::AUTOMATIC, false, true, "off"),
          "HDR10+ as Dolby Vision: the setting off is said before the display");
    Check(why(HM::NO_HDR10PLUS, OC::AUTOMATIC, false, true, "nodv"),
          "HDR10+ as Dolby Vision: no native Dolby Vision is said before HDR10+ (choosing Dolby Vision would not help)");
    // (1.2.3) Always: also to a display that takes HDR10+, by the user's preference; the rest as before
    Check(why(HM::ALWAYS, OC::AUTOMATIC, true, true, ""),
          "HDR10+ as Dolby Vision, always: sent so to a display that takes HDR10+");
    Check(why(HM::ALWAYS, OC::AUTOMATIC, true, false, ""), "HDR10+ as Dolby Vision, always: and to one that does not");
    Check(why(HM::ALWAYS, OC::AUTOMATIC, false, true, "nodv"), "HDR10+ as Dolby Vision, always: no native Dolby Vision");
    Check(why(HM::ALWAYS, OC::HDR10, true, true, "choice") && why(HM::ALWAYS, OC::KODI, true, true, "choice"),
          "HDR10+ as Dolby Vision, always: another output chosen for the video wins over the setting");
    Check(why(HM::ALWAYS, OC::DOLBY_VISION, true, true, ""), "HDR10+ as Dolby Vision, always: chosen for the video too");
    Check(Hdr10PlusAsDolbyVisionFor(HM::ALWAYS, true, true, true),
          "HDR10+ as Dolby Vision, always: converted for a display that takes HDR10+");
    Check(Hdr10PlusAsDolbyVisionFor(HM::ALWAYS, true, true, false),
          "HDR10+ as Dolby Vision, always: converted for a display without HDR10+");
    Check(!Hdr10PlusAsDolbyVisionFor(HM::NO_HDR10PLUS, true, true, true) &&
              Hdr10PlusAsDolbyVisionFor(HM::NO_HDR10PLUS, true, true, false),
          "HDR10+ as Dolby Vision, where the display has no HDR10+: only there, as on did");
    Check(!Hdr10PlusAsDolbyVisionFor(HM::OFF, true, true, false) && !Hdr10PlusAsDolbyVisionFor(HM::OFF, true, true, true),
          "HDR10+ as Dolby Vision, off: never");
    Check(!Hdr10PlusAsDolbyVisionFor(HM::ALWAYS, false, true, true) &&
              !Hdr10PlusAsDolbyVisionFor(HM::ALWAYS, true, false, true),
          "HDR10+ as Dolby Vision, always: never without native Dolby Vision allowed and had");
    // what sent it as Dolby Vision to a display that takes HDR10+ (Hdr10PlusAsDolbyVisionOver()): the player information
    // and kodi.log say it
    const auto over = [](HM mode, OC choice, bool native, bool takes, const char* want)
    { return std::strcmp(Hdr10PlusAsDolbyVisionOver(mode, choice, native, takes), want) == 0; };
    Check(over(HM::ALWAYS, OC::AUTOMATIC, true, true, "always"),
          "HDR10+ over HDR10+: always, to a display that takes HDR10+, is said");
    Check(over(HM::ALWAYS, OC::AUTOMATIC, true, false, ""), "HDR10+ over HDR10+: always, to a display without it: nothing");
    Check(over(HM::NO_HDR10PLUS, OC::AUTOMATIC, true, true, ""),
          "HDR10+ over HDR10+: where the display has no HDR10+, a display that takes it gets its HDR10+: nothing");
    Check(over(HM::OFF, OC::DOLBY_VISION, true, true, "choice") && over(HM::ALWAYS, OC::DOLBY_VISION, true, true, "choice"),
          "HDR10+ over HDR10+: Dolby Vision chosen for the video is what sent it, whatever the setting");
    Check(over(HM::OFF, OC::DOLBY_VISION, true, false, ""), "HDR10+ over HDR10+: chosen, a display without HDR10+: nothing");
    Check(over(HM::ALWAYS, OC::AUTOMATIC, false, true, "") && over(HM::ALWAYS, OC::HDR10, true, true, ""),
          "HDR10+ over HDR10+: not sent as Dolby Vision: nothing");
    const OC choices[] = {OC::AUTOMATIC, OC::HDR10,     OC::KODI,         OC::MEASURED,
                          OC::DOLBY_VISION, OC::HDR10PLUS, OC::REBUILT_HDR10};
    for (const HM mode : {HM::OFF, HM::NO_HDR10PLUS, HM::ALWAYS})
      for (const OC choice : choices)
        for (int native = 0; native < 2; ++native)
          for (int sent = 0; sent < 2; ++sent)
          {
            const bool chosen = choice == OC::DOLBY_VISION;
            // (the choice of the video sends it as Dolby Vision whatever the setting and the display: as always does)
            const bool converted =
                chosen ? Hdr10PlusAsDolbyVisionFor(HM::ALWAYS, true, native, sent)
                       : choice == OC::AUTOMATIC && Hdr10PlusAsDolbyVisionFor(mode, true, native, sent);
            Check((*Hdr10PlusAsDolbyVisionWhy(mode, choice, native, sent) == '\0') == converted,
                  "HDR10+ as Dolby Vision: a reason exactly where it is not sent so");
            Check((*Hdr10PlusAsDolbyVisionOver(mode, choice, native, sent) != '\0') == (converted && sent),
                  "HDR10+ over HDR10+: said exactly where it is sent so to a display that takes HDR10+");
          }
  }
  // Hades (1.2.3): the values of "Play HDR10+ videos as Dolby Vision", and the boolean that earlier builds saved,
  // taken over when the settings load (settings/SettingTakeOver.h, Migrate(), as CSettings::Load() runs it): a user who
  // had it on keeps exactly what it did
  {
    using HM = Hdr10PlusMode;
    using KODI::SETTINGS::HADES::Migrate;
    Check(Hdr10PlusModeOf(0) == HM::OFF && Hdr10PlusModeOf(1) == HM::NO_HDR10PLUS && Hdr10PlusModeOf(2) == HM::ALWAYS,
          "the values of videoscreen.hadesdvfromhdr10plusmode");
    Check(Hdr10PlusModeOf(-1) == HM::OFF && Hdr10PlusModeOf(3) == HM::OFF, "a value that is none of them: off");
    const int deflt = static_cast<int>(HM::OFF);
    // guisettings.xml of 1.2.2 or before: <setting id="videoscreen.hadesdvfromhdr10plus">true</setting>, no new setting
    const auto on = Migrate(deflt, deflt, true, Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(on.mode == static_cast<int>(HM::NO_HDR10PLUS) && on.modeChanged && on.old && !on.oldChanged,
          "the boolean true: where the display has no HDR10+, and the boolean stays true");
    Check(!Hdr10PlusAsDolbyVisionFor(Hdr10PlusModeOf(on.mode), true, true, true) &&
              Hdr10PlusAsDolbyVisionFor(Hdr10PlusModeOf(on.mode), true, true, false),
          "the boolean true taken over: the verdict of before, not to a display that takes HDR10+");
    // ... false, or no line at all (the default was not saved)
    const auto off = Migrate(deflt, deflt, false, Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(off.mode == deflt && !off.modeChanged && !off.old && !off.oldChanged, "the boolean false or not saved: off");
    // a value chosen in this build stays, and the boolean says what an earlier build can do of it
    const auto always = Migrate(static_cast<int>(HM::ALWAYS), deflt, false, Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(always.mode == static_cast<int>(HM::ALWAYS) && !always.modeChanged && always.old && always.oldChanged,
          "always stays, and the boolean is set true for an earlier build (which sends where the display has no HDR10+)");
    const auto alwaysOn = Migrate(static_cast<int>(HM::ALWAYS), deflt, true, Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(alwaysOn.mode == static_cast<int>(HM::ALWAYS) && !alwaysOn.modeChanged && !alwaysOn.oldChanged,
          "always with the boolean true: both stay");
    const auto noHdr = Migrate(static_cast<int>(HM::NO_HDR10PLUS), deflt, true, Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(noHdr.mode == static_cast<int>(HM::NO_HDR10PLUS) && !noHdr.modeChanged && noHdr.old && !noHdr.oldChanged,
          "where the display has no HDR10+ with the boolean true: both stay");
    // the user sets it back to off after the update: the boolean follows (CHadesTakeOverSync), so off stays at the next
    // start and in an earlier build
    const auto back = Migrate(deflt, deflt, Hdr10PlusModeToOld(deflt), Hdr10PlusModeFromOld, Hdr10PlusModeToOld);
    Check(back.mode == deflt && !back.modeChanged && !back.old, "set back to off: off at the next start");
    Check(!Hdr10PlusModeToOld(0) && Hdr10PlusModeToOld(1) && Hdr10PlusModeToOld(2) && !Hdr10PlusModeToOld(7),
          "the boolean kept for earlier builds: true for the two values that send HDR10+ as Dolby Vision");
  }
  std::printf("policy: %d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
