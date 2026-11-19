/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the mode of the menus of each display (windowing/gbm/DisplayModeMemory.h):
 * a start or a move of Kodi to another display takes the mode that display had, or the one it prefers,
 * not the mode of the settings set on another display (the LG C5 of 2026-11-18, which started in the
 * 2560x1440 of the Samsung Odyssey G60SD); with one display, the mode of the settings as always.
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -o t display_modes_test.cpp && ./t
 */
#include "windowing/gbm/DisplayModeMemory.h"

#include <cstdio>
#include <string>

using namespace KODI::WINDOWING::GBM::DISPLAY_MODES;
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

int main()
{
  const std::string samsung = Key("Samsung Electric Company", "Odyssey G60SD");
  const std::string lg = Key("LG Electronics", "LG TV SSCR2");
  const std::string samsungMode = "02560x01440119.99759pstd";
  const std::string lgMode = "03840x02160120.00000pstd";

  Check(samsung == "Samsung Electric Company / Odyssey G60SD", "the key: make / model");
  Check(Key("", "").empty(), "no make and no model: no key");
  Check(Key("A\tB", "C\nD") == "A B / C D", "a tab or a line break in them: a space");

  // one display only: the mode of the settings, as always
  CDisplayModeMemory m;
  Check(m.Empty() && m.Choose(lg, samsungMode) == samsungMode,
        "nothing known yet: the mode of the settings (Kodi on one display, as before)");
  Check(m.Choose("", lgMode) == lgMode, "a display without a key: the mode of the settings");

  // the test of 2026-11-18: Kodi showed on the G60SD, then starts on the LG C5
  Check(m.Remember(samsung, samsungMode), "the G60SD noted");
  Check(!m.Remember(samsung, samsungMode), "... the same again changes nothing");
  Check(m.Choose(lg, samsungMode) == DESKTOP,
        "the LG C5, new to Kodi, with the settings at the G60SD's 2560x1440: the mode it prefers");
  Check(m.Choose(lg, samsungMode) != samsungMode, "... not the mode of the settings (1.1e took it)");
  Check(m.Remember(lg, lgMode), "the LG C5 set to 3840x2160 at 120 Hz");
  Check(m.Choose(lg, samsungMode) == lgMode, "back on the LG C5: its own 3840x2160");
  Check(m.Choose(samsung, lgMode) == samsungMode, "back on the G60SD: its own 2560x1440");
  Check(m.Choose(lg, lgMode) == lgMode, "the settings already the LG C5's: as they are");

  // kept as text, read back
  CDisplayModeMemory n;
  n.Parse(m.Text());
  Check(n.Of(samsung) == samsungMode && n.Of(lg) == lgMode, "written and read back");
  n.Parse("no tab here\n\tno key\nkey\t\nkey\tmode\textra\n" + lg + "\t" + lgMode + "\r\n");
  Check(n.Of(lg) == lgMode && !n.Of("key") && !n.Of("no tab here"), "lines that are not key<TAB>mode: left out");
  Check(!n.Remember(lg, "a\tb") && !n.Remember("", lgMode), "a mode with a tab, or no key: not noted");

  // the displays used last are kept
  CDisplayModeMemory l;
  for (size_t i = 0; i < LIMIT + 3; ++i)
    l.Remember("display " + std::to_string(i), "DESKTOP");
  Check(!l.Of("display 0") && !l.Of("display 2") && l.Of("display 3") && l.Of("display " + std::to_string(LIMIT + 2)),
        "at most LIMIT displays, the oldest dropped");
  l.Remember("display 3", lgMode);
  l.Remember("display new", lgMode);
  Check(l.Of("display 3") == lgMode, "a display used again is kept as the newest");

  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
