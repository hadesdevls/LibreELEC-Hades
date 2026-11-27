// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// picture_defaults_test.cpp: the settings that change the picture of an HDR video from the film
// (xbmc/settings/PictureDefaults.h of the Kodi tree): which count for which video, the words of the line of
// the overview page of the player information, and the line of kodi.log. Prints a line per failed check and
// PASSED or FAILED (n) last.
#include "settings/PictureDefaults.h"

#include <cstdio>
#include <map>
#include <string>

using namespace KODI::SETTINGS::PICTURE;

static int failed = 0;
static void expect(bool ok, const char* what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// the settings of a Kodi as a map: id -> (value, default value); an id not in it is a setting that this
// Kodi does not have
struct Kodi
{
  std::map<std::string, std::pair<int, int>> s;
  std::optional<Value> operator()(const char* id) const
  {
    const auto it = s.find(id);
    if (it == s.end())
      return std::nullopt;
    return Value{it->second.first, it->second.first == it->second.second};
  }
};

static Kodi Defaults()
{
  Kodi k;
  k.s = {{"videoscreen.hadesdvnotrims", {0, 0}},     {"videoscreen.hadesdvcm40", {0, 0}},
         {"videoscreen.hadesdvcm40manual", {0, 0}},  {"videoscreen.hadesdvtransport", {2, 2}},
         {"videoscreen.hadesdvcomposer", {0, 0}},    {"videoscreen.hadesdvstepmode", {0, 0}},
         {"videoscreen.hadesdvllalways", {0, 0}},    {"videoscreen.hadesdvllmanual", {0, 0}},
         {"videoscreen.hadesdvllblackedid", {1, 1}}, {"videoscreen.hadeshdrdepth", {0, 0}},
         {"videoscreen.hadesmaxbpc", {0, 0}},        {"videoscreen.hadeshdr420", {0, 0}},
         {"videoscreen.hadesdvhdr10map", {0, 0}},    {"videoscreen.hadesdvtrimpass", {0, 0}},
         {"videoscreen.hadeshdr10adapt", {0, 0}},    {"videoscreen.hadeshdr10scenes", {0, 0}},
         {"videoscreen.hadeshdr10limiton", {0, 0}},  {"videoscreen.hadesdvfromhdr10", {0, 0}},
         {"videoscreen.hadesdvfromhdr10plusmode", {0, 0}}, {"videoscreen.hadeshlgasdv", {0, 0}},
         // (1.2.3: the boolean that videoscreen.hadesdvfromhdr10plusmode took over, kept hidden for earlier builds)
         {"videoscreen.hadesdvfromhdr10plus", {0, 0}}};
  return k;
}

static std::string Joined(const std::vector<Changed>& c)
{
  std::string s;
  for (const Changed& x : c)
    s += (s.empty() ? "" : ",") + x.setting + "=" + std::to_string(x.value);
  return s;
}

int main()
{
  const auto read = [](const Kodi& k) { return [&k](const char* id) { return k(id); }; };

  // every setting at its default: nothing, for every kind of video
  {
    const Kodi k = Defaults();
    for (unsigned scope : {unsigned(DV_NATIVE), unsigned(DV_HDR10), unsigned(HDR10), unsigned(HLG),
                           unsigned(ANY_HDR)})
      expect(ChangedFor(scope, read(k)).empty(), "defaults: nothing counts");
    expect(Line(ChangedFor(ANY_HDR, read(k)), nullptr).empty(), "defaults: no line");
  }

  // lvasan's three (trims left out, v4.0 added, the peak of v4.0 by hand), native Dolby Vision
  {
    Kodi k = Defaults();
    k.s["videoscreen.hadesdvnotrims"].first = 1;
    k.s["videoscreen.hadesdvcm40"].first = 1;
    k.s["videoscreen.hadesdvcm40manual"].first = 1;
    const auto c = ChangedFor(DV_NATIVE, read(k));
    expect(Joined(c) == "videoscreen.hadesdvnotrims=1,videoscreen.hadesdvcm40=1,"
                        "videoscreen.hadesdvcm40manual=1",
           "three settings of native Dolby Vision, in order");
    expect(Line(c, nullptr) == "trims left out, v4.0 added, v4.0 peak by hand",
           "the line names the three");
    // they change nothing of an HDR10 video, nor of Dolby Vision rebuilt as HDR10
    expect(ChangedFor(HDR10, read(k)).empty(), "not named for HDR10");
    expect(ChangedFor(DV_HDR10, read(k)).empty(), "not named for Dolby Vision as HDR10");
    const std::string log = LogLine(KindName(DV_NATIVE), c);
    expect(log.find("Picture settings: for this native Dolby Vision video 3 settings change") == 0,
           "kodi.log: the kind and the count");
    expect(log.find("videoscreen.hadesdvcm40manual=1 (v4.0 peak by hand)") != std::string::npos,
           "kodi.log: each setting with its value and words");
  }

  // values: only those that change the picture count
  {
    Kodi k = Defaults();
    k.s["videoscreen.hadesdvstepmode"].first = 1; // Never: the picture is the same
    expect(ChangedFor(DV_NATIVE, read(k)).empty(), "step-down Never does not count");
    k.s["videoscreen.hadesdvstepmode"].first = 3; // Level 2: lower fidelity
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "fidelity lowered by hand",
           "step-down Level 2 counts");
    k = Defaults();
    k.s["videoscreen.hadesdvcm40"].first = 3;
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "v4.0 left out", "v2.9 only says left out");
    k.s["videoscreen.hadesdvtransport"].first = 0;
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "v4.0 left out, sent in IPT",
           "the transport IPT");
    k.s["videoscreen.hadesdvtransport"].first = 1;
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "v4.0 left out, converted to BT.2020",
           "the transport converted");
    // a boolean whose default is on counts when it is off
    k = Defaults();
    k.s["videoscreen.hadesdvllblackedid"].first = 0;
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "TV black not used", "a default of on");
  }

  // a setting that this Kodi does not have is passed over; one that it has is read (the settings of the
  // form of the metadata come with another front)
  {
    Kodi k = Defaults();
    expect(ChangedFor(DV_NATIVE, read(k)).empty(), "no videoscreen.hadesdvl5form here");
    k.s["videoscreen.hadesdvl5form"] = {2, 0};
    k.s["videoscreen.hadesdvlevelorder"] = {1, 0}; // on, its default off
    expect(Line(ChangedFor(DV_NATIVE, read(k)), nullptr) == "active area changed, blocks reordered",
           "the form of the metadata, where there");
  }

  // the line: three words and how many more; the same words once; the words of the interface
  {
    Kodi k = Defaults();
    k.s["videoscreen.hadesdvnotrims"].first = 1;
    k.s["videoscreen.hadesdvcm40"].first = 2;
    k.s["videoscreen.hadesdvtransport"].first = 0;
    k.s["videoscreen.hadeshdrdepth"].first = 8;
    k.s["videoscreen.hadesmaxbpc"].first = 8; // the same words as the one before
    const auto c = ChangedFor(DV_NATIVE, read(k));
    expect(c.size() == 5, "five settings count");
    expect(Words(c).size() == 4, "four words: two settings say the same");
    expect(Line(c, nullptr) == "trims left out, v4.0 added, sent in IPT +1", "three words and +1");
    const auto spanish = [](uint32_t id, const std::string& en) {
      return id == 54420 ? std::string("sin trims") : en;
    };
    expect(Line(c, spanish).find("sin trims, ") == 0, "the words of the interface");
  }

  // the scope: what is playing
  {
    expect(ScopeOf("dolbyvision", "native", "dv") == DV_NATIVE, "native Dolby Vision");
    expect(ScopeOf("dolbyvision", "", "dv") == DV_NATIVE, "Dolby Vision told dv");
    expect(ScopeOf("dolbyvision", "hdr10", "pq") == DV_HDR10, "Dolby Vision as HDR10");
    expect(ScopeOf("dolbyvision", "sdr", "sdr") == 0, "Dolby Vision as SDR: not these settings");
    expect(ScopeOf("hdr10", "", "pq") == HDR10, "HDR10");
    expect(ScopeOf("hdr10plus", "", "hdr10plus") == HDR10, "HDR10+");
    expect(ScopeOf("hdr10", "native", "dv") == (HDR10 | DV_NATIVE), "HDR10 as Dolby Vision");
    expect(ScopeOf("hlg", "", "hlg") == HLG, "HLG");
    expect(ScopeOf("", "", "sdr") == 0, "SDR");
    Kodi k = Defaults();
    k.s["videoscreen.hadesdvfromhdr10"].first = 1;
    k.s["videoscreen.hadesdvnotrims"].first = 1;
    expect(Line(ChangedFor(ScopeOf("hdr10", "native", "dv", "measured"), read(k)), nullptr) ==
               "trims left out, made Dolby Vision",
           "HDR10 made Dolby Vision by its setting: both kinds");
    // (up to 1.2.3 "made Dolby Vision" was named here too: a setting that made nothing of the video)
    expect(ChangedFor(ScopeOf("hdr10", "", "pq"), read(k)).empty(),
           "HDR10 sent as HDR10: the setting that would make it Dolby Vision made nothing");
    k = Defaults();
    k.s["videoscreen.hadesdvhdr10map"].first = 1;
    expect(Line(ChangedFor(ScopeOf("dolbyvision", "hdr10", "pq"), read(k)), nullptr) ==
               "mapped to your peak",
           "Dolby Vision as HDR10 mapped to the display");
    expect(ChangedFor(ScopeOf("dolbyvision", "native", "dv"), read(k)).empty(),
           "the mapping to the display is not of native output");
    k = Defaults();
    k.s["videoscreen.hadeshdr10adapt"].first = 2;
    expect(Line(ChangedFor(HDR10, read(k)), nullptr) == "adapted to your TV", "HDR10 adapted, any value");
  }

  // the video's own choice of its output ("HDR output (this video)") overrides the settings: a setting
  // that it overrode is neither named, nor counted, nor put back by the button (the ids are what the
  // button resets)
  {
    const auto ids = [](const std::vector<Changed>& c)
    {
      std::string s;
      for (const Changed& x : c)
        s += (s.empty() ? "" : ";") + x.setting;
      return s;
    };
    // Hokum, 2026-11-26: "HDR10 as Dolby Vision, measured" on, the video played without Dolby Vision
    Kodi k = Defaults();
    k.s["videoscreen.hadesdvfromhdr10"].first = 1;
    unsigned sc = ScopeOf("hdr10", "", "pq", "", false);
    expect(ChangedFor(sc, read(k)).empty(), "HDR10, choice without Dolby Vision: the setting is not named");
    expect(Words(ChangedFor(sc, read(k))).empty(), "... nor counted");
    expect(ids(ChangedFor(sc, read(k))).empty(), "... nor put back");
    // the video's choice "measured" made it Dolby Vision: not the setting, on or off
    sc = ScopeOf("hdr10", "native", "dv", "measured", false);
    expect(ChangedFor(sc, read(k)).empty(), "HDR10 measured by the video's choice: the setting is not named");
    k.s["videoscreen.hadesdvnotrims"].first = 1;
    expect(ids(ChangedFor(sc, read(k))) == "videoscreen.hadesdvnotrims",
           "... the settings of the Dolby Vision that goes out still are");
    k = Defaults();
    k.s["videoscreen.hadesdvfromhdr10"].first = 1;
    expect(ids(ChangedFor(ScopeOf("hdr10", "native", "dv", "measured", true), read(k))) ==
               "videoscreen.hadesdvfromhdr10",
           "HDR10 measured by the setting, choice Automatic: named");
    expect(ChangedFor(ScopeOf("hdr10", "native", "dv", "measuredauto", true), read(k)).empty(),
           "HDR10 measured because the output cannot send HDR10: not the setting");
    expect(ChangedFor(ScopeOf("hdr10", "", "pq", "", true), read(k)).empty(),
           "HDR10, choice Automatic, display without Dolby Vision: the setting made nothing");
    // HDR10+: its own setting, not the one of HDR10, made it Dolby Vision (1.2.3: the setting of three values, which
    // took over the boolean of before; the boolean, kept hidden and true for earlier builds, is never named: the button
    // puts the setting back, and the boolean follows it)
    k.s["videoscreen.hadesdvfromhdr10plusmode"].first = 1;
    k.s["videoscreen.hadesdvfromhdr10plus"].first = 1;
    expect(ids(ChangedFor(ScopeOf("hdr10plus", "native", "dv", "hdr10plus", true), read(k))) ==
               "videoscreen.hadesdvfromhdr10plusmode",
           "HDR10+ made Dolby Vision by its setting: that one only");
    expect(ids(ChangedFor(ScopeOf("hdr10plus", "native", "dv", "hdr10pluscurve", true), read(k))) ==
               "videoscreen.hadesdvfromhdr10plusmode",
           "... also mapped by its HDR10+ curve");
    expect(ChangedFor(ScopeOf("hdr10plus", "native", "dv", "hdr10plus", false), read(k)).empty(),
           "HDR10+, the video's choice Dolby Vision: not the setting");
    expect(ChangedFor(ScopeOf("hdr10plus", "", "hdr10plus", "", true), read(k)).empty(),
           "HDR10+ sent as HDR10+: the setting made nothing");
    // (1.2.3) Always makes the picture from the video's HDR10+ the same way, also for a display that takes HDR10+: it
    // counts the same, with the same words, and kodi.log says its value
    k.s["videoscreen.hadesdvfromhdr10plusmode"].first = 2;
    const unsigned madeHdr10Plus = ScopeOf("hdr10plus", "native", "dv", "hdr10plus", true);
    const auto always = ChangedFor(madeHdr10Plus, read(k));
    expect(ids(always) == "videoscreen.hadesdvfromhdr10plusmode", "HDR10+ made Dolby Vision by Always: named");
    expect(Line(always, nullptr) == "made Dolby Vision", "... with the words of the other value");
    expect(LogLine(KindName(madeHdr10Plus), always).find("videoscreen.hadesdvfromhdr10plusmode=2 (made Dolby Vision)") !=
               std::string::npos,
           "... and kodi.log says its value");
    expect(ids(ChangedFor(ScopeOf("hdr10plus", "native", "dv", "hdr10pluscurve", true), read(k))) ==
               "videoscreen.hadesdvfromhdr10plusmode",
           "Always, mapped by its HDR10+ curve: named");
    expect(ChangedFor(ScopeOf("hdr10plus", "native", "dv", "hdr10plus", false), read(k)).empty(),
           "Always, the video's choice Dolby Vision: not the setting");
    expect(ChangedFor(ScopeOf("hdr10plus", "", "hdr10plus", "", true), read(k)).empty(),
           "Always, sent as HDR10+ all the same (no native Dolby Vision): the setting made nothing");
    expect(ids(ChangedFor(ScopeOf("hdr10", "native", "dv", "measured", true), read(k))) ==
               "videoscreen.hadesdvfromhdr10",
           "Always: not named for an HDR10 video measured, only the setting of HDR10");
    // HLG
    k = Defaults();
    k.s["videoscreen.hadeshlgasdv"].first = 1;
    expect(ids(ChangedFor(ScopeOf("hlg", "native", "dv", "measuredhlg", true), read(k))) ==
               "videoscreen.hadeshlgasdv",
           "HLG made Dolby Vision by its setting: named");
    expect(ChangedFor(ScopeOf("hlg", "native", "dv", "measuredhlg", false), read(k)).empty(),
           "HLG, the video's choice Dolby Vision: not the setting");
    expect(ChangedFor(ScopeOf("hlg", "", "hlg", "", false), read(k)).empty(),
           "HLG, the video's choice as Kodi plays it: not the setting");
    // as SDR, the video's choice or "HDR as SDR": no setting of HDR output reaches its picture
    k = Defaults();
    k.s["videoscreen.hadesdvfromhdr10"].first = 1;
    k.s["videoscreen.hadeshdr10adapt"].first = 2;
    k.s["videoscreen.hadeshdrdepth"].first = 8;
    expect(ScopeOf("hdr10", "", "sdr", "", false) == 0, "HDR10 as SDR: no scope");
    expect(ScopeOf("hlg", "", "sdr", "", true) == 0, "HLG as SDR: no scope");
    expect(ScopeOf("dolbyvision", "", "sdr", "", false) == 0, "Dolby Vision as SDR: no scope");
    expect(ChangedFor(ScopeOf("hdr10", "", "sdr", "", false), read(k)).empty(), "HDR10 as SDR: nothing named");
    // the origin is read only for a video that goes out as Dolby Vision, and never for the video's own
    // Dolby Vision
    expect(ScopeOf("dolbyvision", "native", "dv", "measured", true) == DV_NATIVE,
           "Dolby Vision measured in place of its own: no setting made it Dolby Vision");
    expect(ScopeOf("hdr10", "", "pq", "measured", true) == HDR10, "an origin without Dolby Vision out");
  }

  // kodi.log where nothing counts
  expect(LogLine("HDR10", {}) ==
             "Picture settings: for this HDR10 video every setting that changes the picture is at its "
             "default",
         "kodi.log: nothing");

  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
