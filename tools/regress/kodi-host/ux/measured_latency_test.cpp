// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// measured_latency_test.cpp: the correction of Kodi's display latency that Hades measures (xbmc/windowing/gbm/
// MeasuredLatency.h of the Kodi tree) where nothing is measured yet for a display mode with this GPU, driver and build
// (CLatencyMemory::Provisional()), and when what was measured is taken (in the source: a seek takes it, not only the
// next video).
//
// The owner's 1.2.3 test build 7530675 (start-52): each video that was the first at its rate with a new build played a
// refresh period before its sound (Avatar, TrueHD at 23.976 Hz: -38 ms; Carrie, E-AC-3 at 24 Hz: -62 ms), and each seek
// after the measurement said "from the next video or seek on" kept it (a seek empties the render queue through
// CRenderManager::DiscardBuffer(), which did not take the measurement; only Flush() did). The memory of that PC before
// the build: the build before (4eabe75) measured 1 period at every mode and way. Prints a line per failed check and
// PASSED or FAILED (n) last.
#include "windowing/gbm/MeasuredLatency.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using KODI::WINDOWING::GBM::CLatencyMemory;
using KODI::WINDOWING::GBM::CMeasuredLatency;
using From = CLatencyMemory::From;

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
std::string Read(const std::string& path)
{
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

const char* Name(From from)
{
  switch (from)
  {
    case From::NOTHING:
      return "nothing";
    case From::THIS_KEY:
      return "this key";
    case From::OTHER_KEYS:
      return "other keys";
  }
  return "?";
}

constexpr uint64_t OLD = 0x4eabe75000000001ULL; // the build before
constexpr uint64_t NEW = 0x7530675000000002ULL; // the build of start-52
constexpr uint64_t IGPU = 0x1a2b3c4d5e6f7081ULL; // another GPU of the same PC

std::string N(uint64_t key, unsigned int w, unsigned int h, unsigned int mhz, CMeasuredLatency::Way way,
              bool interlaced = false)
{
  return CLatencyMemory::Name(key, w, h, mhz, interlaced, way);
}

void Expect(const std::string& all, const std::string& name, From from, int periods, const std::string& what)
{
  int got = -9;
  const From f = CLatencyMemory::Provisional(all, name, got);
  expect(f == from && (from == From::NOTHING || got == periods),
         what + ": " + Name(f) + " " + std::to_string(got) + ", expected " + Name(from) + " " +
             std::to_string(periods));
}
} // namespace

int main()
{
  using Way = CMeasuredLatency::Way;
  const Way W = Way::WAITING, O = Way::ORDINARY, A = Way::AHEAD;
  // what the build before measured on the owner's PC
  std::string memory;
  memory = CLatencyMemory::Put(memory, N(OLD, 3840, 2160, 23976, W), 1);
  memory = CLatencyMemory::Put(memory, N(OLD, 3840, 2160, 23976, O), 1);
  memory = CLatencyMemory::Put(memory, N(OLD, 3840, 2160, 120000, W), 1);
  memory = CLatencyMemory::Put(memory, N(OLD, 1920, 1080, 23977, O), 1);

  // Avatar, the first video of the new build, at 23.976 Hz the way that waits: what the build before measured there
  Expect(memory, N(NEW, 3840, 2160, 23976, W), From::OTHER_KEYS, 1, "a new build at a mode measured before");
  // ... and its ordinary output
  Expect(memory, N(NEW, 3840, 2160, 23976, O), From::OTHER_KEYS, 1, "a new build, the ordinary output");
  // once the new build measured 23.976 Hz, Carrie at 24 Hz: what this build measured at its other modes, first
  std::string after = CLatencyMemory::Put(memory, N(NEW, 3840, 2160, 23976, W), 1);
  Expect(after, N(NEW, 3840, 2160, 24000, W), From::THIS_KEY, 1, "this build at another mode");
  // this build's other modes come before the builds before at this mode, where both are there and differ
  std::string both = CLatencyMemory::Put(after, N(OLD, 3840, 2160, 24000, W), 0);
  Expect(both, N(NEW, 3840, 2160, 24000, W), From::THIS_KEY, 1, "this build first, before the builds before");
  // a way is never taken for another: nothing of the new build for frames ahead, and nothing before either
  Expect(after, N(NEW, 3840, 2160, 24000, A), From::NOTHING, 0, "frames ahead: nothing measured that way");
  // this key's modes that disagree: nothing, and not the other keys either
  std::string split = CLatencyMemory::Put(after, N(NEW, 1920, 1080, 60000, W), 0);
  Expect(split, N(NEW, 3840, 2160, 24000, W), From::NOTHING, 0, "this build disagrees with itself");
  // other keys that disagree at this mode: nothing
  std::string others = CLatencyMemory::Put(memory, N(IGPU, 3840, 2160, 23976, W), 0);
  Expect(others, N(NEW, 3840, 2160, 23976, W), From::NOTHING, 0, "the keys before disagree");
  // a mode that nothing measured: nothing
  Expect(memory, N(NEW, 2560, 1440, 59951, W), From::NOTHING, 0, "a mode never measured");
  // an interlaced mode is not its progressive one
  Expect(memory, N(NEW, 3840, 2160, 23976, W, true), From::NOTHING, 0, "an interlaced mode");
  // an empty memory, or one that is not well formed
  Expect("", N(NEW, 3840, 2160, 23976, W), From::NOTHING, 0, "nothing kept");
  Expect("zz/3840x2160@23976/w=1 4eabe75000000001/3840x2160@23976/w=7", N(NEW, 3840, 2160, 23976, W),
         From::NOTHING, 0, "entries not well formed");
  // the measured value of the same name is never consulted here: Get() has it (the caller asks Provisional() only
  // without it); and a measurement of the same key at the same mode is not "another mode"
  Expect(after, N(NEW, 3840, 2160, 23976, W), From::OTHER_KEYS, 1, "the same name: the other keys only");
  // two other keys that agree, one of them a period sooner (-1) at another mode: the mode's entries alone count
  std::string two = CLatencyMemory::Put(memory, N(IGPU, 3840, 2160, 23976, W), 1);
  two = CLatencyMemory::Put(two, N(IGPU, 1280, 720, 50000, W), -1);
  Expect(two, N(NEW, 3840, 2160, 23976, W), From::OTHER_KEYS, 1, "two keys before that agree at this mode");

  // where it is taken (in the source)
  const std::string K = KODI_TREE;
  const std::string ws = Read(K + "/xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  expect(ws.find("      !known && !name.empty() && period > 0.0\n          ? CLatencyMemory::Provisional(LatencyMemory(), "
                 "name, provisional)") != std::string::npos,
         "WinSystemGbmGLESContext.cpp: what was measured elsewhere is asked only where this mode has nothing");
  expect(ws.find("  const int applied = known ? CLatencyMemory::Taken(LatencyOn(), LatencyMemory(), name)\n"
                 "                      : from != CLatencyMemory::From::NOTHING && LatencyOn() ? provisional\n") !=
             std::string::npos,
         "WinSystemGbmGLESContext.cpp: it is applied, with the setting on, until this mode is measured");
  const std::string rm = Read(K + "/xbmc/cores/VideoPlayer/VideoRenderers/RenderManager.cpp");
  const size_t discard = rm.find("void CRenderManager::DiscardBuffer()");
  const size_t flag = rm.find("  m_latencyDiscarded = true;", discard);
  const size_t next = rm.find("\n}\n", discard);
  expect(discard != std::string::npos && flag != std::string::npos && flag < next,
         "RenderManager.cpp: a seek (DiscardBuffer()) marks the measurement to be taken");
  const size_t move = rm.find("void CRenderManager::FrameMove()");
  const size_t take = rm.find("  if (m_latencyDiscarded.exchange(false))", move);
  const size_t commit = rm.find("      dvWindow->VideoLatencyCommit(false);", take);
  const size_t prepare = rm.find("      PrepareNextRender();", move);
  expect(move != std::string::npos && take != std::string::npos && commit != std::string::npos &&
             prepare != std::string::npos && take < commit && commit < prepare,
         "RenderManager.cpp: FrameMove() takes it before a picture after the seek is chosen");

  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
