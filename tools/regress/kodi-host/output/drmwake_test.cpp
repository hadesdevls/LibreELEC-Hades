// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of DRMWAKE (xbmc/windowing/gbm/drm/DRMWake.h): what to do with the output
// after a wake, and CSleepWatch, whether the system slept since the output was last looked at.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/windowing/gbm/drm drmwake_test.cpp
// With -DOLD_HEADERS (the header before, without CSleepWatch) the checks of it fail.
#include "DRMWake.h"

#include <cstdint>
#include <cstdio>

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

int main()
{
  using DRMWAKE::Output;
  Check("what to do after a wake is as before",
        DRMWAKE::AfterWake(false, 1) == Output::OFF_BY_KODI &&
            DRMWAKE::AfterWake(true, std::nullopt) == Output::UNKNOWN &&
            DRMWAKE::AfterWake(true, 1) == Output::KEPT && DRMWAKE::AfterWake(true, 0) == Output::SET_AGAIN);
#ifndef OLD_HEADERS
  constexpr int64_t s = 1000000000;
  DRMWAKE::CSleepWatch w;
  Check("the first look tells nothing (the time slept before Kodi started)", w.Look(3600 * s) == 0);
  Check("frames without a suspend tell nothing", w.Look(3600 * s) == 0 && w.Look(3600 * s + 5) == 0);
  Check("a suspend of 40 s is told once, with its time", w.Look(3640 * s + 5) == 40 * s && w.Look(3640 * s + 5) == 0);
  Check("less than a second is no suspend", w.Look(3640 * s + 5 + s - 1) == 0);
  Check("a second is", w.Look(3640 * s + 5 + s - 1 + s) == s);
  // OnSystemWake() looks too: the output path does not handle that wake again
  w.Look(4000 * s);
  Check("a wake that OnWake() looked at first is not told again", w.Look(4000 * s) == 0);
  Check("clocks read across a suspend (CLOCK_BOOTTIME before it, CLOCK_MONOTONIC after) tell nothing, "
        "and the look after them tells the suspend",
        w.Look(3000 * s) == 0 && w.Look(4100 * s) == 1100 * s);
#else
  Check("whether the system slept is seen by the output (CSleepWatch)", false);
#endif
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
