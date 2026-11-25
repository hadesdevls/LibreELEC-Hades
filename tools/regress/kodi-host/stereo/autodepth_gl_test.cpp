// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: the depth measured from a 3D video through the graphics, as Kodi does it, on Mesa's software
// OpenGL ES 3 (llvmpipe, EGL without a window): StereoDepthCapture.cpp (the copy of each eye out of the framebuffer,
// halved in steps, read back through a pixel buffer behind a fence), StereoDepthMeter.cpp (the thread that matches,
// the planner, the area of the subtitles, what kodi.log is told) and StereoDepth.cpp of the tree, with a stand-in of
// utils/log.h (glstub/).
//
// A framebuffer of the size of a 3D output is drawn as Kodi's split 3D outputs have it (CGraphicContext::
// StereoCorrection(): the right eye below the left one, past the blanking, or to its right), each eye from a picture
// of known depth: a background 12 pixels (of 1920) behind the screen, a thing in the subtitles' area 24 pixels in front
// (1.25 % of the width) and a nearer one at the top, 36 pixels (1.875 %). For each eye the viewport is set as
// SetStereoView() sets it, and CStereoDepthMeter::AfterVideo() is called as the render manager calls it; Frame() once
// a frame, a frame each 80 ms. The layouts: frame packing at 1080p (1920 x 2205), side by side at half width, and top
// and bottom at half height with a film of 2.39:1 between black bars (the video's rectangle inside the eye's screen).
// After two seconds the subtitles are at 1.5 % (in front of the thing by the margin of 0.25 %) and the interface at
// the comfort limit, 2 % (the thing at the top), as a shift of half of it to each eye. An anaglyph output and a video
// drawn once for both eyes under the interface (native Dolby Vision) are not measured, and kodi.log says why (an HDR
// video, drawn in each eye's view: hdrdepth_gl_test.cpp). The GL state is as it was.
// Prints "ok:"/"FAIL:" lines, the time the render thread spent, and "<n> failed".
#include "StereoDepthCapture.h"
#include "StereoDepthMeter.h"
#include "utils/log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace KODI::STEREODEPTH;

static int failures = 0;
static void check(bool ok, const char* what, double got, double want)
{
  std::printf("%s: %s: %.5f (want %.5f)\n", ok ? "ok" : "FAIL", what, got, want);
  failures += !ok;
}

// --------------------------------------------------------------------------------------------- the pictures
struct Noise
{
  int cells;
  float size;
  std::vector<float> grid;
  Noise(uint32_t seed, float cellSize, int width) : size(cellSize)
  {
    cells = static_cast<int>(width / cellSize) + 4;
    grid.resize(static_cast<size_t>(cells) * cells);
    uint32_t s = seed;
    for (float& g : grid)
    {
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      g = static_cast<float>(s % 1000) / 1000.0f;
    }
  }
  float At(float x, float y) const
  {
    const float gx = x / size + 1.0f;
    const float gy = y / size + 1.0f;
    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, cells - 2);
    const int iy = std::clamp(static_cast<int>(std::floor(gy)), 0, cells - 2);
    const float fx = gx - static_cast<float>(ix);
    const float fy = gy - static_cast<float>(iy);
    auto g = [&](int a, int b) { return grid[static_cast<size_t>(b) * cells + a]; };
    return (g(ix, iy) * (1 - fx) + g(ix + 1, iy) * fx) * (1 - fy) +
           (g(ix, iy + 1) * (1 - fx) + g(ix + 1, iy + 1) * fx) * fy;
  }
};

// one eye of the film, 1920 wide and 'height' high: the background at +12, a thing at -24 in the subtitles' area
// (x 700..1100, the lower 80..95 % of the picture), a thing at -36 at the top (x 300..900, 10..30 %)
static std::vector<uint8_t> Eye(bool right, int height)
{
  const Noise a(11, 12.0f, 2100), b(12, 5.0f, 2100), c(21, 12.0f, 2100), e(22, 5.0f, 2100);
  std::vector<uint8_t> out(static_cast<size_t>(1920) * height);
  struct T
  {
    float x1, y1, x2, y2, d;
    const Noise* n;
  };
  const T things[] = {{700, 0.80f * height, 1100, 0.95f * height, -24.0f, &c},
                      {300, 0.10f * height, 900, 0.30f * height, -36.0f, &e}};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < 1920; ++x)
    {
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);
      // the right eye sees at x what the left saw at x - d
      float v = 40 + 120 * a.At((right ? fx - 12.0f : fx) + 40, fy) + 60 * b.At((right ? fx - 12.0f : fx) + 40, fy);
      for (const T& t : things)
      {
        const float lx = right ? fx - t.d : fx;
        if (fy >= t.y1 && fy < t.y2 && lx >= t.x1 && lx < t.x2)
          v = 30 + 150 * t.n->At(lx, fy) + 60 * (t.n == &c ? b : a).At(lx * 0.5f, fy);
      }
      out[static_cast<size_t>(y) * 1920 + x] = static_cast<uint8_t>(std::clamp(v + (right ? 10.0f : 0.0f), 0.0f, 255.0f));
    }
  return out;
}

// the picture of w x h (whole divisions of 1920 x sh), averaged, as RGBA, its first row the bottom one (GL)
static std::vector<uint8_t> Rgba(const std::vector<uint8_t>& src, int sh, int w, int h)
{
  const int rx = 1920 / w;
  const int ry = sh / h;
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
    {
      int sum = 0;
      for (int j = 0; j < ry; ++j)
        for (int i = 0; i < rx; ++i)
          sum += src[static_cast<size_t>(y * ry + j) * 1920 + x * rx + i];
      const uint8_t v = static_cast<uint8_t>(sum / (rx * ry));
      uint8_t* p = out.data() + (static_cast<size_t>(h - 1 - y) * w + x) * 4;
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
  return out;
}

// ---------------------------------------------------------------------------------------------- the outputs
struct Layout
{
  const char* name;
  RenderStereoMode mode;
  int eyeW, eyeH, blank;
  CRect dest; //!< the video in the eye's screen
  int videoH; //!< the film's height at 1920 wide
};

static std::vector<uint8_t> g_eyes[2];

static bool Run(const Layout& l, GLuint fbo)
{
  const bool tab = l.mode == RenderStereoMode::SPLIT_HORIZONTAL;
  const int fbW = tab ? l.eyeW : 2 * l.eyeW + l.blank;
  const int fbH = tab ? 2 * l.eyeH + l.blank : l.eyeH;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbW, fbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  // each eye's viewport in GL coordinates (from the bottom), as SetStereoView() sets it
  const int vx[2] = {0, tab ? 0 : l.eyeW + l.blank};
  const int vy[2] = {tab ? l.eyeH + l.blank : 0, 0};
  const int dw = static_cast<int>(l.dest.Width());
  const int dh = static_cast<int>(l.dest.Height());
  for (int e = 0; e < 2; ++e)
  {
    const std::vector<uint8_t> rgba = Rgba(g_eyes[e], l.videoH, dw, dh);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, vx[e] + static_cast<int>(l.dest.x1),
                    vy[e] + (l.eyeH - static_cast<int>(l.dest.y2)), dw, dh, GL_RGBA, GL_UNSIGNED_BYTE,
                    rgba.data());
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  CStereoDepthMeter meter;
  CStereoDepthMeter::Facts facts;
  facts.mode = l.mode;
  facts.stereoVideo = true;
  facts.fullscreen = true;
  facts.sourceWidth = 1920;
  facts.destWidth = l.dest.Width();
  facts.subtitleVisible = true;
  facts.subtitleEvent = 1;
  // where libass drew a line: the middle of the picture, 86..92 % of its height
  facts.subtitleArea = CRect(l.dest.x1 + 0.35f * l.dest.Width(), l.dest.y1 + 0.86f * l.dest.Height(),
                             l.dest.x1 + 0.65f * l.dest.Width(), l.dest.y1 + 0.92f * l.dest.Height());
  CStereoDepthMeter::Depths depths;
  double renderMs = 0.0;
  int frames = 0;
  glEnable(GL_SCISSOR_TEST);
  for (int i = 0; i < 30; ++i)
  {
    auto t0 = std::chrono::steady_clock::now();
    depths = meter.Frame(facts);
    for (int e = 0; e < 2; ++e)
    {
      glViewport(vx[e], vy[e], l.eyeW, l.eyeH);
      glScissor(vx[e], vy[e], l.eyeW, l.eyeH);
      meter.AfterVideo(e == 0 ? RenderStereoView::LEFT : RenderStereoView::RIGHT, l.dest, l.eyeW, l.eyeH,
                       1000.0 * i);
    }
    renderMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }

  // what the render thread is left with
  GLint bound = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
  GLint viewport[4]{};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
  const GLenum error = glGetError();
  std::printf("%s: the render thread spent %.2f ms a frame (llvmpipe: the graphics' work is the processor's)\n",
              l.name, renderMs / frames);
  check(static_cast<GLuint>(bound) == fbo && scissor && viewport[0] == vx[1] && error == GL_NO_ERROR,
        (std::string(l.name) + ": the GL state is as it was, no GL error").c_str(), error, 0);
  check(depths.subtitles && std::fabs(*depths.subtitles - 0.015) <= 0.001,
        (std::string(l.name) + ": the subtitles, in front of the thing at 1.25 %").c_str(),
        depths.subtitles.value_or(-1), 0.015);
  const double gui = 0.5 * 0.02 * l.dest.Width();
  check(std::fabs(depths.guiPixels - gui) <= 0.01,
        (std::string(l.name) + ": the interface's shift of each eye, at the comfort limit (pixels)").c_str(),
        depths.guiPixels, gui);

  meter.Release();
  glDeleteTextures(1, &texture);
  return true;
}

static bool Logged(const char* text)
{
  for (const std::string& line : CLog::Lines())
    if (line.find(text) != std::string::npos)
      return true;
  return false;
}

int main()
{
  auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  EGLDisplay display = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                  : EGL_NO_DISPLAY;
  EGLint major = 0, minor = 0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
  {
    std::printf("FAIL: no EGL display without a window (Mesa's surfaceless platform)\n1 failed\n");
    return 1;
  }
  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RED_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  eglChooseConfig(display, configAttribs, &config, 1, &count);
  const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext context = count ? eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs) : EGL_NO_CONTEXT;
  if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
  {
    std::printf("FAIL: no OpenGL ES 3 context\n1 failed\n");
    return 1;
  }
  std::printf("GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  std::string why;
  check(CStereoDepthCapture::Supported(why), "OpenGL ES 3 is taken as able to copy", 1, 1);

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);

  // the film at 16:9 and at 2.39:1 (804 lines of 1920)
  const Layout layouts[] = {
      {"frame packing 1080p", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 1080, 45, CRect(0, 0, 1920, 1080), 1080},
      {"side by side, half", RenderStereoMode::SPLIT_VERTICAL, 960, 1080, 0, CRect(0, 0, 960, 1080), 1080},
      {"top and bottom, half, 2.39:1", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 540, 0, CRect(0, 69, 1920, 471),
       804},
  };
  for (const Layout& l : layouts)
  {
    g_eyes[0] = Eye(false, l.videoH);
    g_eyes[1] = Eye(true, l.videoH);
    Run(l, fbo);
  }
  check(Logged("subtitles and the interface over the video measured from the two pictures"),
        "kodi.log: measured", 1, 1);

  // not measured: told why, nothing given
  {
    CStereoDepthMeter meter;
    CStereoDepthMeter::Facts facts;
    facts.stereoVideo = true;
    facts.fullscreen = true;
    facts.destWidth = 1920;
    facts.mode = RenderStereoMode::ANAGLYPH_RED_CYAN;
    const CStereoDepthMeter::Depths a = meter.Frame(facts);
    check(!a.subtitles && a.guiPixels == 0.0f && Logged("an anaglyph output draws both eyes into one picture"),
          "anaglyph: not measured, and kodi.log says why", 0, 0);
    facts.mode = RenderStereoMode::SPLIT_VERTICAL;
    facts.drawnInGuiPass = false;
    const CStereoDepthMeter::Depths b = meter.Frame(facts);
    check(!b.subtitles && b.guiPixels == 0.0f && Logged("drawn under the interface once for both eyes"),
          "a video drawn once for both eyes under the interface: not measured, and kodi.log says why", 0, 0);
    facts.drawnInGuiPass = true;
    facts.automatic = false;
    facts.fixedPixels = 3;
    meter.Frame(facts);
    check(Logged("subtitles at the depth set (3 pixels of each eye)"), "Fixed: kodi.log says so", 0, 0);
    facts.automatic = true;
    facts.discDepth = true;
    meter.Frame(facts);
    check(Logged("subtitles by the plane offsets of the disc"), "a disc's offsets: kodi.log says so", 0, 0);
    meter.Release();
  }

  glDeleteFramebuffers(1, &fbo);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
