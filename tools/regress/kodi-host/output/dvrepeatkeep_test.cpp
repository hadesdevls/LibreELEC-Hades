// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of CDVRepeatKeep (xbmc/windowing/gbm/DVRepeatKeep.h): how a picture of
// native Dolby Vision output that repeats is kept on the screen. Build: g++ -std=c++20 -Wall -Wextra
// -I <tree>/xbmc/windowing/gbm dvrepeatkeep_test.cpp. Against the header before it builds as well
// (CommitTook() is then not called) and the checks of a device without vblank fail.
#include "DVRepeatKeep.h"

#include <cerrno>
#include <cstdio>

using KODI::WINDOWING::GBM::CDVRepeatKeep;
using Pace = CDVRepeatKeep::Pace;

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

template<typename K>
static bool Took(K& keep, double ms, double period)
{
  if constexpr (requires { keep.CommitTook(ms, period); })
    return keep.CommitTook(ms, period);
  else
    return false;
}

int main()
{
  const double film = 1000.0 / 23.976;
  {
    // a device with vblanks (amdgpu, i915): as before
    CDVRepeatKeep k;
    k.Begin(false);
    Check("with vblanks, a repeat is paced by them, after either way", k.Way(false) == Pace::VBLANK &&
                                                                         k.Way(true) == Pace::VBLANK &&
                                                                         !k.ByCommit());
    Check("another error is told, and leaves the pictures drawn again in that output",
          k.VBlankFailed(EINVAL) && k.Way(false) == Pace::NONE && !k.DeviceHasNoVBlank());
    k.Begin(false);
    Check("... the next output tries the vblanks again", k.Way(false) == Pace::VBLANK);
    k.Begin(true);
    Check("asked for (dvkeepbycommit), a commit keeps it after a frame that waited", k.Way(false) == Pace::COMMIT &&
                                                                                       k.Way(true) == Pace::VBLANK &&
                                                                                       k.ByCommit());
    k.CommitFailed();
    Check("... a refused commit leaves the vblanks", k.Way(false) == Pace::VBLANK && !k.ByCommit());
  }
  {
    // nvidia-drm with its defaults: no vblank at all
    CDVRepeatKeep k;
    k.Begin(false);
    Check("the first failure of the vblanks with EOPNOTSUPP is told once",
          k.VBlankFailed(EOPNOTSUPP) && !k.VBlankFailed(EOPNOTSUPP) && k.DeviceHasNoVBlank());
    Check("without vblanks, a repeat after a frame that waited is kept by a commit", k.Way(false) == Pace::COMMIT &&
                                                                                       k.ByCommit());
    Check("... after a frame ahead it is drawn again", k.Way(true) == Pace::NONE);
    Check("commits that take a refresh go on", !Took(k, 41.0, film) && !Took(k, 40.0, film) &&
                                                   !Took(k, 41.5, film) && k.Way(false) == Pace::COMMIT);
    Check("two that come back at once, then one that waits: still kept",
          !Took(k, 0.3, film) && !Took(k, 0.3, film) && !Took(k, 41.0, film) && k.Way(false) == Pace::COMMIT);
    Check("the third in a row that comes back at once ends it, told once",
          !Took(k, 0.3, film) && !Took(k, 0.3, film) && Took(k, 0.3, film) && k.Way(false) == Pace::NONE &&
              !k.ByCommit());
    k.Begin(false);
    Check("the next output keeps them by commits again", k.Way(false) == Pace::COMMIT);
    k.CommitFailed();
    Check("a refused commit leaves them drawn again for that output (there are no vblanks)",
          k.Way(false) == Pace::NONE && k.Way(true) == Pace::NONE);
    k.Begin(true);
    Check("... and asked for, the next output has them kept by commits", k.Way(false) == Pace::COMMIT);
    Check("no refresh period known: a commit is not judged", !Took(k, 0.1, 0.0) && !Took(k, 0.1, 0.0) &&
                                                               !Took(k, 0.1, 0.0) && k.Way(false) == Pace::COMMIT);
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
