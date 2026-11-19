// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): an HDR10 or HLG picture (no
// Dolby Vision) for an output that takes no HDR10, on the software OpenGL ES (zink on lavapipe), with the
// bridge of a tree (dvbridge_render_plain()) and the libplacebo of the build. What it draws is read back and
// written for the references to compare; what can be checked without one is checked here.
//
//   asis : the picture as it is (mapping off): its own signal, PQ or HLG, in BT.2020
//   sdr  : mapped to an SDR output (dvbridge_sdr_mapping(), the mastering primaries in it when given)
//
// Usage: hdrsdr_test <yuv420p10le, 3840x2160, BT.2020 NCL, limited> <chroma location> <pq|hlg>
//                    <max luma> <min luma> <maxcll> <maxfall> <p3|2020|none> <out prefix> [<timed draws>]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"

#define W 3840
#define H 2160

static int g_failures;

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static void* ReadAll(const char* path, size_t* size)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void* p = malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
  {
    free(p);
    p = NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return p;
}

static double NowMs(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int CmpDouble(const void* a, const void* b)
{
  const double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
  float* pic;
  float* ref;
};

static int ReadFloats(GLuint texture, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, into);
  const int read = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return read;
}

static const struct dvbridge_view WHOLE = {0, 0, (float)W, (float)H, 0, 0, W, H};

static int Draw(struct gl* g, const struct pl_frame* source, const struct dvbridge_plain* plain)
{
  if (!dvbridge_render_plain(g->renderer, source, plain, WHOLE, W, H))
    return 0;
  pl_tex tex = dvbridge_render_hdr10_texture(g->renderer);
  const GLuint t = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  return t && ReadFloats(t, g->pic);
}

static int WriteRgb(const char* prefix, const char* name, const float* rgba)
{
  char path[1024];
  snprintf(path, sizeof(path), "%s%s.f32", prefix, name);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  float* row = malloc((size_t)W * 3 * sizeof(float));
  int ok = row != NULL;
  for (int y = 0; ok && y < H; ++y)
  {
    for (int x = 0; x < W; ++x)
      memcpy(row + (size_t)x * 3, rgba + ((size_t)y * W + x) * 4, 3 * sizeof(float));
    ok = fwrite(row, sizeof(float), (size_t)W * 3, f) == (size_t)W * 3;
  }
  free(row);
  return fclose(f) == 0 && ok;
}

static void Range(const float* p, long* outside, long* strange, double* lo, double* hi)
{
  *outside = *strange = 0;
  *lo = 1e9;
  *hi = -1e9;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    for (int c = 0; c < 3; ++c)
    {
      const float v = p[i * 4 + c];
      if (!isfinite(v))
      {
        ++*strange;
        continue;
      }
      *outside += v < 0 || v > 1;
      if (v < *lo) *lo = v;
      if (v > *hi) *hi = v;
    }
}

static void Time(struct gl* g, const struct pl_frame* source, const struct dvbridge_plain* plain,
                 const struct dvbridge_hdr10_mapping* mapping, int times, const char* what)
{
  struct dvbridge_renderer* r = dvbridge_renderer_create_hdr10(g->gl->gpu, g->log);
  if (!r || !dvbridge_render_hdr10_mapping(r, mapping))
  {
    printf("timing %s: no renderer\n", what);
    dvbridge_renderer_destroy(r);
    return;
  }
  glFinish();
  double t0 = NowMs();
  int ok = dvbridge_render_plain(r, source, plain, WHOLE, W, H);
  glFinish();
  const double first = NowMs() - t0;
  double* ms = calloc((size_t)times, sizeof(double));
  for (int i = 0; i < times; ++i)
  {
    t0 = NowMs();
    ok = ok && dvbridge_render_plain(r, source, plain, WHOLE, W, H);
    glFinish();
    ms[i] = NowMs() - t0;
  }
  qsort(ms, (size_t)times, sizeof(double), CmpDouble);
  printf("timing %s: %s; first draw %.0f ms; then %d draws: min %.0f, median %.0f, max %.0f ms\n", what,
         ok ? "drawn" : "NOT DRAWN", first, times, ms[0], ms[times / 2], ms[times - 1]);
  free(ms);
  dvbridge_renderer_destroy(r);
}

int main(int argc, char** argv)
{
  if (argc != 10 && argc != 11)
    return 2;
  size_t blBytes = 0;
  uint16_t* bl = ReadAll(argv[1], &blBytes);
  const enum AVChromaLocation loc = (enum AVChromaLocation)atoi(argv[2]);
  struct dvbridge_plain plain = {.hlg = !strcmp(argv[3], "hlg"), .full_range = false,
                                 .max_luma = (float)atof(argv[4]), .min_luma = (float)atof(argv[5]),
                                 .max_cll = (float)atof(argv[6]), .max_fall = (float)atof(argv[7])};
  const char* prim = argv[8];
  const char* prefix = argv[9];
  const int times = argc == 11 ? atoi(argv[10]) : 0;
  if (!bl || blBytes != (size_t)W * H * 2 * 3 / 2)
  {
    fprintf(stderr, "inputs\n");
    return 1;
  }

  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  struct gl g = {0};
  g.log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  g.gl = pl_opengl_create(g.log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                  .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  g.renderer = g.gl ? dvbridge_renderer_create_hdr10(g.gl->gpu, g.log) : NULL;
  g.pic = malloc((size_t)W * H * 16);
  g.ref = malloc((size_t)W * H * 16);
  Check(g.gl && g.renderer && g.pic && g.ref, "libplacebo and the renderer");
  if (!g.gl || !g.renderer || !g.pic || !g.ref)
    return 1;

  // --- the picture as P010, as Kodi imports a VAAPI surface
  GLuint tex[2];
  glGenTextures(2, tex);
  {
    uint16_t* y = malloc((size_t)W * H * 2);
    uint16_t* c = malloc((size_t)W * H);
    const uint16_t* u = bl + (size_t)W * H;
    const uint16_t* v = u + (size_t)W * H / 4;
    for (size_t i = 0; i < (size_t)W * H; ++i)
      y[i] = (uint16_t)(bl[i] << 6);
    for (size_t i = 0; i < (size_t)W * H / 4; ++i)
    {
      c[i * 2] = (uint16_t)(u[i] << 6);
      c[i * 2 + 1] = (uint16_t)(v[i] << 6);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, W, H, 0, GL_RED, GL_UNSIGNED_SHORT, y);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, W / 2, H / 2, 0, GL_RG, GL_UNSIGNED_SHORT, c);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(y);
    free(c);
  }
  const struct dvbridge_gl_plane planes[3] = {{tex[0], W, H}, {tex[1], W / 2, H / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0};
  Check(glGetError() == GL_NO_ERROR && dvbridge_gl_frame_import(&b, g.gl->gpu, DVBRIDGE_P010, W, H, planes, loc),
        "the picture imported (P010)");

  long outside, strange;
  double lo, hi;
  // --- 1. as it is
  dvbridge_render_hdr10_mapping(g.renderer, NULL);
  const int asis = Draw(&g, &b.frame, &plain);
  Check(asis, "the picture drawn as it is (its own signal, BT.2020)");
  if (!asis)
    return 1;
  memcpy(g.ref, g.pic, (size_t)W * H * 16);
  Range(g.pic, &outside, &strange, &lo, &hi);
  printf("as it is: channels from %.6f to %.6f; outside [0, 1] %ld, not finite %ld\n", lo, hi, outside, strange);
  WriteRgb(prefix, "asis", g.pic);

  // --- 2. mapped to an SDR output
  float xy[8];
  dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);
  const struct dvbridge_hdr10_mapping sdr = dvbridge_sdr_mapping(!strcmp(prim, "p3") ? xy : NULL);
  Check(dvbridge_render_hdr10_mapping(g.renderer, &sdr), "the renderer takes the SDR mapping");
  const int mapped = Draw(&g, &b.frame, &plain);
  Check(mapped, "the picture drawn mapped to SDR");
  Range(g.pic, &outside, &strange, &lo, &hi);
  printf("mapped to SDR: channels from %.6f to %.6f; outside [0, 1] %ld, not finite %ld\n", lo, hi, outside, strange);
  Check(mapped && !outside && !strange, "... within [0, 1], every value finite");
  long same = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    same += g.pic[i] == g.ref[i];
  Check(mapped && same < (long)W * H * 2, "... and not the picture as it is");
  WriteRgb(prefix, "sdr", g.pic);

  // --- 3. what is refused draws nothing (the caller then shows the picture its own way)
  {
    const struct dvbridge_plain bad[] = {
        {.hlg = plain.hlg, .max_luma = -1},
        {.hlg = plain.hlg, .max_luma = NAN},
        {.hlg = plain.hlg, .max_luma = 1000, .min_luma = 1000},
        {.hlg = plain.hlg, .max_cll = 20000},
    };
    int refused = 1;
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
      refused = refused && !dvbridge_render_plain(g.renderer, &b.frame, &bad[i], WHOLE, W, H) &&
                !dvbridge_render_hdr10_texture(g.renderer);
    Check(refused, "HDR10 metadata that does not hold is refused, and nothing is drawn");
    struct pl_frame layered = b.frame;
    layered.enhancement_layer = &b.frame;
    Check(!dvbridge_render_plain(g.renderer, &layered, &plain, WHOLE, W, H) &&
              !dvbridge_render_plain(g.renderer, &b.frame, NULL, WHOLE, W, H) &&
              !dvbridge_render_plain(NULL, &b.frame, &plain, WHOLE, W, H),
          "a source with an enhancement layer, no colour or no renderer: refused");
    // the last whole picture stays (for the frame that could not be drawn): the SDR one
    pl_tex last = dvbridge_render_hdr10_last(g.renderer);
    Check(last != NULL, "... and the last whole picture is still there");
  }
  {
    const int off = dvbridge_render_hdr10_mapping(g.renderer, NULL) && Draw(&g, &b.frame, &plain) &&
                    !memcmp(g.pic, g.ref, (size_t)W * H * 16);
    Check(off, "the mapping off again draws the picture as it is, to the bit");
  }
  if (times > 0)
  {
    Time(&g, &b.frame, &plain, NULL, times, "as it is");
    Time(&g, &b.frame, &plain, &sdr, times, "mapped to SDR");
  }
  dvbridge_gl_frame_release(&b);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
