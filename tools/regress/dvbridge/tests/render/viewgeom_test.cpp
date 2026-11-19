// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: DVBRIDGE::ViewGeometry() (where native Dolby Vision draws a
// picture whose view the user changed), with the rectangles that Kodi's CBaseRenderer::ManageRenderArea() makes,
// values worked out by hand. (What the bridge's serializer takes of a geometry with a view: margins_test.c.)
#include "DVViewGeometry.h"

#include <cmath>
#include <cstdio>

static int g_fail;

static void Check(bool ok, const char* what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  g_fail += !ok;
}

static bool Near(float a, double b)
{
  return std::fabs(static_cast<double>(a) - b) < 1e-3;
}

struct Want
{
  bool drawn;
  int x, y, w, h;
  double v0, v1, v2, v3;
};

static void Case(const char* what, int w, int h, double sx1, double sy1, double sx2, double sy2, double dx1,
                 double dy1, double dx2, double dy2, const Want& want)
{
  dvbridge_geometry g{};
  const bool ok = DVBRIDGE::ViewGeometry(w, h, sx1, sy1, sx2, sy2, dx1, dy1, dx2, dy2, g);
  bool good = ok == want.drawn;
  if (ok && want.drawn)
    good = g.source_width == w && g.source_height == h && g.x == want.x && g.y == want.y && g.width == want.w &&
           g.height == want.h && Near(g.view[0], want.v0) && Near(g.view[1], want.v1) && Near(g.view[2], want.v2) &&
           Near(g.view[3], want.v3);
  char text[512];
  std::snprintf(text, sizeof text, "%s: %s, place %d %d %d %d, view %.3f %.3f %.3f %.3f", what, ok ? "drawn" : "not drawn",
                g.x, g.y, g.width, g.height, static_cast<double>(g.view[0]), static_cast<double>(g.view[1]),
                static_cast<double>(g.view[2]), static_cast<double>(g.view[3]));
  Check(good, text);
}

int main()
{
  // zoom 4/3 of a 4K picture: Kodi's destination (-640, -360) - (4480, 2520) cut to the screen, the source with it
  Case("zoom of 4K", 3840, 2160, 480, 270, 3360, 1890, 0, 0, 3840, 2160, {true, 0, 0, 3840, 2160, 480, 270, 3360, 1890});
  // a vertical shift that takes the picture 540 rows up out of the raster
  Case("4K shifted up", 3840, 2160, 0, 540, 3840, 2160, 0, 0, 3840, 1620, {true, 0, 0, 3840, 1620, 0, 540, 3840, 2160});
  // edges between pixels: x from 127.3 (to the even column 128), y from 10.6 (to 11), right at 3712.9 (to 3712),
  // bottom at 2000.2 (to 2000); a 1920x1080 source over (127.3, 10.6) - (3712.9, 2000.2): the source moved with them
  {
    const double kx = 1920.0 / (3712.9 - 127.3), ky = 1080.0 / (2000.2 - 10.6);
    Case("edges between pixels", 1920, 1080, 0, 0, 1920, 1080, 127.3, 10.6, 3712.9, 2000.2,
         {true, 128, 11, 3584, 1989, (128 - 127.3) * kx, (11 - 10.6) * ky, (3712 - 127.3) * kx, (2000 - 10.6) * ky});
  }
  // pixels that are not square: 1440x1080 shown at 16:9 over the raster
  Case("1440x1080 stretched", 1440, 1080, 0, 0, 1440, 1080, 0, 0, 3840, 2160, {true, 0, 0, 3840, 2160, 0, 0, 1440, 1080});
  // a zoom of 1080p by 1.2: its middle (160, 90) - (1760, 990) over the raster
  Case("zoom of 1080p", 1920, 1080, 160, 90, 1760, 990, 0, 0, 3840, 2160, {true, 0, 0, 3840, 2160, 160, 90, 1760, 990});
  // nothing of it on the raster, or not a rectangle
  Case("shifted out of the raster", 3840, 2160, 0, 2159.5, 3840, 2160, 0, 0, 3840, 0.5, {false, 0, 0, 0, 0, 0, 0, 0, 0});
  Case("an empty source", 3840, 2160, 10, 10, 10, 20, 0, 0, 3840, 2160, {false, 0, 0, 0, 0, 0, 0, 0, 0});
  // the same geometry: the view counts
  dvbridge_geometry a{1920, 1080, 0, 0, 3840, 2160, {0, 0, 0, 0}}, b = a;
  Check(DVBRIDGE::SameGeometry(a, b), "the same geometry is the same");
  b.view[1] = 1.0f;
  Check(!DVBRIDGE::SameGeometry(a, b), "another view is another geometry");
  std::printf("%s: %d failed\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail != 0;
}
