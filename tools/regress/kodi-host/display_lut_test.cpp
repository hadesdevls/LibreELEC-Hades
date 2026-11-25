/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the 3D LUT of each display (windowing/gbm/DisplayLut.h): which outputs take a LUT and
 * of which kind (the SDR that Kodi's own renderer draws too; never native Dolby Vision that the TV maps, HDR that the
 * display maps itself, nor the videos of the DRM PRIME decoder), a LUT whose file says it was made for another kind of
 * picture refused, the LUTs of each display
 * kept apart (one display's never applied to another), and the line of the player information.
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -o t display_lut_test.cpp && ./t
 */
#include "windowing/gbm/DisplayLut.h"

#include <cstdio>
#include <string>

using namespace KODI::WINDOWING::GBM::DISPLAY_LUT;
static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

int main()
{
  // --- which outputs take a LUT, and of which kind
  Check(TakeOf(Output::SDR_MAPPED).lut && TakeOf(Output::SDR_MAPPED).kind == Kind::SDR,
        "HDR or Dolby Vision mapped to SDR takes the LUT for SDR");
  Check(TakeOf(Output::SDR_AS_IS).lut && TakeOf(Output::SDR_AS_IS).kind == Kind::SDR,
        "an SDR video drawn by the bridge takes the LUT for SDR");
  Check(TakeOf(Output::HDR_MAPPED).lut && TakeOf(Output::HDR_MAPPED).kind == Kind::HDR,
        "HDR mapped to the display takes the LUT for HDR");
  Check(TakeOf(Output::DV_LOW_LATENCY).lut && TakeOf(Output::DV_LOW_LATENCY).kind == Kind::HDR,
        "Dolby Vision Low-Latency takes the LUT for HDR");
  Check(!TakeOf(Output::DV_TV_LED).lut && TakeOf(Output::DV_TV_LED).why == Reason::TV_LED,
        "native Dolby Vision that the TV maps takes none, and says why");
  Check(!TakeOf(Output::HDR_DISPLAY_MAPS).lut && TakeOf(Output::HDR_DISPLAY_MAPS).why == Reason::DISPLAY_MAPS,
        "HDR that the display maps itself takes none, and says why");
  Check(TakeOf(Output::KODI_SDR).lut && TakeOf(Output::KODI_SDR).kind == Kind::SDR,
        "an SDR picture that Kodi's own renderer draws (an SDR video, HDR tone mapped by its shaders) takes the LUT "
        "for SDR");
  Check(!TakeOf(Output::KODI_HDR).lut && TakeOf(Output::KODI_HDR).why == Reason::DISPLAY_MAPS &&
            TakeOf(Output::KODI_HDR).kind == Kind::HDR,
        "HDR that Kodi's renderer passes through to the display takes none: the display maps it, and says why");
  Check(!TakeOf(Output::PRIME).lut && TakeOf(Output::PRIME).why == Reason::PRIME_DECODER &&
            TakeOf(Output::PRIME).kind == Kind::SDR,
        "a video of the DRM PRIME decoder takes none (no pass of Kodi's video shaders), and says why");
  Check(!TakeOf(Output::NONE).lut && TakeOf(Output::NONE).why == Reason::NONE, "no video: nothing");

  // --- what a file says it was made for
  std::string why;
  Check(Fits(Kind::HDR, SIGNAL_UNKNOWN, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT709) == Reason::NONE,
        "HDR: a .cube that says nothing is taken for the kind it is chosen for");
  Check(Fits(Kind::HDR, SIGNAL_PQ, GAMUT_BT2020, GAMUT_BT709, GAMUT_BT709) == Reason::NONE,
        "HDR: a LUT of PQ in BT.2020 fits");
  Check(Fits(Kind::HDR, SIGNAL_SDR, GAMUT_BT709, GAMUT_BT709, GAMUT_BT709, &why) == Reason::OTHER_KIND &&
            why.find("SDR") != std::string::npos,
        "HDR: a LUT of SDR is refused, and why names SDR");
  Check(Fits(Kind::HDR, SIGNAL_HLG, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT709) == Reason::OTHER_KIND,
        "HDR: a LUT of HLG is refused (the output that the bridge maps is PQ)");
  Check(Fits(Kind::HDR, SIGNAL_PQ, GAMUT_P3_D65, GAMUT_BT709, GAMUT_BT709) == Reason::OTHER_KIND,
        "HDR: a LUT whose primaries are not BT.2020 is refused");
  Check(Fits(Kind::SDR, SIGNAL_PQ, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT709, &why) == Reason::OTHER_KIND &&
            why.find("PQ") != std::string::npos,
        "SDR: a LUT of PQ is refused, and why names PQ");
  Check(Fits(Kind::SDR, SIGNAL_HLG, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT709) == Reason::OTHER_KIND,
        "SDR: a LUT of HLG is refused");
  Check(Fits(Kind::SDR, SIGNAL_UNKNOWN, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT709) == Reason::NONE,
        "SDR: a .cube made (the setting says) for BT.709, on an SDR output in BT.709, fits");
  Check(Fits(Kind::SDR, SIGNAL_UNKNOWN, GAMUT_UNKNOWN, GAMUT_BT709, GAMUT_BT2020, &why) == Reason::OTHER_KIND &&
            why.find("setting") != std::string::npos,
        "SDR: a .cube made for BT.709 on an SDR output in BT.2020 is refused, and why says the setting said it");
  Check(Fits(Kind::SDR, SIGNAL_UNKNOWN, GAMUT_UNKNOWN, GAMUT_BT2020, GAMUT_BT2020) == Reason::NONE,
        "SDR: a .cube made (the setting says) for BT.2020 on an SDR output in BT.2020 fits");
  Check(Fits(Kind::SDR, SIGNAL_SDR, GAMUT_P3_D65, GAMUT_BT709, GAMUT_P3_D65) == Reason::NONE,
        "SDR: a file that says DCI-P3 fits an SDR output in DCI-P3 whatever the setting says");
  Check(Fits(Kind::SDR, SIGNAL_SDR, GAMUT_BT709, GAMUT_P3_D65, GAMUT_P3_D65, &why) == Reason::OTHER_KIND &&
            why.find("its file says so") != std::string::npos,
        "SDR: a file that says BT.709 is refused on an SDR output in DCI-P3, whatever the setting says");
  Check(GamutOfSetting(0) == GAMUT_BT709 && GamutOfSetting(1) == GAMUT_BT2020 && GamutOfSetting(2) == GAMUT_P3_D65 &&
            GamutOfSetting(7) == GAMUT_BT709,
        "the gamut of the settings of SDR (0 BT.709, 1 BT.2020, 2 DCI-P3; anything else BT.709)");

  // --- the LUTs of each display
  const std::string lg = "LG Electronics / LG TV SSCR2", jvc = "JVC KENWOOD / DLA-NZ8";
  CDisplayLutMemory m;
  Check(m.Text().empty() && m.Of(lg, Kind::SDR).empty() && !m.Knows(lg), "nothing noted at first");
  Check(m.Remember(lg, Kind::HDR, "/storage/luts/lg-hdr.cube"), "the LUT for HDR of the TV noted");
  Check(!m.Remember(lg, Kind::HDR, "/storage/luts/lg-hdr.cube"), "the same again: nothing changes");
  Check(m.Remember(jvc, Kind::SDR, "/storage/luts/jvc-sdr.3dlut"), "the LUT for SDR of the projector noted");
  Check(m.Of(lg, Kind::HDR) == "/storage/luts/lg-hdr.cube" && m.Of(lg, Kind::SDR).empty() &&
            m.Of(jvc, Kind::SDR) == "/storage/luts/jvc-sdr.3dlut" && m.Of(jvc, Kind::HDR).empty(),
        "each display has its own LUTs of each kind");
  Check(PathFor(m, jvc, Kind::HDR, "/storage/luts/lg-hdr.cube").empty(),
        "the projector has no LUT for HDR: the TV's, in the setting, is not applied to it");
  Check(PathFor(m, lg, Kind::HDR, "") == "/storage/luts/lg-hdr.cube", "the TV's own LUT, whatever the setting");
  Check(PathFor(m, "Unknown / Display", Kind::SDR, "/x.cube").empty(),
        "a display not seen has no LUT, whatever the setting");
  Check(PathFor(m, "", Kind::SDR, "/x.cube") == "/x.cube",
        "a display not known by its EDID (no key): the setting's LUT");
  CDisplayLutMemory n;
  n.Parse(m.Text() + "garbage line\nkey\tother\t/p\n\t\tx\nkey\tsdr\t\n" + "a\tsdr\tb\tc\n");
  Check(n.Text() == m.Text(), "the file read back as it was written; lines that are not of it left out");
  n.Parse(std::string("x\tsdr\t/a.cube\r\n"));
  Check(n.Of("x", Kind::SDR) == "/a.cube", "a line that ends in CR LF");
  Check(m.Remember(lg, Kind::HDR, ""), "the LUT for HDR of the TV taken away");
  Check(m.Of(lg, Kind::HDR).empty() && m.Of(jvc, Kind::SDR) == "/storage/luts/jvc-sdr.3dlut",
        "... and nothing else changes");
  Check(!m.Remember(lg, Kind::SDR, "/a\tb.cube") && !m.Remember("", Kind::SDR, "/a.cube"),
        "a path with a tab, or no display: not noted");
  CDisplayLutMemory big;
  for (size_t i = 0; i <= CDisplayLutMemory::LIMIT; ++i)
    big.Remember("display " + std::to_string(i), Kind::SDR, "/lut" + std::to_string(i) + ".cube");
  Check(!big.Knows("display 0") && big.Knows("display 1") &&
            big.Knows("display " + std::to_string(CDisplayLutMemory::LIMIT)),
        "the displays used last are kept, LIMIT of them");

  // --- the line of the player information
  Check(Line("/storage/luts/a.cube", Reason::APPLIED, "x") == "a.cube", "applied: the name of the file");
  Check(Line("/storage/luts/a.cube", Reason::TV_LED, "not applied: Dolby Vision that the TV maps") ==
            "not applied: Dolby Vision that the TV maps",
        "not applied: why (the file is in kodi.log)");
  Check(Line("/storage/luts/a.cube", Reason::OFF, "x").empty() && Line("", Reason::APPLIED, "x").empty() &&
            Line("/a.cube", Reason::NONE, "x").empty(),
        "off, no LUT, or nothing to say: no line");
  // Hades: a LUT being read off the render thread, or its table put on the GPU: its word, not a fault
  Check(Line("/storage/luts/a.3dlut", Reason::LOADING, "loading") == "loading",
        "a LUT being read or put on the GPU: the player information says loading");
  Check(Reason::LOADING != Reason::UNREADABLE && Reason::LOADING != Reason::GPU_FAILED &&
            static_cast<int>(Reason::LOADING) == static_cast<int>(Reason::GPU_FAILED) + 1,
        "loading is a reason of its own, after those of before (the texts of the window system are in this order)");
  Check(Line("/l/LG C5 #1 2026-11-24 22-00 2.2 F-S XYZLUT+MTX.cube", Reason::APPLIED, "") == "LG C5 #1 2026-...XYZLUT+MTX.cube" &&
            Line("/l/LG C5 #1 2026-11-24 22-00 2.2 F-S XYZLUT+MTX.cube", Reason::APPLIED, "").size() == LINE_NAME,
        "a long name: its first and last characters, with ... between them, in one line of the row");
  {
    // a name of letters of two bytes in UTF-8 (U+00E1), with one of one byte among them: cut between whole letters
    std::string accented;
    for (int i = 0; i < 20; ++i)
      accented += i == 10 ? std::string("x") : std::string("\xc3\xa1");
    accented += ".cube";
    const std::string cut = ShortName(accented);
    bool whole = cut.size() <= LINE_NAME && cut.find("...") != std::string::npos;
    for (size_t i = 0; i < cut.size(); ++i)
    {
      const auto c = static_cast<unsigned char>(cut[i]);
      if (c == 0xC3 && (i + 1 >= cut.size() || static_cast<unsigned char>(cut[i + 1]) != 0xA1))
        whole = false;
      if (c == 0xA1 && (i == 0 || static_cast<unsigned char>(cut[i - 1]) != 0xC3))
        whole = false;
    }
    Check(whole && ShortName(std::string(LINE_NAME, 'a')) == std::string(LINE_NAME, 'a'),
          "a name cut between whole UTF-8 letters; one of LINE_NAME kept whole");
  }
  Check(FileName("smb://nas/luts/b.3dl") == "b.3dl" && FileName("C:\\luts\\c.cube") == "c.cube" &&
            FileName("d.cube") == "d.cube",
        "the name of a file without its folders");

  std::printf("%d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
