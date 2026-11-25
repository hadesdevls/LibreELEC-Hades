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
         {"videoscreen.hadesdvfromhdr10plus", {0, 0}}, {"videoscreen.hadeshlgasdv", {0, 0}}};
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
    expect(Line(ChangedFor(ScopeOf("hdr10", "native", "dv"), read(k)), nullptr) ==
               "trims left out, made Dolby Vision",
           "HDR10 as Dolby Vision: both kinds");
    expect(Line(ChangedFor(ScopeOf("hdr10", "", "pq"), read(k)), nullptr) == "made Dolby Vision",
           "HDR10 as HDR10: only its own");
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
