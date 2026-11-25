// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// subtitle_bars_test.cpp: subtitles in the black bars (xbmc/cores/VideoPlayer/VideoRenderers/SubtitleBars.h of
// the Kodi tree): what a measured picture says of its bar, the bar of a video over time (a dark scene, a fade,
// an IMAX scene, the active area of Dolby Vision), the room under the picture on the screen, and the sizes of
// text that the help of the setting gives (38 fits a 2.39:1 film on a 16:9 screen, 34 a 2.35:1 one). Prints a
// line per failed check and PASSED or FAILED (n) last.
#include "cores/VideoPlayer/VideoRenderers/SubtitleBars.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace KODI::SUBTITLES::BARS;

static int failed = 0;
static void expect(bool ok, const char* what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// a measurement as the GPU writes it: rows of BANDS samples (RGBA, the luma in red), from the top, each
// row two rows of the picture; value(row, band) in 0..255
template<typename F>
static std::vector<uint8_t> Picture(int rows, F value)
{
  std::vector<uint8_t> v(static_cast<size_t>(rows) * BANDS * 4, 255);
  for (int r = 0; r < rows; ++r)
    for (int b = 0; b < BANDS; ++b)
      v[(static_cast<size_t>(r) * BANDS + b) * 4] = static_cast<uint8_t>(value(r, b));
  return v;
}

// a 3840x2160 film of 2.39:1 in a 16:9 picture: 276 rows of bars at the top and the bottom (138 rows of the
// measurement); limited range, black 16
constexpr int ROWS = 1080, BAR = 138;
static Measure Letterbox(int inside, int bars = 16)
{
  const auto pic = Picture(ROWS, [&](int r, int) { return r < BAR || r >= ROWS - BAR ? bars : inside; });
  return Analyse(pic.data(), ROWS, 2, 2160, 16.0f / 255.0f);
}

int main()
{
  // ---- Analyse
  {
    const Measure m = Letterbox(120);
    expect(m.black == 276 && m.across == 276 && m.edge, "a bright scene: the bar and its edge");
    const Measure n = Letterbox(120, 19); // (3 codes of noise in the bars of a stream)
    expect(n.black == 276 && n.edge, "the noise of a black bar is black");
    const Measure d = Letterbox(24); // a dark scene: picture above the black, not lit
    expect(!d.edge, "a dark scene does not show the edge");
    // a dark scene with a lamp: the picture lit in two bands only near the top
    const auto lamp = Picture(ROWS, [&](int r, int b) {
      if (r < BAR || r >= ROWS - BAR)
        return 16;
      return (r < 300 && b < 2) ? 200 : 24;
    });
    const Measure l = Analyse(lamp.data(), ROWS, 2, 2160, 16.0f / 255.0f);
    expect(!l.edge && l.black == 276, "a dark scene with a lamp: its black rows, no edge");
    // a line burnt into the bar (four bands lit): not picture across it
    const auto burnt = Picture(ROWS, [&](int r, int b) {
      if (r >= ROWS - 40 && r < ROWS - 20 && b >= 6 && b < 10)
        return 230;
      return r < BAR || r >= ROWS - BAR ? 16 : 120;
    });
    const Measure bu = Analyse(burnt.data(), ROWS, 2, 2160, 16.0f / 255.0f);
    expect(bu.across == 276 && bu.black == 40 && !bu.edge, "a line burnt into the bar");
    // full range (black 0): an IPT picture of profile 5
    const auto full = Picture(ROWS, [&](int r, int) { return r >= ROWS - BAR ? 0 : 100; });
    const Measure f = Analyse(full.data(), ROWS, 2, 2160, 0.0f);
    expect(f.black == 276 && f.edge, "full range");
    // no bar
    const Measure z = Analyse(Picture(ROWS, [](int, int) { return 120; }).data(), ROWS, 2, 2160, 16.0f / 255.0f);
    expect(z.black == 0 && !z.edge, "no bar");
  }

  // ---- the bar over time
  {
    CBarsHysteresis h;
    double t = 0;
    // the film starts in black (a fade), then dark: nothing is taken
    for (int i = 0; i < 20; ++i, t += 0.5)
      h.Measured(Letterbox(16), t);
    for (int i = 0; i < 20; ++i, t += 0.5)
      h.Measured(Letterbox(24), t);
    expect(h.Now().rows == 0, "black and dark pictures take no bar");
    // bright: taken at the third measurement that sees the edge, two seconds after the first
    h.Measured(Letterbox(120), t);
    h.Measured(Letterbox(120), t += 0.5);
    h.Measured(Letterbox(120), t += 0.5);
    expect(h.Now().rows == 0, "three measurements within a second: not yet");
    h.Measured(Letterbox(120), t += 1.0);
    expect(h.Now().rows == 276 && h.Now().how == HOW_MEASURED, "taken after two seconds");
    // a dark scene and a fade later: nothing moves
    for (int i = 0; i < 40; ++i, t += 0.5)
      h.Measured(Letterbox(i % 2 ? 16 : 24), t);
    expect(h.Now().rows == 276, "dark scenes and fades do not move it");
    // an IMAX scene opens to 1.90:1 (69 rows of bars): at once
    const auto imax = Picture(ROWS, [&](int r, int) { return r < 35 || r >= ROWS - 35 ? 16 : 120; });
    h.Measured(Analyse(imax.data(), ROWS, 2, 2160, 16.0f / 255.0f), t += 0.5);
    expect(h.Now().rows == 70 && h.Now().how == HOW_SHRUNK, "an IMAX scene: the bar that is left, at once");
    // back to 2.39:1: taken again only after it is seen as before
    h.Measured(Letterbox(120), t += 0.5);
    expect(h.Now().rows == 70, "back to 2.39: not at once");
    h.Measured(Letterbox(120), t += 1.0);
    h.Measured(Letterbox(120), t += 1.0);
    expect(h.Now().rows == 276, "back to 2.39 after two seconds");
    // a picture across the whole bar (16:9 credits): no bar
    h.Measured(Letterbox(120, 120), t += 0.5);
    expect(h.Now().rows == 0, "a 16:9 scene: no bar");
    // another size: a new video
    h.Measured(Letterbox(120), t += 0.5);
    Measure other = Letterbox(120);
    other.height = 1080;
    h.Measured(other, t += 0.5);
    expect(h.Now().height == 1080 && h.Now().rows == 0, "another size starts again");
    // a bar under the share of the picture that makes one: none
    CBarsHysteresis s;
    Measure thin = Letterbox(120);
    thin.black = thin.across = 20;
    for (int i = 0; i < 4; ++i)
      s.Measured(thin, i * 1.0);
    expect(s.Now().rows == 0, "a bar of 20 rows of 2160 is none");
  }

  // ---- the active area of Dolby Vision
  {
    CBarsHysteresis h;
    h.L5(2160, 276);
    expect(h.Now().rows == 276 && h.Now().how == HOW_L5, "level 5: at once");
    h.L5(2160, 0);
    for (int i = 0; i < 3; ++i)
      h.Measured(Letterbox(24), i * 1.0);
    expect(h.Now().rows == 276, "a level 5 of the whole picture says nothing: the bar stays");
    CBarsHysteresis z;
    z.L5(2160, 0);
    for (int i = 0; i < 3; ++i)
      z.Measured(Letterbox(120), i * 1.0);
    expect(z.Now().rows == 276 && z.Now().how == HOW_MEASURED, "a level 5 of zeros: measured");
    // a level 5 that a measurement contradicts is not taken again while it lasts
    CBarsHysteresis c;
    c.L5(2160, 276);
    c.Measured(Letterbox(120, 120), 0.0);
    expect(c.Now().rows == 0, "picture across the bar of level 5");
    c.L5(2160, 276);
    expect(c.Now().rows == 0, "the same level 5 is not taken again");
    c.L5(2160, 280);
    expect(c.Now().rows == 280, "another value of level 5 is");
    // the measurement does not take more than level 5 says
    CBarsHysteresis g;
    g.L5(2160, 140);
    for (int i = 0; i < 4; ++i)
      g.Measured(Letterbox(120), i * 1.0);
    expect(g.Now().rows == 140, "the measurement does not grow a bar of level 5");
  }

  // ---- text: the sizes of the help of the setting (a 1080 frame; libass: two lines, line spacing 12,
  // border 25, no shadow, as Kodi's defaults)
  {
    const float frame = 1080.0f;
    const float bar239 = (frame - 1920.0f / 2.39f) / 2.0f, bar235 = (frame - 1920.0f / 2.35f) / 2.0f;
    const auto need = [&](int size) {
      return TextOf(size, 12, 25, 15, Border::OUTLINE_NO_SHADOW, frame).twoLines + 2 * GapOf(frame);
    };
    expect(std::fabs(TextOf(42, 12, 25, 15, Border::OUTLINE_NO_SHADOW, frame).twoLines - 145.65f) < 0.01f,
           "two lines at Kodi's default size: 145.65 px");
    expect(need(38) <= bar239 && need(40) > bar239, "38 fits the bar of 2.39:1, 40 does not");
    expect(need(34) <= bar235 && need(36) > bar235, "34 fits the bar of 2.35:1, 36 does not");
    const TextSize s = TextOf(42, 12, 25, 15, Border::OUTLINE, frame);
    expect(std::fabs(s.below - (3.75f + 2.25f)) < 0.01f, "outline and shadow under the lower line");
  }

  // ---- the room under the picture
  {
    const Rect view{0, 0, 1920, 1080};
    // a 3840x2160 picture with 276 rows of bars, drawn on the whole screen
    Bar bar{2160, 276, HOW_MEASURED};
    Place p = PlaceIn({0, 0, 3840, 2160}, {0, 0, 1920, 1080}, view, bar, 120, 2);
    expect(p.inBar && std::fabs(p.pictureBottom - 942) < 0.01f && std::fabs(p.room - 138) < 0.01f,
           "the bar of the picture, on the screen");
    expect(std::fabs(p.blockBottom - (942 + (138 + 120) / 2.0f)) < 0.01f, "two lines centred in it");
    expect(!PlaceIn({0, 0, 3840, 2160}, {0, 0, 1920, 1080}, view, bar, 140, 2).inBar, "too small");
    // a 3840x1600 encode, drawn smaller than the screen: Kodi's own bar
    p = PlaceIn({0, 0, 3840, 1600}, {0, 140, 1920, 940}, view, Bar{1600, 0, nullptr}, 120, 2);
    expect(p.inBar && std::fabs(p.room - 140) < 0.01f, "Kodi's own bar under a picture drawn smaller");
    // a zoom that draws the bars beyond the screen: no room
    p = PlaceIn({0, 0, 3840, 2160}, {-344, -194, 2264, 1274}, view, bar, 120, 2);
    expect(!p.inBar && p.why == WHY_NO_ROOM, "a zoom: the picture reaches the bottom");
    // a source that is a part of the picture (Kodi's crop): its rows
    p = PlaceIn({0, 100, 3840, 2060}, {0, 0, 1920, 1080}, view, bar, 120, 2);
    expect(std::fabs(p.pictureBottom - (1884.0f - 100.0f) * 1080.0f / 1960.0f) < 0.01f, "a source cut");
    // picture subtitles: one line and two lines on the same line
    p = PlaceIn({0, 0, 3840, 2160}, {0, 0, 1920, 1080}, view, bar, 0, 2);
    const float two = GroupBottom(p, 100, 0, 2);
    const float one = GroupBottom(p, 50, 100, 2);
    expect(two > 0 && std::fabs(two - one) < 0.01f, "one line sits where the lower of two does");
    expect(GroupBottom(p, 136, 0, 2) < 0, "a group taller than the room stays");
  }

  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
