// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the pack of the converted transport (dvbridge_gl_pack(), tools/dvbridge/dvbridge_gl_pack.c)
// on a software OpenGL ES (zink on lavapipe): the GL state that it leaves (the state of dvbridge_gl_state_save()
// and the clear colour, read before and after, with an unusual state set first: a program, a vertex array and an
// array buffer bound, other draw and read framebuffers, blending, scissor and dither on, a scissor box, blend
// functions, a viewport, a colour mask, an unpack alignment, textures and samplers on four units, another active
// unit), and the bytes that it writes (a hash), with and without metadata, in three pictures: it must leave the
// state as it was; the runner compares the bytes with a reference and proves the checks with mutants of
// dvbridge_gl_pack.c.
// usage: pack_state_test
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_pack.h"
#include "pack_sources.h"

#define W 3840
#define H 2160

static int g_failures, g_checks;
static void Check(int ok, const char* what)
{
  ++g_checks;
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static uint64_t Hash(const uint8_t* p, size_t n)
{
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i)
    h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

static GLuint Texture(GLenum format, int w, int h)
{
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

static GLuint Framebuffer(GLuint t)
{
  GLuint f;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return f;
}

struct snapshot
{
  struct dvbridge_gl_state s;
  GLfloat clear[4];
};

static void Snap(struct snapshot* s)
{
  memset(s, 0, sizeof(*s));
  dvbridge_gl_state_save(&s->s);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, s->clear);
}

int main(void)
{
  setvbuf(stdout, NULL, _IOLBF, 0);
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
  EGLint pbAttr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("pack state test, %s; renderer: %s\n",
         "the new bridge",
         glGetString(GL_RENDERER));
  struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  Check(packer != NULL, "the packer is made");
  if (!packer)
    return 1;
  // the picture to pack: PQ RGB with an alpha of 2 (nothing drawn over it), a pattern
  const GLuint picture = Texture(GL_RGBA32F, W, H);
  float* rows = malloc(sizeof(float) * 4 * W * 16);
  const GLuint target = Texture(GL_RGBA8, W, H), fbo = Framebuffer(target);
  uint8_t* bytes = malloc((size_t)W * H * 4);
  uint32_t packets[512];
  // other state of a caller (what Kodi's GUI rendering leaves, and more)
  const GLuint other = Texture(GL_RGBA8, 16, 16), other2 = Texture(GL_RGBA8, 16, 16), readFbo = Framebuffer(other),
               drawFbo = Framebuffer(other2);
  GLuint samplers[2], vao, buffer;
  glGenSamplers(2, samplers);
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &buffer);
  const char* vs = "#version 300 es\nvoid main() { gl_Position = vec4(0.0); }\n";
  const char* fs = "#version 300 es\nprecision mediump float;\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n";
  GLuint program = glCreateProgram(), s1 = glCreateShader(GL_VERTEX_SHADER), s2 = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(s1, 1, &vs, NULL);
  glCompileShader(s1);
  glShaderSource(s2, 1, &fs, NULL);
  glCompileShader(s2);
  glAttachShader(program, s1);
  glAttachShader(program, s2);
  glLinkProgram(program);
  for (int pic = 0; pic < 3; ++pic)
  {
    for (int y = 0; y < H; y += 16)
    {
      for (int i = 0; i < W * 16; ++i)
      {
        const int x = i % W, yy = y + i / W;
        rows[i * 4 + 0] = (float)((x * 7 + yy * 3 + pic * 101) % 1024) / 1023.0f;
        rows[i * 4 + 1] = (float)((x * 5 + yy * 11 + pic * 37) % 1024) / 1023.0f;
        rows[i * 4 + 2] = (float)((x * 13 + yy * 2 + pic * 59) % 1024) / 1023.0f;
        rows[i * 4 + 3] = 2.0f;
      }
      glBindTexture(GL_TEXTURE_2D, picture);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, W, 16, GL_RGBA, GL_FLOAT, rows);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    for (unsigned i = 0; i < 512; ++i)
      packets[i] = 0x9E3779B9u * (i + 1 + 1000u * (unsigned)pic);
    for (int with = 0; with < 2; ++with)
    {
      // the state of the caller
      glUseProgram(program);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
      glEnable(GL_BLEND);
      glEnable(GL_SCISSOR_TEST);
      glEnable(GL_DITHER);
      glScissor(1, 2, 3, 4);
      glViewport(5, 6, 7, 8);
      glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
      glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
      for (int u = 0; u < 4; ++u)
      {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, u & 1 ? other : other2);
        glBindSampler(u, samplers[u & 1]);
      }
      glActiveTexture(GL_TEXTURE3);
      struct snapshot before, after;
      Snap(&before);
      const int ok = with ? dvbridge_gl_pack(packer, picture, fbo, W, H, packets, 1 + (unsigned)pic * 7, pic == 1)
                          : dvbridge_gl_pack_no_metadata(packer, picture, fbo, W, H, pic == 1);
      Snap(&after);
      const int same = !memcmp(&before, &after, sizeof(before));
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
      printf("pack: picture %d, %s: bytes %016llx\n", pic, with ? "with metadata" : "without metadata",
             (unsigned long long)Hash(bytes, (size_t)W * H * 4));
      char line[160];
      snprintf(line, sizeof line, "picture %d, %s: packed, and the GL state is as it was%s", pic,
               with ? "with metadata" : "without metadata", same ? "" : " (NOT)");
      Check(ok && same && glGetError() == GL_NO_ERROR, line);
    }
  }
  printf("%d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
