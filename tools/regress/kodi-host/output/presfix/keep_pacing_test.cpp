/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): the render loop of native Dolby Vision
 * output with kodi-9268 (a picture that is on the screen is kept instead of drawn again,
 * KeepDVBridgeFrame()) where the vblank ioctl is not there (nvidia-drm with its defaults), and
 * K35 (frames presented a frame ahead) switching on and off. From the model of the review
 * (review-a/tests/keep_pacing_model.cpp).
 *
 * The loop is transcribed (RenderManager's choice of the picture by time, PresentRender(),
 * KeepDVBridgeFrame()); what decides how a picture is kept is, with the tree's
 * windowing/gbm/DVRepeatKeep.h, the real CDVRepeatKeep (Way(), VBlankFailed()). Without it
 * (the code before the fix) that code is transcribed: every keep that cannot be paced forgets the
 * picture on the screen, presents nothing and sleeps 10 ms, and the next one tries again.
 *  - A pass that draws: CPU time O, then GPU time D. The way that waits: glFinish(), then a commit
 *    that returns with its flip, at the first vblank after the GPU is done. A frame ahead: the
 *    commit is made once the flip of the frame before has come (its out-fence) or the refresh of a
 *    picture kept in place of a frame ahead, and flips at the first vblank after both it and the
 *    GPU work are done.
 *  - A pass that keeps the picture, paced by the vblanks: after the CPU time O, the flip of a frame
 *    presented a frame ahead is waited for, then the refresh of a picture kept before; then,
 *    presented the way that waits, the next vblank; a frame ahead, none (the refresh after the
 *    current one is kept as the flip of this one). Paced by a commit of the same picture
 *    (video/dvkeepbycommit, the tree only, the way that waits only): the commit returns with its
 *    flip, at the next vblank.
 *  - nvidia-drm 615.78.08 with its defaults has no vblank support at all (EOPNOTSUPP;
 *    nvidia-drm-os-interface.c:47, nvidia-drm-drv.c:906-918, drm_vblank.c:601-604, 1735-1752).
 *
 * A refresh is "wrong" when the picture on the screen at that vblank is not the one whose time it
 * is. Times are in ticks of 1/120000 s.
 */
#if __has_include("windowing/gbm/DVRepeatKeep.h")
#include "windowing/gbm/DVRepeatKeep.h"
#define HAVE_FIX 1
using KODI::WINDOWING::GBM::CDVRepeatKeep;
#else
#define HAVE_FIX 0
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

using Tick = int64_t;
constexpr Tick TPS = 120000;
constexpr Tick EPS = 12; // 0.1 ms: a commit or a wait returns this long after its vblank
static Tick Ms(double ms) { return static_cast<Tick>(ms * TPS / 1000.0 + 0.5); }

struct Config
{
  Tick period;
  Tick picture;
  double cpuMs;
  double gpuMs;
  bool keep; //!< kodi-9268 on (videoscreen.hadesdvkeeprepeats)
  bool vblank; //!< the vblank ioctl works (amdgpu, i915; not nvidia-drm with its defaults)
  int on1 = -1, off1 = -1, on2 = -1; //!< frames presented a frame ahead, by pass
  double seconds = 12;
  bool byCommit = false; //!< video/dvkeepbycommit (the tree only)
  bool mutNoAdjust = false;
  bool mutKeepNoWait = false;
};

struct Result
{
  int refreshes = 0;
  int wrong = 0;
  int drawn = 0;
  int kept = 0;
  int keepFailed = 0; //!< passes that tried to keep, could not pace, and presented nothing
  int presentedAhead = 0;
};

#if HAVE_FIX
using Keep = CDVRepeatKeep;
#else
struct Keep
{
  void Begin(bool) {}
};
#endif

static Result Run(const Config& c, Keep& keep)
{
  struct Flip
  {
    int64_t vblank;
    int64_t picture;
  };
  std::vector<Flip> flips;
  const Tick P = c.period;
  const Tick O = Ms(c.cpuMs);
  const Tick D = Ms(c.gpuMs);
  Tick t = EPS;
  bool shownValid = false;
  int64_t shownPicture = -1;
  bool lastAhead = false;
  int64_t aheadFlip = -1;
  int64_t repeatFlip = -1;
  Result r;
  keep.Begin(c.byCommit);
  const Tick end = static_cast<Tick>(c.seconds * TPS);
  auto vblankAfter = [P](Tick x) { return x / P + 1; };
  for (int pass = 0; t < end; ++pass)
  {
    const bool wantAhead = (c.on1 >= 0 && pass >= c.on1 && (c.off1 < 0 || pass < c.off1)) ||
                           (c.on2 >= 0 && pass >= c.on2);
    const int64_t picture = (t + P + (lastAhead && !c.mutNoAdjust ? P : 0)) / c.picture;
    const Tick present = t + O;
#if HAVE_FIX
    const auto way = keep.Way(lastAhead);
    const bool tries = way != Keep::Pace::NONE; // DVKeepsPicture()
#else
    const bool tries = true;
#endif
    if (c.keep && tries && shownValid && picture == shownPicture)
    {
      // KeepDVBridgeFrame()
      Tick now = present;
      if (aheadFlip >= 0)
      {
        now = std::max(now, aheadFlip * P + EPS);
        aheadFlip = -1;
      }
#if HAVE_FIX
      if (way == Keep::Pace::COMMIT)
      {
        // a commit of the same picture that returns with its flip
        now = (now / P + 1) * P + EPS;
        ++r.kept;
        t = now;
        continue;
      }
#endif
      if (!c.vblank)
      {
        shownValid = false;
        repeatFlip = -1;
        ++r.keepFailed;
#if HAVE_FIX
        keep.VBlankFailed(EOPNOTSUPP);
#endif
        t = now + Ms(10);
        continue;
      }
      if (repeatFlip >= 0)
      {
        now = std::max(now, repeatFlip * P + EPS);
        repeatFlip = -1;
      }
      const int64_t current = now / P;
      if (lastAhead)
        repeatFlip = current + 1;
      else if (!c.mutKeepNoWait)
        now = (current + 1) * P + EPS;
      ++r.kept;
      t = now;
      continue;
    }
    ++r.drawn;
    const Tick gpuDone = present + D;
    Tick commit = wantAhead ? present : gpuDone;
    if (repeatFlip >= 0)
    {
      commit = std::max(commit, repeatFlip * P + EPS);
      repeatFlip = -1;
    }
    if (aheadFlip >= 0)
    {
      commit = std::max(commit, aheadFlip * P + EPS);
      aheadFlip = -1;
    }
    const int64_t flipAt = vblankAfter(std::max(commit, gpuDone));
    flips.push_back({flipAt, picture});
    shownValid = true;
    shownPicture = picture;
    if (wantAhead)
    {
      ++r.presentedAhead;
      aheadFlip = flipAt;
      lastAhead = true;
      t = commit + EPS;
    }
    else
    {
      lastAhead = false;
      t = flipAt * P + EPS;
    }
  }
  size_t f = 0;
  int64_t onScreen = -1;
  const int64_t firstVblank = TPS / P;
  const int64_t lastVblank = (end - TPS / 2) / P;
  for (int64_t w = 0; w <= lastVblank; ++w)
  {
    while (f < flips.size() && flips[f].vblank <= w)
      onScreen = flips[f++].picture;
    if (w < firstVblank)
      continue;
    ++r.refreshes;
    r.wrong += onScreen != (w * P) / c.picture;
  }
  return r;
}

static Result Run(const Config& c)
{
  Keep keep;
  return Run(c, keep);
}

static void Report(const std::string& what, const Result& r)
{
  std::printf("model: %-74s %4d refreshes, %4d wrong, %4d drawn, %4d kept, %4d keep passes that "
              "presented nothing, %4d a frame ahead\n",
              what.c_str(), r.refreshes, r.wrong, r.drawn, r.kept, r.keepFailed, r.presentedAhead);
}

int main()
{
  std::printf("tree: %s\n", HAVE_FIX ? "CDVRepeatKeep (DVRepeatKeep.h)" : "the code before the fix");
  const Tick p5994 = 2002;
  const Tick p23976 = 5005;
  const Tick f23976 = 5005;
  const Tick f2997 = 4004;

  // --- kodi-9268 on nvidia-drm: 23.976 fps film at 59.94 Hz (the refresh rate not adjusted), the
  // way that waits
  for (const double gpu : {3.6, 5.2, 7.0})
  {
    Config base{p5994, f23976, 2.0, gpu, false, true};
    const Result first = Run(base);
    Config keepOk = base;
    keepOk.keep = true;
    const Result kept = Run(keepOk);
    Config keepNv = keepOk;
    keepNv.vblank = false;
    // two native outputs one after the other, in one start of Kodi
    Keep session;
    const Result nv = Run(keepNv, session);
    const Result nv2 = Run(keepNv, session);
    const std::string g = " (GPU " + std::to_string(gpu).substr(0, 3) + " ms, CPU 2 ms)";
    Report("film at 59.94 Hz, every refresh drawn (1.0 RC1)" + g, first);
    Report("film at 59.94 Hz, kodi-9268, vblank ioctl works (amdgpu, i915)" + g, kept);
    Report("film at 59.94 Hz, kodi-9268, no vblank ioctl (nvidia-drm defaults)" + g, nv);
    Report("... the next native output in the same start of Kodi" + g, nv2);
    Check(first.wrong == 0, "model sanity: every refresh drawn in time gives the cadence" + g);
    Check(kept.wrong == 0 && kept.kept > 0,
          "kodi-9268 where the vblank ioctl works: the same cadence, and pictures kept" + g);
    Check(nv.keepFailed <= 1 && nv2.keepFailed == 0,
          "without the vblank ioctl: one pass finds it out, then every frame is drawn in that "
          "native output and the ones after it, until Kodi is started again" + g);
    Check(nv.wrong <= first.wrong && nv2.wrong <= first.wrong,
          "kodi-9268 without the vblank ioctl is no worse than drawing every refresh" + g);
#if HAVE_FIX
    // option B: a commit of the same picture paces it, no vblank needed
    Config byCommit = keepNv;
    byCommit.byCommit = true;
    const Result nvb = Run(byCommit);
    Report("film at 59.94 Hz, no vblank ioctl, kept by a commit (dvkeepbycommit)" + g, nvb);
    Check(nvb.wrong == 0 && nvb.kept > 0 && nvb.keepFailed == 0,
          "dvkeepbycommit without the vblank ioctl: pictures kept, the same cadence" + g);
#endif
  }

  std::printf("model: sensitivity, film at 59.94 Hz, kodi-9268 without the vblank ioctl: wrong "
              "refreshes by CPU time of a pass (rows) and GPU time of a frame (columns 3.6, 5.2, "
              "7.0, 9.0 ms); every refresh drawn gives 0 in every cell\n");
  int worse = 0;
  for (const double cpu : {0.5, 1.0, 2.0, 3.0})
  {
    std::printf("model:   CPU %.1f ms:", cpu);
    for (const double gpu : {3.6, 5.2, 7.0, 9.0})
    {
      Config nvc{p5994, f23976, cpu, gpu, true, false};
      Config firstc{p5994, f23976, cpu, gpu, false, true};
      const int nvWrong = Run(nvc).wrong;
      const int firstWrong = Run(firstc).wrong;
      std::printf(" %4d (1.0 RC1 %d)", nvWrong, firstWrong);
      worse += nvWrong > firstWrong;
    }
    std::printf("\n");
  }
  Check(worse == 0, "no vblank, every CPU and GPU time of the table: no worse than 1.0 RC1");

  // --- K35: frames a frame ahead switched on and off (vblank ioctl working)
  struct Case
  {
    const char* name;
    Tick period;
    Tick picture;
    double gpu;
  };
  for (const Case& k : {Case{"film at 23.976 Hz (one refresh a picture)", p23976, f23976, 30.0},
                        Case{"29.97 fps at 59.94 Hz (two refreshes a picture)", p5994, f2997, 9.0},
                        Case{"film at 59.94 Hz (two and a half refreshes)", p5994, f23976, 9.0},
                        Case{"film at 59.94 Hz, GPU slower than a refresh", p5994, f23976, 25.0}})
  {
    for (const bool keepOn : {false, true})
    {
      for (const bool commitOn : {false, true})
      {
        if (commitOn && (!keepOn || !HAVE_FIX))
          continue;
        Config c{k.period, k.picture, 2.0, k.gpu, keepOn, true, 60, 300, 520};
        c.byCommit = commitOn;
        const Result r = Run(c);
        const std::string what = std::string(k.name) +
                                 (keepOn ? (commitOn ? ", kodi-9268 by commit" : ", kodi-9268")
                                         : ", every refresh drawn") +
                                 ", a frame ahead on/off/on";
        Report(what, r);
        if (k.gpu < 1000.0 * k.period / TPS)
          Check(r.wrong == 0, "K35 switching on and off loses or repeats no picture: " + what);
      }
    }
  }
  {
    Config a{p5994, f23976, 2.0, 9.0, true, true, 60, 300, 520};
    a.mutNoAdjust = true;
    const Result ra = Run(a);
    Report("MUTATION no latency adjustment for frames ahead (film at 59.94 Hz)", ra);
    Check(ra.wrong > 0, "mutation caught: frames ahead without the latency adjustment show "
                        "pictures at the wrong refreshes");
    Config b{p5994, f23976, 2.0, 5.2, true, true};
    b.mutKeepNoWait = true;
    const Result rb = Run(b);
    Report("MUTATION a kept picture that does not wait for the vblank", rb);
    Check(rb.wrong > 0, "mutation caught: kept pictures that do not pace the loop");
  }
  std::printf(g_failures ? "KEEP-PACING-TEST FAILED (%d)\n" : "KEEP-PACING-TEST PASSED\n",
              g_failures);
  return g_failures ? 1 : 0;
}
