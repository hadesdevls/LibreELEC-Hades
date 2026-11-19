// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the level 5 active area on the raster through the part of the
// picture drawn (dvbridge_dv_geometry_view(), dvbridge_dv_margins()), values worked out by hand. A picture that is not
// 3840x2160 has its offsets read both ways (of the picture, of a 4K master: dvbridge_dv_margins()) and each side takes
// the smaller margin: the expected values below are that.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_metadata.h"

static int g_fail;

static void Case(const char* what, int w, int h, int x, int y, int dw, int dh, const float* view, unsigned l,
                 unsigned r, unsigned t, unsigned b, int ok_expected, const unsigned want[4])
{
  static struct dvbridge_dv d;
  memset(&d, 0, sizeof(d));
  unsigned v[4] = {9999, 9999, 9999, 9999};
  const int ok = dvbridge_dv_geometry_view(&d, w, h, x, y, dw, dh, view) && dvbridge_dv_margins(&d, l, r, t, b, v);
  const int good = ok == ok_expected && (!ok || !memcmp(v, want, sizeof(v)));
  printf("%s %s: %s, margins %u %u %u %u %s\n", good ? "ok:  " : "FAIL:", what, ok ? "taken" : "refused", v[0], v[1],
         v[2], v[3], ok ? "" : (d.refused ? d.refused : ""));
  g_fail += !good;
}

int main(void)
{
  // the whole picture, as before: 1080p with bars of 140 rows (as offsets of the picture 280 rows on the raster, as
  // a master's 140: the smaller)
  Case("1080p on the raster, no view", 1920, 1080, 0, 0, 3840, 2160, NULL, 0, 0, 140, 140, 1, (unsigned[]){0, 0, 140, 140});
  Case("the same with the whole picture as its view", 1920, 1080, 0, 0, 3840, 2160, (float[]){0, 0, 1920, 1080}, 0, 0,
       140, 140, 1, (unsigned[]){0, 0, 140, 140});
  Case("4K at its place, no view", 3840, 2160, 0, 0, 3840, 2160, NULL, 0, 0, 276, 276, 1, (unsigned[]){0, 0, 276, 276});
  Case("no view at another shape: refused, as before", 1440, 1080, 0, 0, 3840, 2160, NULL, 0, 0, 0, 0, 0,
       (unsigned[]){0, 0, 0, 0});
  // zoom of a 4K picture: its middle 2880x1620 on the raster (x 4/3); the bars of 276 rows are 6 rows in the view
  Case("zoom of 4K, bars partly in the view", 3840, 2160, 0, 0, 3840, 2160, (float[]){480, 270, 3360, 1890}, 0, 0, 276,
       276, 1, (unsigned[]){0, 0, 8, 8});
  Case("zoom of 4K inside the active area", 3840, 2160, 0, 0, 3840, 2160, (float[]){480, 300, 3360, 1860}, 0, 0, 276,
       276, 1, (unsigned[]){0, 0, 0, 0});
  // a 4K picture shifted up out of the raster by 540 rows: rows 540..2159 drawn at 0..1619; its bottom bar of 276 rows
  // ends the picture's place at 1344, below which the raster is outside the place (540 rows)
  Case("4K shifted up out of the raster", 3840, 2160, 0, 0, 3840, 1620, (float[]){0, 540, 3840, 2160}, 0, 0, 276, 276,
       1, (unsigned[]){0, 0, 0, 816});
  // ... and down, at 540 (rows 0..1619 at 540..2159)
  Case("4K shifted down out of the raster", 3840, 2160, 0, 540, 3840, 1620, (float[]){0, 0, 3840, 1620}, 0, 0, 276, 276,
       1, (unsigned[]){0, 0, 816, 0});
  // zoom of 1080p, the middle 1440x810 (x 8/3), bars of 300 rows: as offsets of the picture (300 - 135) x 8/3 = 440, as
  // a master's (150 - 135) x 8/3 = 40
  Case("zoom of 1080p", 1920, 1080, 0, 0, 3840, 2160, (float[]){240, 135, 1680, 945}, 0, 0, 300, 300, 1,
       (unsigned[]){0, 0, 40, 40});
  // a picture of pixels that are not square: 1440x1080 shown at 16:9, a 3840x2160 master's pillarbox of 480 columns at
  // each side (as offsets of the picture 1280 on the raster; as a master's, 480 - 960 cropped: none)
  Case("1440x1080 stretched to the raster", 1440, 1080, 0, 0, 3840, 2160, (float[]){0, 0, 1440, 1080}, 480, 480, 0, 0,
       1, (unsigned[]){0, 0, 0, 0});
  // the view beyond the picture: refused
  Case("a view beyond the picture", 1920, 1080, 0, 0, 3840, 2160, (float[]){0, 0, 1921, 1080}, 0, 0, 0, 0, 0,
       (unsigned[]){0, 0, 0, 0});
  // a crop of the master (3840x1600, the bars cut off, the master's offsets of 280) zoomed: the master reading leaves
  // no bar (280 - 560 removed), whatever the view
  Case("zoom of a 3840x1600 crop with the master's offsets", 3840, 1600, 0, 0, 3840, 2160,
       (float[]){480, 200, 3360, 1400}, 0, 0, 280, 280, 1, (unsigned[]){0, 0, 0, 0});
  printf("%s: %d failed\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail != 0;
}
