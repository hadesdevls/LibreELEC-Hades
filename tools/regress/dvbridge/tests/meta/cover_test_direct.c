// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: what the bridge measures of what is drawn over the bars of a composed picture
// (dvbridge_source_cover(), tools/dvbridge/dvbridge_source.c) and the target that wraps the window
// (dvbridge_render_set_window()), on a software OpenGL ES (zink on lavapipe).
//
//  cover:  on an RGBA32F picture of alpha 2 with things drawn over it with Kodi's blend functions, for 9 active
//          areas and 9 kinds of drawing (none, the dimmed background of a window, an OSD strip over the bottom, a
//          strip over the top, text, the faintest opacity of an 8-bit GUI over everything, an opacity of 2^-20,
//          something inside the active picture only, 300 random pixels) and 25 single pixels each (every edge,
//          corner and block boundary, and random ones), the edges that the bridge measures against those of the
//          measurement it replaced (one point a pixel of the bars, blended into one pixel: copied here as it was)
//          and against those counted on the CPU from the picture read back;
//  time:   both measurements under the dimmed background and under the OSD strip, over the bars of a 2.39:1
//          picture, and with nothing drawn (5 times each);
//  frames: pictures of a profile 7 FEL stream drawn the composed way with things drawn over them and packed with
//          what is drawn measured, as Kodi draws a frame with GUI elements, in both transports: a hash of the
//          bytes of each and the level 5 block they carry (compared outside with the tree before the change);
//          then frames of one picture with a GUI that stays and one that changes every 6 frames: the time of each,
//          and the GPU programs and GL objects made meanwhile;
//  window: the direct way of the source transport into the default framebuffer, wrapped as Kodi wraps its
//          window, against the same into a texture; and what libplacebo says of a pseudo-texture.
//  gui:    frames with Kodi's GUI composited over them by Kodi's own composite
//          (the shaders of the tree, its LUTs, projection and blend factors, as CompositeGui() has them with native
//          Dolby Vision), from GUI textures of four sizes with 9 drawings and single texels at the boundaries of
//          the tiles: packed as before (the whole picture) and with the picture made where the GUI reaches only
//          (dvbridge_render_set_drawn(), the picture's texture filled with something else first, as a frame
//          before would have left it): the bytes, compared; with PQ subtitles drawn into the picture anywhere
//          after it is made whole (dvbridge_render_complete()); the picture made whole against the whole one,
//          for a capture; and the time of a frame each way.
// usage: cover_test <file> <all|cover|time|frames|window|gui|fuse|guitime> [pictures] [Kodi's shaders (for gui, fuse
// and guitime)]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/cache.h>
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
#include "dvbridge_source.h"

#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI

#include "pack_sources.h"

#define W 3840
#define H 2160

static int g_failures;
static long g_pseudo;   // libplacebo's messages of a pseudo-texture
static long g_messages; // all of its warnings and errors

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
  {
    ++g_messages;
    if (strstr(msg, "pseudo-texture"))
      ++g_pseudo;
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
  }
}

static double Now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

// ---- GL objects that the bridge's own code makes (the --wrap of the link): counted
static long g_compiles, g_links, g_textures, g_storages, g_framebuffers;
void __real_glCompileShader(GLuint shader);
void __wrap_glCompileShader(GLuint shader)
{
  ++g_compiles;
  __real_glCompileShader(shader);
}
void __real_glLinkProgram(GLuint program);
void __wrap_glLinkProgram(GLuint program)
{
  ++g_links;
  __real_glLinkProgram(program);
}
void __real_glGenTextures(GLsizei n, GLuint* textures);
void __wrap_glGenTextures(GLsizei n, GLuint* textures)
{
  g_textures += n;
  __real_glGenTextures(n, textures);
}
void __real_glTexStorage2D(GLenum target, GLsizei levels, GLenum format, GLsizei w, GLsizei h);
void __wrap_glTexStorage2D(GLenum target, GLsizei levels, GLenum format, GLsizei w, GLsizei h)
{
  ++g_storages;
  __real_glTexStorage2D(target, levels, format, w, h);
}
void __real_glGenFramebuffers(GLsizei n, GLuint* framebuffers);
void __wrap_glGenFramebuffers(GLsizei n, GLuint* framebuffers)
{
  g_framebuffers += n;
  __real_glGenFramebuffers(n, framebuffers);
}
static long Made(void)
{
  return g_compiles + g_links + g_textures + g_storages + g_framebuffers;
}

// ---- drawing: the picture (alpha 2) and things over it with Kodi's blend functions
static const char FS_VERTEX[] = "#version 300 es\nvoid main() {\n"
                                " vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                                " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n";
static const char COLOUR_FRAGMENT[] = "#version 300 es\nprecision highp float;\nuniform vec4 c;\nout vec4 color;\n"
                                      "void main() { color = c; }\n";
static GLuint g_colourProgram, g_vao;
static GLint g_colourLoc;

static GLuint Program(const char* vs, const char* fs)
{
  GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), p = glCreateProgram();
  glShaderSource(v, 1, &vs, NULL);
  glCompileShader(v);
  glShaderSource(f, 1, &fs, NULL);
  glCompileShader(f);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[2048] = "";
    glGetProgramInfoLog(p, sizeof(log), NULL, log);
    fprintf(stderr, "program: %s\n", log);
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return ok ? p : 0;
}

// a rectangle of the raster (rows from the top: the picture's first row is its top) in a colour, blended
// (Kodi's GUI composite and overlays: RGB a colour + (1 - a) dst, alpha a + dst (1 - a)) or written
static void Rect(GLuint fbo, int x0, int y0, int x1, int y1, float r, float gr, float b, float a, int blend)
{
  if (x1 <= x0 || y1 <= y0)
    return;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, W, H);
  glUseProgram(g_colourProgram);
  glBindVertexArray(g_vao);
  glUniform4f(g_colourLoc, r, gr, b, a);
  glColorMask(1, 1, 1, 1);
  if (blend)
  {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  else
    glDisable(GL_BLEND);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, y0, x1 - x0, y1 - y0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
}

static GLuint FramebufferOf(GLuint texture)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!complete)
  {
    glDeleteFramebuffers(1, &fb);
    return 0;
  }
  return fb;
}

static uint32_t g_seed = 1;
static uint32_t Random(void)
{
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 8;
}

#define PATTERNS 9
static const char* const PATTERN_NAMES[PATTERNS] = {
    "nothing drawn", "a window's dimmed background", "an OSD strip over the bottom", "a strip over the top",
    "text over the bottom", "the faintest opacity of an 8-bit GUI over everything", "an opacity of 2^-20",
    "something inside the active picture only", "300 random pixels"};

// Kodi's drawing number k over a picture whose active area has these margins
static void Pattern(GLuint fbo, int k, const unsigned m[4])
{
  const int x0 = (int)m[0], y0 = (int)m[2], x1 = W - (int)m[1], y1 = H - (int)m[3];
  switch (k)
  {
    case 1:
      Rect(fbo, 0, 0, W, H, 0.0f, 0.0f, 0.0f, 0.5f, 1);
      break;
    case 2:
      Rect(fbo, 0, H - 300, W, H, 0.1f, 0.1f, 0.12f, 0.75f, 1);
      Rect(fbo, 200, H - 120, 2600, H - 100, 0.9f, 0.9f, 0.9f, 1.0f, 1); // the progress of the seek bar
      break;
    case 3:
      Rect(fbo, 0, 0, W, 100, 0.8f, 0.2f, 0.2f, 1.0f, 1);
      break;
    case 4:
      for (int i = 0; i < 40; ++i)
        Rect(fbo, 600 + 70 * i, H - 140, 600 + 70 * i + 5, H - 131, 1.0f, 1.0f, 1.0f, 1.0f, 1);
      break;
    case 5:
      Rect(fbo, 0, 0, W, H, 1.0f, 1.0f, 1.0f, 1.0f / 255.0f, 1);
      break;
    case 6:
      Rect(fbo, 100, 100, 300, 200, 1.0f, 0.0f, 1.0f, 1.0f / 1048576.0f, 1);
      Rect(fbo, 3500, 2100, 3600, 2150, 1.0f, 0.0f, 1.0f, 1.0f / 1048576.0f, 1);
      break;
    case 7:
      Rect(fbo, x0 + 10, y0 + 10, x1 - 10, y1 - 10, 0.2f, 0.9f, 0.2f, 1.0f, 1);
      break;
    case 8:
      for (int i = 0; i < 300; ++i)
      {
        const int x = (int)(Random() % W), y = (int)(Random() % H);
        Rect(fbo, x, y, x + 1, y + 1, 0.5f, 0.5f, 0.5f, 0.5f, 1);
      }
      break;
    default:
      break;
  }
}

// the edges beyond the margins where the picture read back has something drawn over it (alpha not 2)
static unsigned CpuEdges(GLuint fbo, float* floats, const unsigned m[4])
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, floats);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  unsigned e = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (floats[((size_t)y * W + x) * 4 + 3] != 2.0f)
        e |= (x < (int)m[0] ? 1u : 0u) | (x >= W - (int)m[1] ? 2u : 0u) | (y < (int)m[2] ? 4u : 0u) |
             (y >= H - (int)m[3] ? 8u : 0u);
  return e;
}

// ---- the measurement that dvbridge_source_cover() made before (tools/dvbridge/dvbridge_source.c of an earlier bridge),
// as it was: one point for each pixel of the bars, into a 1x1 target, blended by their maximum
static const char OLD_VERTEX[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D drawn;\n"
    "uniform ivec4 active_rect;\n" /* x0, y0, x1, y1 */
    "flat out vec4 edges;\n"
    "void main()\n"
    "{\n"
    "    int i = gl_VertexID;\n"
    "    int above = 3840 * active_rect.y, below = 3840 * (2160 - active_rect.w);\n"
    "    int left = active_rect.x, sides = left + 3840 - active_rect.z;\n"
    "    ivec2 p;\n"
    "    if (i < above) {\n"
    "        p = ivec2(i % 3840, i / 3840);\n"
    "    } else if (i < above + below) {\n"
    "        i -= above;\n"
    "        p = ivec2(i % 3840, active_rect.w + i / 3840);\n"
    "    } else {\n"
    "        i -= above + below;\n"
    "        int s = i % sides;\n"
    "        p = ivec2(s < left ? s : active_rect.z + s - left, active_rect.y + i / sides);\n"
    "    }\n"
    "    edges = vec4(p.x < active_rect.x, p.x >= active_rect.z, p.y < active_rect.y,\n"
    "                 p.y >= active_rect.w);\n"
    "    bool covered = texelFetch(drawn, p, 0).a != 2.0;\n"
    "    gl_Position = covered ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);\n"
    "    gl_PointSize = 1.0;\n"
    "}\n";
static const char OLD_FRAGMENT[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "flat in vec4 edges;\n"
    "out vec4 color;\n"
    "void main()\n"
    "{\n"
    "    color = edges;\n"
    "}\n";
static GLuint g_oldProgram, g_oldTexture, g_oldFbo, g_oldSampler;
static GLint g_oldDrawn, g_oldActive;

static int OldCover(GLuint layer, const unsigned margins[4], unsigned* edges)
{
  if (!layer || margins[0] + margins[1] >= 3840 || margins[2] + margins[3] >= 2160)
    return 0;
  *edges = 0;
  const int x0 = (int)margins[0], x1 = 3840 - (int)margins[1];
  const int y0 = (int)margins[2], y1 = 2160 - (int)margins[3];
  const int points = 3840 * y0 + 3840 * (2160 - y1) + (x0 + 3840 - x1) * (y1 - y0);
  if (!points)
    return 1;
  glBindFramebuffer(GL_FRAMEBUFFER, g_oldFbo);
  glViewport(0, 0, 1, 1);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_oldProgram);
  glBindVertexArray(g_vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, layer);
  glBindSampler(0, g_oldSampler);
  glUniform1i(g_oldDrawn, 0);
  glUniform4i(g_oldActive, x0, y0, x1, y1);
  glEnable(GL_BLEND);
  glBlendEquation(GL_MAX);
  glBlendFunc(GL_ONE, GL_ONE);
  glDrawArrays(GL_POINTS, 0, points);
  glDisable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  uint8_t rgba[4] = {0};
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  for (int i = 0; i < 4; ++i)
    *edges |= (rgba[i] >= 128 ? 1u : 0u) << i;
  glBindSampler(0, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
  return glGetError() == GL_NO_ERROR;
}

static int SetUpOld(void)
{
  g_oldProgram = Program(OLD_VERTEX, OLD_FRAGMENT);
  if (!g_oldProgram)
    return 0;
  g_oldDrawn = glGetUniformLocation(g_oldProgram, "drawn");
  g_oldActive = glGetUniformLocation(g_oldProgram, "active_rect");
  glGenTextures(1, &g_oldTexture);
  glBindTexture(GL_TEXTURE_2D, g_oldTexture);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
  glBindTexture(GL_TEXTURE_2D, 0);
  g_oldFbo = FramebufferOf(g_oldTexture);
  glGenSamplers(1, &g_oldSampler);
  glSamplerParameteri(g_oldSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(g_oldSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glSamplerParameteri(g_oldSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(g_oldSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return g_oldFbo != 0 && glGetError() == GL_NO_ERROR;
}

// ---- 1. the edges, three ways
static const unsigned MARGINS[][4] = {
    {0, 0, 275, 276},     // 2.39:1, the stream's level 5 block
    {240, 240, 0, 0},     // pillarbox
    {3, 5, 7, 9},         // small and odd, all four
    {241, 239, 276, 276}, // windowbox, odd
    {0, 0, 0, 0},         // no bars
    {0, 0, 1, 0},         // one row of bars
    {1919, 1920, 0, 0},   // one active column
    {0, 0, 1079, 1080},   // one active row
    {16, 17, 15, 16},     // at and beside the boundaries of the blocks
};
#define NMARGINS (int)(sizeof(MARGINS) / sizeof(*MARGINS))

static long g_cases, g_oldWrong, g_newWrong, g_failedCalls;

static void OneCase(struct dvbridge_source_packer* packer, GLuint picture, GLuint fbo, float* floats,
                    const unsigned m[4], const char* what)
{
  const unsigned cpu = CpuEdges(fbo, floats, m);
  unsigned old = 99, now = 99;
  const int oldOk = OldCover(picture, m, &old);
  const int newOk = dvbridge_source_cover(packer, picture, m, &now);
  ++g_cases;
  g_failedCalls += !oldOk + !newOk;
  g_oldWrong += !oldOk || old != cpu;
  g_newWrong += !newOk || now != cpu;
  if (!oldOk || !newOk || old != cpu || now != cpu)
    printf("MISMATCH margins %u %u %u %u, %s: cpu %x, before %x (%s), now %x (%s)\n", m[0], m[1], m[2], m[3], what,
           cpu, old, oldOk ? "measured" : "FAILED", now, newOk ? "measured" : "FAILED");
}

static void CoverPart(struct dvbridge_source_packer* packer, GLuint picture, GLuint fbo, float* floats)
{
  for (int mi = 0; mi < NMARGINS; ++mi)
  {
    const unsigned* m = MARGINS[mi];
    g_seed = 1000u + (unsigned)mi;
    for (int k = 0; k < PATTERNS; ++k)
    {
      Rect(fbo, 0, 0, W, H, 0.25f, 0.5f, 0.75f, 2.0f, 0);
      Pattern(fbo, k, m);
      OneCase(packer, picture, fbo, floats, m, PATTERN_NAMES[k]);
    }
    // single pixels: the corners of the raster, the pixels at either side of each edge, the corners of the active
    // picture, at the boundaries of the blocks, the centre, and random ones
    const int x0 = (int)m[0], y0 = (int)m[2], x1 = W - (int)m[1], y1 = H - (int)m[3];
    int probes[25][2] = {{0, 0},          {W - 1, 0},      {0, H - 1},      {W - 1, H - 1},  {x0 - 1, y0},
                         {x0, y0 - 1},    {x1, y1 - 1},    {x1 - 1, y1},    {x0, y0},        {x1 - 1, y1 - 1},
                         {x0 - 1, y0 - 1}, {x1, y1},       {15, 15},        {16, 16},        {15, 16},
                         {16, 15},        {W / 2, H / 2},  {W / 2, y0 - 1}, {W / 2, y1},     {x0 - 1, H / 2}};
    for (int i = 20; i < 25; ++i)
    {
      probes[i][0] = (int)(Random() % W);
      probes[i][1] = (int)(Random() % H);
    }
    for (int i = 0; i < 25; ++i)
    {
      const int x = probes[i][0], y = probes[i][1];
      if (x < 0 || y < 0 || x >= W || y >= H)
        continue;
      Rect(fbo, 0, 0, W, H, 0.25f, 0.5f, 0.75f, 2.0f, 0);
      Rect(fbo, x, y, x + 1, y + 1, 1.0f, 1.0f, 1.0f, 1.0f / 255.0f, 1);
      char what[64];
      snprintf(what, sizeof(what), "the pixel %d,%d", x, y);
      OneCase(packer, picture, fbo, floats, m, what);
    }
  }
  printf("cover: %ld cases; the measurement before: %ld not the edges counted on the CPU; now: %ld; calls that "
         "failed: %ld\n",
         g_cases, g_oldWrong, g_newWrong, g_failedCalls);
  Check(g_cases > 200 && g_newWrong == 0 && g_failedCalls == 0,
        "the edges measured now are those counted on the CPU from the picture read back, in every case");
  Check(g_oldWrong == 0, "... and those of the measurement before (copied in this test), in every case");
}

// ---- 2. the time of each
static int Compare(const void* a, const void* b)
{
  const double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

static void TimePart(struct dvbridge_source_packer* packer, GLuint picture, GLuint fbo)
{
  const unsigned* m = MARGINS[0];
  static const int kinds[] = {0, 2, 1};
  for (int ki = 0; ki < 3; ++ki)
  {
    const int k = kinds[ki];
    Rect(fbo, 0, 0, W, H, 0.25f, 0.5f, 0.75f, 2.0f, 0);
    Pattern(fbo, k, m);
    double t[2][5];
    unsigned e[2] = {0, 0};
    for (int i = 0; i < 5; ++i)
      for (int which = 0; which < 2; ++which)
      {
        glFinish();
        const double t0 = Now();
        if (which)
          dvbridge_source_cover(packer, picture, m, &e[1]);
        else
          OldCover(picture, m, &e[0]);
        glFinish();
        t[which][i] = (Now() - t0) * 1000.0;
      }
    qsort(t[0], 5, sizeof(double), Compare);
    qsort(t[1], 5, sizeof(double), Compare);
    printf("time: %s over the bars of a 2.39:1 picture (edges %x and %x): the measurement before %.1f ms (median; "
           "least %.1f), now %.1f ms (least %.1f), on this software renderer, the read back included\n",
           PATTERN_NAMES[k], e[0], e[1], t[0][2], t[0][0], t[1][2], t[1][0]);
  }
}

// ---- 3. and 4. pictures of the stream
struct layer
{
  GLuint tex[2];
  int w, h;
  struct dvbridge_gl_plane planes[3];
};

// yuv420p10le to P010
static int Upload(struct layer* l, const AVFrame* f)
{
  if (f->format != AV_PIX_FMT_YUV420P10LE)
    return 0;
  const int w = f->width, h = f->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
  if (!y || !c)
    return 0;
  for (int r = 0; r < h; ++r)
  {
    const uint16_t* s = (const uint16_t*)(f->data[0] + (ptrdiff_t)r * f->linesize[0]);
    for (int x = 0; x < w; ++x)
      y[(size_t)r * w + x] = (uint16_t)(s[x] << 6);
  }
  for (int r = 0; r < ch; ++r)
  {
    const uint16_t* u = (const uint16_t*)(f->data[1] + (ptrdiff_t)r * f->linesize[1]);
    const uint16_t* v = (const uint16_t*)(f->data[2] + (ptrdiff_t)r * f->linesize[2]);
    for (int x = 0; x < cw; ++x)
    {
      c[((size_t)r * cw + x) * 2] = (uint16_t)(u[x] << 6);
      c[((size_t)r * cw + x) * 2 + 1] = (uint16_t)(v[x] << 6);
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  if (!l->tex[0])
    glGenTextures(2, l->tex);
  glBindTexture(GL_TEXTURE_2D, l->tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, y);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, l->tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, cw, ch, 0, GL_RG, GL_UNSIGNED_SHORT, c);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  free(y);
  free(c);
  l->w = w;
  l->h = h;
  l->planes[0] = (struct dvbridge_gl_plane){l->tex[0], w, h};
  l->planes[1] = (struct dvbridge_gl_plane){l->tex[1], cw, ch};
  l->planes[2] = (struct dvbridge_gl_plane){0, 0, 0};
  return glGetError() == GL_NO_ERROR;
}

struct picture
{
  struct layer bl, el;
  int layered;
  AVDOVIMetadata* meta;
  size_t bytes;
  unsigned number;
};

static int Parity(unsigned v)
{
  return __builtin_parity(v);
}

static uint32_t Crc(const uint8_t* p, size_t n)
{
  uint32_t c = 0xffffffff;
  for (size_t j = 0; j < n; j++)
  {
    c ^= (uint32_t)p[j] << 24;
    for (int i = 0; i < 8; i++)
      c = (c << 1) ^ ((c >> 31) ? 0x04c11db7u : 0);
  }
  return c;
}

// the level 5 block, the metadata id and the scene refresh that a transport's bytes carry (the first copy of each
// packet); -1 where a CRC is wrong or no level 5 block is found
static int TransportL5(const uint8_t* bytes, unsigned l5[4], unsigned* id, unsigned* refresh)
{
  uint8_t packet[16][128];
  unsigned count = 0;
  for (unsigned k = 0; k < 16; ++k)
  {
    memset(packet[k], 0, 128);
    for (unsigned b = 0; b < 1024; ++b)
    {
      const size_t index = (size_t)k * 3072 + b;
      const int x = (int)(index % W), y = (int)(index / W);
      const uint8_t* o = bytes + ((size_t)(H - 1 - y) * W + x) * 4;
      const unsigned Y = o[1] << 4 | (o[2] & 15), C = o[0] << 4 | o[2] >> 4;
      packet[k][b / 8] |= (uint8_t)(((C & 1) ^ (unsigned)Parity(C >> 1) ^ (unsigned)Parity(Y)) << (7 - b % 8));
    }
    if (Crc(packet[k], 128))
      return -1;
    count = k + 1;
    const unsigned type = packet[k][0] >> 6;
    if (type == 0 || type == 3)
      break;
  }
  *id = packet[0][1] & 15;
  uint8_t payload[2048];
  const int size = packet[0][3] << 8 | packet[0][4];
  int n = 0;
  for (unsigned i = 0; i < count && n < size; ++i)
  {
    const int start = i ? 3 : 5, room = i ? 121 : 119;
    for (int j = 0; j < room && n < size; ++j)
      payload[n++] = packet[i][start + j];
  }
  if (n != size || size < 71)
    return -1;
  *refresh = payload[1];
  int pos = 71;
  for (unsigned i = 0; i < payload[70] && pos + 5 <= size; ++i)
  {
    const unsigned length = (unsigned)payload[pos] << 24 | (unsigned)payload[pos + 1] << 16 |
                            (unsigned)payload[pos + 2] << 8 | payload[pos + 3];
    const unsigned level = payload[pos + 4];
    pos += 5;
    if (level == 5 && length == 8)
    {
      for (int j = 0; j < 4; ++j)
        l5[j] = (unsigned)payload[pos + 2 * j] << 8 | payload[pos + 2 * j + 1];
      return size;
    }
    pos += (int)length;
  }
  return -1;
}

static uint64_t Hash(const uint8_t* p, size_t n)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < n; ++i)
  {
    h ^= p[i];
    h *= UINT64_C(1099511628211);
  }
  return h;
}

struct gl
{
  pl_opengl gl;
  pl_log log;
  pl_cache cache;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_packer* packer;
  GLuint fbo, color;
  pl_tex scanout, window;
  uint8_t *bytes, *bytesWindow;
};

static int Import(struct gl* g, const struct picture* p, struct dvbridge_gl_frame* b, struct dvbridge_gl_frame* e)
{
  if (!dvbridge_gl_frame_import(b, g->gl->gpu, DVBRIDGE_P010, p->bl.w, p->bl.h, p->bl.planes, AVCHROMA_LOC_TOPLEFT))
    return 0;
  if (p->layered)
  {
    if (!dvbridge_gl_frame_import(e, g->gl->gpu, DVBRIDGE_P010, p->el.w, p->el.h, p->el.planes,
                                  AVCHROMA_LOC_TOPLEFT))
      return 0;
    b->frame.enhancement_layer = &e->frame;
  }
  return 1;
}

static int ReadInto(GLuint fbo, uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

static double g_pts; // microseconds, a frame period apart, as Kodi passes them

// a frame with GUI elements as Kodi draws it: the composed way, the GUI over its picture, packed with what is
// drawn over measured, committed. gui: a pattern (PATTERNS), or 100 + n: the n-th state of a GUI of an OSD
// strip, a progress bar and a label that change every 6 frames
static int ComposedFrame(struct gl* g, const struct picture* p, int gui, uint64_t* hash, unsigned l5[4],
                         unsigned* id, unsigned* refresh, unsigned* edges)
{
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!Import(g, p, &b, &e))
    return 0;
  g_pts += 41708.0;
  const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
  int ok = dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full);
  pl_tex tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
  const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  ok = ok && picture;
  unsigned margins[4] = {0};
  if (ok)
    dvbridge_active_area(dvbridge_render_candidate(g->renderer), margins);
  // (the framebuffer of the renderer's picture, made once: what the bridge makes is counted)
  static GLuint fbOf, fb;
  if (ok && picture != fbOf)
  {
    glDeleteFramebuffers(1, &fb);
    fb = FramebufferOf(picture);
    fbOf = picture;
  }
  ok = ok && fb;
  if (ok)
  {
    if (gui < 100)
      Pattern(fb, gui, margins);
    else
    {
      const int state = gui - 100;
      Rect(fb, 0, H - 300, W, H, 0.1f, 0.1f, 0.12f, 0.75f, 1);
      Rect(fb, 200, H - 120, 200 + 400 * (state + 1), H - 100, 0.9f, 0.9f, 0.9f, 1.0f, 1);
      Rect(fb, 3300 - 37 * state, H - 220, 3600 - 37 * state, H - 160, 1.0f, 1.0f, 1.0f, 1.0f, 1);
    }
    dvbridge_render_texture_drawn(g->renderer);
  }
  if (edges && ok)
  {
    static float* floats;
    if (!floats)
      floats = malloc((size_t)W * H * 16);
    *edges = CpuEdges(fb, floats, margins);
  }
  ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE);
  ok = ok && ReadInto(g->fbo, g->bytes);
  ok = ok && dvbridge_render_commit(g->renderer);
  if (ok && hash)
    *hash = Hash(g->bytes, (size_t)W * H * 4);
  if (ok && l5 && TransportL5(g->bytes, l5, id, refresh) < 0)
    ok = 0;
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
  return ok;
}

static void FramesPart(struct gl* g, struct picture* pictures, unsigned count)
{
  // what is sent, frame by frame, with each drawing over each picture, in both transports
  static const int kinds[] = {0, 1, 2, 3, 4, 5, 7};
  long frames = 0, wrong = 0;
  const long messagesBefore = g_messages;
  for (int source = 1; source >= 0; --source)
  {
    dvbridge_render_use_source_transport(g->renderer, source);
    for (unsigned i = 0; i < count; ++i)
      for (int ki = 0; ki < (int)(sizeof(kinds) / sizeof(*kinds)); ++ki)
      {
        // (each frame the first of its stream: the level 5 block is that of its own drawing, and not one that the
        // frames before hold, below)
        dvbridge_renderer_reset(g->renderer);
        uint64_t hash = 0;
        unsigned l5[4] = {0}, id = 0, refresh = 0, cpu = 0;
        const int ok = ComposedFrame(g, &pictures[i], kinds[ki], &hash, l5, &id, &refresh, &cpu);
        const int sourceWay = ok && dvbridge_render_source(g->renderer, NULL);
        // what the level 5 block should say: the film's active area, taken out on each edge that something was
        // drawn beyond (CPU), where it has bars
        unsigned margins[4] = {0};
        dvbridge_active_area(dvbridge_render_candidate(g->renderer), margins);
        unsigned want[4];
        for (int j = 0; j < 4; ++j)
          want[j] = cpu >> j & 1 ? 0 : margins[j];
        ++frames;
        const int right = ok && !memcmp(want, l5, sizeof(want));
        wrong += !right;
        printf("frame: picture %u, %s transport (%s), %s: %s, bytes %016llx, L5 sent %u %u %u %u (margins %u %u %u %u, "
               "edges drawn over %x on the CPU), metadata id %u, refresh %u: %s\n",
               pictures[i].number, source ? "source" : "converted", sourceWay ? "source" : "converted",
               PATTERN_NAMES[kinds[ki]], ok ? "presented" : "FAILED", (unsigned long long)hash, l5[0], l5[1], l5[2],
               l5[3], margins[0], margins[1], margins[2], margins[3], cpu, id, refresh,
               right ? "as it should be" : "WRONG");
      }
  }
  Check(frames > 0 && wrong == 0,
        "every frame with something drawn over it is presented, and its level 5 block is the film's taken out to "
        "the edges that something is drawn beyond (read back from the packed bytes)");

  // in a stream: an OSD strip over the bottom bar for 6 frames, then nothing for 3 s of film; what is sent, read
  // back from the packed bytes: the edge stays taken out for 2 s after the last frame drawn over, the metadata id
  // changes with the area, and nothing but the first frame is a scene cut
  {
    dvbridge_render_use_source_transport(g->renderer, 1);
    dvbridge_renderer_reset(g->renderer);
    unsigned margins[4] = {0}, first[4] = {0}, cuts = 0, changes = 0, ids = 0, lastId = 0, last[4] = {0};
    int ok = 1, heldFrom = -1, backAt = -1;
    const int frames = 6 + 72;
    for (int f = 0; f < 2 + frames; ++f)
    {
      unsigned l5[4] = {0}, id = 0, refresh = 0;
      const int drawn = f >= 2 && f < 8;
      ok &= ComposedFrame(g, &pictures[0], drawn ? 2 : 0, NULL, l5, &id, &refresh, NULL);
      if (!f)
        dvbridge_active_area(dvbridge_render_candidate(g->renderer), margins), memcpy(first, l5, sizeof(first));
      cuts += f > 0 && refresh;
      if (f > 0 && memcmp(l5, last, sizeof(last)))
      {
        ++changes;
        if (l5[3] == 0 && heldFrom < 0)
          heldFrom = f;
        if (l5[3] == margins[3] && heldFrom >= 0 && backAt < 0)
          backAt = f;
      }
      ids += f > 1 && id != lastId; // (the second frame takes a new one: the first was a scene cut)
      lastId = id;
      memcpy(last, l5, sizeof(last));
    }
    // drawn over until frame 7: taken out while the time is under that of frame 7 and 2 s, back from then on
    int want = 8;
    while ((want - 7) * 41708.0 < 2e6)
      ++want;
    printf("held: an OSD strip over the bottom bar in frames 2 to 7, then nothing: %s; the bottom edge taken out from "
           "frame %d and back at frame %d (the first 2 s after frame 7: %d); the area changed %u times, the metadata "
           "id %u times; %u scene cuts after the first frame (L5 of the film %u %u %u %u)\n",
           ok ? "all presented" : "NOT ALL presented", heldFrom, backAt, want, changes, ids, cuts, first[0], first[1],
           first[2], first[3]);
    Check(ok && margins[3] > 0 && first[3] == margins[3] && heldFrom == 2 && backAt == want && changes == 2 &&
              ids == 2 && cuts == 0,
          "in a stream, an edge drawn over stays taken out for 2 s of film after the last frame drawn over, the "
          "metadata id changes with the area only, and it is no scene cut (read back from the packed bytes)");
  }

  // a GUI that stays and one that changes every 6 frames: the time of a frame, and what is made meanwhile
  dvbridge_render_use_source_transport(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  for (int warm = 0; warm < 2; ++warm)
    ComposedFrame(g, &pictures[0], 100, NULL, NULL, NULL, NULL, NULL);
  for (int changing = 0; changing < 2; ++changing)
  {
    double t[12];
    const long made = Made();
    const int programs = g->cache ? pl_cache_objects(g->cache) : -1;
    const long messages = g_messages;
    int ok = 1;
    for (int f = 0; f < 12; ++f)
    {
      glFinish();
      const double t0 = Now();
      ok &= ComposedFrame(g, &pictures[0], 100 + (changing ? f / 6 + 1 : 0), NULL, NULL, NULL, NULL, NULL);
      glFinish();
      t[f] = (Now() - t0) * 1000.0;
    }
    qsort(t, 12, sizeof(double), Compare);
    printf("sequence: 12 frames of picture %u with a GUI that %s: %s; a frame %.0f ms (median; least %.0f, most "
           "%.0f) on this software renderer; made meanwhile: %ld GL shaders, programs, textures or framebuffers of "
           "the bridge, %d GPU programs of libplacebo; %ld messages of libplacebo\n",
           pictures[0].number, changing ? "changes every 6 frames" : "stays", ok ? "all presented" : "NOT ALL presented",
           t[6], t[0], t[11], Made() - made, programs >= 0 ? pl_cache_objects(g->cache) - programs : -1,
           g_messages - messages);
    Check(ok && Made() == made && (programs < 0 || pl_cache_objects(g->cache) == programs),
          changing ? "a GUI that changes makes nothing anew in the bridge: no GL object, no GPU program"
                   : "a GUI that stays makes nothing anew in the bridge: no GL object, no GPU program");
  }
  Check(g_messages == messagesBefore, "libplacebo logs nothing over all these frames");
}

static void WindowPart(struct gl* g, struct picture* p)
{
  dvbridge_render_use_source_transport(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!Import(g, p, &b, &e))
  {
    Check(0, "the picture is imported");
    return;
  }
  const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
#ifdef DVBRIDGE_RENDER_WINDOW
  dvbridge_render_set_window(g->renderer, g->window);
  const char* tree = "with the window set";
#else
  const char* tree = "a tree without dvbridge_render_set_window()";
#endif
  // into the default framebuffer, as into Kodi's window, and into a texture; the same frame (no commit)
  const long before = g_pseudo, beforeAll = g_messages;
  g_pts += 41708.0;
  const int window = dvbridge_render_packed(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN,
                                            full, g->window, true) &&
                     dvbridge_render_source(g->renderer, NULL) && ReadInto(0, g->bytesWindow);
  const long pseudo = g_pseudo - before;
  const int texture = dvbridge_render_packed(g->renderer, &b.frame, p->meta, p->bytes, g_pts,
                                             p->layered ? g_pts : NAN, full, g->scanout, true) &&
                      dvbridge_render_source(g->renderer, NULL) && ReadInto(g->fbo, g->bytes);
  const int same = window && texture && !memcmp(g->bytes, g->bytesWindow, (size_t)W * H * 4);
  if (texture)
    printf("direct: picture %u, source transport, bytes %016llx\n", p->number,
           (unsigned long long)Hash(g->bytes, (size_t)W * H * 4));
  const long all = g_messages - beforeAll;
  printf("window (%s): drawn directly in the source transport into the window: %s, into a texture: %s; the bytes "
         "%s; libplacebo said %ld times that a pseudo-texture was unwrapped, and gave %ld warnings and errors in "
         "all\n",
         tree, window ? "yes" : "NO", texture ? "yes" : "NO", same ? "the same" : "NOT the same", pseudo, all);
  Check(window && texture && same, "the direct way of the source transport writes into the window what it writes "
                                   "into a texture");
#ifdef DVBRIDGE_RENDER_WINDOW
  Check(pseudo == 0 && all == 0, "libplacebo is not asked to unwrap the window (a pseudo-texture), and logs nothing");
  // the window not set: libplacebo would be asked, and the frame is not packed into it as the source transport
  dvbridge_render_set_window(g->renderer, NULL);
  const long before2 = g_pseudo;
  g_pts += 41708.0;
  const int unset = dvbridge_render_packed(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN,
                                           full, g->window, true);
  const int unsetSource = unset && dvbridge_render_source(g->renderer, NULL);
  printf("window not set (control): drawn %s, in the source transport %s; libplacebo said %ld times that a "
         "pseudo-texture was unwrapped\n",
         unset ? "yes" : "no", unsetSource ? "yes" : "no", g_pseudo - before2);
  Check(!unsetSource && g_pseudo - before2 > 0, "without the window set, a pseudo-texture is not packed into as the "
                                                "source transport, and libplacebo says what it was asked (this test "
                                                "sees it)");
  dvbridge_render_set_window(g->renderer, g->window);
#endif
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
}

// ---- 5. Kodi's GUI over the picture, as CWinSystemGbmGLESContext::CompositeGui() composites it with native Dolby
// Vision: the shaders of the tree (system/shaders/GLES/2.0/gles_gui_composite.*, no defines: the full range of
// native output), the LUTs of CGuiCompositeShaderGLES (256 entries, R16F, linear, clamped; PQ up to 203 nits),
// its projection with the rows the other way up, its texture coordinates, and its blend factors
static GLuint g_kodiProgram, g_lutDegamma, g_lutTF;
static GLint g_kPos = -1, g_kTex = -1, g_kProj = -1, g_kOotf = -1, g_kSamp = -1, g_kLutD = -1, g_kLutT = -1;

static char* ReadText(const char* dir, const char* name)
{
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* text = malloc((size_t)n + 1);
  if (text && fread(text, 1, (size_t)n, f) != (size_t)n)
  {
    free(text);
    text = NULL;
  }
  if (text)
    text[n] = 0;
  fclose(f);
  return text;
}

static float SrgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

static float ForwardPQ(float L)
{
  const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
  const float Lm1 = powf(L, m1);
  return powf((c1 + c2 * Lm1) / (1.0f + c3 * Lm1), m2);
}

static GLuint Lut(const float* data, int n)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, n, 1, 0, GL_RED, GL_FLOAT, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

static int SetUpKodi(const char* shaders)
{
  char* vs = ReadText(shaders, "gles_gui_composite.vert");
  char* fs = ReadText(shaders, "gles_gui_composite.frag");
  if (!vs || !fs)
    return 0;
  g_kodiProgram = Program(vs, fs);
  free(vs);
  free(fs);
  if (!g_kodiProgram)
    return 0;
  g_kPos = glGetAttribLocation(g_kodiProgram, "a_pos");
  g_kTex = glGetAttribLocation(g_kodiProgram, "a_tex");
  g_kProj = glGetUniformLocation(g_kodiProgram, "u_proj");
  g_kOotf = glGetUniformLocation(g_kodiProgram, "u_ootfGamma");
  g_kSamp = glGetUniformLocation(g_kodiProgram, "u_samp");
  g_kLutD = glGetUniformLocation(g_kodiProgram, "u_lutDegamma");
  g_kLutT = glGetUniformLocation(g_kodiProgram, "u_lutTF");
  float degamma[256], tf[256];
  for (int i = 0; i < 256; ++i)
  {
    degamma[i] = SrgbToLinear((float)i / 255.0f);
    tf[i] = ForwardPQ((float)i / 255.0f * (203.0f / 10000.0f));
  }
  g_lutDegamma = Lut(degamma, 256);
  g_lutTF = Lut(tf, 256);
  return g_kPos >= 0 && g_kTex >= 0 && g_kProj >= 0 && g_kSamp >= 0 && g_lutDegamma && g_lutTF &&
         glGetError() == GL_NO_ERROR;
}

static void KodiComposite(GLuint fb, GLuint gui, int w, int h, int blend)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glViewport(0, 0, W, H);
  glDisable(GL_SCISSOR_TEST);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  for (int unit = 0; unit < 3; ++unit)
    glBindSampler(unit, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_lutDegamma);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, g_lutTF);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gui);
  if (blend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  const float fw = (float)w, fh = (float)h;
  GLfloat proj[16] = {2.0f / fw, 0, 0, 0, 0, -2.0f / fh, 0, 0, 0, 0, -1, 0, -1.0f, 1.0f, 0, 1};
  proj[5] = -proj[5];
  proj[13] = -proj[13];
  glUseProgram(g_kodiProgram);
  glUniformMatrix4fv(g_kProj, 1, GL_FALSE, proj);
  glUniform1f(g_kOotf, 0.0f);
  glUniform1i(g_kSamp, 0);
  glUniform1i(g_kLutD, 1);
  glUniform1i(g_kLutT, 2);
  GLfloat vert[4][2] = {{0, 0}, {fw, 0}, {fw, fh}, {0, fh}};
  GLfloat tex[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  GLubyte idx[4] = {0, 1, 3, 2};
  glVertexAttribPointer((GLuint)g_kPos, 2, GL_FLOAT, GL_FALSE, 0, vert);
  glVertexAttribPointer((GLuint)g_kTex, 2, GL_FLOAT, GL_FALSE, 0, tex);
  glEnableVertexAttribArray((GLuint)g_kPos);
  glEnableVertexAttribArray((GLuint)g_kTex);
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);
  glDisableVertexAttribArray((GLuint)g_kPos);
  glDisableVertexAttribArray((GLuint)g_kTex);
  glDisable(GL_BLEND);
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// a GUI texture as Kodi's GUI FBO has it: RGBA8, linear, clamped to its edges, first row at the bottom
static GLuint GuiTexture(int w, int h, const uint8_t* rgba)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// a texel of the GUI, x and y from its top left as the GUI is laid out (the texture's rows the other way up)
static void Texel(uint8_t* rgba, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  if (x < 0 || y < 0 || x >= w || y >= h)
    return;
  uint8_t* t = rgba + ((size_t)(h - 1 - y) * w + x) * 4;
  t[0] = r;
  t[1] = g;
  t[2] = b;
  t[3] = a;
}

static void GuiRect(uint8_t* rgba, int w, int h, double fx0, double fy0, double fx1, double fy1, uint8_t r,
                    uint8_t g, uint8_t b, uint8_t a)
{
  for (int y = (int)(fy0 * h); y < (int)(fy1 * h); ++y)
    for (int x = (int)(fx0 * w); x < (int)(fx1 * w); ++x)
      Texel(rgba, w, h, x, y, r, g, b, a);
}

#define GUI_KINDS 9
static const char* const GUI_NAMES[GUI_KINDS] = {
    "an empty GUI", "a window over a dimmed screen", "an OSD (a bar at the top and one at the bottom)",
    "two lines of subtitles", "the seek bar alone", "text in the middle", "the faintest opacity in a corner",
    "an opacity of 1 in 255 over everything", "200 random texels"};

static void GuiPattern(uint8_t* rgba, int w, int h, int k)
{
  memset(rgba, 0, (size_t)w * h * 4);
  switch (k)
  {
    case 1:
      GuiRect(rgba, w, h, 0, 0, 1, 1, 0, 0, 0, 128);
      GuiRect(rgba, w, h, 0.2, 0.15, 0.8, 0.85, 30, 30, 34, 235);
      GuiRect(rgba, w, h, 0.25, 0.2, 0.6, 0.23, 240, 240, 240, 255);
      break;
    case 2:
      GuiRect(rgba, w, h, 0, 0, 1, 0.08, 10, 10, 10, 160);
      GuiRect(rgba, w, h, 0, 0.8, 1, 1, 10, 10, 10, 200);
      GuiRect(rgba, w, h, 0.05, 0.9, 0.7, 0.91, 230, 230, 230, 255);
      break;
    case 3:
      for (int i = 0; i < 30; ++i)
      {
        GuiRect(rgba, w, h, 0.3 + i * 0.013, 0.90, 0.3 + i * 0.013 + 0.008, 0.92, 255, 255, 255, 255);
        GuiRect(rgba, w, h, 0.32 + i * 0.012, 0.935, 0.32 + i * 0.012 + 0.008, 0.955, 255, 255, 255, 255);
      }
      break;
    case 4:
      GuiRect(rgba, w, h, 0.05, 0.93, 0.95, 0.95, 40, 40, 40, 180);
      GuiRect(rgba, w, h, 0.05, 0.93, 0.5, 0.95, 220, 220, 220, 255);
      break;
    case 5:
      for (int i = 0; i < 20; ++i)
        GuiRect(rgba, w, h, 0.4 + i * 0.01, 0.48, 0.4 + i * 0.01 + 0.006, 0.5, 255, 255, 255, 255);
      break;
    case 6:
      GuiRect(rgba, w, h, 0.97, 0.0, 1.0, 0.03, 255, 255, 255, 1);
      break;
    case 7:
      GuiRect(rgba, w, h, 0, 0, 1, 1, 255, 255, 255, 1);
      break;
    case 9:
    case 10:
      // Estuary's seek bar (DialogSeekBar.xml): osdfade over the bottom 190 of 1080, from nothing to opaque black,
      // the bar of the time across the width, what is played of it, and the times at either end; 10: with two lines
      // of subtitles (3) over it
      for (int r = 0; r < 19; ++r)
        GuiRect(rgba, w, h, 0, 1.0 - (19 - r) * 0.01, 1, 1.0 - (18 - r) * 0.01, 0, 0, 0, (uint8_t)(r * 255 / 18));
      GuiRect(rgba, w, h, 0.02, 0.905, 0.98, 0.92, 255, 255, 255, 128);
      GuiRect(rgba, w, h, 0.02, 0.905, 0.55, 0.92, 18, 160, 199, 255);
      GuiRect(rgba, w, h, 0.02, 0.94, 0.08, 0.965, 240, 240, 240, 255);
      GuiRect(rgba, w, h, 0.92, 0.94, 0.98, 0.965, 240, 240, 240, 255);
      if (k == 10)
        for (int i = 0; i < 30; ++i)
        {
          GuiRect(rgba, w, h, 0.3 + i * 0.013, 0.80, 0.3 + i * 0.013 + 0.008, 0.82, 255, 255, 255, 255);
          GuiRect(rgba, w, h, 0.32 + i * 0.012, 0.835, 0.32 + i * 0.012 + 0.008, 0.855, 255, 255, 255, 255);
        }
      break;
    case 8:
      for (int i = 0; i < 200; ++i)
        Texel(rgba, w, h, (int)(Random() % (unsigned)w), (int)(Random() % (unsigned)h), 200, 100, 50,
              (uint8_t)(1 + Random() % 255));
      break;
    default:
      break;
  }
}

#ifdef DVBRIDGE_RENDER_DRAWN
static long g_guiCases, g_guiWrong, g_guiFailed, g_completeCases, g_completeWrong;
static int g_fillStale = 1; // (not while the frames are timed)
static double g_part[3];    // while timed: the render (with the picture), the GUI's composite, the packing (ms)

// the frame packed as before (drawn 0) or with the picture where the GUI reaches (drawn: the GUI texture); the
// picture's texture filled first with something else, as a frame before would have left it. overlay: PQ
// subtitles drawn into the picture after it is made whole, at that raster rectangle. floats: where the picture
// is copied whole, before the GUI is composited (made whole first, for a capture)
static int GuiFrame(struct gl* g, const struct picture* p, GLuint gui, int w, int h, int drawn, const int* overlay,
                    uint8_t* bytes, float* floats)
{
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!Import(g, p, &b, &e))
    return 0;
  const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
  // what the frame before left in the picture's texture
  pl_tex tex = dvbridge_render_texture(g->renderer);
  static GLuint fbOf, fb;
  if (tex && g_fillStale)
  {
    const GLuint before = pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL);
    if (before != fbOf)
    {
      glDeleteFramebuffers(1, &fb);
      fb = FramebufferOf(before);
      fbOf = before;
    }
    Rect(fb, 0, 0, W, H, 0.3f, 0.6f, 0.9f, 1.0f, 0);
  }
  dvbridge_render_set_drawn(g->renderer, drawn ? gui : 0, w, h);
  glFinish();
  double t0 = Now();
  int ok = dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full);
  glFinish();
  g_part[0] = (Now() - t0) * 1000.0;
  tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
  const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  ok = ok && picture;
  if (ok && picture != fbOf)
  {
    glDeleteFramebuffers(1, &fb);
    fb = FramebufferOf(picture);
    fbOf = picture;
  }
  if (ok && (floats || overlay))
    ok = dvbridge_render_complete(g->renderer);
  if (ok && floats)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, floats);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  if (ok && overlay)
    Rect(fb, overlay[0], overlay[1], overlay[2], overlay[3], 0.7f, 0.7f, 0.2f, 0.8f, 1);
  t0 = Now();
  if (ok)
  {
    KodiComposite(fb, gui, w, h, 1);
    dvbridge_render_texture_drawn(g->renderer);
  }
  glFinish();
  g_part[1] = (Now() - t0) * 1000.0;
  t0 = Now();
  ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE);
  glFinish();
  g_part[2] = (Now() - t0) * 1000.0;
  ok = ok && ReadInto(g->fbo, bytes);
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
  return ok;
}

static long Differ(const uint8_t* a, const uint8_t* b, size_t n)
{
  long d = 0;
  for (size_t i = 0; i < n; ++i)
    d += a[i] != b[i];
  return d;
}

static void GuiPart(struct gl* g, struct picture* pictures, unsigned count, const char* shaders)
{
  if (!SetUpKodi(shaders))
  {
    Check(0, "Kodi's composite of its GUI is made from the shaders of the tree");
    return;
  }
  static const int SIZES[][2] = {{3840, 2160}, {1920, 1080}, {2560, 1440}, {1280, 720}};
  uint8_t* before = malloc((size_t)W * H * 4);
  uint8_t* now = malloc((size_t)W * H * 4);
  float* whole = malloc((size_t)W * H * 16);
  float* completed = malloc((size_t)W * H * 16);
  if (!before || !now || !whole || !completed)
    return;
  const long messages = g_messages;
  for (int source = 1; source >= 0; --source)
  {
    dvbridge_render_use_source_transport(g->renderer, source);
    dvbridge_renderer_reset(g->renderer);
    for (unsigned i = 0; i < count; ++i)
      for (int s = 0; s < 4; ++s)
      {
        const int w = SIZES[s][0], h = SIZES[s][1];
        uint8_t* rgba = malloc((size_t)w * h * 4);
        g_seed = 77u + (unsigned)(s * 31 + i);
        // the drawings, then single texels at the corners and around the boundaries of the tiles of the GUI and of
        // the raster (16 pixels of the raster are 16, 8, 10.7 and 5.3 texels of these GUIs)
        const int probes[][2] = {{0, 0},           {w - 1, h - 1},     {w - 1, 0},          {0, h - 1},
                                 {15, 15},         {16, 16},           {w / 2 - 1, h / 2},  {w / 2, h / 2 - 1},
                                 {w * 16 / 3840, h * 16 / 2160},       {w * 15 / 3840, h * 31 / 2160},
                                 {w * 1919 / 3840, h * 2143 / 2160},   {w * 1904 / 3840 - 1, h * 287 / 2160}};
        const int kinds = GUI_KINDS + (int)(sizeof(probes) / sizeof(*probes));
        for (int k = 0; k < kinds; ++k)
        {
          if (!source && k > 3)
            break; // (the converted transport is not made in tiles: a few, to see that it is the same)
          if (k < GUI_KINDS)
            GuiPattern(rgba, w, h, k);
          else
          {
            memset(rgba, 0, (size_t)w * h * 4);
            Texel(rgba, w, h, probes[k - GUI_KINDS][0], probes[k - GUI_KINDS][1], 255, 255, 255, 1);
          }
          const GLuint gui = GuiTexture(w, h, rgba);
          g_pts += 41708.0;
          // as before, and in the tiles; then, for some, with PQ subtitles over the picture made whole
          // (the picture made whole, for a capture: where the tiles are few, two lines of subtitles, and where
          // they are all, a window over a dimmed screen)
          const int capture = k == 1 || k == 3;
          int ok = GuiFrame(g, &pictures[i], gui, w, h, 0, NULL, before, capture ? whole : NULL) &&
                   GuiFrame(g, &pictures[i], gui, w, h, 1, NULL, now, capture ? completed : NULL);
          const long differ = ok ? Differ(before, now, (size_t)W * H * 4) : -1;
          ++g_guiCases;
          g_guiFailed += !ok;
          g_guiWrong += differ != 0;
          if (differ != 0)
            printf("DIFFER: picture %u, %s transport, GUI %dx%d, %s %d: %ld bytes\n", pictures[i].number,
                   source ? "source" : "converted", w, h, k < GUI_KINDS ? GUI_NAMES[k] : "single texel",
                   k < GUI_KINDS ? k : k - GUI_KINDS, differ);
          if (ok && capture)
          {
            // the picture made whole, against the whole one (a capture reads it)
            ++g_completeCases;
            const int same = !memcmp(whole, completed, (size_t)W * H * 16);
            g_completeWrong += !same;
            if (!same)
              printf("DIFFER: the picture made whole, picture %u, GUI %dx%d\n", pictures[i].number, w, h);
          }
          if (ok && (k == 3 || k == 4) && s < 2)
          {
            // PQ subtitles drawn into the picture over the bars and over the active picture, after it is made whole
            const int overlay[4] = {700, 1500 + 400 * (k - 3), 3100, 2100};
            ok = GuiFrame(g, &pictures[i], gui, w, h, 0, overlay, before, NULL) &&
                 GuiFrame(g, &pictures[i], gui, w, h, 1, overlay, now, NULL);
            const long d = ok ? Differ(before, now, (size_t)W * H * 4) : -1;
            ++g_guiCases;
            g_guiFailed += !ok;
            g_guiWrong += d != 0;
            if (d != 0)
              printf("DIFFER: picture %u, GUI %dx%d, %s with PQ subtitles: %ld bytes\n", pictures[i].number, w,
                     h, GUI_NAMES[k], d);
          }
          dvbridge_render_commit(g->renderer);
          glDeleteTextures(1, &gui);
        }
        free(rgba);
      }
  }
  printf("gui: %ld frames packed as before and with the picture where the GUI reaches: %ld not the same bytes, %ld "
         "not drawn; the picture made whole against the whole one: %ld, %ld not the same; libplacebo said %ld "
         "things\n",
         g_guiCases, g_guiWrong, g_guiFailed, g_completeCases, g_completeWrong, g_messages - messages);
  Check(g_guiCases > 100 && g_guiWrong == 0 && g_guiFailed == 0,
        "with the picture made where the GUI reaches only, every frame has the bytes it has as before");
  Check(g_completeCases > 0 && g_completeWrong == 0,
        "the picture made whole is the whole picture, to the bit (what a capture reads)");

  // the time of a frame each way, with each drawing over the 3840x2160 GUI (5 times each, the median)
  dvbridge_render_use_source_transport(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  uint8_t* rgba = malloc((size_t)3840 * 2160 * 4);
  g_fillStale = 0;
  static const int timed[] = {0, 3, 4, 2, 1};
  {
    // the frame without GUI elements, drawn directly, as Kodi draws it
    struct dvbridge_gl_frame b = {0}, e = {0};
    double t[7];
    for (int n = 0; n < 7 && Import(g, &pictures[0], &b, &e); ++n)
    {
      g_pts += 41708.0;
      const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
      glFinish();
      const double t0 = Now();
      dvbridge_render_packed(g->renderer, &b.frame, pictures[0].meta, pictures[0].bytes, g_pts,
                             pictures[0].layered ? g_pts : NAN, full, g->scanout, true);
      glFinish();
      t[n] = (Now() - t0) * 1000.0;
      dvbridge_render_commit(g->renderer);
      dvbridge_gl_frame_release(&e);
      dvbridge_gl_frame_release(&b);
    }
    qsort(t, 7, sizeof(double), Compare);
    printf("gui time: no GUI, drawn directly: %.1f ms (median of 7)\n", t[3]);
  }
  for (int ti = 0; ti < 5; ++ti)
  {
    GuiPattern(rgba, 3840, 2160, timed[ti]);
    const GLuint gui = GuiTexture(3840, 2160, rgba);
    double t[2][4][7];
    for (int n = 0; n < 7; ++n)
      for (int drawn = 0; drawn < 2; ++drawn)
      {
        g_pts += 41708.0;
        glFinish();
        const double t0 = Now();
        GuiFrame(g, &pictures[0], gui, 3840, 2160, drawn, NULL, now, NULL);
        glFinish();
        t[drawn][3][n] = (Now() - t0) * 1000.0;
        for (int k = 0; k < 3; ++k)
          t[drawn][k][n] = g_part[k];
        dvbridge_render_commit(g->renderer);
      }
    for (int drawn = 0; drawn < 2; ++drawn)
      for (int k = 0; k < 4; ++k)
        qsort(t[drawn][k], 7, sizeof(double), Compare);
    printf("gui time: %s: as before %.1f ms (the render with the picture %.1f, the GUI's composite %.1f, the "
           "packing %.1f); with the picture where the GUI reaches %.1f ms (%.1f, %.1f, %.1f) (medians of 7, this "
           "software renderer, the read back of the frame included in the whole)\n",
           GUI_NAMES[timed[ti]], t[0][3][3], t[0][0][3], t[0][1][3], t[0][2][3], t[1][3][3], t[1][0][3],
           t[1][1][3], t[1][2][3]);
    glDeleteTextures(1, &gui);
  }
  free(rgba);
  free(before);
  free(now);
  free(whole);
  free(completed);
}

// ---- 6. (an idea, measured) the blend of the GUI done by a shader in place of the blend unit: the picture (P), and
// what Kodi's composite writes (its colour C and opacity a, the composite drawn with no blending into a texture
// cleared to 0), put together by a shader as the blend factors have it (C a + P (1 - a), a + 2 (1 - a)), against
// the picture with the GUI composited by the blend unit: how many floats differ, and how many bytes of the packed
// frame
static const char FUSE_FRAGMENT[] =
    "#version 300 es\nprecision highp float;\nuniform highp sampler2D picture, gui;\nout vec4 color;\n"
    "void main() {\n"
    " ivec2 p = ivec2(gl_FragCoord.xy);\n"
    " vec4 d = texelFetch(picture, p, 0), s = texelFetch(gui, p, 0);\n"
    " color = s.a == 0.0 ? d : vec4(s.rgb * s.a + d.rgb * (1.0 - s.a), s.a + d.a * (1.0 - s.a));\n"
    "}\n";

static void FusePart(struct gl* g, struct picture* p, const char* shaders)
{
  if (!g_kodiProgram && !SetUpKodi(shaders))
    return;
  dvbridge_render_use_source_transport(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  const GLuint fuse = Program(FS_VERTEX, FUSE_FRAGMENT);
  uint8_t* rgba = malloc((size_t)3840 * 2160 * 4);
  float* blended = malloc((size_t)W * H * 16);
  float* shaded = malloc((size_t)W * H * 16);
  uint8_t* bytesBlend = malloc((size_t)W * H * 4);
  uint8_t* bytesShade = malloc((size_t)W * H * 4);
  GLuint scratch[2] = {0, 0}; // the copy of the picture, what the composite writes
  glGenTextures(2, scratch);
  for (int i = 0; i < 2; ++i)
  {
    glBindTexture(GL_TEXTURE_2D, scratch[i]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLuint copyFb = FramebufferOf(scratch[0]), guiFb = FramebufferOf(scratch[1]);
  static const int kinds[] = {1, 2, 3};
  for (int ki = 0; ki < 3; ++ki)
  {
    GuiPattern(rgba, 3840, 2160, kinds[ki]);
    const GLuint gui = GuiTexture(3840, 2160, rgba);
    struct dvbridge_gl_frame b = {0}, e = {0};
    const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
    g_pts += 41708.0;
    int ok = Import(g, p, &b, &e) &&
             dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full);
    pl_tex tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
    const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
    const GLuint picFb = picture ? FramebufferOf(picture) : 0;
    ok = ok && picFb && copyFb && guiFb && fuse;
    if (ok)
    {
      // the picture kept; what the composite writes, without blending
      glBindFramebuffer(GL_READ_FRAMEBUFFER, picFb);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFb);
      glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, guiFb);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      // (with blending off the composite writes what it gives)
      KodiComposite(guiFb, gui, 3840, 2160, 0);
      // the blend unit's
      KodiComposite(picFb, gui, 3840, 2160, 1);
      dvbridge_render_texture_drawn(g->renderer);
      glBindFramebuffer(GL_FRAMEBUFFER, picFb);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, blended);
      ok = dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE) &&
           ReadInto(g->fbo, bytesBlend);
    }
    if (ok)
    {
      // the shader's, into the picture's texture again, packed the same way
      glBindFramebuffer(GL_FRAMEBUFFER, picFb);
      glViewport(0, 0, W, H);
      glDisable(GL_BLEND);
      glUseProgram(fuse);
      glBindVertexArray(g_vao);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, scratch[1]);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, scratch[0]);
      glUniform1i(glGetUniformLocation(fuse, "picture"), 0);
      glUniform1i(glGetUniformLocation(fuse, "gui"), 1);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, shaded);
      glBindVertexArray(0);
      glUseProgram(0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      ok = dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE) &&
           ReadInto(g->fbo, bytesShade);
    }
    if (ok)
    {
      long floats = 0, pixels = 0;
      for (size_t i = 0; i < (size_t)W * H; ++i)
      {
        const int d = memcmp(blended + i * 4, shaded + i * 4, 16) != 0;
        pixels += d;
        for (int c = 0; c < 4; ++c)
          floats += memcmp(blended + i * 4 + c, shaded + i * 4 + c, 4) != 0;
      }
      printf("fuse: %s: the GUI blended by a shader against the blend unit: %ld pixels, %ld floats differ; the "
             "packed frames: %ld bytes differ (this software renderer)\n",
             GUI_NAMES[kinds[ki]], pixels, floats, Differ(bytesBlend, bytesShade, (size_t)W * H * 4));
    }
    else
      printf("fuse: %s: NOT DRAWN\n", GUI_NAMES[kinds[ki]]);
    dvbridge_render_commit(g->renderer);
    glDeleteFramebuffers(1, &picFb);
    dvbridge_gl_frame_release(&e);
    dvbridge_gl_frame_release(&b);
    glDeleteTextures(1, &gui);
  }
  glDeleteFramebuffers(1, &copyFb);
  glDeleteFramebuffers(1, &guiFb);
  glDeleteTextures(2, scratch);
  free(rgba);
  free(blended);
  free(shaded);
  free(bytesBlend);
  free(bytesShade);
}
#endif

// ---- 7. the time of a frame as Kodi draws it with GUI elements over the video, with each of the two ways of
// taking what is drawn over the bars: measured (MEASURE, the way that waits) and every edge with bars (BARS, a
// frame ahead). In either tree (the earlier one: the picture made whole and the point measurement; now: the tiles that
// the GUI reaches and the blocks). No read back of the frame: what the GPU is given, to its end (glFinish).
static void GuiTimePart(struct gl* g, struct picture* p, const char* shaders)
{
  if (!SetUpKodi(shaders))
  {
    Check(0, "Kodi's composite of its GUI is made from the shaders of the tree");
    return;
  }
  dvbridge_render_use_source_transport(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  // (the size of the GUI: GUITIME_GUI=<w>x<h>, 3840x2160 if not given)
  int gw = 3840, gh = 2160;
  if (getenv("GUITIME_GUI"))
    sscanf(getenv("GUITIME_GUI"), "%dx%d", &gw, &gh);
  printf("guitime: a GUI of %dx%d\n", gw, gh);
  uint8_t* rgba = malloc((size_t)gw * gh * 4);
  static const int timed[] = {3, 4, 9, 10, 2, 1, 0};
  const int ntimed = (int)(sizeof(timed) / sizeof(*timed));
  static GLuint fbOf, fb;
  int all = 1;
  {
    struct dvbridge_gl_frame b = {0}, e = {0};
    double t[9];
    int n = 0;
    for (; n < 9 && Import(g, p, &b, &e); ++n)
    {
      g_pts += 41708.0;
      const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
      glFinish();
      const double t0 = Now();
      all &= dvbridge_render_packed(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full,
                                    g->scanout, true);
      glFinish();
      t[n] = (Now() - t0) * 1000.0;
      dvbridge_render_commit(g->renderer);
      dvbridge_gl_frame_release(&e);
      dvbridge_gl_frame_release(&b);
    }
    all &= n == 9;
    qsort(t, 9, sizeof(double), Compare);
    printf("guitime: no GUI, drawn directly: %.1f ms (median of 9; least %.1f)\n", t[4], t[0]);
  }
  for (int ti = 0; ti < ntimed; ++ti)
  {
    GuiPattern(rgba, gw, gh, timed[ti]);
    const GLuint gui = GuiTexture(gw, gh, rgba);
    double t[2][9];
    for (int n = 0; n < 9; ++n)
      for (int c = 0; c < 2; ++c)
      {
        struct dvbridge_gl_frame b = {0}, e = {0};
        int ok = Import(g, p, &b, &e);
        g_pts += 41708.0;
        const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
        glFinish();
        const double t0 = Now();
#ifdef DVBRIDGE_RENDER_DRAWN
        dvbridge_render_set_drawn(g->renderer, gui, gw, gh);
#endif
        ok = ok && dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN,
                                       full);
        pl_tex tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
        const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
        if (picture && picture != fbOf)
        {
          glDeleteFramebuffers(1, &fb);
          fb = FramebufferOf(picture);
          fbOf = picture;
        }
        ok = ok && picture && fb;
        if (ok)
        {
          KodiComposite(fb, gui, gw, gh, 1);
          dvbridge_render_texture_drawn(g->renderer);
        }
        ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true,
                                        c ? DVBRIDGE_COVER_BARS : DVBRIDGE_COVER_MEASURE);
        glFinish();
        t[c][n] = (Now() - t0) * 1000.0;
        all &= ok;
        dvbridge_render_commit(g->renderer);
        dvbridge_gl_frame_release(&e);
        dvbridge_gl_frame_release(&b);
      }
    qsort(t[0], 9, sizeof(double), Compare);
    qsort(t[1], 9, sizeof(double), Compare);
    printf("guitime: %s: the edges measured %.1f ms (median of 9; least %.1f), every edge with bars %.1f ms (least "
           "%.1f)\n",
           timed[ti] == 9    ? "Estuary's seek bar"
           : timed[ti] == 10 ? "Estuary's seek bar under two lines of subtitles"
                             : GUI_NAMES[timed[ti]],
           t[0][4], t[0][0], t[1][4], t[1][0]);
    glDeleteTextures(1, &gui);
  }
  free(rgba);
  Check(all, "every frame timed is drawn and packed");
}

// ---- 8. Kodi's PR 29463 (cinemaONE), measured. The GUI FBO of the HDR composite (RGBA8, cleared to 0), filled as
// each way fills it, and what Kodi's composite (the shaders of each tree) then puts out over HDR10 video (into a
// 10-bit window, as CompositeGui() without native Dolby Vision) and over the picture of native Dolby Vision (the
// composed way, packed). Now each GUI draw squares its alpha into the FBO (GUITexture, GUIFont, DrawQuad); with the
// PR each accumulates coverage, and the composite squares it once. The overlays of text subtitles
// (COverlayGlyphGLES) accumulate coverage in both, and the composite of now does not square it. Uniform layers over
// the whole GUI: each case is what one pixel holds. References: the same layers blended in linear light (GUI white
// at 203 nits, as the composite's LUT has it), and as SDR blends them, in sRGB, carried to 203 nits (video up to
// 203 nits only).
enum { PR_TEX, PR_OVL };
struct pr_layer
{
  int kind;
  double grey, a;
};
struct pr_case
{
  const char* name;
  int n;
  struct pr_layer l[3];
};
static const struct pr_case PR_CASES[] = {
    {"an OSD panel, 50 % black", 1, {{PR_TEX, 0.0, 0.5}}},
    {"OSD text edge on the video, 50 % white", 1, {{PR_TEX, 1.0, 0.5}}},
    {"a panel fading in, 30 % grey", 1, {{PR_TEX, 0.5, 0.3}}},
    {"a 50 % white highlight on an opaque panel", 2, {{PR_TEX, 0.2, 1.0}, {PR_TEX, 1.0, 0.5}}},
    {"a 50 % black shadow on an opaque panel", 2, {{PR_TEX, 0.3, 1.0}, {PR_TEX, 0.0, 0.5}}},
    {"an opaque dialog on a 60 % dimmed screen", 2, {{PR_TEX, 0.0, 0.6}, {PR_TEX, 0.1, 1.0}}},
    {"a 50 % white text edge on a 60 % dimmed screen", 2, {{PR_TEX, 0.0, 0.6}, {PR_TEX, 1.0, 0.5}}},
    {"two 50 % black panels", 2, {{PR_TEX, 0.0, 0.5}, {PR_TEX, 0.0, 0.5}}},
    {"a text subtitle edge, 50 % white", 1, {{PR_OVL, 1.0, 0.5}}},
    {"a text subtitle shadow, 50 % black", 1, {{PR_OVL, 0.0, 0.5}}},
    {"a text subtitle on its 50 % shadow", 2, {{PR_OVL, 0.0, 0.5}, {PR_OVL, 1.0, 1.0}}},
    {"a text subtitle edge on a 50 % OSD panel", 2, {{PR_TEX, 0.0, 0.5}, {PR_OVL, 1.0, 0.5}}},
};
#define PR_CASES_N ((int)(sizeof(PR_CASES) / sizeof(*PR_CASES)))
static const double PR_LEVELS[] = {0.0, 1.0, 10.0, 100.0, 203.0, 1000.0, 4000.0}; // nits of the video
#define PR_LEVELS_N ((int)(sizeof(PR_LEVELS) / sizeof(*PR_LEVELS)))

static double PrQ8(double v)
{
  return floor(v * 255.0 + 0.5) / 255.0;
}
static double PrLin(double s)
{
  return s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}
static double PrEnc(double l)
{
  return l <= 0.0031308 ? 12.92 * l : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
static double PrPQ(double nits)
{
  const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
  const double y = pow(nits / 10000.0, m1);
  return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}
static int PrCode(double nits)
{
  return (int)floor(PrPQ(nits) * 1023.0 + 0.5);
}

static int PrHasOpaque(const struct pr_case* c)
{
  for (int i = 0; i < c->n; ++i)
    if (c->l[i].a >= 1.0)
      return 1;
  return 0;
}

// the GUI FBO with a case's layers (pr: 0 now, 1 the PR)
static void PrGui(GLuint fbo, const struct pr_case* c, int pr)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, 1920, 1080);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_colourProgram);
  glBindVertexArray(g_vao);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  for (int i = 0; i < c->n; ++i)
  {
    const struct pr_layer* l = &c->l[i];
    const float v = (float)PrQ8(l->grey);
    glUniform4f(g_colourLoc, v, v, v, (float)PrQ8(l->a));
    if (l->kind == PR_OVL)
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    else if (pr)
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
    else
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  glUseProgram(0);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// the references: in linear light (nits), and as SDR blends (nits; -1 above 203 nits of video)
static void PrRefs(const struct pr_case* c, double video, double* lin, double* sdr)
{
  double L = video, s = PrEnc(video / 203.0);
  for (int i = 0; i < c->n; ++i)
  {
    const double a = PrQ8(c->l[i].a), v = PrQ8(c->l[i].grey);
    L = a * 203.0 * PrLin(v) + (1.0 - a) * L;
    s = a * v + (1.0 - a) * s;
  }
  *lin = L;
  *sdr = video <= 203.0 ? 203.0 * PrLin(s) : -1.0;
}

static void PrPart(struct gl* g, struct picture* p, const char* shadersNow, const char* shadersPr)
{
  GLuint prog[2] = {0, 0};
  const int set = SetUpKodi(shadersNow) && (prog[0] = g_kodiProgram) && SetUpKodi(shadersPr) &&
                  (prog[1] = g_kodiProgram);
  Check(set, "Kodi's composite of its GUI is made from the shaders of each tree");
  if (!set)
    return;
  GLuint gui = 0, window = 0;
  glGenTextures(1, &gui);
  glBindTexture(GL_TEXTURE_2D, gui);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1920, 1080);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &window);
  glBindTexture(GL_TEXTURE_2D, window);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLuint guiFb = FramebufferOf(gui), windowFb = FramebufferOf(window);
  if (!guiFb || !windowFb)
  {
    Check(0, "the GUI FBO and the 10-bit window are made");
    return;
  }

  // HDR10: each case at each level of the video, the code (of 1023) that each way puts out
  int singleSame = 1, opaqueNow = 1, opaquePr = 1;
  printf("pr: HDR10, codes of 1023 (video, now, with the PR, the linear-light reference, the SDR reference)\n");
  for (int ci = 0; ci < PR_CASES_N; ++ci)
  {
    const struct pr_case* c = &PR_CASES[ci];
    int code[2][PR_LEVELS_N], diffMax = 0;
    double errLin[2] = {0, 0}, errSdr[2] = {0, 0};
    int nSdr = 0;
    char line[1024];
    int at = snprintf(line, sizeof(line), "pr: HDR10, %s:", c->name);
    for (int li = 0; li < PR_LEVELS_N; ++li)
    {
      const int videoCode = PrCode(PR_LEVELS[li]);
      for (int pr = 0; pr < 2; ++pr)
      {
        PrGui(guiFb, c, pr);
        glBindFramebuffer(GL_FRAMEBUFFER, windowFb);
        glViewport(0, 0, W, H);
        const float v = (float)videoCode / 1023.0f;
        glClearColor(v, v, v, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_kodiProgram = prog[pr];
        KodiComposite(windowFb, gui, 1920, 1080, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, windowFb);
        GLuint px = 0;
        glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, &px);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        code[pr][li] = (int)(px & 1023u);
      }
      double lin, sdr;
      PrRefs(c, PR_LEVELS[li], &lin, &sdr);
      const int linCode = PrCode(lin), sdrCode = sdr >= 0 ? PrCode(sdr) : -1;
      for (int pr = 0; pr < 2; ++pr)
      {
        errLin[pr] += abs(code[pr][li] - linCode);
        if (sdrCode >= 0)
          errSdr[pr] += abs(code[pr][li] - sdrCode);
      }
      nSdr += sdrCode >= 0;
      diffMax = abs(code[0][li] - code[1][li]) > diffMax ? abs(code[0][li] - code[1][li]) : diffMax;
      if (at < (int)sizeof(line))
        at += snprintf(line + at, sizeof(line) - (size_t)at, " %d: %d %d %d %d;", videoCode, code[0][li],
                       code[1][li], linCode, sdrCode);
    }
    printf("%s the two ways differ by %d at most; mean distance to the linear-light reference: now %.1f, with the "
           "PR %.1f; to the SDR one: now %.1f, with the PR %.1f\n",
           line, diffMax, errLin[0] / PR_LEVELS_N, errLin[1] / PR_LEVELS_N, errSdr[0] / nSdr, errSdr[1] / nSdr);
    if (c->n == 1 && c->l[0].kind == PR_TEX && diffMax > 1)
      singleSame = 0;
    if (PrHasOpaque(c))
      for (int li = 1; li < PR_LEVELS_N; ++li)
      {
        opaqueNow &= code[0][li] == code[0][0];
        opaquePr &= code[1][li] == code[1][0];
      }
  }
  Check(singleSame, "HDR10: a single layer of the GUI comes out as now with the PR, to 1 code in 1023");
  Check(opaquePr, "HDR10: with the PR no video shows through a GUI with an opaque layer");
  printf("pr: HDR10: now, video %s through a GUI with an opaque layer\n", opaqueNow ? "does not show" : "SHOWS");

  // native Dolby Vision, the composed way: the codes of the transport (12 bits) that the two ways differ in
  printf("pr: native Dolby Vision (the composed way, picture %u), what the PR changes in the transport\n", p->number);
  uint8_t* bytes[2] = {malloc((size_t)W * H * 4), malloc((size_t)W * H * 4)};
  int dvSingleSame = 1, dvOk = 1;
  static GLuint fbOf, fb;
  for (int ci = 0; ci < PR_CASES_N && bytes[0] && bytes[1]; ++ci)
  {
    const struct pr_case* c = &PR_CASES[ci];
    for (int pr = 0; pr < 2; ++pr)
    {
      struct dvbridge_gl_frame b = {0}, e = {0};
      int ok = Import(g, p, &b, &e);
      dvbridge_renderer_reset(g->renderer);
      g_pts = 41708.0 * 1000.0;
      const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
      ok = ok && dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full);
      pl_tex tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
      const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
      if (picture && picture != fbOf)
      {
        glDeleteFramebuffers(1, &fb);
        fb = FramebufferOf(picture);
        fbOf = picture;
      }
      ok = ok && picture && fb;
      if (ok)
      {
        PrGui(guiFb, c, pr);
        g_kodiProgram = prog[pr];
        KodiComposite(fb, gui, 1920, 1080, 1);
        dvbridge_render_texture_drawn(g->renderer);
      }
      ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE) &&
           ReadInto(g->fbo, bytes[pr]);
      dvOk &= ok;
      dvbridge_gl_frame_release(&e);
      dvbridge_gl_frame_release(&b);
    }
    long pixels = 0;
    int dyMax = 0, dcMax = 0;
    double dySum = 0;
    for (size_t i = 0; i < (size_t)W * H; ++i)
    {
      const uint8_t *x = bytes[0] + i * 4, *y = bytes[1] + i * 4;
      const int y0 = x[1] << 4 | (x[2] & 15), y1 = y[1] << 4 | (y[2] & 15);
      const int c0 = (x[0] << 4 | x[2] >> 4) >> 1, c1 = (y[0] << 4 | y[2] >> 4) >> 1; // (its last bit: metadata)
      const int dy = abs(y0 - y1), dc = abs(c0 - c1) * 2;
      pixels += dy || dc;
      dyMax = dy > dyMax ? dy : dyMax;
      dcMax = dc > dcMax ? dc : dcMax;
      dySum += dy;
    }
    printf("pr: native Dolby Vision, %s: %ld of %d pixels differ; luma by %d codes of 4095 at most (mean %.2f), "
           "chroma by %d\n",
           c->name, pixels, W * H, dyMax, dySum / ((double)W * H), dcMax);
    if (c->n == 1 && c->l[0].kind == PR_TEX && (dyMax > 4 || dcMax > 4))
      dvSingleSame = 0;
  }
  Check(dvOk, "native Dolby Vision: every frame is drawn and packed both ways");
  Check(dvSingleSame, "native Dolby Vision: a single layer of the GUI comes out as now with the PR, to 4 codes in "
                      "4095 (an eighth-bit step of the FBO)");
  free(bytes[0]);
  free(bytes[1]);
  glDeleteFramebuffers(1, &guiFb);
  glDeleteFramebuffers(1, &windowFb);
  glDeleteTextures(1, &gui);
  glDeleteTextures(1, &window);
}

// ---- 9. The GUI's own drawing at 1920x1080 and at 3840x2160 (the size of Kodi's GUI follows the mode on GBM):
// a frame of a GUI like Estuary's home (a full-screen picture, two translucent bars, 400 small blended quads for
// text and icons) into an RGBA8 target, then, as with HDR video, Kodi's composite of it into the 10-bit window. A
// proxy of the fill, not Kodi's own drawing (no Kodi here). Medians of 31 frames, the GPU's work to its end.
static const char TEX_FRAGMENT[] = "#version 300 es\nprecision highp float;\nuniform sampler2D t;\nuniform vec2 size;\n"
                                   "out vec4 color;\nvoid main() { color = texture(t, gl_FragCoord.xy / size); }\n";

static void GuiResPart(const char* shaders)
{
  if (!SetUpKodi(shaders))
  {
    Check(0, "Kodi's composite of its GUI is made from the shaders of the tree");
    return;
  }
  const GLuint texProgram = Program(FS_VERTEX, TEX_FRAGMENT);
  uint8_t* rgba = malloc((size_t)1920 * 1080 * 4);
  for (size_t i = 0; rgba && i < (size_t)1920 * 1080; ++i)
  {
    rgba[i * 4] = (uint8_t)(i * 7);
    rgba[i * 4 + 1] = (uint8_t)(i / 1920);
    rgba[i * 4 + 2] = (uint8_t)(i % 1920);
    rgba[i * 4 + 3] = 255;
  }
  const GLuint fanart = rgba ? GuiTexture(1920, 1080, rgba) : 0;
  free(rgba);
  GLuint window = 0;
  glGenTextures(1, &window);
  glBindTexture(GL_TEXTURE_2D, window);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLuint windowFb = FramebufferOf(window);
  int ok = texProgram && fanart && windowFb;
  static const int SIZES[][2] = {{1920, 1080}, {3840, 2160}};
  for (int s = 0; s < 2 && ok; ++s)
  {
    const int w = SIZES[s][0], h = SIZES[s][1];
    GLuint gui = 0;
    glGenTextures(1, &gui);
    glBindTexture(GL_TEXTURE_2D, gui);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    const GLuint guiFb = FramebufferOf(gui);
    double draw[31], comp[31];
    for (int n = 0; n < 31 && guiFb; ++n)
    {
      glFinish();
      double t0 = Now();
      glBindFramebuffer(GL_FRAMEBUFFER, guiFb);
      glViewport(0, 0, w, h);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindVertexArray(g_vao);
      glUseProgram(texProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, fanart);
      glUniform1i(glGetUniformLocation(texProgram, "t"), 0);
      glUniform2f(glGetUniformLocation(texProgram, "size"), (float)w, (float)h);
      glDisable(GL_BLEND);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glUseProgram(g_colourProgram);
      glEnable(GL_BLEND);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
      glEnable(GL_SCISSOR_TEST);
      glUniform4f(g_colourLoc, 0.0f, 0.0f, 0.0f, 0.6f);
      glScissor(0, 0, w, h / 4);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glScissor(0, h - h / 8, w, h / 8);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glUniform4f(g_colourLoc, 1.0f, 1.0f, 1.0f, 0.8f);
      for (int q = 0; q < 400; ++q)
      {
        // (a glyph or an icon: 20 x 30 of the 1920 x 1080 layout, scaled)
        const int qx = (q % 40) * w / 40, qy = (q / 40) * h / 10;
        glScissor(qx, qy, 20 * w / 1920, 30 * h / 1080);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_BLEND);
      glFinish();
      draw[n] = (Now() - t0) * 1000.0;
      t0 = Now();
      KodiComposite(windowFb, gui, w, h, 1);
      glFinish();
      comp[n] = (Now() - t0) * 1000.0;
    }
    qsort(draw, 31, sizeof(double), Compare);
    qsort(comp, 31, sizeof(double), Compare);
    printf("guires: a GUI of %dx%d: its drawing %.2f ms (median of 31; least %.2f), its composite over HDR video into "
           "the 3840x2160 window %.2f ms (least %.2f)\n",
           w, h, draw[15], draw[0], comp[15], comp[0]);
    ok &= guiFb != 0 && glGetError() == GL_NO_ERROR;
    glDeleteFramebuffers(1, &guiFb);
    glDeleteTextures(1, &gui);
  }
  Check(ok, "the GUI is drawn and composited at both sizes");
}

static int Decode(const char* file, struct picture* pictures, unsigned wanted)
{
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, file, NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 0;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVStream* stream = fc->streams[index];
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(base, stream->codecpar);
  base->pkt_timebase = stream->time_base;
  base->thread_count = 16;
  base->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 0;
  struct dvbridge_fel* fel = dvbridge_fel_create(stream->codecpar, stream->time_base);
  fel->decoder->get_format = avcodec_default_get_format;
  fel->decoder->thread_count = 16;
  fel->decoder->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(fel->decoder, codec, NULL) < 0)
    return 0;
  fel->opened = true;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* pending[64];
  unsigned numPending = 0, number = 0, got = 0;
  int end = 0, ok = 1;
  while (!end && ok && got < wanted)
  {
    const int read = av_read_frame(fc, packet);
    if (read >= 0 && packet->stream_index != index)
    {
      av_packet_unref(packet);
      continue;
    }
    end = read < 0;
    if (!end)
    {
      avcodec_send_packet(base, packet);
      if (!dvbridge_fel_submit(fel, packet))
        ok = 0;
      av_packet_unref(packet);
    }
    else
    {
      avcodec_send_packet(base, NULL);
      dvbridge_fel_drain(fel);
    }
    while (ok && avcodec_receive_frame(base, frame) >= 0)
    {
      pending[numPending++] = av_frame_clone(frame);
      av_frame_unref(frame);
      while (numPending && got < wanted)
      {
        AVFrame* b = pending[0];
        AVFrame* lay = NULL;
        const int result = dvbridge_fel_take(fel, b->pts, &lay);
        if (result == 0 && !end)
          break;
        const unsigned n = number++;
        // pictures 3, 27, 51...: those of the other tests of the bridge
        if (n >= 3 && (n - 3) % 24 == 0)
        {
          const AVFrame* from = b;
          if (lay && av_frame_get_side_data(lay, AV_FRAME_DATA_DOVI_METADATA))
            from = lay;
          const AVFrameSideData* sd = av_frame_get_side_data(from, AV_FRAME_DATA_DOVI_METADATA);
          struct picture* p = &pictures[got];
          if (sd && Upload(&p->bl, b) && (!lay || Upload(&p->el, lay)))
          {
            p->layered = lay != NULL;
            p->meta = av_malloc(sd->size);
            memcpy(p->meta, sd->data, sd->size);
            p->bytes = sd->size;
            p->number = n;
            ++got;
          }
        }
        av_frame_free(&lay);
        av_frame_free(&b);
        memmove(pending, pending + 1, --numPending * sizeof(*pending));
      }
    }
  }
  return ok && got == wanted;
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  const char* mode = argv[2];
  const unsigned wanted = argc > 3 ? (unsigned)atoi(argv[3]) : 2;
  const int all = !strcmp(mode, "all");
  av_log_set_level(AV_LOG_ERROR);

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
  EGLint pbAttr[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  struct gl g = {0};
  g.log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  g.gl = pl_opengl_create(g.log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                 .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!g.gl)
    return 1;
  // libplacebo's GPU programs, counted (as Kodi gives it a cache, dvbridge_cache.h)
  g.cache = pl_cache_create(pl_cache_params(.log = g.log, .max_total_size = 64 << 20));
  if (g.cache)
    pl_gpu_set_cache(g.gl->gpu, g.cache);
  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  struct dvbridge_source_packer* packer = dvbridge_source_packer_create();
  g_colourProgram = Program(FS_VERTEX, COLOUR_FRAGMENT);
  g_colourLoc = g_colourProgram ? glGetUniformLocation(g_colourProgram, "c") : -1;
  glGenVertexArrays(1, &g_vao);
  Check(g.renderer && g.packer && packer && g_colourProgram && SetUpOld(),
        "the renderer, the packers, the drawing program and the measurement before are made");
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  g.fbo = FramebufferOf(g.color);
  g.scanout = pl_opengl_wrap(g.gl->gpu, pl_opengl_wrap_params(.texture = g.color, .target = GL_TEXTURE_2D,
                                                             .iformat = GL_RGBA8, .width = W, .height = H));
  // the default framebuffer (the pbuffer), wrapped as Kodi wraps its window (CDVBridgeGLES::Initialize())
  g.window = pl_opengl_wrap(g.gl->gpu, pl_opengl_wrap_params(.width = W, .height = H));
  g.bytes = malloc((size_t)W * H * 4);
  g.bytesWindow = malloc((size_t)W * H * 4);
  if (!g.renderer || !g.packer || !packer || !g.fbo || !g.scanout || !g.window || !g.bytes)
    return 1;

  // the picture for the edges: RGBA32F, as the renderer's
  GLuint picture = 0;
  glGenTextures(1, &picture);
  glBindTexture(GL_TEXTURE_2D, picture);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLuint pictureFbo = FramebufferOf(picture);
  float* floats = malloc((size_t)W * H * 16);
  if (!pictureFbo || !floats)
    return 1;

  if (all || !strcmp(mode, "cover"))
    CoverPart(packer, picture, pictureFbo, floats);
  if (all || !strcmp(mode, "time"))
    TimePart(packer, picture, pictureFbo);
  const char* shaders = argc > 4 ? argv[4] : NULL;
  if (shaders && !strcmp(mode, "guires"))
    GuiResPart(shaders);
  if (all || !strcmp(mode, "frames") || !strcmp(mode, "window") || !strcmp(mode, "gui") ||
      !strcmp(mode, "fuse") || !strcmp(mode, "guitime") || !strcmp(mode, "pr"))
  {
    struct picture* pictures = calloc(wanted, sizeof(*pictures));
    const int decoded = Decode(argv[1], pictures, wanted);
    Check(decoded, "the pictures asked for are decoded, with their enhancement layer and metadata");
    if (decoded)
    {
      if (all || !strcmp(mode, "window"))
        WindowPart(&g, &pictures[0]);
      if (all || !strcmp(mode, "frames"))
        FramesPart(&g, pictures, wanted);
#ifdef DVBRIDGE_RENDER_DRAWN
      if (shaders && (all || !strcmp(mode, "gui")))
        GuiPart(&g, pictures, wanted, shaders);
      if (shaders && (all || !strcmp(mode, "fuse")))
        FusePart(&g, &pictures[0], shaders);
#endif
      // (not in all: a measurement, run on its own)
      if (shaders && !strcmp(mode, "guitime"))
        GuiTimePart(&g, &pictures[0], shaders);
      // (pr: the shaders of now, then those with the PR)
      if (shaders && argc > 5 && !strcmp(mode, "pr"))
        PrPart(&g, &pictures[0], shaders, argv[5]);
    }
  }
  printf("libplacebo: %ld warnings and errors\n", g_messages);
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  fflush(stdout);
  _Exit(g_failures != 0);
}
