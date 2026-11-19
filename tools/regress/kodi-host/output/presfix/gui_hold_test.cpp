/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): the GUI while a change of mode waits for
 * the output of the video that opens (CModeChangeHold, START; kodi-9284). the code before the fix commits nothing
 * meanwhile (PresentRender(): ModeHoldCommits() false, sleep), and the GUI, the "loading" of a
 * stream among it, froze for up to START_MS (3 s). With the tree, a frame of the GUI goes out alone
 * in the mode on the screen (CModeChangeHold::GuiGoesOn(), CWinSystemGbm::FlipHeldGui(),
 * CDRMAtomic::FlipPageAlone(): a request of its own with the buffer, no modeset, nothing of what is
 * queued), and the mode still goes out once, with the output of the video.
 *
 * The real CModeChangeHold decides; the loop and the commits are transcribed. The GUI animates
 * (a frame drawn every refresh of 60 Hz); the renderer of the video is configured 2.5 s after the
 * hold begins (a stream over the network).
 */
#include "windowing/gbm/ModeChangeHold.h"

#include <algorithm>
#include <cstdio>
#include <string>

using KODI::WINDOWING::GBM::CModeChangeHold;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

template<typename H>
concept GuiAware = requires(const H h) { h.GuiGoesOn(); };

struct Result
{
  int guiShownInHold = 0;
  int longestFreezeMs = 0;
  int modeCommits = 0; //!< commits that carried the mode
  bool modeWithOutput = false; //!< ... the first one also carried the output of the video
  int modeOnScreenDuringHold = 0; //!< GUI frames shown in the new mode before the hold ended
};

template<typename H>
static Result Run(CModeChangeHold::Kind kind)
{
  H hold;
  Result r;
  bool needModeset = true; // HoldModeChange(): SetVideoMode()
  bool outputQueued = false;
  bool modeOnScreen = false;
  int lastShown = 0;
  if (kind == CModeChangeHold::Kind::START)
    hold.Start(0);
  else
    hold.Stop(0);
  bool held = true;
  for (int ms = 0; ms <= 4000; ms += 17)
  {
    if (held && ms >= 2500 && kind == CModeChangeHold::Kind::START)
    {
      outputQueued = true; // the renderer is configured: SetHDR() queues
      hold.Configured();
    }
    if (held && ms >= 1000 && kind == CModeChangeHold::Kind::STOP)
      hold.ModeChange();
    held = hold.Holding() != CModeChangeHold::Kind::NONE;
    if (held && !hold.Commits(ms))
    {
      bool shown = false;
      if constexpr (GuiAware<H>)
      {
        if (hold.GuiGoesOn())
        {
          // FlipHeldGui(): the buffer alone, in the mode on the screen
          shown = true;
          r.guiShownInHold++;
          r.modeOnScreenDuringHold += modeOnScreen;
        }
      }
      if (shown)
        lastShown = ms;
      r.longestFreezeMs = std::max(r.longestFreezeMs, ms - lastShown);
      continue;
    }
    held = false;
    // an ordinary commit: FlipPage()
    if (needModeset)
    {
      ++r.modeCommits;
      if (r.modeCommits == 1)
        r.modeWithOutput = outputQueued;
      modeOnScreen = true;
      needModeset = false;
    }
    outputQueued = false;
    lastShown = ms;
  }
  return r;
}

template<typename H>
static void CheckStop()
{
  if constexpr (GuiAware<H>)
  {
    H stop;
    stop.Stop(0);
    Check(!stop.GuiGoesOn(), "STOP: no frame of the GUI goes out while the end of the HDR "
                             "signalling waits (the screen has the signalling of the video)");
  }
}

int main()
{
  std::printf("tree: %s\n", GuiAware<CModeChangeHold> ? "CModeChangeHold::GuiGoesOn()" : "the code before the fix");
  const Result start = Run<CModeChangeHold>(CModeChangeHold::Kind::START);
  std::printf("model: START: %d frames of the GUI shown during the hold, longest freeze %d ms, the "
              "mode in %d commit(s), with the output of the video: %s\n",
              start.guiShownInHold, start.longestFreezeMs, start.modeCommits,
              start.modeWithOutput ? "yes" : "no");
  Check(start.longestFreezeMs <= 50, "START: the GUI goes on while the change of mode waits");
  Check(start.modeCommits == 1 && start.modeWithOutput && start.modeOnScreenDuringHold == 0,
        "START: the mode still goes out once, with the output of the video, and not with a frame of "
        "the GUI");
  CheckStop<CModeChangeHold>();
  std::printf(g_failures ? "GUI-HOLD-TEST FAILED (%d)\n" : "GUI-HOLD-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
