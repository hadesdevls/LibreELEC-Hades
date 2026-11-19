#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""The GPU time of each pass (src/passtime_check.h) wired into a copy of a bridge test, by exact edits (each text
found exactly once, or it stops):
  passtime_wire.py dvrender <src/dvrender_oracle.c> <out .c>   every render of each way timed
  passtime_wire.py measured <src/measured_oracle.c> <out .c>   every render and pack timed
passtime_check.h's folder has to be on the include path (CPATH)."""
import re
import sys

kind, src, dst = sys.argv[1:4]
t = open(src, encoding='utf-8').read()


def once(old, new, what):
    global t
    if t.count(old) != 1:
        sys.exit('passtime_wire: %s: the text is there %d times' % (what, t.count(old)))
    t = t.replace(old, new)


m = re.search(r'^static void Check\(int ok, const char\* what\)\n\{\n.*?^\}\n', t, re.M | re.S)
if not m:
    sys.exit('passtime_wire: no Check()')
t = t[:m.end()] + '\n#include "passtime_check.h" // (passtime_wire.py)\n' + t[m.end():]
if kind == 'dvrender':
    once("      .egl_display = dpy, .egl_context = ctx, .allow_software = true));\n  Check(g.gl != NULL",
         "      .egl_display = dpy, .egl_context = ctx, .allow_software = true,\n"
         "      .gles_timer_queries = PassTimesOn()));\n  Check(g.gl != NULL", 'gl')
    once("  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);\n",
         "  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);\n  PassTimesStart(g.renderer);\n", 'renderer')
    once("  if (dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry))\n  {\n    early[0]",
         "  if (TIMED_RENDER(dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry)))\n  {\n    early[0]",
         'composed')
    once("    direct = dvbridge_render_packed(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry, g->scanout, true) &&\n",
         "    direct = TIMED_RENDER(dvbridge_render_packed(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry,\n"
         "                                                 g->scanout, true)) &&\n", 'direct')
    once('  Check(g_bits == 0, "every metadata bit reads back the same from the two ways");\n',
         '  Check(g_bits == 0, "every metadata bit reads back the same from the two ways");\n'
         '  PassTimesSummary(g.gl->gpu, g.renderer, 0, 1);\n', 'summary')
elif kind == 'measured':
    once(".egl_display = dpy, .egl_context = ctx, .allow_software = true));\n  if (!g.gl)",
         ".egl_display = dpy, .egl_context = ctx, .allow_software = true,\n"
         "                                                 .gles_timer_queries = PassTimesOn()));\n  if (!g.gl)", 'gl')
    once("  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);\n",
         "  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);\n  PassTimesStart(g.renderer);\n", 'renderer')
    once("  int ok = dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, g_pts, NAN, full);\n",
         "  int ok = TIMED_RENDER(dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, g_pts, NAN, full));\n", 'render')
    once("  ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE);\n",
         "  ok = ok && TIMED_RENDER(dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE));\n"
         "  g_packs += ok;\n", 'pack')
    once('  printf("libplacebo: %ld warnings and errors\\n", g_messages);\n',
         '  // (an HDR10 picture: the source transport takes the decoder\'s planes, libplacebo draws none of them)\n'
         '  PassTimesSummary(g.gl->gpu, g.renderer, g_packs, 0);\n'
         '  printf("libplacebo: %ld warnings and errors\\n", g_messages);\n', 'summary')
else:
    sys.exit('dvrender or measured')
open(dst, 'w', encoding='utf-8').write(t)
print('%s: the GPU time of each pass wired in (%s)' % (dst, kind))
