/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the mode at the frame rate of a video at another size of the display, where
 * Kodi's default whitelist finds none (windowing/ExactRateElsewhere.h, CResolutionUtils::FindExactRateElsewhere()):
 *  - the LG C5 with the menus left at 2560x1440 by a monitor: a film of 1920x1080 at 23.976 fps gets 3840x2160 at
 *    23.976 Hz (the display's preferred size), not the 119.998 Hz of the menus;
 *  - a display without any mode at that rate or twice it (the Samsung Odyssey G60SD): nothing, as before;
 *  - a whitelist of the user, native Dolby Vision, HDR10, HDR10+, HLG, 3D, a video whose kind is not known: nothing;
 *  - the order: the preferred size, the nearest size at or above the menus', the largest under them (never under
 *    the video's); twice the rate only where no size has the rate itself; a video not known to be progressive takes
 *    no mode of 30 Hz or less but 24 Hz.
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -o t exact_rate_test.cpp && ./t
 */
#include "windowing/ExactRateElsewhere.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING;
using namespace KODI::WINDOWING::EXACT_RATE;
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

static std::string Name(const std::vector<Mode>& modes, const Choice& c)
{
  if (c.index < 0)
    return c.notLooked ? std::string("nothing looked for: ") + c.notLooked : "none";
  const Mode& m = modes[static_cast<size_t>(c.index)];
  return std::to_string(m.width) + "x" + std::to_string(m.height) + "@" + std::to_string(m.refresh) +
         " (" + WhyName(c.why) + ")";
}

int main()
{
  const double film = 24000.0 / 1001.0;
  // the LG C5 (by its EDID, as Kodi lists its modes: progressive 2D), 2560x1440 at 119.998 Hz for the menus
  const std::vector<Mode> c5 = {
      {3840, 2160, 120.0, true},   {3840, 2160, 119.88, true}, {3840, 2160, 60.0, true},
      {3840, 2160, 59.94, true},   {3840, 2160, 50.0, true},   {3840, 2160, 30.0, true},
      {3840, 2160, 29.97, true},   {3840, 2160, 25.0, true},   {3840, 2160, 24.0, true},
      {3840, 2160, 23.976, true},  {2560, 1440, 119.998, true}, {2560, 1440, 59.951, true},
      {1920, 1080, 120.0, true},   {1920, 1080, 60.0, true},   {1920, 1080, 59.94, true},
      {1920, 1080, 50.0, true},    {1920, 1080, 24.0, true},   {1920, 1080, 23.976, true},
      {1920, 1080, 60.0, false}, // (an interlaced mode: never)
  };
  Query lg;
  lg.video = WholeMultipleVideo::SDR;
  lg.progressive = true;
  lg.fps = film;
  lg.videoWidth = 1920;
  lg.videoHeight = 1080;
  lg.menusWidth = 2560;
  lg.menusHeight = 1440;
  lg.preferredWidth = 3840;
  lg.preferredHeight = 2160;

  // the case of the test of 2026-11-18: 3840x2160 at 23.976 Hz
  {
    const Choice c = Find(lg, c5);
    Check(c.index == 9 && c.multiple == 1 && c.why == Why::PREFERRED,
          "LG C5, menus at 1440p, 1080p film: 3840x2160 at 23.976 Hz: " + Name(c5, c));
  }
  // the same display with a whitelist of the user: Kodi's choice is the user's
  {
    Query q = lg;
    q.userWhitelist = true;
    const Choice c = Find(q, c5);
    Check(c.index < 0 && c.notLooked, "a whitelist of the user: nothing looked for: " + Name(c5, c));
  }
  // what keeps its way: native Dolby Vision, HDR, 3D, a kind not known, no rate
  {
    struct
    {
      const char* what;
      void (*set)(Query&);
    } kept[] = {
        {"native Dolby Vision", [](Query& q) { q.nativeDV = true; }},
        {"3D", [](Query& q) { q.is3D = true; }},
        {"HDR10", [](Query& q) { q.video = WholeMultipleVideo::HDR10; }},
        {"HDR10+", [](Query& q) { q.video = WholeMultipleVideo::HDR10PLUS; }},
        {"HLG", [](Query& q) { q.video = WholeMultipleVideo::HLG; }},
        {"Dolby Vision", [](Query& q) { q.video = WholeMultipleVideo::DOLBY_VISION; }},
        {"not known", [](Query& q) { q.video = WholeMultipleVideo::UNKNOWN; }},
        {"no frame rate", [](Query& q) { q.fps = 0.0; }},
    };
    for (const auto& k : kept)
    {
      Query q = lg;
      k.set(q);
      const Choice c = Find(q, c5);
      Check(c.index < 0 && c.notLooked, std::string("kept as before: ") + k.what + ": " + Name(c5, c));
    }
  }
  // the Samsung Odyssey G60SD: no mode of 23.976 or 47.952 Hz at any size: nothing, as before
  {
    const std::vector<Mode> g60sd = {{2560, 1440, 359.999, true}, {2560, 1440, 239.957, true},
                                     {2560, 1440, 119.998, true}, {2560, 1440, 59.951, true},
                                     {1920, 1080, 119.88, true},  {1920, 1080, 60.0, true},
                                     {1920, 1080, 59.94, true},   {1920, 1080, 50.0, true}};
    Query q = lg;
    q.menusWidth = 2560;
    q.menusHeight = 1440;
    q.preferredWidth = 2560;
    q.preferredHeight = 1440;
    const Choice c = Find(q, g60sd);
    Check(c.index < 0 && !c.notLooked, "the G60SD has no mode at 23.976 or 47.952 Hz: none: " + Name(g60sd, c));
    // ... but 25 fps goes at 50 Hz, twice its rate, at 1920x1080, the only size with it
    q.fps = 25.0;
    const Choice d = Find(q, g60sd);
    Check(d.index == 7 && d.multiple == 2 && d.why == Why::UNDER_MENUS,
          "the G60SD, 25 fps: 1920x1080 at 50 Hz, twice the rate: " + Name(g60sd, d));
  }
  // the order: the preferred size first, then the nearest at or above the menus, then the largest under them
  {
    const std::vector<Mode> m = {{1920, 1080, film, true}, {5120, 2160, film, true}, {3840, 2160, film, true},
                                 {2560, 1440, 120.0, true}};
    Query q = lg;
    q.preferredWidth = 5120;
    q.preferredHeight = 2160;
    Check(Find(q, m).index == 1, "the preferred size first: " + Name(m, Find(q, m)));
    q.preferredWidth = 7680;
    q.preferredHeight = 4320;
    Check(Find(q, m).index == 2 && Find(q, m).why == Why::ABOVE_MENUS,
          "no preferred size at the rate: the nearest above the menus (3840x2160, not 5120x2160): " +
              Name(m, Find(q, m)));
    const std::vector<Mode> under = {{1280, 720, film, true}, {1920, 1080, film, true}, {2560, 1440, 120.0, true}};
    Check(Find(q, under).index == 1 && Find(q, under).why == Why::UNDER_MENUS,
          "only sizes under the menus: the largest, 1920x1080: " + Name(under, Find(q, under)));
    // never under the size of the video: a film of 3840x2160 takes no 1920x1080
    q.videoWidth = 3840;
    q.videoHeight = 2160;
    Check(Find(q, under).index < 0, "a 2160p film and only 1080p at its rate: none: " + Name(under, Find(q, under)));
    // the rate itself before twice it, at whatever size
    const std::vector<Mode> twice = {{3840, 2160, 2 * film, true}, {1920, 1080, film, true}};
    Query r = lg;
    Check(Find(r, twice).index == 1 && Find(r, twice).multiple == 1,
          "the rate itself at 1080p before twice it at the preferred size: " + Name(twice, Find(r, twice)));
    const std::vector<Mode> onlyTwice = {{3840, 2160, 2 * film, true}};
    Check(Find(r, onlyTwice).index == 0 && Find(r, onlyTwice).multiple == 2,
          "twice the rate where nothing has the rate: " + Name(onlyTwice, Find(r, onlyTwice)));
    // a mode of another kind (interlaced, 3D) is never taken
    const std::vector<Mode> other = {{3840, 2160, film, false}};
    Check(Find(r, other).index < 0, "a mode of another kind than the menus: none");
    // a mode within Kodi's 0.01 Hz, and not further
    const std::vector<Mode> near = {{3840, 2160, 23.98, true}, {3840, 2160, 24.0, true}};
    Check(Find(r, near).index == 0, "23.98 Hz is 23.976 within 0.01 Hz, 24 Hz is not: " + Name(near, Find(r, near)));
  }
  // a video not known to be progressive: no mode of 25 or 30 Hz (Kodi's default whitelist), 50 Hz yes
  {
    const std::vector<Mode> m = {{3840, 2160, 25.0, true}, {3840, 2160, 50.0, true}, {2560, 1440, 119.998, true}};
    Query q = lg;
    q.fps = 25.0;
    q.progressive = false;
    const Choice c = Find(q, m);
    Check(c.index == 1 && c.multiple == 2, "25 fps not known to be progressive: 50 Hz, not 25: " + Name(m, c));
    q.progressive = true;
    Check(Find(q, m).index == 0, "25p known to be progressive: 25 Hz");
    // 24 Hz is always taken, as Kodi's whitelist takes it
    const std::vector<Mode> m24 = {{3840, 2160, 24.0, true}};
    q.fps = 24.0;
    q.progressive = false;
    Check(Find(q, m24).index == 0, "24 fps not known to be progressive: 24 Hz still");
  }
  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
