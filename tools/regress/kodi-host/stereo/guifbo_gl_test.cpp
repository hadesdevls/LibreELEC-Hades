// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test (issue #34): the GUI's FBO of the HDR composite in a 3D output, over several frames, through the graphics,
// on Mesa's software OpenGL ES 3 (llvmpipe, EGL without a window), with Kodi's StereoViews.h (the eyes of a pass:
// KODI::RENDERING::STEREO_VIEWS::ForEachView()), WholeBuffer.h (ClearWhole() and CWholeBuffer) and StereoGap.h of the
// tree under test.
//
// Where the GUI is composited over the video (HDR10 or HLG passed through), Kodi draws the GUI into an FBO
// (CWinSystemGbmGLESContext::BeginGuiComposite() clears it at the start of a frame after one in which the GUI drew
// something: m_guiFboClean), the GUI pass of each eye into it (CApplication::Render(), the eye's viewport and scissor
// box set by CGraphicContext::SetStereoView()), the window's buffer cleared (EndGuiComposite()), the video in each eye
// after it (RenderEx()), and the FBO composited over the whole buffer (CompositeGui()), only in a frame in which the GUI
// drew something. Over a video, the GUI pass clears nothing itself: CGUIWindowFullScreen::ClearBackground() calls
// CGraphicContext::Clear() (no colour: CRenderSystemGLES::InvalidateColorBuffer() with the geometry clear that is
// Kodi's default, AdvancedSettings m_guiGeometryClear) where the renderer does not bypass the framebuffer (the GLES
// renderers: CRenderManager::Configure(), UpdateVideoRender(VideoBypassesFramebuffer())). So the FBO holds what
// BeginGuiComposite() leaves of it, plus what the GUI draws.
//
// The frames of a video that starts with the menus of before it still drawn (its first frames: the GUI fading out
// over the FBO that the composite has just made, which holds what its memory held: magenta here), then a subtitle
// shown, then another one, each in both eyes at the depth of subtitles (the left eye's copy moved right, the right
// eye's moved left), in frame packing 1080p (1920 x 2205), top and bottom and side by side; and an FBO made in a frame
// whose GUI pass is left out. In the last frame each eye must show its video and the subtitle on the screen, at its
// place, and nothing else: nothing of the FBO's memory, of the menus, of the subtitles before; the right eye laid out as
// the left one; the lines between the eyes black; the viewport and the scissor box of the view OFF put back.
//
// Built with -DOLD_RULE, the FBO is cleared as before the fix (glClear under the viewport and scissor box that the
// frame before left), which must fail. Without 3D (an HDR video in 2D) the frames of both ways are the same bytes. And
// the caller in the tree: BeginGuiComposite() clears the FBO with ClearWhole() (KODI_TREE).
// Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "rendering/StereoViews.h"
#include "windowing/gbm/StereoGap.h"
#include "windowing/gbm/WholeBuffer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef KODI_TREE
#error "KODI_TREE: the Kodi tree under test"
#endif

using KODI::RENDERING::STEREO_VIEWS::ForEachView;
namespace GBM = KODI::WINDOWING::GBM;
namespace GAP = KODI::WINDOWING::GBM::STEREO_GAP;

static int failures = 0;
static void check(bool ok, const std::string& what, const std::string& detail = {})
{
  std::printf("%s: %s%s%s\n", ok ? "ok" : "FAIL", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  failures += !ok;
}

// ---------------------------------------------------------------------------------------------- GL helpers
static GLuint Compile(GLenum type, const char* src)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::printf("FAIL: shader: %s\n", log);
    ++failures;
  }
  return s;
}
static GLuint Program(const char* vs, const char* fs)
{
  GLuint p = glCreateProgram();
  glAttachShader(p, Compile(GL_VERTEX_SHADER, vs));
  glAttachShader(p, Compile(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(p, 0, "pos");
  glBindAttribLocation(p, 1, "tex");
  glLinkProgram(p);
  return p;
}
static const char* VS = "#version 300 es\nin vec2 pos; in vec2 tex; out vec2 t;\n"
                        "void main() { t = tex; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char* FS_TEX = "#version 300 es\nprecision mediump float; uniform sampler2D s; in vec2 t; out vec4 c;\n"
                            "void main() { c = texture(s, t); }\n";
static const char* FS_COL = "#version 300 es\nprecision mediump float; uniform vec4 col; in vec2 t; out vec4 c;\n"
                            "void main() { c = col; }\n";
static GLuint g_tex = 0, g_col = 0;

static void Quad(const float pos[8], const float tex[8])
{
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tex);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
}
static GLuint Texture(int w, int h, const std::vector<uint8_t>* data)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data ? data->data() : nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}
static GLuint Framebuffer(GLuint tex)
{
  GLuint f = 0;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  return f;
}
static std::vector<uint8_t> Solid(int w, int h, const uint8_t c[4])
{
  std::vector<uint8_t> d(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < d.size(); i += 4)
    std::memcpy(&d[i], c, 4);
  return d;
}

// ---------------------------------------------------------------------------------------------- the colours
// the video of each eye (one colour per view: the picture "left_right", as MVC and MV-HEVC come), the subtitles
// (opaque white), the menus of before the video (grey at 60 % opacity, as a GUI fading out), and what the FBO's memory
// held when it was made (magenta) and the window's buffer before the video (green)
static const uint8_t L_VIDEO[4] = {200, 40, 40, 255}, R_VIDEO[4] = {40, 40, 200, 255};
static const uint8_t WHITE[4] = {255, 255, 255, 255}, BLACK[4] = {0, 0, 0, 255};
static const uint8_t STALE[4] = {0, 255, 0, 255}, GARBAGE[4] = {255, 0, 255, 255};
static const float MENUS[4] = {0.5f, 0.5f, 0.5f, 0.6f};
constexpr int PW = 64, PH = 32;
constexpr int DEPTH = 12; // the depth of the subtitles: pixels each eye's copy moves (in front of the screen)

static std::vector<uint8_t> Picture()
{
  std::vector<uint8_t> d(static_cast<size_t>(PW) * PH * 4);
  for (int y = 0; y < PH; ++y)
    for (int x = 0; x < PW; ++x)
      std::memcpy(&d[(static_cast<size_t>(y) * PW + x) * 4], x < PW / 2 ? L_VIDEO : R_VIDEO, 4);
  return d;
}

struct Layout
{
  const char* name;
  RenderStereoMode mode;
  int bufferW, bufferH; // the frame
  int eyeW, eyeH, blanking; // one eye's screen (CGraphicContext::ResInfo()) and the lines between the eyes
};

// A rectangle of the GUI, in the pixels of one eye's screen, from its top left
struct Box
{
  int x0, y0, x1, y1;
};

enum class Way
{
  NOW, // the tree's: ClearWhole()
  BEFORE, // before the fix: glClear under the box the frame before left
};

// ---------------------------------------------------------------------------------------------- Kodi's frame
struct Kodi
{
  const Layout* l;
  RenderStereoView view = RenderStereoView::OFF;

  void Place(RenderStereoView v, int& x, int& y) const
  {
    x = 0;
    y = 0;
    if (v != RenderStereoView::RIGHT)
      return;
    if (l->mode == RenderStereoMode::SPLIT_HORIZONTAL)
      y += l->eyeH + l->blanking;
    if (l->mode == RenderStereoMode::SPLIT_VERTICAL)
      x += l->eyeW + l->blanking;
  }
  // CGraphicContext::SetStereoView(): the viewport and the scissor box of one eye's screen, the right eye's moved to
  // its place; the view OFF at the place of the left eye (GraphicContext.cpp)
  void SetStereoView(RenderStereoView v)
  {
    view = v;
    int x, y;
    Place(v, x, y);
    glScissor(x, l->bufferH - y - l->eyeH, l->eyeW, l->eyeH);
    glViewport(x, l->bufferH - y - l->eyeH, l->eyeW, l->eyeH);
  }
  // the depth of the GUI: in front of the screen the left eye's copy goes right and the right eye's left
  // (CGraphicContext::SetStereoView() with the stereo factor of the GUI, SetCameraPosition())
  int Shift() const
  {
    return view == RenderStereoView::LEFT ? DEPTH : view == RenderStereoView::RIGHT ? -DEPTH : 0;
  }
  // a box of the GUI drawn into the eye of the view, in the eye's pixels (blended as the GUI is)
  void DrawBox(const Box& b, const float c[4]) const
  {
    const int s = Shift();
    const float x0 = -1 + 2.0f * (b.x0 + s) / l->eyeW, x1 = -1 + 2.0f * (b.x1 + s) / l->eyeW;
    const float y0 = 1 - 2.0f * b.y0 / l->eyeH, y1 = 1 - 2.0f * b.y1 / l->eyeH;
    const float pos[8] = {x0, y0, x0, y1, x1, y0, x1, y1};
    const float tex[8] = {};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_col);
    glUniform4f(glGetUniformLocation(g_col, "col"), c[0], c[1], c[2], c[3]);
    Quad(pos, tex);
  }
  // the renderer's draw of the video in the view's eye: the eye's half of the picture over the eye's screen
  void DrawVideo(GLuint picture) const
  {
    float s0 = 0, s1 = 1;
    if (view == RenderStereoView::LEFT)
      s1 = 0.5f;
    else if (view == RenderStereoView::RIGHT)
      s0 = 0.5f;
    const float pos[8] = {-1, 1, -1, -1, 1, 1, 1, -1};
    const float tex[8] = {s0, 0, s0, 1, s1, 0, s1, 1};
    glDisable(GL_BLEND);
    glUseProgram(g_tex);
    glBindTexture(GL_TEXTURE_2D, picture);
    Quad(pos, tex);
  }
  // CompositeGui(), single plane: the whole FBO over the whole buffer (CWholeBuffer), blended with the GUI's alpha
  void Composite(GLuint guiTexture) const
  {
    const GBM::CWholeBuffer whole(l->bufferW, l->bufferH);
    const float pos[8] = {-1, 1, -1, -1, 1, 1, 1, -1};
    const float tex[8] = {0, 1, 0, 0, 1, 1, 1, 0};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_tex);
    glBindTexture(GL_TEXTURE_2D, guiTexture);
    Quad(pos, tex);
  }
};

// What the GUI draws in a frame: the menus over the whole eye (fading out), and the subtitles
struct GuiFrame
{
  bool render = true; // the GUI pass runs (else left out: CApplication's guiWillRender)
  bool menus = false;
  std::vector<Box> subtitles;
};

struct Run
{
  std::vector<uint8_t> pixels; // the last frame, row 0 the top
  GLint viewport[4]{}, box[4]{};
};

// The FBO cleared by BeginGuiComposite(): the tree's way, or the way before
static void ClearFbo(const Layout& l, Way way)
{
  if (way == Way::NOW)
    GBM::ClearWhole(l.bufferW, l.bufferH, 0.0f, 0.0f, 0.0f, 0.0f);
  else
  {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

// Kodi's frames of a video with the GUI composited, the FBO made before the first one (its memory: magenta), the
// window's buffer holding green, the state of GL that a frame of Kodi leaves (the view OFF)
static Run Frames(const Layout& l, const std::vector<GuiFrame>& frames, Way way)
{
  Kodi k{&l};
  std::vector<uint8_t> picData = Picture();
  GLuint picture = Texture(PW, PH, &picData);
  std::vector<uint8_t> stale = Solid(l.bufferW, l.bufferH, STALE);
  GLuint windowTex = Texture(l.bufferW, l.bufferH, &stale);
  GLuint window = Framebuffer(windowTex);
  std::vector<uint8_t> garbage = Solid(l.bufferW, l.bufferH, GARBAGE);
  GLuint guiTex = Texture(l.bufferW, l.bufferH, &garbage);
  GLuint gui = Framebuffer(guiTex);
  glBindFramebuffer(GL_FRAMEBUFFER, window);
  glEnable(GL_SCISSOR_TEST); // (CRenderSystemGLES: always on)
  k.SetStereoView(RenderStereoView::OFF);
  const auto setView = [&k](RenderStereoView v) { k.SetStereoView(v); };

  bool fboClean = false; // (m_guiFboClean: a fresh FBO is undefined)
  bool made = false;
  for (const GuiFrame& f : frames)
  {
    // BeginGuiComposite()
    if (!made)
    {
      made = true;
      if (!f.render)
      {
        // made in a frame whose GUI pass is left out: cleared at once, and taken as clean
        glBindFramebuffer(GL_FRAMEBUFFER, gui);
        ClearFbo(l, way);
        glBindFramebuffer(GL_FRAMEBUFFER, window);
        fboClean = true;
      }
    }
    int elements = 0;
    if (f.render)
    {
      glBindFramebuffer(GL_FRAMEBUFFER, gui);
      if (!fboClean)
      {
        ClearFbo(l, way);
        fboClean = true;
      }
      // the GUI pass in each eye (CGUIWindowFullScreen::ClearBackground(): no colour cleared)
      ForEachView(l.mode, setView, [&] {
        if (f.menus)
        {
          k.DrawBox({0, 0, l.eyeW, l.eyeH}, MENUS);
          ++elements;
        }
        for (const Box& b : f.subtitles)
        {
          const float white[4] = {1, 1, 1, 1};
          k.DrawBox(b, white);
          ++elements;
        }
      });
    }
    // EndGuiComposite(): the window's buffer, cleared whole
    glBindFramebuffer(GL_FRAMEBUFFER, window);
    {
      const GBM::CWholeBuffer whole(l.bufferW, l.bufferH);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
    }
    // RenderEx(): the video in each eye
    ForEachView(l.mode, setView, [&] { k.DrawVideo(picture); });
    // CompositeGui(): only where the GUI pass drew something, or where a left-out pass reuses an FBO that is not clean
    if (f.render)
      fboClean = elements == 0;
    if (!fboClean)
      k.Composite(guiTex);
    // ClearStereoGap()
    if (l.mode == RenderStereoMode::SPLIT_HORIZONTAL || l.mode == RenderStereoMode::SPLIT_VERTICAL)
    {
      const bool tab = l.mode == RenderStereoMode::SPLIT_HORIZONTAL;
      if (const auto gap = GAP::GapOf(tab, !tab, l.eyeW, l.eyeH, l.blanking, l.bufferW, l.bufferH))
        GAP::Clear(*gap);
    }
  }

  Run r;
  glGetIntegerv(GL_VIEWPORT, r.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, r.box);
  std::vector<uint8_t> gl(static_cast<size_t>(l.bufferW) * l.bufferH * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, l.bufferW, l.bufferH, GL_RGBA, GL_UNSIGNED_BYTE, gl.data());
  r.pixels.resize(gl.size());
  const size_t row = static_cast<size_t>(l.bufferW) * 4;
  for (int y = 0; y < l.bufferH; ++y)
    std::memcpy(&r.pixels[y * row], &gl[(l.bufferH - 1 - y) * row], row);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &window);
  glDeleteFramebuffers(1, &gui);
  GLuint texs[3] = {picture, windowTex, guiTex};
  glDeleteTextures(3, texs);
  return r;
}

static bool Same(const uint8_t* p, const uint8_t c[4])
{
  return p[0] == c[0] && p[1] == c[1] && p[2] == c[2];
}
static std::string Rgb(const uint8_t* p)
{
  char s[32];
  std::snprintf(s, sizeof(s), "%u,%u,%u", p[0], p[1], p[2]);
  return s;
}

// The last frame against what it must be: in each eye its video, and white where the last frame's subtitles are (at
// the eye's depth); the lines between the eyes black; the state of the view OFF
static void CheckLast(const Layout& l, const Run& r, const std::vector<Box>& shown, const std::string& name)
{
  Kodi k{&l};
  const auto at = [&](int x, int y) { return &r.pixels[(static_cast<size_t>(y) * l.bufferW + x) * 4]; };
  const bool twoEyes = l.mode == RenderStereoMode::SPLIT_HORIZONTAL || l.mode == RenderStereoMode::SPLIT_VERTICAL;
  std::vector<std::vector<bool>> white(2);
  for (int e = 0; e < (twoEyes ? 2 : 1); ++e)
  {
    const RenderStereoView v = !twoEyes ? RenderStereoView::OFF : e == 0 ? RenderStereoView::LEFT : RenderStereoView::RIGHT;
    k.view = v;
    int ex, ey;
    k.Place(v, ex, ey);
    const int s = k.Shift();
    const uint8_t* video = e == 0 && twoEyes ? L_VIDEO : e == 1 ? R_VIDEO : nullptr;
    size_t wrong = 0, garbage = 0, menus = 0, old = 0;
    std::string first;
    white[e].assign(static_cast<size_t>(l.eyeW) * l.eyeH, false);
    for (int y = 0; y < l.eyeH; ++y)
      for (int x = 0; x < l.eyeW; ++x)
      {
        bool sub = false;
        for (const Box& b : shown)
          sub = sub || (x >= b.x0 + s && x < b.x1 + s && y >= b.y0 && y < b.y1);
        const uint8_t* p = at(ex + x, ey + y);
        white[e][static_cast<size_t>(y) * l.eyeW + x] = Same(p, WHITE);
        // (2D: the whole picture over the screen, both halves)
        const uint8_t* want = sub ? WHITE : video ? video : (x < l.eyeW / 2 ? L_VIDEO : R_VIDEO);
        if (!Same(p, want))
        {
          if (!wrong++)
            first = "at " + std::to_string(ex + x) + "," + std::to_string(ey + y) + " " + Rgb(p) + ", want " + Rgb(want);
          garbage += p[0] > 150 && p[1] < 100 && p[2] > 150 && !sub;
          menus += !sub && p[0] > 60 && p[1] > 60 && p[2] > 60 && !Same(p, WHITE);
          old += !sub && Same(p, WHITE);
        }
      }
    const std::string eye = !twoEyes ? "the screen" : e == 0 ? "left eye" : "right eye";
    check(wrong == 0, name + ": " + eye + " shows its video and the subtitle on the screen at its depth, nothing else",
          wrong ? std::to_string(wrong) + " pixels wrong (of the FBO's memory " + std::to_string(garbage) +
                      ", of the menus " + std::to_string(menus) + ", subtitles before " + std::to_string(old) +
                      "), first " + first
                : std::to_string(l.eyeW * l.eyeH) + " pixels");
  }
  if (twoEyes)
  {
    // the right eye laid out as the left one: its subtitle is the left eye's moved by twice the depth
    size_t differ = 0;
    for (int y = 0; y < l.eyeH; ++y)
      for (int x = 0; x < l.eyeW; ++x)
      {
        const int xl = x + 2 * DEPTH;
        const bool left = xl < l.eyeW && white[0][static_cast<size_t>(y) * l.eyeW + xl];
        differ += left != white[1][static_cast<size_t>(y) * l.eyeW + x] && xl < l.eyeW;
      }
    check(differ == 0, name + ": the right eye's GUI is the left eye's, at the other eye's depth",
          std::to_string(differ) + " pixels differ");
  }
  if (l.blanking > 0)
  {
    size_t notBlack = 0;
    for (int y = l.eyeH; y < l.eyeH + l.blanking; ++y)
      for (int x = 0; x < l.bufferW; ++x)
        notBlack += !Same(at(x, y), BLACK);
    check(notBlack == 0, name + ": the lines between the eyes are black", std::to_string(notBlack) + " not black");
  }
  const GLint want[4] = {0, l.bufferH - l.eyeH, l.eyeW, l.eyeH};
  check(std::equal(want, want + 4, r.viewport) && std::equal(want, want + 4, r.box),
        name + ": the viewport and the scissor box of the view OFF are put back",
        "box " + std::to_string(r.box[0]) + "," + std::to_string(r.box[1]) + " " + std::to_string(r.box[2]) + "x" +
            std::to_string(r.box[3]));
}

// ---------------------------------------------------------------------------------------------- the caller
static std::string Read(const std::string& path)
{
  std::ifstream in(path);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
static std::string NoSpace(std::string s)
{
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
  return s;
}
static std::string Body(const std::string& text, const std::string& head)
{
  const size_t at = text.find(head + "\n{");
  if (at == std::string::npos)
    return {};
  const size_t end = text.find("\n}\n", at);
  return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
static size_t Count(const std::string& s, const std::string& what)
{
  size_t n = 0;
  for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size()))
    ++n;
  return n;
}
static void Caller()
{
  const std::string gles = Read(std::string(KODI_TREE) + "/xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string begin =
      NoSpace(Body(gles, "bool CWinSystemGbmGLESContext::BeginGuiComposite(bool guiWillRender)"));
  check(!begin.empty() && Count(begin, "ClearWhole(width,height,0.0f,0.0f,0.0f,0.0f);") == 2 &&
            Count(begin, "glClear(") == 0 && Count(begin, "intwidth=m_nWidth;intheight=m_nHeight;") == 1,
        "BeginGuiComposite(): both clears of the GUI's FBO (of a frame, and of an FBO made in a frame whose GUI pass "
        "is left out) over all of it (ClearWhole() of the FBO's size), none under the box the frame before left",
        std::to_string(Count(begin, "ClearWhole(")) + " ClearWhole(), " + std::to_string(Count(begin, "glClear(")) +
            " glClear()");
  const std::string full =
      NoSpace(Read(std::string(KODI_TREE) + "/xbmc/video/windows/GUIWindowFullScreen.cpp"));
  check(full.find("if(appPlayer->IsRenderingVideoLayer())CServiceBroker::GetWinSystem()->GetGfxContext().Clear(0);"
                  "elseif(!CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_guiGeometryClear||!"
                  "appPlayer->IsRenderingVideo())CServiceBroker::GetWinSystem()->GetGfxContext().Clear(0xff000000);"
                  "elseCServiceBroker::GetWinSystem()->GetGfxContext().Clear();") != std::string::npos,
        "CGUIWindowFullScreen::ClearBackground(): over a video drawn by a GLES renderer it clears no colour (the "
        "model of the GUI pass here)");
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
  g_tex = Program(VS, FS_TEX);
  g_col = Program(VS, FS_COL);
  glUseProgram(g_tex);
  glUniform1i(glGetUniformLocation(g_tex, "s"), 0);
  glActiveTexture(GL_TEXTURE0);

#ifdef OLD_RULE
  const Way way = Way::BEFORE;
  std::printf("the rule before the fix (OLD_RULE): it must fail\n");
#else
  const Way way = Way::NOW;
#endif

  const Layout layouts[] = {
      {"frame packing 1080p", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 2205, 1920, 1080, 45},
      {"top and bottom (half)", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 1080, 1920, 540, 0},
      {"side by side (half)", RenderStereoMode::SPLIT_VERTICAL, 1920, 1080, 960, 1080, 0},
  };
  for (const Layout& l : layouts)
  {
    // subtitles in the lower part of the eye's screen: one, then another in another place
    const Box a{l.eyeW / 4, l.eyeH * 80 / 100, l.eyeW / 2, l.eyeH * 85 / 100};
    const Box b{l.eyeW / 3, l.eyeH * 86 / 100, l.eyeW * 3 / 4, l.eyeH * 91 / 100};
    // the video starts: the menus still drawn over it, then a frame with nothing, a subtitle, another one
    std::vector<GuiFrame> frames(5);
    frames[0].menus = true;
    frames[1].menus = true;
    frames[3].subtitles = {a};
    frames[4].subtitles = {b};
    const std::string name = std::string("HDR, ") + l.name + ", a subtitle after the menus";
    CheckLast(l, Frames(l, frames, way), frames.back().subtitles, name);
    // an FBO made in a frame whose GUI pass is left out, then a subtitle
    std::vector<GuiFrame> skipped(2);
    skipped[0].render = false;
    skipped[1].subtitles = {a};
    CheckLast(l, Frames(l, skipped, way), skipped.back().subtitles,
              std::string("HDR, ") + l.name + ", the FBO made in a frame whose GUI pass is left out");
  }

  // without 3D: the same bytes in both ways (ClearWhole() sets nothing over a whole box)
  {
    const Layout flat{"2D", RenderStereoMode::OFF, 1920, 1080, 1920, 1080, 0};
    const Box a{480, 864, 960, 918};
    std::vector<GuiFrame> frames(4);
    frames[0].menus = true;
    frames[2].subtitles = {a};
    frames[3].subtitles = {a};
    const Run now = Frames(flat, frames, Way::NOW), before = Frames(flat, frames, Way::BEFORE);
    check(now.pixels == before.pixels, "HDR in 2D: the same bytes as before the fix");
    CheckLast(flat, now, frames.back().subtitles, "HDR in 2D");
  }
  Caller();

  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
