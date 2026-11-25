// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): an HDR10 or HLG picture mapped to an SDR output in
// a wide container (dvbridge_hdr10_mapping::gamut of an SDR target: BT.709 as before, BT.2020 or DCI-P3 (D65), what
// videoscreen.hadessdrhdrgamut asks for), on the software OpenGL ES (zink on lavapipe), with the bridge of a tree
// (dvbridge_render_plain()) and the libplacebo of the build. What it draws is read back as floats (the signal, 0 to 1)
// and checked here, with the colour arithmetic of the test's own (the matrices made from the CIE xy of each gamut):
//
//   - the renderer takes an SDR mapping in BT.2020 and in DCI-P3 (before: refused, only BT.709) and refuses one in a
//     gamut that is no container (a display's own primaries, DVBRIDGE_HDR10_GAMUT_CUSTOM);
//   - each picture is within [0, 1] and finite;
//   - the colours well inside BT.709 and below the white come out the same in every container: the picture
//     converted to BT.709 in linear light (BT.1886 with its black at zero, a pure 2.4 power, as the mapping writes) is
//     the BT.709 picture (the tone curve does not depend on the container, the gamut mapping leaves such colours,
//     and a picture written in other primaries than its container's would move them);
//   - the wide containers hold colours that BT.709 cannot: the picture was mapped into the container, not into BT.709
//     and then put in it (which is what a "BT.709 in BT.2020" output does: the display is told BT.2020 and gets
//     nothing outside BT.709);
//   - a source mastered in DCI-P3 (the PQ chart) stays within DCI-P3 in its BT.2020 picture; one of BT.2020 colours
//     (the HLG chart) goes past it there, which its DCI-P3 picture maps into DCI-P3. (DCI-P3 and BT.2020 are not
//     the same picture for a DCI-P3 source: the tone curve takes bright saturated colours past the source's gamut,
//     and each container's gamut mapping brings them back to its own edge.)
//
// Usage: sdrgamut_test <yuv420p10le, 3840x2160, BT.2020 NCL, limited> <chroma location> <pq|hlg>
//                      <max luma> <min luma> <maxcll> <maxfall> <p3|2020|none>
// Prints "ok:"/"FAIL:" lines and "PASSED|FAILED: <n> failed".
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"

#define W 3840
#define H 2160
#define N ((size_t)W * H)

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

// ---------------------------------------------------------------------------------- the test's own colour arithmetic
typedef double M3[3][3];

static void Inverse(const M3 m, M3 out)
{
  const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0],
               h = m[2][1], i = m[2][2];
  const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  out[0][0] = (e * i - f * h) / det;
  out[0][1] = (c * h - b * i) / det;
  out[0][2] = (b * f - c * e) / det;
  out[1][0] = (f * g - d * i) / det;
  out[1][1] = (a * i - c * g) / det;
  out[1][2] = (c * d - a * f) / det;
  out[2][0] = (d * h - e * g) / det;
  out[2][1] = (b * g - a * h) / det;
  out[2][2] = (a * e - b * d) / det;
}

static void Multiply(const M3 a, const M3 b, M3 out)
{
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
}

// RGB to XYZ of a gamut given by its xy (red, green, blue, white)
static void ToXyz(const double xy[8], M3 out)
{
  M3 m, inv;
  for (int c = 0; c < 3; ++c)
  {
    const double x = xy[2 * c], y = xy[2 * c + 1];
    m[0][c] = x / y;
    m[1][c] = 1.0;
    m[2][c] = (1.0 - x - y) / y;
  }
  Inverse(m, inv);
  const double w[3] = {xy[6] / xy[7], 1.0, (1.0 - xy[6] - xy[7]) / xy[7]};
  double s[3];
  for (int r = 0; r < 3; ++r)
    s[r] = inv[r][0] * w[0] + inv[r][1] * w[1] + inv[r][2] * w[2];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      out[r][c] = m[r][c] * s[c];
}

static const double XY709[8] = {0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
static const double XY2020[8] = {0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290};
static const double XYP3[8] = {0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290};

// the matrix from the linear light of one gamut to that of another
static void Between(const double from[8], const double to[8], M3 out)
{
  M3 a, b, binv;
  ToXyz(from, a);
  ToXyz(to, b);
  Inverse(b, binv);
  Multiply(binv, a, out);
}

static void Apply(const M3 m, const double in[3], double out[3])
{
  for (int r = 0; r < 3; ++r)
    out[r] = m[r][0] * in[0] + m[r][1] * in[1] + m[r][2] * in[2];
}

// the signal of the reference SDR target (BT.1886 with its black at zero: a pure 2.4 power) and back
static double Lin(double v) { return pow(v < 0 ? 0 : v, 2.4); }
static double Sig(double l) { return l <= 0 ? 0 : pow(l, 1.0 / 2.4); }

// ---------------------------------------------------------------------------------------------------- drawing
struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
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

// the picture mapped with m into out; 0 where the renderer refuses the mapping or does not draw
static int Draw(struct gl* g, const struct pl_frame* source, const struct dvbridge_plain* plain,
                const struct dvbridge_hdr10_mapping* m, float* out)
{
  if (!dvbridge_render_hdr10_mapping(g->renderer, m))
    return 0;
  if (!dvbridge_render_plain(g->renderer, source, plain, WHOLE, W, H))
    return 0;
  pl_tex tex = dvbridge_render_hdr10_texture(g->renderer);
  const GLuint t = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  return t && ReadFloats(t, out);
}

// within [0, 1] and finite
static int Holds(const float* p, const char* name)
{
  long outside = 0, strange = 0;
  for (size_t i = 0; i < N; ++i)
    for (int c = 0; c < 3; ++c)
    {
      const float v = p[i * 4 + c];
      if (!isfinite(v))
        ++strange;
      else
        outside += v < 0 || v > 1;
    }
  printf("%s: outside [0, 1] %ld, not finite %ld\n", name, outside, strange);
  return !outside && !strange;
}

// the picture in one container converted to another in linear light (the signal of the reference: a pure 2.4 power)
static void Convert(const float* p, size_t i, const M3 m, double lin[3])
{
  const double l[3] = {Lin(p[i * 4]), Lin(p[i * 4 + 1]), Lin(p[i * 4 + 2])};
  Apply(m, l, lin);
}

static int CmpFloat(const void* a, const void* b)
{
  const float x = *(const float*)a, y = *(const float*)b;
  return x < y ? -1 : x > y;
}

// the colours well inside BT.709 of the BT.709 picture (the saturation of its linear light, (max - min) / max, below
// sat, its signal between 0.05 and top: away from the edge of every gamut, where the gamut mapping leaves a colour
// as it is, and below the white, where the tone curve closes every gamut to grey): the difference of the signal of
// another container's picture converted to BT.709 from it, at the 99.9th percentile and at most, and how many
static long InsideApart(const float* p709, const float* other, const M3 toBt709, double sat, double top, double* p999,
                        double* worst)
{
  float* d = malloc(N * sizeof(float));
  long n = 0;
  *worst = 0;
  for (size_t i = 0; d && i < N; ++i)
  {
    const double l[3] = {Lin(p709[i * 4]), Lin(p709[i * 4 + 1]), Lin(p709[i * 4 + 2])};
    const double hi = fmax(l[0], fmax(l[1], l[2])), lo = fmin(l[0], fmin(l[1], l[2]));
    const double s = fmax(p709[i * 4], fmax(p709[i * 4 + 1], p709[i * 4 + 2]));
    if (s < 0.05 || s > top || hi <= 0 || (hi - lo) / hi >= sat)
      continue;
    double lin[3];
    Convert(other, i, toBt709, lin);
    double most = 0;
    for (int c = 0; c < 3; ++c)
      most = fmax(most, fabs(Sig(lin[c]) - p709[i * 4 + c]));
    d[n++] = (float)most;
    *worst = fmax(*worst, most);
  }
  *p999 = 0;
  if (n > 0)
  {
    qsort(d, (size_t)n, sizeof(float), CmpFloat);
    *p999 = d[(size_t)((double)(n - 1) * 0.999)];
  }
  free(d);
  return n;
}

// the pixels of a picture whose colour is outside a gamut (a channel of its linear light in that gamut below -0.002:
// a hundredth of the signal's code 13 of 255 at a pure 2.4 power), with its matrix to that gamut
static long Outside(const float* p, const M3 toGamut)
{
  long n = 0;
  for (size_t i = 0; i < N; ++i)
  {
    double lin[3];
    Convert(p, i, toGamut, lin);
    n += lin[0] < -0.002 || lin[1] < -0.002 || lin[2] < -0.002;
  }
  return n;
}

int main(int argc, char** argv)
{
  if (argc != 9)
    return 2;
  size_t blBytes = 0;
  uint16_t* bl = ReadAll(argv[1], &blBytes);
  const enum AVChromaLocation loc = (enum AVChromaLocation)atoi(argv[2]);
  struct dvbridge_plain plain = {.hlg = !strcmp(argv[3], "hlg"), .full_range = false,
                                 .max_luma = (float)atof(argv[4]), .min_luma = (float)atof(argv[5]),
                                 .max_cll = (float)atof(argv[6]), .max_fall = (float)atof(argv[7])};
  const char* prim = argv[8];
  if (!bl || blBytes != N * 2 * 3 / 2)
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
  float* p709 = malloc(N * 16);
  float* p2020 = malloc(N * 16);
  float* pP3 = malloc(N * 16);
  Check(g.gl && g.renderer && p709 && p2020 && pP3, "libplacebo and the renderer");
  if (!g.gl || !g.renderer || !p709 || !p2020 || !pP3)
    return 1;

  // --- the picture as P010, as Kodi imports a VAAPI surface (as hdrsdr_test.c)
  GLuint tex[2];
  glGenTextures(2, tex);
  {
    uint16_t* y = malloc(N * 2);
    uint16_t* c = malloc(N);
    const uint16_t* u = bl + N;
    const uint16_t* v = u + N / 4;
    for (size_t i = 0; i < N; ++i)
      y[i] = (uint16_t)(bl[i] << 6);
    for (size_t i = 0; i < N / 4; ++i)
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

  // --- the mappings: the reference SDR target, its gamut the container
  float xy[8];
  dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);
  const struct dvbridge_hdr10_mapping m709 = dvbridge_sdr_mapping(!strcmp(prim, "p3") ? xy : NULL);
  struct dvbridge_hdr10_mapping m2020 = m709, mP3 = m709, mCustom = m709;
  m2020.gamut = DVBRIDGE_HDR10_GAMUT_BT2020;
  mP3.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;
  mCustom.gamut = DVBRIDGE_HDR10_GAMUT_CUSTOM;
  dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, mCustom.target_xy);
  Check(dvbridge_hdr10_mapping_valid(&m709) && dvbridge_hdr10_mapping_valid(&m2020) &&
            dvbridge_hdr10_mapping_valid(&mP3),
        "SDR mappings in BT.709, BT.2020 and DCI-P3 (D65) are valid");
  Check(!dvbridge_hdr10_mapping_valid(&mCustom) && !dvbridge_render_hdr10_mapping(g.renderer, &mCustom),
        "an SDR mapping in a display's own gamut (no container a display is told) is refused");
  Check(dvbridge_sdr_primaries(&m709) == PL_COLOR_PRIM_BT_709 &&
            dvbridge_sdr_primaries(&m2020) == PL_COLOR_PRIM_BT_2020 &&
            dvbridge_sdr_primaries(&mP3) == PL_COLOR_PRIM_DISPLAY_P3,
        "the primaries of each container");

  const int d709 = Draw(&g, &b.frame, &plain, &m709, p709);
  const int d2020 = Draw(&g, &b.frame, &plain, &m2020, p2020);
  const int dP3 = Draw(&g, &b.frame, &plain, &mP3, pP3);
  Check(d709, "drawn mapped to SDR in BT.709");
  Check(d2020, "drawn mapped to SDR in BT.2020");
  Check(dP3, "drawn mapped to SDR in DCI-P3 (D65)");
  if (!d709 || !d2020 || !dP3)
  {
    printf("FAILED: %d failed\n", g_failures);
    return 1;
  }
  Check(Holds(p709, "BT.709") && Holds(p2020, "BT.2020") && Holds(pP3, "DCI-P3"),
        "every picture within [0, 1], every value finite");

  M3 b2020To709, p3To709, b709To709, toP3From2020;
  Between(XY2020, XY709, b2020To709);
  Between(XYP3, XY709, p3To709);
  Between(XY709, XY709, b709To709);
  Between(XY2020, XYP3, toP3From2020);

  // --- the colours well inside BT.709, below the white: the same in every container (the tone curve and the
  // signal do not depend on it, and the gamut mapping leaves them; a picture written in other primaries than the
  // container's moves every colour that is not grey)
  for (int k = 0; k < 4; ++k)
  {
    static const double sats[4] = {0.02, 0.1, 0.2, 0.3};
    double a999, aWorst, b999, bWorst;
    const long na = InsideApart(p709, p2020, b2020To709, sats[k], 0.7, &a999, &aWorst);
    const long nb = InsideApart(p709, pP3, p3To709, sats[k], 0.7, &b999, &bWorst);
    printf("inside BT.709, saturation below %.2f, signal 0.05 to 0.7: %ld pixels; from the BT.709 picture, the "
           "BT.2020 one %.6f at 99.9 %% (at most %.6f), the DCI-P3 one %.6f at 99.9 %% (at most %.6f; %ld)\n",
           sats[k], na, a999, aWorst, b999, bWorst, nb);
  }
  {
    double a999, aWorst, b999, bWorst;
    const long n1 = InsideApart(p709, p2020, b2020To709, 0.2, 0.7, &a999, &aWorst);
    InsideApart(p709, pP3, p3To709, 0.2, 0.7, &b999, &bWorst);
    // (measured on the charts: within 0.00013 of the signal at most; a signal written in BT.709 whatever the
    // container, 0.006 to 0.019 at 99.9 %)
    Check(n1 > 100000 && a999 <= 1.0 / 1023.0 && b999 <= 1.0 / 1023.0,
          "the colours well inside BT.709 (saturation below 0.2, signal 0.05 to 0.7) as in BT.709 in both wide "
          "containers: 99.9 % within one code of 1023");
  }

  // --- the wide containers hold colours that BT.709 cannot
  const long out709in709 = Outside(p709, b709To709);
  const long out709in2020 = Outside(p2020, b2020To709);
  const long out709inP3 = Outside(pP3, p3To709);
  const long outP3in2020 = Outside(p2020, toP3From2020);
  printf("pixels outside BT.709: of the BT.709 picture %ld, of the BT.2020 one %ld, of the DCI-P3 one %ld; outside "
         "DCI-P3, of the BT.2020 one %ld\n",
         out709in709, out709in2020, out709inP3, outP3in2020);
  Check(out709in709 == 0, "the BT.709 picture: nothing outside BT.709 (the measure itself)");
  Check(out709in2020 > 1000, "the BT.2020 picture has colours outside BT.709: mapped into BT.2020, not into BT.709");
  Check(out709inP3 > 1000, "the DCI-P3 picture has colours outside BT.709: mapped into DCI-P3, not into BT.709");

  // --- DCI-P3 against BT.2020: a source mastered in DCI-P3 stays within it in BT.2020; one of BT.2020 colours goes
  // past it there, and the DCI-P3 picture holds it within DCI-P3 (by being a signal of [0, 1] in it)
  if (!strcmp(prim, "p3"))
    Check(outP3in2020 < (long)(N / 1000), "a source mastered in DCI-P3: its BT.2020 picture stays (99.9 %) within "
                                          "DCI-P3");
  else
    Check(outP3in2020 > 1000, "a source of BT.2020 colours: its BT.2020 picture has colours outside DCI-P3, which "
                              "the DCI-P3 picture maps into it");

  dvbridge_gl_frame_release(&b);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
