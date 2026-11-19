// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of CVideoPasses (xbmc/application/VideoPasses.h): the passes of the interface
// loop over 1.5 refresh periods while a video plays (two before issue #3), for the line of the minute.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/application videopasses_test.cpp
// With -DOLD_RULE: the rule before (no pass with a video playing counted), whose checks then fail.
#ifndef OLD_RULE
#include "VideoPasses.h"
#else
#include <cstdint>
#include <string>
struct CVideoPasses // before: a pass with a video playing was left out of the line of the minute
{
  unsigned int count = 0;
  long long longestMs = 0;
  std::string windows;
  bool Count(long long, double, long long, long long, long long, int64_t) { return false; }
  std::string Text(int64_t) const { return {}; }
};
#endif

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
  const double film = 1000.0 / 23.976;
  const double sixty = 1000.0 / 60.0;
  CVideoPasses v;
  Check("passes of a refresh, or under 1.5, tell nothing", !v.Count(42, film, 1, 1, 40, 1000) &&
                                                       !v.Count(62, film, 1, 1, 60, 1000) && v.count == 0);
  Check("nor with no refresh known", !v.Count(500, 0.0, 1, 1, 1, 1000) && v.count == 0);
  Check("a pass of 208 ms at 23.976 Hz counts, and is the longest", v.Count(208, film, 2, 1, 205, 10000) && v.count == 1);
  v.windows = "12005 (VideoFullScreen.xml) under dialog 13001 (script-addon-seek_dialog.xml)";
  Check("a shorter one counts, and is not the longest", !v.Count(120, film, 100, 1, 19, 20000) && v.count == 2);
  Check("at 60 Hz, 34 ms is over two refreshes", !v.Count(34, sixty, 0, 0, 34, 30000) && v.count == 3);
  const std::string text = v.Text(70000);
  std::printf("text: %s\n", text.c_str());
  Check("the line says how many, the longest, when, how its time went, and the windows on top",
        text.find("3 passes took more than 1.5 refresh periods") != std::string::npos &&
            text.find("the longest 208 ms") != std::string::npos &&
            text.find("of refreshes of 41.7 ms") != std::string::npos &&
            text.find("60.0 s before this line") != std::string::npos &&
            text.find("process 2, frame move 1, render 205") != std::string::npos &&
            text.find("script-addon-seek_dialog.xml") != std::string::npos);
  CVideoPasses gap;
  gap.Count(300, film, -1, -1, -1, 0);
  Check("a gap around a nested loop has no split", gap.Text(1000).find("process") == std::string::npos &&
                                                       gap.Text(1000).find("window none") != std::string::npos);
  CVideoPasses none;
  Check("without any, nothing is added to the line", none.Text(0).empty());
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
