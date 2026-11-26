# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of hdrsdr_test.c (rg_edit): HDR over the tunnel drawn again (dvbridge_render_set_tunnel(),
# dvbridge_render_reused()). With TUNNEL_REUSE=<0|1> set (the reuse off or on), after its own checks the test draws
# the picture into the tunnel of a transport renderer four times, as Kodi draws a paused picture with the OSD over it:
# each time drawn, a GUI composited into it (an opaque rectangle 200 pixels further right each time; TUNNEL_GUI=0:
# none), the bridge told (dvbridge_render_texture_drawn()), and packed; each packed frame is written
# (<prefix>tunnel-<n>.rgba), and a line says how many draws took the picture of the one before.
BLOCK = r'''  if (getenv("TUNNEL_REUSE"))
  {
    struct dvbridge_renderer* t = dvbridge_renderer_create(g.gl->gpu, g.log);
    struct dvbridge_gl_packer* pk = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
    const int gui = !getenv("TUNNEL_GUI") || atoi(getenv("TUNNEL_GUI"));
    int ok = t && pk && dvbridge_render_set_tunnel(t, true);
    if (t)
      dvbridge_render_allow_reuse(t, atoi(getenv("TUNNEL_REUSE")) != 0);
    GLuint col = 0, fbo = 0, over = 0;
    glGenTextures(1, &col);
    glBindTexture(GL_TEXTURE_2D, col);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glGenFramebuffers(1, &over);
    uint8_t* bytes = malloc((size_t)W * H * 4);
    int reused = 0, drawn = 0;
    for (int i = 0; ok && i < 4; ++i)
    {
      ok = dvbridge_render_plain(t, &b.frame, &plain, WHOLE, W, H);
      reused += ok && dvbridge_render_reused(t);
      pl_tex pic = ok ? dvbridge_render_texture(t) : NULL;
      const GLuint id = pic ? pl_opengl_unwrap(g.gl->gpu, pic, NULL, NULL, NULL) : 0;
      ok = ok && id;
      if (ok && gui)
      {
        glBindFramebuffer(GL_FRAMEBUFFER, over);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, id, 0);
        glEnable(GL_SCISSOR_TEST);
        glScissor(400 + 200 * i, 300, 600, 400);
        glClearColor(0.55f, 0.45f, 0.35f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        dvbridge_render_texture_drawn(t);
      }
      ok = ok && dvbridge_render_pack(t, pk, fbo, W, H, true, DVBRIDGE_COVER_NONE);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      ok = ok && glGetError() == GL_NO_ERROR;
      char name[32];
      snprintf(name, sizeof name, "tunnel-%d.rgba", i);
      char path[1024];
      snprintf(path, sizeof path, "%s%s", prefix, name);
      FILE* f = ok ? fopen(path, "wb") : NULL;
      ok = ok && f && fwrite(bytes, 1, (size_t)W * H * 4, f) == (size_t)W * H * 4;
      if (f)
        fclose(f);
      drawn += ok;
    }
    printf("tunnel: drawn %d of 4 times (GUI %d, reuse %d), %d reused\n", drawn, gui, atoi(getenv("TUNNEL_REUSE")),
           reused);
    Check(drawn == 4, "HDR over the tunnel drawn and packed four times");
    free(bytes);
    dvbridge_gl_packer_destroy(pk);
    dvbridge_renderer_destroy(t);
  }
  dvbridge_gl_frame_release(&b);
'''
PAIRS = [
    ('#include "dvbridge_render.h"\n', '#include "dvbridge_render.h"\n#include "dvbridge_gl_pack.h"\n#include "pack_sources.h"\n'),
    ('  dvbridge_gl_frame_release(&b);\n', BLOCK),
]
