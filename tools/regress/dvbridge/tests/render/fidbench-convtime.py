# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c after fidbench-mapping.py (rg_edit): with FB_TIMES=<n>, each picture for output
# as HDR10 (or mapped, as FB_SDR and FB_PROJ say) is drawn n times more after the first, each followed by glFinish(),
# and the median and the least of those times are printed ("picture <n> tag <tag>: drawn again <n> times: median <ms>
# ms, least <ms> ms"): the GPU time of the conversion at the level of FB_STEP, on the software renderer.
DRAW_AGAIN = r'''  if (ok && envi("FB_TIMES", 0) > 0)
  {
    const int times = envi("FB_TIMES", 0);
    double ms[64];
    int n = 0;
    for (; n < times && n < 64; ++n)
    {
      struct timespec a, z;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &a);
      if (!dvbridge_render_hdr10(g->renderer, &b->frame, meta, bytes, pts, elPts, whole, W, H))
        break;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &z);
      ms[n] = (z.tv_sec - a.tv_sec) * 1e3 + (z.tv_nsec - a.tv_nsec) / 1e6;
    }
    for (int i = 1; i < n; ++i)
      for (int j = i; j > 0 && ms[j] < ms[j - 1]; --j)
      {
        const double s = ms[j];
        ms[j] = ms[j - 1];
        ms[j - 1] = s;
      }
    if (n > 0)
      printf("picture %u tag %s: drawn again %d times: median %.1f ms, least %.1f ms\n", number, tag, n, ms[n / 2],
             ms[0]);
  }
'''
TEX = '  pl_tex tex = ok ? dvbridge_render_hdr10_texture(g->renderer) : NULL;\n'
PAIRS = [
    ('#include <string.h>\n', '#include <string.h>\n#include <time.h>\n'),
    (TEX, DRAW_AGAIN + TEX),
]
