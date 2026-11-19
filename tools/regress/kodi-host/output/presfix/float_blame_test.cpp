/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): which refused commits of the ordinary
 * output take a video of more than 8 bits back to the 8-bit output (K22,
 * CWinSystemGbmEGLContext::CheckFloatOutput()).
 *
 * the code before the fix holds every refusal that TakeFlipRefused() tells of against the half-float buffer:
 * HDR at 8 bits for that video, and one of the 3 videos after which it is not tried for a while
 * (the reviewer: also when the commit that falls back with the same buffer is taken, and for the
 * commits of Hide()/Show() that FlipStalled() makes). With the tree's
 * windowing/gbm/drm/FloatRefusal.h, the refused request and the request it fell back to are asked
 * again with an 8-bit buffer (CDRMAtomic::RefusedForBuffer()), and JudgeFloatRefusal() decides;
 * only NOT_BUFFER spares the half-float output.
 *
 * The driver is modelled by what it takes: a request is a set of what it carries, and each case
 * says which combinations it refuses. The flow of CheckFloatOutput() is transcribed.
 */
#if __has_include("windowing/gbm/drm/FloatRefusal.h")
#include "windowing/gbm/drm/FloatRefusal.h"
#define HAVE_FIX 1
using KODI::WINDOWING::GBM::FloatRefusal;
using KODI::WINDOWING::GBM::JudgeFloatRefusal;
#else
#define HAVE_FIX 0
#endif

#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <string>

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

using Request = std::set<std::string>;
using Driver = std::function<bool(const Request&)>; //!< whether it takes a request (TEST_ONLY)

[[maybe_unused]] static Request With(Request r, const std::string& buffer)
{
  r.erase("FLOAT");
  r.erase("RGB8");
  r.insert(buffer);
  return r;
}

//! CheckFloatOutput() after a refused test of `refused`, which fell back to `before`; whether the
//! 8-bit output is put back. testRefused: the refusal was of a test (a copy of it is kept), not
//! of a commit whose test passed.
static bool PutsBack8Bit(const Driver& takes, const Request& before, const Request& refused,
                         bool testRefused = true, bool standInMade = true)
{
#if HAVE_FIX
  // RefusalAboutBuffer() -> CDRMAtomic::RefusedForBuffer()
  FloatRefusal about = FloatRefusal::UNKNOWN;
  if (testRefused && standInMade)
  {
    const std::optional<bool> beforeTakes8 = takes(With(before, "RGB8"));
    // (asked whatever the first answer: the decision does not lean on RefusedForBuffer() asking
    // the second only after a yes)
    const std::optional<bool> refusedTakes8 = takes(With(refused, "RGB8"));
    about = JudgeFloatRefusal(beforeTakes8, refusedTakes8);
  }
  return about != FloatRefusal::NOT_BUFFER;
#else
  (void)takes;
  (void)before;
  (void)refused;
  (void)testRefused;
  (void)standInMade;
  return true;
#endif
}

int main()
{
  std::printf("tree: %s\n", HAVE_FIX ? "JudgeFloatRefusal (FloatRefusal.h)" : "the code before the fix");
  const Request frame{"FLOAT", "FB"};
  const Request hdrFrame{"FLOAT", "FB", "HDR"};

  // K22's own case: the HDR metadata of the video with the half-float buffer is refused, and
  // with an 8-bit buffer taken (what the 8-bit output then plays as before)
  const Driver noFloatHdr = [](const Request& r) { return !(r.count("FLOAT") && r.count("HDR")); };
  Check(PutsBack8Bit(noFloatHdr, frame, hdrFrame),
        "HDR metadata refused with the half-float buffer and taken with an 8-bit one: the 8-bit "
        "output is put back (K22)");

  // a mode that the link cannot carry, whatever the buffer
  const Driver noModeB = [](const Request& r) { return !r.count("MODE=B"); };
  Request modeFrame = frame;
  modeFrame.insert("MODE=B");
  Check(!PutsBack8Bit(noModeB, frame, modeFrame),
        "a change of mode that the link cannot carry with any buffer: not held against the "
        "half-float output");

  // FlipStalled(): the output turned on again, which the driver refuses with a dead engine
  const Driver noActive = [](const Request& r) { return !r.count("ACTIVE=1"); };
  Request show{"ACTIVE=1", "MODE=A"};
  Check(!PutsBack8Bit(noActive, frame, show),
        "Show() refused for any buffer (the commits of FlipStalled()): not held against it");

  // safe side: what cannot be told apart is held against it, as before
  const Driver noRgb8 = [](const Request& r) { return !r.count("RGB8") && !r.count("MODE=B"); };
  Check(PutsBack8Bit(noRgb8, frame, modeFrame),
        "the 8-bit buffer is not taken even by the request before: held against it (not known)");
  Check(PutsBack8Bit(noModeB, frame, modeFrame, false),
        "a commit refused after its test passed: held against it, as before");
  Check(PutsBack8Bit(noModeB, frame, modeFrame, true, false),
        "no 8-bit buffer could be made: held against it, as before");
  // HDR metadata refused with any buffer (the display, not the buffer)
  const Driver noHdr = [](const Request& r) { return !r.count("HDR"); };
  Check(!PutsBack8Bit(noHdr, frame, hdrFrame),
        "HDR metadata that the driver refuses with any buffer: not held against it");
  std::printf(g_failures ? "FLOAT-BLAME-TEST FAILED (%d)\n" : "FLOAT-BLAME-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
