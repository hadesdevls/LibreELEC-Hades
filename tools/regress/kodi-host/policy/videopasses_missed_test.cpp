// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: a pass of the interface loop in which one refresh is missed while a
// video plays is counted for the line of the minute (CVideoPasses, xbmc/application/VideoPasses.h of the tree
// given with -I). Issue #3: one gap of 83 ms between frames at 23.976 Hz as the Settings window opened, which the
// bound of two refresh periods (the code before the fix) counted or not by a millisecond. With that header the checks of
// one missed refresh fail; with the fix they pass.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/application videopasses_missed_test.cpp
#include "VideoPasses.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

int main()
{
  const double film = 1000.0 / 23.976; // 41.708 ms
  const double sixty = 1000.0 / 60.0;
  {
    CVideoPasses v;
    Check("a pass of one refresh (42 ms at 23.976 Hz) is not counted", !v.Count(42, film, 1, 1, 40, 1000));
    Check("nor one of 62 ms (under 1.5 periods)", !v.Count(62, film, 1, 1, 60, 1000) && v.count == 0);
    Check("a pass of 63 ms counts (over 1.5 periods: the loop missed a refresh)",
          v.Count(63, film, 1, 50, 12, 2000) && v.count == 1);
    Check("a pass of 83 ms (one refresh missed, the gap of issue #3) counts, and is the longest",
          v.Count(83, film, 2, 70, 11, 3000) && v.count == 2);
    v.windows = "10004 (Settings.xml)";
    const std::string text = v.Text(13000);
    std::printf("text: %s\n", text.c_str());
    Check("the line says 1.5 refresh periods, the longest, and its split",
          text.find("2 passes took more than 1.5 refresh periods") != std::string::npos &&
              text.find("the longest 83 ms") != std::string::npos &&
              text.find("process 2, frame move 70, render 11") != std::string::npos &&
              text.find("Settings.xml") != std::string::npos);
  }
  {
    CVideoPasses v;
    Check("at 60 Hz, 25 ms is not counted", !v.Count(25, sixty, 0, 0, 25, 0));
    Check("at 60 Hz, 26 ms is", v.Count(26, sixty, 0, 0, 26, 0) && v.count == 1);
  }
  {
    CVideoPasses v;
    Check("with no refresh known nothing counts", !v.Count(500, 0.0, 1, 1, 1, 0) && v.count == 0);
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
