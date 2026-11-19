/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.1): the change of mode from TMDS to HDMI FRL on nvidia-drm and the way back from a hang of it
 * (windowing/gbm/FrlModeChange.h): which changes of mode go out with the output off first (OffFirst()), the TMDS
 * mode that the menus go to (PickTmdsMode(), on the modes of the LG C5 as nvidia-drm gives them on the
 * RTX 4080 SUPER of the tests) and when they go to it (Fallback()). Built with the tree's header:
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -I <kodi>/xbmc/windowing/gbm -o t frl_mode_change_test.cpp
 */
#include "windowing/gbm/FrlModeChange.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM::FRL_MODE_CHANGE;
using namespace std::chrono_literals;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

// NV_OUTPUT_LINK (nvidia-open-kernel-modules-9057): bits 0-7 the kind, 8-15 the FRL rate, 16-23 the format
static uint64_t Link(unsigned kind, unsigned rate = 0, unsigned format = 0)
{
  return kind | rate << 8 | format << 16;
}

int main()
{
  // ---- OffFirst(): 3840x2160 at 23.976 Hz (296.7 MHz, TMDS) back to 3840x2160 at 120 Hz (1188 MHz, FRL)
  Check(OffFirst(Link(1), 1188000, true, false), "TMDS to 1188 MHz: the output off first");
  Check(OffFirst(Link(2), 1188000, true, false), "TMDS scrambled (2560x1440 at 120 Hz) to 1188 MHz: off first");
  Check(!OffFirst(Link(3, 4), 1188000, true, false), "FRL to FRL: one commit, as NVKMS shuts the head down anyway");
  Check(!OffFirst(Link(1), 594000, true, false), "TMDS to 594 MHz (3840x2160 at 60 Hz, TMDS): one commit");
  Check(!OffFirst(Link(1), 600000, true, false), "TMDS to 600 MHz, the most TMDS carries: one commit");
  Check(OffFirst(Link(1), 600001, true, false), "... and above it: off first");
  Check(!OffFirst(Link(4), 1188000, true, false), "DisplayPort: one commit");
  Check(!OffFirst(std::nullopt, 1188000, true, false), "a driver that does not say the link: one commit");
  Check(!OffFirst(Link(0), 1188000, true, false), "nothing driven (link 0): one commit");
  Check(!OffFirst(Link(1), 1188000, false, false), "an output meant to be off: nothing to turn off");
  Check(!OffFirst(Link(1), 1188000, true, true), "a move to another output: one commit");

  // ---- PickTmdsMode(): the modes of the LG C5 (HDMI-A-3 of the RTX 4080 SUPER), with their clocks
  const std::vector<Mode> c5 = {
      {3840, 2160, 120.0f, false, false, 1188000},  {3840, 2160, 119.88f, false, false, 1186813},
      {3840, 2160, 100.0f, false, false, 1188000},  {3840, 2160, 60.0f, false, false, 594000},
      {3840, 2160, 59.94f, false, false, 593407},   {3840, 2160, 50.0f, false, false, 594000},
      {3840, 2160, 30.0f, false, false, 297000},    {3840, 2160, 23.976f, false, false, 296703},
      {2560, 1440, 120.0f, false, false, 497750},   {1920, 1080, 120.0f, false, false, 297000},
      {1920, 1080, 60.0f, true, false, 74250},      {3840, 2160, 24.0f, false, true, 297000},
  };
  {
    const auto p = PickTmdsMode(c5, {3840, 2160, 120.0f, false, false, 1188000});
    Check(p && *p == 3, "the menus at 3840x2160 at 120 Hz: 3840x2160 at 60 Hz (594 MHz)");
  }
  {
    const auto p = PickTmdsMode(c5, {3840, 2160, 119.88f, false, false, 1186813});
    Check(p && *p == 4, "the menus at 119.88 Hz: 59.94 Hz, of the same family");
  }
  {
    const auto p = PickTmdsMode(c5, {2560, 1440, 120.0f, false, false, 497750});
    Check(p && *p == 8, "2560x1440 at 120 Hz: itself (TMDS carries it)");
  }
  {
    const auto p = PickTmdsMode(c5, {5120, 2160, 120.0f, false, false, 1485000});
    Check(!p, "a size with no TMDS mode: none");
  }
  {
    // clocks not known: the refresh rate decides (at most 60 Hz)
    std::vector<Mode> unknown = c5;
    for (auto& m : unknown)
      m.clockKHz = 0;
    const auto p = PickTmdsMode(unknown, {3840, 2160, 120.0f, false, false, 0});
    Check(p && *p == 3, "clocks not known: 3840x2160 at 60 Hz by its rate");
  }
  {
    // a mode of 60 Hz whose clock TMDS does not carry is not taken
    std::vector<Mode> m = {{3840, 2160, 60.0f, false, false, 1188000}, {3840, 2160, 30.0f, false, false, 297000}};
    const auto p = PickTmdsMode(m, {3840, 2160, 120.0f, false, false, 1188000});
    Check(p && *p == 1, "a clock over 600 MHz is not TMDS whatever its rate: 30 Hz");
  }
  {
    std::vector<Mode> m = {{3840, 2160, 60.0f, true, false, 297000}, {3840, 2160, 60.0f, false, true, 297000}};
    Check(!PickTmdsMode(m, {3840, 2160, 120.0f, false, false, 1188000}), "interlaced and 3D modes are not taken");
  }

  // ---- Fallback()
  Check(Fallback(true, 17s, true, false, 0) == Action::TMDS_MODE,
        "FRL, flips stalled 17 s after the change of mode (2026-11-18): the menus go to TMDS");
  Check(Fallback(true, 60s, true, false, 2) == Action::TMDS_MODE, "... up to 60 s after it, a third time");
  Check(Fallback(true, 61s, true, false, 0) == Action::SAME_MODE, "61 s after the change of mode: as before");
  Check(Fallback(true, std::nullopt, true, false, 0) == Action::SAME_MODE, "no change of mode seen: as before");
  Check(Fallback(false, 17s, true, false, 0) == Action::SAME_MODE, "over TMDS already: as before");
  Check(Fallback(true, 17s, false, false, 0) == Action::SAME_MODE, "no TMDS mode of that size: as before");
  Check(Fallback(true, 17s, true, true, 0) == Action::SAME_MODE,
        "this failure went to TMDS already and still stalls: the output off and on, as before (no loop)");
  Check(Fallback(true, 17s, true, false, MAX_FALLBACKS) == Action::SAME_MODE,
        "three times since Kodi started: no more (no loop)");

  std::printf(g_failures ? "FRL-MODE-CHANGE-TEST FAILED (%d)\n" : "FRL-MODE-CHANGE-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
