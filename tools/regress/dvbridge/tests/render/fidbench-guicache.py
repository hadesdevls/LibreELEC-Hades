# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c (rg_edit): the GUI cache of the bridge as Kodi uses it. It is turned on as
# Kodi turns it on (dvbridge_render_use_gui_cache(); FB_GCACHE=0 leaves it off), the composite over the picture is
# given its key as Kodi gives it (dvbridge_render_set_drawn_key(); FB_GKEY, default 1), FB_GCHECK=1 asks the bridge
# to check every tile it copied against the tile packed again (a tile that differs is a failure), and what the cache
# did with each frame is printed. The outputs must be those of the bridge without the cache, byte for byte.
# (the functions are those of tools/dvbridge/dvbridge_render.h)
PAIRS = [
    ('  if (gui && envi("FB_TILES", 1))\n'
     '    dvbridge_render_set_drawn(g->renderer, g_gui, W, H);\n',
     '  if (gui && envi("FB_TILES", 1))\n'
     '  {\n'
     '    // the key of the composite, as Kodi gives it (FB_GKEY)\n'
     '    dvbridge_render_set_drawn_key(g->renderer, (uint64_t)envi("FB_GKEY", 1));\n'
     '    dvbridge_render_set_drawn(g->renderer, g_gui, W, H);\n'
     '  }\n'),
    ('        ReadRgba8(g, g->bytes))\n'
     '      WriteFile(dir, tag, number, "composed.rgba", g->bytes, (size_t)W * H * 4);\n',
     '        ReadRgba8(g, g->bytes))\n'
     '    {\n'
     '      WriteFile(dir, tag, number, "composed.rgba", g->bytes, (size_t)W * H * 4);\n'
     '      // what the GUI cache did with this frame, on stdout only\n'
     '      unsigned c[2], k[4];\n'
     '      if (gui && dvbridge_render_gui_cache_read(g->renderer, c))\n'
     '        printf("gcache picture %u: copied %u packed %u\\n", number, c[0], c[1]);\n'
     '      if (gui && dvbridge_render_gui_cache_tiles(g->renderer, k) && k[3])\n'
     '      {\n'
     '        printf("gcheck picture %u: copied %u differ %u\\n", number, k[0], k[2]);\n'
     '        if (k[2])\n'
     '          ++g_failures;\n'
     '      }\n'
     '    }\n'),
    ('  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n',
     '  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n'
     '  // the GUI cache on, as Kodi turns it on, unless FB_GCACHE=0; FB_GCHECK=1 its check\n'
     '  dvbridge_render_use_gui_cache(g.renderer, envi("FB_GCACHE", 1) != 0);\n'
     '  dvbridge_render_gui_cache_check(g.renderer, envi("FB_GCHECK", 0) != 0);\n'
     '  printf("GUI cache: %s\\n", envi("FB_GCACHE", 1) ? "on" : "off");\n'),
]
