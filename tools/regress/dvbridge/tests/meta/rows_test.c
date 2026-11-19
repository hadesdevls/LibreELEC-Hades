// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: pass 2 of the source transport (dvbridge_source_rows(), tools/dvbridge/dvbridge_source.c)
// against the integer filter that dvbridge_source.h defines, computed here on the CPU, for every chroma location
// that dvbridge_source_setup() takes (eight shifts), three pictures (the raster, a 1920x1080 one, and one of odd
// height at an odd row), three sets of margins, and with and without `all`: every row of 4:2:2 of every column,
// code for code, and 0 where the transport does not read.
//
// The texture of pass 2 has a texel for each row of 4:2:0, its two rows of 4:2:2 in rg and ba (an earlier bridge
// had a texel for each row of 4:2:2, the even rows in the first half of the texture; both gave every code of the
// CPU). Also says how many rows of 4:2:0 a fragment of pass 2 fetches for a pair of rows of 4:2:2. The runner proves
// the checks with mutants of dvbridge_source.c.
//
// usage: rows_test (on a software OpenGL ES: zink on lavapipe)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_source.c"

static int g_failures, g_checks;
static void Check(int ok, const char* what)
{
  ++g_checks;
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static uint32_t g_seed = 20261027u;
static uint32_t Rand(void)
{
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 8;
}

// the params of a chroma location, as dvbridge_source_setup() makes them (its filters only)
static void Params(struct dvbridge_source_params* out, float sx, float sy)
{
  memset(out, 0, sizeof(*out));
  for (int parity = 0; parity < 2; ++parity)
    place(parity, sy, out->vweights[parity], &out->vfirst[parity], &out->vtaps[parity]);
  place(0, sx, out->hweights, &out->hfirst, &out->htaps);
  if (out->htaps == 1 && out->hfirst == 0)
    out->htaps = 0;
  out->bits = 12;
  out->code_max = 4095;
}

static int Clamp(int v, int lo, int hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

// one case: a chroma plane of random codes, pass 2 on the GPU, and the CPU's rows
static long Case(struct dvbridge_source_packer* p, float sx, float sy, int width, int height, int x, int y,
                 const unsigned margins[4], int all, long* compared)
{
  struct dvbridge_source_params params;
  Params(&params, sx, sy);
  const int cw = (width + 1) / 2, ch = (height + 1) / 2, half = (height + 1) / 2;
  int* codes = malloc(sizeof(int) * cw * ch * 2);
  float* plane = malloc(sizeof(float) * cw * ch * 2);
  for (int i = 0; i < cw * ch * 2; ++i)
  {
    codes[i] = (int)(Rand() % 4096);
    plane[i] = codes[i] / 4096.0f; // h / 2^bits: exact
  }
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG32F, cw, ch);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch, GL_RG, GL_FLOAT, plane);
  glBindTexture(GL_TEXTURE_2D, 0);
  const int made = dvbridge_source_rows(p, &params, tex, width, height, x, y, margins, all != 0);
  long wrong = -1;
  if (made)
  {
    // the texture of pass 2, read back
    const int th = half;
    uint32_t* got = malloc(sizeof(uint32_t) * 4 * cw * th);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, p->chroma.framebuffer);
    glReadPixels(0, 0, cw, th, GL_RGBA_INTEGER, GL_UNSIGNED_INT, got);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // the horizontal pass (chroma off the even luma column), then the vertical one, as the header defines them
    int* h = malloc(sizeof(int) * cw * ch * 2);
    for (int yy = 0; yy < ch; ++yy)
      for (int xx = 0; xx < cw; ++xx)
        for (int c = 0; c < 2; ++c)
        {
          if (!params.htaps)
          {
            h[(yy * cw + xx) * 2 + c] = codes[(yy * cw + xx) * 2 + c];
            continue;
          }
          long a = 0;
          for (int k = 0; k < params.htaps; ++k)
            a += (long)params.hweights[k] * codes[(yy * cw + Clamp(xx + params.hfirst + k, 0, cw - 1)) * 2 + c];
          h[(yy * cw + xx) * 2 + c] = (int)((a + (1 << 11)) >> 12);
        }
    const int shift = DVBRIDGE_SOURCE_WEIGHT_BITS + (params.htaps ? DVBRIDGE_SOURCE_FRAC_BITS : 0);
    // (the rows and columns that the transport reads, as dvbridge_source_rows() takes them)
    const int x0 = (int)margins[0], x1 = 3840 - (int)margins[1];
    const int y0 = (int)margins[2], y1 = 2160 - (int)margins[3];
    const int a = (x0 > x ? x0 : x) - x, b = (x1 < x + width ? x1 : x + width) - x;
    const int needed[4] = {all ? 0 : (a + 1) / 2, all ? cw : (b + 1) / 2, all ? 0 : (y0 > y ? y0 : y) - y,
                           all ? height : (y1 < y + height ? y1 : y + height) - y};
    wrong = 0;
    for (int r = 0; r < height; ++r)
      for (int xx = 0; xx < cw; ++xx)
      {
        const int parity = r & 1;
        const int read = xx >= needed[0] && xx < needed[1] && r >= needed[2] && r < needed[3];
        for (int c = 0; c < 2; ++c)
        {
          int want = 0;
          if (read)
          {
            long s = 0;
            for (int k = 0; k < params.vtaps[parity]; ++k)
              s += (long)params.vweights[parity][k] *
                   h[(Clamp((r >> 1) + params.vfirst[parity] + k, 0, ch - 1) * cw + xx) * 2 + c];
            want = Clamp((int)((s + (1L << (shift - 1))) >> shift), 0, params.code_max);
          }
          const uint32_t v = got[((r >> 1) * cw + xx) * 4 + 2 * parity + c];
          wrong += v != (uint32_t)want;
          ++*compared;
        }
      }
    free(h);
    free(got);
  }
  glDeleteTextures(1, &tex);
  free(codes);
  free(plane);
  return wrong;
}

int main(void)
{
  setvbuf(stdout, NULL, _IOLBF, 0);
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLConfig cfg = NULL;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("rows test, %s; renderer: %s | %s\n",
         "the new bridge",
         glGetString(GL_RENDERER), glGetString(GL_VERSION));
  struct dvbridge_source_packer* p = dvbridge_source_packer_create();
  Check(p != NULL, "the packer is made");
  if (!p)
    return 1;

  // the chroma locations: libplacebo's plane shifts (luma samples) of 4:2:0, and two more within the range
  static const struct
  {
    float sx, sy;
    const char* name;
  } loc[] = {{-0.5f, 0.0f, "left (HEVC's default)"},      {-0.5f, -0.5f, "top left (UHD Blu-ray)"},
             {0.0f, 0.0f, "centre"},                      {0.0f, -0.5f, "top"},
             {-0.5f, 0.5f, "bottom left"},                {0.0f, 0.5f, "bottom"},
             {-0.5f, 0.25f, "a quarter row (in range)"}, {0.25f, -1.0f, "the most of the range"}};
  static const struct
  {
    int w, h, x, y;
  } pic[] = {{3840, 2160, 0, 0}, {1920, 1080, 960, 540}, {1280, 537, 256, 811}};
  static const unsigned margins[][4] = {{0, 0, 0, 0}, {0, 0, 276, 276}, {120, 40, 300, 7}};
  long compared = 0;
  for (unsigned l = 0; l < sizeof(loc) / sizeof(*loc); ++l)
  {
    struct dvbridge_source_params params;
    Params(&params, loc[l].sx, loc[l].sy);
    const int lo = params.vfirst[0] < params.vfirst[1] ? params.vfirst[0] : params.vfirst[1];
    const int e0 = params.vfirst[0] + params.vtaps[0], e1 = params.vfirst[1] + params.vtaps[1];
    printf("chroma %s: the filters of the two rows of a pair read %d and %d rows of 4:2:0 from %d and %d; fetched "
           "%d before (each its own), %d now (%d rows from %d); horizontal pass %s\n",
           loc[l].name, params.vtaps[0], params.vtaps[1], params.vfirst[0], params.vfirst[1],
           params.vtaps[0] + params.vtaps[1], (e0 > e1 ? e0 : e1) - lo, (e0 > e1 ? e0 : e1) - lo, lo,
           params.htaps ? "made" : "not made (on the even column)");
    for (unsigned s = 0; s < sizeof(pic) / sizeof(*pic); ++s)
      for (unsigned m = 0; m < sizeof(margins) / sizeof(*margins); ++m)
        for (int all = 0; all < 2; ++all)
        {
          const long wrong = Case(p, loc[l].sx, loc[l].sy, pic[s].w, pic[s].h, pic[s].x, pic[s].y, margins[m], all,
                                  &compared);
          char line[256];
          snprintf(line, sizeof line, "%s, %dx%d at %d,%d, margins %u %u %u %u%s: %s", loc[l].name, pic[s].w, pic[s].h,
                   pic[s].x, pic[s].y, margins[m][0], margins[m][1], margins[m][2], margins[m][3], all ? ", all" : "",
                   wrong < 0 ? "NOT MADE" : wrong ? "codes differ from the CPU's" : "every code is the CPU's");
          if (wrong)
            printf("      %ld codes differ\n", wrong);
          Check(wrong == 0, line);
        }
  }
  printf("codes compared: %ld\n", compared);
  dvbridge_source_packer_destroy(p);
  printf("%d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
