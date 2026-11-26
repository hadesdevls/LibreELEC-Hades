// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// videoframe_gl_test.cpp: the frame of the video at its own size (issue #30) on a GPU: Kodi's CaptureOwnSize.cpp,
// CaptureReadback.cpp and CaptureConvert.cpp of the tree, with Mesa's software OpenGL ES 3 through EGL without a
// window, and FFmpeg's libswscale and libavutil of the recipe (the SDR copy of 1.2).
//   - a picture drawn again at its own size (DrawOwnSize(), as CLinuxRendererGLES::CaptureVideoOwnSize() draws it
//     in one pass: the corners of the part of the picture that is shown, from a texture larger than it, onto a
//     framebuffer of its own size with its top at y = 0): value for value the texels of that part, top row first,
//     at 8 bits (GL_RGBA8) and at 10 (GL_RGB10_A2, read as X2BGR10LE), its size, its format;
//   - the picture of Kodi's renderer before its scaler read from its corner (ReadTextureCorner()): an 8-bit, a 10-bit
//     and a half-float texture larger than the picture, value for value, the floats as 16 bits;
//   - the SDR copy that 1.2 writes next to an HDR capture (CaptureToSdrCopy()), made from a 10-bit PQ frame as read
//     here: the same picture, within one code, as the one made from its values given at 16 bits; and a PQ ramp mapped
//     as 1.2 maps it (black 0, the peak 255);
//   - the state of GL put back as Kodi had it (the framebuffers bound for drawing and for reading, the viewport, the
//     scissor and its box, blending, the texture bound), also where nothing could be drawn;
//   - what each read costs on this software GPU for a 3840x2160 picture (a measure, no verdict).
// Prints a line per failed check and PASSED or FAILED (n) last.
#include "rendering/capture/CaptureConvert.h"
#include "rendering/capture/CaptureOwnSize.h"
#include "rendering/capture/CapturePixels.h"
#include "rendering/capture/CaptureTypes.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace KODI::RENDERING::CAPTURE;

static int failed = 0;
static void expect(bool ok, const char* what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// a pseudo-random code of 'bits' bits for texel (x, y), channel c
static unsigned Code(int x, int y, int c, int bits)
{
  unsigned v = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u ^
               static_cast<unsigned>(c + 1) * 83492791u;
  v ^= v >> 13;
  v *= 0x5bd1e995u;
  v ^= v >> 15;
  return v & ((1u << bits) - 1);
}

static GLuint Program()
{
  const char* vs = "#version 300 es\n"
                   "in vec2 a_pos; in vec2 a_tex; uniform vec2 u_size; out vec2 v_tex;\n"
                   "void main() { v_tex = a_tex; gl_Position = vec4(a_pos / u_size * 2.0 - 1.0, 0.0, 1.0); }\n";
  const char* fs = "#version 300 es\nprecision highp float;\n"
                   "in vec2 v_tex; uniform sampler2D u_tex; out vec4 o;\n"
                   "void main() { o = texture(u_tex, v_tex); }\n";
  auto shader = [](GLenum type, const char* src)
  {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
      std::printf("FAIL: a shader of the test does not compile\n");
    return s;
  };
  GLuint p = glCreateProgram();
  glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(p, 0, "a_pos");
  glBindAttribLocation(p, 1, "a_tex");
  glLinkProgram(p);
  return p;
}

// the source picture: a texture of tw x th, the part shown (w x h) at (sx, sy); as Kodi's planes, linear filtering
static GLuint Source(bool deep, int tw, int th)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (deep)
  {
    std::vector<uint32_t> v(static_cast<size_t>(tw) * th);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x)
        v[static_cast<size_t>(y) * tw + x] = Code(x, y, 0, 10) | (Code(x, y, 1, 10) << 10) |
                                             (Code(x, y, 2, 10) << 20) | (3u << 30);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, tw, th, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
                 v.data());
  }
  else
  {
    std::vector<uint8_t> v(static_cast<size_t>(tw) * th * 4);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x)
        for (int c = 0; c < 4; ++c)
          v[(static_cast<size_t>(y) * tw + x) * 4 + c] =
              static_cast<uint8_t>(c == 3 ? 255 : Code(x, y, c, 8));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, v.data());
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

// as CLinuxRendererGLES::CaptureVideoOwnSize() draws the picture again: the corners (0,0) (w,0) (w,h) (0,h) with
// Ortho2D(0, w, 0, h), the top-left one with the top-left of the part shown (its texture coordinates as
// CalculateTextureSourceRects() gives them, normalized)
static bool DrawPart(GLuint program, GLuint texture, int tw, int th, int sx, int sy, int w, int h)
{
  const float x1 = static_cast<float>(sx) / tw, x2 = static_cast<float>(sx + w) / tw;
  const float y1 = static_cast<float>(sy) / th, y2 = static_cast<float>(sy + h) / th;
  const GLfloat pos[] = {0, 0, static_cast<GLfloat>(w), 0, static_cast<GLfloat>(w), static_cast<GLfloat>(h),
                         0, static_cast<GLfloat>(h)};
  const GLfloat tex[] = {x1, y1, x2, y1, x2, y2, x1, y2};
  glUseProgram(program);
  glUniform2f(glGetUniformLocation(program, "u_size"), static_cast<float>(w), static_cast<float>(h));
  glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tex);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  const GLubyte idx[4] = {0, 1, 3, 2};
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glUseProgram(0);
  return true;
}

// the state Kodi has when the frame is drawn: a framebuffer of the window bound for drawing, another for reading, a
// viewport, a scissor box with the scissor on, blending on, a texture bound
struct State
{
  GLint draw, read, texture, viewport[4], box[4];
  GLboolean scissor, blend;
};
static State Now()
{
  State s{};
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.draw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.read);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture);
  glGetIntegerv(GL_VIEWPORT, s.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, s.box);
  s.scissor = glIsEnabled(GL_SCISSOR_TEST);
  s.blend = glIsEnabled(GL_BLEND);
  return s;
}
static void ExpectKept(const State& a, const char* what)
{
  const State b = Now();
  char line[200];
  std::snprintf(line, sizeof(line), "%s: the framebuffer bound for drawing is put back", what);
  expect(a.draw == b.draw, line);
  std::snprintf(line, sizeof(line), "%s: the framebuffer bound for reading is put back", what);
  expect(a.read == b.read, line);
  std::snprintf(line, sizeof(line), "%s: the texture bound is put back", what);
  expect(a.texture == b.texture, line);
  std::snprintf(line, sizeof(line), "%s: the viewport is put back", what);
  expect(std::memcmp(a.viewport, b.viewport, sizeof(a.viewport)) == 0, line);
  std::snprintf(line, sizeof(line), "%s: the scissor box is put back", what);
  expect(std::memcmp(a.box, b.box, sizeof(a.box)) == 0, line);
  std::snprintf(line, sizeof(line), "%s: the scissor is on again", what);
  expect(a.scissor == b.scissor, line);
  std::snprintf(line, sizeof(line), "%s: blending is on again", what);
  expect(a.blend == b.blend, line);
}

static uint16_t Half(float f)
{
  // (the values here are in [0, 1]: normal halves or zero)
  if (f <= 0.0f)
    return 0;
  int e = 0;
  const float m = std::frexp(f, &e); // f = m * 2^e, m in [0.5, 1)
  const int exp = e - 1 + 15;
  const unsigned mant = static_cast<unsigned>(std::lround((m * 2.0f - 1.0f) * 1024.0f));
  if (mant == 1024)
    return static_cast<uint16_t>((exp + 1) << 10);
  return static_cast<uint16_t>((exp << 10) | mant);
}
static float FromHalf(uint16_t h)
{
  const int exp = (h >> 10) & 31;
  const int mant = h & 1023;
  if (exp == 0)
    return std::ldexp(static_cast<float>(mant), -24);
  return std::ldexp(1.0f + static_cast<float>(mant) / 1024.0f, exp - 15);
}

static CaptureResult Result(ReadbackBuffer& b)
{
  CaptureResult r;
  r.pixels = std::make_shared<CHeapCapturePixels>(std::move(b.pixels));
  r.width = b.width;
  r.height = b.height;
  r.stride = b.stride;
  r.format = b.format;
  return r;
}

int main()
{
  EGLDisplay dpy = EGL_NO_DISPLAY;
  auto get = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, nullptr);
  EGLint major = 0, minor = 0, n = 0;
  EGLConfig cfg;
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API) ||
      !eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
  {
    std::printf("FAIL: no EGL display of OpenGL ES 3\nFAILED (1)\n");
    return 1;
  }
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
  {
    std::printf("FAIL: no OpenGL ES 3.0 context\nFAILED (1)\n");
    return 1;
  }
  std::printf("GL_RENDERER %s, %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  const GLuint program = Program();

  // Kodi's state: the window's framebuffer (here one of the test) bound for drawing and another for reading
  GLuint window = 0, other = 0, windowTex = 0, otherTex = 0, bound = 0;
  for (GLuint* f : {&window, &other})
  {
    GLuint& t = f == &window ? windowTex : otherTex;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, f);
    glBindFramebuffer(GL_FRAMEBUFFER, *f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  }
  glGenTextures(1, &bound);
  const auto kodiState = [&]()
  {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, window);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, other);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bound);
    glViewport(3, 4, 50, 40);
    glScissor(1, 2, 30, 20);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  };

  // ---- drawn again at its own size, 8 and 10 bits: the part of 64 x 36 at (7, 5) of a texture of 100 x 60
  constexpr int TW = 100, TH = 60, SX = 7, SY = 5, W = 64, H = 36;
  for (const bool deep : {false, true})
  {
    const GLuint src = Source(deep, TW, TH);
    kodiState();
    const State before = Now();
    ReadbackBuffer out;
    // (a drawing that leaves another framebuffer bound for reading: what is read is still the picture drawn)
    const bool ok = DrawOwnSize(W, H, deep,
                                [&]()
                                {
                                  const bool drawn = DrawPart(program, src, TW, TH, SX, SY, W, H);
                                  glBindFramebuffer(GL_READ_FRAMEBUFFER, other);
                                  return drawn;
                                },
                                out);
    expect(ok, deep ? "DrawOwnSize, 10 bits: it draws and reads" : "DrawOwnSize, 8 bits: it draws and reads");
    ExpectKept(before, deep ? "DrawOwnSize, 10 bits" : "DrawOwnSize, 8 bits");
    if (ok)
    {
      expect(out.width == W && out.height == H, "DrawOwnSize: the size of the picture");
      expect(out.stride == W * 4, "DrawOwnSize: the rows top-down (a positive stride), packed");
      expect(out.format == (deep ? AV_PIX_FMT_X2BGR10LE : AV_PIX_FMT_RGBA),
             deep ? "DrawOwnSize, 10 bits: read as X2BGR10LE" : "DrawOwnSize, 8 bits: read as RGBA");
      int wrong = 0, firstX = -1, firstY = -1;
      for (int y = 0; y < H && out.pixels; ++y)
        for (int x = 0; x < W; ++x)
        {
          bool same = true;
          if (deep)
          {
            uint32_t v;
            std::memcpy(&v, out.pixels.get() + (static_cast<size_t>(y) * W + x) * 4, 4);
            for (int c = 0; c < 3; ++c)
              same = same && ((v >> (10 * c)) & 1023u) == Code(SX + x, SY + y, c, 10);
          }
          else
            for (int c = 0; c < 3; ++c)
              same = same && out.pixels[(static_cast<size_t>(y) * W + x) * 4 + c] == Code(SX + x, SY + y, c, 8);
          if (!same && wrong++ == 0)
          {
            firstX = x;
            firstY = y;
          }
        }
      if (wrong)
        std::printf("  %d pixels differ, the first at (%d, %d)\n", wrong, firstX, firstY);
      expect(wrong == 0, deep ? "DrawOwnSize, 10 bits: the part shown, value for value, its top row first"
                              : "DrawOwnSize, 8 bits: the part shown, value for value, its top row first");
    }
    glDeleteTextures(1, &src);
  }

  // ---- nothing drawn: nothing read, the state put back
  {
    kodiState();
    const State before = Now();
    ReadbackBuffer out;
    expect(!DrawOwnSize(W, H, false, []() { return false; }, out) && !out.pixels,
           "DrawOwnSize: a picture that is not drawn gives nothing");
    ExpectKept(before, "DrawOwnSize, nothing drawn");
    expect(!DrawOwnSize(0, H, false, []() { return true; }, out), "DrawOwnSize: no size, nothing");
  }

  // ---- the picture before Kodi's scaler, read from its corner: 8 bits, 10 bits, half floats
  constexpr int FW = 80, FH = 50, PW = 64, PH = 36;
  for (int kind = 0; kind < 3; ++kind)
  {
    const char* name = kind == 0 ? "8 bits" : kind == 1 ? "10 bits" : "half floats";
    GLuint t = 0;
    std::vector<uint16_t> halves;
    if (kind == 0)
      t = Source(false, FW, FH);
    else if (kind == 1)
      t = Source(true, FW, FH);
    else
    {
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      halves.resize(static_cast<size_t>(FW) * FH * 4);
      for (int y = 0; y < FH; ++y)
        for (int x = 0; x < FW; ++x)
          for (int c = 0; c < 4; ++c)
            halves[(static_cast<size_t>(y) * FW + x) * 4 + c] =
                c == 3 ? Half(1.0f) : Half(static_cast<float>(Code(x, y, c, 10)) / 1023.0f);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, FW, FH, 0, GL_RGBA, GL_HALF_FLOAT, halves.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    kodiState();
    const State before = Now();
    ReadbackBuffer out;
    const bool ok = ReadTextureCorner(t, PW, PH, out);
    char line[160];
    std::snprintf(line, sizeof(line), "ReadTextureCorner, %s: it reads", name);
    expect(ok, line);
    std::snprintf(line, sizeof(line), "ReadTextureCorner, %s", name);
    ExpectKept(before, line);
    if (ok)
    {
      const AVPixelFormat want = kind == 0 ? AV_PIX_FMT_RGBA : kind == 1 ? AV_PIX_FMT_X2BGR10LE : AV_PIX_FMT_RGBA64LE;
      std::snprintf(line, sizeof(line), "ReadTextureCorner, %s: its format, size and rows top-down", name);
      expect(out.format == want && out.width == PW && out.height == PH &&
                 out.stride == (kind == 2 ? PW * 8 : PW * 4),
             line);
      int wrong = 0;
      for (int y = 0; y < PH && out.pixels && out.format == want; ++y)
        for (int x = 0; x < PW; ++x)
          for (int c = 0; c < 3; ++c)
          {
            unsigned got = 0, exp = 0;
            if (kind == 0)
            {
              got = out.pixels[(static_cast<size_t>(y) * PW + x) * 4 + c];
              exp = Code(x, y, c, 8);
            }
            else if (kind == 1)
            {
              uint32_t v;
              std::memcpy(&v, out.pixels.get() + (static_cast<size_t>(y) * PW + x) * 4, 4);
              got = (v >> (10 * c)) & 1023u;
              exp = Code(x, y, c, 10);
            }
            else
            {
              uint16_t v;
              std::memcpy(&v, out.pixels.get() + ((static_cast<size_t>(y) * PW + x) * 4 + c) * 2, 2);
              got = v;
              const float f = FromHalf(halves[(static_cast<size_t>(y) * FW + x) * 4 + c]);
              exp = static_cast<unsigned>(static_cast<double>(f) * 65535.0 + 0.5);
            }
            wrong += got != exp;
          }
      std::snprintf(line, sizeof(line), "ReadTextureCorner, %s: the corner of the picture, value for value", name);
      if (wrong)
        std::printf("  %d values differ\n", wrong);
      expect(wrong == 0, line);
    }
    glDeleteTextures(1, &t);
  }
  {
    ReadbackBuffer out;
    expect(!ReadTextureCorner(0, PW, PH, out), "ReadTextureCorner: no texture, nothing");
  }

  // ---- the SDR copy of 1.2 from a frame of 10 bits in PQ as read here, against the same picture at 16 bits
  {
    // a PQ ramp across, the colours of the codes down: BT.2020, PQ, full range, a mastering peak of 1000 cd/m2
    constexpr int RW = 64, RH = 8;
    std::vector<uint32_t> ten(static_cast<size_t>(RW) * RH);
    std::vector<uint16_t> sixteen(static_cast<size_t>(RW) * RH * 4);
    for (int y = 0; y < RH; ++y)
      for (int x = 0; x < RW; ++x)
      {
        unsigned code[3];
        for (int c = 0; c < 3; ++c)
          code[c] = y == 0 ? static_cast<unsigned>(x * 1023 / (RW - 1)) : Code(x, y, c, 10);
        ten[static_cast<size_t>(y) * RW + x] = code[0] | (code[1] << 10) | (code[2] << 20) | (3u << 30);
        for (int c = 0; c < 3; ++c)
          sixteen[(static_cast<size_t>(y) * RW + x) * 4 + c] =
              static_cast<uint16_t>((code[c] << 6) | (code[c] >> 4));
        sixteen[(static_cast<size_t>(y) * RW + x) * 4 + 3] = 65535;
      }
    // (the 10-bit frame through the GPU: a texture read as Kodi's picture is read)
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, RW, RH, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, ten.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ReadbackBuffer read;
    const bool ok = ReadTextureCorner(t, RW, RH, read);
    expect(ok && read.format == AV_PIX_FMT_X2BGR10LE, "the PQ frame of 10 bits is read as X2BGR10LE");
    glDeleteTextures(1, &t);
    if (ok)
    {
      CaptureResult a = Result(read);
      ReadbackBuffer b16;
      b16.width = RW;
      b16.height = RH;
      b16.stride = RW * 8;
      b16.format = AV_PIX_FMT_RGBA64LE;
      b16.pixels = std::make_unique<uint8_t[]>(sixteen.size() * 2);
      std::memcpy(b16.pixels.get(), sixteen.data(), sixteen.size() * 2);
      CaptureResult b = Result(b16);
      for (CaptureResult* r : {&a, &b})
      {
        r->color.primaries = AVCOL_PRI_BT2020;
        r->color.transfer = AVCOL_TRC_SMPTE2084;
        r->color.range = AVCOL_RANGE_JPEG;
        r->hasDisplayMetadata = true;
        r->displayMetadata.has_luminance = 1;
        r->displayMetadata.max_luminance = AVRational{1000, 1};
        r->displayMetadata.min_luminance = AVRational{0, 1};
      }
      std::vector<uint8_t> ca(static_cast<size_t>(RW) * RH * 4), cb(ca.size());
      const bool madeA = CaptureToSdrCopy(a, ca.data());
      const bool madeB = CaptureToSdrCopy(b, cb.data());
      expect(madeA, "the SDR copy of the 10-bit frame is made");
      expect(madeB, "the SDR copy of the 16-bit frame is made");
      int far = 0;
      for (size_t i = 0; i < ca.size(); ++i)
        if (i % 4 != 3 && std::abs(int(ca[i]) - int(cb[i])) > 1)
          ++far;
      if (far)
        std::printf("  %d values of the two copies more than one code apart\n", far);
      expect(madeA && madeB && far == 0, "the SDR copy of the 10-bit frame is that of the same picture at 16 bits");
      // (the ramp as 1.2 maps it: black to black, the peak of the film to white; BGRA)
      if (madeA)
      {
        expect(ca[0] <= 1 && ca[1] <= 1 && ca[2] <= 1, "the SDR copy: PQ black is black");
        const size_t peak = static_cast<size_t>(RW - 1) * 4;
        std::printf("the SDR copy of the PQ ramp, row 0: first %u, last %u (code 1023, 10000 cd/m2)\n", ca[2],
                    ca[peak + 2]);
        expect(ca[peak] >= 254 && ca[peak + 1] >= 254 && ca[peak + 2] >= 254,
               "the SDR copy: above the peak of the film is white");
      }
    }
  }

  // ---- what a read costs here for 3840 x 2160 (a software GPU: a measure, no verdict)
  {
    const GLuint big = Source(true, 64, 64);
    for (const bool deep : {false, true})
    {
      ReadbackBuffer out;
      const auto t0 = std::chrono::steady_clock::now();
      const bool ok = DrawOwnSize(3840, 2160, deep, [&]() { return DrawPart(program, big, 64, 64, 0, 0, 64, 64); }, out);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::printf("measure: DrawOwnSize 3840x2160 at %d bits on %s: %.1f ms (%s)\n", deep ? 10 : 8,
                  reinterpret_cast<const char*>(glGetString(GL_RENDERER)), ms, ok ? "read" : "not read");
    }
    glDeleteTextures(1, &big);
  }

  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglTerminate(dpy);
  return failed ? 1 : 0;
}
