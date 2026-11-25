// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: an SDR output in a wide container (KODI::UTILS::SDRWIDE of the tree under test, xbmc/utils/SdrWide.h:
// an SDR video of BT.2020, and HDR mapped to SDR in BT.2020 or DCI-P3 (D65), videoscreen.hadessdrhdrgamut).
//
//   - the matrices of SdrWide.h against the test's own, made from the CIE xy of BT.709, BT.2020 and DCI-P3 (D65) with
//     the white of D65 (ITU-R BT.2087 gives BT.709 to BT.2020 the same way); every row sums to 1 (grey stays grey);
//   - the setting's values and their containers, the primaries Kodi's shaders draw into for each (CConvertMatrix's
//     destination), the pictures the setting is about (PQ and HLG), and why a container is not used (Check()): the
//     EDID and the connector are asked for the value of the container asked for, never for the other's;
//   - the composite of the GUI as the tree's gles_gui_composite.frag and .vert draw it, on Mesa's software OpenGL ES 3
//     (llvmpipe, EGL without a window), into a floating-point framebuffer that holds a picture, with the blending of
//     CWinSystemGbmGLESContext::CompositeGui(): for u_sdrWide 1 (BT.2020) and 2 (DCI-P3), full and limited range,
//     every pixel against SDRWIDE::Composite() of the container (within half a code of 1023); u_sdrWide 2 is not
//     u_sdrWide 1's picture.
//
// Built with -DKODI_TREE="<tree>" (the shaders are read from it). Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "utils/SdrWide.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef KODI_TREE
#error "KODI_TREE: the Kodi tree under test"
#endif

namespace SDRWIDE = KODI::UTILS::SDRWIDE;
using Mat = std::array<std::array<double, 3>, 3>;

static int failures = 0;
static void check(bool ok, const std::string& what, const std::string& detail = {})
{
  std::printf("%s: %s%s%s\n", ok ? "ok" : "FAIL", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  failures += !ok;
}

// ------------------------------------------------------------------------------------------ the test's own matrices
static Mat Inverse(const Mat& m)
{
  const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0],
               h = m[2][1], i = m[2][2];
  const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  return {{{(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det},
           {(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det},
           {(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det}}};
}
static Mat Multiply(const Mat& a, const Mat& b)
{
  Mat o{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      o[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
  return o;
}
static Mat ToXyz(const double xy[6])
{
  const double wx = 0.3127, wy = 0.3290;
  Mat m{};
  for (int c = 0; c < 3; ++c)
  {
    m[0][c] = xy[2 * c] / xy[2 * c + 1];
    m[1][c] = 1.0;
    m[2][c] = (1.0 - xy[2 * c] - xy[2 * c + 1]) / xy[2 * c + 1];
  }
  const Mat inv = Inverse(m);
  const double w[3] = {wx / wy, 1.0, (1.0 - wx - wy) / wy};
  for (int c = 0; c < 3; ++c)
  {
    const double s = inv[c][0] * w[0] + inv[c][1] * w[1] + inv[c][2] * w[2];
    for (int r = 0; r < 3; ++r)
      m[r][c] *= s;
  }
  return m;
}
static const double XY709[6] = {0.640, 0.330, 0.300, 0.600, 0.150, 0.060};
static const double XY2020[6] = {0.708, 0.292, 0.170, 0.797, 0.131, 0.046};
static const double XYP3[6] = {0.680, 0.320, 0.265, 0.690, 0.150, 0.060};

static double Apart(const Mat& a, const Mat& b)
{
  double d = 0;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      d = std::fmax(d, std::fabs(a[r][c] - b[r][c]));
  return d;
}

// ---------------------------------------------------------------------------------------------- GL
static std::string Read(const std::string& path)
{
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
static GLuint Compile(GLenum type, const std::string& src)
{
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[2048];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::printf("FAIL: shader: %s\n", log);
    ++failures;
    return 0;
  }
  return s;
}
// the composite program as Kodi builds it: the defines after the #version line (CGLSLShaderProgram)
static GLuint Program(const std::string& defines)
{
  auto withDefines = [&](std::string src) {
    const size_t eol = src.find('\n', src.find("#version"));
    return src.insert(eol + 1, defines);
  };
  const std::string dir = std::string(KODI_TREE) + "/system/shaders/GLES/2.0/";
  const GLuint vs = Compile(GL_VERTEX_SHADER, withDefines(Read(dir + "gles_gui_composite.vert")));
  const GLuint fs = Compile(GL_FRAGMENT_SHADER, withDefines(Read(dir + "gles_gui_composite.frag")));
  if (!vs || !fs)
    return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  return ok ? p : 0;
}

constexpr int GW = 64, GH = 64; // the GUI's FBO and the picture: 4096 pixels

// a pixel of the GUI as the FBO holds it (premultiplied by its coverage) and of the picture, made from its index
static void Pixel(int i, std::array<double, 3>& colour, double& coverage, std::array<double, 3>& picture)
{
  const int a = i % 16, b = (i / 16) % 16, c = (i / 256) % 16;
  colour = {a / 15.0, b / 15.0, c / 15.0}; // the GUI's colour (not premultiplied), 16 levels a channel
  coverage = ((i * 37) % 17) / 16.0; // 0 (discarded) to 1
  picture = {((i * 7) % 11) / 10.0, ((i * 3) % 13) / 12.0, ((i * 5) % 7) / 6.0};
}

// the frame drawn by the composite into a float framebuffer: the picture first, then the GUI blended over it
static bool Draw(GLuint prog, float sdrWide, std::vector<float>& out)
{
  std::vector<unsigned char> gui(GW * GH * 4);
  std::vector<float> pic(GW * GH * 4);
  for (int i = 0; i < GW * GH; ++i)
  {
    std::array<double, 3> colour, picture;
    double coverage;
    Pixel(i, colour, coverage, picture);
    // the FBO holds the colour premultiplied by the coverage (the blend functions of the GUI), as 8-bit codes
    for (int k = 0; k < 3; ++k)
      gui[i * 4 + k] = static_cast<unsigned char>(std::lround(colour[k] * coverage * 255.0));
    gui[i * 4 + 3] = static_cast<unsigned char>(std::lround(coverage * 255.0));
    for (int k = 0; k < 3; ++k)
      pic[i * 4 + k] = static_cast<float>(picture[k]);
    pic[i * 4 + 3] = 1.0f;
  }
  GLuint tex[2], fb;
  glGenTextures(2, tex);
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GW, GH, 0, GL_RGBA, GL_UNSIGNED_BYTE, gui.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, GW, GH, 0, GL_RGBA, GL_FLOAT, pic.data());
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return false;
  glViewport(0, 0, GW, GH);
  glUseProgram(prog);
  static const GLfloat proj[16] = {2.0f / GW, 0, 0, 0, 0, 2.0f / GH, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};
  glUniformMatrix4fv(glGetUniformLocation(prog, "u_proj"), 1, GL_FALSE, proj);
  glUniform1i(glGetUniformLocation(prog, "u_samp"), 0);
  glUniform1f(glGetUniformLocation(prog, "u_ootfGamma"), 0.0f);
  glUniform1f(glGetUniformLocation(prog, "u_pqPeak"), 0.0203f);
  glUniform1f(glGetUniformLocation(prog, "u_hlgWhite"), 1.0f);
  glUniform1f(glGetUniformLocation(prog, "u_guiGamma"), 0.0f);
  const GLint wide = glGetUniformLocation(prog, "u_sdrWide");
  if (wide < 0)
    return false;
  glUniform1f(wide, sdrWide);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  // (the blending of CompositeGui(): the colour by the shader's alpha over what the buffer holds)
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  const GLfloat pos[] = {0, 0, GW, 0, 0, GH, GW, GH};
  const GLfloat uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
  const GLint posLoc = glGetAttribLocation(prog, "a_pos"), texLoc = glGetAttribLocation(prog, "a_tex");
  glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, pos);
  glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
  glEnableVertexAttribArray(posLoc);
  glEnableVertexAttribArray(texLoc);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisable(GL_BLEND);
  out.assign(GW * GH * 4, 0.0f);
  glReadPixels(0, 0, GW, GH, GL_RGBA, GL_FLOAT, out.data());
  const bool ok = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  glDeleteTextures(2, tex);
  return ok;
}

// every pixel against SDRWIDE::Composite() of the container (limited: the range of the shader's KODI_LIMITED_RANGE
// on the GUI's colour); the largest difference
static double Against(const std::vector<float>& drawn, SDRWIDE::Container container, bool limited)
{
  double worst = 0;
  for (int i = 0; i < GW * GH; ++i)
  {
    std::array<double, 3> colour, picture;
    double coverage;
    Pixel(i, colour, coverage, picture);
    const double a = std::lround(coverage * 255.0) / 255.0;
    std::array<double, 3> pre{};
    for (int k = 0; k < 3; ++k)
      pre[k] = std::lround(colour[k] * coverage * 255.0) / 255.0;
    std::array<double, 3> want = SDRWIDE::Composite(pre, a, picture, container);
    if (limited && a > 0)
    {
      // (the shader puts the GUI's colour in limited range before it is mixed by its coverage)
      std::array<double, 3> c{};
      for (int k = 0; k < 3; ++k)
        c[k] = std::fmin(std::fmax(pre[k] / a, 0.0), 1.0);
      const auto conv = SDRWIDE::ToContainer(c, container);
      for (int k = 0; k < 3; ++k)
        want[k] = (conv[k] * (219.0 / 255.0) + 16.0 / 255.0) * a + picture[k] * (1.0 - a);
    }
    for (int k = 0; k < 3; ++k)
      worst = std::fmax(worst, std::fabs(drawn[i * 4 + k] - want[k]));
  }
  return worst;
}

int main()
{
  // --- the matrices
  const Mat to709 = Inverse(ToXyz(XY709));
  (void)to709;
  const Mat m2020 = Multiply(Inverse(ToXyz(XY2020)), ToXyz(XY709));
  const Mat mP3 = Multiply(Inverse(ToXyz(XYP3)), ToXyz(XY709));
  char d[160];
  std::snprintf(d, sizeof(d), "%.2g", Apart(m2020, SDRWIDE::BT709_TO_BT2020));
  check(Apart(m2020, SDRWIDE::BT709_TO_BT2020) < 1e-8, "BT.709 to BT.2020 as made from the CIE xy", d);
  std::snprintf(d, sizeof(d), "%.2g", Apart(mP3, SDRWIDE::BT709_TO_P3_D65));
  check(Apart(mP3, SDRWIDE::BT709_TO_P3_D65) < 1e-8, "BT.709 to DCI-P3 (D65) as made from the CIE xy", d);
  bool sums = true;
  for (const Mat* m : {&SDRWIDE::BT709_TO_BT2020, &SDRWIDE::BT709_TO_P3_D65})
    for (int r = 0; r < 3; ++r)
      sums = sums && std::fabs((*m)[r][0] + (*m)[r][1] + (*m)[r][2] - 1.0) < 1e-8;
  check(sums, "every row of both matrices sums to 1: grey stays grey");
  check(&SDRWIDE::Matrix(SDRWIDE::Container::BT2020) == &SDRWIDE::BT709_TO_BT2020 &&
            &SDRWIDE::Matrix(SDRWIDE::Container::P3_D65) == &SDRWIDE::BT709_TO_P3_D65 &&
            SDRWIDE::Matrix(SDRWIDE::Container::BT709)[0][0] == 1.0 &&
            SDRWIDE::Matrix(SDRWIDE::Container::BT709)[0][1] == 0.0,
        "the matrix of each container (BT.709: none)");
  {
    // the red of BT.709 in DCI-P3: its linear light (0.822, 0.033, 0.017) through the 2.4 power
    const auto red = SDRWIDE::ToContainer({1.0, 0.0, 0.0}, SDRWIDE::Container::P3_D65);
    const auto grey = SDRWIDE::ToContainer({0.5, 0.5, 0.5}, SDRWIDE::Container::P3_D65);
    check(std::fabs(red[0] - std::pow(0.822461969, 1 / 2.4)) < 1e-9 &&
              std::fabs(red[1] - std::pow(0.033194199, 1 / 2.4)) < 1e-9 &&
              std::fabs(red[2] - std::pow(0.017082631, 1 / 2.4)) < 1e-9 && std::fabs(grey[0] - 0.5) < 1e-9 &&
              std::fabs(grey[1] - 0.5) < 1e-9 && std::fabs(grey[2] - 0.5) < 1e-9,
          "the red of BT.709 in DCI-P3, and a grey that stays the same code");
    const auto as2020 = SDRWIDE::ToBt2020({0.2, 0.7, 0.4});
    const auto viaContainer = SDRWIDE::ToContainer({0.2, 0.7, 0.4}, SDRWIDE::Container::BT2020);
    check(as2020 == viaContainer, "ToBt2020() is the container BT.2020");
  }

  // --- the setting, the primaries, the pictures it is about
  check(SDRWIDE::ContainerOfSetting(0) == SDRWIDE::Container::BT709 &&
            SDRWIDE::ContainerOfSetting(1) == SDRWIDE::Container::BT2020 &&
            SDRWIDE::ContainerOfSetting(2) == SDRWIDE::Container::P3_D65 &&
            SDRWIDE::ContainerOfSetting(3) == SDRWIDE::Container::BT709 &&
            SDRWIDE::ContainerOfSetting(-1) == SDRWIDE::Container::BT709,
        "the values of videoscreen.hadessdrhdrgamut (another: BT.709)");
  check(SDRWIDE::Primaries(SDRWIDE::Container::BT709) == AVCOL_PRI_BT709 &&
            SDRWIDE::Primaries(SDRWIDE::Container::BT2020) == AVCOL_PRI_BT2020 &&
            SDRWIDE::Primaries(SDRWIDE::Container::P3_D65) == AVCOL_PRI_SMPTE432,
        "the primaries Kodi's shaders draw into for each container");
  check(SDRWIDE::MappedCandidate(AVCOL_TRC_SMPTE2084) && SDRWIDE::MappedCandidate(AVCOL_TRC_ARIB_STD_B67) &&
            !SDRWIDE::MappedCandidate(AVCOL_TRC_BT709) && !SDRWIDE::MappedCandidate(AVCOL_TRC_UNSPECIFIED),
        "the setting is about PQ and HLG pictures");

  // --- why a container is not used
  {
    using W = SDRWIDE::Why;
    SDRWIDE::Facts f;
    f.mapped = true;
    f.setting = true;
    f.container = SDRWIDE::Container::P3_D65;
    f.display = f.driver = true; // BT.2020 is taken; DCI-P3 not
    check(SDRWIDE::Check(f) == W::DISPLAY_P3, "DCI-P3 asked for, the EDID declares BT.2020 only: not used");
    f.displayP3 = true;
    check(SDRWIDE::Check(f) == W::DRIVER_P3, "... the EDID declares it, the connector offers no DCI-P3_RGB_D65");
    f.driverP3 = true;
    check(SDRWIDE::Check(f) == W::NONE, "... both: used");
    f.display = f.driver = false;
    check(SDRWIDE::Check(f) == W::NONE, "... whatever the display and the connector say of BT.2020");
    f.container = SDRWIDE::Container::BT2020;
    check(SDRWIDE::Check(f) == W::DISPLAY, "BT.2020 asked for, the EDID declares DCI-P3 only: not used");
    f.display = true;
    check(SDRWIDE::Check(f) == W::DRIVER, "... the connector offers no BT2020_RGB");
    f.driver = true;
    check(SDRWIDE::Check(f) == W::NONE, "... both: used");
    f.stereo = true;
    check(SDRWIDE::Check(f) == W::STEREO, "not in 3D");
    f.stereo = false;
    f.nativeDV = true;
    check(SDRWIDE::Check(f) == W::DOLBY_VISION, "not while native Dolby Vision holds the output");
    f.nativeDV = false;
    f.container = SDRWIDE::Container::BT709;
    check(SDRWIDE::Check(f) == W::SETTING, "BT.709 asked for (the default): nothing to set up");
    f.container = SDRWIDE::Container::P3_D65;
    f.displayP3 = f.driverP3 = false;
    f.test = true;
    check(SDRWIDE::Check(f) == W::NONE, "a test (sdrbt2020test): neither is asked");
    check(std::string(SDRWIDE::Text(W::SETTING, true)).find("videoscreen.hadessdrhdrgamut") != std::string::npos &&
              std::string(SDRWIDE::Text(W::SETTING)).find("videoscreen.hadessdrbt2020") != std::string::npos &&
              std::string(SDRWIDE::Text(W::DISPLAY_P3)).find("DCI-P3") != std::string::npos &&
              std::string(SDRWIDE::Text(W::DRIVER_P3)).find("DCI-P3_RGB_D65") != std::string::npos,
          "each reason names its setting or its value");
    // an SDR video of BT.2020 (videoscreen.hadessdrbt2020): as before, BT.2020 by its facts
    SDRWIDE::Facts v;
    v.setting = true;
    v.display = v.driver = true;
    check(SDRWIDE::Check(v) == W::NONE && v.container == SDRWIDE::Container::BT2020,
          "an SDR video of BT.2020 goes out in BT.2020, as before");
  }

  // --- the composite through the graphics
  EGLDisplay dpy = EGL_NO_DISPLAY;
  auto get = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, nullptr);
  EGLint major = 0, minor = 0, n = 0;
  EGLConfig cfg{};
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext ctx = EGL_NO_CONTEXT;
  const bool gl = dpy != EGL_NO_DISPLAY && eglInitialize(dpy, &major, &minor) && eglBindAPI(EGL_OPENGL_ES_API) &&
                  eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) && n > 0 &&
                  (ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr)) != EGL_NO_CONTEXT &&
                  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
  check(gl, "OpenGL ES 3 on llvmpipe");
  if (gl)
  {
    std::printf("renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    for (int limited = 0; limited < 2; ++limited)
    {
      const GLuint prog = Program(limited ? "#define KODI_LIMITED_RANGE 1\n" : "");
      check(prog != 0, std::string("the tree's composite shader built") + (limited ? " (limited range)" : ""));
      if (!prog)
        continue;
      std::vector<float> wide2020, wideP3;
      const bool d1 = Draw(prog, 1.0f, wide2020), d2 = Draw(prog, 2.0f, wideP3);
      check(d1 && d2, "drawn with u_sdrWide 1 and 2");
      if (!d1 || !d2)
        continue;
      const double e2020 = Against(wide2020, SDRWIDE::Container::BT2020, limited);
      const double eP3 = Against(wideP3, SDRWIDE::Container::P3_D65, limited);
      const double cross = Against(wideP3, SDRWIDE::Container::BT2020, limited);
      std::snprintf(d, sizeof(d), "largest difference %.2g (BT.2020), %.2g (DCI-P3); DCI-P3's against BT.2020's model %.4f",
                    e2020, eP3, cross);
      const std::string range = limited ? " (limited range)" : " (full range)";
      check(e2020 < 0.5 / 1023.0, "u_sdrWide 1: every pixel the GUI converted to BT.2020 over the picture" + range, d);
      check(eP3 < 0.5 / 1023.0, "u_sdrWide 2: every pixel the GUI converted to DCI-P3 (D65) over the picture" + range,
            d);
      check(cross > 0.05, "u_sdrWide 2 is not BT.2020's conversion" + range, d);
      glDeleteProgram(prog);
    }
  }
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
