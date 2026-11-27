// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// start_skips_test.cpp: the pictures that the renderer leaves out before the first picture of a video is shown
// (xbmc/cores/VideoPlayer/VideoRenderers/StartSkips.h of the Kodi tree), counted apart from the pictures it skips while
// the video plays: the skip loop of CRenderManager::PrepareNextRender() played over the start of Avatar on the owner's
// RTX 4080 SUPER (1.2.3: the clock ran from the first picture ready, the first frame went out 126 ms later, three
// pictures due at 23.976 Hz; the player information said "skipped 1"), and where RenderManager.cpp takes it (in the
// source). Hades 1.2.3 (av3): the same for a stream set up again while the video plays (Start(true): the owner's Hokum,
// its output switched while it played, "skipped 7"), and each choice that skips pictures while the video plays told in
// kodi.log with what came before it (CSkipsTold: the output showed no new picture for more than a refresh and a half,
// or the pictures came late), ten a video. Prints a line per failed check and PASSED or FAILED (n) last.
#include "cores/VideoPlayer/VideoRenderers/StartSkips.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>

using KODI::RENDERQUEUE::CSkipsTold;
using KODI::RENDERQUEUE::CStartSkips;

static int failed = 0;
static void expect(bool ok, const std::string& what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what.c_str());
    ++failed;
  }
}

namespace
{
// the choice of PrepareNextRender(): the latest of the queued pictures that are due is shown; each one left out goes
// through m_presentsourcePast, the first of them uncounted (as Kodi has it), the others counted as skipped unless no
// picture was shown yet
struct Renderer
{
  CStartSkips start;
  int skipped = 0; // m_QueueSkip
  bool past = false; // m_presentsourcePast >= 0
  std::string said;
  std::deque<double> queued; // pts in ms

  void Prepare(double renderPts)
  {
    if (queued.empty() || renderPts < queued.front())
      return;
    size_t idx = 0;
    while (idx + 1 < queued.size() && renderPts >= queued[idx + 1] + 0.98 * 41.708)
      ++idx;
    for (size_t i = 0; i < idx; ++i)
    {
      if (past && !start.Skipped())
        ++skipped;
      past = true;
      queued.pop_front();
    }
    const std::string line = start.Shown((renderPts - queued.front()) * 1000.0);
    if (!line.empty())
      said = line;
    queued.pop_front();
  }
};

std::string Read(const std::string& path)
{
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
} // namespace

int main()
{
  const double period = 1000.0 / 23.976;
  // the start of Avatar: four pictures queued from the clock's 0, the output shows its first frame 126 ms later
  {
    Renderer r;
    r.start.Start();
    for (int i = 0; i < 8; ++i)
      r.queued.push_back(i * period);
    r.Prepare(126.0);
    expect(r.skipped == 0, "the start: no picture counted as skipped (got " + std::to_string(r.skipped) + ")");
    expect(r.start.Count() == 1, "the start: one picture counted apart (got " + std::to_string(r.start.Count()) + ")");
    expect(r.said.find("1 picture of the start came due before the output showed its first frame") == 0,
           "kodi.log says it: " + r.said);
    expect(r.said.find(", 1 ms after its time by the clock") != std::string::npos ||
               r.said.find(" ms after its time by the clock") != std::string::npos,
           "kodi.log says how late the picture shown was");
    // then the video plays: a picture late by more than a period is skipped as before, and counted
    for (double t = 126.0 + period; t < 126.0 + 3 * period; t += period)
      r.Prepare(t);
    r.Prepare(126.0 + 6 * period);
    expect(r.skipped >= 1, "while it plays: a late picture is counted as skipped (got " + std::to_string(r.skipped) + ")");
    expect(r.start.Count() == 1, "while it plays: nothing more counted apart");
  }
  // Hokum (HDR10): seven pictures came due before the first frame: five counted apart, none skipped
  {
    Renderer r;
    r.start.Start();
    for (int i = 0; i < 10; ++i)
      r.queued.push_back(i * period);
    r.Prepare(7.2 * period);
    expect(r.skipped == 0 && r.start.Count() == 5,
           "seven due at the first frame: five apart, none skipped (got " + std::to_string(r.start.Count()) + "/" +
               std::to_string(r.skipped) + ")");
    expect(r.said.find("5 pictures of the start came due") == 0 && r.said.find("were left out") != std::string::npos,
           "kodi.log: five, in the plural");
  }
  // a start in time: nothing said
  {
    Renderer r;
    r.start.Start();
    for (int i = 0; i < 4; ++i)
      r.queued.push_back(i * period);
    r.Prepare(5.0);
    expect(r.said.empty() && r.start.Count() == 0 && r.skipped == 0, "a start in time: nothing said or counted");
    expect(r.start.FirstShown(), "the first picture is marked shown");
  }
  // a new video starts the count again
  {
    CStartSkips s;
    s.Start();
    expect(s.Skipped(), "before the first picture: counted apart");
    s.Shown(0.0);
    expect(!s.Skipped(), "after it: not counted apart");
    s.Start();
    expect(s.Skipped() && s.Count() == 1, "the next video: counted apart again");
  }
  // a stream set up again while the video plays: what comes due before its first frame is counted apart, and said as
  // of the stream set up again
  {
    Renderer r;
    r.start.Start();
    for (int i = 0; i < 4; ++i)
      r.queued.push_back(i * period);
    r.Prepare(5.0);
    // set up again (CRenderManager::Configure() with a picture shown): the queue starts again, the clock runs on
    r.start.Start(true);
    r.past = false;
    r.queued.clear();
    for (int i = 20; i < 32; ++i)
      r.queued.push_back(i * period);
    r.Prepare(30.3 * period);
    expect(r.skipped == 0 && r.start.Count() == 8,
           "nine left out at the first frame of the stream set up again: eight apart, none skipped (got " +
               std::to_string(r.start.Count()) + "/" + std::to_string(r.skipped) + ")");
    expect(r.said.find("8 pictures of the stream set up again while the video played") == 0,
           "kodi.log says them as of the stream set up again: " + r.said);
  }
  // each choice that skips while the video plays is told, with what came before it
  {
    using C = CSkipsTold::Clock;
    const C::time_point t0 = C::time_point() + std::chrono::seconds(100);
    const auto at = [&](double ms)
    { return t0 + std::chrono::microseconds(static_cast<long long>(ms * 1000.0)); };
    CSkipsTold told;
    told.Start();
    expect(told.Chosen(at(0), 0, 0.0, period, 0).empty(), "the first picture, nothing skipped: nothing said");
    expect(told.Chosen(at(period), 0, 0.0, period, 0).empty(), "a picture in time: nothing said");
    // the output showed nothing for 268 ms (the longest gap of the owner's first windows): two pictures skipped
    const std::string stall = told.Chosen(at(period + 268.0), 2, 60000.0, period, 2);
    expect(stall.find("2 pictures skipped as late while the video plays (2 in this video, ") == 0,
           "a stall: how many, and in this video: " + stall);
    expect(stall.find("chosen 268.0 ms earlier (6.4 refresh periods): the output showed no new picture meanwhile") !=
               std::string::npos,
           "a stall: said as the output showing nothing: " + stall);
    expect(stall.find("the one shown instead was 60.0 ms after its time by the clock") != std::string::npos,
           "a stall: how late the picture shown was: " + stall);
    // pictures that came late, with a frame at every refresh
    const std::string late = told.Chosen(at(period * 2 + 268.0), 1, 45000.0, period, 3);
    expect(late.find("1 picture skipped") == 0 &&
               late.find("the pictures were late when they came (the decoder, the player)") != std::string::npos,
           "late pictures: said as late when they came: " + late);
    // ten a video, then once that the rest are counted only, then nothing
    CSkipsTold many;
    many.Start();
    int lines = 0, rest = 0;
    for (int i = 0; i < 30; ++i)
    {
      const std::string line = many.Chosen(at(i * period), 1, 0.0, period, i + 1);
      if (line.find("further pictures skipped in this video are counted only") == 0)
        ++rest;
      else if (!line.empty())
        ++lines;
    }
    expect(lines == CSkipsTold::MOST && rest == 1,
           "ten lines a video and one for the rest (" + std::to_string(lines) + ", " + std::to_string(rest) + ")");
    many.Start();
    expect(!many.Chosen(at(1000.0), 1, 0.0, period, 1).empty(), "the next video: told again");
  }
  // where RenderManager.cpp takes it
  {
    const std::string rm = Read(std::string(KODI_TREE) + "/xbmc/cores/VideoPlayer/VideoRenderers/RenderManager.cpp");
    expect(rm.find("  m_QueueSkip   = 0;\n  m_startSkips.Start();\n") != std::string::npos,
           "RenderManager.cpp: a video starts the count again where the skips are reset (PreInit())");
    expect(rm.find("        if (!m_startSkips.Skipped())\n          m_QueueSkip++;\n") != std::string::npos,
           "RenderManager.cpp: a late picture left out before the first one shown is not counted as skipped");
    size_t at = 0;
    int shown = 0;
    while ((at = rm.find("m_startSkips.Shown(", at)) != std::string::npos)
    {
      ++shown;
      ++at;
    }
    expect(shown == 2, "RenderManager.cpp: both ways of choosing a picture mark the first one shown (" +
                           std::to_string(shown) + ")");
    expect(rm.find("    if (m_startSkips.FirstShown())\n      m_startSkips.Start(true);\n") != std::string::npos,
           "RenderManager.cpp: a stream set up again after a picture was shown counts apart again (Configure())");
    expect(rm.find("  m_startSkips.Start();\n  m_skipsTold.Start();\n") != std::string::npos,
           "RenderManager.cpp: a video starts the lines of the skips again (PreInit())");
    size_t chosen = 0;
    int choices = 0;
    while ((chosen = rm.find("m_skipsTold.Chosen(", chosen)) != std::string::npos)
    {
      ++choices;
      ++chosen;
    }
    expect(choices == 2, "RenderManager.cpp: both ways of choosing a picture are told to CSkipsTold (" +
                             std::to_string(choices) + ")");
    expect(rm.find("    const int skippedBefore = m_QueueSkip;") != std::string::npos &&
               rm.find("                                             m_QueueSkip - skippedBefore,") != std::string::npos,
           "RenderManager.cpp: what a choice skipped is what it counted");
  }
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
