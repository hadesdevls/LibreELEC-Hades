// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of CDVAhead (xbmc/windowing/gbm/DVAhead.h): whether the frames of native Dolby Vision output are
// presented a frame ahead, fed as the window system feeds it. Build: g++ -std=c++20 -Wall -Wextra
// -fsanitize=address,undefined -I <work repo>/xbmc/windowing/gbm dvahead_test.cpp
//
// Hades: also the frames presented the way that waits that miss their refresh again and again
// (CDVAhead::PresentedWaiting()), with the 10 s windows of real logs as data:
//   dvahead_test [<windows.tsv>...]   (rows of the per-window lines of kodi.log files, see run-kodi-host.sh)
// Built against a header without PresentedWaiting() the frames are told to nothing (Waited() below),
// so that the same test shows what the header before did with them.
#include "DVAhead.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using KODI::WINDOWING::GBM::CDVAhead;
using Mode = CDVAhead::Mode;
using Change = CDVAhead::Change;

// The first column of a row names the set of logs it comes from; run-kodi-host.sh gives it as the first 12
// hexadecimal digits of the SHA256 of the name the set had, so that nothing here names a report. These two are the
// sets of the logs of issue #4 that the replay checks by name: the Quadro RTX 4000's profile 8 film of 1.0, and its
// profile 7 film of 1.0 RC3.
static const char* const LOG_FILM = "d4be6d73ba3f";
static const char* const LOG_RC3 = "e2540df8251c";

// A frame presented the way that waits, as the window system tells it; NONE with a header that has
// no such call
template<typename A>
static Change Waited(A& ahead, bool late, double period)
{
  if constexpr (requires { ahead.PresentedWaiting(late, period); })
    return ahead.PresentedWaiting(late, period);
  else
    return Change::NONE;
}

// Hades: the GPU time of a frame without GUI elements (nothing with a header without TimedWithoutGui())
template<typename A>
static void Timed(A& ahead, double ms, double period)
{
  if constexpr (requires { ahead.TimedWithoutGui(ms, period); })
    ahead.TimedWithoutGui(ms, period);
}
template<typename A>
static bool SustainedSaid(const A& ahead)
{
  if constexpr (requires { ahead.SustainedRuns(); })
    return ahead.SustainedRuns();
  else
    return false;
}
template<typename A>
static bool Sustained(const A& ahead, unsigned int hits)
{
  if constexpr (requires { ahead.SustainedRuns(); })
    return ahead.SustainedRuns() && ahead.SustainedCalledHits() == hits;
  else
    return false;
}

// ... what it says of them (false and 0 with that header)
template<typename A>
static bool MissesCall(const A& ahead, double period)
{
  if constexpr (requires { ahead.MissesCallForIt(period); })
    return ahead.MissesCallForIt(period);
  else
    return false;
}
template<typename A>
static bool MissedWere(const A& ahead, unsigned int before, unsigned int last)
{
  if constexpr (requires { ahead.MissedLast(); })
    return ahead.MissedBefore() == before && ahead.MissedLast() == last;
  else
    return false;
}

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

// n frames of the same time: the changes they made, as a count of each
struct Fed
{
  unsigned int needed = 0;
  unsigned int notNeeded = 0;
};

static Fed Feed(CDVAhead& ahead, unsigned int frames, double ms, double period)
{
  Fed fed;
  for (unsigned int i = 0; i < frames; ++i)
  {
    const Change change = ahead.Measured(ms, period);
    fed.needed += change == Change::NEEDED;
    fed.notNeeded += change == Change::NOT_NEEDED;
  }
  return fed;
}

// A run of frames presented the way that waits, of which the ones at the positions in late missed
// their refresh: the changes they made
static Fed Runs(CDVAhead& ahead, unsigned int frames, std::vector<unsigned int> late, double period)
{
  Fed fed;
  std::sort(late.begin(), late.end());
  size_t next = 0;
  for (unsigned int i = 0; i < frames; ++i)
  {
    const bool isLate = next < late.size() && late[next] == i;
    if (isLate)
      ++next;
    const Change change = Waited(ahead, isLate, period);
    fed.needed += change == Change::NEEDED;
    fed.notNeeded += change == Change::NOT_NEEDED;
  }
  return fed;
}

// Hades: a run of frames presented the way that waits, the GPU time of each told first (ms, and overMs at the
// positions in over), of which the ones at the positions in late missed their refresh
static Fed TimedRuns(CDVAhead& ahead, unsigned int frames, std::vector<unsigned int> late, std::vector<unsigned int> over,
                     double ms, double overMs, double period)
{
  Fed fed;
  std::sort(late.begin(), late.end());
  std::sort(over.begin(), over.end());
  size_t next = 0, nextOver = 0;
  for (unsigned int i = 0; i < frames; ++i)
  {
    const bool isOver = nextOver < over.size() && over[nextOver] == i;
    if (isOver)
      ++nextOver;
    Timed(ahead, isOver ? overMs : ms, period);
    const bool isLate = next < late.size() && late[next] == i;
    if (isLate)
      ++next;
    const Change change = Waited(ahead, isLate, period);
    fed.needed += change == Change::NEEDED;
    fed.notNeeded += change == Change::NOT_NEEDED;
  }
  return fed;
}

// n late frames spread over a run of 240
static std::vector<unsigned int> Spread(unsigned int n)
{
  std::vector<unsigned int> at;
  for (unsigned int i = 0; i < n; ++i)
    at.push_back(i * 240 / n + 7);
  return at;
}

// ---- the windows of real logs, replayed frame by frame
struct Window
{
  std::string source, log, renderer;
  int output = 0;
  unsigned int shown = 0, notShown = 0, gui = 0, late = 0, ahead = 0, still = 0;
  double gpuAvg = 0.0, gpuMax = 0.0;
};

static std::vector<Window> ReadWindows(const char* path)
{
  std::vector<Window> windows;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line))
  {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t'))
      f.push_back(cell);
    // source log renderer output time kind shown kept notshown gui prepavg prepmax presavg presmax
    // gap late ahead flips flipgapmax flipslate gpuavg gpumax gpuframes still
    // (Hades: kodi.old.log.gz too)
    if (f.size() < 24 || f[5] != "last" ||
        (f[1].find("kodi.log.gz") == std::string::npos && f[1].find("kodi.old.log.gz") == std::string::npos))
      continue;
    Window w;
    w.source = f[0];
    w.log = f[1];
    w.renderer = f[2];
    w.output = std::atoi(f[3].c_str());
    w.shown = std::strtoul(f[6].c_str(), nullptr, 10);
    w.notShown = std::strtoul(f[8].c_str(), nullptr, 10);
    w.gui = std::strtoul(f[9].c_str(), nullptr, 10);
    w.late = std::strtoul(f[15].c_str(), nullptr, 10);
    w.ahead = std::strtoul(f[16].c_str(), nullptr, 10);
    w.still = std::strtoul(f[23].c_str(), nullptr, 10);
    w.gpuAvg = std::atof(f[20].c_str());
    w.gpuMax = std::atof(f[21].c_str());
    windows.push_back(w);
  }
  return windows;
}

// the refresh period of a window of 10 s, by the frames in it
static double PeriodOf(const Window& w)
{
  const double rates[] = {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0};
  const double counted = (w.shown + w.notShown) / 10.0;
  double best = rates[0];
  for (double r : rates)
    if (std::abs(r - counted) < std::abs(best - counted))
      best = r;
  return 1000.0 / best;
}

struct Replay
{
  bool engaged = false;
  unsigned int windows = 0; //!< windows replayed until it was
};

// One log: its windows of frames presented the way that waits, the late ones at random places in
// each (seeded). bound "low": only windows without GUI elements, whose late frames are frames
// without them; "high": every window, every late frame of it counted as one without them. The first
// window of an output (its modeset), windows with frames ahead, pauses, and windows of fewer than 100
// frames are left out.
static Replay ReplayLog(const std::vector<Window>& all, const std::string& source, const std::string& log,
                        bool high, uint32_t seed)
{
  CDVAhead ahead;
  Replay r;
  int output = -1;
  uint32_t state = seed * 2654435761u + 1u;
  auto rnd = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
  for (const auto& w : all)
  {
    if (w.source != source || w.log != log)
      continue;
    if (w.output != output)
    {
      if (output >= 0)
        ahead.Ended();
      output = w.output;
      continue; // (its first window)
    }
    if (w.ahead > 0 || w.still > 0 || w.shown < 100 || (!high && w.gui > 0))
      continue;
    ++r.windows;
    std::vector<unsigned int> late;
    while (late.size() < std::min(w.late, w.shown))
    {
      const unsigned int at = rnd() % w.shown;
      if (std::find(late.begin(), late.end(), at) == late.end())
        late.push_back(at);
    }
    const double period = PeriodOf(w);
    // the GPU time: the window's average, and its maximum on its late frames where that is over a
    // period (a window without a late frame and a maximum over a period: one frame of it); the high bound tells
    // the GPU time of the windows with GUI elements as that of frames without them as well
    std::vector<unsigned int> over;
    if (w.gpuMax > period)
    {
      over = late;
      if (over.empty())
        over.push_back(rnd() % w.shown);
    }
    TimedRuns(ahead, w.shown, late, over, w.gpuAvg, w.gpuMax, period);
    if (ahead.Wanted(Mode::NEEDED, period))
    {
      r.engaged = true;
      return r;
    }
  }
  return r;
}

static int ReplayFiles(int count, char** paths)
{
  std::vector<Window> all;
  for (int i = 0; i < count; ++i)
  {
    auto rows = ReadWindows(paths[i]);
    all.insert(all.end(), rows.begin(), rows.end());
  }
  std::printf("replay: %zu windows of 10 s read\n", all.size());
  // the logs, in order
  std::vector<std::pair<std::string, std::string>> logs;
  std::map<std::pair<std::string, std::string>, std::string> renderers;
  for (const auto& w : all)
  {
    const auto key = std::make_pair(w.source, w.log);
    if (!renderers.count(key))
    {
      logs.push_back(key);
      renderers[key] = w.renderer;
    }
  }
  constexpr uint32_t SEEDS = 50;
  bool any4080 = false;
  bool quadroSeen = false, quadroFilm = false, any5090 = false;
  for (const auto& key : logs)
  {
    const std::string& renderer = renderers[key];
    unsigned int on[2] = {0, 0};
    std::vector<unsigned int> when[2];
    for (int high = 0; high < 2; ++high)
      for (uint32_t seed = 0; seed < SEEDS; ++seed)
      {
        const Replay r = ReplayLog(all, key.first, key.second, high, seed);
        if (r.engaged)
        {
          ++on[high];
          when[high].push_back(r.windows);
        }
      }
    auto median = [](std::vector<unsigned int> v) -> int
    {
      if (v.empty())
        return -1;
      std::sort(v.begin(), v.end());
      return static_cast<int>(v[v.size() / 2]);
    };
    std::printf("replay: %s %s (%s): frames ahead called for in %u of %u replays without GUI "
                "windows (median window %d), %u of %u with all of them (median window %d)\n",
                key.first.c_str(), key.second.c_str(), renderer.c_str(), on[0], SEEDS,
                median(when[0]), on[1], SEEDS, median(when[1]));
    if (renderer.find("RTX 4080") != std::string::npos)
    {
      any4080 = true;
      Check(("the RTX 4080 SUPER never calls for it: " + key.first + " " + key.second).c_str(),
            on[0] == 0 && on[1] == 0);
    }
    if (key.first == LOG_FILM && key.second == "x/hades-diag/start-46/shutdown/kodi.log.gz")
    {
      quadroFilm = true;
      Check(("the Quadro RTX 4000 at 35 W with its profile 8 film of 1.0 (issue #4, start-46) calls for it in every "
             "replay without GUI windows (median window " + std::to_string(median(when[0])) + "), and in every one "
             "with all of them (median window " + std::to_string(median(when[1])) + ")").c_str(),
            on[0] == SEEDS && on[1] == SEEDS);
    }
    if (renderer.find("RTX 5090") != std::string::npos)
    {
      any5090 = true;
      Check(("the RTX 5090 never calls for it: " + key.first + " " + key.second).c_str(), on[0] == 0 && on[1] == 0);
    }
    if (key.first == LOG_RC3 && key.second == "hades-diag/start-4/shutdown/kodi.log.gz")
    {
      quadroSeen = true;
      Check("the Quadro RTX 4000 with its profile 7 film (issue #4, start-4) calls for it in "
            "every replay, both ways",
            on[0] == SEEDS && on[1] == SEEDS);
    }
  }
  Check("the replay had logs of the RTX 4080 SUPER and the one of issue #4", any4080 && quadroSeen);
  Check("... and the Quadro's film of 1.0 and a log of the RTX 5090", quadroFilm && any5090);
  return 0;
}

int main(int argc, char** argv)
{
  const double film = 1000.0 / 23.976; // 41.7 ms
  const double sixty = 1000.0 / 59.94; // 16.7 ms

  {
    CDVAhead ahead;
    Check("nothing measured: not wanted where it goes by need, wanted when asked for always, never when turned off",
          !ahead.Wanted(Mode::NEEDED, film) && ahead.Wanted(Mode::ALWAYS, film) &&
              !ahead.Wanted(Mode::NEVER, film));
    Check("no refresh period known: not wanted in any mode", !ahead.Wanted(Mode::ALWAYS, 0.0));
    // the integrated GPU: 30 ms composed, of 41.7
    Fed fed = Feed(ahead, 23, 30.0, film);
    Check("23 frames of 30 ms decide nothing yet", fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 1, 30.0, film);
    Check("the 24th does: frames of 30 ms of 41.7 call for it, and it is told once",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film) && ahead.Average() > 29.9 &&
              ahead.Average() < 30.1);
    fed = Feed(ahead, 24 * 60, 30.0, film);
    Check("a minute more of them changes nothing and tells nothing", fed.needed == 0 && fed.notNeeded == 0 &&
                                                                         ahead.Wanted(Mode::NEEDED, film));
    Check("... and never when it is turned off", !ahead.Wanted(Mode::NEVER, film));
    // frames between the two marks keep what there is
    fed = Feed(ahead, 24 * 10, 17.0, film); // 0.41 of the period
    Check("frames of 17 ms (between the two marks) keep it", fed.notNeeded == 0 && ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 24, 12.0, film); // 0.29
    Check("a second of frames of 12 ms ends it, told once", fed.notNeeded == 1 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 24 * 10, 17.0, film);
    Check("... and frames of 17 ms do not bring it back", fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 24, 21.0, film); // 0.504
    Check("frames of 21 ms, half the period, do", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // the discrete GPU: 8 ms without GUI elements, 18 with them, of 41.7; and 9 ms of the 16.7 of 60 Hz
    CDVAhead ahead;
    Fed fed = Feed(ahead, 24 * 60, 8.0, film);
    Fed gui = Feed(ahead, 24 * 60, 18.0, film);
    Check("frames of 8 ms and of 18 ms of 41.7 never call for it",
          fed.needed == 0 && gui.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 60, 9.0, sixty);
    Check("frames of 9 ms of the 16.7 ms of a 60 Hz refresh do", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, sixty));
    Check("... which says nothing of film: not wanted at 23.976 Hz", !ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 24 * 60, 8.0, film);
    Check("... and the frames of a film after it do not call for it, nor end it for 60 Hz",
          fed.needed == 0 && fed.notNeeded == 0 && !ahead.Wanted(Mode::NEEDED, film) &&
              ahead.Wanted(Mode::NEEDED, sixty));
  }
  {
    // a change of refresh rate in the middle of an average starts the average again
    CDVAhead ahead;
    Feed(ahead, 20, 30.0, film);
    Fed fed = Feed(ahead, 23, 4.0, sixty);
    Check("frames of two refresh rates are not averaged together",
          fed.needed == 0 && fed.notNeeded == 0 && !ahead.Wanted(Mode::NEEDED, film) &&
              !ahead.Wanted(Mode::NEEDED, sixty));
    fed = Feed(ahead, 1, 4.0, sixty);
    Check("... the 24th of the second rate ends an average of that rate alone (4 ms: not called for)",
          fed.needed == 0 && ahead.Average() > 3.9 && ahead.Average() < 4.1);
    Check("times that are no times are left out", ahead.Measured(0.0, film) == Change::NONE &&
                                                      ahead.Measured(30.0, 0.0) == Change::NONE &&
                                                      ahead.Measured(-1.0, film) == Change::NONE);
  }
  {
    // what is found stays from one native output to the next
    CDVAhead ahead;
    Feed(ahead, 24, 30.0, film);
    ahead.Presented();
    Check("the end of a native output that had it tells nothing", !ahead.Ended());
    Check("... and the next one has it from its first frame", ahead.Wanted(Mode::NEEDED, film));
    Feed(ahead, 20, 4.0, film);
    ahead.Ended();
    Fed fed = Feed(ahead, 23, 30.0, film);
    Check("an average does not go on from one output into the next", fed.notNeeded == 0 && fed.needed == 0);
  }
  {
    // failures
    CDVAhead ahead;
    Feed(ahead, 24, 30.0, film);
    Check("two frames that could not be presented a frame ahead leave it on",
          !ahead.Failed() && !ahead.Failed() && ahead.Wanted(Mode::NEEDED, film) && !ahead.Off());
    Check("the third gives it up for this native output, and says so once",
          ahead.Failed() && !ahead.Wanted(Mode::NEEDED, film) && !ahead.Wanted(Mode::ALWAYS, film) &&
              ahead.Off() && !ahead.Failed());
    Check("the end of that output does not give it up for good", !ahead.Ended());
    Check("the next output tries again", ahead.Wanted(Mode::NEEDED, film) && !ahead.Off());
    ahead.Failed();
    ahead.Failed();
    ahead.Failed();
    Check("a second output that gives it up: still not for good", !ahead.Ended() && ahead.Wanted(Mode::NEEDED, film));
    ahead.Failed();
    ahead.Failed();
    ahead.Failed();
    Check("the third in a row: not again until Kodi starts again, told once",
          ahead.Ended() && !ahead.Wanted(Mode::NEEDED, film) && !ahead.Wanted(Mode::ALWAYS, film) &&
              ahead.Off() && !ahead.Ended() && !ahead.Failed() && !ahead.Failed(true));
  }
  {
    // an output that had it to its end stops the count of those that gave it up
    CDVAhead ahead;
    for (int output = 0; output < 2; ++output)
    {
      ahead.Failed();
      ahead.Failed();
      ahead.Failed();
      ahead.Ended();
    }
    ahead.Presented();
    ahead.Failed();
    Check("two outputs gave it up, the third had it to its end: nothing is given up",
          !ahead.Ended() && ahead.Wanted(Mode::ALWAYS, film));
    for (int output = 0; output < 2; ++output)
    {
      ahead.Failed();
      ahead.Failed();
      ahead.Failed();
      Check("... and two more that give it up are not three in a row", !ahead.Ended());
    }
    // an output in which nothing was presented that way says nothing either way
    Check("an output without a frame ahead and without a failure leaves the count", !ahead.Ended());
    ahead.Failed();
    ahead.Failed();
    ahead.Failed();
    Check("... so the next one that gives it up is the third", ahead.Ended());
  }
  {
    // failures that come far apart are not three in a row: a minute of frames presented a frame ahead
    // forgets the ones before it
    CDVAhead ahead;
    ahead.Failed();
    ahead.Failed();
    for (int i = 0; i < 1439; ++i)
      ahead.Presented();
    Check("two failures, 1439 frames presented a frame ahead, a third failure: given up for this output",
          ahead.Failed() && ahead.Off());
    ahead.Ended();
    ahead.Failed();
    ahead.Failed();
    for (int i = 0; i < 1440; ++i)
      ahead.Presented();
    Check("two failures, a minute of frames presented a frame ahead, and two more: not given up",
          !ahead.Failed() && !ahead.Failed() && !ahead.Off());
    Check("... the third of those gives it up", ahead.Failed() && ahead.Off());
    ahead.Ended();
    // a failure in the middle of the minute starts the minute again
    ahead.Failed();
    for (int i = 0; i < 1000; ++i)
      ahead.Presented();
    ahead.Failed();
    for (int i = 0; i < 1000; ++i)
      ahead.Presented();
    Check("a failure starts the minute again: two failures 1000 frames apart and a third 1000 frames later give it up",
          ahead.Failed() && ahead.Off());
  }
  {
    // what counts as the same refresh rate, and the mark itself
    CDVAhead ahead;
    Fed fed = Feed(ahead, 24, 20.0, 40.0);
    Check("frames that take exactly half of the refresh period call for it", fed.needed == 1);
    CDVAhead film24;
    Feed(film24, 24, 30.0, 1000.0 / 23.976);
    Check("found at 23.976 Hz: wanted at 24 Hz too, not at 25 Hz",
          film24.Wanted(Mode::NEEDED, 1000.0 / 24.0) && !film24.Wanted(Mode::NEEDED, 1000.0 / 25.0));
    fed = Feed(film24, 24, 30.0, 1000.0 / 24.0);
    Check("... and frames at 24 Hz go on with it, without telling of it again", fed.needed == 0 && fed.notNeeded == 0 &&
                                                                                 film24.Wanted(Mode::NEEDED, 1000.0 / 24.0));
  }
  {
    CDVAhead ahead;
    Check("a reason that does not pass gives it up at once, told once",
          ahead.Failed(true) && !ahead.Wanted(Mode::ALWAYS, film) && ahead.Off() && !ahead.Failed(true) &&
              !ahead.Ended() && !ahead.Wanted(Mode::ALWAYS, film));
  }
  // ---- Hades: frames presented the way that waits that miss their refresh
  {
    CDVAhead ahead;
    Fed fed = Runs(ahead, 240, Spread(3), film);
    Check("a run of 240 with 3 that missed their refresh does not call for it alone",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240, Spread(3), film);
    Check("a second such run does, told once, and the GPU time was never told",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film) && MissesCall(ahead, film));
    Check("... with the figures of the two runs for the log", MissedWere(ahead, 3, 3));
    fed = Runs(ahead, 240 * 6, Spread(9), film);
    Check("more of them tell nothing more", fed.needed == 0 && fed.notNeeded == 0);
    // an average of light frames does not end it: they were light when they missed their refresh
    fed = Feed(ahead, 24 * 60, 8.0, film);
    Check("a minute of GPU times of 8 ms of 41.7 does not end it, and tells nothing",
          fed.needed == 0 && fed.notNeeded == 0 && ahead.Wanted(Mode::NEEDED, film));
    Check("... at that refresh rate only", !ahead.Wanted(Mode::NEEDED, sixty));
    ahead.Presented();
    Check("it is kept from one native output to the next", !ahead.Ended() && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    CDVAhead ahead;
    Fed fed = Runs(ahead, 240 * 30, Spread(2), film);
    Check("runs of 240 with 2 that missed their refresh never call for it (5 minutes)",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240, Spread(3), film);
    fed = Runs(ahead, 240, {}, film);
    fed = Runs(ahead, 240, {}, film);
    fed = Runs(ahead, 240, Spread(3), film);
    Check("two runs that count, three runs apart, do not either", fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240, {}, film);
    fed = Runs(ahead, 240, Spread(4), film);
    Check("... two in three do", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // what is set aside
    CDVAhead ahead;
    Runs(ahead, 240, Spread(3), film);
    ahead.Seeked();
    // 2 s of film: 48 frames, all of them late, count for nothing
    std::vector<unsigned int> first48;
    for (unsigned int i = 0; i < 48; ++i)
      first48.push_back(i);
    Fed fed = Runs(ahead, 48, first48, film);
    fed = Runs(ahead, 240, {}, film);
    Check("after a seek, 2 s of frames that all missed their refresh count for nothing",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240, Spread(3), film);
    Check("... the runs after it count, and with the one before the seek call for it",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    CDVAhead ahead;
    // GUI elements came and went: the first second of frames without them after that is set aside,
    // and the run goes on
    Runs(ahead, 200, {10, 20}, film);
    ahead.Changed();
    std::vector<unsigned int> first24;
    for (unsigned int i = 0; i < 24; ++i)
      first24.push_back(i);
    Fed fed = Runs(ahead, 24, first24, film);
    fed = Runs(ahead, 40, {30}, film); // the run of 240 ends here, with 3
    Check("after GUI elements, 1 s of late frames counts for nothing; the run goes on and counts",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240, Spread(3), film);
    Check("... and with the next one calls for it", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // both reasons: the average that ends the one of the GPU time does not end it while misses call for it
    CDVAhead ahead;
    Fed fed = Feed(ahead, 24, 30.0, film);
    Check("the GPU time calls for it first", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
    fed = Runs(ahead, 240 * 3, Spread(9), film);
    Check("frames presented the way that waits while it is called for tell nothing", fed.needed == 0);
    fed = Feed(ahead, 24, 8.0, film);
    Check("light frames end what the GPU time called for, and tell it", fed.notNeeded == 1 &&
                                                                          !ahead.Wanted(Mode::NEEDED, film));
    Runs(ahead, 240, Spread(3), film);
    fed = Runs(ahead, 240, Spread(3), film);
    Check("then misses call for it", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
    fed = Feed(ahead, 24, 30.0, film);
    Check("... and a heavy average says nothing more", fed.needed == 0 && fed.notNeeded == 0);
    fed = Feed(ahead, 24, 8.0, film);
    Check("... nor a light one: nothing ends while the misses call for it",
          fed.needed == 0 && fed.notNeeded == 0 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // refresh rates and the rest
    CDVAhead ahead;
    Runs(ahead, 240, Spread(3), film);
    Fed fed = Runs(ahead, 240, Spread(3), sixty);
    Check("runs at two refresh rates do not count together", fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film) &&
                                                                  !ahead.Wanted(Mode::NEEDED, sixty));
    fed = Runs(ahead, 240, Spread(3), sixty);
    Check("two runs at 59.94 Hz call for it there, not at 23.976 Hz",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, sixty) && !ahead.Wanted(Mode::NEEDED, film));
    Check("no refresh period: nothing", Waited(ahead, true, 0.0) == Change::NONE);
    Check("it is never wanted where frames ahead are turned off", !ahead.Wanted(Mode::NEVER, sixty));
    ahead.Failed();
    ahead.Failed();
    ahead.Failed();
    Check("three failures give it up for the output all the same", ahead.Off() && !ahead.Wanted(Mode::NEEDED, sixty));
    ahead.Ended();
    Check("... and the next output has it again", ahead.Wanted(Mode::NEEDED, sixty));
  }
  {
    // a run half counted does not go on into the next output
    CDVAhead ahead;
    Runs(ahead, 240, Spread(3), film);
    Runs(ahead, 200, {1, 2, 3}, film);
    ahead.Ended();
    Fed fed = Runs(ahead, 40, {}, film);
    fed = Runs(ahead, 240, Spread(3), film);
    Check("the runs of an output that ended do not go on into the next one",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
  }
  // ---- Hades: a GPU that cannot keep up at all: QUICK_LATE of QUICK_WINDOW frames late in QUICK_RUNS runs in a row,
  // past the first START_ASIDE_MS of an output, call for frames ahead at once (QuickRuns(), the GT 1030)
  {
    const unsigned int aside = static_cast<unsigned int>(CDVAhead::START_ASIDE_MS / film) + 1;
    const unsigned int frames = aside + CDVAhead::QUICK_RUNS * CDVAhead::QUICK_WINDOW;
    std::vector<unsigned int> late, atStart;
    for (unsigned int r = 0; r < CDVAhead::QUICK_RUNS; ++r)
      for (unsigned int k = 0; k < CDVAhead::QUICK_LATE; ++k)
      {
        late.push_back(aside + r * CDVAhead::QUICK_WINDOW + k * 4);
        atStart.push_back(r * CDVAhead::QUICK_WINDOW + k * 4);
      }
    CDVAhead quick;
    Fed fed = Runs(quick, frames, late, film);
    Check("quick: QUICK_RUNS runs of QUICK_WINDOW frames with QUICK_LATE late each call for it at once, told as such",
          fed.needed == 1 && quick.QuickRuns() && quick.Wanted(Mode::NEEDED, film));
    CDVAhead fewer;
    late.pop_back();
    fed = Runs(fewer, frames, late, film);
    Check("quick: ... one late frame fewer in the last run does not", fed.needed == 0 && !fewer.QuickRuns());
    CDVAhead early;
    fed = Runs(early, frames - aside, atStart, film);
    Check("quick: ... nor the same runs in the first START_ASIDE_MS of an output", fed.needed == 0 && !early.QuickRuns());
  }
  // ---- Hades: misses all along, each with a frame over a period of GPU time (SustainedRuns(), issue #4)
  {
    CDVAhead ahead;
    const double over = 45.0; // (a frame over the 41.7 ms of a film refresh)
    Fed fed{};
    for (int r = 0; r < 6; ++r)
    {
      const Fed f = TimedRuns(ahead, 240, {100}, {100}, 8.0, over, film);
      fed.needed += f.needed;
    }
    Check("six runs of 240, each with a frame late and a frame over a period, do not call for it",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    fed = TimedRuns(ahead, 240, {100}, {100}, 8.0, over, film);
    Check("the seventh does, told once, as sustained misses (7 runs)",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film) && MissesCall(ahead, film) && Sustained(ahead, 7));
    fed = TimedRuns(ahead, 240 * 10, Spread(1), {7}, 8.0, over, film);
    Check("... more of them tell nothing more", fed.needed == 0 && fed.notNeeded == 0);
    ahead.Presented();
    Check("... and it is kept into the next native output, at that rate", !ahead.Ended() &&
              ahead.Wanted(Mode::NEEDED, film) && !ahead.Wanted(Mode::NEEDED, sixty));
  }
  {
    // the RTX 4080 SUPER: frames late now and then, without a frame over a period of GPU time
    CDVAhead ahead;
    Fed fed{};
    for (int r = 0; r < 300; ++r)
    {
      const Fed f = TimedRuns(ahead, 240, {50}, {}, 9.0, 0.0, film);
      fed.needed += f.needed;
    }
    Check("300 runs, each with a frame late and none over a period of GPU time, never call for it",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    // frames over a period, none late
    for (int r = 0; r < 60; ++r)
    {
      const Fed f = TimedRuns(ahead, 240, {}, {10, 20}, 9.0, 60.0, film);
      fed.needed += f.needed;
    }
    Check("60 runs with frames over a period and none late never call for it",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
    // late and over in different runs: not the same run
    for (int r = 0; r < 60; ++r)
    {
      const Fed f = TimedRuns(ahead, 240, r % 2 ? std::vector<unsigned int>{30} : std::vector<unsigned int>{},
                              r % 2 ? std::vector<unsigned int>{} : std::vector<unsigned int>{30}, 9.0, 60.0, film);
      fed.needed += f.needed;
    }
    Check("late frames and frames over a period in alternate runs never call for it",
          fed.needed == 0 && !ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // marked runs too far apart: five in every 30
    CDVAhead ahead;
    Fed fed{};
    for (int r = 0; r < 300; ++r)
    {
      const bool mark = r % 5 == 0;
      const Fed f = TimedRuns(ahead, 240, mark ? std::vector<unsigned int>{90} : std::vector<unsigned int>{},
                              mark ? std::vector<unsigned int>{90} : std::vector<unsigned int>{}, 9.0, 50.0, film);
      fed.needed += f.needed;
    }
    Check("a marked run in every five (six in 30) never calls for it", fed.needed == 0 &&
                                                                               !ahead.Wanted(Mode::NEEDED, film));
    // one more in the window: a marked run in every five (six in 30) does
    for (int r = 0; r < 30; ++r)
    {
      const bool mark = r % 4 == 0;
      const Fed f = TimedRuns(ahead, 240, mark ? std::vector<unsigned int>{90} : std::vector<unsigned int>{},
                              mark ? std::vector<unsigned int>{90} : std::vector<unsigned int>{}, 9.0, 50.0, film);
      fed.needed += f.needed;
    }
    Check("... a marked run in every four (seven or eight in 30) does", fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
  }
  {
    // what ends the count: a refresh rate, the end of an output; a seek keeps it
    CDVAhead ahead;
    for (int r = 0; r < 6; ++r)
      TimedRuns(ahead, 240, {100}, {100}, 8.0, 45.0, film);
    ahead.Seeked();
    TimedRuns(ahead, 48, {}, {}, 8.0, 0.0, film); // (the 2 s set aside)
    Fed fed = TimedRuns(ahead, 240, {100}, {100}, 8.0, 45.0, film);
    Check("a seek keeps the marked runs: six before it and one after call for it",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film));
    CDVAhead b;
    for (int r = 0; r < 6; ++r)
      TimedRuns(b, 240, {100}, {100}, 8.0, 45.0, film);
    b.Ended();
    fed = TimedRuns(b, 240, {100}, {100}, 8.0, 45.0, film);
    Check("the end of an output forgets them: six, the end, one: not called for",
          fed.needed == 0 && !b.Wanted(Mode::NEEDED, film));
    CDVAhead c;
    for (int r = 0; r < 6; ++r)
      TimedRuns(c, 240, {100}, {100}, 8.0, 45.0, film);
    TimedRuns(c, 240, {100}, {100}, 8.0, 45.0 * 16.7 / 41.7, sixty);
    fed = TimedRuns(c, 240, {100}, {100}, 8.0, 45.0, film);
    Check("another refresh rate in between forgets them", fed.needed == 0 && !c.Wanted(Mode::NEEDED, film));
    // the frames over a period in the time set aside after GUI elements went count for nothing
    CDVAhead d;
    for (int r = 0; r < 6; ++r)
      TimedRuns(d, 240, {100}, {100}, 8.0, 45.0, film);
    d.Changed();
    fed = TimedRuns(d, 264, {5}, {5}, 8.0, 45.0, film); // (frame 5 is in the first second: set aside; the run ends at 264)
    Check("a late frame over a period in the second set aside after GUI elements does not count",
          fed.needed == 0 && !d.Wanted(Mode::NEEDED, film));
  }
  {
    // the rule of MISS_LATE comes first, and says so
    CDVAhead ahead;
    TimedRuns(ahead, 240, Spread(3), Spread(3), 8.0, 45.0, film);
    const Fed fed = TimedRuns(ahead, 240, Spread(3), Spread(3), 8.0, 45.0, film);
    Check("two runs with 3 late call for it by MISS_LATE, not as sustained misses",
          fed.needed == 1 && ahead.Wanted(Mode::NEEDED, film) && !SustainedSaid(ahead) && MissedWere(ahead, 3, 3));
  }
  if (argc > 1)
    ReplayFiles(argc - 1, argv + 1);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
