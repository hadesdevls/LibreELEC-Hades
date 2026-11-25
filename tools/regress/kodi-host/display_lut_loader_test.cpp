// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// display_lut_loader_test.cpp: the LUT files of the display read and converted off the render thread
// (xbmc/windowing/gbm/DisplayLutLoader.h of the Kodi tree), with loads of the test's own that take a set time and count
// the tables alive: the render thread never waits for a read (LOADING at once, READY with the table once it is read,
// or with none and why); one file is read at a time; a file asked for anew lets go of the table of before before it is
// read; a read whose file is no longer asked for (another one, or Clear()) is never given; at most a table of each kind
// and one being read are alive; the loader ends while a read runs. Built with the sanitizers (address and undefined,
// and thread in a second build). Prints a line per failed check and PASSED or FAILED (n) last.
#include "windowing/gbm/DisplayLutLoader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using KODI::WINDOWING::GBM::DISPLAY_LUT::CLutLoader;
using namespace std::chrono_literals;

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
std::atomic<int> g_live{0}; // tables alive
std::atomic<int> g_maxLive{0};
std::atomic<int> g_reading{0}; // reads in progress
std::atomic<int> g_maxReading{0};
std::atomic<int> g_reads{0};
std::atomic<int> g_liveAtRead{-1}; // tables alive as the last read began

struct Table
{
  std::string path;
  int range;
  Table(std::string p, int r) : path(std::move(p)), range(r)
  {
    const int now = ++g_live;
    int was = g_maxLive.load();
    while (now > was && !g_maxLive.compare_exchange_weak(was, now))
      ;
  }
  ~Table() { --g_live; }
};

// a read: "slow*" takes 300 ms, "bad*" fails, anything else 50 ms
CLutLoader<Table>::Ptr Read(const std::string& path, int range, std::string& why)
{
  g_liveAtRead = g_live.load();
  const int now = ++g_reading;
  int was = g_maxReading.load();
  while (now > was && !g_maxReading.compare_exchange_weak(was, now))
    ;
  ++g_reads;
  std::this_thread::sleep_for(path.rfind("slow", 0) == 0 ? 300ms : 50ms);
  --g_reading;
  if (path.rfind("bad", 0) == 0)
  {
    why = "the file could not be read";
    return nullptr;
  }
  return std::make_shared<Table>(path, range);
}

using Loader = CLutLoader<Table>;

// asks until the table is READY (at most 3 s); how long the longest Want() took, in ms
Loader::Got Until(Loader& l, int kind, const std::string& path, int range, double* longestMs = nullptr)
{
  Loader::Got got;
  double longest = 0;
  const auto end = std::chrono::steady_clock::now() + 3s;
  do
  {
    const auto start = std::chrono::steady_clock::now();
    got = l.Want(kind, path, range);
    longest = std::max(longest,
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    if (got.state == Loader::State::READY)
      break;
    std::this_thread::sleep_for(5ms);
  } while (std::chrono::steady_clock::now() < end);
  if (longestMs)
    *longestMs = longest;
  return got;
}
} // namespace

int main()
{
  {
    Loader l(Read);
    // the render thread never waits: LOADING at once while a read of 300 ms runs
    const auto start = std::chrono::steady_clock::now();
    Loader::Got got = l.Want(0, "slow-a.cube", 0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    expect(got.state == Loader::State::LOADING && !got.table, "the first ask: loading");
    expect(ms < 100, "the first ask returns at once, not after the read (" + std::to_string(ms) + " ms)");
    double longest = 0;
    got = Until(l, 0, "slow-a.cube", 0, &longest);
    expect(longest < 100, "no ask waits for the read (the longest " + std::to_string(longest) + " ms)");
    expect(got.state == Loader::State::READY && got.table && got.table->path == "slow-a.cube" && got.fresh,
           "ready with its table, fresh the first time");
    expect(got.ms >= 250, "the time of the read is told (" + std::to_string(got.ms) + " ms)");
    got = l.Want(0, "slow-a.cube", 0);
    expect(got.state == Loader::State::READY && got.table && !got.fresh, "asked again: the same table, not fresh");
    expect(g_reads == 1, "read once (" + std::to_string(g_reads.load()) + ")");
    got = {};
    // another file of the kind: the table of before is let go first, before the new one is read
    g_liveAtRead = -1;
    got = Until(l, 0, "b.cube", 0);
    expect(got.table && got.table->path == "b.cube", "the new file of the kind");
    expect(g_liveAtRead == 0, "the table of before was let go before the new file was read (" +
                                  std::to_string(g_liveAtRead.load()) + " alive)");
    got = {};
    // another range of the same file is another table
    got = Until(l, 0, "b.cube", 1);
    expect(got.table && got.table->range == 1, "another range: read again");
    got = {};
    // a file that cannot be read: ready, no table, why
    got = Until(l, 1, "bad.cube", 0);
    expect(got.state == Loader::State::READY && !got.table && got.why == "the file could not be read" && got.fresh,
           "a file that cannot be read: no table and why");
    got = {};
  }
  expect(g_live == 0, "every table let go with the loader (" + std::to_string(g_live.load()) + ")");
  {
    // both kinds asked at once: one file read at a time
    g_maxReading = 0;
    Loader l(Read);
    l.Want(0, "slow-s.cube", 0);
    l.Want(1, "slow-h.cube", 0);
    Loader::Got s = Until(l, 0, "slow-s.cube", 0), h = Until(l, 1, "slow-h.cube", 0);
    expect(s.table && h.table, "both kinds ready");
    expect(g_maxReading == 1, "one file read at a time (" + std::to_string(g_maxReading.load()) + " at once)");
  }
  {
    // a file asked for while another of the kind is read: the first is never given, and is let go of
    Loader l(Read);
    const int reads = g_reads;
    l.Want(0, "slow-old.cube", 0);
    std::this_thread::sleep_for(50ms);
    Loader::Got got = Until(l, 0, "new.cube", 0);
    expect(got.table && got.table->path == "new.cube", "the file asked for last is given, not the one read before");
    expect(g_reads - reads == 2, "both were read, one after the other (" + std::to_string(g_reads - reads) + ")");
    got = {};
    std::this_thread::sleep_for(20ms);
    expect(g_live == 1, "the table of the file no longer asked for is let go (" + std::to_string(g_live.load()) +
                            " alive)");
  }
  {
    // Clear() while a file is read: what it reads is let go of when it ends, and nothing is given
    Loader l(Read);
    l.Want(0, "slow-c.cube", 0);
    std::this_thread::sleep_for(50ms);
    l.Clear();
    std::this_thread::sleep_for(400ms);
    expect(!l.Busy(), "the read ended");
    expect(g_live == 0, "cleared: the table read meanwhile is let go (" + std::to_string(g_live.load()) + " alive)");
    Loader::Got got = l.Want(0, "slow-c.cube", 0);
    expect(got.state == Loader::State::LOADING, "asked again after Clear(): read again");
  }
  {
    // the loader ends while a read runs: it waits for it, and nothing is left
    auto* l = new Loader(Read);
    l->Want(0, "slow-d.cube", 0);
    std::this_thread::sleep_for(20ms);
    delete l;
    expect(g_reading == 0, "the read had ended when the loader was gone");
  }
  expect(g_live == 0, "no table left (" + std::to_string(g_live.load()) + ")");
  expect(g_maxLive <= 3, "never more than a table of each kind and one being read alive (" +
                             std::to_string(g_maxLive.load()) + ")");
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
