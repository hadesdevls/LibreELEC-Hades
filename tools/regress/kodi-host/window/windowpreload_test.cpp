// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: CWindowPreload (xbmc/guilib/WindowPreload.h of the tree given with -I),
// the windows that open over a video loaded ahead while the menus are idle. A model of the loop of
// CGUIWindowManager::PreloadForVideo(): one window a pass, only while idle, the line once at the end.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/guilib windowpreload_test.cpp
#include "WindowPreload.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

int main()
{
  using R = CWindowPreload::Result;
  // when the menus are idle
  Check("idle: initialized, nothing playing, no modal, screen awake, 2 s without input",
        CWindowPreload::Idle(true, false, false, false, 2));
  Check("not before the windows are there", !CWindowPreload::Idle(false, false, false, false, 9));
  Check("not while something plays (a video, music, pictures)", !CWindowPreload::Idle(true, true, false, false, 9));
  Check("not with a modal dialog open", !CWindowPreload::Idle(true, false, true, false, 9));
  Check("not with the screensaver or DPMS on", !CWindowPreload::Idle(true, false, false, true, 9));
  Check("not within 2 s of an input", !CWindowPreload::Idle(true, false, false, false, 1));

  // the loop: one a pass, only while idle
  CWindowPreload p;
  p.Reset({1, 2, 3, 4, 5});
  const std::vector<bool> idle = {false, true, false, true, true, false, true, true, true};
  std::vector<int> taken;
  std::vector<std::string> lines;
  const char* files[] = {"VideoOSD.xml", "DialogPlayerProcessInfo.xml", "Settings.xml", "VideoOSDSettings.xml",
                         "DialogFullScreenInfo.xml"};
  const R results[] = {R::LOADED, R::LOADED, R::ALREADY, R::NOT_IN_SKIN, R::FAILED};
  for (const bool i : idle)
  {
    if (!i || !p.Pending())
      continue;
    const int id = p.Next();
    taken.push_back(id);
    p.Took(files[id - 1], results[id - 1], id == 1 ? 12.25 : id == 2 ? 30.5 : 0.0);
    const std::string s = p.Summary();
    if (!s.empty())
      lines.push_back(s);
  }
  Check("one window a pass, in order, only on idle passes", taken == std::vector<int>({1, 2, 3, 4, 5}));
  Check("nothing pending after the last", !p.Pending());
  Check("the line once, after the last", lines.size() == 1 && p.Summary().empty());
  const std::string line = lines.empty() ? "" : lines[0];
  std::printf("line: %s\n", line.c_str());
  Check("it says how many were loaded, each with its time, the sum and the longest",
        line.find("GUI: 2 windows that open over a video were loaded ahead") == 0 &&
            line.find("VideoOSD.xml 12.2 ms, DialogPlayerProcessInfo.xml 30.5 ms; 42.8 ms in all, the "
                      "longest 30.5 ms") != std::string::npos);
  Check("and the others by what became of them",
        line.find("; loaded already: Settings.xml") != std::string::npos &&
            line.find("; not in the skin: VideoOSDSettings.xml") != std::string::npos &&
            line.find("; failed: DialogFullScreenInfo.xml") != std::string::npos &&
            line.find("not kept in memory") == std::string::npos);

  // a new skin: the list again, and its own line
  p.Reset({7});
  Check("after Reset() the list is pending again", p.Pending());
  Check("no line before the last", p.Summary().empty());
  p.Next();
  p.Took("SettingsX.xml", R::NOT_KEPT, 0.0);
  const std::string again = p.Summary();
  Check("the line of the new list", again.find("GUI: 0 windows") == 0 &&
                                        again.find("not kept in memory: SettingsX.xml") != std::string::npos &&
                                        again.find("ms in all") == std::string::npos);
  CWindowPreload empty;
  Check("an empty list says nothing", !empty.Pending() && empty.Summary().empty());
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
