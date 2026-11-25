// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: the wait for the last pictures of a file at its end (xbmc/cores/VideoPlayer/EofPresentation.h,
// CVideoPlayer::OnExit()): while pictures are queued, for a file that played to its end and has video, bounded.
#include "EofPresentation.h"

#include <cstdio>

using namespace KODI::VIDEOPLAYER::EOF_PRESENTATION;
using std::chrono::milliseconds;

namespace
{
int g_checks = 0, g_failures = 0;
void Check(bool ok, const char* what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}
} // namespace

int main()
{
  Check(Waits(true, true, 3, milliseconds{0}), "the end of the file, 3 pictures queued: it waits");
  Check(Waits(true, true, 1, milliseconds{1999}), "... still, just under the bound");
  Check(!Waits(true, true, 1, milliseconds{2000}), "... not past the bound (a render loop that does not present)");
  Check(!Waits(true, true, 0, milliseconds{0}), "nothing queued: no wait");
  Check(!Waits(false, true, 3, milliseconds{0}), "a stop, an error or another file: no wait");
  Check(!Waits(true, false, 3, milliseconds{0}), "no video stream: no wait");
  Check(MAX_WAIT <= milliseconds{2000} && STEP <= milliseconds{20},
        "the bound and the step stay short (the end of playback is not held up)");
  std::printf("EOF-PRESENTATION %d checks, %d failed\n", g_checks, g_failures);
  if (!g_failures)
    std::printf("ALL PASSED\n");
  return g_failures ? 1 : 0;
}
