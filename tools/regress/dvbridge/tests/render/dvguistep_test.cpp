// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: CDVGuiStep (xbmc/windowing/gbm/DVGuiStep.h), the fidelity step-down
// level of frames with GUI elements over the picture, fed with frames as the window system feeds it: GPU times and
// presentations of frames with GUI elements of a profile 7 picture with its full enhancement layer, at the level they
// are drawn at. Both ways: the base (the level of the frames without GUI elements) wherever those frames keep up, and
// for any format, base or period nothing is known of; down the ladder below the base where they do not keep up (the
// two signals, two windows unless the GPU cannot keep up at all, nothing while frame ahead can still be had, nothing
// in the seconds after a seek or after GUI elements come), never to a level that is measured to save nothing, never
// above the base; back up where the level above would fit with room to spare, after its wait, with no flapping.
// usage: dvguistep_test (build: g++ -std=c++17 -I<tree>/xbmc/windowing/gbm dvguistep_test.cpp)
#include "DVGuiStep.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

using KODI::WINDOWING::GBM::CDVGuiStep;
using KODI::WINDOWING::GBM::CDVStepDown;
using Pace = CDVGuiStep::Pace;
using Why = CDVGuiStep::Why;

static int g_failures, g_checks;

static void Check(bool ok, const char* what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

static const uint64_t FEL = 0x8000000000000789ull, OTHER = 0x8000000000000abcull;
static const double FILM = 1000.0 / 23.976, SIXTY = 1000.0 / 60.0;
// the ladders of the renderer (dvbridge_render_step_levels()): the source transport with the residual drawn, the
// converted one, a picture without the residual in the source transport (profile 5, 8: nothing to step), and the
// converted one without it with the composer of CCM 001
static const unsigned int SOURCE_FEL = 0x7u, CONVERTED_FEL = 0xfu, SOURCE_PLAIN = 0x1u, CONVERTED_PLAIN = 0x9u;
using Times = std::array<double, 4>;
using Lates = std::array<unsigned int, 4>;
// an integrated Vega 8 (Ryzen 5 3500U) under the OSD, profile 7 FEL at 23.976 Hz: a guess of its shape, not a
// measurement (none exists): beyond the period at level 0, behind at 1, keeping up at 2; a third of the presentations
// late where it does not keep up
static const Times VEGA = {50.0, 44.0, 30.0, 30.0};
static const Lates VEGA_LATE = {3, 3, 0, 0};
// a GPU on which the levels save nothing (NVIDIA: the reconstruction is not where the time goes), and frames that
// are behind all the same (another cause)
static const Times FLAT = {44.0, 44.0, 44.0, 44.0};
static const Lates FLAT_LATE = {3, 3, 3, 3};
// a GPU that keeps up (the RTX 4080 under the player information: a few ms)
static const Times FAST = {5.0, 4.8, 4.5, 4.5};
static const Lates NONE = {0, 0, 0, 0};

struct Run
{
  unsigned int downs = 0, ups = 0, firstDown = 0, secondDown = 0, firstUp = 0;
  int level = 0, lowest = 99, highest = -1;
  Why lastWhy = Why::NONE;
  bool nothing = false;
};

// n frames with GUI elements of a format, drawn at the level the decision asks, each taking the time of that level
// and late when its lateEvery says (every n-th; 0: none)
static Run Feed(CDVGuiStep& d, uint64_t format, unsigned int n, const Times& ms, const Lates& late, Pace pace,
                int base = 0, unsigned int ladder = SOURCE_FEL, double period = FILM)
{
  Run run;
  for (unsigned int i = 1; i <= n; ++i)
  {
    const int level = d.Level(format, base, ladder, period);
    run.lowest = std::min(run.lowest, level);
    run.highest = std::max(run.highest, level);
    Check(CDVStepDown::In(CDVStepDown::Ladder(ladder), level), "a level of the ladder");
    const unsigned int every = late[static_cast<size_t>(level)];
    d.Presented(format, base, ladder, level, every > 0 && i % every == 0, pace, period);
    const auto s = d.Measured(format, base, ladder, level, ms[static_cast<size_t>(level)], period, pace);
    if (s.why == Why::NOTHING_KEEPS_UP)
      run.nothing = true;
    if (!s.changed)
      continue;
    if (std::getenv("DVGUISTEP_TRACE"))
      std::printf("  frame %u: level %d -> %d (why %d, %.1f ms, est %.1f, %u/%u late)\n", i, s.from, s.to,
                  static_cast<int>(s.why), s.averageMs, s.estimateMs, s.lost, s.presented);
    run.lastWhy = s.why;
    Check(s.from == level, "a change from the level the frames were drawn at");
    Check(s.base == base, "the change says the base");
    if (s.to > s.from)
    {
      ++run.downs;
      if (!run.firstDown)
        run.firstDown = i;
      else if (!run.secondDown)
        run.secondDown = i;
    }
    else
    {
      ++run.ups;
      if (!run.firstUp)
        run.firstUp = i;
    }
  }
  run.level = d.Level(format, base, ladder, period);
  if (std::getenv("DVGUISTEP_TRACE"))
    std::printf("  run of %u: downs %u ups %u level %d lowest %d highest %d\n", n, run.downs, run.ups, run.level,
                run.lowest, run.highest);
  return run;
}

int main()
{
  const unsigned int W = CDVGuiStep::WINDOW;
  // (the first seconds of a video are left out: 2 s of film are 48 frames)
  const unsigned int settle = 49;

  // --- where nothing changes: the bytes of every frame are those of the level of the frames without GUI elements
  {
    CDVGuiStep d;
    Check(d.Level(FEL, 0, SOURCE_FEL, FILM) == 0 && d.Level(FEL, 1, SOURCE_FEL, FILM) == 1 &&
              d.Level(FEL, 2, CONVERTED_FEL, SIXTY) == 2,
          "nothing known: the base, whatever it is");
    Check(!CDVGuiStep::Applies(0, SOURCE_PLAIN) && !CDVGuiStep::Applies(2, SOURCE_FEL) &&
              !CDVGuiStep::Applies(3, CONVERTED_FEL),
          "no level below the base in the ladder: not one this decides for");
    Check(CDVGuiStep::Applies(0, SOURCE_FEL) && CDVGuiStep::Applies(1, SOURCE_FEL) &&
              CDVGuiStep::Applies(0, CONVERTED_PLAIN) && !CDVGuiStep::Applies(1, CONVERTED_PLAIN),
          "a level below the base in the ladder: one this decides for (1 is not in 0x9)");
    // a picture without its residual in the source transport: whatever its frames take, nothing
    const Run r = Feed(d, FEL, settle + 20 * W, VEGA, VEGA_LATE, Pace::AHEAD, 0, SOURCE_PLAIN);
    Check(r.downs == 0 && r.level == 0, "ladder 0x1 (profile 5 or 8 in the source transport): never below the base");
  }
  {
    // a GPU that keeps up under the GUI: the base, for every base, with frames ahead and the way that waits
    for (int base = 0; base <= 1; ++base)
    {
      CDVGuiStep d;
      const Run a = Feed(d, FEL, settle + 40 * W, FAST, NONE, Pace::AHEAD, base);
      Check(a.downs == 0 && a.ups == 0 && a.level == base && a.lowest == base && a.highest == base,
            "frames with GUI elements that keep up: always the base (a frame ahead)");
      CDVGuiStep e;
      const Run b = Feed(e, FEL, settle + 40 * W, FAST, NONE, Pace::WAITING, base);
      Check(b.downs == 0 && b.level == base, "... and the way that waits");
      // ... nor with now and then a late presentation (a hiccup that is not the GPU's)
      CDVGuiStep f;
      Check(Feed(f, FEL, settle + 40 * W, FAST, {4, 4, 4, 4}, Pace::AHEAD, base).downs == 0,
            "late presentations alone (the GPU time fits): the base");
    }
  }
  {
    // GPU times over the mark alone, no refresh missed: the base
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 20 * W, {38.0, 30.0, 20.0, 20.0}, NONE, Pace::AHEAD);
    Check(r.downs == 0 && r.level == 0, "over the mark but no refresh missed: the base");
  }

  // --- down where the frames with GUI elements do not keep up
  {
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 2 && r.level == 2 && r.ups == 0, "the Vega under the OSD: down to level 2, where it keeps up");
    Check(r.firstDown > 0 && r.firstDown <= settle + W + 1, "beyond the period: one window decides");
    Check(r.secondDown > r.firstDown + W, "level 2 only after a window measured at level 1");
    Check(d.Level(OTHER, 0, SOURCE_FEL, FILM) == 0 && d.Level(FEL, 0, SOURCE_FEL, SIXTY) == 0 &&
              d.Level(FEL, 1, SOURCE_FEL, FILM) == 1 && d.Level(FEL, 0, CONVERTED_FEL, FILM) == 0,
          "and only for that format, base, ladder and refresh period");
    // and stays: no flapping in twenty minutes of film
    const Run s = Feed(d, FEL, 600 * W, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(s.downs == 0 && s.ups == 0 && s.level == 2, "it keeps up at level 2: it stays there");
  }
  {
    // behind but not beyond (38 of 41.7 ms: over 85 %, late): two windows in a row
    CDVGuiStep d;
    const Times t = {38.0, 30.0, 20.0, 20.0};
    const Lates l = {3, 0, 0, 0};
    Run r = Feed(d, FEL, settle + W, t, l, Pace::AHEAD);
    Check(r.downs == 0, "one window behind (not beyond): not yet");
    r = Feed(d, FEL, W, t, l, Pace::AHEAD);
    Check(r.downs == 1 && r.level == 1, "two windows behind in a row: a level down");
    // the way that waits: frames over 80 % of the period, as many refreshes missed
    CDVGuiStep e;
    const Run w = Feed(e, FEL, settle + 4 * W, {36.0, 20.0, 20.0, 20.0}, {3, 0, 0, 0}, Pace::WAITING);
    Check(w.downs == 1 && w.level == 1, "the way that waits: down as well");
  }
  {
    // the base is the level of the frames without GUI elements: never above it, down from it
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD, 1);
    Check(r.lowest == 1 && r.level == 2 && r.downs == 1, "base 1: down to 2, never level 0");
  }
  {
    // nothing goes down while frame ahead can still be had (it comes first)
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 20 * W, VEGA, VEGA_LATE, Pace::PENDING);
    Check(r.downs == 0 && r.level == 0, "pace pending: nothing goes down");
  }
  {
    // the first seconds of a video are left out
    CDVGuiStep d;
    Run r = Feed(d, FEL, 48, VEGA, VEGA_LATE, Pace::AHEAD);
    r = Feed(d, FEL, W - 1, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 0, "the start of a video is left out (a window of 47 frames after it is not one)");
    // after a seek, again
    CDVGuiStep e;
    Feed(e, FEL, settle + W - 10, VEGA, VEGA_LATE, Pace::AHEAD);
    e.Seeked();
    r = Feed(e, FEL, 40, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 0, "a seek: its window begins again, and the seconds after it are left out");
    // after GUI elements come (a frame without them first): the first second is left out, and the window begins
    // again
    CDVGuiStep f;
    Feed(f, FEL, settle + W - 10, VEGA, VEGA_LATE, Pace::AHEAD);
    f.Plain();
    // (a second of film at 23.976 Hz: 24 frames)
    r = Feed(f, FEL, 24 + W - 1, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 0, "GUI elements that come: the first second is left out, the window begins again");
    r = Feed(f, FEL, 1, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 1, "... and then it decides");
  }
  {
    // a frame drawn at another level than the format's is not taken
    CDVGuiStep d;
    unsigned int changes = 0;
    // (frames at its level that keep up, presented late now and then, and between them frames of another level that
    // would not)
    for (unsigned int i = 0; i < settle + 20 * W; ++i)
    {
      for (int k = 0; k < 2; ++k)
      {
        d.Presented(FEL, 0, SOURCE_FEL, 0, (2 * i + k) % 3 == 0, Pace::AHEAD, FILM);
        changes += d.Measured(FEL, 0, SOURCE_FEL, 0, 5.0, FILM, Pace::AHEAD).changed ? 1 : 0;
      }
      d.Presented(FEL, 0, SOURCE_FEL, 1, true, Pace::AHEAD, FILM);
      changes += d.Measured(FEL, 0, SOURCE_FEL, 1, 500.0, FILM, Pace::AHEAD).changed ? 1 : 0;
    }
    Check(changes == 0 && d.Level(FEL, 0, SOURCE_FEL, FILM) == 0, "frames drawn at another level: not taken");
  }

  // --- never a level that is measured to save nothing
  {
    // the levels save nothing, and the frames are behind all the same: level 1 is tried, measured to save nothing,
    // level 2 is tried (not measured yet), measured to save nothing: back to the base, and there it stays
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 30 * W, FLAT, FLAT_LATE, Pace::AHEAD);
    Check(r.level == 0, "levels that save nothing: back to the base");
    Check(r.lastWhy == Why::NO_GAIN, "... as the fastest measured, the most faithful of those that save nothing");
    const Run s = Feed(d, FEL, 200 * W, FLAT, FLAT_LATE, Pace::AHEAD);
    Check(s.downs == 0 && s.level == 0, "... and stays there at that pace");
    // another pace: decided again, by what the levels were measured to take for each other, which is no saving:
    // no level is drawn for nothing
    const Run t = Feed(d, FEL, 30 * W, FLAT, FLAT_LATE, Pace::WAITING);
    Check(t.downs == 0 && t.level == 0 && t.nothing,
          "another pace: the levels measured to save nothing are not drawn again, and it says so");
  }
  {
    // level 1 saves nothing, level 2 does: level 2
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 10 * W, {50.0, 50.0, 30.0, 30.0}, {3, 3, 0, 0}, Pace::AHEAD);
    Check(r.level == 2, "a level that saves nothing is passed over for one that does");
  }
  {
    // no level keeps up, the lowest is the fastest: it stays there, and says so once
    CDVGuiStep d;
    const Run r = Feed(d, FEL, settle + 30 * W, {60.0, 55.0, 48.0, 48.0}, {2, 2, 2, 2}, Pace::AHEAD);
    Check(r.level == 2 && r.nothing, "no level keeps up: the fastest measured (2), and it says so");
  }

  // --- back up
  {
    CDVGuiStep d;
    Run r = Feed(d, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD);
    Check(r.level == 2, "(down to 2 first)");
    // the GPU gets faster (it was held up): level 2 now 20 ms, so level 1 would take 20 x 44 / 30 = 29.3 ms, which
    // fits 75 % of the period (31.3 ms); not before a minute of film after it went down
    const Times faster = {33.3, 29.3, 20.0, 20.0};
    r = Feed(d, FEL, 20 * W, faster, NONE, Pace::AHEAD);
    Check(r.ups == 0, "not up before its wait (a minute of film)");
    r = Feed(d, FEL, 10 * W, faster, NONE, Pace::AHEAD);
    Check(r.ups == 1 && r.level == 1, "up one level once the level above would fit with room to spare");
    // at level 1 (29.3 ms) level 0 would take 29.3 x 50 / 44 = 33.3 ms: over 31.3, it stays
    r = Feed(d, FEL, 60 * W, faster, NONE, Pace::AHEAD);
    Check(r.ups == 0 && r.downs == 0 && r.level == 1, "level 0 would not fit with room to spare: it stays at 1");
  }
  {
    // up to a level that does not keep up after all: down again, and the wait doubles
    CDVGuiStep d;
    Feed(d, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD);
    // level 2 faster for a while (level 1 estimated to fit), but level 1 is beyond when it is drawn
    const Times tease = {50.0, 44.0, 20.0, 20.0};
    Run r = Feed(d, FEL, 25 * W, tease, VEGA_LATE, Pace::AHEAD);
    Check(r.ups == 1 && r.downs == 0, "(up to level 1 by the estimate)");
    r = Feed(d, FEL, 3 * W, tease, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 1 && r.level == 2, "level 1 does not keep up after all: down again");
    // that window measured level 1 at 44 ms for level 2's 20: with level 2 at 20 ms level 1 would not fit, and it
    // does not go up again (nothing on a guess)
    r = Feed(d, FEL, 100 * W, tease, VEGA_LATE, Pace::AHEAD);
    Check(r.ups == 0 && r.level == 2, "level 1 measured not to fit: it stays at 2");
    // the GPU gets faster again (level 2 at 9 ms, so level 1 would take 9 x 44 / 20 = 19.8 ms): up again, but only
    // after the doubled wait (two minutes of film since it went down: 100 windows have passed, so at once)
    CDVGuiStep e;
    Feed(e, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD);
    r = Feed(e, FEL, 25 * W, tease, VEGA_LATE, Pace::AHEAD);
    r = Feed(e, FEL, 3 * W, tease, VEGA_LATE, Pace::AHEAD);
    Check(r.downs == 1 && r.level == 2, "(down again)");
    const Times quick = {30.0, 19.8, 9.0, 9.0};
    r = Feed(e, FEL, 40 * W, quick, NONE, Pace::AHEAD);
    Check(r.ups == 0, "the wait doubled: not up again after one minute of film");
    r = Feed(e, FEL, 25 * W, quick, NONE, Pace::AHEAD);
    Check(r.ups >= 1 && r.downs == 0, "... but after two");
  }
  {
    // up never above the base
    CDVGuiStep d;
    Feed(d, FEL, settle + 8 * W, VEGA, VEGA_LATE, Pace::AHEAD, 1);
    const Run r = Feed(d, FEL, 200 * W, {5.0, 5.0, 4.0, 4.0}, NONE, Pace::AHEAD, 1);
    Check(r.lowest == 1 && r.level == 1 && r.ups == 1, "up to the base, never above it");
  }

  std::printf("%s: %d of %d checks\n", g_failures ? "FAILED" : "ALL PASSED", g_checks - g_failures, g_checks);
  return g_failures ? 1 : 0;
}
