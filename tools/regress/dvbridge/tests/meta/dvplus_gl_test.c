// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades 2026-10-27, TEST ONLY: Dolby Vision sent as HDR10+, the samples taken on the GPU (tools/dvbridge/dvbridge_dvplus_gl.c),
// on a software OpenGL ES (zink on lavapipe):
//   G1 a float texture of known values (some below 0 and above 1), a place inside it: every sample of the GPU is the
//      pixel the grid names, maxRGB clamped to [0, 1], as a 16-bit code, against the same computed on the CPU;
//   G2 the GL state of the caller is kept (program, framebuffers, viewport, texture and sampler of unit 0, active
//      unit, pixel pack buffer and alignment, blend and scissor);
//   G3 four in flight at most, and they come back in order;
//   G4 the pictures of a Dolby Vision stream rebuilt as for output as HDR10 (dvbridge_render_hdr10(), the RPU of each
//      picture): the GPU's samples against the picture read back, and the statistics of the samples against those of
//      every pixel (InfoFrame codes); the decisions (dvbridge_dvplus.c) on them with the measurement of each picture
//      taken one picture later, the scenes of the RPU.
// usage: dvplus_gl_test <Dolby Vision file> <pictures>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_dvplus.h"
#include "dvbridge_dvplus_gl.h"
#include "dvbridge_gl_frame.h"
#include "dvbridge_render.h"

static int g_failures, g_passes;
static double NowMs(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (ok)
    ++g_passes;
  else
    ++g_failures;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

struct got
{
  double pts[16];
  uint16_t* codes[16];
  uint16_t* luma[16];
  size_t count[16];
  int n;
};
static void Got(double pts, const uint16_t* codes, const uint16_t* luma, size_t count, void* context)
{
  struct got* g = context;
  if (g->n >= 16)
    return;
  g->pts[g->n] = pts;
  g->codes[g->n] = malloc(count * sizeof(uint16_t));
  memcpy(g->codes[g->n], codes, count * sizeof(uint16_t));
  g->luma[g->n] = malloc(count * sizeof(uint16_t));
  memcpy(g->luma[g->n], luma, count * sizeof(uint16_t));
  g->count[g->n] = count;
  ++g->n;
}
static void GotFree(struct got* g)
{
  for (int i = 0; i < g->n; ++i)
    free(g->codes[i]), free(g->luma[i]);
  memset(g, 0, sizeof(*g));
}
static void CollectAll(struct dvbridge_dvplus_gl* s, struct got* g)
{
  for (int i = 0; i < 200; ++i)
  {
    glFinish();
    if (!dvbridge_dvplus_gl_collect(s, Got, g))
      return;
  }
}

// the samples that the grid names, from a float RGBA picture read back (first row the top)
static size_t Expected(const float* rgba, int width, int x, int y, int w, int h, uint16_t* out)
{
  int cols, rows;
  dvbridge_dvplus_gl_grid(w, h, &cols, &rows);
  size_t n = 0;
  for (int j = 0; j < rows; ++j)
    for (int i = 0; i < cols; ++i)
    {
      const int sx = x + (2 * i + 1) * w / (2 * cols), sy = y + (2 * j + 1) * h / (2 * rows);
      const float* p = rgba + ((size_t)sy * width + sx) * 4;
      double m = 0;
      for (int c = 0; c < 3; ++c)
      {
        const double v = p[c] < 0 ? 0 : p[c] > 1 ? 1 : p[c];
        m = v > m ? v : m;
      }
      out[n++] = (uint16_t)floor(m * 65535.0 + 0.5);
    }
  return n;
}

// the luminance of the samples that the grid names (BT.2020, of the linear R, G and B, ST 2084 each way), as a
// 16-bit PQ code, in float64
static double Eotf(double e)
{
  const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
               c3 = 2392.0 / 4096 * 32;
  const double p = pow(e, 1 / m2);
  return pow((p - c1 > 0 ? p - c1 : 0) / (c2 - c3 * p), 1 / m1);
}
static double Oetf(double y)
{
  const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
               c3 = 2392.0 / 4096 * 32;
  const double p = pow(y, m1);
  return pow((c1 + c2 * p) / (1 + c3 * p), m2);
}
static size_t ExpectedLuma(const float* rgba, int width, int x, int y, int w, int h, uint16_t* out)
{
  int cols, rows;
  dvbridge_dvplus_gl_grid(w, h, &cols, &rows);
  size_t n = 0;
  for (int j = 0; j < rows; ++j)
    for (int i = 0; i < cols; ++i)
    {
      const int sx = x + (2 * i + 1) * w / (2 * cols), sy = y + (2 * j + 1) * h / (2 * rows);
      const float* p = rgba + ((size_t)sy * width + sx) * 4;
      double c[3];
      for (int k = 0; k < 3; ++k)
        c[k] = Eotf(p[k] < 0 ? 0 : p[k] > 1 ? 1 : p[k]);
      const double lum = 0.2627 * c[0] + 0.6780 * c[1] + 0.0593 * c[2];
      out[n++] = (uint16_t)floor(Oetf(lum) * 65535.0 + 0.5);
    }
  return n;
}

static int Compare(const uint16_t* a, const uint16_t* b, size_t n, size_t* off1, size_t* offMore)
{
  *off1 = *offMore = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const int d = abs((int)a[i] - (int)b[i]);
    *off1 += d == 1;
    *offMore += d > 1;
  }
  return *offMore == 0;
}

static GLuint FramebufferOf(GLuint texture)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return fb;
}

static void ReadFloat(GLuint texture, int w, int h, float* out)
{
  GLuint fb = FramebufferOf(texture);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
}

// yuv420p10le to P010 textures (as hdr10plus_bridge_test.c)
static int Upload(GLuint tex[2], struct dvbridge_gl_plane planes[3], const AVFrame* f)
{
  if (f->format != AV_PIX_FMT_YUV420P10LE)
    return 0;
  const int w = f->width, h = f->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
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
  if (!tex[0])
    glGenTextures(2, tex);
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, y);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, cw, ch, 0, GL_RG, GL_UNSIGNED_SHORT, c);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  free(y);
  free(c);
  planes[0] = (struct dvbridge_gl_plane){tex[0], w, h};
  planes[1] = (struct dvbridge_gl_plane){tex[1], cw, ch};
  planes[2] = (struct dvbridge_gl_plane){0, 0, 0};
  return glGetError() == GL_NO_ERROR;
}

static void StatsOfCodes(const struct dvbridge_dvplus* m, const uint16_t* codes, size_t n, struct dvbridge_dvplus_stats* s)
{
  static struct dvbridge_dvplus_picture p;
  static uint64_t h[DVBRIDGE_DVPLUS_BINS];
  memset(&p, 0, sizeof(p));
  dvbridge_dvplus_picture_add(m, &p, codes, NULL, n);
  for (int b = 0; b < DVBRIDGE_DVPLUS_BINS; ++b)
    h[b] = p.hist[b];
  dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, p.count, p.sum, s);
}

static int MaxCodeDiff(const struct dvbridge_dvplus_stats* a, const struct dvbridge_dvplus_stats* b)
{
  int worst = abs((int)dvbridge_dvplus_vsif_code(a->average) - (int)dvbridge_dvplus_vsif_code(b->average));
  for (int i = 0; i < DVBRIDGE_DVPLUS_PLACES; ++i)
  {
    const int d = abs((int)dvbridge_dvplus_vsif_code(a->percentiles[i]) - (int)dvbridge_dvplus_vsif_code(b->percentiles[i]));
    worst = d > worst ? d : worst;
  }
  return worst;
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  const int wanted = atoi(argv[2]);
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
  // (OpenGL ES 3.0, as the sampler asks for: no compute, no 3.1)
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  struct dvbridge_dvplus_gl* sampler = dvbridge_dvplus_gl_create();
  Check(sampler != NULL, "the sampler is made (OpenGL ES 3.0)");
  if (!sampler)
    return 1;
  struct dvbridge_dvplus* m = dvbridge_dvplus_create(NULL);

  // G1
  const int W = 3840, H = 2160;
  float* rgba = malloc((size_t)W * H * 16);
  srand(11);
  for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    rgba[i] = (float)((rand() % 12000) / 10000.0 - 0.1); // -0.1 .. 1.1
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, rgba);
  // (filters that leave a float texture incomplete for a sampler that filters: the sampler must not depend on them)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  uint16_t* want = malloc((size_t)W * H * 2);
  struct got got = {0};
  {
    const int px = 100, py = 276, pw = 3640, ph = 1608;
    Check(dvbridge_dvplus_gl_submit(sampler, tex, W, H, px, py, pw, ph, 1.0), "G1 queued");
    CollectAll(sampler, &got);
    const size_t nw = Expected(rgba, W, px, py, pw, ph, want);
    size_t o1 = 0, om = 0;
    Check(got.n == 1 && got.count[0] == nw && got.pts[0] == 1.0, "G1 one result, of the grid's size, its pts");
    if (got.n == 1 && got.count[0] == nw)
    {
      const int same = Compare(got.codes[0], want, nw, &o1, &om);
      printf("     G1: %zu samples, %zu one code off, %zu more\n", nw, o1, om);
      Check(same && o1 * 100 <= nw, "G1 every sample the pixel the grid names (one code off at most, in under 1 %: the rounding of float32 against float64)");
      // the luminance of each sample against float64
      ExpectedLuma(rgba, W, px, py, pw, ph, want);
      int worst = 0;
      size_t off = 0;
      for (size_t i = 0; i < nw; ++i)
      {
        const int dd = abs((int)got.luma[0][i] - (int)want[i]);
        worst = dd > worst ? dd : worst;
        off += dd > 0;
      }
      printf("     G1 luminance: %zu samples, %zu off, at most %d codes of 65535\n", nw, off, worst);
      Check(worst <= 2, "G1 the luminance of every sample within 2 codes of 65535 of float64 (the float32 of a GPU)");
    }
    Check(!dvbridge_dvplus_gl_submit(sampler, tex, W, H, 3000, 0, 1000, 100, 2.0), "G1 a place outside the texture: refused");
    GotFree(&got);
  }
  // G2
  {
    GLuint prog = glCreateProgram(); // (an empty program object: only its name is compared)
    GLuint other = 0, fbTex = 0, buffer = 0, samp = 0;
    glGenTextures(1, &other);
    glGenTextures(1, &fbTex);
    glBindTexture(GL_TEXTURE_2D, fbTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
    GLuint fb = FramebufferOf(fbTex);
    glGenBuffers(1, &buffer);
    glGenSamplers(1, &samp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, other);
    glBindSampler(0, samp);
    glActiveTexture(GL_TEXTURE3);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glViewport(1, 2, 3, 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    GLint progBefore = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &progBefore);
    Check(dvbridge_dvplus_gl_submit(sampler, tex, W, H, 0, 0, W, H, 3.0), "G2 queued");
    GLint v[4] = {0}, b = 0, t = 0, s = 0, a = 0, pk = 0, al = 0, p = 0, d = 0, r = 0;
    glGetIntegerv(GL_VIEWPORT, v);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &a);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
    glGetIntegerv(GL_SAMPLER_BINDING, &s);
    glActiveTexture(a);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pk);
    glGetIntegerv(GL_PACK_ALIGNMENT, &al);
    glGetIntegerv(GL_CURRENT_PROGRAM, &p);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &d);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &r);
    b = glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST);
    Check(v[0] == 1 && v[1] == 2 && v[2] == 3 && v[3] == 4 && a == GL_TEXTURE3 && t == (GLint)other &&
              s == (GLint)samp && pk == (GLint)buffer && al == 8 && p == progBefore && d == (GLint)fb &&
              r == (GLint)fb && b,
          "G2 the caller's GL state is kept");
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    CollectAll(sampler, &got);
    GotFree(&got);
  }
  // G3
  {
    int queued = 0;
    for (int i = 0; i < 5; ++i)
      queued += dvbridge_dvplus_gl_submit(sampler, tex, W, H, 0, 0, 1920, 1080, 10.0 + i);
    Check(queued == 4, "G3 four in flight at most");
    CollectAll(sampler, &got);
    Check(got.n == 4 && got.pts[0] == 10 && got.pts[1] == 11 && got.pts[2] == 12 && got.pts[3] == 13,
          "G3 they come back in order");
    GotFree(&got);
  }
  glDeleteTextures(1, &tex);

  // G4
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  struct dvbridge_renderer* renderer = gl ? dvbridge_renderer_create_hdr10(gl->gpu, log) : NULL;
  Check(renderer != NULL, "G4 a renderer for output as HDR10");
  AVFormatContext* fc = NULL;
  if (!renderer || avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int vs = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  const AVCodec* codec = avcodec_find_decoder(fc->streams[vs]->codecpar->codec_id);
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, fc->streams[vs]->codecpar);
  if (avcodec_open2(dec, codec, NULL) < 0)
    return 1;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  GLuint planes[2] = {0, 0};
  struct dvbridge_gl_plane gp[3];
  int done = 0, drawn = 0, worstSampled = 0, sentPictures = 0, worstScene = 0, scenes = 0;
  size_t allOff1 = 0, allOffMore = 0, allSamples = 0;
  struct dvbridge_dvplus_stats sentLast;
  // what was measured of the pictures before (one picture of latency), and every pixel of the scene so far
  static uint64_t fullScene[DVBRIDGE_DVPLUS_BINS];
  uint64_t fullCount = 0;
  double fullSum = 0;
  int pendingValid = 0;
  int starts = 0, startsOwn = 0;
  double renderMs = 0, waitMs = 0, takeMs = 0, waitMax = 0;
  int timed = 0;
  static struct dvbridge_dvplus_picture pending;
  int eof = 0;
  while (done < wanted && !eof)
  {
    if (av_read_frame(fc, pkt) < 0)
    {
      avcodec_send_packet(dec, NULL);
      eof = 1;
    }
    else if (pkt->stream_index == vs)
      avcodec_send_packet(dec, pkt);
    av_packet_unref(pkt);
    while (done < wanted && avcodec_receive_frame(dec, f) == 0)
    {
      const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DOVI_METADATA);
      if (!sd || !Upload(planes, gp, f))
        continue;
      ++done;
      struct dvbridge_gl_frame b = {0};
      if (!dvbridge_gl_frame_import(&b, gl->gpu, DVBRIDGE_P010, f->width, f->height, gp, AVCHROMA_LOC_TOPLEFT))
        continue;
      const double pts = done;
      // (the measurement of the picture before, taken now; then this picture is presented)
      if (pendingValid)
        dvbridge_dvplus_measured(m, &pending);
      struct dvbridge_dvplus_rpu rpu;
      dvbridge_dvplus_rpu_of(sd->data, sd->size, &rpu);
      struct dvbridge_dvplus_stats sent;
      bool refresh = false;
      bool send = dvbridge_dvplus_present(m, pts, &rpu, &sent, &refresh);
      if (refresh)
      {
        ++scenes;
        memset(fullScene, 0, sizeof(fullScene));
        fullCount = 0;
        fullSum = 0;
      }
      const struct dvbridge_view view = {0, 0, (float)f->width, (float)f->height, 0, 0, W, H};
      const double t0 = NowMs();
      const int ok = dvbridge_render_hdr10(renderer, &b.frame, sd->data, sd->size, pts, NAN, view, W, H);
      dvbridge_gl_frame_release(&b);
      if (!ok)
        continue;
      ++drawn;
      const unsigned t = pl_opengl_unwrap(gl->gpu, dvbridge_render_hdr10_texture(renderer), NULL, NULL, NULL);
      int area[4];
      const float src[4] = {0, 0, (float)f->width, (float)f->height};
      const int dst[4] = {0, 0, W, H};
      dvbridge_dvplus_area(sd->data, sd->size, f->width, f->height, src, dst, area);
      if (!dvbridge_dvplus_gl_submit(sampler, t, W, H, area[0], area[1], area[2], area[3], pts))
        continue;
      // G5: what the first picture of a scene waits for: the rest of its GPU work (the rebuilding and the samples)
      const double t1 = NowMs();
      const int finished = dvbridge_dvplus_gl_wait(sampler, pts, 5000000000ULL);
      const double t2 = NowMs();
      CollectAll(sampler, &got);
      const double t3 = NowMs();
      if (finished)
      {
        ++timed;
        renderMs += t1 - t0;
        waitMs += t2 - t1;
        takeMs += t3 - t2;
        waitMax = t2 - t1 > waitMax ? t2 - t1 : waitMax;
      }
      ReadFloat(t, W, H, rgba);
      const size_t nw = Expected(rgba, W, area[0], area[1], area[2], area[3], want);
      if (got.n == 1 && got.count[0] == nw)
      {
        size_t o1, om;
        Compare(got.codes[0], want, nw, &o1, &om);
        allOff1 += o1;
        allOffMore += om;
        allSamples += nw;
        // every pixel of the place (all of them, as the bench's ideal)
        static uint16_t* all = NULL;
        if (!all)
          all = malloc((size_t)W * H * 2);
        size_t na = 0;
        for (int y = area[1]; y < area[1] + area[3]; ++y)
          for (int x = area[0]; x < area[0] + area[2]; ++x)
          {
            const float* p = rgba + ((size_t)y * W + x) * 4;
            double mx = 0;
            for (int c = 0; c < 3; ++c)
              mx = fmax(mx, p[c] < 0 ? 0 : p[c] > 1 ? 1 : p[c]);
            all[na++] = (uint16_t)floor(mx * 65535.0 + 0.5);
          }
        struct dvbridge_dvplus_stats sampled, full;
        StatsOfCodes(m, got.codes[0], nw, &sampled);
        StatsOfCodes(m, all, na, &full);
        const int d = MaxCodeDiff(&sampled, &full);
        worstSampled = d > worstSampled ? d : worstSampled;
        // the scene so far from every pixel, against what goes out with the next picture
        static struct dvbridge_dvplus_picture pp;
        memset(&pp, 0, sizeof(pp));
        dvbridge_dvplus_picture_add(m, &pp, all, NULL, na);
        for (int i = 0; i < DVBRIDGE_DVPLUS_BINS; ++i)
          fullScene[i] += pp.hist[i];
        fullCount += pp.count;
        fullSum += pp.sum;
        memset(&pending, 0, sizeof(pending));
        pending.pts = pts;
        dvbridge_dvplus_picture_add(m, &pending, got.codes[0], got.luma[0], nw);
        pendingValid = 1;
        if (refresh)
        {
          // the first picture of a scene waits for its own measurement: what goes out with it is then of its
          // scene, which is that picture alone (and the same when it is shown again)
          ++starts;
          struct dvbridge_dvplus_stats own, alone, again;
          const int took = dvbridge_dvplus_measured(m, &pending);
          pendingValid = 0;
          StatsOfCodes(m, got.codes[0], nw, &alone);
          const int sentOwn = took && dvbridge_dvplus_present_again(m, pts, &own) &&
                              MaxCodeDiff(&own, &alone) == 0 &&
                              dvbridge_dvplus_present(m, pts, &rpu, &again, NULL) && MaxCodeDiff(&again, &own) == 0;
          startsOwn += sentOwn;
          send = 1;
          sent = own;
        }
        if (send)
        {
          ++sentPictures;
          sentLast = sent;
        }
        printf("     picture %d: area %d %d %d %d, %zu samples; level 1 %u/%u/%u%s; sampled vs every pixel: %d codes at "
               "most; %s average %u p99.98 %u (0.1 cd/m2)\n",
               done, area[0], area[1], area[2], area[3], nw, rpu.min_pq, rpu.avg_pq, rpu.max_pq,
               refresh ? ", a new scene" : "", d, send ? "sent" : "nothing sent", send ? sent.average : 0,
               send ? sent.percentiles[8] : 0);
        (void)worstScene;
      }
      GotFree(&got);
      dvbridge_render_commit(renderer);
    }
  }
  printf("     G4: %d pictures, %d rebuilt, %d scenes; %zu samples, %zu one code off, %zu more\n", done, drawn, scenes,
         allSamples, allOff1, allOffMore);
  Check(drawn >= 2 && drawn == done, "G4 every picture rebuilt");
  Check(allSamples > 0 && allOffMore == 0 && allOff1 * 100 <= allSamples,
        "G4 the GPU's samples are the rebuilt picture's pixels (one code off at most, by rounding)");
  Check(worstSampled <= 1, "G4 the statistics of the samples within one InfoFrame code of those of every pixel");
  Check(sentPictures == drawn, "G4 HDR10+ from the first picture on (it starts a scene, and waits for its measurement)");
  Check(starts > 0 && startsOwn == starts,
        "G5 the first picture of each scene, waited for, goes out with its own measurement, also shown again");
  if (timed)
    printf("     G5: %d pictures: the rebuilding and the samples queued in %.1f ms on average; then the wait for the "
           "GPU %.1f ms (longest %.1f); the samples taken and the statistics %.2f ms\n",
           timed, renderMs / timed, waitMs / timed, waitMax, takeMs / timed);
  (void)sentLast;
  dvbridge_dvplus_gl_destroy(sampler);
  dvbridge_dvplus_destroy(m);
  printf("%s: %d of %d\n", g_failures ? "FAILED" : "ALL PASSED", g_passes, g_passes + g_failures);
  return g_failures != 0;
}
