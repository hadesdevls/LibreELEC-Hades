// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades (1.0 RC4), host test of DVBRIDGE::EndOfStreamFrame() (xbmc/windowing/gbm/DVEndOfStream.h): the frame that
// marks the end of the Dolby Vision signal (video/dveos) is sent only with the setting on, the transport live,
// outside Low-Latency, and once; with the setting off, never (the exit from native Dolby Vision is what it was).
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc/windowing/gbm dveos_test.cpp
#include "DVEndOfStream.h"

#include <cstdio>

int main()
{
  int failures = 0, cases = 0;
  for (int i = 0; i < 16; ++i)
  {
    const bool setting = i & 1, live = i & 2, lowLatency = i & 4, sent = i & 8;
    const bool got = DVBRIDGE::EndOfStreamFrame(setting, live, lowLatency, sent);
    const bool want = setting && live && !lowLatency && !sent;
    ++cases;
    if (got != want || (!setting && got))
    {
      ++failures;
      std::printf("FAIL: setting %d live %d lowLatency %d sent %d: %d\n", setting, live, lowLatency, sent, got);
    }
  }
  std::printf("X1 %d cases, %d failed; with the setting off: never\n", cases, failures);
  return failures != 0;
}
