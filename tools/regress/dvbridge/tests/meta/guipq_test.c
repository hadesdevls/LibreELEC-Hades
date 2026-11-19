// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (host, software OpenGL ES): the GUI made PQ or HLG by Kodi's GUI composite shader
// (system/shaders/GLES/2.0/gles_gui_composite.frag) with the LUTs and uniforms of GuiCompositeShaderGLES, as
// CompositeGui() draws it over a picture, measured against the exact conversion in double precision: every code
// 0..255 of a grey ramp and of a red, a green and a blue ramp, opaque, and greys over 128 levels of coverage, at
// GUI whites of 25, 100 and 300 %, for PQ (HDR10, Dolby Vision, the menus in Dolby Vision) and HLG. The picture
// under the GUI is a pattern of floats with the alpha of nothing drawn over: every pixel the GUI does not cover must
// keep its bits. The shader with its exact conversion taken out (the LUTs alone, as the shader of 1.0 RC4 drew the
// GUI; the runner makes it) is measured the same way and must be off by more than 2 codes, and mutations of the
// shader of the tree must fail.
// Usage: guipq_test <the shader without its exact conversion> <shader of the tree> <vertex shader>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64

static int g_failures, g_checks;
static void Check(int ok, const char* what)
{
  g_checks++;
  if (!ok)
  {
    g_failures++;
    printf("FAIL: %s\n", what);
  }
}

static char* ReadFile(const char* path)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* s = malloc(n + 1);
  if (s && fread(s, 1, n, f) != (size_t)n)
  {
    free(s);
    s = NULL;
  }
  if (s)
    s[n] = 0;
  fclose(f);
  return s;
}

// a copy of src with the first `from` replaced by `to` (NULL where it is not there)
static char* Edited(const char* src, const char* from, const char* to)
{
  const char* at = strstr(src, from);
  if (!at)
    return NULL;
  char* out = malloc(strlen(src) - strlen(from) + strlen(to) + 1);
  memcpy(out, src, at - src);
  strcpy(out + (at - src), to);
  strcat(out, at + strlen(from));
  return out;
}

static GLuint Program(const char* vs, const char* fs)
{
  const char* srcs[2] = {vs, fs};
  const GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
  GLuint p = glCreateProgram();
  for (int i = 0; i < 2; i++)
  {
    GLuint s = glCreateShader(types[i]);
    glShaderSource(s, 1, &srcs[i], NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char log[2048];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      printf("shader: %s\n", log);
      return 0;
    }
    glAttachShader(p, s);
  }
  glLinkProgram(p);
  GLint linked = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &linked);
  return linked ? p : 0;
}

// --- GuiCompositeShaderGLES: its LUTs, in floats as it computes them, stored as R16F
static float ForwardPQf(float L)
{
  const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
  float Lm1 = powf(L, m1);
  return powf((c1 + c2 * Lm1) / (1.0f + c3 * Lm1), m2);
}
static float SRGBToLinearf(float v)
{
  return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}
static GLuint Lut(const float* data)
{
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 256, 1, 0, GL_RED, GL_FLOAT, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

// --- the exact conversion, double precision: IEC 61966-2-1, BT.709 to BT.2020 as ITU-R BT.2087 gives it, ST 2084
static const double M709TO2020[3][3] = {{0.627403895934699, 0.329283038377884, 0.0433130656874169},
                                        {0.0690972893582956, 0.919540395075459, 0.0113623155662454},
                                        {0.0163914388751503, 0.0880133078772257, 0.895595253247624}};
static double Srgb(double v)
{
  return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
static double PQ(double L)
{
  const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
               c3 = 2392.0 / 4096 * 32;
  const double y = pow(fmax(L, 0), m1);
  return pow((c1 + c2 * y) / (1 + c3 * y), m2);
}
// the signal of an opaque GUI pixel: PQ at the white (10000 cd/m2 units), or HLG as the shader defines it (Kodi's
// HLG GUI path: the white against 203 cd/m2 on the 1000 cd/m2 reference display, the inverse OOTF of gamma 1.2)
static void Exact(const uint8_t px[3], int hlg, double white, double out[3])
{
  double lin[3], bt[3];
  for (int i = 0; i < 3; i++)
    lin[i] = Srgb(px[i] / 255.0) * (hlg ? white : 1.0);
  for (int i = 0; i < 3; i++)
    bt[i] = M709TO2020[i][0] * lin[0] + M709TO2020[i][1] * lin[1] + M709TO2020[i][2] * lin[2];
  if (!hlg)
  {
    for (int i = 0; i < 3; i++)
      out[i] = PQ(bt[i] * 203.0 * white / 10000.0);
    return;
  }
  const double cspMax = 1000.0 / 203.0, A = 0.17883277, B = 0.28466892, C = 0.55991073, g = 1.2;
  double s[3];
  for (int i = 0; i < 3; i++)
    s[i] = bt[i] / cspMax;
  const double Y = 0.2627 * s[0] + 0.6780 * s[1] + 0.0593 * s[2];
  const double k = 12.0 * pow(fmax(1e-6, Y), (1.0 - g) / g);
  for (int i = 0; i < 3; i++)
  {
    s[i] *= k;
    out[i] = s[i] >= 1.0 ? A * log(fmax(s[i] - B, 1e-6)) + C : 0.5 * sqrt(fmax(s[i], 0.0));
  }
}

struct gl
{
  GLuint gui, target, fb, vbo;
};

static uint8_t g_gui[H][W][4]; // as uploaded: row 0 first (the bottom of Kodi's FBO)
static float g_video[H][W][4];

// the GUI: four ramps of the 256 codes (grey, red, green, blue), opaque, then greys over 128 levels of coverage;
// texel (x, y) of the screen (from the top) is row H-1-y of the FBO, as Kodi's FBO has it
static void MakeGui(void)
{
  memset(g_gui, 0, sizeof(g_gui));
  for (int n = 0; n < W * H; n++)
  {
    const int x = n % W, y = n / W;
    uint8_t* t = g_gui[H - 1 - y][x];
    if (n < 1024)
    {
      const int code = n % 256, ramp = n / 256;
      t[0] = ramp == 0 || ramp == 1 ? code : 0;
      t[1] = ramp == 0 || ramp == 2 ? code : 0;
      t[2] = ramp == 0 || ramp == 3 ? code : 0;
      t[3] = 255;
    }
    else if (n < 1024 + 512)
    {
      static const uint8_t greys[4] = {13, 51, 128, 255};
      const int k = n - 1024;
      t[0] = t[1] = t[2] = greys[k / 128];
      t[3] = (uint8_t)(1 + 2 * (k % 128));
    }
    // the rest stays 0: nothing of the GUI, the picture under it untouched
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
    {
      uint32_t h = (uint32_t)(y * W + x) * 2654435761u;
      for (int c = 0; c < 3; c++)
      {
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        g_video[y][x][c] = (float)(h & 0xffffff) / 16777216.0f;
      }
      g_video[y][x][3] = 2.0f; // (the alpha of a pixel that nothing is drawn over: dvbridge_render.c)
    }
}

struct result
{
  double mostOpaque, mostCoverage;
  int worst, worstChannel; //!< the opaque texel (its index in the ramps) and channel of mostOpaque
  double worstGot, worstExpected;
  long videoChanged;
  double grey[256]; // the 12-bit luma code of each grey, as the converted packer computes it
};

static int Run(struct gl* g, GLuint prog, int hlg, int whitePct, struct result* r)
{
  const float white = whitePct / 100.0f;
  float deg[256], tf[256];
  for (int i = 0; i < 256; i++)
  {
    deg[i] = SRGBToLinearf((float)i / 255) * (hlg ? white : 1.0f);
    tf[i] = ForwardPQf((float)i / 255 * (203.0f * white / 10000.0f));
  }
  GLuint lutD = Lut(deg), lutT = Lut(tf);
  // the picture under the GUI
  glBindTexture(GL_TEXTURE_2D, g->target);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, g_video);
  glBindFramebuffer(GL_FRAMEBUFFER, g->fb);
  glViewport(0, 0, W, H);
  glDisable(GL_SCISSOR_TEST);
  glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "u_samp"), 0);
  glUniform1i(glGetUniformLocation(prog, "u_lutDegamma"), 1);
  glUniform1i(glGetUniformLocation(prog, "u_lutTF"), 2);
  glUniform1f(glGetUniformLocation(prog, "u_ootfGamma"), hlg ? 1.2f : 0.0f);
  // (the uniforms of the exact conversion, where the shader has them, as GuiCompositeShaderGLES::OnEnabled())
  const GLint peak = glGetUniformLocation(prog, "u_pqPeak"), hw = glGetUniformLocation(prog, "u_hlgWhite");
  if (peak >= 0)
    glUniform1f(peak, 203.0f * white / 10000.0f);
  if (hw >= 0)
    glUniform1f(hw, hlg ? white : 1.0f);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, lutD);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, lutT);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g->gui);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  GLfloat proj[16] = {2.0f / W, 0, 0, 0, 0, 2.0f / H, 0, 0, 0, 0, -1, 0, -1.0f, -1.0f, 0, 1};
  glUniformMatrix4fv(glGetUniformLocation(prog, "u_proj"), 1, GL_FALSE, proj);
  const GLint aPos = glGetAttribLocation(prog, "a_pos"), aTex = glGetAttribLocation(prog, "a_tex");
  GLfloat vert[4][2] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
  GLfloat texc[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  GLubyte idx[4] = {0, 1, 3, 2};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, vert);
  glVertexAttribPointer(aTex, 2, GL_FLOAT, GL_FALSE, 0, texc);
  glEnableVertexAttribArray(aPos);
  glEnableVertexAttribArray(aTex);
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);
  glDisableVertexAttribArray(aPos);
  glDisableVertexAttribArray(aTex);
  glDisable(GL_BLEND);
  static float out[H][W][4];
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &lutD);
  glDeleteTextures(1, &lutT);
  if (glGetError() != GL_NO_ERROR)
    return 0;
  memset(r, 0, sizeof(*r));
  for (int n = 0; n < W * H; n++)
  {
    const int x = n % W, y = n / W;
    // the GUI over screen row y lands on row y of the picture (its first row is the top, as libplacebo's)
    const uint8_t* t = g_gui[H - 1 - y][x];
    const float* o = out[y][x];
    if (t[3] == 0)
    {
      r->videoChanged += memcmp(o, g_video[y][x], sizeof(float) * 4) != 0;
      continue;
    }
    double e[3];
    Exact(t, hlg, white, e);
    const double a = t[3] / 255.0, srcA = a * a;
    double most = 0;
    for (int c = 0; c < 3; c++)
    {
      const double expect = e[c] * srcA + g_video[y][x][c] * (1 - srcA);
      most = fmax(most, fabs(o[c] - expect) * 4095.0);
    }
    if (t[3] == 255)
    {
      if (most > r->mostOpaque)
      {
        r->worst = n;
        for (int c = 0; c < 3; c++)
          if (fabs(o[c] - e[c]) * 4095.0 >= most - 1e-9)
          {
            r->worstChannel = c;
            r->worstGot = o[c] * 4095.0;
            r->worstExpected = e[c] * 4095.0;
          }
      }
      r->mostOpaque = fmax(r->mostOpaque, most);
      if (n < 256)
        r->grey[n] = 256.0 + 3504.0 * (0.2627 * o[0] + 0.6780 * o[1] + 0.0593 * o[2]);
    }
    else
      r->mostCoverage = fmax(r->mostCoverage, most);
  }
  return 1;
}

int main(int argc, char** argv)
{
  if (argc != 4)
    return 2;
  char* oldFs = ReadFile(argv[1]);
  char* newFs = ReadFile(argv[2]);
  char* vs = ReadFile(argv[3]);
  if (!oldFs || !newFs || !vs)
    return 1;
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
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
  GLint range[2], precision = 0;
  glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
  printf("fragment highp float: %d bits of precision\n", precision);

  struct gl g = {0};
  MakeGui();
  glGenTextures(1, &g.gui);
  glBindTexture(GL_TEXTURE_2D, g.gui);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_gui);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &g.target);
  glBindTexture(GL_TEXTURE_2D, g.target);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H); // the full-precision picture of the bridge
  glGenFramebuffers(1, &g.fb);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.target, 0);
  Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "an RGBA32F target");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  GLuint oldP = Program(vs, oldFs), newP = Program(vs, newFs);
  Check(oldP && newP, "both shaders compile and link");
  if (!oldP || !newP)
    return 1;
  Check(strstr(newFs, "GL_FRAGMENT_PRECISION_HIGH") != NULL, "the shader of the tree has the exact conversion");

  const int whites[3] = {25, 100, 300};
  const int shown[] = {1, 2, 5, 13, 20, 28, 40, 51, 64, 96, 128, 192, 255};
  for (int hlg = 0; hlg <= 1; hlg++)
    for (int w = 0; w < 3; w++)
    {
      struct result before, after;
      Check(Run(&g, oldP, hlg, whites[w], &before) && Run(&g, newP, hlg, whites[w], &after), "drawn");
      char what[256];
      printf("%s at %d %%: largest difference to the exact conversion, opaque %.3f codes of 4095 (without it %.1f), "
             "with coverage %.3f (%.1f); picture pixels changed %ld (%ld)\n",
             hlg ? "HLG" : "PQ ", whites[w], after.mostOpaque, before.mostOpaque, after.mostCoverage,
             before.mostCoverage, after.videoChanged, before.videoChanged);
      printf("    worst: ramp %d (grey, red, green, blue), code %d, channel %d: %.3f against %.3f\n", after.worst / 256,
             after.worst % 256, after.worstChannel, after.worstGot, after.worstExpected);
      if (w == 1 || !hlg)
      {
        printf("    grey code -> 12-bit luma (packer formula): ");
        for (unsigned i = 0; i < sizeof(shown) / sizeof(shown[0]); i++)
        {
          double e[3];
          const uint8_t px[3] = {shown[i], shown[i], shown[i]};
          Exact(px, hlg, whites[w] / 100.0, e);
          const double ey = 256.0 + 3504.0 * (0.2627 * e[0] + 0.6780 * e[1] + 0.0593 * e[2]);
          printf("%d: %.0f/%.0f (exact %.0f)  ", shown[i], before.grey[shown[i]], after.grey[shown[i]], ey);
        }
        printf("\n");
      }
      snprintf(what, sizeof(what), "%s %d %%: opaque within 0.5 codes of the exact conversion (%.3f)", hlg ? "HLG" : "PQ",
               whites[w], after.mostOpaque);
      Check(after.mostOpaque <= 0.5, what);
      snprintf(what, sizeof(what), "%s %d %%: with coverage within 0.5 codes (%.3f)", hlg ? "HLG" : "PQ", whites[w],
               after.mostCoverage);
      Check(after.mostCoverage <= 0.5, what);
      snprintf(what, sizeof(what), "%s %d %%: no pixel of the picture without GUI changes a bit (%ld)", hlg ? "HLG" : "PQ",
               whites[w], after.videoChanged);
      Check(after.videoChanged == 0 && before.videoChanged == 0, what);
      snprintf(what, sizeof(what), "%s %d %%: without the exact conversion, measured as off by more than 2 codes (%.1f)", hlg ? "HLG" : "PQ",
               whites[w], before.mostOpaque);
      Check(before.mostOpaque > 2.0, what);
    }

  // mutations of the new shader: each must make the measurement fail
  struct
  {
    const char* name;
    const char* from;
    const char* to;
    int hlg, white;
  } mut[] = {
      {"a pure 2.4 power in place of the sRGB EOTF",
       "return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));",
       "return pow(c, vec3(2.4));", 0, 100},
      {"no BT.709 to BT.2020", "  linear = bt709_to_bt2020 * linear;\n", "\n", 0, 100},
      {"the GUI white not taken (PQ)", "result = forwardPQ(linear * u_pqPeak);", "result = forwardPQ(linear * 0.0203);", 0,
       300},
      {"the GUI white not taken (HLG)", "    linear *= u_hlgWhite;\n", "\n", 1, 25},
      {"the LUTs (as without highp)", "#if defined(GL_FRAGMENT_PRECISION_HIGH)", "#if 0", 0, 100},
      {"the alpha not squared", "gl_FragColor = vec4(result, gui.a * gui.a);", "gl_FragColor = vec4(result, gui.a);", 0, 100},
  };
  for (unsigned m = 0; m < sizeof(mut) / sizeof(mut[0]); m++)
  {
    char* fs = Edited(newFs, mut[m].from, mut[m].to);
    char what[256];
    snprintf(what, sizeof(what), "mutation \"%s\" applies", mut[m].name);
    Check(fs != NULL, what);
    if (!fs)
      continue;
    GLuint p = Program(vs, fs);
    struct result r;
    const int ran = p && Run(&g, p, mut[m].hlg, mut[m].white, &r);
    const int caught = !ran || r.mostOpaque > 0.5 || r.mostCoverage > 0.5 || r.videoChanged;
    printf("mutation %-44s caught: %s (opaque %.2f, coverage %.2f)\n", mut[m].name, caught ? "yes" : "NO",
           ran ? r.mostOpaque : -1, ran ? r.mostCoverage : -1);
    snprintf(what, sizeof(what), "mutation \"%s\" is caught", mut[m].name);
    Check(caught, what);
    free(fs);
  }
  printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "ALL PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
