/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: how exact Kodi's own timing of flips can be (VRR_VIDEO::Pacing::TIMER, windowing/gbm/VrrVideo.h)
 * on a host: the same schedule (CSchedule) and the same wait (clock_nanosleep, CLOCK_MONOTONIC, TIMER_ABSTIME) as
 * CWinSystemGbm::PaceVrrVideo(), with some work between the slots like a render loop, measured as Kodi measures it
 * (CIntervals: each interval between two wakes against the one asked for) and as the wake itself (late by how much).
 * The commit of the flip and the display are not here: on hardware kodi.log measures them.
 * usage: vrr_pacer_bench <seconds> <rate Hz> <work us per slot>
 */
#include "windowing/gbm/VrrVideo.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace KODI::WINDOWING::GBM::VRR_VIDEO;

static int64_t Now()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char** argv)
{
  const double seconds = argc > 1 ? std::atof(argv[1]) : 60.0;
  const double rate = argc > 2 ? std::atof(argv[2]) : 48000.0 / 1001.0;
  const int64_t workNs = (argc > 3 ? std::atoll(argv[3]) : 3000) * 1000LL;
  const double period = 1e9 / rate;
  CSchedule schedule;
  schedule.Start(Now() + static_cast<int64_t>(period), period);
  CIntervals intervals;
  CIntervals wakes;
  const int64_t end = Now() + static_cast<int64_t>(seconds * 1e9);
  int64_t lastWake = 0;
  int64_t lastSlot = 0;
  uint64_t late = 0;
  volatile uint64_t sink = 0;
  while (Now() < end)
  {
    // the work of a frame (drawing it), before its slot is taken, as in Kodi's render loop
    const int64_t workEnd = Now() + workNs;
    while (Now() < workEnd)
      sink = sink + 1;
    uint64_t skipped = 0;
    const int64_t slot = schedule.Time(schedule.Take(Now(), 500000, skipped));
    late += skipped;
    struct timespec ts;
    ts.tv_sec = slot / 1000000000LL;
    ts.tv_nsec = slot % 1000000000LL;
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
    {
    }
    const int64_t wake = Now();
    wakes.Add(wake - slot, 0);
    if (lastWake && !skipped)
      intervals.Add((wake - lastWake) - (slot - lastSlot), 0);
    lastWake = wake;
    lastSlot = slot;
  }
  std::printf("rate %.3f Hz, %.0f s, %lld us of work per slot: %llu slots, %llu missed\n", rate, seconds,
              static_cast<long long>(workNs / 1000), static_cast<unsigned long long>(wakes.Count()),
              static_cast<unsigned long long>(late));
  std::printf("wake after its slot: median %.0f us, p99 %.0f us, max %.0f us, %llu over 1 ms\n",
              wakes.QuantileUs(0.5), wakes.QuantileUs(0.99), wakes.MaxUs(),
              static_cast<unsigned long long>(wakes.Over1ms()));
  std::printf("interval against the one asked for: p1 %.0f us, median %.0f us, p99 %.0f us, min %.0f, max %.0f us, "
              "%llu over 1 ms\n",
              intervals.QuantileUs(0.01), intervals.QuantileUs(0.5), intervals.QuantileUs(0.99), intervals.MinUs(),
              intervals.MaxUs(), static_cast<unsigned long long>(intervals.Over1ms()));
  return 0;
}
