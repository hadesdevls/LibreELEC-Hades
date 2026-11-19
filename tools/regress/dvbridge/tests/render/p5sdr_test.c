// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): the picture of a profile 5
// stream for an output that takes no HDR10, on the software OpenGL ES (zink on lavapipe), with the bridge of a
// tree (tools/dvbridge) and the libplacebo of the build. What it draws is read back and written for the
// reference to compare (p5cmp.py); what can be checked without a reference is checked here.
//
//   hdr10: the picture as mastered (dvbridge_render_hdr10(), mapping off), PQ in BT.2020
//   sdr  : the same mapped to an SDR output (dvbridge_sdr_mapping()), BT.709 with the BT.1886 EOTF
//
// Usage: p5sdr_test <AVDOVIMetadata file> <base layer: yuv420p10le, 3840x2160> <chroma location> <out prefix>
//                   [copy]
//   copy: also the copies of the SDR picture to 8- and 10-bit framebuffers, full and limited range
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
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
  void* p = av_malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
  {
    av_free(p);
    p = NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return p;
}

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

struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_copier* copier;
  float* pic;
  float* ref;
};

static GLuint Texture(struct gl* g)
{
  pl_tex tex = dvbridge_render_hdr10_texture(g->renderer);
  return tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
}

static int Draw(struct gl* g, const struct pl_frame* source, const AVDOVIMetadata* meta, size_t bytes)
{
  const struct dvbridge_view whole = {0, 0, (float)W, (float)H, 0, 0, W, H};
  return dvbridge_render_hdr10(g->renderer, source, meta, bytes, 0.0, NAN, whole, W, H) && Texture(g) &&
         ReadFloats(Texture(g), g->pic);
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

// What a picture costs to draw with a mapping (NULL: as mastered), on a renderer made for it: its first draw
// (the shaders made, and the tables of the mapping), then `times` more, each waited for (glFinish()).
static void Time(struct gl* g, const struct pl_frame* source, const AVDOVIMetadata* meta, size_t bytes,
                 const struct dvbridge_hdr10_mapping* mapping, int times, const char* what)
{
  const struct dvbridge_view whole = {0, 0, (float)W, (float)H, 0, 0, W, H};
  struct dvbridge_renderer* r = dvbridge_renderer_create_hdr10(g->gl->gpu, g->log);
  if (!r || !dvbridge_render_hdr10_mapping(r, mapping))
  {
    printf("timing %s: no renderer\n", what);
    dvbridge_renderer_destroy(r);
    return;
  }
  glFinish();
  double t0 = NowMs();
  int ok = dvbridge_render_hdr10(r, source, meta, bytes, 0.0, NAN, whole, W, H);
  glFinish();
  const double first = NowMs() - t0;
  double* ms = calloc((size_t)times, sizeof(double));
  for (int i = 0; i < times; ++i)
  {
    t0 = NowMs();
    ok = ok && dvbridge_render_hdr10(r, source, meta, bytes, 0.0, NAN, whole, W, H);
    glFinish();
    ms[i] = NowMs() - t0;
  }
  qsort(ms, (size_t)times, sizeof(double), CmpDouble);
  printf("timing %s: %s; first draw %.0f ms; then %d draws: min %.0f, median %.0f, max %.0f ms\n", what,
         ok ? "drawn" : "NOT DRAWN", first, times, ms[0], ms[times / 2], ms[times - 1]);
  free(ms);
  dvbridge_renderer_destroy(r);
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
  // rows from the top (the texture's first row is the top one)
  for (int y = 0; ok && y < H; ++y)
  {
    for (int x = 0; x < W; ++x)
      memcpy(row + (size_t)x * 3, rgba + ((size_t)y * W + x) * 4, 3 * sizeof(float));
    ok = fwrite(row, sizeof(float), (size_t)W * 3, f) == (size_t)W * 3;
  }
  free(row);
  return fclose(f) == 0 && ok;
}

// what is in [0, 1] and finite, and the largest and smallest channel
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

// The copy of the SDR picture into a framebuffer of `bits` (8 or 10), full or limited range, against the nearest
// code of each value: full, v * (2^n - 1); limited, the range of an n-bit RGB signal, (219 v + 16) * 2^(n - 8).
static long CopyCheck(struct gl* g, GLuint texture, int bits, int limited, long* mostOff)
{
  GLuint color = 0, fbo = 0;
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexStorage2D(GL_TEXTURE_2D, 1, bits == 10 ? GL_RGB10_A2 : GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  long wrong = -1;
  *mostOff = 0;
  if (dvbridge_gl_copy(g->copier, texture, fbo, W, H, 0, 0, W, H, false, limited))
  {
    void* out = malloc((size_t)W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (bits == 10)
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, out);
    else
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    wrong = 0;
    const double top = (1 << bits) - 1, step = 1 << (bits - 8);
    for (size_t i = 0; i < (size_t)W * H; ++i)
      for (int c = 0; c < 3; ++c)
      {
        const double v = fmin(fmax(g->pic[i * 4 + c], 0.0), 1.0);
        const double want = limited ? (219.0 * v + 16.0) * step : v * top;
        const long code = bits == 10 ? (long)((((const uint32_t*)out)[i] >> (10 * c)) & 1023)
                                     : (long)((const uint8_t*)out)[i * 4 + c];
        const long nearest = lround(want);
        // (a value half way between two codes may go either way)
        if (code != nearest && fabs(fabs(want - floor(want)) - 0.5) > 1e-3)
        {
          ++wrong;
          if (labs(code - nearest) > *mostOff)
            *mostOff = labs(code - nearest);
        }
      }
    free(out);
  }
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &color);
  return wrong;
}

int main(int argc, char** argv)
{
  if (argc != 5 && argc != 6)
    return 2;
  const int copies = argc == 6 && !strcmp(argv[5], "copy");
  size_t bytes = 0, blBytes = 0;
  AVDOVIMetadata* meta = ReadAll(argv[1], &bytes);
  uint16_t* bl = ReadAll(argv[2], &blBytes);
  const enum AVChromaLocation loc = (enum AVChromaLocation)atoi(argv[3]);
  const char* prefix = argv[4];
  if (!meta || !bl || blBytes != (size_t)W * H * 2 * 3 / 2)
  {
    fprintf(stderr, "inputs\n");
    return 1;
  }

  // --- a surfaceless OpenGL ES context
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
  g.copier = dvbridge_gl_copier_create();
  g.pic = malloc((size_t)W * H * 16);
  g.ref = malloc((size_t)W * H * 16);
  Check(g.gl && g.renderer && g.copier && g.pic && g.ref, "libplacebo, the renderer for output as HDR10 and the copier");
  if (!g.gl || !g.renderer || !g.copier || !g.pic || !g.ref)
    return 1;

  // --- the base layer as P010 (16 bits, the 10 in the high bits), as Kodi imports a VAAPI surface
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
        "the base layer imported (P010)");

  long outside, strange;
  double lo, hi;
  // --- 1. as mastered
  dvbridge_render_hdr10_mapping(g.renderer, NULL);
  const int hdr10 = Draw(&g, &b.frame, meta, bytes);
  Check(hdr10, "the profile 5 picture drawn as mastered (PQ, BT.2020)");
  if (!hdr10)
    return 1;
  memcpy(g.ref, g.pic, (size_t)W * H * 16);
  Range(g.pic, &outside, &strange, &lo, &hi);
  printf("as mastered: channels from %.6f to %.6f (PQ); outside [0, 1] %ld, not finite %ld\n", lo, hi, outside,
         strange);
  WriteRgb(prefix, "hdr10", g.pic);

#ifdef DVBRIDGE_SDR_MAPPING
  // --- 2. mapped to an SDR output
  const struct dvbridge_hdr10_mapping sdr = dvbridge_sdr_mapping(NULL);
  Check(sdr.enabled && sdr.target == DVBRIDGE_HDR10_TARGET_SDR && sdr.gamut == DVBRIDGE_HDR10_GAMUT_BT709 &&
            sdr.curve == DVBRIDGE_HDR10_CURVE_BT2390 && sdr.peak == DVBRIDGE_SDR_WHITE && !sdr.source_known &&
            dvbridge_hdr10_mapping_valid(&sdr),
        "the SDR mapping: to 203 cd/m2, BT.709, BT.2390, valid");
  Check(dvbridge_render_hdr10_mapping(g.renderer, &sdr), "the renderer takes it");
  const int mapped = Draw(&g, &b.frame, meta, bytes);
  Check(mapped, "the picture drawn mapped to SDR");
  Range(g.pic, &outside, &strange, &lo, &hi);
  printf("mapped to SDR: channels from %.6f to %.6f; outside [0, 1] %ld, not finite %ld\n", lo, hi, outside, strange);
  Check(mapped && !outside && !strange, "... within [0, 1], every value finite");
  long same = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    same += g.pic[i] == g.ref[i];
  Check(mapped && same < (long)W * H * 2, "... and not the picture as mastered");
  WriteRgb(prefix, "sdr", g.pic);
  float* sdrPic = malloc((size_t)W * H * 16);
  if (sdrPic)
    memcpy(sdrPic, g.pic, (size_t)W * H * 16);
  {
    // a source gamut given (the HDR10 metadata of a stream): taken, and P3 primaries are a gamut
    float xy[8];
    dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);
    const struct dvbridge_hdr10_mapping p3 = dvbridge_sdr_mapping(xy);
    float bad[8] = {0};
    const struct dvbridge_hdr10_mapping none = dvbridge_sdr_mapping(bad);
    Check(p3.source_known && dvbridge_hdr10_mapping_valid(&p3) && !none.source_known &&
              dvbridge_hdr10_mapping_valid(&none),
          "a source gamut is taken where it is one, and left out where it is not");
  }
  {
    // not valid: SDR in another gamut than BT.709, and a target that does not exist: refused, the picture as mastered
    struct dvbridge_hdr10_mapping wrong = sdr;
    wrong.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;
    struct dvbridge_hdr10_mapping unknown = sdr;
    unknown.target = (enum dvbridge_hdr10_target)7;
    const int refused = !dvbridge_hdr10_mapping_valid(&wrong) && !dvbridge_hdr10_mapping_valid(&unknown) &&
                        !dvbridge_render_hdr10_mapping(g.renderer, &wrong) && Draw(&g, &b.frame, meta, bytes) &&
                        !memcmp(g.pic, g.ref, (size_t)W * H * 16);
    Check(refused, "an SDR mapping in another gamut, and an unknown target, are refused: the picture as mastered");
    struct dvbridge_hdr10_static s;
    Check(!dvbridge_hdr10_static_mapped(&s, meta, bytes, &sdr), "an SDR output is told no HDR10 metadata");
  }
  {
    // off again: the picture as mastered, to the bit
    const int off = dvbridge_render_hdr10_mapping(g.renderer, NULL) && Draw(&g, &b.frame, meta, bytes) &&
                    !memcmp(g.pic, g.ref, (size_t)W * H * 16);
    Check(off, "the mapping off again draws the picture as mastered, to the bit");
  }
  {
    // the HDR10 mapping of a projector still is one: within its peak
    struct dvbridge_hdr10_mapping proj = {0};
    proj.enabled = true;
    proj.peak = 300;
    proj.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;
    proj.curve = DVBRIDGE_HDR10_CURVE_BT2390;
    const int drawn = dvbridge_render_hdr10_mapping(g.renderer, &proj) && Draw(&g, &b.frame, meta, bytes);
    Range(g.pic, &outside, &strange, &lo, &hi);
    const double ceiling = pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, 300.0f);
    printf("mapped to an HDR10 display of 300 cd/m2: channels up to %.6f (its peak %.6f, PQ)\n", hi, ceiling);
    Check(drawn && hi <= ceiling + 1e-5 && !strange, "the HDR10 mapping to a display is as before: within its peak");
    dvbridge_render_hdr10_mapping(g.renderer, NULL);
  }
  {
    // P5SDR_TIMES=<n>: what each way costs to draw (as mastered, mapped to SDR, mapped to a projector of
    // 150 cd/m2 in DCI-P3)
    const char* t = getenv("P5SDR_TIMES");
    const int times = t ? atoi(t) : 0;
    if (times > 0)
    {
      struct dvbridge_hdr10_mapping proj = {0};
      proj.enabled = true;
      proj.peak = 150;
      proj.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;
      proj.curve = DVBRIDGE_HDR10_CURVE_BT2390;
      Time(&g, &b.frame, meta, bytes, NULL, times, "as mastered");
      Time(&g, &b.frame, meta, bytes, &sdr, times, "mapped to SDR");
      Time(&g, &b.frame, meta, bytes, &proj, times, "mapped to a projector");
      // ... and that projector's picture, for its reference (refmap ... proj)
      if (dvbridge_render_hdr10_mapping(g.renderer, &proj) && Draw(&g, &b.frame, meta, bytes))
        WriteRgb(prefix, "proj", g.pic);
      dvbridge_render_hdr10_mapping(g.renderer, NULL);
    }
  }
  if (copies && sdrPic)
  {
    // the SDR picture in a texture of its own, as the renderer's would be, copied to the window's framebuffer
    memcpy(g.pic, sdrPic, (size_t)W * H * 16);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, sdrPic);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int bits = 8; bits <= 10; bits += 2)
      for (int limited = 0; limited < 2; ++limited)
      {
        long most = 0;
        const long wrong = CopyCheck(&g, t, bits, limited, &most);
        char what[256];
        snprintf(what, sizeof(what), "the copy to a %d-bit framebuffer, %s range: every value at its nearest code "
                                     "(%ld not, by %ld codes at most)", bits, limited ? "limited" : "full", wrong, most);
        Check(wrong == 0, what);
      }
    glDeleteTextures(1, &t);
  }
  free(sdrPic);
#else
  printf("this bridge has no SDR mapping (DVBRIDGE_SDR_MAPPING)\n");
  Check(0, "the bridge maps a picture to an SDR output");
#endif
  dvbridge_gl_frame_release(&b);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
