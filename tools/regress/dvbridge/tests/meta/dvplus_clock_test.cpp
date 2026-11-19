// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades 2026-10-27, TEST ONLY: where the first picture of a scene of Dolby Vision sent as HDR10+ counts the next vblank
// from (KODI::WINDOWING::GBM::CDVPlusClock, xbmc/windowing/gbm/DVPlusClock.h of the tree) and how long it may then wait
// (dvbridge_dvplus_wait_budget(), tools/dvbridge), on the host:
//   C1 the vblank ioctl gives a vblank: from it, also where a flip is known;
//   C2 no vblank (nvidia-drm), a flip known in this mode: from the flip, the next vblank on its grid;
//   C3 every flip anchors again: from the last one, with its own phase;
//   C4 a flip that did not come, or whose time could not be read: nothing until the next one;
//   C5 a change of mode (another period), a new video: nothing from the flips before;
//   C6 a flip of more than a second ago, no period: no wait.
// usage: dvplus_clock_test
extern "C"
{
#include "dvbridge_dvplus.h"
}
#include "windowing/gbm/DVPlusClock.h"

#include <cstdio>

using KODI::WINDOWING::GBM::CDVPlusClock;

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

int main()
{
  const int64_t P24 = 41708333, P60 = 16683333, M = DVBRIDGE_DVPLUS_WAIT_MARGIN;
  const int64_t F = 10000000000LL; // a flip at 10 s
  CDVPlusClock::Source source;
  {
    CDVPlusClock c;
    c.Flipped(F, P24);
    const int64_t a = c.Anchor(F + 5000000, P24, &source);
    Check(source == CDVPlusClock::Source::VBLANK && a == F + 5000000, "C1 the vblank, also with a flip known");
    Check(dvbridge_dvplus_wait_budget(F + 15000000, a, P24, M) == F + 5000000 + P24 - M - (F + 15000000),
          "C1 ... until the margin before the vblank after it");
  }
  {
    CDVPlusClock c;
    c.Flipped(F, P24);
    const int64_t now = F + 2 * P24 + P24 / 2;
    const int64_t a = c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::FLIP && a == F, "C2 no vblank: the last flip");
    Check(dvbridge_dvplus_wait_budget(now, a, P24, M) == F + 3 * P24 - M - now,
          "C2 ... the next vblank on its grid, two and a half periods on");
    Check(dvbridge_dvplus_wait_budget(F + 3 * P24 - M / 2, a, P24, M) < 0, "C2 ... within the margin of it: no wait");
  }
  {
    CDVPlusClock c;
    c.Flipped(F, P24);
    const int64_t F2 = F + P24 + 300000; // the next flip, 0.3 ms later on the clock than the grid of the first
    c.Flipped(F2, P24);
    const int64_t a = c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::FLIP && a == F2, "C3 every flip anchors again: the last one");
    const int64_t now = F2 + 10000000;
    Check(dvbridge_dvplus_wait_budget(now, a, P24, M) == F2 + P24 - M - now, "C3 ... with its own phase");
  }
  {
    CDVPlusClock c;
    c.Flipped(F, P24);
    c.Lost();
    c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::NONE, "C4 a flip that did not come: nothing");
    c.Flipped(0, P24);
    c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::NONE, "C4 a flip whose time could not be read: nothing");
    c.Flipped(F, P24);
    c.Flipped(0, P24);
    c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::NONE, "C4 ... also after one that could");
    c.Flipped(F + P24, P24);
    c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::FLIP, "C4 the next flip that comes: from it again");
  }
  {
    CDVPlusClock c;
    c.Flipped(F, P60);
    c.Anchor(0, P24, &source);
    Check(source == CDVPlusClock::Source::NONE, "C5 a flip in a mode of another period: nothing");
    c.Lost(); // (a change of mode of the same period, a new video)
    c.Anchor(0, P60, &source);
    Check(source == CDVPlusClock::Source::NONE, "C5 a change of mode: nothing from the flips before");
  }
  {
    CDVPlusClock c;
    c.Flipped(F, P24);
    const int64_t a = c.Anchor(0, P24, &source);
    Check(dvbridge_dvplus_wait_budget(F + 1500000000LL, a, P24, M) == 0, "C6 a flip of more than a second ago: no wait");
    c.Anchor(0, 0, &source);
    Check(source == CDVPlusClock::Source::NONE, "C6 no period: nothing");
  }
  std::printf("%s: %d of %d\n", g_failures ? "FAILED" : "ALL PASSED", g_checks - g_failures, g_checks);
  return g_failures != 0;
}
