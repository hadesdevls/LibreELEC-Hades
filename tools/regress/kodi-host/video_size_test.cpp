/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of "Adjust display resolution" (videoplayer.hadesadjustresolution,
 * windowing/VideoSizeMode.h, CResolutionUtils::ChooseBestResolution()): a video in the mode of its own size at the
 * refresh rate Kodi chose for it.
 *  - the LG C5 of the tests (its modes as Kodi listed them on 2026-11-19), menus at 3840x2160: a film of 1920x1080
 *    (or 1920x800) at 23.976 fps, for which Kodi chose 3840x2160 at 23.976 Hz, goes out at 1920x1080 at 23.977 Hz;
 *    720p at 50 Hz at 1280x720; 720p at 23.976 (no such mode) and a 4K film stay; a DVD of 720x576 at 50 Hz;
 *  - the Samsung Odyssey G60SD (menus 2560x1440 at 119.998 Hz, no mode of 24 Hz): 1920x1080 at 120 Hz;
 *  - the refresh rate within Kodi's 0.01 Hz and not further, the kind of the mode (interlaced, 3D layout) kept,
 *    the nearest size and then the nearest rate;
 *  - nothing with the setting off, with a whitelist of the user, for native Dolby Vision, or without a size;
 *  - OfVideoSize() is Kodi's MatchesMode() (Resolution.cpp calls it): its tolerances;
 *  - (crash) with "Video at its own rate with VRR" the size wins (ChooseForVrr()): 1080p on the LG C5 at 1920x1080
 *    at 120 Hz, the G60SD at 359.986 Hz over FRL and 120 Hz over TMDS (no change to FRL for a video), 720p over FRL
 *    with none (its modes are under FRL_KEEP::MIN_KEPT_KHZ and would go over TMDS: no VRR), a 4K film and an
 *    unknown VRR range in the menus, NVKMS's 50 Hz, with the pixel clocks of the LG C5 and the G60SD;
 *  - (crash3) the G60SD with "Where the display has no mode at the video's rate": the decision asked twice (from the
 *    menus, then from 1920x1080 at 359.986 Hz, where Kodi's whitelist takes 1920x1080 at 59.940 Hz) stays VRR in
 *    1920x1080 at 359.986 Hz (VRR_VIDEO::KodiStays()); with VRR off, no change of size.
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -o t video_size_test.cpp && ./t
 */
#include "windowing/VideoSizeMode.h"
#include "windowing/gbm/FrlKeep.h"
#include "windowing/gbm/VrrVideo.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::VIDEO_SIZE;
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

static std::string Name(const std::vector<Mode>& modes, int place)
{
  if (place < 0)
    return "stays";
  const Mode& m = modes[static_cast<size_t>(place)];
  return std::to_string(m.width) + "x" + std::to_string(m.height) + "@" + std::to_string(m.refresh);
}

static int Find(const std::vector<Mode>& modes, int w, int h, double hz, unsigned int kind = 0)
{
  for (size_t i = 0; i < modes.size(); ++i)
    if (modes[i].width == w && modes[i].height == h && modes[i].refresh == hz && modes[i].kind == kind)
      return static_cast<int>(i);
  return -100;
}

int main()
{
  constexpr unsigned int INTERLACED = 1, SBS = 8;
  // the LG C5 as Kodi listed its modes (kodi.log of start 42, "Found resolution"), 2D progressive, with an
  // interlaced 1080i and a 3D side-by-side variant of 1080p24 added
  const std::vector<Mode> c5 = {
      {3840, 2160, 60.0, 0},      {4096, 2160, 120.0, 0},     {4096, 2160, 23.975758, 0},
      {3840, 2160, 120.0, 0},     {3840, 2160, 119.879799, 0}, {3840, 2160, 100.0, 0},
      {3840, 2160, 59.940403, 0}, {3840, 2160, 50.0, 0},      {3840, 2160, 30.0, 0},
      {3840, 2160, 29.969696, 0}, {3840, 2160, 25.0, 0},      {3840, 2160, 24.0, 0},
      {3840, 2160, 23.975758, 0}, {2560, 1440, 119.997589, 0}, {1920, 1080, 120.0, 0},
      {1920, 1080, 119.878784, 0}, {1920, 1080, 100.0, 0},    {1920, 1080, 60.0, 0},
      {1920, 1080, 59.939392, 0}, {1920, 1080, 50.0, 0},      {1920, 1080, 30.0, 0},
      {1920, 1080, 29.971718, 0}, {1920, 1080, 25.0, 0},      {1920, 1080, 24.0, 0},
      {1920, 1080, 23.977373, 0}, {1280, 1024, 60.019741, 0}, {1280, 720, 60.0, 0},
      {1280, 720, 59.943436, 0},  {1280, 720, 50.0, 0},       {720, 576, 50.0, 0},
      {720, 480, 60.0, 0},        {720, 480, 59.94006, 0},    {640, 480, 59.952381, 0},
      {1920, 1080, 25.0, INTERLACED}, {1920, 1080, 24.0, SBS}, {3840, 2160, 24.0, SBS},
  };
  const Mode uhd23976{3840, 2160, 23.975758, 0};
  {
    const int p = Choose(c5, uhd23976, 1920, 1080);
    Check(p == Find(c5, 1920, 1080, 23.977373), "LG C5, 1080p film at 3840x2160 23.976: " + Name(c5, p));
    Check(Choose(c5, uhd23976, 1920, 800) == p, "LG C5, a scope film of 1920x800: 1920x1080 too");
    Check(Choose(c5, uhd23976, 1920, 1088) == p, "LG C5, 1920x1088 as a decoder pads it: 1920x1080");
    Check(Choose(c5, uhd23976, 1440, 1080) == p, "LG C5, 1440x1080: 1920x1080");
    Check(Choose(c5, uhd23976, 3840, 2160) < 0, "LG C5, a 4K film: 3840x2160 stays");
    Check(Choose(c5, uhd23976, 3832, 1600) < 0, "LG C5, a 4K scope film cropped by its encoder: stays");
    Check(Choose(c5, uhd23976, 1280, 720) < 0, "LG C5, 720p at 23.976: no mode of 1280x720 at it, stays");
    const Mode uhd50{3840, 2160, 50.0, 0};
    Check(Choose(c5, uhd50, 1280, 720) == Find(c5, 1280, 720, 50.0), "LG C5, 720p at 50 Hz: 1280x720 at 50");
    Check(Choose(c5, uhd50, 720, 576) == Find(c5, 720, 576, 50.0), "LG C5, a DVD of 720x576 at 50 Hz: 720x576");
    Check(Choose(c5, uhd50, 1920, 1080) == Find(c5, 1920, 1080, 50.0),
          "LG C5, 1080p at 50 Hz: the progressive 1920x1080, not the interlaced one");
    const Mode uhd24{3840, 2160, 24.0, 0};
    Check(Choose(c5, uhd24, 1920, 1080) == Find(c5, 1920, 1080, 24.0),
          "LG C5, 24.000 Hz: 1920x1080 at 24.000, not 23.977 (the nearest rate of the 0.01 Hz)");
    // the kind of the mode Kodi chose is kept: 3D side by side
    const Mode sbs{3840, 2160, 24.0, SBS};
    Check(Choose(c5, sbs, 1920, 1080) == Find(c5, 1920, 1080, 24.0, SBS), "LG C5, 3D side by side: its 1080p");
    const Mode interlaced{3840, 2160, 25.0, INTERLACED};
    Check(Choose(c5, interlaced, 1920, 1080) == Find(c5, 1920, 1080, 25.0, INTERLACED),
          "an interlaced mode: the 1920x1080 of its kind");
    const Mode interlaced50{3840, 2160, 50.0, INTERLACED};
    Check(Choose(c5, interlaced50, 1920, 1080) < 0, "an interlaced mode with no 1920x1080 of its kind: stays");
  }
  // the rate within Kodi's 0.01 Hz, and not further
  {
    const std::vector<Mode> m = {{1920, 1080, 23.99, 0}, {1920, 1080, 23.96, 0}};
    Check(Choose(m, uhd23976, 1920, 1080) < 0, "23.99 and 23.96 Hz are not 23.976 within 0.01 Hz: stays");
    const std::vector<Mode> near = {{1920, 1080, 23.98, 0}};
    Check(Choose(near, uhd23976, 1920, 1080) == 0, "23.98 Hz is 23.976 within 0.01 Hz");
  }
  // the nearest size, then the nearest rate
  {
    const std::vector<Mode> m = {{1920, 1200, 23.976, 0}, {1920, 1080, 23.98, 0}, {1920, 1080, 23.976, 0}};
    const int p = Choose(m, uhd23976, 1920, 1080);
    Check(p == 2, "1920x1080 before 1920x1200, and 23.976 before 23.98: " + Name(m, p));
    const std::vector<Mode> only1200 = {{1920, 1200, 23.976, 0}};
    Check(Choose(only1200, uhd23976, 1920, 1080) == 0, "a monitor of 1920x1200: Kodi's rule takes it for 1080p");
  }
  // the Samsung Odyssey G60SD, menus at 2560x1440 at 119.998 Hz, no mode of 24 Hz: Kodi stays in the menus
  {
    const std::vector<Mode> g = {{2560, 1440, 119.998962, 0}, {2560, 1440, 359.999054, 0},
                                 {2560, 1440, 59.999481, 0},  {1920, 1080, 359.986328, 0},
                                 {1920, 1080, 120.0, 0},      {1920, 1080, 119.878784, 0},
                                 {1920, 1080, 60.0, 0}};
    const Mode menus{2560, 1440, 119.998962, 0};
    const int p = Choose(g, menus, 1920, 1080);
    Check(p == 4, "G60SD, 1080p film in the mode of the menus: 1920x1080 at 120 Hz: " + Name(g, p));
    const Mode m360{2560, 1440, 359.999054, 0};
    Check(Choose(g, m360, 1920, 1080) < 0,
          "G60SD, menus at 359.999 Hz: its 1920x1080 at 359.986 is not within 0.01 Hz, 2560x1440 stays");
  }
  // when the setting looks
  {
    Check(NotLooked(true, false, false, 1920, 1080) == nullptr, "on, no whitelist, not native DV: looks");
    Check(NotLooked(false, false, false, 1920, 1080) != nullptr, "off: nothing");
    Check(NotLooked(true, true, false, 1920, 1080) != nullptr, "a whitelist of the user: nothing");
    Check(NotLooked(true, false, true, 1920, 1080) != nullptr, "native Dolby Vision: its transport stays");
    Check(NotLooked(true, false, false, 0, 1080) != nullptr, "no size: nothing");
  }
  // Kodi's MatchesMode(), as it was before it called this
  {
    Check(OfVideoSize(1920, 1080, 1920, 1080), "1920x1080 in 1920x1080");
    Check(OfVideoSize(1920, 1088, 1920, 1080), "1920x1088 in 1920x1080 (32 lines over)");
    Check(!OfVideoSize(1920, 1113, 1920, 1080), "1920x1113 not in 1920x1080 (33 lines over)");
    Check(OfVideoSize(3832, 2160, 3840, 2160), "3832x2160 in 3840x2160 (cropped)");
    Check(!OfVideoSize(1280, 720, 1920, 1080), "1280x720 not in 1920x1080");
    Check(!OfVideoSize(1920, 1080, 3840, 2160), "1920x1080 not in 3840x2160");
    Check(!OfVideoSize(1929, 1080, 1920, 1080), "1929 wide not in 1920 (8 over at most)");
  }
  // (crash, 1.1) the LG C5 with the pixel clock of each mode (modetest of start 46, HDMI-A-3)
  const std::vector<Mode> lg = {
      {3840, 2160, 120.0, 0, 1188000},      {3840, 2160, 119.879799, 0, 1186810}, {3840, 2160, 100.0, 0, 1188000},
      {3840, 2160, 60.0, 0, 594000},        {3840, 2160, 59.940403, 0, 593410},   {3840, 2160, 50.0, 0, 594000},
      {3840, 2160, 30.0, 0, 297000},        {3840, 2160, 29.969696, 0, 296700},   {3840, 2160, 25.0, 0, 297000},
      {3840, 2160, 24.0, 0, 297000},        {3840, 2160, 23.975758, 0, 296700},   {2560, 1440, 119.997589, 0, 497750},
      {1920, 1080, 120.0, 0, 297000},       {1920, 1080, 119.878784, 0, 296700},  {1920, 1080, 100.0, 0, 297000},
      {1920, 1080, 60.0, 0, 148500},        {1920, 1080, 59.939392, 0, 148350},   {1920, 1080, 50.0, 0, 148500},
      {1920, 1080, 30.0, 0, 74250},         {1920, 1080, 29.971718, 0, 74180},    {1920, 1080, 25.0, 0, 74250},
      {1920, 1080, 24.0, 0, 74250},         {1920, 1080, 23.977373, 0, 74180},    {1280, 720, 60.0, 0, 74250},
      {1280, 720, 59.943436, 0, 74180},     {1280, 720, 50.0, 0, 74250},          {720, 576, 50.0, 0, 27000},
      {1920, 1080, 24.0, SBS, 148500},      {1920, 1080, 25.0, INTERLACED, 74250},
  };
  const auto lgAt = [&lg](int w, int h, double hz, unsigned int kind = 0) { return Find(lg, w, h, hz, kind); };
  const auto lgMode = [&lg](int place) { return lg[static_cast<size_t>(place)]; };
  // (crash, 1.1) "Adjust display resolution" with "Video at its own rate with VRR": the size wins (ChooseForVrr())
  {
    VrrLink frl; // the LG C5 (HDMI VRR 40 to 120 Hz), NVKMS, the menus at 4K 120 Hz over FRL
    frl.vrrMin = 40;
    frl.vrrMax = 120;
    frl.minRefresh = 49.995;
    frl.minLines = 720;
    frl.minFactor = 1.1;
    frl.floorKHz = KODI::WINDOWING::GBM::FRL_KEEP::MIN_KEPT_KHZ;
    const Mode menus{3840, 2160, 120.0, 0, 1188000};
    VrrChoice c = ChooseForVrr(lg, menus, 1920, 1080, frl);
    Check(c.place == VrrPlace::MODE && c.index == lgAt(1920, 1080, 120.0),
          "LG C5, 1080p with VRR: 1920x1080 at 120 Hz, the highest in its VRR range: " + Name(lg, c.index));
    Check(ChooseForVrr(lg, menus, 3840, 2160, frl).place == VrrPlace::MENUS, "LG C5, a 4K film: the menus");
    Check(ChooseForVrr(lg, menus, 1280, 720, frl).place == VrrPlace::NONE,
          "LG C5 over FRL, 720p: its modes are under the floor, none with VRR here: no VRR");
    VrrLink tmds = frl; // the menus at 4K 60 Hz over TMDS
    tmds.floorKHz = 0;
    tmds.ceilingKHz = 600000;
    c = ChooseForVrr(lg, {3840, 2160, 60.0, 0, 594000}, 1280, 720, tmds);
    Check(c.place == VrrPlace::MODE && c.index == lgAt(1280, 720, 60.0), "LG C5 over TMDS, 720p: 1280x720 at 60 Hz");
    // the Samsung Odyssey G60SD (HDMI VRR 48 to 360 Hz), with its clocks (modetest of start 46, HDMI-A-2)
    const std::vector<Mode> g = {{2560, 1440, 119.998962, 0, 555720}, {2560, 1440, 359.999054, 0, 1667170},
                                 {2560, 1440, 239.96, 0, 1111250},    {1920, 1080, 359.986328, 0, 937750},
                                 {1920, 1080, 239.92691, 0, 625000},  {1920, 1080, 120.0, 0, 297000},
                                 {1920, 1080, 119.878784, 0, 296700}, {1920, 1080, 60.0, 0, 148500}};
    VrrLink gfrl = frl;
    gfrl.vrrMin = 48;
    gfrl.vrrMax = 360;
    c = ChooseForVrr(g, g[1], 1920, 1080, gfrl);
    Check(c.place == VrrPlace::MODE && c.index == 3, "G60SD, menus at 360 Hz over FRL: 1920x1080 at 359.986 Hz");
    VrrLink gtmds = gfrl;
    gtmds.floorKHz = 0;
    gtmds.ceilingKHz = 600000;
    c = ChooseForVrr(g, g[0], 1920, 1080, gtmds);
    Check(c.place == VrrPlace::MODE && c.index == 5,
          "G60SD, menus at 120 Hz over TMDS: 1920x1080 at 120 Hz, none that would need FRL: " + Name(g, c.index));
    Check(ChooseForVrr(g, g[1], 3840, 2160, gfrl).place == VrrPlace::MENUS,
          "G60SD, a 4K film: no mode of its size, the menus as before");
    VrrLink unknown = frl;
    unknown.vrrMin = 0;
    Check(ChooseForVrr(lg, menus, 1920, 1080, unknown).place == VrrPlace::MENUS, "no VRR range known: the menus");
    // NVKMS gives no VRR under 50 Hz: a mode of 48 Hz is none with VRR there, and is on another driver
    const std::vector<Mode> m48 = {{1920, 1080, 48.0, 0, 148500}};
    Check(ChooseForVrr(m48, menus, 1920, 1080, tmds).place == VrrPlace::NONE, "NVKMS: 1080p at 48 Hz has no VRR");
    VrrLink amd = tmds;
    amd.minRefresh = 0.0;
    amd.minLines = 0;
    amd.minFactor = 0.0;
    Check(ChooseForVrr(m48, menus, 1920, 1080, amd).place == VrrPlace::MODE, "another driver: 1080p at 48 Hz has");
    Mode noClock = lgMode(lgAt(1920, 1080, 120.0));
    noClock.clockKHz = 0;
    Check(!VrrModeOk(noClock, frl), "a clock that is not known is not over the floor");
    Check(VrrModeOk(lgMode(lgAt(1920, 1080, 120.0)), frl), "1080p at 120 Hz carries VRR on the LG C5 over FRL");
  }
  // (crash3) the Samsung Odyssey G60SD, "Adjust display resolution" with "Video at its own rate with VRR" at "Where the
  // display has no mode at the video's rate", the menus at 2560x1440 at 359.999 Hz over FRL, a 1080p film at 23.976 fps:
  // the decision of CResolutionUtils::ChooseBestResolution() asked twice, as Kodi asks it (when the video opens, from
  // the menus; once its first picture comes, from the mode on the output), with the modes of the display (modetest)
  {
    namespace VRR = KODI::WINDOWING::GBM::VRR_VIDEO;
    const std::vector<Mode> g = {{2560, 1440, 119.998962, 0, 555720}, {2560, 1440, 359.999054, 0, 1667170},
                                 {2560, 1440, 239.96, 0, 1111250},    {2560, 1440, 59.999481, 0, 277860},
                                 {1920, 1080, 359.986328, 0, 937750}, {1920, 1080, 239.92691, 0, 625000},
                                 {1920, 1080, 120.0, 0, 297000},      {1920, 1080, 119.878784, 0, 296700},
                                 {1920, 1080, 60.0, 0, 148500},       {1920, 1080, 59.939392, 0, 148350}};
    std::vector<double> rates;
    for (const Mode& m : g)
      rates.push_back(m.refresh);
    const double fps = 23.976;
    const bool hasMode = VRR::DisplayHasModeFor(rates, fps);
    Check(!hasMode, "G60SD: no mode at 23.976 Hz or twice it at any size");
    VrrLink link;
    link.vrrMin = 48;
    link.vrrMax = 360;
    link.minRefresh = VRR::NVIDIA_VRR_MIN_REFRESH;
    link.minLines = VRR::NVIDIA_VRR_MIN_LINES;
    link.minFactor = VRR::NVIDIA_VRR_MIN_FACTOR;
    link.floorKHz = KODI::WINDOWING::GBM::FRL_KEEP::MIN_KEPT_KHZ;
    const Mode menus = g[1];
    const VrrChoice sized = ChooseForVrr(g, menus, 1920, 1080, link);
    Check(sized.place == VrrPlace::MODE && sized.index == 4,
          "G60SD over FRL: the mode of the video's size with VRR is 1920x1080 at 359.986 Hz: " + Name(g, sized.index));
    // the first ask, from the menus: Kodi's whitelist stays in them (no mode at 23.976, nor its 3:2 at 2560 wide)
    const bool first = VRR::Automatic(true, VRR::KodiStays(true, true, false), menus.refresh, fps, hasMode);
    Check(first, "first ask, from the menus: VRR in 1920x1080 at 359.986 Hz");
    // the second ask, from 1920x1080 at 359.986 Hz: Kodi's whitelist takes the 3:2 of 1920x1080 at 59.940 Hz
    const bool second = VRR::Automatic(true, VRR::KodiStays(false, true, true), menus.refresh, fps, hasMode);
    Check(second, "second ask, from the mode of the video's size (Kodi's choice 1920x1080 at 59.940): VRR still, "
                  "no second change of mode");
    // ... and nothing else changes: Kodi's own choice elsewhere is not taken as the menus
    Check(!VRR::KodiStays(false, true, false), "a choice of Kodi's in another mode than the menus, the output not in "
                                               "the mode of the video's size: as before, not the menus");
    Check(!VRR::KodiStays(false, false, false), "without the mode of the video's size: as before");
    Check(VRR::KodiStays(true, false, false), "Kodi's choice in the menus: as before");
    // with VRR off: Kodi's choice stays in the menus, and their 359.999 Hz has no 1920x1080 within 0.01 Hz
    Check(Choose(g, menus, 1920, 1080) < 0, "VRR off: 1920x1080 at 359.986 is not 359.999 within 0.01 Hz, no change");
  }
  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
