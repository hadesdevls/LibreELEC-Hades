// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (host, software OpenGL ES): a frame of the menus in Dolby Vision, made as Kodi makes
// it (CWinSystemGbmGLESContext::CompositeGui() with native output on): dvbridge_render_graphics() clears the
// transport picture, Kodi's own GUI composite shader (system/shaders/GLES/2.0/gles_gui_composite.*, read from the
// tree, with the LUTs that GuiCompositeShaderGLES makes, written again here) draws a GUI into it with the blending of
// native output, and dvbridge_render_pack() packs it in the converted transport. The output is read back and decoded:
//   - every pixel that the GUI does not cover is the black of the transport (Y 256, Cb and Cr 2048);
//   - the metadata in the low bits of the chroma are the candidate's packets, bit for bit;
//   - each patch of a known GUI colour (opaque and translucent) has the codes that a model of the same pipeline gives
//     (the LUTs in half floats, sampled as GL samples them, and the packer's formula), and the codes of the exact
//     conversion (sRGB, BT.709 to BT.2020, PQ at the GUI's white, BT.2020 YCbCr in limited range) are reported next to
//     them: the difference is that of Kodi's LUTs, the same as for the GUI over any HDR video.
// Usage: dvmenus_render_test <kodi tree>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_core.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_graphics.h"
#include "dvbridge_render.h"
#include "pack_sources.h"

#define W 3840
#define H 2160

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

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo: %s\n", msg);
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

static GLuint Shader(GLenum type, const char* src)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
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
  return s;
}

// --- GuiCompositeShaderGLES's LUTs (xbmc/rendering/gles/GuiCompositeShaderGLES.cpp), in floats as there
static const float M1 = 0.1593017578125f, M2 = 78.84375f, C1 = 0.8359375f, C2 = 18.8515625f, C3 = 18.6875f;
static float ForwardPQ(float L)
{
  float Lm1 = powf(L, M1);
  return powf((C1 + C2 * Lm1) / (1.0f + C3 * Lm1), M2);
}
static float SRGBToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}
#define LUT_SIZE 256

// float to half and back, round to nearest even (the R16F texture)
static float Half(float f)
{
  union { float f; uint32_t u; } v = {f};
  const uint32_t sign = v.u & 0x80000000u;
  v.u ^= sign;
  if (v.f < 6.103515625e-05f) // subnormal half: steps of 2^-24
  {
    const float q = nearbyintf(v.f * 16777216.0f) / 16777216.0f;
    return sign ? -q : q;
  }
  uint32_t u = v.u;
  const uint32_t rest = u & 0x1fffu;
  u &= ~0x1fffu;
  if (rest > 0x1000u || (rest == 0x1000u && (u & 0x2000u)))
    u += 0x2000u;
  v.u = u | sign;
  return v.f;
}

static GLuint LutTexture(const float* data, float* stored)
{
  for (int i = 0; i < LUT_SIZE; i++)
    stored[i] = Half(data[i]);
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, LUT_SIZE, 1, 0, GL_RED, GL_FLOAT, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return glGetError() == GL_NO_ERROR ? t : 0;
}

// GL's linear sampling of a 1D LUT at s (clamp to edge)
static double Sample(const float* lut, double s)
{
  double u = s * LUT_SIZE - 0.5;
  if (u <= 0)
    return lut[0];
  if (u >= LUT_SIZE - 1)
    return lut[LUT_SIZE - 1];
  const int i = (int)floor(u);
  const double t = u - i;
  return lut[i] * (1 - t) + lut[i + 1] * t;
}

static const double BT709_TO_BT2020[3][3] = {
    {0.6274, 0.3293, 0.0433}, {0.0691, 0.9195, 0.0114}, {0.0164, 0.0880, 0.8956}};

struct codes { int y0, y1, cb, cr; };

static struct codes Pack(const double pq[3])
{
  // pack_gles300.frag, for a pair of pixels of the same colour
  const double ys = pq[0] * .2627 + pq[1] * .6780 + pq[2] * .0593;
  struct codes c;
  c.y0 = c.y1 = (int)floor(256.0 + 3504.0 * ys + .5);
  c.cb = (int)floor(2048.0 + 3584.0 * (pq[2] - ys) / 1.8814 + .5);
  c.cr = (int)floor(2048.0 + 3584.0 * (pq[0] - ys) / 1.4746 + .5);
  return c;
}

// the model of Kodi's pipeline: the LUTs as stored and sampled, the matrix, the blend over black (alpha a^2)
static struct codes Model(const uint8_t px[4], const float* degamma, const float* tf)
{
  double lin[3], bt[3], pq[3];
  for (int i = 0; i < 3; i++)
    lin[i] = Sample(degamma, px[i] / 255.0);
  for (int i = 0; i < 3; i++)
    bt[i] = BT709_TO_BT2020[i][0] * lin[0] + BT709_TO_BT2020[i][1] * lin[1] + BT709_TO_BT2020[i][2] * lin[2];
  const double a = px[3] / 255.0;
  for (int i = 0; i < 3; i++)
    pq[i] = Sample(tf, bt[i]) * a * a;
  return Pack(pq);
}

// the exact conversion of the same GUI pixel (double precision), its blending as the shader blends it
static struct codes Exact(const uint8_t px[4], double white)
{
  double lin[3], bt[3], pq[3];
  for (int i = 0; i < 3; i++)
  {
    const double v = px[i] / 255.0;
    lin[i] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
  }
  const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
               c3 = 2392.0 / 4096 * 32;
  const double a = px[3] / 255.0;
  for (int i = 0; i < 3; i++)
  {
    bt[i] = BT709_TO_BT2020[i][0] * lin[0] + BT709_TO_BT2020[i][1] * lin[1] + BT709_TO_BT2020[i][2] * lin[2];
    const double y = pow(fmax(bt[i], 0) * white / 10000.0, m1);
    pq[i] = pow((c1 + c2 * y) / (1 + c3 * y), m2) * a * a;
  }
  return Pack(pq);
}

static unsigned Parity(unsigned v)
{
  v ^= v >> 16;
  v ^= v >> 8;
  v ^= v >> 4;
  v ^= v >> 2;
  v ^= v >> 1;
  return v & 1;
}

struct patch { const char* name; uint8_t px[4]; };
static const struct patch PATCHES[] = {
    {"white", {255, 255, 255, 255}},       {"black (opaque)", {0, 0, 0, 255}},
    {"grey 50 %", {128, 128, 128, 255}},     {"grey 20 %", {51, 51, 51, 255}},
    {"grey 5 %", {13, 13, 13, 255}},         {"code 1", {1, 1, 1, 255}},
    {"code 254", {254, 254, 254, 255}},      {"red", {255, 0, 0, 255}},
    {"green", {0, 255, 0, 255}},             {"blue", {0, 0, 255, 255}},
    {"Estuary blue", {18, 160, 199, 255}},   {"Estuary dark", {28, 32, 40, 255}},
    {"yellow", {255, 255, 0, 255}},          {"white 50 % (FBO a 128)", {128, 128, 128, 128}},
    {"grey a 64", {64, 64, 64, 64}},         {"skin shade a 192", {20, 24, 30, 192}},
};
#define NPATCH (sizeof(PATCHES) / sizeof(PATCHES[0]))
// each patch: 128 x 64 pixels at x = 200 + 160 * (i % 16), y = 200 + 100 * (i / 16), rows from the top
#define PX(i) (200 + 160 * ((i) % 16))
#define PY(i) (200 + 100 * ((i) / 16))

static void Decode(const uint8_t* out, int x, int row, int* y, int* c)
{
  // the output framebuffer is read with its first row at the bottom; the transport's first row is the top
  const uint8_t* p = out + ((size_t)(H - 1 - row) * W + x) * 4;
  *y = (p[1] << 4) | (p[2] & 15);
  *c = (p[0] << 4) | (p[2] >> 4);
}

int main(int argc, char** argv)
{
  if (argc != 2)
    return 2;
  char path[4096];
  snprintf(path, sizeof(path), "%s/system/shaders/GLES/2.0/gles_gui_composite.vert", argv[1]);
  char* vs = ReadFile(path);
  snprintf(path, sizeof(path), "%s/system/shaders/GLES/2.0/gles_gui_composite.frag", argv[1]);
  char* fs = ReadFile(path);
  if (!vs || !fs)
  {
    printf("FAIL: the GUI composite shaders of the tree\n");
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
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
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
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_INFO));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t(*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx,
                                                        .allow_software = true));
  struct dvbridge_renderer* r = gl ? dvbridge_renderer_create(gl->gpu, log) : NULL;
  struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  Check(r && packer, "the renderer and the packer are made");
  if (!r || !packer)
    return 1;

  // the composite program, as Kodi builds it (no KODI_LIMITED_RANGE: native output composes in full range)
  GLuint prog = glCreateProgram();
  GLuint v = Shader(GL_VERTEX_SHADER, vs), f = Shader(GL_FRAGMENT_SHADER, fs);
  glAttachShader(prog, v);
  glAttachShader(prog, f);
  glLinkProgram(prog);
  GLint linked = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  Check(v && f && linked, "Kodi's GUI composite shader compiles and links");
  if (!linked)
    return 1;
  const GLint aPos = glGetAttribLocation(prog, "a_pos"), aTex = glGetAttribLocation(prog, "a_tex");
  glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "u_samp"), 0);
  glUniform1i(glGetUniformLocation(prog, "u_lutDegamma"), 1);
  glUniform1i(glGetUniformLocation(prog, "u_lutTF"), 2);
  glUniform1f(glGetUniformLocation(prog, "u_ootfGamma"), 0.0f);
  const GLint uProj = glGetUniformLocation(prog, "u_proj");
  glUseProgram(0);

  // the GUI: transparent, with the patches; Kodi's FBO holds the top of the screen in its last row
  uint8_t* gui = calloc((size_t)W * H, 4);
  for (unsigned i = 0; i < NPATCH; i++)
    for (int yy = 0; yy < 64; yy++)
      for (int xx = 0; xx < 128; xx++)
        memcpy(gui + ((size_t)(H - 1 - (PY(i) + yy)) * W + PX(i) + xx) * 4, PATCHES[i].px, 4);
  GLuint guiTex;
  glGenTextures(1, &guiTex);
  glBindTexture(GL_TEXTURE_2D, guiTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, gui);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  free(gui);

  // the output: an RGBA8 framebuffer of the raster (the window's back buffer)
  GLuint outTex, outFbo;
  glGenTextures(1, &outTex);
  glBindTexture(GL_TEXTURE_2D, outTex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glGenFramebuffers(1, &outFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
  Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "an RGBA8 3840x2160 output");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  uint8_t* out = malloc((size_t)W * H * 4);

  const int whites[] = {100, 25, 300};
  double frame = 0;
  for (unsigned wi = 0; wi < sizeof(whites) / sizeof(whites[0]); wi++)
  {
    const float white = whites[wi] / 100.0f;
    const double nits = 203.0 * whites[wi] / 100.0;
    float deg[LUT_SIZE], tfl[LUT_SIZE], degS[LUT_SIZE], tfS[LUT_SIZE];
    for (int i = 0; i < LUT_SIZE; i++)
    {
      deg[i] = SRGBToLinear((float)i / (LUT_SIZE - 1));
      tfl[i] = ForwardPQ((float)i / (LUT_SIZE - 1) * (203.0f * white / 10000.0f));
    }
    GLuint lutD = LutTexture(deg, degS), lutT = LutTexture(tfl, tfS);
    Check(lutD && lutT, "the LUTs, in R16F");

    size_t bytes = 0;
    AVDOVIMetadata* meta = dvbridge_graphics_metadata(nits, wi == 0, &bytes);
    frame += 1000.0;
    const int prepared = meta && dvbridge_render_graphics(r, meta, bytes, frame);
    Check(prepared, "dvbridge_render_graphics() prepares the frame");
    pl_tex tex = dvbridge_render_texture(r);
    Check(tex != NULL, "the transport picture is there to draw the GUI into");
    if (!prepared || !tex)
      return 1;
    const GLuint pq = pl_opengl_unwrap(gl->gpu, tex, NULL, NULL, NULL);
    GLuint fb;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pq, 0);
    Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "the transport picture as a target");
    // CompositeGui(): the viewport of the raster, no scissor, the blending of native output, the flipped projection
    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lutD);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lutT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, guiTex);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const float w = W, h = H;
    GLfloat proj[16] = {2.0f / w, 0, 0, 0, 0, -2.0f / h, 0, 0, 0, 0, -1, 0, -1.0f, 1.0f, 0, 1};
    proj[5] = -proj[5];
    proj[13] = -proj[13];
    glUseProgram(prog);
    glUniformMatrix4fv(uProj, 1, GL_FALSE, proj);
    // (the uniforms of the exact conversion, where the shader has them: GuiCompositeShaderGLES::OnEnabled())
    if (glGetUniformLocation(prog, "u_pqPeak") >= 0)
      glUniform1f(glGetUniformLocation(prog, "u_pqPeak"), 203.0f * white / 10000.0f);
    if (glGetUniformLocation(prog, "u_hlgWhite") >= 0)
      glUniform1f(glGetUniformLocation(prog, "u_hlgWhite"), 1.0f);
    GLfloat vert[4][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    GLfloat texc[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    GLubyte idx[4] = {0, 1, 3, 2};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, vert);
    glVertexAttribPointer(aTex, 2, GL_FLOAT, GL_FALSE, 0, texc);
    glEnableVertexAttribArray(aPos);
    glEnableVertexAttribArray(aTex);
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);
    glDisableVertexAttribArray(aPos);
    glDisableVertexAttribArray(aTex);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fb);
    Check(glGetError() == GL_NO_ERROR, "the GUI is composited without a GL error");
    // CDVBridgeGLES::Pack(0, 3840, 2160, true): the window, first row at the bottom
    Check(dvbridge_render_pack(r, packer, outFbo, W, H, true, DVBRIDGE_COVER_NONE), "the frame is packed");
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Check(glGetError() == GL_NO_ERROR, "the output is read back");

    // the metadata bits: the candidate's packets, three times each, in the low bit of the chroma
    unsigned count = 0;
    const uint32_t* words = dvbridge_packets(dvbridge_render_candidate(r), &count);
    long bitsWrong = 0, black = 0, notBlack = 0;
    for (long index = 0; index < (long)W * H; index++)
    {
      const int row = (int)(index / W), x = (int)(index % W);
      int y, c;
      Decode(out, x, row, &y, &c);
      if (index < (long)count * 3072)
      {
        const unsigned packet = index / 3072, bit = index % 1024, word = packet * 32 + bit / 32;
        const unsigned value = (words[word] >> (31 - bit % 32)) & 1u;
        if (((c & 1) ^ Parity(c >> 1) ^ Parity(y)) != value)
          bitsWrong++;
        c &= ~1;
      }
      int inPatch = 0;
      for (unsigned i = 0; i < NPATCH && !inPatch; i++)
        inPatch = x >= PX(i) && x < PX(i) + 128 && row >= PY(i) && row < PY(i) + 64;
      if (!inPatch)
      {
        if (y == 256 && (c == 2048 || (index < (long)count * 3072 && (c & ~1) == 2048)))
          black++;
        else
          notBlack++;
      }
    }
    char what[256];
    snprintf(what, sizeof(what), "white %d %%: the %u packet(s) of the metadata in the chroma, %ld bits wrong", whites[wi],
             count, bitsWrong);
    Check(count >= 1 && bitsWrong == 0, what);
    snprintf(what, sizeof(what), "white %d %%: every pixel the GUI does not cover is the transport's black (%ld not)",
             whites[wi], notBlack);
    Check(notBlack == 0 && black > 0, what);

    int mostModel = 0, mostExact = 0;
    printf("white %d %% (%.2f cd/m2), %u packet(s):\n", whites[wi], nits, count);
    for (unsigned i = 0; i < NPATCH; i++)
    {
      // the middle pair of the patch, an even and an odd pixel
      const int x = PX(i) + 64, row = PY(i) + 32;
      int y0, cb, y1, cr;
      Decode(out, x, row, &y0, &cb);
      Decode(out, x + 1, row, &y1, &cr);
      const struct codes m = Model(PATCHES[i].px, degS, tfS), e = Exact(PATCHES[i].px, nits);
      const int dm = (int)fmax(fmax(abs(y0 - m.y0), abs(y1 - m.y1)), fmax(abs(cb - m.cb), abs(cr - m.cr)));
      const int de = (int)fmax(fmax(abs(y0 - e.y0), abs(y1 - e.y1)), fmax(abs(cb - e.cb), abs(cr - e.cr)));
      if (dm > mostModel)
        mostModel = dm;
      if (de > mostExact)
        mostExact = de;
      // every pixel of the patch has the codes of the middle pair (one colour)
      long differ = 0;
      for (int yy = 0; yy < 64; yy++)
        for (int xx = 0; xx < 128; xx += 2)
        {
          int a, b, c2, d;
          Decode(out, PX(i) + xx, PY(i) + yy, &a, &b);
          Decode(out, PX(i) + xx + 1, PY(i) + yy, &c2, &d);
          differ += a != y0 || b != cb || c2 != y1 || d != cr;
        }
      printf("  %-24s sRGB %3u %3u %3u a %3u: Y %4d %4d Cb %4d Cr %4d | model %4d %4d %4d | exact %4d %4d %4d%s\n",
             PATCHES[i].name, PATCHES[i].px[0], PATCHES[i].px[1], PATCHES[i].px[2], PATCHES[i].px[3], y0, y1, cb, cr,
             m.y0, m.cb, m.cr, e.y0, e.cb, e.cr, differ ? "  (patch not uniform)" : "");
      snprintf(what, sizeof(what), "white %d %%, %s: the patch is one colour (%ld pairs differ)", whites[wi],
               PATCHES[i].name, differ);
      Check(!differ, what);
    }
    // (Kodi's composite shader converts the GUI exactly where it has highp; the model of the
    // LUTs of 1.0 RC4 is printed for comparison)
    printf("  largest difference to the LUT model of 1.0 RC4: %d code(s); to the exact conversion: %d code(s) of 4096\n",
           mostModel, mostExact);
    snprintf(what, sizeof(what), "white %d %%: the codes are those of the exact conversion, within 1 code (%d)",
             whites[wi], mostExact);
    Check(mostExact <= 1, what);
    // the frame is shown: the metadata state goes on (dvbridge_render_commit())
    Check(dvbridge_render_commit(r), "the frame is committed");
    av_free(meta);
    glDeleteTextures(1, &lutD);
    glDeleteTextures(1, &lutT);
  }
  // fail closed: no frame of graphics with Low-Latency, nor without metadata
  {
    size_t bytes = 0;
    AVDOVIMetadata* meta = dvbridge_graphics_metadata(203.0, false, &bytes);
    Check(!dvbridge_render_graphics(r, NULL, 0, 5000.0) && !dvbridge_render_texture(r),
          "no metadata: nothing prepared, no picture to draw into");
    Check(!dvbridge_render_graphics(r, meta, bytes, NAN), "no time: nothing prepared");
    av_free(meta);
  }
  printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "ALL PASSED", g_checks, g_failures);
  dvbridge_gl_packer_destroy(packer);
  dvbridge_renderer_destroy(r);
  pl_opengl_destroy(&gl);
  pl_log_destroy(&log);
  return g_failures ? 1 : 0;
}
