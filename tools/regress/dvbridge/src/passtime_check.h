// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the GPU time of each pass (dvbridge_render_time_passes()) in a bridge
// test. DV_PASSTIMES=1 makes libplacebo with its timers on OpenGL ES (gles_timer_queries, a patch of the libplacebo
// recipe) and turns
// the timing on in the renderer; without it nothing of it is to be made, and the test checks that. Every render is
// timed on the host, with glFinish() after it (on Mesa's software GL the GPU's work is the CPU's), for what the
// timing costs. Included by a test after its Check(); the bridge's functions are weak, so that the test also builds
// with a bridge that has none of it (it then says so and fails).
#ifndef HADES_PASSTIME_CHECK_H
#define HADES_PASSTIME_CHECK_H

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_passtime.h" // (tools/dvbridge of the tree under test)

__attribute__((weak)) void dvbridge_render_time_passes(struct dvbridge_renderer* renderer, bool on);
__attribute__((weak)) bool dvbridge_render_passes_timed(const struct dvbridge_renderer* renderer, bool* placebo,
                                                        bool* own);
__attribute__((weak)) unsigned dvbridge_render_take_pass_times(struct dvbridge_renderer* renderer,
                                                               struct dvbridge_pass_time* out, unsigned max);

static int g_passOn = -1;
static double g_renderMs, g_renderCpuMs;
static long g_renders;
static long g_packs __attribute__((unused));

static int PassTimesOn(void)
{
  if (g_passOn < 0)
  {
    const char* e = getenv("DV_PASSTIMES");
    g_passOn = e && !strcmp(e, "1");
  }
  return g_passOn;
}

static double PassNowMs(clockid_t clock)
{
  struct timespec t;
  clock_gettime(clock, &t);
  return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

// the wall time, and the CPU time of the process (on lavapipe the GPU's work is done by threads of the process:
// the CPU time is what the work took, less what other processes on the machine take from it)
#define TIMED_RENDER(expr) \
  ({ \
    const double t0_ = PassNowMs(CLOCK_MONOTONIC), c0_ = PassNowMs(CLOCK_PROCESS_CPUTIME_ID); \
    const int ok_ = (expr); \
    glFinish(); \
    g_renderMs += PassNowMs(CLOCK_MONOTONIC) - t0_; \
    g_renderCpuMs += PassNowMs(CLOCK_PROCESS_CPUTIME_ID) - c0_; \
    ++g_renders; \
    ok_; \
  })

static void PassTimesStart(struct dvbridge_renderer* r)
{
  if (PassTimesOn() && dvbridge_render_time_passes)
    dvbridge_render_time_passes(r, true);
}

// own: how many times the bridge's own passes ran (0: this test draws none of them); placeboDraws: libplacebo draws
// pictures of this test (0: none, where the source transport takes the decoder's planes because the composer's
// mapping is the identity: then none of libplacebo's passes may have a time)
static void PassTimesSummary(pl_gpu gpu, struct dvbridge_renderer* r, long own, int placeboDraws)
{
  printf("render time: %.1f ms over %ld renders, %.3f ms each (pass timing %s); CPU time %.3f ms each\n", g_renderMs,
         g_renders, g_renders ? g_renderMs / g_renders : 0.0, PassTimesOn() ? "on" : "off",
         g_renders ? g_renderCpuMs / g_renders : 0.0);
  if (!dvbridge_render_time_passes || !dvbridge_render_passes_timed || !dvbridge_render_take_pass_times)
  {
    Check(0, "the bridge times the passes (it has no dvbridge_render_time_passes())");
    return;
  }
  if (PassTimesOn())
  {
    bool placebo = false, mine = false;
    const bool timed = dvbridge_render_passes_timed(r, &placebo, &mine);
    struct dvbridge_pass_time t[32];
    const unsigned n = dvbridge_render_take_pass_times(r, t, 32);
    long placeboTimes = 0, ownTimes = 0, ownPack = 0;
    for (unsigned i = 0; i < n && i < 32; ++i)
    {
      printf("pass: %-64s %5u times, average %.3f ms, longest %.3f ms\n", t[i].name, t[i].count,
             t[i].count ? t[i].sum_ms / t[i].count : 0.0, t[i].max_ms);
      if (!strncmp(t[i].name, "bridge: ", 8))
      {
        ownTimes += t[i].count;
        if (strstr(t[i].name, "packing"))
          ownPack += t[i].count;
      }
      else
        placeboTimes += t[i].count;
    }
    printf("pass times: %u passes, %ld times of libplacebo's, %ld of the bridge's own (%ld of the packing, of %ld "
           "packings)\n", n, placeboTimes, ownTimes, ownPack, own);
    Check(timed && placebo && mine,
          "the passes are timed: libplacebo's (its GPU makes timers on OpenGL ES with gles_timer_queries) and the "
          "bridge's own (GL_EXT_disjoint_timer_query)");
    if (placeboDraws)
      Check(placeboTimes > 0, "libplacebo's passes have their times");
    else
      Check(placeboTimes == 0, "libplacebo draws none of these pictures (the source transport takes the decoder's "
                               "planes where the composer's mapping is the identity): none of its passes has a time");
    // each packing's time comes back on a later frame: all of them but those still in flight (at most 8)
    if (own)
      Check(ownPack <= own && ownPack >= own - 8, "the bridge's packing has the time of every packing but the last few");
    Check(dvbridge_render_take_pass_times(r, t, 32) == 0, "a window taken starts again: nothing in the next one");
  }
  else
  {
    pl_timer timer = pl_timer_create(gpu);
    Check(timer == NULL, "off: libplacebo makes no timer on OpenGL ES, so its passes begin no query");
    pl_timer_destroy(gpu, &timer);
    Check(!dvbridge_render_passes_timed(r, NULL, NULL) && !dvbridge_render_take_pass_times(r, NULL, 0),
          "off: the renderer times nothing");
  }
}

#endif
