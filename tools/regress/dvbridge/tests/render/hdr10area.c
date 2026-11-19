// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): Dolby Vision for
// output as HDR10 to a 1920x1080 output, as Kodi draws it (CWinSystemGbmGLESContext::RenderDVConversion(): the
// picture reconstructed into the place Kodi gives it, dvbridge_render_hdr10(), then copied into the window within
// the place of this frame, CDVBridgeGLES::CopyConversion(): dvbridge_gl_copy_area() with the place of the last
// whole picture and the area of this frame, first row at the top). The window is a 10-bit framebuffer filled with
// a value that no copy writes before each copy. Checked, in full and in limited range:
//   - nothing outside the area of the frame is written (Kodi draws its black bars there);
//   - inside it, the same words as a renderer of its own that drew only that picture (whatever the texture held
//     of the pictures at other places before);
//   - a picture shown again at a larger area: what of the area it does not cover is black (code 0, or 64 in the
//     limited range); at a smaller area, only that area is written.
// Usage: hdr10area <data dir> <picture>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixfmt.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "layers.h"
#include "upload.h"

#define OW 1920
#define OH 1080

static int g_fail;
static GLuint g_fb, g_tex;
static pl_gpu g_gpu;

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static void Fill(void)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void ReadWindow(uint32_t* out)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, OW, OH, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, out);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// a pixel of the window (GL row, bottom first) in a rect given with its first row at the top, as the copy flips it
static int In(struct dvbridge_gl_rect r, int x, int glrow)
{
  const int y = OH - 1 - glrow;
  return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

// CDVBridgeGLES::CopyConversion(): the last whole picture, at its place, within the area of this frame
static int Copy(struct dvbridge_renderer* r, struct dvbridge_gl_copier* c, struct dvbridge_gl_rect place,
                struct dvbridge_gl_rect area, bool limited, uint32_t* out)
{
  const pl_tex t = dvbridge_render_hdr10_last(r);
  const GLuint id = t ? pl_opengl_unwrap(g_gpu, t, NULL, NULL, NULL) : 0;
  Fill();
  // MUT=1: copied at the place of the picture, as before Kodi gave the copy the area of the frame (a mutation that
  // the checks have to catch)
  const char* mut = getenv("MUT");
  if (mut && !strcmp(mut, "1"))
    area = place;
  const int ok = id && dvbridge_gl_copy_area(c, id, g_fb, OW, OH, &place, &area, true, limited);
  ReadWindow(out);
  return ok && glGetError() == GL_NO_ERROR;
}

static struct dvbridge_gl_rect Place(struct dvbridge_view v)
{
  return (struct dvbridge_gl_rect){v.dst_x, v.dst_y, v.dst_width, v.dst_height};
}

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  g_fail += !ok;
}

int main(int argc, char** argv)
{
  if (argc != 3)
    return 2;
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, nc = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLConfig cfg = NULL;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nc) || !nc)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!gl)
    return 1;
  g_gpu = gl->gpu;
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, OW, OH);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g_fb);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  struct dvbridge_gl_copier* copier = dvbridge_gl_copier_create();
  if (!complete || !copier)
    return 1;
  const int levels = dvbridge_gl_copy_levels(g_fb);
  printf("window: 10-bit framebuffer %dx%d, levels %d\n", OW, OH, levels);

  char pdir[1024];
  snprintf(pdir, sizeof pdir, "%s/p%s", argv[1], argv[2]);
  struct fid_picture p;
  struct fid_gl_layer bl = {0}, el = {0};
  if (!fid_load(pdir, &p) || !fid_upload(&bl, p.bl, p.bl_loc, 1) || !fid_upload(&el, p.el, p.el_loc, 1))
    return 1;
  struct dvbridge_gl_plane bp[3] = {{bl.tex[0], bl.w, bl.h}, {bl.tex[1], (bl.w + 1) / 2, (bl.h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_plane ep[3] = {{el.tex[0], el.w, el.h}, {el.tex[1], (el.w + 1) / 2, (el.h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!dvbridge_gl_frame_import(&b, g_gpu, DVBRIDGE_P010, bl.w, bl.h, bp, (enum AVChromaLocation)bl.loc) ||
      !dvbridge_gl_frame_import(&e, g_gpu, DVBRIDGE_P010, el.w, el.h, ep, (enum AVChromaLocation)el.loc))
    return 1;
  b.frame.enhancement_layer = &e.frame;
  const AVDOVIMetadata* meta = p.meta;
  const double pts = p.bl_pts / 1000.0;

  // the whole picture over the output; a part of it letterboxed (its rows 276 to 1884, as a film of 2.39:1 is shown
  // without its bars); the whole picture pillarboxed (4:3)
  const struct dvbridge_view vA = {0, 0, 3840, 2160, 0, 0, OW, OH};
  const struct dvbridge_view vB = {0, 276, 3840, 1884, 0, 138, OW, 804};
  const struct dvbridge_view vC = {0, 0, 3840, 2160, 240, 0, 1440, OH};
  uint32_t* win = malloc((size_t)OW * OH * 4);
  uint32_t* ref = malloc((size_t)OW * OH * 4);
  Fill();
  ReadWindow(win);
  const uint32_t sentinel = win[0];
  printf("the window filled with 0x%08x before each copy\n", sentinel);

  for (int limited = 0; limited < 2; ++limited)
  {
    struct dvbridge_renderer* r = dvbridge_renderer_create_hdr10(g_gpu, log);
    const int codes = dvbridge_render_hdr10_store(r, levels, limited);
    printf("--- %s range: the picture kept as %s\n", limited ? "limited" : "full", codes ? "10-bit codes" : "floats");
    const uint32_t black = limited ? 64 : 0;
    // the renderer of the frames draws A, B, C, then the frames that show the last whole picture again
    const struct dvbridge_view seq[3] = {vA, vB, vC};
    const char* names[3] = {"whole", "letterboxed", "pillarboxed"};
    for (int i = 0; i < 3; ++i)
    {
      if (!dvbridge_render_hdr10(r, &b.frame, meta, p.meta_size, pts, pts, seq[i], OW, OH) ||
          !Copy(r, copier, Place(seq[i]), Place(seq[i]), limited, win))
      {
        Check(0, "drawn and copied");
        continue;
      }
      // a renderer of its own that drew this picture alone
      struct dvbridge_renderer* f = dvbridge_renderer_create_hdr10(g_gpu, log);
      dvbridge_render_hdr10_store(f, levels, limited);
      const int fine = dvbridge_render_hdr10(f, &b.frame, meta, p.meta_size, pts, pts, seq[i], OW, OH) &&
                       Copy(f, copier, Place(seq[i]), Place(seq[i]), limited, ref);
      dvbridge_renderer_destroy(f);
      long outside = 0, differ = 0, in = 0;
      for (int y = 0; y < OH; ++y)
        for (int x = 0; x < OW; ++x)
        {
          const size_t k = (size_t)y * OW + x;
          if (In(Place(seq[i]), x, y))
          {
            ++in;
            differ += win[k] != ref[k];
          }
          else
            outside += win[k] != sentinel;
        }
      char what[256];
      snprintf(what, sizeof what, "%s (after the pictures before it at other places): %ld of %ld words inside differ from "
               "a renderer that drew it alone, %ld words written outside its place", names[i], differ, in, outside);
      Check(fine && !differ && !outside, what);
    }
    // the last whole picture (pillarboxed) shown again where this frame's place is the whole output
    {
      Copy(r, copier, Place(vC), Place(vA), limited, win);
      long notBlack = 0, band = 0, changedInside = 0;
      Copy(r, copier, Place(vC), Place(vC), limited, ref);
      for (int y = 0; y < OH; ++y)
        for (int x = 0; x < OW; ++x)
        {
          const size_t k = (size_t)y * OW + x;
          if (In(Place(vC), x, y))
            changedInside += win[k] != ref[k];
          else
          {
            ++band;
            const uint32_t w = win[k];
            notBlack += (w & 1023) != black || ((w >> 10) & 1023) != black || ((w >> 20) & 1023) != black;
          }
        }
      char what[256];
      snprintf(what, sizeof what, "the pillarboxed picture shown again over the whole output: %ld of %ld words of the "
               "area it does not cover not black (%u), %ld words of the picture otherwise", notBlack, band, black,
               changedInside);
      Check(!notBlack && !changedInside, what);
    }
    // the whole picture drawn, then shown again where this frame's place is the letterboxed one
    if (dvbridge_render_hdr10(r, &b.frame, meta, p.meta_size, pts, pts, vA, OW, OH))
    {
      Copy(r, copier, Place(vA), Place(vB), limited, win);
      Copy(r, copier, Place(vA), Place(vA), limited, ref);
      long outside = 0, differ = 0;
      for (int y = 0; y < OH; ++y)
        for (int x = 0; x < OW; ++x)
        {
          const size_t k = (size_t)y * OW + x;
          if (In(Place(vB), x, y))
            differ += win[k] != ref[k];
          else
            outside += win[k] != sentinel;
        }
      char what[256];
      snprintf(what, sizeof what, "the whole picture shown again in a letterboxed place: %ld words outside it written, "
               "%ld inside it not those of the picture", outside, differ);
      Check(!outside && !differ, what);
    }
    else
      Check(0, "the whole picture drawn again");
    dvbridge_renderer_destroy(r);
  }
  printf("%s\n", g_fail ? "HDR10AREA-FAILED" : "HDR10AREA-PASSED");
  fflush(stdout);
  _Exit(g_fail ? 1 : 0);
}
