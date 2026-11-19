// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of CPresentLag (xbmc/windowing/gbm/PresentLag.h): how long after the time
// that the player's clock calls for a frame its flip came, per way of presenting it.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/windowing/gbm presentlag_test.cpp
// With -DOLD_RULE: before it nothing was measured (the checks then fail).
#ifndef OLD_RULE
#include "PresentLag.h"
using KODI::WINDOWING::GBM::CPresentLag;
#else
#include <cstdint>
#include <string>
class CPresentLag // before: nothing of it was measured
{
public:
  enum class Way { WAITING, AHEAD, ORDINARY, COUNT };
  void Add(Way, int64_t, int64_t) {}
  unsigned int Count(Way) const { return 0; }
  double AverageMs(Way) const { return 0.0; }
  std::string Text(double) const { return {}; }
  void Reset() {}
};
#endif

#include <cmath>
#include <cstdint>
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
  using Way = CPresentLag::Way;
  constexpr int64_t ms = 1000000;
  const int64_t t0 = 5000 * ms;
  CPresentLag lag;
  Check("nothing told: nothing to say", lag.Text(83.4).empty() && lag.Count(Way::WAITING) == 0);
  // the way that waits: flips 41.7 ms before their frames' times; ahead: 0.5 ms after
  for (int i = 0; i < 24; ++i)
  {
    const int64_t due = t0 + i * 41708333;
    lag.Add(Way::WAITING, due, due - 41708333 + (i % 2) * ms);
    lag.Add(Way::AHEAD, due, due + ms / 2);
  }
  Check("each way is counted apart", lag.Count(Way::WAITING) == 24 && lag.Count(Way::AHEAD) == 24 &&
                                         lag.Count(Way::ORDINARY) == 0);
  Check("a picture before its time is a negative time", std::fabs(lag.AverageMs(Way::WAITING) + 41.2) < 0.1);
  Check("... one after it, positive", std::fabs(lag.AverageMs(Way::AHEAD) - 0.5) < 0.01);
  lag.Add(Way::ORDINARY, t0, t0 + 3000 * ms);
  lag.Add(Way::ORDINARY, t0, t0 - 2500 * ms);
  lag.Add(Way::ORDINARY, 0, t0);
  lag.Add(Way::ORDINARY, t0, 0);
  Check("times more than 2 s apart, or not known, are left out", lag.Count(Way::ORDINARY) == 0);
  const std::string text = lag.Text(125.1);
  std::printf("text: %s\n", text.c_str());
  Check("the line has, per way, the average, the least, the most and how many, and the latency Kodi takes",
        text.find("-41.2 ms (-41.7 to -40.7) over 24 frames presented the way that waits") != std::string::npos &&
            text.find("0.5 ms (0.5 to 0.5) over 24 frames presented a frame ahead") != std::string::npos &&
            text.find("of the ordinary output") == std::string::npos &&
            text.find("125.1 ms ahead of their flips") != std::string::npos);
  lag.Reset();
  Check("after a reset, nothing", lag.Text(1.0).empty());
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
