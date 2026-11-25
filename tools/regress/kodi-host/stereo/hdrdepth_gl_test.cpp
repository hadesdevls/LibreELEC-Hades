// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: the depth measured from a 3D video in HDR10 and in HLG through the graphics, as Kodi measures it since
// the video of an HDR picture is drawn in the view of each eye under the composited interface (CApplication::Render(),
// CWinSystem::VideoLayerInEachEye()): StereoDepthCapture.cpp, StereoDepthMeter.cpp and StereoDepth.cpp of the tree,
// on Mesa's software OpenGL ES 3 (llvmpipe, EGL without a window), with a stand-in of utils/log.h (glstub/).
//
// The pictures of known depth of autodepth_gl_test.cpp (a background 12 pixels of 1920 behind the screen, a thing in
// the subtitles' area 24 pixels in front, 1.25 % of the width, a nearer one at the top, 36 pixels, 1.875 %), drawn
// into a framebuffer of 10 bits a channel (RGB10_A2, as Kodi's HDR output has its window) in the signal of the video:
//   - SDR, as they are (the reference);
//   - PQ (HDR10): the light of each SDR code by BT.1886 (a pure 2.4 power) with its white at 203 cd/m2, where ITU-R
//     BT.2408 places the white of SDR in HDR, in the PQ signal;
//   - HLG: the same light on HLG's reference display of 1000 cd/m2, back through its OOTF (gamma 1.2) to scene light,
//     in the HLG signal;
// both at full light and at a quarter of it (a dim scene: the textures are nearer black, where PQ's and HLG's codes are
// fewer). In frame packing at 1080p, side by side at half width, and top and bottom at half height with a film of
// 2.39:1 between bars. The facts are those of an HDR video: not drawn in the interface's pass, drawn in each eye's
// view. Each must give what the SDR reference gives: the subtitles at 1.5 % (in front of the thing by 0.25 %) and the
// interface at the comfort limit, 2 %; and kodi.log says it is measured. A video drawn once for both eyes under the
// interface (native Dolby Vision) is still not measured, and kodi.log says why. And the caller in the tree (KODI_TREE):
// CRenderManager copies the eye of the video in the pass that draws it, the interface's or the one after it, and tells
// the meter whether the video is drawn in each eye's view.
// Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "StereoDepthCapture.h"
#include "StereoDepthMeter.h"
#include "utils/log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace KODI::STEREODEPTH;

#ifndef KODI_TREE
#error "KODI_TREE: the Kodi tree under test"
#endif

static int failures = 0;
static void check(bool ok, const std::string& what, double got, double want)
{
  std::printf("%s: %s: %.5f (want %.5f)\n", ok ? "ok" : "FAIL", what.c_str(), got, want);
  failures += !ok;
}

// --------------------------------------------------------------------------------------------- the pictures
// (as autodepth_gl_test.cpp)
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

// one eye of the film, 1920 wide and 'height' high, as SDR codes (0 to 255, as floats)
static std::vector<float> Eye(bool right, int height)
{
  const Noise a(11, 12.0f, 2100), b(12, 5.0f, 2100), c(21, 12.0f, 2100), e(22, 5.0f, 2100);
  std::vector<float> out(static_cast<size_t>(1920) * height);
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
      float v = 40 + 120 * a.At((right ? fx - 12.0f : fx) + 40, fy) + 60 * b.At((right ? fx - 12.0f : fx) + 40, fy);
      for (const T& t : things)
      {
        const float lx = right ? fx - t.d : fx;
        if (fy >= t.y1 && fy < t.y2 && lx >= t.x1 && lx < t.x2)
          v = 30 + 150 * t.n->At(lx, fy) + 60 * (t.n == &c ? b : a).At(lx * 0.5f, fy);
      }
      out[static_cast<size_t>(y) * 1920 + x] = std::clamp(v + (right ? 10.0f : 0.0f), 0.0f, 255.0f);
    }
  return out;
}

// ------------------------------------------------------------------------------------------------ the signals
enum class Signal
{
  SDR,
  PQ,
  HLG,
};

// SMPTE ST 2084: the signal of a light in cd/m2
static double Pq(double nits)
{
  const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
               c3 = 2392.0 / 4096 * 32;
  const double y = std::pow(std::max(nits, 0.0) / 10000.0, m1);
  return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
}

// ARIB STD-B67 (ITU-R BT.2100): the signal of a scene light (0 to 1)
static double Hlg(double e)
{
  const double a = 0.17883277, b = 1 - 4 * a, c = 0.5 - a * std::log(4 * a);
  return e <= 1.0 / 12 ? std::sqrt(3 * e) : a * std::log(12 * e - b) + c;
}

// an SDR code (0 to 255) in the signal, at a share of its light (gain)
static double InSignal(double code, Signal s, double gain)
{
  const double light = std::pow(code / 255.0, 2.4) * gain; // of the SDR white
  switch (s)
  {
    case Signal::PQ:
      return Pq(203.0 * light);
    case Signal::HLG:
      // on the reference display of 1000 cd/m2: display light 203 * light, a grey's scene light through the
      // inverse of the OOTF (Y_d = Y_s ^ 1.2)
      return Hlg(std::pow(203.0 * light / 1000.0, 1.0 / 1.2));
    default:
      return std::pow(light, 1.0 / 2.4);
  }
}

// the picture of w x h (whole divisions of 1920 x sh), averaged, in the signal, as RGB10_A2 words, its first row the
// bottom one (GL)
static std::vector<uint32_t> Rgb10(const std::vector<float>& src, int sh, int w, int h, Signal s, double gain)
{
  const int rx = 1920 / w;
  const int ry = sh / h;
  std::vector<uint32_t> out(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
    {
      double sum = 0;
      for (int j = 0; j < ry; ++j)
        for (int i = 0; i < rx; ++i)
          sum += src[static_cast<size_t>(y * ry + j) * 1920 + x * rx + i];
      const uint32_t v =
          static_cast<uint32_t>(std::lround(std::clamp(InSignal(sum / (rx * ry), s, gain), 0.0, 1.0) * 1023.0));
      out[static_cast<size_t>(h - 1 - y) * w + x] = v | v << 10 | v << 20 | 3u << 30;
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

struct Got
{
  bool subtitles = false;
  double subtitlesAt = -1;
  double guiPixels = 0;
};

static std::vector<float> g_eyes[2];

static Got Run(const Layout& l, GLuint fbo, Signal s, double gain, bool eachEye)
{
  const bool tab = l.mode == RenderStereoMode::SPLIT_HORIZONTAL;
  const int fbW = tab ? l.eyeW : 2 * l.eyeW + l.blank;
  const int fbH = tab ? 2 * l.eyeH + l.blank : l.eyeH;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, fbW, fbH, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  const int vx[2] = {0, tab ? 0 : l.eyeW + l.blank};
  const int vy[2] = {tab ? l.eyeH + l.blank : 0, 0};
  const int dw = static_cast<int>(l.dest.Width());
  const int dh = static_cast<int>(l.dest.Height());
  for (int e = 0; e < 2; ++e)
  {
    const std::vector<uint32_t> words = Rgb10(g_eyes[e], l.videoH, dw, dh, s, gain);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, vx[e] + static_cast<int>(l.dest.x1),
                    vy[e] + (l.eyeH - static_cast<int>(l.dest.y2)), dw, dh, GL_RGBA,
                    GL_UNSIGNED_INT_2_10_10_10_REV, words.data());
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  CStereoDepthMeter meter;
  CStereoDepthMeter::Facts facts;
  facts.mode = l.mode;
  facts.stereoVideo = true;
  facts.fullscreen = true;
  facts.sourceWidth = 1920;
  facts.destWidth = l.dest.Width();
  // an HDR video: drawn after the interface's passes, under it; in the view of each eye (or once, for both)
  facts.drawnInGuiPass = s == Signal::SDR;
  facts.drawnInEachEye = s != Signal::SDR && eachEye;
  facts.subtitleVisible = true;
  facts.subtitleEvent = 1;
  facts.subtitleArea = CRect(l.dest.x1 + 0.35f * l.dest.Width(), l.dest.y1 + 0.86f * l.dest.Height(),
                             l.dest.x1 + 0.65f * l.dest.Width(), l.dest.y1 + 0.92f * l.dest.Height());
  CStereoDepthMeter::Depths depths;
  glEnable(GL_SCISSOR_TEST);
  for (int i = 0; i < 30; ++i)
  {
    depths = meter.Frame(facts);
    for (int e = 0; e < 2; ++e)
    {
      glViewport(vx[e], vy[e], l.eyeW, l.eyeH);
      glScissor(vx[e], vy[e], l.eyeW, l.eyeH);
      meter.AfterVideo(e == 0 ? RenderStereoView::LEFT : RenderStereoView::RIGHT, l.dest, l.eyeW, l.eyeH,
                       1000.0 * i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  glDisable(GL_SCISSOR_TEST);
  meter.Release();
  glDeleteTextures(1, &texture);
  Got m;
  m.subtitles = depths.subtitles.has_value();
  m.subtitlesAt = depths.subtitles.value_or(-1);
  m.guiPixels = depths.guiPixels;
  return m;
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

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  const Layout layouts[] = {
      {"frame packing 1080p", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 1080, 45, CRect(0, 0, 1920, 1080), 1080},
      {"side by side, half", RenderStereoMode::SPLIT_VERTICAL, 960, 1080, 0, CRect(0, 0, 960, 1080), 1080},
      {"top and bottom, half, 2.39:1", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 540, 0, CRect(0, 69, 1920, 471),
       804},
  };
  struct Case
  {
    const char* name;
    Signal s;
    double gain;
  };
  const Case cases[] = {{"SDR", Signal::SDR, 1.0},
                        {"HDR10 (PQ)", Signal::PQ, 1.0},
                        {"HDR10 (PQ), a quarter of the light", Signal::PQ, 0.25},
                        {"HLG", Signal::HLG, 1.0},
                        {"HLG, a quarter of the light", Signal::HLG, 0.25}};
  for (const Layout& l : layouts)
  {
    g_eyes[0] = Eye(false, l.videoH);
    g_eyes[1] = Eye(true, l.videoH);
    Got reference;
    for (const Case& c : cases)
    {
      const Got m = Run(l, fbo, c.s, c.gain, true);
      if (c.s == Signal::SDR)
        reference = m;
      const std::string what = std::string(l.name) + ", " + c.name;
      check(m.subtitles && std::fabs(m.subtitlesAt - 0.015) <= 0.001,
            what + ": the subtitles, in front of the thing at 1.25 %", m.subtitlesAt, 0.015);
      check(m.subtitles && reference.subtitles && std::fabs(m.subtitlesAt - reference.subtitlesAt) <= 0.0005,
            what + ": the subtitles where SDR puts them (within 0.05 % of the width)", m.subtitlesAt,
            reference.subtitlesAt);
      const double gui = 0.5 * 0.02 * l.dest.Width();
      check(std::fabs(m.guiPixels - gui) <= 0.01, what + ": the interface's shift of each eye (pixels)", m.guiPixels,
            gui);
    }
  }
  check(Logged("subtitles and the interface over the video measured from the two pictures"),
        "kodi.log: measured", 1, 1);
  // drawn once for both eyes under the interface: not measured, and told why
  {
    g_eyes[0] = Eye(false, layouts[1].videoH);
    g_eyes[1] = Eye(true, layouts[1].videoH);
    const Got m = Run(layouts[1], fbo, Signal::PQ, 1.0, false);
    check(!m.subtitles && m.guiPixels == 0.0 && Logged("drawn under the interface once for both eyes"),
          "a video drawn once for both eyes under the interface: not measured, and kodi.log says why", 0, 0);
  }
  // the caller: CRenderManager::Render() copies after the video of the pass that drew it (gui or not), and
  // CRenderManager tells the meter whether the video is drawn in each eye's view
  {
    std::ifstream f(std::string(KODI_TREE) + "/xbmc/cores/VideoPlayer/VideoRenderers/RenderManager.cpp");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    const size_t call = src.find("m_stereoDepth.AfterVideo(");
    const size_t guard = src.rfind("if (presented", call);
    const std::string cond = guard != std::string::npos ? src.substr(guard, src.find(')', guard) - guard + 1) : "";
    check(call != std::string::npos && cond == "if (presented)",
          "RenderManager.cpp: the eye is copied in the pass that drew the video, whichever it is (" + cond + ")", 1,
          1);
    // (the statement without its spaces: as clang-format may break it)
    const size_t set = src.find("facts.drawnInEachEye =");
    std::string statement = set != std::string::npos ? src.substr(set, src.find(';', set) - set + 1) : "";
    statement.erase(std::remove_if(statement.begin(), statement.end(), [](char c) { return std::isspace(c) != 0; }),
                    statement.end());
    check(statement == "facts.drawnInEachEye=!facts.drawnInGuiPass&&CServiceBroker::GetWinSystem()->IsGuiComposite()&&"
                       "CServiceBroker::GetWinSystem()->VideoLayerInEachEye();",
          "RenderManager.cpp: the meter is told the video is drawn in each eye's view by the window system", 1, 1);
  }
  glDeleteFramebuffers(1, &fbo);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
