// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: the depth of subtitles and of the interface over a 3D video that carries none of its own, measured
// from the two pictures: StereoDepth.cpp of the tree (MeasureNearest(), SignatureOf()/IsCut(), CPlanner), and the
// shift the overlay renderer draws a subtitle with (the body of CRenderer::SubtitleDepth() out of the tree's
// OverlayRenderer.cpp, by depth_gen.py).
//
// The pictures are made here, of known depth: two eyes of 480 x 270 (the size of the copy the graphics make,
// CStereoDepthCapture) of a smooth random texture (value noise, two octaves) seen at a disparity, with things in front
// of it at theirs, to fractions of a pixel; the right eye 12 levels brighter and with noise of +-3 levels, as two
// cameras or two encodings differ. Disparity d is where the right eye sees what the left sees at x: x + d; in front of
// the screen d < 0, and the depth of a thing is -d / 480.
//   measure  the nearest thing of an area: a plane, a thing in front of it, one behind it, a thing outside the area,
//            fractions of a pixel, a thing nearer than the search, a flat area, one bright pixel and one wrong block
//            (they must not move the result), and the whole picture
//   cut      a cut is told from a picture of the same shot and from a fade
//   planner  a subtitle appears in front of what was measured, comes forward only, smoothly, where two measures in a
//            row put something nearer, never jumps, never comes nearer than the comfort limit; one that follows another
//            closely keeps its depth unless the new one is further off than the hysteresis; a cut forgets the shot
//            before it only after two measures of the new one; the interface takes its depth as it opens and keeps it,
//            in front of the subtitles
//   film     a film at 24 pictures a second, each second picture measured and its measure ready two pictures later, as
//            the render manager does: a thing in the subtitles' area comes nearer while two subtitles are shown, and
//            in no picture is a subtitle drawn behind it (by the shift of SubtitleDepth()); a thing that comes into
//            the area at once while a subtitle is shown has it in front within 0.6 s. A subtitle never goes back nor
//            moves by more than the speed allows. -DOLD_RULE: the subtitles as Kodi placed them (the setting, 0),
//            which must fail
// Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "StereoDepth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

using namespace KODI::STEREODEPTH;

static int failures = 0;
static void check(bool ok, const char* what, double got, double want)
{
  std::printf("%s: %s: %.5f (want %.5f)\n", ok ? "ok" : "FAIL", what, got, want);
  failures += !ok;
}
static void near(const char* what, std::optional<float> got, double want, double tolerance)
{
  if (!got)
  {
    std::printf("FAIL: %s: nothing found (want %.5f)\n", what, want);
    ++failures;
    return;
  }
  check(std::fabs(*got - want) <= tolerance, what, *got, want);
}

// ------------------------------------------------------------------------------- the stand-ins of SubtitleDepth()
enum class RenderStereoView { OFF, LEFT, RIGHT };
enum class RenderStereoMode { OFF, SPLIT_HORIZONTAL, SPLIT_VERTICAL, MONO };
struct CRect
{
  float x1{0}, y1{0}, x2{0}, y2{0};
  float Width() const { return x2 - x1; }
};
struct CGraphicContext
{
  RenderStereoMode mode{RenderStereoMode::SPLIT_VERTICAL};
  RenderStereoView view{RenderStereoView::LEFT};
  RenderStereoMode GetStereoMode() const { return mode; }
  RenderStereoView GetStereoView() const { return view; }
};
struct Win
{
  CGraphicContext gfx;
  CGraphicContext& GetGfxContext() { return gfx; }
};
struct CServiceBroker
{
  static Win* GetWinSystem()
  {
    static Win w;
    return &w;
  }
};
// the setting subtitles.stereoscopicdepth at Kodi's default
static int GetStereoscopicDepth()
{
  return 0;
}
struct COverlay
{
  bool m_isDiscMenu{false};
};
struct CRenderer
{
  std::optional<int> m_subtitlePlaneOffset;
  std::optional<float> m_measuredDepth;
  CRect m_rs{0, 0, 1920, 1080}, m_rd{0, 0, 1920, 1080};
  float SubtitleDepth(const COverlay& o) const;
};
float CRenderer::SubtitleDepth(const COverlay& o) const
#include "subtitle_depth_body.inc"

// ---------------------------------------------------------------------------------------------- the pictures
constexpr int W = 480;
constexpr int H = 270;

struct Rng
{
  uint32_t s;
  uint32_t Next()
  {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float Unit() { return static_cast<float>(Next() % 100000) / 100000.0f; }
};

// a smooth texture that can be sampled anywhere: value noise of two octaves (cells of 6 and 2.5 pixels)
struct Texture
{
  int cells{0};
  float size{1};
  std::vector<float> grid;
  Texture(uint32_t seed, float cellSize, int width) : size(cellSize)
  {
    Rng r{seed};
    cells = static_cast<int>(width / cellSize) + 4;
    grid.resize(static_cast<size_t>(cells) * cells);
    for (float& g : grid)
      g = r.Unit();
  }
  float At(float x, float y) const
  {
    const float gx = x / size + 1.0f;
    const float gy = y / size + 1.0f;
    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, cells - 2);
    const int iy = std::clamp(static_cast<int>(std::floor(gy)), 0, cells - 2);
    const float fx = gx - static_cast<float>(ix);
    const float fy = gy - static_cast<float>(iy);
    auto g = [&](int a, int b) { return grid[static_cast<size_t>(b) * cells + a]; };
    const float top = g(ix, iy) * (1 - fx) + g(ix + 1, iy) * fx;
    const float bottom = g(ix, iy + 1) * (1 - fx) + g(ix + 1, iy + 1) * fx;
    return top * (1 - fy) + bottom * fy;
  }
};

struct Thing
{
  float x1, y1, x2, y2; // where the left eye sees it
  float d; // its disparity
  uint32_t seed;
  bool flat{false};
};

struct Pair
{
  std::vector<uint8_t> left = std::vector<uint8_t>(static_cast<size_t>(W) * H);
  std::vector<uint8_t> right = std::vector<uint8_t>(static_cast<size_t>(W) * H);
  Plane L() const { return {left.data(), W, H, W}; }
  Plane R() const { return {right.data(), W, H, W}; }
};

static float Sample(const Texture& a, const Texture& b, float x, float y)
{
  return 40.0f + 120.0f * a.At(x, y) + 55.0f * b.At(x, y);
}

// the background at disparity bg (or flat grey), and the things over it, the last nearest
static Pair Make(float bg, const std::vector<Thing>& things, uint32_t noiseSeed = 7, bool flatBackground = false)
{
  Pair p;
  const Texture b1(11, 6.0f, W + 64), b2(12, 2.5f, W + 64);
  std::vector<Texture> t1, t2;
  for (const Thing& t : things)
  {
    t1.emplace_back(t.seed, 6.0f, W + 64);
    t2.emplace_back(t.seed + 1000, 2.5f, W + 64);
  }
  Rng noise{noiseSeed};
  for (int y = 0; y < H; ++y)
  {
    for (int x = 0; x < W; ++x)
    {
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);
      // the left eye sees the background at x; the right eye at x sees what the left saw at x - d
      float l = flatBackground ? 128.0f : Sample(b1, b2, fx + 20.0f, fy);
      float r = flatBackground ? 128.0f : Sample(b1, b2, fx - bg + 20.0f, fy);
      for (size_t i = 0; i < things.size(); ++i)
      {
        const Thing& t = things[i];
        if (fy >= t.y1 && fy < t.y2)
        {
          if (fx >= t.x1 && fx < t.x2)
            l = t.flat ? 200.0f : Sample(t1[i], t2[i], fx, fy);
          const float lx = fx - t.d;
          if (lx >= t.x1 && lx < t.x2)
            r = t.flat ? 200.0f : Sample(t1[i], t2[i], lx, fy);
        }
      }
      const float n = static_cast<float>(static_cast<int>(noise.Next() % 7) - 3);
      p.left[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(std::clamp(l, 0.0f, 255.0f));
      p.right[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(std::clamp(r + 12.0f + n, 0.0f, 255.0f));
    }
  }
  return p;
}

static float Z(float d)
{
  return -d / static_cast<float>(W);
}

// the subtitles' area, as CStereoDepthMeter gives it before any subtitle was seen
static const Area SUBS{0.1f, 0.6f, 0.9f, 1.0f};

static void TestMeasure()
{
  const float px = 1.0f / W;
  {
    const Pair p = Make(6.0f, {});
    near("a plane behind the screen (+6)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(6), 0.3 * px);
  }
  {
    const Pair p = Make(6.0f, {{150, 190, 300, 250, -10.0f, 21}});
    const Measure m = MeasureNearest(p.L(), p.R(), SUBS);
    near("a thing in front of it in the area (-10)", m.nearest, Z(-10), 0.3 * px);
    check(m.matched > 100 && m.matched <= m.blocks, "blocks matched in the area", m.matched, 100);
  }
  {
    // nearer than the screen, but only in the upper part: not in the subtitles' area, in the whole picture
    const Pair p = Make(6.0f, {{100, 20, 300, 120, -20.0f, 31}, {150, 190, 300, 250, -10.0f, 21}});
    near("a nearer thing outside the area does not count (-10)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(-10),
         0.3 * px);
    MatchParams whole;
    whole.step = 16;
    near("... and is the nearest of the whole picture (-20)",
         MeasureNearest(p.L(), p.R(), Area{0.02f, 0.02f, 0.98f, 0.98f}, whole).nearest, Z(-20), 0.4 * px);
  }
  {
    const Pair p = Make(6.0f, {{150, 190, 300, 250, 2.0f, 21}});
    near("a thing between the background and the screen (+2): the nearest", MeasureNearest(p.L(), p.R(), SUBS).nearest,
         Z(2), 0.3 * px);
  }
  {
    const Pair p = Make(6.0f, {{150, 190, 300, 250, -7.5f, 21}});
    near("a fraction of a pixel (-7.5)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(-7.5f), 0.35 * px);
  }
  {
    const Pair p = Make(6.0f, {{150, 190, 300, 250, -3.25f, 21}});
    near("a fraction of a pixel (-3.25)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(-3.25f), 0.35 * px);
  }
  {
    // nearer than the 32 pixels looked for (6.7 % of the width): never taken as far; nearer than the comfort limit
    const Pair p = Make(6.0f, {{150, 190, 330, 250, -40.0f, 21}});
    const std::optional<float> got = MeasureNearest(p.L(), p.R(), SUBS).nearest;
    check(got && *got >= PlannerParams{}.comfort + PlannerParams{}.margin,
          "a thing nearer than the search: nearer than the comfort limit", got ? *got : -1,
          PlannerParams{}.comfort + PlannerParams{}.margin);
  }
  {
    const Pair p = Make(0.0f, {}, 7, true);
    const Measure m = MeasureNearest(p.L(), p.R(), SUBS);
    check(!m.nearest && m.blocks == 0, "a flat area (a black bar): nothing", m.blocks, 0);
  }
  {
    // a bright pixel in the left eye only, and a block of 8 x 8 at -30 in front of a plane at +6: neither is the
    // nearest thing (one block of more than a hundred matched)
    Pair p = Make(6.0f, {{200, 200, 208, 208, -30.0f, 41}});
    p.left[static_cast<size_t>(230) * W + 100] = 255;
    near("one bright pixel and one stray block do not move it (+6)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(6),
         0.3 * px);
  }
  {
    // a small thing does: 32 x 24 pixels (1.9 % of the area) at -12
    const Pair p = Make(6.0f, {{200, 200, 232, 224, -12.0f, 41}});
    near("a small thing, 32 x 24 pixels (-12)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(-12), 0.5 * px);
  }
  {
    // stripes 6 pixels apart at the screen (0), over the plane at +6: a block of them matches as well 6, 12 ...
    // pixels further, which is no measure of them; never taken as nearer than they are
    Pair p = Make(6.0f, {});
    for (int y = 190; y < 250; ++y)
      for (int x = 120; x < 360; ++x)
        p.left[static_cast<size_t>(y) * W + x] = p.right[static_cast<size_t>(y) * W + x] =
            static_cast<uint8_t>(x % 6 < 3 ? 60 : 190);
    const std::optional<float> got = MeasureNearest(p.L(), p.R(), SUBS).nearest;
    check(got && *got <= Z(0) + 0.5 * px, "stripes (a texture that repeats) are not taken as near", got ? *got : -1,
          Z(0));
  }
  {
    // different light in the two eyes: matched by the shape of the blocks, not their level
    Pair p = Make(6.0f, {{150, 190, 300, 250, -10.0f, 21}});
    for (uint8_t& v : p.right)
      v = static_cast<uint8_t>(std::min(255, v + 25));
    near("a brighter right eye (-10)", MeasureNearest(p.L(), p.R(), SUBS).nearest, Z(-10), 0.3 * px);
  }
}

static void TestCut()
{
  const Pair a = Make(6.0f, {{150, 190, 300, 250, -10.0f, 21}});
  const Pair b = Make(6.0f, {{160, 190, 310, 250, -10.0f, 21}}, 9);
  // another shot: darker, with a bright thing in its upper half
  Pair c = Make(-2.0f, {{50, 20, 400, 120, -3.0f, 61, true}}, 5);
  for (uint8_t& v : c.left)
    v = static_cast<uint8_t>(v < 200 ? v * 35 / 100 : v);
  Pair fade = a;
  for (uint8_t& v : fade.left)
    v = static_cast<uint8_t>(v * 9 / 10);
  check(!IsCut(SignatureOf(a.L()), SignatureOf(b.L())), "the same shot, a thing moved: no cut", 0, 0);
  check(!IsCut(SignatureOf(a.L()), SignatureOf(fade.L())), "a picture 10 % darker (a fade): no cut", 0, 0);
  check(IsCut(SignatureOf(a.L()), SignatureOf(c.L())), "another shot: a cut", 1, 1);
}

static void TestPlanner()
{
  const PlannerParams p;
  {
    CPlanner planner;
    check(planner.Subtitles(0.0, true, 1) == p.floor, "nothing measured: the screen", planner.Subtitles(0.0, true, 1),
          p.floor);
  }
  {
    CPlanner planner;
    planner.AddMeasure(0.0, 0.010f, 0.012f, false);
    planner.AddMeasure(0.1, 0.008f, 0.012f, false);
    const float s = planner.Subtitles(0.2, true, 1);
    check(std::fabs(s - (0.008f + p.margin)) < 1e-6, "a subtitle appears in front of the nearest confirmed", s,
          0.008 + p.margin);
    // one measure nearer: not yet; a second one: it comes forward, smoothly
    planner.AddMeasure(0.3, 0.016f, 0.016f, false);
    float t1 = planner.Subtitles(0.32, true, 1);
    check(std::fabs(t1 - s) < 1e-6, "one measure nearer does not move it", t1, s);
    planner.AddMeasure(0.4, 0.015f, 0.016f, false);
    float before = planner.Subtitles(0.42, true, 1);
    float most = 0.0f;
    float last = before;
    int back = 0;
    for (int i = 1; i <= 48; ++i)
    {
      const float now = planner.Subtitles(0.42 + i / 24.0, true, 1);
      most = std::max(most, now - last);
      back += now < last;
      last = now;
    }
    check(back == 0, "pictures where it went back within the subtitle", back, 0);
    check(most <= p.speed / 24.0 + 1e-6, "it moves by no more than the speed allows in a picture", most,
          p.speed / 24.0);
    check(std::fabs(last - (0.015f + p.margin)) < 1e-6, "... to in front of the two measures", last, 0.015 + p.margin);
    // farther again within the same subtitle: it stays
    planner.AddMeasure(2.5, -0.01f, -0.01f, false);
    planner.AddMeasure(2.6, -0.01f, -0.01f, false);
    const float stays = planner.Subtitles(2.7, true, 1);
    check(std::fabs(stays - last) < 1e-6, "the thing goes back: the subtitle stays", stays, last);
    // the next subtitle, long after: where the measures put it now (the screen)
    planner.Subtitles(2.8, false, 1);
    planner.AddMeasure(4.0, -0.01f, -0.01f, false);
    planner.AddMeasure(4.1, -0.01f, -0.01f, false);
    const float next = planner.Subtitles(4.2, true, 2);
    check(next == p.floor, "a later subtitle over things behind the screen: at the screen", next, p.floor);
  }
  {
    CPlanner planner;
    planner.AddMeasure(0.0, 0.05f, 0.05f, false);
    planner.AddMeasure(0.1, 0.05f, 0.05f, false);
    const float s = planner.Subtitles(0.2, true, 1);
    check(s == p.comfort, "a thing nearer than the comfort limit: the subtitle at the limit", s, p.comfort);
    const float g = planner.Gui(0.2, false, s);
    check(g == p.comfort, "... and so is the interface", g, p.comfort);
  }
  {
    // hysteresis: the next subtitle 0.2 s after, its depth 0.001 off: the same depth; 0.004 off: its own
    CPlanner planner;
    planner.AddMeasure(0.0, 0.010f, 0.0f, false);
    planner.AddMeasure(0.1, 0.010f, 0.0f, false);
    const float a = planner.Subtitles(0.2, true, 1);
    planner.Subtitles(1.0, false, 1);
    planner.AddMeasure(1.05, 0.011f, 0.0f, false);
    planner.AddMeasure(1.10, 0.011f, 0.0f, false);
    // (what was measured in the last second still counts: 0.011 is the nearest)
    const float b = planner.Subtitles(1.2, true, 2);
    check(b == a, "the next subtitle soon after, 0.001 nearer: the depth of the one before", b, a);
    planner.Subtitles(2.0, false, 2);
    planner.AddMeasure(2.05, 0.015f, 0.0f, false);
    planner.AddMeasure(2.10, 0.015f, 0.0f, false);
    const float c = planner.Subtitles(2.2, true, 3);
    check(std::fabs(c - (0.015f + p.margin)) < 1e-6, "the next one, 0.004 nearer: its own depth", c,
          0.015 + p.margin);
    // replaced while shown (no gap), by one over things nearer: its own depth at once, not a slide
    planner.AddMeasure(2.30, 0.019f, 0.0f, false);
    planner.AddMeasure(2.40, 0.019f, 0.0f, false);
    const float d = planner.Subtitles(2.5, true, 4);
    check(std::fabs(d - std::min(0.019f + p.margin, p.comfort)) < 1e-6, "a subtitle that replaces another: its own",
          d, std::min(0.019f + p.margin, p.comfort));
  }
  {
    // a cut from a shot with a near thing to one where all is behind the screen
    CPlanner planner;
    planner.AddMeasure(0.0, 0.015f, 0.015f, false);
    planner.AddMeasure(0.1, 0.015f, 0.015f, false);
    planner.AddMeasure(0.2, -0.01f, -0.01f, true);
    const float a = planner.Subtitles(0.25, true, 1);
    check(std::fabs(a - (0.015f + p.margin)) < 1e-6, "one measure after a cut: the shot before still counts", a,
          0.015 + p.margin);
    planner.Subtitles(0.3, false, 1);
    planner.AddMeasure(0.3, -0.01f, -0.01f, false);
    const float b = planner.Subtitles(1.3, true, 2);
    check(b == p.floor, "two measures after it: the shot before is forgotten", b, p.floor);
  }
  {
    // the interface: its depth as it opens, kept while open, never behind the subtitles
    CPlanner planner;
    planner.AddMeasure(0.0, 0.0f, 0.012f, false);
    planner.AddMeasure(0.1, 0.0f, 0.012f, false);
    const float closed = planner.Gui(0.15, false, std::nullopt);
    const float open = planner.Gui(0.2, true, std::nullopt);
    check(std::fabs(open - (0.012f + p.margin)) < 1e-6, "the interface opens in front of the nearest of the picture",
          open, 0.012 + p.margin);
    check(closed == open, "... at the depth it was given the frame before it opened", closed, open);
    planner.AddMeasure(0.3, 0.0f, -0.01f, false);
    planner.AddMeasure(0.4, 0.0f, -0.01f, false);
    const float kept = planner.Gui(0.5, true, std::nullopt);
    check(kept == open, "the picture goes back: the open interface stays", kept, open);
    const float front = planner.Gui(0.6, true, 0.018f);
    check(front == 0.018f, "subtitles nearer than it: it comes in front of them", front, 0.018f);
    const float stays = planner.Gui(0.7, true, std::nullopt);
    check(stays == 0.018f, "... and stays there when they go", stays, 0.018f);
    const float again = planner.Gui(5.0, false, std::nullopt);
    check(again == p.floor, "closed, and the picture behind the screen: at the screen next time", again, p.floor);
  }
}

// ------------------------------------------------------------------------------------------------- the film
struct FilmResult
{
  int conflicts{0}; //!< pictures with a subtitle drawn behind the thing, after the reaction time
  int shown{0};
  int backwards{0};
  float worstStep{0.0f};
  double reaction{0.0}; //!< from the thing's coming into the area to the last picture in conflict
};

// 24 pictures a second; each second picture is copied (as CStereoDepthMeter does at 24p: one in 75 ms at most, of a
// new picture) and its measure is there two pictures later; a subtitle visible as said. The thing is at (thingX, d)
// at each time; the subtitle is drawn by the shift SubtitleDepth() gives it.
template<typename D, typename X, typename V>
static FilmResult Film(double seconds, D thingD, X thingX, V visibleAt, double enters)
{
  CPlanner planner;
  CRenderer renderer;
  FilmResult r;
  struct Ready
  {
    int at;
    double t;
    std::optional<float> area;
    std::optional<float> whole;
    bool cut;
  };
  std::vector<Ready> pending;
  Signature before;
  MatchParams whole;
  whole.step = 16;
  float last = 0.0f;
  uint64_t lastEvent = 0;
  const int pictures = static_cast<int>(seconds * 24);
  for (int i = 0; i < pictures; ++i)
  {
    const double t = i / 24.0;
    const float d = thingD(t);
    const float x = thingX(t);
    if (i % 2 == 0)
    {
      const Pair p = Make(6.0f, {{x, 185, x + 160, 255, d, 51}}, 100 + i);
      const Signature s = SignatureOf(p.L());
      pending.push_back({i + 2, t, MeasureNearest(p.L(), p.R(), SUBS).nearest,
                         MeasureNearest(p.L(), p.R(), {0.02f, 0.02f, 0.98f, 0.98f}, whole).nearest,
                         IsCut(before, s)});
      before = s;
    }
    for (auto it = pending.begin(); it != pending.end();)
    {
      if (it->at <= i)
      {
        planner.AddMeasure(it->t, it->area, it->whole, it->cut);
        it = pending.erase(it);
      }
      else
        ++it;
    }
    const uint64_t event = visibleAt(t);
    const bool visible = event != 0;
    const float depth = planner.Subtitles(t, visible, event);
#ifdef OLD_RULE
    (void)depth;
    renderer.m_measuredDepth.reset();
#else
    renderer.m_measuredDepth = depth;
#endif
    if (!visible)
      continue;
    ++r.shown;
    CServiceBroker::GetWinSystem()->GetGfxContext().view = RenderStereoView::LEFT;
    const float left = renderer.SubtitleDepth(COverlay{});
    CServiceBroker::GetWinSystem()->GetGfxContext().view = RenderStereoView::RIGHT;
    const float right = renderer.SubtitleDepth(COverlay{});
    // the disparity it is drawn with, over the width: its depth
    const float drawn = (left - right) / renderer.m_rd.Width();
    if (x > 0.0f && drawn < Z(d))
    {
      r.reaction = std::max(r.reaction, t + 1.0 / 24.0 - enters);
      if (t >= enters + 0.6)
      {
        ++r.conflicts;
        if (r.conflicts <= 3)
          std::printf("picture %d (%.2f s): the subtitle at %.5f, the thing at %.5f in front of it\n", i, t, drawn,
                      Z(d));
      }
    }
    if (event == lastEvent)
    {
      r.worstStep = std::max(r.worstStep, std::fabs(drawn - last));
      r.backwards += drawn < last - 1e-7f;
    }
    last = drawn;
    lastEvent = event;
  }
  return r;
}

static void TestFilm()
{
  const double step = PlannerParams{}.speed / 24.0 + 1e-6;
  // a thing in the area from 0.2 s, coming nearer from -4 to -8 pixels (0.83 % to 1.67 %) between 1.5 s and 3.5 s;
  // two subtitles, 0.5 s to 2.6 s and 2.8 s to 4.8 s: each appears in front of it and stays in front of it
  {
    const FilmResult r = Film(
        5.0,
        [](double t) {
          return t < 1.5 ? -4.0f : t > 3.5 ? -8.0f : static_cast<float>(-4.0 - 4.0 * (t - 1.5) / 2.0);
        },
        [](double t) { return static_cast<float>(t < 0.2 ? -200.0 : 160.0); },
        [](double t) -> uint64_t { return t >= 0.5 && t < 2.6 ? 1 : t >= 2.8 && t < 4.8 ? 2 : 0; }, 0.2);
    check(r.reaction == 0.0, "a thing that comes nearer: time with the subtitle behind it (s)", r.reaction, 0.0);
    check(r.shown > 90, "pictures with a subtitle", r.shown, 90);
    check(r.backwards == 0, "pictures where a subtitle went back while shown", r.backwards, 0);
    check(r.worstStep <= step, "the largest move of a subtitle between two pictures", r.worstStep, step);
  }
  // a thing that comes into the area at once (-8 pixels) while a subtitle is shown: the subtitle comes in front of it
  // within the reaction time (two measures, then the move at the speed allowed), smoothly
  {
    const FilmResult r = Film(
        3.0, [](double) { return -8.0f; }, [](double t) { return static_cast<float>(t < 1.0 ? -200.0 : 160.0); },
        [](double t) -> uint64_t { return t >= 0.5 && t < 2.9 ? 1 : 0; }, 1.0);
    check(r.conflicts == 0, "a thing that comes at once: pictures behind it after 0.6 s", r.conflicts, 0);
    check(r.reaction > 0.0 && r.reaction <= 0.6, "... the time it was behind it (s)", r.reaction, 0.6);
    check(r.backwards == 0, "... pictures where it went back", r.backwards, 0);
    check(r.worstStep <= step, "... the largest move between two pictures", r.worstStep, step);
  }
}

int main()
{
  TestMeasure();
  TestCut();
  TestPlanner();
  TestFilm();
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
