/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (tools/regress/mvc): host test that pins which output STEREO::Choose()
 * (xbmc/windowing/gbm/drm/DRMStereo.h) gives a 3D video for the picture rates of film and video,
 * the source layouts and displays with and without a 3D mode at twice the refresh rate, and where
 * the refresh rate is doubled (each picture shown twice).
 *
 * The header is included as it is (it is the pure logic, without DRM or Kodi). What Kodi does
 * around it is copied here from where it is, and only that:
 *  - the drawing arrangement of a source: CStereoscopicsManager maps "left_right"/"right_left" to
 *    SPLIT_VERTICAL and "top_bottom"/"bottom_top" to SPLIT_HORIZONTAL (StereoscopicsManager.cpp
 *    52-55); an MVC film (two decoded views) is packed "left_right" (MultiviewChoice.h:53), so it
 *    is drawn as a side-by-side source is; a full and a half side-by-side source are both drawn
 *    side by side: Choose() is not told the layout of the source;
 *  - CWinSystemGbm::GetStereoOutputArrangement() (WinSystemGbm.cpp:911-930): side by side is
 *    drawn one above the other where PrefersTopAndBottom() (that gives frame packing);
 *  - CWinSystemGbm::GetStereoOffers() (WinSystemGbm.cpp:748-796): Allowed::otherSize is "no
 *    whitelist", doubleRate is otherSize or the whitelist's double refresh option, pictureRate is
 *    the video's frame rate while a video plays (0 otherwise); on nvidia-drm the half layouts that
 *    the EDID has for a timing without a mode are told by the InfoFrame.
 * Kodi's mode is the 1080p mode of the video's rate (Kodi's refresh rate adjustment), unless a
 * case says otherwise.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Werror -I <kodi tree>/xbmc/windowing/gbm/drm stereo_rate_test.cpp
 * Prints the table, "N checks, M failures" and PASSED or FAILED (exit 1).
 */
#include "DRMStereo.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM::STEREO;

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

// ---- CTA-861 timings, as the kernel lists them (clock in kHz; 1000/1001 rates with the clock rounded)
static Mode Timing(int width, int height, int nominal, bool video, bool interlaced = false)
{
  Mode m;
  m.hdisplay = static_cast<uint16_t>(width);
  m.vdisplay = static_cast<uint16_t>(height);
  uint32_t clock = 74250;
  if (height == 1080)
  {
    m.vtotal = 1125;
    m.htotal = nominal == 24 ? 2750 : (nominal == 25 || nominal == 50) ? 2640 : 2200;
    clock = (!interlaced && (nominal == 50 || nominal == 60)) ? 148500 : 74250;
    if (interlaced)
      m.flags |= FLAG_INTERLACE;
  }
  else // 720p
  {
    m.vtotal = 750;
    m.htotal = nominal == 50 ? 1980 : 1650;
  }
  m.clock = video ? (clock == 148500 ? 148352 : 74176) : clock;
  // the sync positions only have to be the same for the 2D mode and its 3D forms
  m.hsyncStart = static_cast<uint16_t>(width + 88);
  m.hsyncEnd = static_cast<uint16_t>(width + 132);
  m.vsyncStart = static_cast<uint16_t>(height + 4);
  m.vsyncEnd = static_cast<uint16_t>(height + 9);
  return m;
}

static Mode As(Mode m, uint32_t flag)
{
  m.flags |= flag;
  return m;
}

struct Rate
{
  int nominal;
  bool video;
  const char* name;
};
// the picture rates of film and video (Kodi's mode is the 1080p mode of the rate)
static const Rate RATES[] = {{24, true, "23.976"}, {24, false, "24"},   {25, false, "25"},
                             {30, true, "29.97"},  {30, false, "30"},   {50, false, "50"},
                             {60, true, "59.94"},  {60, false, "60"}};

enum class Src
{
  FRAME_PACKED_MVC,
  SBS_HALF,
  SBS_FULL,
  TAB,
};
static const char* SrcName(Src s)
{
  switch (s)
  {
    case Src::FRAME_PACKED_MVC:
      return "MVC (frame packed)";
    case Src::SBS_HALF:
      return "SBS half";
    case Src::SBS_FULL:
      return "SBS full";
    default:
      return "TAB";
  }
}

struct Display
{
  std::string name;
  std::vector<Mode> modes;
  //! nvidia-drm: the half layouts told by the InfoFrame, by a predicate on the 2D mode (the EDID)
  std::function<bool(const Mode&, Layout)> infoFrame;
};

// the 2D modes of an HDMI display: 1080p at 24, 25, 30, 50, 60 (and 1000/1001), 720p50/60, 1080i50/60
static std::vector<Mode> Modes2D()
{
  std::vector<Mode> v;
  for (const Rate& r : RATES)
    v.push_back(Timing(1920, 1080, r.nominal, r.video));
  v.push_back(Timing(1280, 720, 50, false));
  v.push_back(Timing(1280, 720, 60, true));
  v.push_back(Timing(1280, 720, 60, false));
  v.push_back(Timing(1920, 1080, 50, false, true));
  v.push_back(Timing(1920, 1080, 60, true, true));
  v.push_back(Timing(1920, 1080, 60, false, true));
  return v;
}

// the 3D forms that HDMI 1.4 makes mandatory for a 3D display (3D_present): 1080p24 and 720p50/60
// frame packing and top-and-bottom, 1080i50/60 side-by-side (half)
static void AddMandatory(std::vector<Mode>& v)
{
  const std::vector<Mode> base = v;
  for (const Mode& m : base)
  {
    const double r = RefreshRate(m);
    const bool i = m.flags & FLAG_INTERLACE;
    if ((m.vdisplay == 1080 && !i && std::fabs(r - 24) < 0.1) || (m.vdisplay == 720))
    {
      v.push_back(As(m, FLAG_3D_FRAME_PACKING));
      v.push_back(As(m, FLAG_3D_TOP_AND_BOTTOM));
    }
    if (m.vdisplay == 1080 && i)
      v.push_back(As(m, FLAG_3D_SIDE_BY_SIDE_HALF));
  }
}

// the half layouts at 1080p of the given nominal rates (both clocks where the rate has two)
static void AddHalf1080p(std::vector<Mode>& v, std::initializer_list<int> nominals, bool video, bool plain)
{
  const std::vector<Mode> base = v;
  for (const Mode& m : base)
  {
    if (m.vdisplay != 1080 || (m.flags & (FLAG_INTERLACE | FLAG_3D_MASK)))
      continue;
    const double r = RefreshRate(m);
    for (int n : nominals)
    {
      const bool isPlain = std::fabs(r - n) < 0.001;
      const bool isVideo = std::fabs(r - n * 1000.0 / 1001.0) < 0.01;
      if ((plain && isPlain) || (video && isVideo))
      {
        v.push_back(As(m, FLAG_3D_TOP_AND_BOTTOM));
        v.push_back(As(m, FLAG_3D_SIDE_BY_SIDE_HALF));
      }
    }
  }
}

static int Find(const std::vector<Mode>& modes, int height, double rate, bool interlaced = false)
{
  for (std::size_t i = 0; i < modes.size(); ++i)
  {
    const Mode& m = modes[i];
    if (m.vdisplay == height && !(m.flags & FLAG_3D_MASK) &&
        ((m.flags & FLAG_INTERLACE) != 0) == interlaced && std::fabs(RefreshRate(m) - rate) < 0.01)
      return static_cast<int>(i);
  }
  return -1;
}

static double RateOf(const Rate& r)
{
  return r.video ? r.nominal * 1000.0 / 1001.0 : r.nominal;
}

static std::vector<Offer> OffersOf(const Display& d)
{
  std::vector<Offer> offers = Offers(d.modes);
  if (d.infoFrame)
  {
    for (Offer& o : offers)
    {
      o.infoFrameTopAndBottom = o.topAndBottom < 0 && d.infoFrame(d.modes[o.base], Layout::TOP_AND_BOTTOM);
      o.infoFrameSideBySide = o.sideBySide < 0 && d.infoFrame(d.modes[o.base], Layout::SIDE_BY_SIDE_HALF);
    }
  }
  return offers;
}

// what Kodi sends for a source: the arrangement (GetStereoOutputArrangement()) and the choice
static Choice Output(const std::vector<Offer>& offers, int base, Src src, const Allowed& allowed)
{
  Arrangement a = src == Src::TAB ? Arrangement::TOP_AND_BOTTOM : Arrangement::SIDE_BY_SIDE;
  if (a == Arrangement::SIDE_BY_SIDE && PrefersTopAndBottom(offers, base, allowed))
    a = Arrangement::TOP_AND_BOTTOM;
  return Choose(offers, base, a, allowed);
}

static std::string Describe(const Display& d, const Choice& c, int base)
{
  if (c.layout == Layout::NONE)
    return "2D";
  const char* l = c.layout == Layout::FRAME_PACKING  ? "FP"
                  : c.layout == Layout::TOP_AND_BOTTOM ? "TAB"
                  : c.layout == Layout::SIDE_BY_SIDE_HALF ? "SBS"
                                                          : "OTHER";
  const Mode& m = d.modes[c.base];
  char s[96];
  std::snprintf(s, sizeof(s), "%s%s %dx%d@%.3f%s", l, c.infoFrame ? "-IF" : "", m.hdisplay, m.vdisplay,
                RefreshRate(m), std::fabs(RefreshRate(m) - RefreshRate(d.modes[base])) >= 0.01 ? " x2" : "");
  return s;
}

// a choice must be consistent with the display: its mode sends the timing of its base in its layout
// (or is the base, told by the InfoFrame), and its refresh rate is Kodi's or exactly twice it
static void Consistent(const Display& d, const Choice& c, int base, const std::string& what)
{
  if (c.layout == Layout::NONE)
  {
    Check(c.base == -1 && c.mode == -1 && !c.infoFrame, what + ": 2D is an empty choice");
    return;
  }
  Check(c.base >= 0 && c.mode >= 0 && c.base < (int)d.modes.size() && c.mode < (int)d.modes.size(),
        what + ": indices in the list");
  if (c.base < 0 || c.mode < 0 || c.base >= (int)d.modes.size() || c.mode >= (int)d.modes.size())
    return;
  if (c.infoFrame)
    Check(c.mode == c.base && LayoutOf(d.modes[c.mode].flags) == Layout::NONE, what + ": InfoFrame in the 2D mode");
  else
    Check(LayoutOf(d.modes[c.mode].flags) == c.layout && SameTiming(d.modes[c.mode], d.modes[c.base]),
          what + ": the 3D mode of the base timing");
  const double own = RefreshRate(d.modes[base]);
  const double got = RefreshRate(d.modes[c.base]);
  Check(std::fabs(got - own) < 0.01 || std::fabs(got - 2 * own) < 0.01, what + ": Kodi's rate or exactly twice it");
}

int main()
{
  // ---- the displays
  Display none{"2D only", Modes2D(), nullptr};
  Display mand{"HDMI 1.4 mandatory 3D only", Modes2D(), nullptr};
  AddMandatory(mand.modes);
  Display p5060{"mandatory + half layouts at 1080p50/59.94/60", mand.modes, nullptr};
  AddHalf1080p(p5060.modes, {50, 60}, true, true);
  Display p2530{"as above + half layouts at 1080p25/29.97/30", p5060.modes, nullptr};
  AddHalf1080p(p2530.modes, {25, 30}, true, true);
  // nvidia-drm: no modes for the half layouts; the EDID (here) has them for 1080p50/60 and 1080p24;
  // frame packing modes at 1080p24 (nvidia-open-kernel-modules-9001)
  Display nvidia{"nvidia-drm: FP modes at 1080p24, half layouts by InfoFrame at 1080p24/50/60", Modes2D(), nullptr};
  {
    const std::vector<Mode> b = nvidia.modes;
    for (const Mode& m : b)
      if (m.vdisplay == 1080 && !(m.flags & FLAG_INTERLACE) && std::fabs(RefreshRate(m) - 24) < 0.1)
        nvidia.modes.push_back(As(m, FLAG_3D_FRAME_PACKING));
    nvidia.infoFrame = [](const Mode& m, Layout) {
      const double r = RefreshRate(m);
      return m.vdisplay == 1080 && !(m.flags & FLAG_INTERLACE) &&
             (std::fabs(r - 24) < 0.1 || std::fabs(r - 50) < 0.1 || std::fabs(r - 60) < 0.1);
    };
  }

  // ---- the pinned table: for each display and rate, what a source drawn side by side (MVC, SBS
  // half, SBS full) and a top-and-bottom source get
  struct Row
  {
    const Display* d;
    const char* rate;
    const char* sideBySide;
    const char* topAndBottom;
  };
  const Row table[] = {
      {&none, "23.976", "2D", "2D"},
      {&none, "24", "2D", "2D"},
      {&none, "25", "2D", "2D"},
      {&none, "29.97", "2D", "2D"},
      {&none, "30", "2D", "2D"},
      {&none, "50", "2D", "2D"},
      {&none, "59.94", "2D", "2D"},
      {&none, "60", "2D", "2D"},
      {&mand, "23.976", "FP 1920x1080@23.976", "FP 1920x1080@23.976"},
      {&mand, "24", "FP 1920x1080@24.000", "FP 1920x1080@24.000"},
      {&mand, "25", "2D", "2D"},
      {&mand, "29.97", "2D", "2D"},
      {&mand, "30", "2D", "2D"},
      {&mand, "50", "2D", "2D"},
      {&mand, "59.94", "2D", "2D"},
      {&mand, "60", "2D", "2D"},
      {&p5060, "23.976", "FP 1920x1080@23.976", "FP 1920x1080@23.976"},
      {&p5060, "24", "FP 1920x1080@24.000", "FP 1920x1080@24.000"},
      {&p5060, "25", "SBS 1920x1080@50.000 x2", "TAB 1920x1080@50.000 x2"},
      {&p5060, "29.97", "SBS 1920x1080@59.940 x2", "TAB 1920x1080@59.940 x2"},
      {&p5060, "30", "SBS 1920x1080@60.000 x2", "TAB 1920x1080@60.000 x2"},
      {&p5060, "50", "SBS 1920x1080@50.000", "TAB 1920x1080@50.000"},
      {&p5060, "59.94", "SBS 1920x1080@59.940", "TAB 1920x1080@59.940"},
      {&p5060, "60", "SBS 1920x1080@60.000", "TAB 1920x1080@60.000"},
      {&p2530, "23.976", "FP 1920x1080@23.976", "FP 1920x1080@23.976"},
      {&p2530, "24", "FP 1920x1080@24.000", "FP 1920x1080@24.000"},
      {&p2530, "25", "SBS 1920x1080@25.000", "TAB 1920x1080@25.000"},
      {&p2530, "29.97", "SBS 1920x1080@29.970", "TAB 1920x1080@29.970"},
      {&p2530, "30", "SBS 1920x1080@30.000", "TAB 1920x1080@30.000"},
      {&p2530, "50", "SBS 1920x1080@50.000", "TAB 1920x1080@50.000"},
      {&p2530, "59.94", "SBS 1920x1080@59.940", "TAB 1920x1080@59.940"},
      {&p2530, "60", "SBS 1920x1080@60.000", "TAB 1920x1080@60.000"},
      {&nvidia, "23.976", "FP 1920x1080@23.976", "FP 1920x1080@23.976"},
      {&nvidia, "24", "FP 1920x1080@24.000", "FP 1920x1080@24.000"},
      {&nvidia, "25", "SBS-IF 1920x1080@50.000 x2", "TAB-IF 1920x1080@50.000 x2"},
      {&nvidia, "29.97", "SBS-IF 1920x1080@59.940 x2", "TAB-IF 1920x1080@59.940 x2"},
      {&nvidia, "30", "SBS-IF 1920x1080@60.000 x2", "TAB-IF 1920x1080@60.000 x2"},
      {&nvidia, "50", "SBS-IF 1920x1080@50.000", "TAB-IF 1920x1080@50.000"},
      {&nvidia, "59.94", "SBS-IF 1920x1080@59.940", "TAB-IF 1920x1080@59.940"},
      {&nvidia, "60", "SBS-IF 1920x1080@60.000", "TAB-IF 1920x1080@60.000"},
  };

  std::printf("%-58s %-7s %-20s %-30s\n", "display", "rate", "source", "output (x2: twice the refresh rate)");
  int doubled = 0;
  for (const Row& row : table)
  {
    const Display& d = *row.d;
    const Rate* rate = nullptr;
    for (const Rate& r : RATES)
      if (std::string(r.name) == row.rate)
        rate = &r;
    const int base = Find(d.modes, 1080, RateOf(*rate));
    Check(base >= 0, d.name + " " + row.rate + ": Kodi's mode is in the list");
    if (base < 0)
      continue;
    const std::vector<Offer> offers = OffersOf(d);
    Allowed allowed; // no whitelist: otherSize, doubleRate
    allowed.pictureRate = RefreshRate(d.modes[base]); // a video of that rate plays
    for (Src s : {Src::FRAME_PACKED_MVC, Src::SBS_HALF, Src::SBS_FULL, Src::TAB})
    {
      const Choice c = Output(offers, base, s, allowed);
      const std::string got = Describe(d, c, base);
      const std::string want = s == Src::TAB ? row.topAndBottom : row.sideBySide;
      std::printf("%-58s %-7s %-20s %-30s\n", d.name.c_str(), row.rate, SrcName(s), got.c_str());
      Check(got == want, d.name + " " + row.rate + " " + SrcName(s) + ": got '" + got + "', pinned '" + want + "'");
      Consistent(d, c, base, d.name + " " + row.rate + " " + SrcName(s));
      if (got.find(" x2") != std::string::npos)
      {
        ++doubled;
        // doubled only where nothing exists at Kodi's own rate
        const Arrangement a = s == Src::TAB ? Arrangement::TOP_AND_BOTTOM : Arrangement::SIDE_BY_SIDE;
        const Offer* own = nullptr;
        for (const Offer& o : offers)
          if (o.base == base)
            own = &o;
        Check(own && DETAIL::ChooseAtRate(offers, *own, own->refresh, a, allowed).layout == Layout::NONE,
              d.name + " " + row.rate + " " + SrcName(s) + ": doubled only where nothing exists at Kodi's rate");
      }
    }
  }
  // 25, 29.97 and 30 on the two displays with half layouts at 1080p50/60 only, four sources each
  Check(doubled == 24, "the outputs that are doubled: 24 of them (" + std::to_string(doubled) + ")");

  // ---- the conditions of the doubling (display with the half layouts at 1080p50/60, a 30p video)
  const std::vector<Offer> offers = OffersOf(p5060);
  const int p30 = Find(p5060.modes, 1080, 30.0);
  const int p60 = Find(p5060.modes, 1080, 60.0);
  const auto rowOf = [&](const Display& d, const Choice& c, int base) { return Describe(d, c, base); };
  {
    Allowed a; // the GUI, no video: the picture rate is not known
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "2D",
          "no video playing (picture rate 0): no doubling, 2D");
  }
  {
    Allowed a;
    a.pictureRate = 30.0;
    a.doubleRate = false; // a whitelist without its double refresh rate option
    a.otherSize = false;
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "2D",
          "a whitelist without the double refresh rate option: 2D");
    // ... even where the whitelist has 1080p60 (Kodi's own choice would not double the refresh rate)
    std::vector<Offer> w = offers;
    for (Offer& o : w)
      if (o.base == p60)
        o.whitelisted = true;
    Check(rowOf(p5060, Output(w, p30, Src::SBS_HALF, a), p30) == "2D",
          "a whitelist with 1080p60 in it, without the double refresh rate option: 2D");
  }
  {
    Allowed a;
    a.pictureRate = 30.0;
    a.doubleRate = true; // the whitelist allows double refresh rates
    a.otherSize = false;
    std::vector<Offer> w = offers;
    Check(rowOf(p5060, Output(w, p30, Src::SBS_HALF, a), p30) == "2D",
          "a whitelist with the option, 1080p60 not in it: 2D");
    for (Offer& o : w)
      if (o.base == p60)
        o.whitelisted = true;
    Check(rowOf(p5060, Output(w, p30, Src::SBS_HALF, a), p30) == "SBS 1920x1080@60.000 x2",
          "a whitelist with the option, 1080p60 in it: SBS at 60, doubled");
  }
  {
    Allowed a;
    a.pictureRate = 30.0;
    // Kodi stays in 1080p60 (no refresh rate adjustment) for a 30p video: that is Kodi's own mode
    Check(rowOf(p5060, Output(offers, p60, Src::SBS_HALF, a), p60) == "SBS 1920x1080@60.000",
          "30p video in Kodi's 1080p60 mode: SBS at 60, Kodi's own rate, not counted as doubled");
    const std::vector<Offer> m = OffersOf(mand);
    const int mp60 = Find(mand.modes, 1080, 60.0);
    Check(rowOf(mand, Output(m, mp60, Src::SBS_HALF, a), mp60) == "2D",
          "30p video in 1080p60 on a mandatory-only display: 2D, never 120 Hz");
  }
  {
    // 29.97 takes 59.94, never 60.000; 30 takes 60.000, never 59.94
    Display only60{"half layouts at 1080p60.000 only", mand.modes, nullptr};
    AddHalf1080p(only60.modes, {60}, false, true);
    Display only5994{"half layouts at 1080p59.94 only", mand.modes, nullptr};
    AddHalf1080p(only5994.modes, {60}, true, false);
    Allowed a;
    const int b2997 = Find(only60.modes, 1080, 30000.0 / 1001);
    a.pictureRate = RefreshRate(only60.modes[b2997]);
    Check(rowOf(only60, Output(OffersOf(only60), b2997, Src::SBS_HALF, a), b2997) == "2D",
          "29.97 with half layouts only at 60.000: 2D (never 60.000 for 29.97)");
    const int b30 = Find(only5994.modes, 1080, 30.0);
    a.pictureRate = 30.0;
    Check(rowOf(only5994, Output(OffersOf(only5994), b30, Src::SBS_HALF, a), b30) == "2D",
          "30 with half layouts only at 59.94: 2D");
    const int c2997 = Find(only5994.modes, 1080, 30000.0 / 1001);
    a.pictureRate = RefreshRate(only5994.modes[c2997]);
    Check(rowOf(only5994, Output(OffersOf(only5994), c2997, Src::TAB, a), c2997) == "TAB 1920x1080@59.940 x2",
          "29.97 with half layouts only at 59.94: TAB at 59.94, doubled");
  }
  {
    // a picture rate that is not Kodi's refresh rate (a 24p video in a 30 Hz mode): no doubling
    Allowed a;
    a.pictureRate = 24.0;
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "2D",
          "picture rate 24 in a 30 Hz mode: no doubling");
    a.pictureRate = 30.004; // within 0.01 of the refresh rate: one picture a refresh
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "SBS 1920x1080@60.000 x2",
          "picture rate within 0.01 of the refresh rate: doubled");
    a.pictureRate = 30.011;
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "2D",
          "picture rate 0.011 away from the refresh rate: no doubling");
  }
  {
    // the driver refused the half modes (and no InfoFrame): 2D at both rates
    Allowed a;
    a.pictureRate = 30.0;
    a.halfModes = false;
    Check(rowOf(p5060, Output(offers, p30, Src::SBS_HALF, a), p30) == "2D", "half modes refused: 2D");
    a.halfModes = true;
    a.framePacking = false;
    Check(rowOf(p5060, Output(offers, p30, Src::TAB, a), p30) == "TAB 1920x1080@60.000 x2",
          "frame packing refused: the TAB of twice the rate is unaffected");
  }
  {
    // the 1080-line rule at twice the rate: 720p60 frame packing is not taken for a 1080p30 mode
    const std::vector<Offer> m = OffersOf(mand);
    const int mp30 = Find(mand.modes, 1080, 30.0);
    Allowed a;
    a.pictureRate = 30.0;
    Check(!PrefersTopAndBottom(m, mp30, a), "1080p30 on a mandatory-only display: 720p60 FP is not preferred");
    // ... but a 720p30 desktop? Kodi's mode of 720 lines: not in the list (HDMI has no 720p30): nothing
  }

  // ---- what the doubling relies on of the rest of the choice: another size, the order of the list,
  // the InfoFrame per layout, a mode Kodi has that is not in the list. Choose() is called directly too
  // (the drawing that GetStereoOutputMode() hands it, after GetStereoOutputArrangement()).
  {
    // Kodi in 2160p (a 4K 3D display): HDMI has no 3D layout of 2160p; half layouts at 1080p60/50 and
    // 2560x1440p60 (the logic takes any size), frame packing at 1080p24
    Display uhd{"2160p desktop, 3D at 1080p and 1440p", {}, nullptr};
    for (int n : {24, 30, 60})
    {
      Mode m = Timing(1920, 1080, n, false);
      m.hdisplay = 3840;
      m.vdisplay = 2160;
      m.htotal = static_cast<uint16_t>(m.htotal * 2);
      m.vtotal = 2250;
      m.clock *= 4;
      m.hsyncStart = 3840 + 176;
      m.hsyncEnd = 3840 + 264;
      m.vsyncStart = 2160 + 8;
      m.vsyncEnd = 2160 + 18;
      uhd.modes.push_back(m);
    }
    for (const Rate& r : RATES)
      if (!r.video)
        uhd.modes.push_back(Timing(1920, 1080, r.nominal, false));
    Mode q = Timing(1920, 1080, 60, false);
    q.hdisplay = 2560;
    q.vdisplay = 1440;
    q.htotal = 2720;
    q.vtotal = 1481;
    q.clock = 241700;
    q.hsyncStart = 2560 + 48;
    q.hsyncEnd = 2560 + 80;
    q.vsyncStart = 1440 + 3;
    q.vsyncEnd = 1440 + 8;
    uhd.modes.push_back(q);
    {
      const std::vector<Mode> b = uhd.modes;
      for (const Mode& m : b)
      {
        const double r = RefreshRate(m);
        if (m.vdisplay == 1080 && std::fabs(r - 24) < 0.01)
          uhd.modes.push_back(As(m, FLAG_3D_FRAME_PACKING));
        if ((m.vdisplay == 1080 && (std::fabs(r - 60) < 0.01 || std::fabs(r - 50) < 0.01)) ||
            (m.vdisplay == 1440 && std::fabs(r - 60) < 0.01))
          uhd.modes.push_back(As(m, FLAG_3D_SIDE_BY_SIDE_HALF));
      }
    }
    const std::vector<Offer> o = OffersOf(uhd);
    const int u24 = Find(uhd.modes, 2160, 24.0), u30 = Find(uhd.modes, 2160, 30.0), u60 = Find(uhd.modes, 2160, 60.0);
    Allowed a;
    a.pictureRate = 24.0;
    Check(Describe(uhd, Output(o, u24, Src::FRAME_PACKED_MVC, a), u24) == "FP 1920x1080@24.000",
          "2160p24 desktop: frame packing at 1080p24, another size (the largest that has it)");
    a.pictureRate = 60.0;
    Check(Describe(uhd, Output(o, u60, Src::SBS_HALF, a), u60) == "SBS 2560x1440@60.000",
          "2160p60 desktop: SBS at the largest size not above it that has it (1440p over 1080p)");
    a.pictureRate = 30.0;
    Check(Describe(uhd, Output(o, u30, Src::SBS_HALF, a), u30) == "SBS 2560x1440@60.000 x2",
          "2160p30 desktop, nothing at 30: SBS at 1440p60, another size at twice the rate");
    a.otherSize = false; // a whitelist: another size only where the whitelist has its 2D mode
    Check(Describe(uhd, Output(o, u30, Src::SBS_HALF, a), u30) == "2D", "2160p30, whitelist without 1440p/1080p: 2D");
    // a 3D layout at 2160p60 itself (the logic is the same for any size): Kodi's size wins
    Display uhd3 = uhd;
    uhd3.modes.push_back(As(uhd.modes[u60], FLAG_3D_SIDE_BY_SIDE_HALF));
    a = Allowed{};
    a.pictureRate = 30.0;
    Check(Describe(uhd3, Output(OffersOf(uhd3), u30, Src::SBS_HALF, a), u30) == "SBS 3840x2160@60.000 x2",
          "2160p30 with SBS at 2160p60: Kodi's size at twice the rate first");
  }
  {
    // the modes in another order: a 3D mode first in the list, a second 1080p60 timing that has the
    // half layouts while the first one has not
    Display first{"3D modes first", {}, nullptr};
    const Mode p24 = Timing(1920, 1080, 24, false), p30 = Timing(1920, 1080, 30, false);
    const Mode p60 = Timing(1920, 1080, 60, false);
    Mode p60b = p60; // another 1080p60 timing (other blanking)
    p60b.htotal = 2080;
    p60b.vtotal = 1111;
    p60b.clock = 138650;
    first.modes = {As(p24, FLAG_3D_FRAME_PACKING), As(p60b, FLAG_3D_SIDE_BY_SIDE_HALF), p24, p30, p60, p60b};
    const std::vector<Offer> o = OffersOf(first);
    Allowed a;
    a.pictureRate = 24.0;
    Check(Describe(first, Output(o, 2, Src::TAB, a), 2) == "FP 1920x1080@24.000", "a frame packing mode at index 0");
    {
      // a whitelist that does not have Kodi's mode (the desktop mode, kept where no mode of it fits): the
      // timing of Kodi's mode is still its own, frame packed at index 0
      Allowed w = a;
      w.otherSize = false;
      Check(Describe(first, Output(o, 2, Src::TAB, w), 2) == "FP 1920x1080@24.000",
            "whitelist without Kodi's mode: its own frame packing mode, at index 0");
    }
    a.pictureRate = 30.0;
    const Choice c = Output(o, 3, Src::SBS_HALF, a);
    Check(Describe(first, c, 3) == "SBS 1920x1080@59.999 x2" && c.mode == 1 && c.base == 5,
          "30p: SBS of the second 1080p60 timing, at index 1 (" + Describe(first, c, 3) + ")");
    // a half layout mode at index 0; a frame packing mode of another size at index 0
    Display half0{"SBS mode first", {As(p60, FLAG_3D_SIDE_BY_SIDE_HALF), p30, p60}, nullptr};
    const Choice h = Output(OffersOf(half0), 1, Src::SBS_HALF, a);
    Check(Describe(half0, h, 1) == "SBS 1920x1080@60.000 x2" && h.mode == 0, "a half layout mode at index 0");
    Mode u24 = p24;
    u24.hdisplay = 3840;
    u24.vdisplay = 2160;
    u24.htotal = 5500;
    u24.vtotal = 2250;
    u24.clock = 297000;
    Display fp0{"FP of 1080p24 first, 2160p24 desktop", {As(p24, FLAG_3D_FRAME_PACKING), u24, p24}, nullptr};
    a.pictureRate = 24.0;
    const Choice f = Output(OffersOf(fp0), 1, Src::TAB, a);
    Check(Describe(fp0, f, 1) == "FP 1920x1080@24.000" && f.mode == 0 && f.base == 2,
          "frame packing of another size at index 0");
  }
  {
    // the InfoFrame of one layout only (an EDID with side-by-side (half) at 1080p60 and not top-and-bottom)
    Display nvSbs{"nvidia-drm, InfoFrame SBS only at 1080p60", Modes2D(), nullptr};
    nvSbs.infoFrame = [](const Mode& m, Layout l) {
      return l == Layout::SIDE_BY_SIDE_HALF && m.vdisplay == 1080 && !(m.flags & FLAG_INTERLACE) &&
             std::fabs(RefreshRate(m) - 60) < 0.01;
    };
    const std::vector<Offer> o = OffersOf(nvSbs);
    const int b30 = Find(nvSbs.modes, 1080, 30.0);
    Allowed a;
    a.pictureRate = 30.0;
    Check(Describe(nvSbs, Output(o, b30, Src::SBS_HALF, a), b30) == "SBS-IF 1920x1080@60.000 x2",
          "InfoFrame of SBS only: an SBS source gets it at twice the rate");
    Check(Describe(nvSbs, Output(o, b30, Src::TAB, a), b30) == "2D", "InfoFrame of SBS only: a TAB source gets 2D");
  }
  {
    // Choose() as GetStereoOutputMode() calls it, with the drawing it is handed: frame packing only for
    // eyes drawn one above the other, never for side by side; frame packing refused by the driver: TAB
    const std::vector<Offer> m = OffersOf(mand);
    const int b24 = Find(mand.modes, 1080, 24.0);
    Allowed a;
    a.pictureRate = 24.0;
    Check(Describe(mand, Choose(m, b24, Arrangement::SIDE_BY_SIDE, a), b24) == "2D",
          "side by side drawing at 1080p24 on a mandatory-only display: 2D (no SBS there, no 48 Hz)");
    a.framePacking = false;
    Check(Describe(mand, Choose(m, b24, Arrangement::TOP_AND_BOTTOM, a), b24) == "TAB 1920x1080@24.000",
          "frame packing refused: TAB at 1080p24");
    // an interlaced mode of Kodi: never frame packing, SBS of 1080i
    a = Allowed{};
    const int i50 = Find(mand.modes, 1080, 50.0, true);
    a.pictureRate = 50.0;
    Check(Describe(mand, Choose(m, i50, Arrangement::TOP_AND_BOTTOM, a), i50) == "2D",
          "1080i50 drawn one above the other: 2D (no frame packing of interlaced timing, no TAB of 1080i)");
    Check(Describe(mand, Choose(m, i50, Arrangement::SIDE_BY_SIDE, a), i50) == "SBS 1920x1080@50.000",
          "1080i50 drawn side by side: SBS of 1080i50");
    // a mode that is not in the list: nothing
    a.pictureRate = 30.0;
    Check(Describe(mand, Choose(m, 999, Arrangement::SIDE_BY_SIDE, a), 0) == "2D", "a base that is no offer: 2D");
    Check(Describe(mand, Choose(m, -1, Arrangement::SIDE_BY_SIDE, a), 0) == "2D", "base -1: 2D");
  }

  {
    // what the choice reads of the modes: the refresh rate Kodi works out (interlaced: fields; double
    // scan; vscan; no totals), a frame packing mode of interlaced timing is never offered, and of two
    // modes of one timing and layout the first in the list is the one
    Mode i60 = Timing(1920, 1080, 60, false, true);
    Check(std::fabs(RefreshRate(i60) - 60.0) < 1e-9, "RefreshRate: 1080i60 is 60 fields a second");
    Mode d = Timing(1280, 720, 60, false);
    d.flags |= FLAG_DBLSCAN;
    Check(std::fabs(RefreshRate(d) - 30.0) < 1e-9, "RefreshRate: double scan halves it");
    Mode v = Timing(1280, 720, 60, false);
    v.vscan = 2;
    Check(std::fabs(RefreshRate(v) - 30.0) < 1e-9, "RefreshRate: vscan 2 halves it");
    v.vscan = 1;
    Check(std::fabs(RefreshRate(v) - 60.0) < 1e-9, "RefreshRate: vscan 1 changes nothing");
    Mode z = Timing(1280, 720, 60, false);
    z.htotal = 0;
    Check(RefreshRate(z) == 0.0, "RefreshRate: no htotal, 0");
    z = Timing(1280, 720, 60, false);
    z.vtotal = 0;
    Check(RefreshRate(z) == 0.0, "RefreshRate: no vtotal, 0");
    const Mode p60 = Timing(1920, 1080, 60, false);
    const std::vector<Mode> li = {As(i60, FLAG_3D_FRAME_PACKING), i60};
    const std::vector<Offer> oi = Offers(li);
    Check(oi.size() == 1 && oi[0].base == 1 && oi[0].framePacking == -1 && oi[0].interlaced,
          "Offers: no frame packing of interlaced timing");
    for (uint32_t flag : {FLAG_3D_FRAME_PACKING, FLAG_3D_TOP_AND_BOTTOM, FLAG_3D_SIDE_BY_SIDE_HALF})
    {
      const std::vector<Mode> l = {As(p60, flag), As(p60, flag), p60};
      const std::vector<Offer> o = Offers(l);
      const int got = o.empty() ? -9
                      : flag == FLAG_3D_FRAME_PACKING ? o[0].framePacking
                      : flag == FLAG_3D_TOP_AND_BOTTOM ? o[0].topAndBottom
                                                         : o[0].sideBySide;
      Check(o.size() == 1 && o[0].base == 2 && got == 0,
            std::string("Offers: of two ") + LayoutName(LayoutOf(flag)) + " modes of one timing, the first (" +
                std::to_string(got) + ")");
    }
  }

  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
