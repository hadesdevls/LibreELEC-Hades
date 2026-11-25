// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// subtitle_bars_gl_test.cpp: the measurement of the bars for subtitles in the black bars on a GPU
// (xbmc/cores/VideoPlayer/VideoRenderers/SubtitleBarsGLES.cpp of the Kodi tree, with Mesa's software GPU, EGL
// surfaceless, OpenGL ES 3): pictures of 8 bits (NV12's luma, R8) and of 10 bits in 16 (P010's, R16), limited and
// full range, a texture taller than the picture (a VAAPI surface of 2176 rows for 2160) whose rows below it are
// left out, a measurement read back at a later frame and never waited for, at most twice a second, and the state
// of GL put back as it was. Prints a line per failed check and PASSED or FAILED (n) last.
// Built with stubs of Kodi's system_gl.h and utils/log.h (stub/).
#include "cores/VideoPlayer/VideoRenderers/SubtitleBarsGLES.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifndef GL_R16_EXT
#define GL_R16_EXT 0x822A
#endif

using namespace KODI::SUBTITLES::BARS;

static int failed = 0;
static void expect(bool ok, const char* what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// a luma texture of w x th texels, the picture w x h in it: bars of bar rows at its top and bottom of code
// black, picture of code lit, the rows below the picture (padding) of code pad
static GLuint Luma(bool sixteen, int w, int th, int h, int bar, unsigned black, unsigned lit, unsigned pad)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  if (sixteen)
  {
    std::vector<uint16_t> v(static_cast<size_t>(w) * th);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < w; ++x)
        v[static_cast<size_t>(y) * w + x] =
            static_cast<uint16_t>((y >= h ? pad : (y < bar || y >= h - bar) ? black : lit) << 6);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16_EXT, w, th, 0, GL_RED, GL_UNSIGNED_SHORT, v.data());
  }
  else
  {
    std::vector<uint8_t> v(static_cast<size_t>(w) * th);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < w; ++x)
        v[static_cast<size_t>(y) * w + x] =
            static_cast<uint8_t>(y >= h ? pad : (y < bar || y >= h - bar) ? black : lit);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, th, 0, GL_RED, GL_UNSIGNED_BYTE, v.data());
  }
  // (as Kodi has its planes: a filter without mipmaps, so that the texture is complete)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// what Tick gives within some frames (a frame every 5 ms, as many as it takes the GPU, at most 3 s)
static bool Measured(CBarsMeasureGLES& m, GLuint t, int tw, int th, int w, int h, float scale, float black,
                     Measure& out, int* frames = nullptr)
{
  for (int i = 0; i < 600; ++i)
  {
    if (m.Tick(t, tw, th, w, h, scale, black, out))
    {
      if (frames)
        *frames = i;
      return true;
    }
    glFlush();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
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
  std::printf("GL_RENDERER %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  const bool norm16 = std::strstr(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),
                                  "GL_EXT_texture_norm16") != nullptr;
  expect(norm16, "GL_EXT_texture_norm16 (the 10-bit planes of Kodi)");

  // the state that Kodi has when the renderer draws: a framebuffer of its own, a viewport, a program, the
  // texture unit 2 active with a texture on unit 0, blending and the scissor on
  GLuint kodiFbo = 0, kodiTex = 0, kodiFboTex = 0;
  glGenFramebuffers(1, &kodiFbo);
  glGenTextures(1, &kodiFboTex);
  glBindTexture(GL_TEXTURE_2D, kodiFboTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, kodiFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, kodiFboTex, 0);
  glGenTextures(1, &kodiTex);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, kodiTex);
  glActiveTexture(GL_TEXTURE2);
  glViewport(3, 4, 50, 40);
  glEnable(GL_BLEND);
  glEnable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

  CBarsMeasureGLES m;
  Measure out;
  int frames = -1;

  // NV12, limited range: a 1920x1080 film of 2.39:1 (138 rows of bars), its picture at code 120
  {
    const GLuint t = Luma(false, 1920, 1088, 1080, 138, 16, 120, 255);
    const bool got = Measured(m, t, 1920, 1088, 1920, 1080, 1.0f, 16.0f / 255.0f, out, &frames);
    expect(got, "8 bits: a measurement read back");
    expect(got && out.height == 1080 && out.black == 138 && out.across == 138 && out.edge,
           "8 bits: the bar of 138 rows and its edge (the rows below the picture left out)");
    expect(frames >= 1, "read back at a later frame, not in the one it was drawn in");
    glDeleteTextures(1, &t);
  }
  // the state of GL as Kodi had it
  {
    GLint fbo = 0, vp[4] = {}, active = 0, tex0 = 0;
    GLboolean mask[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
    glActiveTexture(static_cast<GLenum>(active));
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    expect(static_cast<GLuint>(fbo) == kodiFbo, "the framebuffer put back");
    expect(vp[0] == 3 && vp[1] == 4 && vp[2] == 50 && vp[3] == 40, "the viewport put back");
    expect(active == GL_TEXTURE2 && static_cast<GLuint>(tex0) == kodiTex, "the textures put back");
    expect(glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_DEPTH_TEST),
           "blending and the scissor put back");
    expect(mask[0] && mask[1] && mask[2] && !mask[3], "the colour mask put back");
    expect(glGetError() == GL_NO_ERROR, "no error of GL");
  }
  // at most twice a second: right after a measurement, nothing is drawn or read
  {
    const GLuint t = Luma(false, 64, 64, 64, 8, 16, 120, 0);
    Measure none;
    bool any = false;
    for (int i = 0; i < 20; ++i)
      any = m.Tick(t, 64, 64, 64, 64, 1.0f, 16.0f / 255.0f, none) || any;
    expect(!any, "nothing measured within half a second of the last");
    glDeleteTextures(1, &t);
    std::this_thread::sleep_for(std::chrono::milliseconds(520));
  }
  // P010, limited range, a 3840x2160 film of 2.39:1 in a surface of 2176 rows; bars with 3 codes of noise
  if (norm16)
  {
    const GLuint t = Luma(true, 3840, 2176, 2160, 276, 64 + 3, 500, 1023);
    const bool got = Measured(m, t, 3840, 2176, 3840, 2160, 1.0f, 16.0f / 255.0f, out);
    expect(got && out.height == 2160 && out.black == 276 && out.across == 276 && out.edge,
           "10 bits in 16: the bar of 276 rows and its edge");
    glDeleteTextures(1, &t);
    std::this_thread::sleep_for(std::chrono::milliseconds(520));
    // a dark scene: the picture just above the black, not lit
    const GLuint d = Luma(true, 3840, 2160, 2160, 276, 64, 64 + 50, 0);
    const bool gotDark = Measured(m, d, 3840, 2160, 3840, 2160, 1.0f, 16.0f / 255.0f, out);
    expect(gotDark && out.black == 276 && !out.edge, "a dark scene: its black rows, no edge");
    glDeleteTextures(1, &d);
    std::this_thread::sleep_for(std::chrono::milliseconds(520));
  }
  // 10 bits of software decoding (the codes in the low bits: the scale of CConvertMatrix), full range
  if (norm16)
  {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    std::vector<uint16_t> v(256 * 256);
    for (int y = 0; y < 256; ++y)
      for (int x = 0; x < 256; ++x)
        v[static_cast<size_t>(y) * 256 + x] = static_cast<uint16_t>(y >= 256 - 40 ? 0 : 600);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16_EXT, 256, 256, 0, GL_RED, GL_UNSIGNED_SHORT, v.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    const bool got = Measured(m, t, 256, 256, 256, 256, 65535.0f / 1023.0f, 0.0f, out);
    expect(got && out.black == 40 && out.edge, "10 bits in the low bits, full range: the bar of 40 rows");
    glDeleteTextures(1, &t);
  }

  m.Release();
  expect(glGetError() == GL_NO_ERROR, "no error of GL at the end");
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
