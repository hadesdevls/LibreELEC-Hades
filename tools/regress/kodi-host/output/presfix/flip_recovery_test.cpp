/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): when the output is turned off and on again
 * for flips that do not come (CWinSystemGbmGLESContext::FlipStalled(), kodi-9207), with a driver
 * that takes the modeset that turns it on and then fails to apply it, saying nothing
 * (nvidia-drm 615.78.08, nvidia-drm-modeset.c:821-846 and 940-946: "must not return failure after
 * calling drm_atomic_helper_swap_state()"; a test of 2026-10-14, "Failed to apply
 * atomic modeset. Error code: -22" in dmesg and nothing in kodi.log).
 *
 * With the tree's windowing/gbm/FlipStallRecovery.h the real CFlipStallRecovery decides. Without it
 * (the code before the fix) its rule is transcribed (WinSystemGbmGLESContext.cpp:110-116 and 713-719: a reset from
 * the second stall in a row, at most once a minute and three times while Kodi runs).
 *
 * What a frame costs while the flips do not come, from the code and the driver: the wait for the
 * flip of the frame before (FLIP_STALL_WAIT, 2 s), then a commit that waits, which nvidia-drm
 * completes after 3 s; a reset adds the commit that turns the output off (3 s) and the one that
 * turns it on. A frame whose flip comes costs a refresh.
 */
#if __has_include("windowing/gbm/FlipStallRecovery.h")
#include "windowing/gbm/FlipStallRecovery.h"
#define HAVE_FIX 1
#else
#define HAVE_FIX 0
#endif

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

#if HAVE_FIX
using Recovery = KODI::WINDOWING::GBM::CFlipStallRecovery;
#else
// the code before the fix, transcribed
struct Recovery
{
  unsigned int stalls = 0;
  unsigned int resets = 0;
  Clock::time_point last{};
  Clock::time_point lastReset{};
  bool Stalled(Clock::time_point now)
  {
    stalls = stalls && now - last < 10s ? stalls + 1 : 1;
    last = now;
    const bool reset = stalls >= 2 && resets < 3 && (!resets || now - lastReset >= 60s);
    if (reset)
    {
      lastReset = now;
      ++resets;
    }
    return reset;
  }
  bool Came(Clock::time_point now)
  {
    if (!stalls || now - last < 10s)
      return false;
    stalls = 0;
    return true;
  }
};
#endif

struct Outcome
{
  std::vector<double> resets; //!< seconds from the start, of each reset
  double recovered = -1; //!< when the flips came again (-1: never)
};

/*!
 * Flips stop at failStart (s) for the next failures; in each failure the reset number
 * comesBack[i] (1-based, in that failure) brings the display back (0: none does). Simulated for
 * seconds.
 */
static Outcome Run(Recovery& r, double seconds, const std::vector<double>& failStart,
                   const std::vector<int>& comesBack)
{
  Outcome out;
  const auto t0 = Clock::time_point{} + 1000s;
  double t = 0;
  int failure = -1;
  bool dead = false;
  int resetsHere = 0;
  while (t < seconds)
  {
    if (!dead && failure + 1 < static_cast<int>(failStart.size()) && t >= failStart[failure + 1])
    {
      ++failure;
      dead = true;
      resetsHere = 0;
    }
    if (!dead)
    {
      r.Came(t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t)));
      t += 1.0 / 60;
      continue;
    }
    t += 2.0; // FLIP_STALL_WAIT
    const auto now =
        t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t));
    if (r.Stalled(now))
    {
      out.resets.push_back(t);
      ++resetsHere;
      t += 3.0 + 0.1; // the output off (completes the flips), and on
      if (comesBack[failure] && resetsHere == comesBack[failure])
      {
        dead = false;
        if (out.recovered < 0 || failure > 0)
          out.recovered = t;
        t += 1.0 / 60;
        continue;
      }
    }
    t += 3.0; // the commit of this frame waits; nvidia-drm completes it after 3 s
  }
  return out;
}

static void Report(const char* what, const Outcome& o)
{
  std::string list;
  for (double s : o.resets)
    list += " " + std::to_string(static_cast<int>(s + 0.5));
  std::printf("model: %-62s %zu resets at s:%s; flips back at %.1f s\n", what, o.resets.size(),
              list.c_str(), o.recovered);
}

int main()
{
  std::printf("tree: %s\n", HAVE_FIX ? "CFlipStallRecovery (FlipStallRecovery.h)" : "the code before the fix");
  {
    // the test of 2026-10-14: the first reset does not bring the display back, the second would
    Recovery r;
    const Outcome o = Run(r, 300, {0}, {2});
    Report("dead from 0 s; the second reset brings the display back", o);
    Check(o.recovered >= 0 && o.recovered <= 20,
          "a reset that did not bring the display back is tried again within seconds, not a "
          "minute later");
  }
  {
    Recovery r;
    const Outcome o = Run(r, 300, {0}, {6});
    Report("dead from 0 s; the sixth reset brings it back", o);
    Check(o.recovered >= 0 && o.recovered <= 90,
          "resets go on while the flips do not come (the sixth within a minute and a half)");
  }
  {
    Recovery r;
    const Outcome o = Run(r, 600, {0}, {0});
    Report("dead for 10 minutes", o);
    bool spaced = o.resets.size() >= 2;
    bool growing = true;
    for (size_t i = 1; i < o.resets.size(); ++i)
    {
      const double gap = o.resets[i] - o.resets[i - 1];
      spaced = spaced && gap <= 64 + 8.2; // the longest wait, and one stalled frame
      if (i >= 2)
        growing = growing && gap + 0.01 >= o.resets[i - 1] - o.resets[i - 2];
    }
    spaced = spaced && 600 - o.resets.back() <= 64 + 2 * 8.2;
    Check(spaced, "while the flips do not come, the output is tried again at least about once a "
                  "minute, to the end");
    Check(growing && o.resets.size() <= 20,
          "... with growing waits: no reset at every frame for 10 minutes");
  }
  {
    // two failures: the second starts again with short waits
    Recovery r;
    const Outcome o = Run(r, 900, {0, 400}, {4, 2});
    Report("two failures, 400 s apart (fourth and second resets bring it back)", o);
    Check(o.recovered >= 400 && o.recovered <= 420,
          "a later failure is met as the first one: the output is tried again within seconds");
  }
  std::printf(g_failures ? "FLIP-RECOVERY-TEST FAILED (%d)\n" : "FLIP-RECOVERY-TEST PASSED\n",
              g_failures);
  return g_failures ? 1 : 0;
}
