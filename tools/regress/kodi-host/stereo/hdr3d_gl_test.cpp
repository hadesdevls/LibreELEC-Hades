// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: an HDR video in a 3D output, through the graphics, on Mesa's software OpenGL ES 3 (llvmpipe, EGL
// without a window), with Kodi's StereoViews.h (the eyes of a pass: KODI::RENDERING::STEREO_VIEWS::ForEachView()),
// WholeBuffer.h (the black under the video and the composite of the GUI over the whole frame: CWholeBuffer) and
// StereoGap.h (the lines between the eyes of frame packing) of the tree under test.
//
// Where the GUI is composited over the video (HDR10 or HLG passed through, and SDR of BT.2020 primaries: never in 3D),
// a frame of Kodi is: the GUI pass for each eye into the GUI's FBO (CApplication::Render(), each eye's viewport and
// scissor box set by CGraphicContext::SetStereoView()), the window's buffer cleared (CWinSystemGbmGLESContext::
// EndGuiComposite()), the video drawn after it (CGUIWindowManager::RenderEx(): the renderer takes its half of the
// picture and the place of its eye from the view, CBaseRenderer::ManageRenderArea()), the GUI composited from its FBO
// over it (CompositeGui()) and the lines between the eyes cleared (ClearStereoGap()). This test draws such frames as
// Kodi draws them, the parts that are not those three headers made the way Kodi's code makes them (the file and the
// lines are named where they are), into a framebuffer of the size of each 3D output, and reads every pixel back:
//   - frame packing at 1080p (1920 x 2205: the right eye 45 lines below the left one), from a side-by-side picture
//     (MVC and MV-HEVC arrive as such a picture, "left_right": MultiviewChoice.h), top and bottom at half height from
//     a top-and-bottom picture and side by side at half width from a side-by-side picture, both a film of 2.39:1
//     between black bars;
//   - a 3D video shown in 2D (MONO: the left eye over the whole screen) and an anaglyph (red/cyan: the red of the left
//     eye and the green and blue of the right one);
// each eye must show its own half of the picture over the whole of its place (its bars black), both eyes must have
// the GUI (a box of the OSD, drawn by the GUI pass in each eye), nothing may be left of what the buffer held before
// the frame (green) or of what the GUI's FBO held when it was made (magenta), the lines between the eyes are black,
// and the state of GL is what Kodi leaves at the end of a frame (the viewport and scissor box of the view OFF, all
// colours written).
//
// Built with -DOLD_RULE, the frames are drawn as before the fix (the video once with the view OFF, the clear and the
// composite with the viewport and scissor box the eyes left), which must fail. Without 3D (an HDR video in 2D) and
// without the composite (an SDR video in 3D: the video drawn in the GUI pass of each eye), the frames of both ways are
// the same bytes. And the callers in the tree: CApplication::Render() and CWinSystemGbmGLESContext (KODI_TREE).
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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef KODI_TREE
#error "KODI_TREE: the Kodi tree under test"
#endif

using KODI::RENDERING::STEREO_VIEWS::ForEachView;
using KODI::WINDOWING::GBM::CWholeBuffer;
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
// a texture of one colour (RGBA)
static std::vector<uint8_t> Solid(int w, int h, const uint8_t c[4])
{
  std::vector<uint8_t> d(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < d.size(); i += 4)
    std::memcpy(&d[i], c, 4);
  return d;
}

// ---------------------------------------------------------------------------------------------- the picture
// The 3D picture: each eye's half has an upper and a lower colour (so an eye drawn upside down, or the halves taken
// the wrong way, is seen). Row 0 of the data is the top of the picture, as the decoder gives it; the quad's top
// takes its first rows.
static const uint8_t L_TOP[4] = {220, 30, 30, 255}, L_BOTTOM[4] = {160, 30, 30, 255};
static const uint8_t R_TOP[4] = {30, 30, 220, 255}, R_BOTTOM[4] = {30, 30, 160, 255};
static const uint8_t WHITE[4] = {255, 255, 255, 255}, BLACK[4] = {0, 0, 0, 255};
static const uint8_t STALE[4] = {0, 255, 0, 255}, GARBAGE[4] = {255, 0, 255, 255};
constexpr int PW = 64, PH = 32; // the picture (small: nearest filtering keeps every colour exact)

enum class Packing
{
  SBS, // "left_right": the left eye in the left half
  TAB, // "top_bottom": the left eye in the upper half
};
static std::vector<uint8_t> Picture(Packing packing)
{
  std::vector<uint8_t> d(static_cast<size_t>(PW) * PH * 4);
  for (int y = 0; y < PH; ++y)
    for (int x = 0; x < PW; ++x)
    {
      bool right;
      bool upper;
      if (packing == Packing::SBS)
      {
        right = x >= PW / 2;
        upper = y < PH / 2;
      }
      else
      {
        right = y >= PH / 2;
        upper = (y % (PH / 2)) < PH / 4;
      }
      const uint8_t* c = right ? (upper ? R_TOP : R_BOTTOM) : (upper ? L_TOP : L_BOTTOM);
      std::memcpy(&d[(static_cast<size_t>(y) * PW + x) * 4], c, 4);
    }
  return d;
}

// ---------------------------------------------------------------------------------------------- Kodi's frame
struct Layout
{
  const char* name;
  RenderStereoMode mode;
  int bufferW, bufferH; // the frame
  int eyeW, eyeH, blanking; // one eye's screen (CGraphicContext::ResInfo()) and the gap between the eyes
  Packing packing;
  // the part of the eye's height that the picture takes (CBaseRenderer::CalcNormalRenderRect(): a film of 2.39:1
  // between black bars that the renderer does not draw, clear false); 1: the whole of it
  float film;
};

// What Kodi's graphics state is: the stereo view, and what CGraphicContext::SetStereoView() sets
// (GraphicContext.cpp: the viewport (0, 0, screen width, screen height) of one eye, moved by StereoCorrection() for
// the right eye: below the left one past the blanking (SPLIT_HORIZONTAL) or to its right (SPLIT_VERTICAL); set as
// viewport and scissor box by CRenderSystemGLES::SetViewPort()/SetScissors(), turned upside down against the height
// of the buffer; and the colour mask of an anaglyph, CRenderSystemGLES::SetStereoMode())
struct Kodi
{
  const Layout* l;
  RenderStereoView view = RenderStereoView::OFF;

  // the eye's place, from the top left of the frame
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
  void SetStereoView(RenderStereoView v)
  {
    view = v;
    int x, y;
    Place(v, x, y);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (l->mode == RenderStereoMode::ANAGLYPH_RED_CYAN)
    {
      if (v == RenderStereoView::LEFT)
        glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
      else if (v == RenderStereoView::RIGHT)
        glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glScissor(x, l->bufferH - y - l->eyeH, l->eyeW, l->eyeH);
    glViewport(x, l->bufferH - y - l->eyeH, l->eyeW, l->eyeH);
  }

  // The renderer's draw of the video (LinuxRendererGLES::RenderUpdateVideo(): clear false, alpha 255, no blending):
  // the source rectangle of CBaseRenderer::ManageRenderArea() (BaseRenderer.cpp): of a picture "left_right" the left
  // half for the left view and the right half for the right one, of "top_bottom" the upper and the lower half, and
  // the whole picture for the view OFF; drawn over the eye's screen (the picture of each test has the shape of it)
  void DrawVideo(GLuint picture) const
  {
    float s0 = 0, s1 = 1, t0 = 0, t1 = 1; // the source rectangle, t from the top of the picture
    if (l->packing == Packing::SBS)
    {
      if (view == RenderStereoView::LEFT)
        s1 = 0.5f;
      else if (view == RenderStereoView::RIGHT)
        s0 = 0.5f;
    }
    else
    {
      if (view == RenderStereoView::LEFT)
        t1 = 0.5f;
      else if (view == RenderStereoView::RIGHT)
        t0 = 0.5f;
    }
    const float f = l->film;
    const float pos[8] = {-1, f, -1, -f, 1, f, 1, -f};
    const float tex[8] = {s0, t0, s0, t1, s1, t0, s1, t1};
    glDisable(GL_BLEND);
    glUseProgram(g_tex);
    glBindTexture(GL_TEXTURE_2D, picture);
    Quad(pos, tex);
  }

  // The GUI pass of the eye in the view (CGUIWindowManager::Render()): the video window clears its background
  // (CGUIWindowFullScreen::ClearBackground(): 0 under a video drawn after the GUI, else opaque black; a clear under
  // the scissor box of the eye), the video where it is drawn in this pass (CGUIWindowFullScreen::Render(): the
  // renderer is a layer of the GUI), and a box of the OSD at 10-30 % across and 60-70 % down the eye's screen
  void GuiPass(bool videoAfterGui, GLuint picture) const
  {
    glClearColor(0, 0, 0, videoAfterGui ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!videoAfterGui)
      DrawVideo(picture);
    const float x0 = -1 + 2 * 0.1f, x1 = -1 + 2 * 0.3f, y0 = 1 - 2 * 0.6f, y1 = 1 - 2 * 0.7f;
    const float pos[8] = {x0, y0, x0, y1, x1, y0, x1, y1};
    const float tex[8] = {};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_col);
    glUniform4f(glGetUniformLocation(g_col, "col"), 1, 1, 1, 1);
    Quad(pos, tex);
  }

  // The composite of the GUI's FBO over the window's buffer (CWinSystemGbmGLESContext::CompositeGui(), single plane:
  // the quad of the whole FBO under Kodi's projection, its top left taking the texture's (0, 1), blended with the
  // GUI's alpha; the colour conversion of its shader left out, as nothing here depends on it)
  void Composite(GLuint guiTexture) const
  {
    const float pos[8] = {-1, 1, -1, -1, 1, 1, 1, -1};
    const float tex[8] = {0, 1, 0, 0, 1, 1, 1, 0};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_tex);
    glBindTexture(GL_TEXTURE_2D, guiTexture);
    Quad(pos, tex);
  }
};

enum class Way
{
  NOW, // the tree's
  BEFORE, // before the fix
};

struct Frame
{
  std::vector<uint8_t> pixels; // read back, row 0 the top of the frame
  GLint viewport[4]{}, box[4]{};
  GLboolean scissor = GL_FALSE, mask[4]{};
};

// One frame of Kodi into a buffer that holds green (what the window's buffer had: the GUI of the menus before the
// video), with the GUI's FBO made (magenta, what the memory had) at the start of it, as the first frame of a video.
// composite: the GUI is composited over the video, which is drawn after it (HDR); else the video is drawn in the GUI
// pass (SDR). The state of GL before the frame is that which a frame of Kodi leaves (the view OFF).
static Frame Draw(const Layout& l, bool composite, Way way)
{
  Kodi k{&l};
  std::vector<uint8_t> picData = Picture(l.packing);
  GLuint picture = Texture(PW, PH, &picData);
  std::vector<uint8_t> stale = Solid(l.bufferW, l.bufferH, STALE);
  GLuint windowTex = Texture(l.bufferW, l.bufferH, &stale);
  GLuint window = Framebuffer(windowTex);
  std::vector<uint8_t> garbage = Solid(l.bufferW, l.bufferH, GARBAGE);
  GLuint guiTex = Texture(l.bufferW, l.bufferH, &garbage);
  GLuint gui = Framebuffer(guiTex);
  glBindFramebuffer(GL_FRAMEBUFFER, window);
  glEnable(GL_SCISSOR_TEST); // (CRenderSystemGLES::ResetRenderSystem(): always on)
  k.SetStereoView(RenderStereoView::OFF);

  const auto setView = [&k](RenderStereoView v) { k.SetStereoView(v); };

  if (composite)
  {
    // BeginGuiComposite(): the FBO, made now, cleared (with the state the frame before left)
    glBindFramebuffer(GL_FRAMEBUFFER, gui);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  // the GUI pass (CApplication::Render())
  if (way == Way::NOW)
    ForEachView(l.mode, setView, [&] { k.GuiPass(composite, picture); });
  else if (l.mode != RenderStereoMode::OFF)
  {
    // (the loop before: the same as ForEachView(), shown so by sequence_test below)
    k.SetStereoView(RenderStereoView::LEFT);
    k.GuiPass(composite, picture);
    if (l.mode != RenderStereoMode::MONO)
    {
      k.SetStereoView(RenderStereoView::RIGHT);
      k.GuiPass(composite, picture);
    }
    k.SetStereoView(RenderStereoView::OFF);
  }
  else
    k.GuiPass(composite, picture);

  if (composite)
  {
    // EndGuiComposite(): back to the window's buffer, cleared
    glBindFramebuffer(GL_FRAMEBUFFER, window);
    {
      std::unique_ptr<CWholeBuffer> whole;
      if (way == Way::NOW)
        whole = std::make_unique<CWholeBuffer>(l.bufferW, l.bufferH);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
    }
    // RenderEx(): the video (VideoLayerInEachEye(): not native Dolby Vision)
    if (way == Way::NOW)
      ForEachView(l.mode, setView, [&] { k.DrawVideo(picture); });
    else
      k.DrawVideo(picture);
    // CompositeGui()
    {
      std::unique_ptr<CWholeBuffer> whole;
      if (way == Way::NOW)
        whole = std::make_unique<CWholeBuffer>(l.bufferW, l.bufferH);
      k.Composite(guiTex);
    }
  }
  // ClearStereoGap(): before the frame goes out
  if (l.mode == RenderStereoMode::SPLIT_HORIZONTAL || l.mode == RenderStereoMode::SPLIT_VERTICAL)
  {
    const bool tab = l.mode == RenderStereoMode::SPLIT_HORIZONTAL;
    if (const auto gap = GAP::GapOf(tab, !tab, l.eyeW, l.eyeH, l.blanking, l.bufferW, l.bufferH))
      GAP::Clear(*gap);
  }

  Frame f;
  glGetIntegerv(GL_VIEWPORT, f.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, f.box);
  f.scissor = glIsEnabled(GL_SCISSOR_TEST);
  glGetBooleanv(GL_COLOR_WRITEMASK, f.mask);
  std::vector<uint8_t> gl(static_cast<size_t>(l.bufferW) * l.bufferH * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, l.bufferW, l.bufferH, GL_RGBA, GL_UNSIGNED_BYTE, gl.data());
  f.pixels.resize(gl.size());
  const size_t row = static_cast<size_t>(l.bufferW) * 4;
  for (int y = 0; y < l.bufferH; ++y)
    std::memcpy(&f.pixels[y * row], &gl[(l.bufferH - 1 - y) * row], row);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &window);
  glDeleteFramebuffers(1, &gui);
  GLuint texs[3] = {picture, windowTex, guiTex};
  glDeleteTextures(3, texs);
  return f;
}

static std::string Rgb(const uint8_t* p)
{
  char s[32];
  std::snprintf(s, sizeof(s), "%u,%u,%u", p[0], p[1], p[2]);
  return s;
}
static bool Same(const uint8_t* p, const uint8_t c[4])
{
  return p[0] == c[0] && p[1] == c[1] && p[2] == c[2];
}

// The checks of a 3D frame drawn with the composite. expected(eye, upper) is the colour of the picture in that eye.
static void CheckFrame(const Layout& l, const Frame& f, const std::string& name)
{
  Kodi k{&l};
  const auto at = [&](int x, int y) { return &f.pixels[(static_cast<size_t>(y) * l.bufferW + x) * 4]; };
  const bool twoEyes = l.mode == RenderStereoMode::SPLIT_HORIZONTAL || l.mode == RenderStereoMode::SPLIT_VERTICAL;
  const int eyes = twoEyes ? 2 : 1;
  for (int e = 0; e < eyes; ++e)
  {
    const RenderStereoView v = e == 0 ? RenderStereoView::LEFT : RenderStereoView::RIGHT;
    int ex, ey;
    k.Place(v, ex, ey);
    const char* eyeName = e == 0 ? (twoEyes ? "left eye" : "the screen") : "right eye";
    // the picture over the whole place of the eye: at its corners and its middle, upper and lower part
    int wrong = 0;
    std::string first;
    const float xs[] = {0.02f, 0.5f, 0.98f};
    const float ys[] = {0.02f, 0.2f, 0.7f, 0.98f};
    for (float fy : ys)
      for (float fx : xs)
      {
        const int x = ex + static_cast<int>(fx * (l.eyeW - 1));
        const int y = ey + static_cast<int>(((1 - l.film) / 2 + fy * l.film) * (l.eyeH - 1));
        const bool upper = fy < 0.5f;
        uint8_t want[4];
        if (l.mode == RenderStereoMode::ANAGLYPH_RED_CYAN)
        {
          // red of the left eye, green and blue of the right one
          const uint8_t* lc = upper ? L_TOP : L_BOTTOM;
          const uint8_t* rc = upper ? R_TOP : R_BOTTOM;
          want[0] = lc[0];
          want[1] = rc[1];
          want[2] = rc[2];
        }
        else
        {
          const bool right = e == 1;
          const uint8_t* c = right ? (upper ? R_TOP : R_BOTTOM) : (upper ? L_TOP : L_BOTTOM);
          std::memcpy(want, c, 3);
        }
        const uint8_t* p = at(x, y);
        if (!(p[0] == want[0] && p[1] == want[1] && p[2] == want[2]))
        {
          if (!wrong++)
            first = "at " + std::to_string(x) + "," + std::to_string(y) + " " + Rgb(p) + ", want " + Rgb(want);
        }
      }
    check(wrong == 0, name + ": " + eyeName + " shows " + (e == 0 ? "the left" : "the right") +
                          " half of the picture over its whole place",
          wrong ? std::to_string(wrong) + " of 12 points wrong, first " + first : "12 points");
    if (l.film < 1)
    {
      // the black bars over and under the film: the clear of the frame, never what the buffer had
      size_t notBlack = 0;
      const int bar = static_cast<int>((1 - l.film) / 2 * l.eyeH) - 2;
      for (int y = 0; y < bar; ++y)
        for (int x = 0; x < l.eyeW; ++x)
          notBlack += !Same(at(ex + x, ey + y), BLACK) + !Same(at(ex + x, ey + l.eyeH - 1 - y), BLACK);
      check(notBlack == 0, name + ": " + eyeName + ": the black bars over and under the film are black",
            std::to_string(notBlack) + " pixels not black");
    }
    // the OSD box drawn in the eye's GUI pass, in the eye's place
    const int ox = ex + static_cast<int>(0.2f * l.eyeW), oy = ey + static_cast<int>(0.65f * l.eyeH);
    const bool osd = Same(at(ox, oy), WHITE);
    check(osd, name + ": " + eyeName + " has its GUI (the OSD's box) where its GUI pass drew it",
          Rgb(at(ox, oy)) + " at " + std::to_string(ox) + "," + std::to_string(oy));
  }
  // nothing of what the buffer or the FBO had
  size_t stale = 0, garbage = 0;
  for (size_t i = 0; i < f.pixels.size(); i += 4)
  {
    stale += Same(&f.pixels[i], STALE);
    garbage += Same(&f.pixels[i], GARBAGE);
  }
  check(stale == 0 && garbage == 0, name + ": nothing left of what the buffer or the GUI's FBO held",
        std::to_string(stale) + " pixels of the buffer before, " + std::to_string(garbage) + " of the FBO's memory");
  if (l.blanking > 0)
  {
    size_t notBlack = 0;
    for (int y = l.eyeH; y < l.eyeH + l.blanking; ++y)
      for (int x = 0; x < l.bufferW; ++x)
        notBlack += !Same(at(x, y), BLACK);
    check(notBlack == 0, name + ": the lines between the eyes are black", std::to_string(notBlack) + " not black");
  }
  // the state of GL that the frame leaves: Kodi's after SetStereoView(OFF)
  const GLint want[4] = {0, l.bufferH - l.eyeH, l.eyeW, l.eyeH};
  const bool state = std::equal(want, want + 4, f.viewport) && std::equal(want, want + 4, f.box) && f.scissor &&
                     f.mask[0] && f.mask[1] && f.mask[2] && f.mask[3];
  check(state, name + ": the state of GL is put back (viewport and scissor box of the view OFF, all colours)",
        "viewport " + std::to_string(f.viewport[0]) + "," + std::to_string(f.viewport[1]) + " " +
            std::to_string(f.viewport[2]) + "x" + std::to_string(f.viewport[3]));
}

// ---------------------------------------------------------------------------------------------- the callers
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
// the body of a function: from its head to the first line that is "}"
static std::string Body(const std::string& text, const std::string& head)
{
  const size_t at = text.find(head + "\n{");
  if (at == std::string::npos)
    return {};
  const size_t end = text.find("\n}\n", at);
  return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
static void Callers()
{
  const std::string tree = KODI_TREE;
  const std::string app =
      NoSpace(Body(Read(tree + "/xbmc/application/Application.cpp"), "void CApplication::Render()"));
  check(app.find("KODI::RENDERING::STEREO_VIEWS::ForEachView(gfx.GetStereoMode(),setView,[&hasRendered]{hasRendered|="
                 "CServiceBroker::GetGUI()->GetWindowManager().Render();});") != std::string::npos,
        "CApplication::Render(): the GUI pass in each view (ForEachView())");
  check(app.find("if(compositing&&CServiceBroker::GetWinSystem()->VideoLayerInEachEye())KODI::RENDERING::STEREO_VIEWS::"
                 "ForEachView(gfx.GetStereoMode(),setView,[]{CServiceBroker::GetGUI()->GetWindowManager().RenderEx();}"
                 ");elseCServiceBroker::GetGUI()->GetWindowManager().RenderEx();") != std::string::npos,
        "CApplication::Render(): the video after the composited GUI in each view, where it is drawn into the window");
  const std::string gles = Read(tree + "/xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string end = NoSpace(Body(gles, "void CWinSystemGbmGLESContext::EndGuiComposite()"));
  check(end.find("constCWholeBufferwhole(m_nWidth,m_nHeight);glClearColor(black,black,black,0.0f);glClear(GL_COLOR_"
                 "BUFFER_BIT);") != std::string::npos,
        "EndGuiComposite(): the black under the video over the whole buffer (CWholeBuffer)");
  const std::string comp = NoSpace(Body(gles, "void CWinSystemGbmGLESContext::CompositeGui()"));
  const size_t emplace = comp.find("whole.emplace(m_guiFboWidth,m_guiFboHeight);");
  const size_t draw = comp.find("glDrawElements(GL_TRIANGLE_STRIP,4,GL_UNSIGNED_BYTE,idx);");
  check(emplace != std::string::npos && draw != std::string::npos && emplace < draw &&
            comp.find("if(!dvComposite)#endif{whole.emplace(") != std::string::npos,
        "CompositeGui(): the GUI over the whole buffer before it is drawn, not into the frame of native Dolby Vision");
  const std::string h = NoSpace(Read(tree + "/xbmc/windowing/gbm/WinSystemGbmGLESContext.h"));
  check(h.find("boolVideoLayerInEachEye()constoverride{return!m_outputHandover.HoldsOutput();}") != std::string::npos,
        "CWinSystemGbmGLESContext::VideoLayerInEachEye(): not while native Dolby Vision has the output");
}

// ---------------------------------------------------------------------------------------------- the sequence
// ForEachView() against Kodi's loop before it (Application.cpp of the 1.1 base), for every stereo mode
static void Sequence()
{
  int differ = 0;
  for (int m = 0; m < static_cast<int>(RenderStereoMode::COUNT); ++m)
  {
    const auto mode = static_cast<RenderStereoMode>(m);
    std::string now, before;
    ForEachView(mode, [&now](RenderStereoView v) { now += "v" + std::to_string(static_cast<int>(v)); },
                [&now] { now += "d"; });
    if (mode != RenderStereoMode::OFF)
    {
      before += "v1d";
      if (mode != RenderStereoMode::MONO)
        before += "v2d";
      before += "v0";
    }
    else
      before += "d";
    if (now != before)
    {
      ++differ;
      std::printf("  mode %d: %s, before %s\n", m, now.c_str(), before.c_str());
    }
  }
  check(differ == 0, "ForEachView(): the views and passes of Kodi's loop in every stereo mode",
        std::to_string(static_cast<int>(RenderStereoMode::COUNT)) + " modes, " + std::to_string(differ) + " differ");
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

  const Layout layouts3D[] = {
      {"frame packing 1080p, MVC/MV-HEVC side by side", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 2205, 1920, 1080, 45,
       Packing::SBS, 1.0f},
      {"top and bottom, half, 2.39:1", RenderStereoMode::SPLIT_HORIZONTAL, 1920, 1080, 1920, 540, 0, Packing::TAB,
       804.0f / 1080.0f},
      {"side by side, half, 2.39:1", RenderStereoMode::SPLIT_VERTICAL, 1920, 1080, 960, 1080, 0, Packing::SBS,
       804.0f / 1080.0f},
      {"2D of a 3D video (MONO)", RenderStereoMode::MONO, 1920, 1080, 1920, 1080, 0, Packing::SBS, 1.0f},
      {"anaglyph red/cyan", RenderStereoMode::ANAGLYPH_RED_CYAN, 1920, 1080, 1920, 1080, 0, Packing::TAB, 1.0f},
  };
  // HDR (the GUI composited over the video) in each 3D output
  for (const Layout& l : layouts3D)
    CheckFrame(l, Draw(l, true, way), std::string("HDR, ") + l.name);

  // the same bytes as before: an SDR video in 3D (drawn in the GUI pass), and an HDR video in 2D
  for (const Layout& l : layouts3D)
  {
    const Frame a = Draw(l, false, Way::NOW), b = Draw(l, false, Way::BEFORE);
    check(a.pixels == b.pixels, std::string("SDR, ") + l.name + ": the same bytes as before");
  }
  {
    const Layout flat{"2D, 2.39:1", RenderStereoMode::OFF, 1920, 1080, 1920, 1080, 0, Packing::SBS, 804.0f / 1080.0f};
    const Frame a = Draw(flat, true, Way::NOW), b = Draw(flat, true, Way::BEFORE);
    check(a.pixels == b.pixels, "HDR in 2D: the same bytes as before");
    // and CWholeBuffer sets nothing there
    GLint vp[4], box[4];
    glViewport(0, 0, 1920, 1080);
    glScissor(0, 0, 1920, 1080);
    bool changed;
    {
      const CWholeBuffer whole(1920, 1080);
      changed = whole.Changed();
    }
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, box);
    check(!changed && vp[2] == 1920 && box[3] == 1080, "CWholeBuffer: nothing set over a whole viewport (2D)");
  }
  Sequence();
  Callers();

  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
