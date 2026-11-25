/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades: host test of the chroma of the 4:2:0 pictures that Kodi's GLES renderer draws itself
 * (xbmc/cores/VideoPlayer/VideoRenderers/ChromaStepDown.h):
 *  1. the siting of each chroma location of FFmpeg (ShiftOf(): what the shaders move the samples by),
 *     an unspecified one taken as left, as libplacebo takes it for the bridge;
 *  2. the step-down: a video starts at Lanczos 3; SKIP timed pictures are left out; a window of WINDOW
 *     pictures is behind when 5 % of them, 3 at least, take more than 80 % of the refresh period (not
 *     when they take exactly that); two windows behind in a row, or one whose pictures take more than the
 *     period on average without the slowest, take the filter one level down (Spline16, then bilinear
 *     where the video places the samples), and never further, nor back up; a window of another refresh
 *     period starts again; a forced level never changes and is not timed; a new video starts again;
 *  3. Hades: on a GPU without a usable timer (no GL_EXT_disjoint_timer_query, or timestamps of no bits), the
 *     pictures are measured by the refreshes that they miss (Presented()): the first SKIP presentations are left out,
 *     a window of WINDOW presentations is behind when 5 % of them, 3 at least, missed their refresh, two windows
 *     behind in a row take the filter a level down and one alone never does; a GPU measured by its GPU time takes no
 *     missed refresh (its pictures stay the same bytes), and one measured neither way never changes;
 *  4. the flips of the commits that carry those pictures (CFlipChain): a flip more than 1.5 refresh periods after the
 *     one before missed its refresh, the tag of each picture comes back with it, the chain ends where the pictures
 *     end, and a flip that is not known breaks it.
 * Built with -I <Kodi tree>/xbmc/cores/VideoPlayer/VideoRenderers; each failure prints FAIL; the last
 * line says CHROMA-STEP PASSED or FAILED.
 */
#include "ChromaStepDown.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace KODI::CHROMA;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

const char* N(Filter f)
{
  return Name(f);
}

// feeds n pictures of ms each at that period; true when the level changed with one of them
bool Feed(CStepDown& s, unsigned int n, double ms, double period)
{
  bool changed = false;
  for (unsigned int i = 0; i < n; ++i)
    changed = s.Timed(ms, period) || changed;
  return changed;
}

// a window of WINDOW pictures of which `over` take overMs and the rest fastMs
bool Window(CStepDown& s, unsigned int over, double overMs, double fastMs, double period)
{
  bool changed = false;
  for (unsigned int i = 0; i < CStepDown::WINDOW; ++i)
    changed = s.Timed(i < over ? overMs : fastMs, period) || changed;
  return changed;
}

void TestShift()
{
  struct Case
  {
    int location;
    float x, y;
    const char* name;
  } cases[] = {
      {0, 0.25f, 0.0f, "unspecified (as left)"}, {1, 0.25f, 0.0f, "left"},
      {2, 0.0f, 0.0f, "centre"},                 {3, 0.25f, 0.25f, "top-left"},
      {4, 0.0f, 0.25f, "top"},                   {5, 0.25f, -0.25f, "bottom-left"},
      {6, 0.0f, -0.25f, "bottom"},               {9, 0.25f, 0.0f, "a value FFmpeg does not have (as left)"},
  };
  for (const auto& c : cases)
  {
    const Shift s = ShiftOf(c.location);
    Check(s.x == c.x && s.y == c.y,
          std::string("shift of ") + c.name + ": " + std::to_string(s.x) + ", " + std::to_string(s.y));
  }
  Check(std::string(LocationName(0)).find("left") != std::string::npos,
        "the log names an unspecified location as taken as left");
  Check(std::string(LocationName(3)) == "top-left", "the log names top-left");
  for (int f = 0; f < FILTERS; ++f)
    Check(N(static_cast<Filter>(f))[0] != '?', "every filter has a name");
}

void TestStart()
{
  CStepDown s;
  s.Start();
  Check(s.Current() == Filter::LANCZOS3 && !s.Forced() && s.Timing(), "a video starts at Lanczos 3, timed");
  s.Start(2);
  Check(s.Current() == Filter::SITED && s.Forced() && !s.Timing(), "a forced level is kept and not timed");
  Check(!Feed(s, 400, 100.0, 16.7), "a forced level does not change");
  s.Start(0);
  Check(s.Current() == Filter::LANCZOS3 && s.Forced() && !s.Timing(), "Lanczos 3 forced is not timed");
  Check(!Feed(s, 400, 100.0, 16.7) && s.Current() == Filter::LANCZOS3, "Lanczos 3 forced does not change");
  s.Start(3);
  Check(s.Current() == Filter::KODI && s.Forced(), "Kodi's own way can be forced");
  s.Start(7);
  Check(s.Current() == Filter::LANCZOS3 && !s.Forced(), "a level that does not exist forces nothing");
  s.Start(-1);
  Check(s.Current() == Filter::LANCZOS3 && !s.Forced(), "-1 forces nothing");
}

void TestSkipAndWindows()
{
  const double p = 16.683;
  CStepDown s;
  s.Start();
  // the first SKIP pictures, far too slow, are left out
  Check(!Feed(s, CStepDown::SKIP, 50.0, p), "the first pictures are left out");
  Check(s.Windows() == 0, "no window during the pictures left out");
  // then windows that keep up: nothing changes
  Check(!Feed(s, 4 * CStepDown::WINDOW, 6.0, p), "pictures that keep up change nothing");
  Check(s.Windows() == 4 && s.Current() == Filter::LANCZOS3, "four windows, still Lanczos 3");
  Check(!s.Last().behind && s.Last().over == 0, "a window that keeps up is not behind");
  // one window behind alone: nothing yet
  Check(!Window(s, 3, 0.81 * p + 0.01, 6.0, p), "one window behind alone changes nothing");
  Check(s.Last().behind && s.Last().over == 3, "three pictures over the mark: behind");
  // a window that keeps up between two behind: nothing
  Check(!Window(s, 0, 0, 6.0, p), "a window that keeps up");
  Check(!Window(s, 3, 0.9 * p, 6.0, p), "behind again, but not twice in a row");
  // the second in a row: Spline16
  Check(Window(s, 5, 0.9 * p, 6.0, p), "two windows behind in a row: a level down");
  Check(s.Current() == Filter::SPLINE16, std::string("Lanczos 3 -> Spline16, now ") + N(s.Current()));
  Check(s.Last().frames == CStepDown::WINDOW && s.Last().over == 5, "the window that decided it");
}

void TestMark()
{
  const double p = 20.0;
  CStepDown s;
  s.Start();
  Feed(s, CStepDown::SKIP, 1.0, p);
  // exactly 80 % is not over the mark
  Check(!Window(s, CStepDown::WINDOW, 0.8 * p, 0.8 * p, p) && s.Last().over == 0,
        "pictures at exactly 80 % of the period are not over it");
  // two over the mark, below MIN_OVER: not behind, twice
  Check(!Window(s, 2, 0.95 * p, 1.0, p) && !s.Last().behind, "two pictures over the mark: not behind");
  Check(!Window(s, 2, 0.95 * p, 1.0, p) && s.Current() == Filter::LANCZOS3,
        "two windows of two pictures over the mark: nothing");
  // an average over the period (without the slowest picture): down at once
  Check(Window(s, 1, 200.0, 1.2 * p, p), "a window whose pictures take more than the period: down at once");
  Check(s.Current() == Filter::SPLINE16, "... to Spline16");
  // a slowest picture alone does not make the average
  CStepDown t;
  t.Start();
  Feed(t, CStepDown::SKIP, 1.0, p);
  Check(!Window(t, 1, 5000.0, 1.0, p), "one very slow picture alone changes nothing");
  Check(std::fabs(t.Last().averageMs - 1.0) < 1e-9, "the average leaves out the slowest picture");
}

void TestLadder()
{
  const double p = 16.683;
  CStepDown s;
  s.Start();
  Feed(s, CStepDown::SKIP, 1.0, p);
  Window(s, 10, 0.9 * p, 1.0, p);
  Window(s, 10, 0.9 * p, 1.0, p);
  Check(s.Current() == Filter::SPLINE16, "the first step: Spline16");
  // the pictures right after a change are left out again
  Check(!Feed(s, CStepDown::SKIP, 100.0, p), "the pictures after a change are left out");
  Window(s, 10, 0.9 * p, 1.0, p);
  Check(Window(s, 10, 0.9 * p, 1.0, p), "Spline16 behind twice: a level down");
  Check(s.Current() == Filter::SITED, std::string("Spline16 -> bilinear where the video places it, now ") +
                                          N(s.Current()));
  Check(!s.Timing(), "bilinear where the video places the samples is the last level: not timed");
  Check(!Feed(s, 1000, 100.0, p) && s.Current() == Filter::SITED, "and it stays there");
  // a new video starts again from Lanczos 3
  s.Start();
  Check(s.Current() == Filter::LANCZOS3 && s.Windows() == 0, "a new video starts from Lanczos 3");
}

void TestPeriodChange()
{
  CStepDown s;
  s.Start();
  Feed(s, CStepDown::SKIP, 1.0, 16.683);
  // half a window behind at 60 Hz, the rest at 24 Hz: one window of 24 Hz pictures, keeping up
  Feed(s, CStepDown::WINDOW / 2, 15.0, 16.683);
  Check(!Feed(s, CStepDown::WINDOW, 15.0, 41.708) && !s.Last().behind && s.Last().periodMs == 41.708,
        "a change of refresh rate starts the window again");
  Check(s.Windows() == 1, "one window, of the new rate");
  Check(!s.Timed(-1.0, 16.7) && !s.Timed(5.0, 0.0), "a time or a period that is no number is ignored");
}
// a window of WINDOW presentations of which `late` missed their refresh
bool Presentations(CStepDown& s, unsigned int late, double period)
{
  bool changed = false;
  for (unsigned int i = 0; i < CStepDown::WINDOW; ++i)
    changed = s.Presented(i < late, period) || changed;
  return changed;
}

void TestMissedRefreshes()
{
  const double p = 16.683;
  using Measure = CStepDown::Measure;
  CStepDown s;
  s.Start(-1, Measure::MISSED_REFRESHES);
  Check(s.HowMeasured() == Measure::MISSED_REFRESHES && s.Measuring() && s.CountingRefreshes() && !s.Timing(),
        "without a timer: measured by the refreshes, not timed");
  Check(!Feed(s, 1000, 100.0, p) && s.Current() == Filter::LANCZOS3 && s.Windows() == 0,
        "without a timer a GPU time is never taken");
  // the first SKIP presentations, all late, are left out
  bool changed = false;
  for (unsigned int i = 0; i < CStepDown::SKIP; ++i)
    changed = s.Presented(true, p) || changed;
  Check(!changed && s.Windows() == 0, "the first presentations are left out");
  Check(!Presentations(s, 0, p) && s.Windows() == 1 && !s.Last().behind, "a window without a missed refresh");
  Check(!Presentations(s, 2, p) && !s.Last().behind && s.Last().lost == 2, "two missed refreshes: not behind");
  Check(!Presentations(s, 3, p) && s.Last().behind && s.Last().lost == 3, "three: behind, but one window alone");
  Check(!Presentations(s, 0, p), "a window that keeps up between");
  Check(!Presentations(s, CStepDown::WINDOW, p) && s.Current() == Filter::LANCZOS3,
        "every refresh missed in one window: still one window alone (the cause is not known without the GPU time)");
  Check(Presentations(s, 10, p) && s.Current() == Filter::SPLINE16,
        std::string("two windows behind in a row: Lanczos 3 -> Spline16, now ") + N(s.Current()));
  Check(s.Last().frames == CStepDown::WINDOW && s.Last().lost == 10 && s.Last().periodMs == p,
        "the window that decided it");
  // the presentations right after the change are left out, then the next level the same way
  changed = false;
  for (unsigned int i = 0; i < CStepDown::SKIP; ++i)
    changed = s.Presented(true, p) || changed;
  Check(!changed, "the presentations after a change are left out");
  Presentations(s, 5, p);
  Check(Presentations(s, 5, p) && s.Current() == Filter::SITED, "Spline16 behind twice: bilinear where sited");
  Check(!s.Measuring() && !s.CountingRefreshes(), "the last level: not measured");
  for (unsigned int i = 0; i < 1000; ++i)
    s.Presented(true, p);
  Check(s.Current() == Filter::SITED, "and it stays there");
  // another refresh rate starts the window again
  CStepDown r;
  r.Start(-1, Measure::MISSED_REFRESHES);
  for (unsigned int i = 0; i < CStepDown::SKIP; ++i)
    r.Presented(false, p);
  for (unsigned int i = 0; i < CStepDown::WINDOW / 2; ++i)
    r.Presented(true, p);
  Check(!Presentations(r, 0, 41.708) && r.Windows() == 1 && !r.Last().behind,
        "a change of refresh rate starts the window again");
  Check(!r.Presented(true, 0.0), "a period that is no number is ignored");
  // a GPU with a timer: missed refreshes change nothing (the same levels and bytes as before)
  CStepDown g;
  g.Start();
  Check(g.HowMeasured() == Measure::GPU_TIME && g.Timing() && !g.CountingRefreshes(), "by default: the GPU time");
  for (unsigned int i = 0; i < 1000; ++i)
    Check(!g.Presented(true, p), "a GPU with a timer takes no missed refresh");
  Check(g.Current() == Filter::LANCZOS3 && g.Windows() == 0, "... and stays at Lanczos 3");
  // neither way: never changes, and is not said to be measured
  CStepDown n;
  n.Start(-1, Measure::NONE);
  Check(!n.Measuring() && !n.Timing() && !n.CountingRefreshes(), "measured neither way: not measuring");
  Check(!Feed(n, 1000, 100.0, p) && n.Current() == Filter::LANCZOS3, "... no GPU time taken");
  for (unsigned int i = 0; i < 1000; ++i)
    n.Presented(true, p);
  Check(n.Current() == Filter::LANCZOS3, "... no refresh taken");
  // a forced level is neither
  CStepDown f;
  f.Start(1, Measure::MISSED_REFRESHES);
  for (unsigned int i = 0; i < 1000; ++i)
    f.Presented(true, p);
  Check(f.Current() == Filter::SPLINE16 && f.Forced() && !f.Measuring(), "a forced level does not change");
}

void TestFlipChain()
{
  const double p = 16.683;
  const int64_t ns = 16683000; // a refresh
  CFlipChain c;
  Check(!c.WantsFlips(false), "no picture, nothing before: no flip wanted");
  Check(c.WantsFlips(true), "a picture in this pass: the flip before is wanted, to time it from");
  // pass 1 carries a picture (tag 0); its flip is read in pass 2
  CFlipChain::Outcome o = c.Flipped(1000 * ns, p, true); // the flip of the commit before pass 1 (none carried)
  Check(o.tag < 0, "the flip of a commit without a picture tells nothing of one");
  c.Committed(0);
  o = c.Flipped(1001 * ns, p, true); // pass 2: the flip of pass 1, one refresh later
  Check(o.tag == 0 && !o.late, "one refresh after the one before: on time, with its tag");
  c.Committed(0);
  o = c.Flipped(1003 * ns, p, true); // two refreshes later: missed one
  Check(o.tag == 0 && o.late, "two refreshes after the one before: a missed refresh");
  c.Committed(1);
  o = c.Flipped(1003 * ns + 14 * ns / 10, p, true); // 1.4 refreshes: not late
  Check(o.tag == 1 && !o.late, "1.4 refreshes after the one before: on time (the mark is 1.5, as CDVConvertStep's)");
  c.Committed(1);
  o = c.Flipped(1003 * ns + 14 * ns / 10 + 16 * ns / 10, p, true); // 1.6
  Check(o.tag == 1 && o.late, "1.6 refreshes after: late");
  // a flip that is not known breaks the chain: the next picture is not timed against an old flip
  c.Committed(1);
  o = c.Flipped(0, p, true);
  Check(o.tag < 0, "a flip not known tells nothing");
  c.Committed(1);
  o = c.Flipped(2000 * ns, p, true);
  Check(o.tag < 0, "... and the picture after it is not timed from the flip before the gap");
  c.Committed(1);
  o = c.Flipped(2001 * ns, p, false); // the last picture; this pass has none
  Check(o.tag == 1 && !o.late, "the last picture is timed");
  c.Committed(-1);
  Check(!c.WantsFlips(false), "pictures ended: no flip wanted any more");
  // pictures again after a pause: the first one is timed from a flip of its own run, not from before the pause
  o = c.Flipped(5000 * ns, p, true);
  Check(o.tag < 0, "the commit before the new run carried nothing");
  c.Committed(0);
  o = c.Flipped(5001 * ns, p, true);
  Check(o.tag == 0 && !o.late, "the first picture of a new run, on time, not late from the pause");
}
} // namespace

int main()
{
  TestShift();
  TestStart();
  TestSkipAndWindows();
  TestMark();
  TestLadder();
  TestPeriodChange();
  TestMissedRefreshes();
  TestFlipChain();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  std::printf("%s\n", g_failures ? "CHROMA-STEP FAILED" : "CHROMA-STEP PASSED");
  return g_failures ? 1 : 0;
}
