/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): when does CDVPresentStall end native Dolby
 * Vision output once the flips stop coming, with frames presented a frame ahead (K35)? From the
 * test of the review (review-a/tests/stall_ahead_test.cpp), with the real DVPresentStall.h and
 * DVAhead.h of the tree it is built against.
 *
 * What they are fed is transcribed from CWinSystemGbmGLESContext::PresentRender() and
 * DVAheadEnd()/DVAheadFlipped() (WinSystemGbmGLESContext.cpp; DV_FLIP_WAIT_MS = 1000) and from the
 * drivers:
 *  - nvidia-drm 615.78.08 (nvidia-drm-modeset.c:777-807 and 901-936): a commit that does not wait
 *    is refused (EBUSY) only while a flip of that head is in its flip list; one that waits is
 *    applied at once and waits up to 3 s for the list to empty, after which it completes every
 *    flip of it ("Flip event timeout on head"), which signals their out-fences;
 *  - the kernel's helpers (amdgpu, i915; drm_atomic_helper.c, stall_checks()): a commit that does
 *    not wait is refused (EBUSY) while the flip of the commit before is not done; one that waits
 *    waits for it, 10 s at most ("flip_done timed out").
 *
 * With the tree's CDVPresentStall (Presented(plain, ms, ahead), Flipped()) a frame presented a
 * frame ahead counts neither way, and the flip of one that comes resets the count. the code before the fix has
 * Presented(plain, ms) only, which takes such a frame to be in time: there the test fails (D2).
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I <kodi tree>/xbmc
 */
#include "windowing/gbm/DVAhead.h"
#include "windowing/gbm/DVPresentStall.h"

#include <cstdio>
#include <set>
#include <string>

using KODI::WINDOWING::GBM::CDVAhead;
using KODI::WINDOWING::GBM::CDVPresentStall;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

// The tree's interface, or the code before the fix's
template<typename S>
concept AheadAware = requires(S s) {
  s.Presented(true, 0.0, true);
  s.Flipped();
};
template<typename S>
CDVPresentStall::Verdict Tell(S& stall, bool plain, double ms, bool ahead)
{
  if constexpr (AheadAware<S>)
    return stall.Presented(plain, ms, ahead);
  else
    return stall.Presented(plain, ms);
}
template<typename S>
void TellFlipped(S& stall)
{
  if constexpr (AheadAware<S>)
    stall.Flipped();
}

enum class Driver
{
  NVIDIA,
  HELPERS,
};

struct Outcome
{
  bool gaveUp = false;
  int frames = 0; //!< frames presented until native output ended (or the limit)
  double ms = 0; //!< time those frames took
  int stalled = 0; //!< presentations that CDVPresentStall called STALLED
  int aheadFailures = 0;
  double msBeforeDead = 0; //!< time the frames before the engine died took
};

// One native output. period: refresh period (ms); mode of CDVAhead (NEEDED with frames that call
// for it, or NEVER). The flips are dead from frame deadFrom on (0: never), and the flips of the
// frames in lost do not come (a flip lost once, the display engine alive), each followed by frames
// whose flips come.
static Outcome Run(Driver driver,
                   CDVAhead::Mode mode,
                   double period,
                   int deadFrom,
                   const std::set<int>& lost = {},
                   int limit = 200)
{
  CDVPresentStall stall;
  CDVAhead ahead;
  // frames of 9 ms of the 16.7 ms of a 60 Hz refresh call for it (dvahead_test)
  for (int i = 0; i < 24; ++i)
    ahead.Measured(0.54 * period, period);

  bool flipInList = false; //!< nvidia-drm: a flip of the head that has not come
  bool flipNotDone = false; //!< helpers: the flip of the last commit is not done
  bool aheadPending = false; //!< m_dvAheadPending
  bool aheadLost = false; //!< m_dvAheadLost
  bool fenceOfLast = false; //!< the out-fence of the last commit is there to be waited for
  bool lastFlipComes = true; //!< the flip of the last commit comes by itself

  Outcome out;
  for (int frame = 1; frame <= limit && !out.gaveUp; ++frame)
  {
    const bool dead = deadFrom > 0 && frame >= deadFrom;
    double ms = 0;
    bool wantAhead = ahead.Wanted(mode, period);
    const bool fenced = wantAhead;
    // PresentRender(): the flip of the frame before, when its commit did not wait
    if (fenced || aheadPending)
    {
      if (fenceOfLast)
      {
        fenceOfLast = false;
        if (lastFlipComes)
        {
          ms += period; // it comes with the next refresh
          if (aheadPending)
            TellFlipped(stall); // DVAheadFlipped()
          aheadPending = false;
          flipInList = false;
          flipNotDone = false;
        }
        else
          ms += 1000; // WaitFence(..., DV_FLIP_WAIT_MS)
      }
    }
    if (aheadPending)
    {
      wantAhead = false;
      if (!aheadLost)
      {
        aheadLost = true;
        ahead.Failed();
        ++out.aheadFailures;
      }
    }
    // FlipDVBridgePage(): a frame ahead, or the way that waits; one refused with its fences is
    // made again the way that waits
    bool presentedAhead = false;
    bool refusedWithFences = false;
    auto blocking = [&]()
    {
      if (driver == Driver::NVIDIA)
      {
        // applied at once; waits up to 3 s for its flip list to empty (its own flip and one
        // before it that has not come), then completes every flip of it
        ms += dead || flipInList ? 3000 : period;
        flipInList = false;
      }
      else
      {
        // the reviewer's figure: with the engine dead the commit takes its 10 s
        ms += dead || flipNotDone ? 10000 : period;
        flipNotDone = dead;
      }
      lastFlipComes = !dead;
    };
    if (wantAhead && fenced)
    {
      const bool busy = driver == Driver::NVIDIA ? flipInList : flipNotDone;
      if (busy)
      {
        refusedWithFences = true;
        blocking();
      }
      else
      {
        presentedAhead = true; // returns at once
        const bool comes = !dead && !lost.count(frame);
        if (driver == Driver::NVIDIA)
          flipInList = !comes;
        else
          flipNotDone = !comes;
        lastFlipComes = comes;
      }
    }
    else
      blocking();
    fenceOfLast = presentedAhead;
    // DVAheadEnd()
    if (refusedWithFences)
    {
      ahead.Failed();
      ++out.aheadFailures;
    }
    aheadPending = presentedAhead;
    aheadLost = false;
    if (presentedAhead)
      ahead.Presented();
    // (the transport is live, nothing else pending): DVPresentStalled()
    const auto verdict = Tell(stall, true, ms, presentedAhead);
    out.ms += ms;
    if (!dead)
      out.msBeforeDead = out.ms;
    out.frames = frame;
    if (verdict == CDVPresentStall::Verdict::STALLED)
      ++out.stalled;
    if (verdict == CDVPresentStall::Verdict::GIVE_UP)
      out.gaveUp = true;
  }
  return out;
}

static void Report(const char* what, const Outcome& o)
{
  std::printf("model: %-66s gave up %s after %d frames, %.1f s (%d stalled before, %d frames that "
              "could not be presented a frame ahead)\n",
              what, o.gaveUp ? "yes" : "NO", o.frames, o.ms / 1000.0, o.stalled, o.aheadFailures);
}

int main()
{
  std::printf("CDVPresentStall of the tree: %s\n",
              AheadAware<CDVPresentStall> ? "Presented(plain, ms, ahead) and Flipped()"
                                          : "Presented(plain, ms) only (the code before the fix)");
  const double p60 = 1000.0 / 60;
  const Outcome waiting = Run(Driver::NVIDIA, CDVAhead::Mode::NEVER, p60, 1);
  const Outcome aheadDead = Run(Driver::NVIDIA, CDVAhead::Mode::NEEDED, p60, 1);
  const Outcome helpersWaiting = Run(Driver::HELPERS, CDVAhead::Mode::NEVER, p60, 1);
  const Outcome helpersAhead = Run(Driver::HELPERS, CDVAhead::Mode::NEEDED, p60, 1);
  // flips that come, with a flip lost now and then (frames 50, 400 and 900 of 1200): the display
  // engine is alive
  const Outcome aheadAlive =
      Run(Driver::NVIDIA, CDVAhead::Mode::NEEDED, p60, 0, {50, 400, 900}, 1200);
  // the engine dies after an hour of good frames ahead
  const Outcome aheadLate = Run(Driver::NVIDIA, CDVAhead::Mode::NEEDED, p60, 600, {100}, 1000);
  Report("nvidia-drm, dead, frames presented the way that waits", waiting);
  Report("nvidia-drm, dead, frames presented a frame ahead", aheadDead);
  Report("helpers (amdgpu, i915), dead, the way that waits", helpersWaiting);
  Report("helpers (amdgpu, i915), dead, a frame ahead", helpersAhead);
  Report("nvidia-drm, alive, a frame ahead, three flips lost apart", aheadAlive);
  Report("nvidia-drm, a frame ahead, one flip lost, dead from frame 600", aheadLate);

  Check(waiting.gaveUp && waiting.frames == 2 && waiting.ms <= 6000.5,
        "nvidia-drm, the way that waits: two stalled frames of 3 s end native output (the design "
        "of kodi-9282)");
  Check(helpersWaiting.gaveUp && helpersWaiting.frames == 1, "helpers: one 10 s frame ends it");
  Check(helpersAhead.gaveUp && helpersAhead.ms <= 11000.5 + 1000,
        "helpers, a frame ahead: the next frame waits 1 s for the out-fence, then 10 s, and ends "
        "it");
  // D2: frames presented a frame ahead were taken to be in time and reset the count, so two
  // stalls never came in a row while CDVAhead still presented a frame ahead
  Check(aheadDead.gaveUp && aheadDead.ms <= waiting.ms + 4000.5,
        "nvidia-drm, a frame ahead: native output ends about as soon as the way that waits does "
        "(no more than one wait for an out-fence and one stalled commit later)");
  // ... and what the fix must not do: single lost flips far apart are no dead display
  Check(!aheadAlive.gaveUp && aheadAlive.frames == 1200,
        "nvidia-drm, alive: flips lost once, far apart, do not end native output (the flips "
        "between them start the count again)");
  Check(aheadLate.gaveUp && aheadLate.ms - aheadLate.msBeforeDead <= waiting.ms + 4000.5,
        "a frame ahead, one flip lost long before: once the engine dies, native output ends as "
        "soon as when it dies from the first frame");
  std::printf(g_failures ? "STALL-AHEAD-TEST FAILED (%d)\n" : "STALL-AHEAD-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
