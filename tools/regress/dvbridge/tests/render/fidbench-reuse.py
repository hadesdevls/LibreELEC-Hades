# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c after fidbench-ipt.py (rg_edit): a frame with the GUI over it that draws the picture
# of the frame before it again, as Kodi draws one in a pause with the OSD over it (dvbridge_render_reused()).
#  - The bridge is told when the GUI is composited into its picture (dvbridge_render_texture_drawn(), as
#    CDVBridgeGLES::PQTextureDrawn() tells it in Kodi).
#  - FB_NODIRECT=1: the direct way is not drawn (a frame with nothing over it between two pictures with the GUI over
#    them would leave nothing to take).
#  - FB_REUSE=0 turns the reuse off (dvbridge_render_allow_reuse(), advancedsettings.xml dvreuse);
#    FB_LL=<peak> draws in Low-Latency to a display of that peak (BT.2020, the BT.2390 curve).
#  - FB_AGAIN=<n>: after the composed way of a picture, the same picture is drawn n more times with the GUI over it
#    (render, composite, pack), each followed by glFinish(). The last of them is written (<tag>-p<N>-again.rgba), and a
#    line says how many took the picture of the frame before and their median time ("picture <n> tag <tag>: drawn
#    again <n> times, <r> reused: median <ms> ms (render <ms> ms)").
AGAIN = r'''  const int residual = dvbridge_render_residual_drawn(g->renderer);
  if (rgb && envi("FB_AGAIN", 0) > 0)
  {
    double ms[64], mr[64];
    int n = 0, reused = 0;
    for (; n < envi("FB_AGAIN", 0) && n < 64; ++n)
    {
      struct timespec t0, t1, t2;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &t0);
#ifdef DVBRIDGE_RENDER_DRAWN
      if (gui && envi("FB_TILES", 1))
        dvbridge_render_set_drawn(g->renderer, g_gui, W, H);
#endif
      if (!dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry))
        break;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &t1);
      reused += dvbridge_render_reused(g->renderer);
      if (gui)
      {
        pl_tex tex = dvbridge_render_texture(g->renderer);
        const GLuint id = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
        if (id)
          dvbridge_render_texture_drawn(g->renderer);
        if (!id || !GuiComposite(id))
          break;
      }
      if (!dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, gui ? DVBRIDGE_COVER_MEASURE : DVBRIDGE_COVER_NONE))
        break;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &t2);
      mr[n] = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
      ms[n] = (t2.tv_sec - t0.tv_sec) * 1e3 + (t2.tv_nsec - t0.tv_nsec) / 1e6;
    }
    if (n < envi("FB_AGAIN", 0))
    {
      printf("FAIL picture %u: drawn again %d times of %d\n", number, n, envi("FB_AGAIN", 0));
      ++g_failures;
    }
    for (int i = 1; i < n; ++i)
      for (int j = i; j > 0 && ms[j] < ms[j - 1]; --j)
      {
        double s = ms[j];
        ms[j] = ms[j - 1];
        ms[j - 1] = s;
        s = mr[j];
        mr[j] = mr[j - 1];
        mr[j - 1] = s;
      }
    if (n > 0)
    {
      printf("picture %u tag %s: drawn again %d times, %d reused: median %.1f ms (render %.1f ms)\n", number, tag, n,
             reused, ms[n / 2], mr[n / 2]);
      if (ReadRgba8(g, g->bytes))
        WriteFile(dir, tag, number, "again.rgba", g->bytes, (size_t)W * H * 4);
      else
        ++g_failures;
    }
  }
'''
PAIRS = [
    ('  if (g->subgroups || source)\n', '  if ((g->subgroups || source) && !envi("FB_NODIRECT", 0))\n'),
    ('#include <string.h>\n', '#include <string.h>\n#include <time.h>\n'),
    ('      if (!id || !GuiComposite(id))\n',
     '      if (id)\n        dvbridge_render_texture_drawn(g->renderer);\n      if (!id || !GuiComposite(id))\n'),
    ('  const int residual = dvbridge_render_residual_drawn(g->renderer);\n', AGAIN),
    ('  printf("compute forms asked: %d\\n", envi("FB_COMPUTE", 0) != 0);\n',
     '  printf("compute forms asked: %d\\n", envi("FB_COMPUTE", 0) != 0);\n'
     '  dvbridge_render_allow_reuse(g.renderer, envi("FB_REUSE", 1) != 0);\n'
     '  printf("reuse allowed: %d\\n", envi("FB_REUSE", 1) != 0);\n'
     '  if (envi("FB_LL", 0) > 0)\n'
     '  {\n'
     '    struct dvbridge_hdr10_mapping ll = {0};\n'
     '    ll.enabled = true;\n'
     '    ll.peak = (float)envi("FB_LL", 0);\n'
     '    ll.gamut = DVBRIDGE_HDR10_GAMUT_BT2020;\n'
     '    ll.curve = DVBRIDGE_HDR10_CURVE_BT2390;\n'
     '    if (!dvbridge_render_set_low_latency(g.renderer, &ll))\n'
     '      return 1;\n'
     '    printf("Low-Latency to %d cd/m2\\n", envi("FB_LL", 0));\n'
     '  }\n'),
]
