// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: HDR10 measured as Dolby Vision from end to end on a software OpenGL ES (zink on
// lavapipe): the pictures of an HDR10 stream decoded by FFmpeg, with bars made in them (rows 0..279 and 1880..2159 at a
// faint level, 70 of 1023, under the black level of the measurement) and, from picture 120 on, a darker picture (a cut
// made); each picture measured on the GPU (dvbridge_measure.c), the decisions (dvbridge_measured.c), the decoder's
// metadata (dvbridge_measured_placeholder()) made into the presented ones (dvbridge_measured_apply()) and sent
// through the bridge, in the transport of certified source devices:
//   E1 the transport carries the level 1 that was decided for each picture and the level 5 of the bars measured
//      (top and bottom 280 pixels), its CRCs right;
//   E2 the scene refresh in the transport on the first picture and on the cut, and only there;
//   E3 the picture sent is the HDR10 picture within the active area, sample for sample (x 4), and the bars are sent
//      as black (luma 256, chroma 2048): the faint rows under the black level are written as black;
//   E4 the same pictures with the level 5 taken out of the metadata: the bars go out as they are (the faint level),
//      so E3 is the level 5 measured.
// usage: measured_bridge_test <hdr10 file> <every> <pictures>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_measure.h"
#include "dvbridge_measured.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"

#include "pack_sources.h"

#define W 3840
#define H 2160
#define BAR 280
#define FAINT 70
#define CUT 120

static int g_failures;
static long g_messages;
static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

#include "ccm001_oracle_check.h" // the CCM 001 reader

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
  {
    ++g_messages;
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
  }
}

static GLuint FramebufferOf(GLuint texture)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!complete)
  {
    glDeleteFramebuffers(1, &fb);
    return 0;
  }
  return fb;
}

struct layer
{
  GLuint tex[2];
  int w, h;
  struct dvbridge_gl_plane planes[3];
};

// the decoded picture changed as the test needs it: the bars, and from the cut on a darker picture (10-bit, in place)
static void Shape(AVFrame* f, int index)
{
  const int w = f->width, h = f->height;
  for (int r = 0; r < h; ++r)
  {
    uint16_t* y = (uint16_t*)(f->data[0] + (ptrdiff_t)r * f->linesize[0]);
    const int bar = r < BAR || r >= h - BAR;
    for (int x = 0; x < w; ++x)
      y[x] = bar ? FAINT : index >= CUT ? (uint16_t)(64 + (y[x] - 64) / 3) : y[x];
  }
  for (int r = 0; r < h / 2; ++r)
  {
    const int bar = 2 * r < BAR || 2 * r >= h - BAR;
    if (!bar)
      continue;
    uint16_t* u = (uint16_t*)(f->data[1] + (ptrdiff_t)r * f->linesize[1]);
    uint16_t* v = (uint16_t*)(f->data[2] + (ptrdiff_t)r * f->linesize[2]);
    for (int x = 0; x < w / 2; ++x)
      u[x] = v[x] = 512;
  }
}

// yuv420p10le to P010 (as hdr10plus_bridge_test.c)
static int Upload(struct layer* l, const AVFrame* f)
{
  if (f->format != AV_PIX_FMT_YUV420P10LE)
    return 0;
  const int w = f->width, h = f->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
  if (!y || !c)
    return 0;
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
  if (!l->tex[0])
    glGenTextures(2, l->tex);
  glBindTexture(GL_TEXTURE_2D, l->tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, y);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, l->tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, cw, ch, 0, GL_RG, GL_UNSIGNED_SHORT, c);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  free(y);
  free(c);
  l->w = w;
  l->h = h;
  l->planes[0] = (struct dvbridge_gl_plane){l->tex[0], w, h};
  l->planes[1] = (struct dvbridge_gl_plane){l->tex[1], cw, ch};
  l->planes[2] = (struct dvbridge_gl_plane){0, 0, 0};
  return glGetError() == GL_NO_ERROR;
}

static uint32_t Crc(const uint8_t* p, size_t n)
{
  uint32_t c = 0xffffffff;
  for (size_t j = 0; j < n; j++)
  {
    c ^= (uint32_t)p[j] << 24;
    for (int i = 0; i < 8; i++)
      c = (c << 1) ^ ((c >> 31) ? 0x04c11db7u : 0);
  }
  return c;
}

static char g_blocks[128]; // (the levels and lengths of the blocks of the last transport read)

// the level 1 and level 5 blocks and the scene refresh that a transport's bytes carry; 0: a CRC is wrong or there
// is no level 1 block (level 5: all 9999 when there is none)
static int Transport(const uint8_t* bytes, unsigned l1[3], unsigned l5[4], unsigned* refresh, unsigned* blocks)
{
  g_blocks[0] = 0;
  uint8_t packet[16][128];
  unsigned count = 0;
  for (unsigned k = 0; k < 16; ++k)
  {
    memset(packet[k], 0, 128);
    for (unsigned b = 0; b < 1024; ++b)
    {
      const size_t index = (size_t)k * 3072 + b;
      const int x = (int)(index % W), y = (int)(index / W);
      const uint8_t* o = bytes + ((size_t)(H - 1 - y) * W + x) * 4;
      const unsigned Y = o[1] << 4 | (o[2] & 15), C = o[0] << 4 | o[2] >> 4;
      packet[k][b / 8] |= (uint8_t)(((C & 1) ^ (unsigned)__builtin_parity(C >> 1) ^ (unsigned)__builtin_parity(Y))
                                    << (7 - b % 8));
    }
    if (Crc(packet[k], 128))
      return 0;
    count = k + 1;
    const unsigned type = packet[k][0] >> 6;
    if (type == 0 || type == 3)
      break;
  }
  uint8_t payload[2048];
  const int size = packet[0][3] << 8 | packet[0][4];
  int n = 0;
  for (unsigned i = 0; i < count && n < size; ++i)
  {
    const int start = i ? 3 : 5, room = i ? 121 : 119;
    for (int j = 0; j < room && n < size; ++j)
      payload[n++] = packet[i][start + j];
  }
  if (n != size || size < 71)
    return 0;
  *refresh = payload[1];
  *blocks = payload[70];
  for (int j = 0; j < 4; ++j)
    l5[j] = 9999;
  int pos = 71, found = 0;
  for (unsigned i = 0; i < payload[70] && pos + 5 <= size; ++i)
  {
    const unsigned length = (unsigned)payload[pos] << 24 | (unsigned)payload[pos + 1] << 16 |
                            (unsigned)payload[pos + 2] << 8 | payload[pos + 3];
    const unsigned level = payload[pos + 4];
    pos += 5;
    if (pos + (int)length > size)
      return 0;
    snprintf(g_blocks + strlen(g_blocks), sizeof(g_blocks) - strlen(g_blocks), " L%u:%u", level, length);
    if (level == 1 && length == 6)
    {
      for (int j = 0; j < 3; ++j)
        l1[j] = (unsigned)payload[pos + 2 * j] << 8 | payload[pos + 2 * j + 1];
      found = 1;
    }
    if (level == 5 && length == 8)
    {
      // four offsets of 16 bits, as dvbridge_metadata.h writes them: left, right, top, bottom
      for (int j = 0; j < 4; ++j)
        l5[j] = (unsigned)payload[pos + 2 * j] << 8 | payload[pos + 2 * j + 1];
    }
    pos += (int)length;
  }
  return found;
}

struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_packer* packer;
  GLuint fbo, color;
  uint8_t* bytes;
};

static double g_pts;

// one frame, the composed way: rendered with the metadata, packed in the transport of certified source devices,
// read back, committed
static int Frame(struct gl* g, struct layer* bl, const AVDOVIMetadata* meta, size_t bytes)
{
  struct dvbridge_gl_frame b = {0};
  if (!dvbridge_gl_frame_import(&b, g->gl->gpu, DVBRIDGE_P010, bl->w, bl->h, bl->planes, AVCHROMA_LOC_TOPLEFT))
    return 0;
  g_pts += 41708.0;
  const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
  int ok = dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, g_pts, NAN, full);
  ok = ok && dvbridge_render_source(g->renderer, NULL);
  ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_MEASURE);
  if (ok)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g->bytes);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ok = glGetError() == GL_NO_ERROR;
  }
  // the transport, read as ETSI GS CCM 001 says it is sent
  if (ok)
    OracleTransport(g->bytes, "frame", "composed");
  ok = ok && dvbridge_render_commit(g->renderer);
  dvbridge_gl_frame_release(&b);
  return ok;
}

// the transport against the decoded picture, rows from 16 on (the first rows carry the metadata):
// inside[0..2] luma of the active rows off by 0, 1, more (x 4); inside[3..5] the chroma of their sited rows;
// bars[0] luma of the bar rows at 256 (black), bars[1] at the faint level x 4, bars[2] other; bars[3] chroma at 2048
static void Compare(const uint8_t* bytes, const AVFrame* f, long inside[6], long bars[4])
{
  for (int y = 16; y < H; ++y)
  {
    const int bar = y < BAR || y >= H - BAR;
    const uint16_t* Ys = (const uint16_t*)(f->data[0] + (ptrdiff_t)y * f->linesize[0]);
    const uint16_t* Us = (const uint16_t*)(f->data[1] + (ptrdiff_t)(y / 2) * f->linesize[1]);
    const uint16_t* Vs = (const uint16_t*)(f->data[2] + (ptrdiff_t)(y / 2) * f->linesize[2]);
    for (int x = 0; x < W; ++x)
    {
      const uint8_t* o = bytes + ((size_t)(H - 1 - y) * W + x) * 4;
      const int Y = o[1] << 4 | (o[2] & 15), C = o[0] << 4 | o[2] >> 4;
      if (bar)
      {
        bars[Y == 256 ? 0 : Y == 4 * FAINT ? 1 : 2]++;
        if (!(y & 1))
          bars[3] += abs(C - 2048) <= 1; // (the metadata bit is in the lowest bit of the chroma of the first rows only)
        continue;
      }
      const int d = abs(Y - 4 * Ys[x]);
      inside[d == 0 ? 0 : d == 1 ? 1 : 2]++;
      if (!(y & 1))
      {
        const int c10 = (x & 1) ? Vs[x / 2] : Us[x / 2];
        const int dc = abs(C - 4 * c10);
        inside[3 + (dc == 0 ? 0 : dc == 1 ? 1 : 2)]++;
      }
    }
  }
}

struct collect_ctx
{
  struct dvbridge_measured* decisions;
  int collected;
};

static void Collected(const struct dvbridge_measured_raw* raw, uint64_t tag, void* context)
{
  (void)tag;
  struct collect_ctx* c = context;
  struct dvbridge_measured_picture picture;
  if (dvbridge_measured_reduce(c->decisions, raw, &picture))
  {
    dvbridge_measured_add(c->decisions, &picture);
    ++c->collected;
  }
}

int main(int argc, char** argv)
{
  if (argc < 4)
    return 2;
  const int every = atoi(argv[2]), wanted = atoi(argv[3]);
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
  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  g.fbo = FramebufferOf(g.color);
  g.bytes = malloc((size_t)W * H * 4);
  struct dvbridge_measure* measure = dvbridge_measure_create();
  struct collect_ctx cc = {dvbridge_measured_create(NULL), 0};
  if (!g.renderer || !g.packer || !g.fbo || !g.bytes || !measure || !cc.decisions)
    return 1;
  dvbridge_render_use_source_transport(g.renderer, true);

  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int vs = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  const AVCodec* codec = avcodec_find_decoder(fc->streams[vs]->codecpar->codec_id);
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, fc->streams[vs]->codecpar);
  if (avcodec_open2(dec, codec, NULL) < 0)
    return 1;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  struct layer bl = {0};
  AVMasteringDisplayMetadata mastering = {0};
  AVContentLightMetadata light = {0};
  int have_light = 0, decoded = 0, drawn = 0, sent_ok = 0, l1_right = 0, l5_right = 0, refresh_right = 0;
  int refreshes_taken = 0, cut_taken = -1, mut_drawn = 0;
  AVFrame* keep = NULL;
  struct dvbridge_measured_l1 keep_l1 = {0};
  int bars_from = -1, bars_lost = 0;
  long inside[6] = {0}, bars[4] = {0}, mbars[4] = {0}, minside[6] = {0};
  int eof = 0;
  double pts = 0;
  while (drawn < wanted && !eof)
  {
    if (av_read_frame(fc, pkt) < 0)
    {
      avcodec_send_packet(dec, NULL);
      eof = 1;
    }
    else if (pkt->stream_index == vs)
      avcodec_send_packet(dec, pkt);
    av_packet_unref(pkt);
    while (drawn < wanted && avcodec_receive_frame(dec, f) == 0)
    {
      const AVFrameSideData* md = av_frame_get_side_data(f, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
      if (md)
        mastering = *(const AVMasteringDisplayMetadata*)md->data;
      const AVFrameSideData* cl = av_frame_get_side_data(f, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
      if (cl)
      {
        light = *(const AVContentLightMetadata*)cl->data;
        have_light = 1;
      }
      if (av_frame_make_writable(f) < 0)
        return 1;
      const int index = decoded++;
      Shape(f, index);
      if (!Upload(&bl, f))
        return 1;
      // measured as Kodi measures it, the picture before its presentation, read back when the GPU is done
      pts += 41708.0;
      int window[2];
      dvbridge_measured_window(cc.decisions, bl.w / 2, window);
      if (!dvbridge_measure_submit(measure, bl.tex[0], bl.tex[1], bl.w, bl.h, window, pts, 0))
        return 1;
      glFinish();
      dvbridge_measure_collect(measure, Collected, &cc);
      struct dvbridge_measured_l1 l1;
      const int measured = dvbridge_measured_take(cc.decisions, pts, &l1);
      refreshes_taken += measured && l1.refresh && index > 0;
      if (measured && l1.refresh && index > 0 && cut_taken < 0)
        cut_taken = index;
      if (index % every != 0 && index != CUT)
      {
        av_frame_unref(f);
        continue;
      }
      size_t size = 0;
      AVDOVIMetadata* m = dvbridge_measured_placeholder(&mastering, have_light ? &light : NULL, &size);
      const int applied = m && measured && dvbridge_measured_apply(m, size, &l1);
      if (applied)
      {
        const int ok = Frame(&g, &bl, m, size);
        unsigned got1[3] = {0}, got5[4] = {0}, refresh = 9, blocks = 0;
        const int carried = ok && Transport(g.bytes, got1, got5, &refresh, &blocks);
        sent_ok += ok;
        l1_right += carried && got1[0] == l1.min_pq && got1[1] == l1.max_pq && got1[2] == l1.avg_pq;
        l5_right += carried && got5[0] == l1.area[0] && got5[1] == l1.area[1] && got5[2] == l1.area[2] &&
                    got5[3] == l1.area[3];
        // (the stream fades in from black: its first pictures say nothing of bars)
        const int bars_known = l1.area[0] == 0 && l1.area[1] == 0 && l1.area[2] == BAR && l1.area[3] == BAR;
        if (bars_known && bars_from < 0)
          bars_from = index;
        bars_lost += bars_from >= 0 && !bars_known;
        // the first frame drawn refreshes the scene in any case
        refresh_right += carried && refresh == (unsigned)(drawn == 0 || index == CUT);
        if (ok && bars_known)
          Compare(g.bytes, f, inside, bars);
        printf("picture %d: %s, level 1 %u/%u/%u, carried %u/%u/%u, level 5 %u %u %u %u (decided %u %u %u %u), %u "
               "blocks (%s), refresh %u%s\n",
               index, ok ? "drawn" : "NOT DRAWN", l1.min_pq, l1.avg_pq, l1.max_pq, got1[0], got1[2], got1[1], got5[0],
               got5[1], got5[2], got5[3], l1.area[0], l1.area[1], l1.area[2], l1.area[3], blocks, g_blocks, refresh,
               l1.refresh ? " (a cut measured)" : "");
        // (a picture for E4, after the others: a second render of it here would be a frame of the stream)
        if (!keep && bars_known && index != CUT)
        {
          keep = av_frame_clone(f);
          keep_l1 = l1;
        }
        ++drawn;
      }
      av_free(m);
      av_frame_unref(f);
    }
  }
  // E4: that picture again, as a stream of its own, with the level 5 taken out
  if (keep && Upload(&bl, keep))
  {
    dvbridge_renderer_reset(g.renderer);
    for (int k = 0; k < 2; ++k)
    {
      struct dvbridge_measured_l1 none = keep_l1;
      memset(none.area, 0, sizeof(none.area));
      size_t size = 0;
      AVDOVIMetadata* m2 = dvbridge_measured_placeholder(&mastering, have_light ? &light : NULL, &size);
      // (the level 5 block taken out wherever it is: apply() appends it to metadata without one, and where
      // dvbridge_profile81_metadata() writes one after level 1, apply() fills that one)
      int five = -1;
      if (m2 && dvbridge_measured_apply(m2, size, &none))
        for (int i = 0; i < m2->num_ext_blocks && five < 0; ++i)
          if (av_dovi_get_ext(m2, i)->level == 5)
            five = i;
      if (five >= 0)
      {
        for (int i = five; i + 1 < m2->num_ext_blocks; ++i)
          *av_dovi_get_ext(m2, i) = *av_dovi_get_ext(m2, i + 1);
        --m2->num_ext_blocks;
        if (Frame(&g, &bl, m2, size))
        {
          Compare(g.bytes, keep, minside, mbars);
          ++mut_drawn;
        }
      }
      av_free(m2);
    }
  }
  av_frame_free(&keep);
  printf("E1: %d pictures decoded and measured (%d collected), %d drawn, %d sent, level 1 carried in %d, level 5 as "
         "decided in %d; the bars known from picture %d on, lost after in %d\n", decoded, cc.collected, drawn, sent_ok,
         l1_right, l5_right, bars_from, bars_lost);
  printf("E2: cuts measured after the first picture: %d (the first at %d); the refresh as expected in %d of %d\n",
         refreshes_taken, cut_taken, refresh_right, drawn);
  printf("E3: inside: luma %ld equal, %ld off by 1, %ld more; chroma %ld equal, %ld off by 1, %ld more | bars: luma "
         "%ld black, %ld faint, %ld other; chroma %ld at 2048\n", inside[0], inside[1], inside[2], inside[3], inside[4],
         inside[5], bars[0], bars[1], bars[2], bars[3]);
  printf("E4: without level 5: inside luma %ld equal, %ld more | bars: luma %ld black, %ld faint, %ld other\n",
         minside[0], minside[1] + minside[2], mbars[0], mbars[1], mbars[2]);
  Check(drawn == wanted && sent_ok == drawn && l1_right == drawn && l5_right == drawn && bars_from >= 0 &&
            bars_from <= 24 && !bars_lost,
        "E1: every picture sent natively, with the level 1 and the level 5 decided; the bars measured (280 pixels) "
        "once the picture shows them, and kept");
  Check(cut_taken == CUT && refreshes_taken == 1 && refresh_right == drawn,
        "E2: the cut measured on its picture, the refresh in the transport there and on the first picture only");
  Check(inside[0] > 0 && inside[1] + inside[2] == 0 && inside[3] > 0 && inside[4] + inside[5] == 0 && bars[0] > 0 &&
            bars[1] + bars[2] == 0,
        "E3: the HDR10 picture inside the area sample for sample, the bars sent as black");
  Check(mut_drawn == 2 && minside[0] > 0 && minside[1] + minside[2] == 0 && mbars[1] > 0 && mbars[0] == 0,
        "E4: without the level 5 the bars go out at their faint level: the level 5 measured makes them black");
  OracleSummary();
  printf("libplacebo: %ld warnings and errors\n", g_messages);
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  fflush(stdout);
  _Exit(g_failures != 0);
}
