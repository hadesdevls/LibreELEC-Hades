// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: nothing of this goes into Kodi or into an image.
//
// Native Dolby Vision of pictures cropped to sizes that are not multiples of 4 (3840x1746, 3840x1606, 1920x1038, ...),
// drawn through the bridge of the Kodi tree under test (tools/dvbridge, the files themselves) on a software OpenGL ES
// (zink on lavapipe), placed where Kodi places them: Kodi's destination rectangle (CBaseRenderer::CalcDestRect(), zoom
// 1, square pixels, a 3840x2160 screen) and CLinuxRendererGLES::DVGeometry()'s default path (LinuxRendererGLES.cpp,
// the lines that compute the geometry), written again here in C. The layers are those of p<N> of CB_DATA
// (the output of ref_dump: the base layer decoded), the picture cropped in memory at an even row and column (as a cropped
// encode is: its 4:2:0 chroma starts with its first row), optionally every second sample first (step 2: 1920x1080).
// The metadata: an RPU file (an AVDOVIMetadata blob) or "-" for the picture's own meta.bin. The enhancement layer is
// never attached (a cropped encode is single-layer).
//   <out>/<tag>-composed.rgba   the transport of the composed way (RGBA8 3840x2160, rows as read back)
//   <out>/<tag>-direct.rgba     the transport of the direct way, where the bridge draws it so
//   stdout: the geometry, what the bridge said, the margins and the level 5 block sent
// CB_SOURCE (default 1): the source transport, as Kodi asks for it. CB_WIDE=1: only the checks of a picture wider
// than 3840 (the import and the render of a 4096-wide geometry), nothing drawn.
// Usage: cropbench <data dir> <picture> <rpu file|-> <out dir> <tag> <step> <x0> <y0> <w> <h>
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

#include "dvbridge_core.h"
#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "dvbridge_source.h"
#include "layers.h"

#include "pack_sources.h"

#define W 3840
#define H 2160

static int envi(const char* name, int fallback)
{
  const char* v = getenv(name);
  return v && *v ? atoi(v) : fallback;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static GLuint g_fbo, g_color;

static int ReadRgba8(uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

static void Fill(float v)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(v, v, v, v);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int WriteFile(const char* dir, const char* tag, const char* what, const void* data, size_t size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s-%s", dir, tag, what);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  const int ok = fwrite(data, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

// Kodi's destination rectangle for a w x h picture of square pixels on a 3840x2160 screen, zoom 1, no shift
// (CBaseRenderer::CalcDestRect(), BaseRenderer.cpp:107-..., in floats as there)
static void KodiDest(int w, int h, float d[4])
{
  const float ratio = (float)w / (float)h;
  float nw = 3840.0f, nh = nw / ratio;
  if (nh > 2160.0f)
  {
    nh = 2160.0f;
    nw = nh * ratio;
  }
  if (fabsf(nw - 3840.0f) < 1.0f)
    nw = 3840.0f;
  if (fabsf(nh - 2160.0f) < 1.0f)
    nh = 2160.0f;
  d[0] = (3840.0f - nw) / 2.0f;
  d[1] = (2160.0f - nh) / 2.0f;
  d[2] = d[0] + nw;
  d[3] = d[1] + nh;
}

// CLinuxRendererGLES::DVGeometry(), the path without a view (LinuxRendererGLES.cpp:527-559)
static int DVGeometry(int sw, int sh, const float d[4], struct dvbridge_geometry* g)
{
  if (sw == 0 || sh == 0 || sw > 3840 || sh > 2160)
    return 0;
  const double aspect = (double)sh / sw;
  int left = 2 * (int)ceil(d[0] / 2.0f - 0.01f);
  int right = 2 * (int)floor(d[2] / 2.0f + 0.01f);
  left = left < 0 ? 0 : left > 3840 ? 3840 : left;
  right = right < 0 ? 0 : right > 3840 ? 3840 : right;
  const int width = right - left;
  if (width <= 0)
    return 0;
  int height = (int)lround(width * aspect);
  height = height > 2160 ? 2160 : height;
  if (height <= 0)
    return 0;
  int top = (int)lround((double)(d[1] + d[3]) / 2.0 - height / 2.0);
  top = top < 0 ? 0 : top > 2160 - height ? 2160 - height : top;
  memset(g, 0, sizeof(*g));
  g->source_width = sw;
  g->source_height = sh;
  g->x = left;
  g->y = top;
  g->width = width;
  g->height = height;
  return 1;
}

// the base layer, every step-th sample, cropped to w x h at x0, y0 (even), as P010 textures (upload.h's layout)
static int Upload(const struct fid_plane* p, int step, int x0, int y0, int w, int h, GLuint tex[2])
{
  const int cw = (w + 1) / 2, ch = (h + 1) / 2, cx0 = x0 / 2, cy0 = y0 / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
  if (!y || !c)
    return 0;
  for (int r = 0; r < h; ++r)
    for (int x = 0; x < w; ++x)
      y[(size_t)r * w + x] = (uint16_t)(p[0].v[(size_t)(y0 + r) * step * p[0].w + (size_t)(x0 + x) * step] << 6);
  for (int r = 0; r < ch; ++r)
    for (int x = 0; x < cw; ++x)
    {
      const size_t s = (size_t)(cy0 + r) * step * p[1].w + (size_t)(cx0 + x) * step;
      c[((size_t)r * cw + x) * 2] = (uint16_t)(p[1].v[s] << 6);
      c[((size_t)r * cw + x) * 2 + 1] = (uint16_t)(p[2].v[s] << 6);
    }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glGenTextures(2, tex);
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, y);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, cw, ch, 0, GL_RG, GL_UNSIGNED_SHORT, c);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  free(y);
  free(c);
  return glGetError() == GL_NO_ERROR;
}

static int ReadFile(const char* path, void** out, size_t* size)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  const size_t n = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  void* p = aligned_alloc(64, (n + 63) / 64 * 64);
  const int ok = p && fread(p, 1, n, f) == n;
  fclose(f);
  if (!ok)
  {
    free(p);
    return 0;
  }
  *out = p;
  *size = n;
  return 1;
}

static void Sent(const struct dvbridge_candidate* c, const char* way)
{
  uint8_t levels[32];
  unsigned l5[4] = {0}, margins[4] = {0}, count = 0;
  const int n = dvbridge_sent_blocks(c, levels, 32, l5);
  dvbridge_active_area(c, margins);
  dvbridge_packets(c, &count);
  printf("%s: packets %u, margins (l r t b) %u %u %u %u, blocks %d [", way, count, margins[0], margins[1], margins[2],
         margins[3], n);
  for (int i = 0; i < n && i < 32; ++i)
    printf("%s%u", i ? " " : "", levels[i]);
  printf("], L5 sent (l r t b) %u %u %u %u\n", l5[0], l5[1], l5[2], l5[3]);
  struct dvbridge_reading rd;
  if (dvbridge_candidate_reading(c, &rd))
    printf("%s: reading l5_master %d, l5 %u %u %u %u, as_given %u %u %u %u, margins %u %u %u %u, l5_ignored %s\n", way,
           rd.l5_master, rd.l5[0], rd.l5[1], rd.l5[2], rd.l5[3], rd.as_given[0], rd.as_given[1], rd.as_given[2],
           rd.as_given[3], rd.margins[0], rd.margins[1], rd.margins[2], rd.margins[3],
           rd.l5_ignored ? rd.l5_ignored : "-");
}

int main(int argc, char** argv)
{
  if (argc < 11)
    return 2;
  const char *data = argv[1], *rpu = argv[3], *dir = argv[4], *tag = argv[5];
  const int picture = atoi(argv[2]), step = atoi(argv[6]), x0 = atoi(argv[7]), y0 = atoi(argv[8]),
            w = atoi(argv[9]), h = atoi(argv[10]);
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
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!gl)
    return 1;
  struct dvbridge_renderer* r = dvbridge_renderer_create(gl->gpu, log);
  struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  if (!r || !packer)
    return 1;
  dvbridge_render_use_ccm_composer(r, true);
  dvbridge_render_use_source_transport(r, envi("CB_SOURCE", 1) != 0);
  int failures = 0;

  if (envi("CB_WIDE", 0))
  {
    // a 4096-wide picture: the import of its layers, and a frame of a geometry of that size
    GLuint t[2];
    glGenTextures(2, t);
    glBindTexture(GL_TEXTURE_2D, t[0]);
    glTexStorage2D(GL_TEXTURE_2D, 1, 0x822A, 4096, 2160);
    glBindTexture(GL_TEXTURE_2D, t[1]);
    glTexStorage2D(GL_TEXTURE_2D, 1, 0x822C, 2048, 1080);
    glBindTexture(GL_TEXTURE_2D, 0);
    struct dvbridge_gl_plane planes[3] = {{t[0], 4096, 2160}, {t[1], 2048, 1080}, {0, 0, 0}};
    struct dvbridge_gl_frame f = {0};
    const int imported = dvbridge_gl_frame_import(&f, gl->gpu, DVBRIDGE_P010, 4096, 2160, planes, AVCHROMA_LOC_TOPLEFT);
    printf("wide: dvbridge_gl_frame_import(4096x2160) = %d\n", imported);
    void* m = NULL;
    size_t mb = 0;
    if (!ReadFile(rpu, &m, &mb))
      return 1;
    struct dvbridge_context* c = dvbridge_create();
    struct dvbridge_geometry g = {4096, 2160, 0, 0, 3840, 2025, {0, 0, 0, 0}};
    const char* why = NULL;
    struct dvbridge_candidate* k = dvbridge_prepare_why(c, m, mb, 0.0, g, false, 0, &why);
    printf("wide: dvbridge_prepare_why(4096x2160 at 0,0 3840x2025) = %s (%s)\n", k ? "made" : "NULL", why ? why : "-");
    struct dvbridge_geometry g2 = {4096, 1716, 0, 0, 3840, 1609, {0, 0, 0, 0}};
    k = dvbridge_prepare_why(c, m, mb, 0.0, g2, false, 0, &why);
    printf("wide: dvbridge_prepare_why(4096x1716 at 0,0 3840x1609) = %s (%s)\n", k ? "made" : "NULL", why ? why : "-");
    printf("CROPBENCH-WIDE-DONE\n");
    fflush(stdout);
    _Exit(0);
  }

  char pdir[1024];
  snprintf(pdir, sizeof pdir, "%s/p%d", data, picture);
  struct fid_picture p;
  if (!fid_load(pdir, &p))
  {
    printf("FAIL: picture %d not loaded\n", picture);
    return 1;
  }
  void* meta = p.meta;
  size_t bytes = p.meta_size;
  if (strcmp(rpu, "-") && !ReadFile(rpu, &meta, &bytes))
  {
    printf("FAIL: no %s\n", rpu);
    return 1;
  }
  const int sw = p.bl[0].w / step, sh = p.bl[0].h / step;
  if ((x0 & 1) || (y0 & 1) || x0 + w > sw || y0 + h > sh || w < 2 || h < 2)
  {
    printf("FAIL: crop %d,%d %dx%d of %dx%d\n", x0, y0, w, h, sw, sh);
    return 1;
  }
  GLuint tex[2];
  if (!Upload(p.bl, step, x0, y0, w, h, tex))
  {
    printf("FAIL: upload\n");
    return 1;
  }
  struct dvbridge_gl_plane planes[3] = {{tex[0], w, h}, {tex[1], (w + 1) / 2, (h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0};
  if (!dvbridge_gl_frame_import(&b, gl->gpu, DVBRIDGE_P010, w, h, planes, (enum AVChromaLocation)p.bl_loc))
  {
    printf("FAIL: dvbridge_gl_frame_import(%dx%d) refused\n", w, h);
    return 1;
  }
  float d[4];
  KodiDest(w, h, d);
  struct dvbridge_geometry g;
  if (!DVGeometry(w, h, d, &g))
  {
    printf("FAIL: DVGeometry() refused %dx%d\n", w, h);
    return 1;
  }
  printf("case %s: picture %d step %d crop %d,%d %dx%d; Kodi's destination %.3f %.3f %.3f %.3f; geometry x %d y %d "
         "%dx%d; direct fits %d, source scales %d\n",
         tag, picture, step, x0, y0, w, h, d[0], d[1], d[2], d[3], g.x, g.y, g.width, g.height,
         dvbridge_render_direct_fits(r, g), dvbridge_render_source_scales(r, g));
  glGenTextures(1, &g_color);
  glBindTexture(GL_TEXTURE_2D, g_color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_color, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  pl_tex scanout = pl_opengl_wrap(gl->gpu, pl_opengl_wrap_params(.texture = g_color, .target = GL_TEXTURE_2D,
                                                                .iformat = GL_RGBA8, .width = W, .height = H));
  uint8_t* out = malloc((size_t)W * H * 4);
  if (!complete || !scanout || !out)
    return 1;
  const double pts = p.bl_pts / 1000.0;
  // --- the composed way
  Fill(0.5f);
  if (dvbridge_render_rgb(r, &b.frame, meta, bytes, pts, NAN, g))
  {
    const char* why = NULL;
    const int source = dvbridge_render_source(r, &why);
    printf("composed: source transport %d (%s)\n", source, why ? why : "-");
    Sent(dvbridge_render_candidate(r), "composed");
    Fill(0.25f);
    if (dvbridge_render_pack(r, packer, g_fbo, W, H, true, DVBRIDGE_COVER_NONE) && ReadRgba8(out))
      WriteFile(dir, tag, "composed.rgba", out, (size_t)W * H * 4);
    else
    {
      printf("FAIL: the composed way not packed\n");
      ++failures;
    }
  }
  else
  {
    const char* refused = dvbridge_render_refused(r);
    printf("FAIL: dvbridge_render_rgb() failed (%s)\n", refused ? refused : "-");
    ++failures;
  }
  // --- the direct way, where it fits
  if (dvbridge_render_direct_fits(r, g))
  {
    Fill(0.75f);
    if (dvbridge_render_packed(r, &b.frame, meta, bytes, pts, NAN, g, scanout, true) && ReadRgba8(out))
    {
      const char* why = NULL;
      const int source = dvbridge_render_source(r, &why);
      printf("direct: source transport %d (%s)\n", source, why ? why : "-");
      Sent(dvbridge_render_candidate(r), "direct");
      WriteFile(dir, tag, "direct.rgba", out, (size_t)W * H * 4);
    }
    else
    {
      const char* refused = dvbridge_render_refused(r);
      printf("FAIL: dvbridge_render_packed() failed (%s)\n", refused ? refused : "-");
      ++failures;
    }
  }
  else
    printf("direct: not drawn (the geometry does not fit the direct way)\n");
  printf("%s %s\n", failures ? "CROPBENCH-FAILED" : "CROPBENCH-OK", tag);
  fflush(stdout);
  _Exit(failures ? 1 : 0);
}
