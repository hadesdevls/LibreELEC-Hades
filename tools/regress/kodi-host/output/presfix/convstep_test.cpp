// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: the fidelity step-down of Dolby Vision rebuilt for output as HDR10 or SDR (CDVConvertStep,
// xbmc/windowing/gbm/DVConvertStep.h of the tree under test, with CDVStepDown, the rule of native output), fed as
// CWinSystemGbmGLESContext feeds it: each refresh a commit, its flip time (one period after the one before, or later
// where it missed its refresh), and for each new picture its level, its GPU time and the commit that carries it.
//
//   - graphics that keep up (5 ms of a 41.7 ms refresh, and 30 ms with its refreshes kept) never leave level 0;
//   - the GPU times alone (5 % of the pictures over 80 % of the period, refreshes kept) or the missed refreshes alone
//     (late flips, every picture well under the mark) change nothing: both have to say so;
//   - graphics that do not keep up (both) go down, one level of the picture's ladder at a time, and only to levels
//     of it (a ladder without levels 1 and 2 goes from 0 to 3); a level that keeps up is kept;
//   - a flip late by more than one and a half periods is a missed refresh, one a period after the one before is not,
//     and a flip that is not known tells nothing of the next one;
//   - a picture drawn again (no new picture in the commit) is not counted;
//   - "Never" keeps level 0 whatever is measured; a level set is drawn as the picture's ladder takes it;
//   - a restart (a new video, a seek) leaves the first two seconds out.
// Prints "ok:"/"FAIL:" lines and "CONVSTEP-TEST PASSED", or "CONVSTEP-TEST FAILED: <n> failed".
#include "DVConvertStep.h"

#include <cstdio>
#include <string>

using KODI::WINDOWING::GBM::CDVConvertStep;
using KODI::WINDOWING::GBM::CDVStepDown;

static int failures = 0;
static void check(bool ok, const std::string& what)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  failures += !ok;
}

constexpr double PERIOD = 1000.0 / 23.976; // 41.7 ms
constexpr uint64_t FORMAT = (UINT64_C(1) << 63) | (UINT64_C(1) << 62) | 64; // a FEL picture, all of it active
constexpr unsigned int FEL = 0xf; // the ladder of a FEL picture: every level changes something

struct Run
{
  int lowest = 0; //!< the lowest level drawn (the highest number)
  int last = 0;
  int changes = 0;
  bool offLadder = false;
  int lateSeen = 0;
};

// n refreshes, each with a new picture: its GPU time by gpu(i, level), its flip late (two periods after the one
// before) where late(i, level); flips known unless unknown(i)
template<typename Gpu, typename Late, typename Unknown>
static Run Play(CDVConvertStep& s, int n, unsigned int ladder, Gpu gpu, Late late, Unknown unknown, int64_t& clockNs)
{
  Run r;
  for (int i = 0; i < n; ++i)
  {
    CDVStepDown::Step step;
    const int level = s.Level(FORMAT, ladder, &step);
    r.changes += step.changed ? 1 : 0;
    r.offLadder = r.offLadder || !CDVStepDown::In(CDVStepDown::Ladder(ladder), level);
    r.lowest = std::max(r.lowest, level);
    r.last = level;
    CDVConvertStep::Frame f;
    f.valid = true;
    f.format = FORMAT;
    f.level = level;
    f.ladder = ladder;
    // (the GPU time comes back before the flip, as the timers are read at the next picture)
    const CDVStepDown::Step decided = s.Timed(f, gpu(i, level), PERIOD);
    r.changes += decided.changed ? 1 : 0;
    s.Committed(f);
    clockNs += static_cast<int64_t>((late(i, level) ? 2.0 : 1.0) * PERIOD * 1e6);
    r.lateSeen += s.Flipped(unknown(i) ? 0 : clockNs, PERIOD) ? 1 : 0;
  }
  return r;
}

int main()
{
  int64_t clock = 1000000000;
  const auto never = [](int, int) { return false; };
  const auto known = [](int) { return false; };

  // --- graphics that keep up
  {
    CDVConvertStep s;
    s.Configure(-1, true);
    s.Restart();
    const Run a = Play(s, 3000, FEL, [](int, int) { return 5.0; }, never, known, clock);
    check(a.lowest == 0 && a.changes == 0, "5 ms of a 41.7 ms refresh: level 0 for every picture (the faithful one)");
    const Run b = Play(s, 3000, FEL, [](int, int) { return 30.0; }, never, known, clock);
    check(b.lowest == 0 && b.changes == 0, "30 ms, every refresh kept: level 0");
  }
  // --- one of the two alone changes nothing
  {
    CDVConvertStep s;
    s.Configure(-1, true);
    s.Restart();
    const Run a = Play(s, 3000, FEL, [](int i, int) { return i % 10 == 0 ? 36.0 : 30.0; }, never, known, clock);
    check(a.lowest == 0, "a tenth of the pictures over 80 % of the period, every refresh kept: level 0");
    const Run b = Play(s, 3000, FEL, [](int, int) { return 20.0; }, [](int i, int) { return i % 10 == 0; }, known,
                       clock);
    check(b.lowest == 0 && b.lateSeen > 0, "a tenth of the refreshes missed, every picture well under the mark: level 0");
  }
  // --- both: down, a level of the ladder at a time, and kept where it keeps up
  {
    CDVConvertStep s;
    s.Configure(-1, true);
    s.Restart();
    // level 0 too slow (36 ms, over the mark, and a tenth of its pictures miss their refresh); level 1 keeps up (25
    // ms), and level 0 would not fit again (36 ms by what level 1 takes: more than 70 % of the period)
    const auto gpu = [](int, int level) { return level == 0 ? 36.0 : 25.0; };
    const auto late = [](int i, int level) { return level == 0 && i % 10 == 0; };
    const Run a = Play(s, 3000, FEL, gpu, late, known, clock);
    check(a.lowest == 1 && a.last == 1 && !a.offLadder,
          "the GPU time and the missed refreshes both say so: down to level 1, and kept there (it keeps up)");
    // a ladder without levels 1 and 2 (no residual drawn): from 0 straight to 3
    CDVConvertStep t;
    t.Configure(-1, true);
    t.Restart();
    const Run b = Play(t, 3000, 0x9, gpu, late, known, clock);
    check(b.lowest == 3 && !b.offLadder, "a ladder of levels 0 and 3: down to 3, never to a level not in it");
    // nothing keeps up: down to the last level of the ladder at most
    CDVConvertStep u;
    u.Configure(-1, true);
    u.Restart();
    const Run c = Play(u, 6000, FEL, [](int i, int) { return i % 10 == 0 ? 36.0 : 30.0; },
                       [](int i, int) { return i % 10 == 0; }, known, clock);
    check(c.lowest == 3 && !c.offLadder, "no level keeps up: never past the last level of the ladder");
  }
  // --- the flips
  {
    CDVConvertStep s;
    s.Configure(-1, true);
    CDVConvertStep::Frame f;
    f.valid = true;
    f.format = FORMAT;
    f.ladder = FEL;
    int64_t t = 5000000000;
    s.Committed(f);
    bool first = s.Flipped(t, PERIOD); // (no flip before it: not known)
    s.Committed(f);
    t += static_cast<int64_t>(PERIOD * 1e6);
    const bool onTime = s.Flipped(t, PERIOD);
    s.Committed(f);
    t += static_cast<int64_t>(1.4 * PERIOD * 1e6);
    const bool slightly = s.Flipped(t, PERIOD);
    s.Committed(f);
    t += static_cast<int64_t>(1.6 * PERIOD * 1e6);
    const bool missed = s.Flipped(t, PERIOD);
    check(!first && !onTime && !slightly && missed,
          "a flip a period after the one before is on time, 1.4 periods too (jitter), 1.6 periods missed a refresh");
    s.Committed(f);
    s.Flipped(0, PERIOD); // not known
    s.Committed(f);
    t += static_cast<int64_t>(3 * PERIOD * 1e6);
    const bool afterUnknown = s.Flipped(t, PERIOD);
    check(!afterUnknown, "a flip not known tells nothing of the next one");
    s.Committed(CDVConvertStep::Frame());
    t += static_cast<int64_t>(3 * PERIOD * 1e6);
    const bool repeated = s.Flipped(t, PERIOD);
    check(!repeated, "a commit without a new picture (a picture drawn again) is not counted");
    check(s.WantsFlips(), "the flips are wanted while the chain goes on");
    s.Restart();
    check(!s.WantsFlips(), "... and not after a restart (the end of the conversion)");
  }
  // --- the setting
  {
    CDVConvertStep s;
    s.Configure(-1, false);
    s.Restart();
    const Run a = Play(s, 3000, FEL, [](int, int) { return 40.0; }, [](int, int) { return true; }, known, clock);
    check(a.lowest == 0 && !s.Measuring(), "Never: level 0 whatever is measured");
    CDVConvertStep f;
    f.Configure(2, true);
    check(f.Level(FORMAT, FEL) == 2 && f.Level(FORMAT, 0x9) == 0 && !f.Measuring(),
          "a level set: drawn at it, or at the level of the ladder above it that draws the same picture");
    CDVConvertStep g;
    g.Configure(7, true);
    check(g.Forced() == CDVStepDown::LEVELS - 1, "a level set past the last: the last");
  }
  // --- a restart leaves the first two seconds out
  {
    CDVConvertStep s;
    s.Configure(-1, true);
    s.Restart();
    const auto gpu = [](int, int level) { return level == 0 ? 40.0 : 20.0; };
    const auto late = [](int, int level) { return level == 0; };
    // 2 s (48 pictures) settling, then a window of 48 in which the GPU cannot keep up in any case (every picture
    // over the mark): no change before 48 + 48 pictures
    const Run a = Play(s, 48 + 47, FEL, gpu, late, known, clock);
    check(a.lowest == 0, "the first two seconds and the first window: still level 0");
    const Run b = Play(s, 200, FEL, gpu, late, known, clock);
    check(b.lowest >= 1, "... then down");
  }
  if (failures == 0)
    std::printf("CONVSTEP-TEST PASSED\n");
  else
    std::printf("CONVSTEP-TEST FAILED: %d failed\n", failures);
  return failures ? 1 : 0;
}
