// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): the fidelity bench.
//
// Draws chosen pictures of a profile 7 FEL stream through Kodi's Dolby Vision bridge (tools/dvbridge of the
// branch under test, the files themselves) on a software OpenGL ES (zink on lavapipe), as Kodi draws them, and
// writes what each way gives, for a comparison with references outside the GPU. The layers and the metadata
// are those that ref_dump wrote (<data>/p<N>: the stream decoded and paired as the bridge's render tests do),
// so that every renderer measured gets the same inputs.
//   <out>/<tag>-p<N>-picture.rgb16  the picture of the composed way (the RGBA32F texture that Kodi draws over),
//                                  12-bit codes, three uint16 a pixel, rows as the texture holds them
//   <out>/<tag>-p<N>-picture.f32    the same as floats (FB_FLOATS=1 only)
//   <out>/<tag>-p<N>-direct.rgba    the transport of the direct way (RGBA8 3840x2160 as read back)
//   <out>/<tag>-p<N>-composed.rgba  the transport packed from the composed way (dvbridge_render_pack())
//   <out>/<tag>-p<N>-ccm_{y,cb,cr}.u16  with the source transport: the composed planes that libplacebo's
//                                  composer handed the bridge (h of ETSI GS CCM 001 5.4.3.3, 4:2:0)
//   <out>/<tag>-p<N>-info.txt       what the renderer said of the picture
// Settings, as Kodi's switches set them: FB_CCM (default 1: the composer of CCM 001), FB_SOURCE (default 1: the
// source transport), FB_STEP (default 0: the fidelity step-down level), FB_HALF (default 0; 1: every second
// sample of the layers, a 1920x1080 picture scaled to the raster), FB_FLOATS (default 0); FB_HDR10=1: only the
// picture for output as HDR10 (<tag>-p<N>-hdr10.f32); FB_GUI=<drawing>: a GUI composited over the composed way
// before it is packed (<tag>-p0-gui.rgba8; FB_TILES=0 without telling the bridge what it is drawn over).
// Usage: fidbench <data dir> <out dir> <tag> <picture> [<picture> ...]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixfmt.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#if __has_include("dvbridge_source.h")
#include "dvbridge_source.h"
#endif
#include "layers.h"
#include "upload.h"

#include "pack_sources.h"

#define W 3840
#define H 2160

static int envi(const char* name, int fallback)
{
  const char* v = getenv(name);
  return v && *v ? atoi(v) : fallback;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_packer* packer;
  GLuint fbo, color;
  pl_tex scanout;
  uint8_t* bytes;
  float* floats;
  int subgroups;
};

static int ReadRgba8(struct gl* g, uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

// a texture of floats as RGBA floats (the format that OpenGL ES always reads a float attachment in)
static int ReadFloatTexture(GLuint texture, int w, int h, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  while (glGetError() != GL_NO_ERROR)
    ;
  if (ok)
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, into);
  ok = ok && glGetError() == GL_NO_ERROR;
  // (a 16-bit normalized texture, as the composed planes are with PL_DOVI_CCM_PLANES_UNORM16, is read as unsigned
  // shorts)
  if (!ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
  {
    unsigned short* s = malloc((size_t)w * h * 8);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT, s);
    ok = s && glGetError() == GL_NO_ERROR;
    for (size_t i = 0; ok && i < (size_t)w * h * 4; ++i)
      into[i] = s[i] / 65535.0f;
    free(s);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return ok;
}

static void Fill(struct gl* g, float v)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(v, v, v, v);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int WriteFile(const char* dir, const char* tag, unsigned number, const char* what, const void* data,
                     size_t size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s-p%u-%s", dir, tag, number, what);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  const int ok = fwrite(data, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

static int g_failures;

// --- FB_GUI: a GUI texture composited over the picture of the composed way with the blend factors of Kodi's
// CompositeGui() (SRC_ALPHA, ONE_MINUS_SRC_ALPHA; alpha ONE, ONE_MINUS_SRC_ALPHA), its colours taken as they are
// (Kodi's composite also converts them for HDR: that is the GUI's look, not the video's). The drawings are the
// same upside down, so that the orientation of a texture never matters to the comparison.
//   1 "bands": a colour G over the whole width at alpha 255 (rows 0-199), 128 (300-499) and 1 (600-799), the same
//     mirrored at the bottom, nothing elsewhere
//   2 "subs": white texels at alpha 255 in a pattern of text, rows 400-459 and 1700-1759, columns 1200-2639
static GLuint g_gui, g_prog, g_vao, g_fb;
static uint8_t* g_guipix;

static GLuint Shader(GLenum type, const char* text)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &text, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[1024];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    printf("FAIL composite shader: %s\n", log);
  }
  return s;
}

static int GuiMake(int drawing)
{
  g_guipix = calloc((size_t)W * H, 4);
  if (!g_guipix)
    return 0;
  for (int y = 0; y < H; ++y)
  {
    const int m = y < H - 1 - y ? y : H - 1 - y; // mirrored
    for (int x = 0; x < W; ++x)
    {
      uint8_t* p = g_guipix + ((size_t)y * W + x) * 4;
      if (drawing == 1)
      {
        const int a = m < 200 ? 255 : m >= 300 && m < 500 ? 128 : m >= 600 && m < 800 ? 1 : 0;
        if (a)
          p[0] = 204, p[1] = 77, p[2] = 51, p[3] = (uint8_t)a;
      }
      else if (m >= 400 && m < 460 && x >= 1200 && x < 2640 && (x * 7 + m * 13) % 11 < 3)
        p[0] = p[1] = p[2] = p[3] = 255;
    }
  }
  glGenTextures(1, &g_gui);
  glBindTexture(GL_TEXTURE_2D, g_gui);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_guipix);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  const char* vs = "#version 300 es\nvoid main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
                   "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
  const char* fs = "#version 300 es\nprecision highp float;uniform highp sampler2D gui;out vec4 o;"
                   "void main(){o=texelFetch(gui,ivec2(gl_FragCoord.xy),0);}\n";
  g_prog = glCreateProgram();
  glAttachShader(g_prog, Shader(GL_VERTEX_SHADER, vs));
  glAttachShader(g_prog, Shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(g_prog);
  GLint ok = 0;
  glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
  glGenVertexArrays(1, &g_vao);
  glGenFramebuffers(1, &g_fb);
  return ok && glGetError() == GL_NO_ERROR;
}

static int GuiComposite(GLuint picture)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, picture, 0);
  int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glViewport(0, 0, W, H);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(g_prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_gui);
  glUniform1i(glGetUniformLocation(g_prog, "gui"), 0);
  glBindVertexArray(g_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_BLEND);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  ok = ok && glGetError() == GL_NO_ERROR;
  return ok;
}

// --- FB_HDR10: the picture for output as HDR10 (dvbridge_render_hdr10(), the mapping off, as Kodi's conversion
// to HDR10 draws it), RGB floats, rows as the texture holds them (its first row is the top one)
static void DrawHdr10(struct gl* g, struct dvbridge_gl_frame* b, const AVDOVIMetadata* meta, size_t bytes,
                      double pts, double elPts, int w, int h, const char* dir, const char* tag, unsigned number)
{
  const struct dvbridge_view whole = {0, 0, (float)w, (float)h, 0, 0, W, H};
  const int ok = dvbridge_render_hdr10(g->renderer, &b->frame, meta, bytes, pts, elPts, whole, W, H);
  pl_tex tex = ok ? dvbridge_render_hdr10_texture(g->renderer) : NULL;
  const GLuint id = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  if (!id || !ReadFloatTexture(id, W, H, g->floats))
  {
    printf("FAIL picture %u: the HDR10 picture (render %d)\n", number, ok);
    ++g_failures;
    return;
  }
  float* f3 = malloc((size_t)W * H * 12);
  for (size_t i = 0; i < (size_t)W * H; ++i)
    for (int k = 0; k < 3; ++k)
      f3[i * 3 + k] = g->floats[i * 4 + k];
  if (!WriteFile(dir, tag, number, "hdr10.f32", f3, (size_t)W * H * 12))
    ++g_failures;
  free(f3);
  char text[256];
  snprintf(text, sizeof text, "picture %u tag %s: HDR10 picture drawn, residual drawn %d, composer %s\n", number, tag,
           dvbridge_render_residual_drawn(g->renderer), dvbridge_render_ccm_composer(g->renderer) ? "ccm" : "reshaping");
  printf("%s", text);
  WriteFile(dir, tag, number, "info.txt", text, strlen(text));
}

static void Draw(struct gl* g, const struct fid_gl_layer* bl, const struct fid_gl_layer* el,
                 const AVDOVIMetadata* meta, size_t bytes, double pts, const char* dir, const char* tag,
                 unsigned number)
{
  struct dvbridge_geometry geometry = {bl->w, bl->h, 0, 0, W, H};
  struct dvbridge_gl_plane bp[3] = {{bl->tex[0], bl->w, bl->h}, {bl->tex[1], (bl->w + 1) / 2, (bl->h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_plane ep[3] = {{el->tex[0], el->w, el->h}, {el->tex[1], (el->w + 1) / 2, (el->h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0}, e = {0};
#ifdef DVBRIDGE_GL_FRAME_CHROMA
  const int bok = dvbridge_gl_frame_import(&b, g->gl->gpu, DVBRIDGE_P010, bl->w, bl->h, bp, (enum AVChromaLocation)bl->loc);
  const int eok = dvbridge_gl_frame_import(&e, g->gl->gpu, DVBRIDGE_P010, el->w, el->h, ep, (enum AVChromaLocation)el->loc);
#else
  const int bok = dvbridge_gl_frame_import(&b, g->gl->gpu, DVBRIDGE_P010, bl->w, bl->h, bp);
  const int eok = dvbridge_gl_frame_import(&e, g->gl->gpu, DVBRIDGE_P010, el->w, el->h, ep);
#endif
  if (!bok || !eok)
  {
    printf("FAIL picture %u: layers not imported\n", number);
    ++g_failures;
    return;
  }
  // FB_NOEL=1: the base layer and the RPU alone (as a player that does not decode the enhancement layer)
  const int noel = envi("FB_NOEL", 0);
  b.frame.enhancement_layer = noel ? NULL : &e.frame;
  const double elPts = noel ? NAN : pts;
  if (envi("FB_HDR10", 0))
  {
    DrawHdr10(g, &b, meta, bytes, pts, elPts, bl->w, bl->h, dir, tag, number);
    dvbridge_gl_frame_release(&e);
    dvbridge_gl_frame_release(&b);
    return;
  }
  const int gui = envi("FB_GUI", 0);
  char text[2048];
  int n = 0;
  // --- the composed way
  Fill(g, 0.5f);
#ifdef DVBRIDGE_RENDER_DRAWN
  // as CDVBridgeGLES::SetDrawnOver(): what the GUI will be drawn over, before the frame is prepared (FB_TILES=0:
  // advancedsettings.xml dvdrawntiles false)
  if (gui && envi("FB_TILES", 1))
    dvbridge_render_set_drawn(g->renderer, g_gui, W, H);
#endif
  const int rgb = dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry);
  unsigned margins[4] = {0}, count = 0;
  int source = 0;
  const char* why = NULL;
  if (rgb)
  {
    const struct dvbridge_candidate* candidate = dvbridge_render_candidate(g->renderer);
    dvbridge_packets(candidate, &count);
    dvbridge_active_area(candidate, margins);
#ifdef DVBRIDGE_SOURCE_TAPS
    source = dvbridge_render_source(g->renderer, &why);
#endif
    pl_tex tex = dvbridge_render_texture(g->renderer);
    const GLuint id = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
    if (gui)
    {
      // the picture is not read (made in tiles, it is whole only where the GUI reaches): the GUI goes over it
      if (!id || !GuiComposite(id))
      {
        printf("FAIL picture %u: the GUI not composited\n", number);
        ++g_failures;
      }
    }
    else if (id && ReadFloatTexture(id, W, H, g->floats))
    {
      uint16_t* v = malloc((size_t)W * H * 6);
      float* f3 = malloc((size_t)W * H * 12);
      for (size_t i = 0; i < (size_t)W * H; ++i)
        for (int k = 0; k < 3; ++k)
        {
          float x = g->floats[i * 4 + k];
          f3[i * 3 + k] = x;
          x = x < 0 ? 0 : x > 1 ? 1 : x;
          v[i * 3 + k] = (uint16_t)(x * 4095.0f + 0.5f);
        }
      if (!WriteFile(dir, tag, number, "picture.rgb16", v, (size_t)W * H * 6) ||
          (envi("FB_FLOATS", 0) && !WriteFile(dir, tag, number, "picture.f32", f3, (size_t)W * H * 12)))
        ++g_failures;
      free(v);
      free(f3);
    }
    else
    {
      printf("FAIL picture %u: the picture of the composed way not read\n", number);
      ++g_failures;
    }
#ifdef DVBRIDGE_SOURCE_TAPS
    unsigned planes[2] = {0, 0};
    if (source && dvbridge_render_source_planes(g->renderer, planes))
    {
      const int cw = (bl->w + 1) / 2, ch = (bl->h + 1) / 2;
      float* buf = malloc((size_t)bl->w * bl->h * 16);
      uint16_t* out = malloc((size_t)bl->w * bl->h * 2);
      long off = 0;
      if (ReadFloatTexture(planes[0], bl->w, bl->h, buf))
      {
        for (size_t i = 0; i < (size_t)bl->w * bl->h; ++i)
        {
          const double hv = buf[i * 4] * 4096.0;
          const long r = lround(hv);
          off += fabs(hv - r) > 1e-3;
          out[i] = (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r);
        }
        WriteFile(dir, tag, number, "ccm_y.u16", out, (size_t)bl->w * bl->h * 2);
      }
      else
        ++g_failures;
      if (ReadFloatTexture(planes[1], cw, ch, buf))
      {
        for (int k = 0; k < 2; ++k)
        {
          for (size_t i = 0; i < (size_t)cw * ch; ++i)
          {
            const double hv = buf[i * 4 + k] * 4096.0;
            const long r = lround(hv);
            off += fabs(hv - r) > 1e-3;
            out[i] = (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r);
          }
          WriteFile(dir, tag, number, k ? "ccm_cr.u16" : "ccm_cb.u16", out, (size_t)cw * ch * 2);
        }
      }
      else
        ++g_failures;
      n += snprintf(text + n, sizeof text - n, "composed planes: written; samples not on a whole code: %ld\n", off);
      free(buf);
      free(out);
    }
#endif
    Fill(g, 0.25f);
    // with a GUI: the way that waits, the edges drawn over measured (DVBRIDGE_COVER_MEASURE)
    if (dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, gui ? DVBRIDGE_COVER_MEASURE : DVBRIDGE_COVER_NONE) &&
        ReadRgba8(g, g->bytes))
      WriteFile(dir, tag, number, "composed.rgba", g->bytes, (size_t)W * H * 4);
    else
    {
      printf("FAIL picture %u: the composed way not packed\n", number);
      ++g_failures;
    }
  }
  else
  {
    printf("FAIL picture %u: dvbridge_render_rgb() failed\n", number);
    ++g_failures;
  }
  const int residual = dvbridge_render_residual_drawn(g->renderer);
  // --- the direct way
  int direct = 0, dsource = 0;
  if (g->subgroups || source)
  {
    Fill(g, 0.75f);
    direct = dvbridge_render_packed(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry, g->scanout, true) &&
             ReadRgba8(g, g->bytes);
    if (direct)
      WriteFile(dir, tag, number, "direct.rgba", g->bytes, (size_t)W * H * 4);
#ifdef DVBRIDGE_SOURCE_TAPS
    const char* dwhy = NULL;
    dsource = direct && dvbridge_render_source(g->renderer, &dwhy);
#endif
  }
  n += snprintf(text + n, sizeof text - n,
                "picture %u tag %s: source %dx%d, residual drawn %d, source transport %d (%s), direct %d (source %d), "
                "packets %u, margins %u %u %u %u, step %d, composer %s\n",
                number, tag, bl->w, bl->h, residual, source, why ? why : "-", direct, dsource, count, margins[0],
                margins[1], margins[2], margins[3], dvbridge_render_step(g->renderer),
                dvbridge_render_ccm_composer(g->renderer) ? "ccm" : "reshaping");
  printf("%s", text);
  WriteFile(dir, tag, number, "info.txt", text, strlen(text));
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
}

int main(int argc, char** argv)
{
  if (argc < 5)
    return 2;
  const char* data = argv[1];
  const char* dir = argv[2];
  const char* tag = argv[3];
  const int half = envi("FB_HALF", 0);

  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, nc = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLConfig cfg = NULL;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nc) || !nc)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
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
  // the conversion to HDR10 draws with a renderer of its own, as CDVBridgeGLES makes it for that
  g.renderer = envi("FB_HDR10", 0) ? dvbridge_renderer_create_hdr10(g.gl->gpu, g.log)
                                   : dvbridge_renderer_create(g.gl->gpu, g.log);
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  if (!g.renderer || !g.packer)
    return 1;
  if (envi("FB_GUI", 0))
  {
    if (!GuiMake(envi("FB_GUI", 0)))
      return 1;
    WriteFile(dir, tag, 0, "gui.rgba8", g_guipix, (size_t)W * H * 4);
  }
  dvbridge_render_use_ccm_composer(g.renderer, envi("FB_CCM", 1) != 0);
#ifdef DVBRIDGE_SOURCE_TAPS
  dvbridge_render_use_source_transport(g.renderer, envi("FB_SOURCE", 1) != 0);
#endif
  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));
  printf("settings: composer %s, source transport asked %d, step %d, half %d\n",
         dvbridge_render_ccm_composer(g.renderer) ? "ccm" : "reshaping", envi("FB_SOURCE", 1),
         dvbridge_render_step(g.renderer), half);
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g.scanout = pl_opengl_wrap(g.gl->gpu, pl_opengl_wrap_params(.texture = g.color, .target = GL_TEXTURE_2D,
                                                              .iformat = GL_RGBA8, .width = W, .height = H));
  g.bytes = malloc((size_t)W * H * 4);
  g.floats = malloc((size_t)W * H * 16);
  if (!complete || !g.scanout || !g.bytes || !g.floats)
    return 1;
  {
    GLint size = 0, stages = 0, features = 0;
    if (pl_opengl_has_ext(g.gl, "GL_KHR_shader_subgroup"))
    {
      glGetIntegerv(0x9532, &size);
      glGetIntegerv(0x9533, &stages);
      glGetIntegerv(0x9534, &features);
    }
    g.subgroups = size >= 4 && (stages & GL_FRAGMENT_SHADER_BIT) && (features & 0x81) == 0x81;
    while (glGetError() != GL_NO_ERROR)
      ;
    printf("shader subgroups: %s\n", g.subgroups ? "yes" : "no");
  }
  unsigned drawn = 0;
  struct fid_gl_layer bl = {0}, el = {0};
  for (int i = 4; i < argc; ++i)
  {
    const unsigned number = (unsigned)atoi(argv[i]);
    char pdir[1024];
    snprintf(pdir, sizeof pdir, "%s/p%u", data, number);
    struct fid_picture p;
    if (!fid_load(pdir, &p) || !fid_upload(&bl, p.bl, p.bl_loc, half ? 2 : 1) ||
        !fid_upload(&el, p.el, p.el_loc, half ? 2 : 1))
    {
      printf("FAIL picture %u: not loaded\n", number);
      ++g_failures;
      continue;
    }
    if (p.bl_pts != p.el_pts)
      printf("NOTE picture %u: the layers' times differ (%lld, %lld)\n", number, p.bl_pts, p.el_pts);
    Draw(&g, &bl, &el, (const AVDOVIMetadata*)p.meta, p.meta_size, p.bl_pts / 1000.0, dir, tag, number);
    ++drawn;
    fid_free(&p);
    fflush(stdout);
  }
  printf("%s: %u of %d pictures drawn, %d failures\n", drawn == (unsigned)(argc - 4) && !g_failures ? "FIDBENCH-OK" : "FIDBENCH-FAILED",
         drawn, argc - 4, g_failures);
  fflush(stdout);
  _Exit(drawn == (unsigned)(argc - 4) && !g_failures ? 0 : 1);
}
