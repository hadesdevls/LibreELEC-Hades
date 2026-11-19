// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of CDVFrameTest (xbmc/windowing/gbm/drm/DVFrameTest.h): which commits are
// tested before they are made. Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/windowing/gbm/drm
// dvframetest_test.cpp. With -DOLD_RULE it checks the rule before it (every commit tested), which
// the checks of the frames without a test then fail.
#ifndef OLD_RULE
#include "DVFrameTest.h"
using KODI::WINDOWING::GBM::CDVFrameTest;
#else
struct CDVFrameTest // the rule before: every commit was tested first
{
  static constexpr unsigned int FRAMES_TESTED = 240;
  bool Tests(bool, bool) const { return true; }
  void Accepted(bool, bool) {}
};
#endif

#include <cstdio>

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

// n frames of native output committed (accepted): how many of them were tested first
static unsigned int Frames(CDVFrameTest& t, unsigned int n)
{
  unsigned int tested = 0;
  for (unsigned int i = 0; i < n; ++i)
  {
    tested += t.Tests(true, false);
    t.Accepted(true, false);
  }
  return tested;
}

int main()
{
  CDVFrameTest t;
  Check("a commit of the ordinary output is always tested (its refusal falls back)",
        t.Tests(false, false) && t.Tests(false, true));
  Check("a modeset of native output is always tested", t.Tests(true, true));
  t.Accepted(true, true); // the first frame of the transport
  Check("after it, its first 240 frames are tested (their tests are timed)", Frames(t, 240) == 240);
  Check("... the frames after them are not", !t.Tests(true, false) && Frames(t, 1000) == 0);
  Check("... while the ordinary output and modesets still are", t.Tests(false, false) && t.Tests(true, true));
  // commits of the ordinary output in between change nothing of it
  t.Accepted(false, true);
  t.Accepted(false, false);
  Check("commits of the ordinary output in between change nothing", !t.Tests(true, false));
  // a frame that the driver refused was not accepted: it does not count
  Check("a modeset of native output (a change of mode, the next video) starts the count again",
        [&] { t.Accepted(true, true); return Frames(t, 240) == 240 && !t.Tests(true, false); }());
  {
    CDVFrameTest u;
    unsigned int tested = 0;
    for (unsigned int i = 0; i < 300; ++i)
      tested += u.Tests(true, false); // never accepted: refused frames
    Check("frames that were refused do not count: they are tested as long as none is taken", tested == 300);
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
