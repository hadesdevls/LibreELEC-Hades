// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): frames of profile 5 (IPT-PQ-c2),
// profile 7 FEL and profile 8.1 pictures with Kodi's GUI composited over them as Kodi composites it with native
// Dolby Vision (CompositeGui(): the shaders of the tree, its blend factors and projection), drawn the composed way
// of the source transport (the default) with the picture made where the GUI reaches (dvbridge_render_set_drawn())
// and packed with what is drawn over the bars measured, as Kodi draws a frame with GUI elements.
//
//   bytes: for every picture, GUI drawing and GUI size, a hash of the packed bytes of the frame and its level 5
//          block (compared outside between two trees of the bridge: the same bytes, or not);
//   time:  the time of a frame without GUI elements drawn directly, and of one with each GUI drawing: the render
//          with the picture, the GUI's composite and the packing, each waited for (glFinish), medians of 9.
//
// The profile 5 pictures are those of the p5sdr test (pictures of the profile 7 test stream's base layer taken to
// IPT-PQ-c2 with the profile 5 RPU of dovi_tool's test assets, p5gen.py); the profile 8.1 one is a picture of that
// base layer with the profile 8.1 RPU of those assets; the FEL ones are pictures 3 and 27 of the test stream with
// their enhancement layer.
//
// usage: p5gui_test <FEL file> <bytes|time> <Kodi's shaders> <dir of the p5sdr pictures> <dir of their metadata>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/cache.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "dvbridge_source.h"

#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI

#include "pack_sources.h"

// the GUI cache, where the bridge has it
__attribute__((weak)) void dvbridge_render_use_gui_cache(struct dvbridge_renderer* renderer, bool use);
__attribute__((weak)) void dvbridge_render_set_drawn_key(struct dvbridge_renderer* renderer, uint64_t key);
__attribute__((weak)) void dvbridge_render_gui_cache_check(struct dvbridge_renderer* renderer, bool check);
__attribute__((weak)) bool dvbridge_render_gui_cache_tiles(struct dvbridge_renderer* renderer, unsigned counts[4]);
__attribute__((weak)) bool dvbridge_render_gui_cache_read(struct dvbridge_renderer* renderer, unsigned counts[2]);
static uint64_t g_key = 1;  // the key of the composite given with the GUI texture
static GLuint g_lutTFUsed; // the PQ table that the composite takes (two GUI whites)
static GLuint g_lutTF2;

#define W 3840
#define H 2160

static int g_failures;
static long g_messages;

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

// the level of the conversion under the GUI that this run asked for (P5GUI_LEVEL; 0 by default)
static int g_guiLevel = 0;

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
  {
    ++g_messages;
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
  }
}

static double Now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static int Compare(const void* a, const void* b)
{
  const double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

static GLuint Program(const char* vs, const char* fs)
{
  GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), p = glCreateProgram();
  glShaderSource(v, 1, &vs, NULL);
  glCompileShader(v);
  glShaderSource(f, 1, &fs, NULL);
  glCompileShader(f);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[2048] = "";
    glGetProgramInfoLog(p, sizeof(log), NULL, log);
    fprintf(stderr, "program: %s\n", log);
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return ok ? p : 0;
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

// a rectangle of the raster (rows from the top: the picture's first row is its top) blended in a colour, as Kodi
// blends its overlays (RGB a colour + (1 - a) dst, alpha a + dst (1 - a))
static const char FS_VERTEX[] = "#version 300 es\nvoid main() {\n"
                                " vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                                " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n";
static const char COLOUR_FRAGMENT[] = "#version 300 es\nprecision highp float;\nuniform vec4 c;\nout vec4 color;\n"
                                      "void main() { color = c; }\n";
static GLuint g_colourProgram, g_vao;

static void Rect(GLuint fbo, int x0, int y0, int x1, int y1, float r, float gr, float b, float a)
{
  if (!g_colourProgram)
  {
    g_colourProgram = Program(FS_VERTEX, COLOUR_FRAGMENT);
    glGenVertexArrays(1, &g_vao);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, W, H);
  glUseProgram(g_colourProgram);
  glBindVertexArray(g_vao);
  glUniform4f(glGetUniformLocation(g_colourProgram, "c"), r, gr, b, a);
  glColorMask(1, 1, 1, 1);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, y0, x1 - x0, y1 - y0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
}

static uint32_t g_seed = 1;
static uint32_t Random(void)
{
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 8;
}

// ---- the pictures
struct layer
{
  GLuint tex[2];
  int w, h;
  struct dvbridge_gl_plane planes[3];
};

// yuv420p10le to P010
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

struct picture
{
  struct layer bl, el;
  int layered;
  AVDOVIMetadata* meta;
  size_t bytes;
  int location; // the chroma location of its planes (AVChromaLocation)
  char name[32];
};

static void* ReadAll(const char* path, size_t* size)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void* p = av_malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
  {
    av_free(p);
    p = NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return p;
}

// a picture from a raw yuv420p10le file of 3840x2160 (Y, U, V, rows from the top) and an AVDOVIMetadata file
static int LoadRaw(struct picture* p, const char* meta, const char* yuv, int location, const char* name)
{
  p->meta = ReadAll(meta, &p->bytes);
  if (!p->meta || p->bytes < sizeof(AVDOVIMetadata))
    return 0;
  AVFrame* f = av_frame_alloc();
  f->format = AV_PIX_FMT_YUV420P10LE;
  f->width = W;
  f->height = H;
  FILE* in = fopen(yuv, "rb");
  int ok = in && av_frame_get_buffer(f, 0) >= 0;
  for (int plane = 0; ok && plane < 3; ++plane)
  {
    const int w = plane ? W / 2 : W, h = plane ? H / 2 : H;
    for (int r = 0; ok && r < h; ++r)
      ok = fread(f->data[plane] + (ptrdiff_t)r * f->linesize[plane], 2, (size_t)w, in) == (size_t)w;
  }
  if (in)
    fclose(in);
  ok = ok && Upload(&p->bl, f);
  av_frame_free(&f);
  p->layered = 0;
  p->location = location;
  snprintf(p->name, sizeof(p->name), "%s", name);
  return ok;
}

// pictures 3, 27, 51...: those of the other tests of the bridge, with their enhancement layer
static int Decode(const char* file, struct picture* pictures, unsigned wanted)
{
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, file, NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 0;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVStream* stream = fc->streams[index];
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(base, stream->codecpar);
  base->pkt_timebase = stream->time_base;
  base->thread_count = 16;
  base->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 0;
  struct dvbridge_fel* fel = dvbridge_fel_create(stream->codecpar, stream->time_base);
  fel->decoder->get_format = avcodec_default_get_format;
  fel->decoder->thread_count = 16;
  fel->decoder->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(fel->decoder, codec, NULL) < 0)
    return 0;
  fel->opened = true;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* pending[64];
  unsigned numPending = 0, number = 0, got = 0;
  int end = 0, ok = 1;
  while (!end && ok && got < wanted)
  {
    const int read = av_read_frame(fc, packet);
    if (read >= 0 && packet->stream_index != index)
    {
      av_packet_unref(packet);
      continue;
    }
    end = read < 0;
    if (!end)
    {
      avcodec_send_packet(base, packet);
      if (!dvbridge_fel_submit(fel, packet))
        ok = 0;
      av_packet_unref(packet);
    }
    else
    {
      avcodec_send_packet(base, NULL);
      dvbridge_fel_drain(fel);
    }
    while (ok && avcodec_receive_frame(base, frame) >= 0)
    {
      pending[numPending++] = av_frame_clone(frame);
      av_frame_unref(frame);
      while (numPending && got < wanted)
      {
        AVFrame* b = pending[0];
        AVFrame* lay = NULL;
        const int result = dvbridge_fel_take(fel, b->pts, &lay);
        if (result == 0 && !end)
          break;
        const unsigned n = number++;
        if (n >= 3 && (n - 3) % 24 == 0)
        {
          const AVFrame* from = b;
          if (lay && av_frame_get_side_data(lay, AV_FRAME_DATA_DOVI_METADATA))
            from = lay;
          const AVFrameSideData* sd = av_frame_get_side_data(from, AV_FRAME_DATA_DOVI_METADATA);
          struct picture* p = &pictures[got];
          if (sd && lay && Upload(&p->bl, b) && Upload(&p->el, lay))
          {
            p->layered = 1;
            p->meta = av_malloc(sd->size);
            memcpy(p->meta, sd->data, sd->size);
            p->bytes = sd->size;
            p->location = AVCHROMA_LOC_TOPLEFT;
            snprintf(p->name, sizeof(p->name), "fel-%u", n);
            ++got;
          }
        }
        av_frame_free(&lay);
        av_frame_free(&b);
        memmove(pending, pending + 1, --numPending * sizeof(*pending));
      }
    }
  }
  return ok && got == wanted;
}

// ---- Kodi's composite of its GUI (CWinSystemGbmGLESContext::CompositeGui() with native Dolby Vision)
static GLuint g_kodiProgram, g_lutDegamma, g_lutTF;
static GLint g_kPos = -1, g_kTex = -1, g_kProj = -1, g_kOotf = -1, g_kSamp = -1, g_kLutD = -1, g_kLutT = -1;

static char* ReadText(const char* dir, const char* name)
{
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  size_t n = 0;
  char* raw = ReadAll(path, &n);
  if (!raw)
    return NULL;
  char* text = malloc(n + 1);
  memcpy(text, raw, n);
  text[n] = 0;
  av_free(raw);
  return text;
}

static float SrgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

static float ForwardPQ(float L)
{
  const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
  const float Lm1 = powf(L, m1);
  return powf((c1 + c2 * Lm1) / (1.0f + c3 * Lm1), m2);
}

static GLuint Lut(const float* data, int n)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, n, 1, 0, GL_RED, GL_FLOAT, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

static int SetUpKodi(const char* shaders)
{
  char* vs = ReadText(shaders, "gles_gui_composite.vert");
  char* fs = ReadText(shaders, "gles_gui_composite.frag");
  if (!vs || !fs)
    return 0;
  g_kodiProgram = Program(vs, fs);
  free(vs);
  free(fs);
  if (!g_kodiProgram)
    return 0;
  g_kPos = glGetAttribLocation(g_kodiProgram, "a_pos");
  g_kTex = glGetAttribLocation(g_kodiProgram, "a_tex");
  g_kProj = glGetUniformLocation(g_kodiProgram, "u_proj");
  g_kOotf = glGetUniformLocation(g_kodiProgram, "u_ootfGamma");
  g_kSamp = glGetUniformLocation(g_kodiProgram, "u_samp");
  g_kLutD = glGetUniformLocation(g_kodiProgram, "u_lutDegamma");
  g_kLutT = glGetUniformLocation(g_kodiProgram, "u_lutTF");
  float degamma[256], tf[1024];
  for (int i = 0; i < 256; ++i)
    degamma[i] = SrgbToLinear((float)i / 255.0f);
  for (int i = 0; i < 1024; ++i)
    tf[i] = ForwardPQ((float)i / 1023.0f * (203.0f / 10000.0f));
  g_lutDegamma = Lut(degamma, 256);
  g_lutTF = Lut(tf, 1024);
  // (and the table of a GUI white of 300 %, as videoscreen.hadesguiwhite makes it)
  for (int i = 0; i < 1024; ++i)
    tf[i] = ForwardPQ((float)i / 1023.0f * (609.0f / 10000.0f));
  g_lutTF2 = Lut(tf, 1024);
  g_lutTFUsed = g_lutTF;
  return g_kPos >= 0 && g_kTex >= 0 && g_kProj >= 0 && g_kSamp >= 0 && g_lutDegamma && g_lutTF &&
         glGetError() == GL_NO_ERROR;
}

static void KodiComposite(GLuint fb, GLuint gui, int w, int h)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glViewport(0, 0, W, H);
  glDisable(GL_SCISSOR_TEST);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  for (int unit = 0; unit < 3; ++unit)
    glBindSampler(unit, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_lutDegamma);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, g_lutTFUsed);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gui);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  const float fw = (float)w, fh = (float)h;
  GLfloat proj[16] = {2.0f / fw, 0, 0, 0, 0, -2.0f / fh, 0, 0, 0, 0, -1, 0, -1.0f, 1.0f, 0, 1};
  proj[5] = -proj[5];
  proj[13] = -proj[13];
  glUseProgram(g_kodiProgram);
  glUniformMatrix4fv(g_kProj, 1, GL_FALSE, proj);
  glUniform1f(g_kOotf, 0.0f);
  glUniform1i(g_kSamp, 0);
  glUniform1i(g_kLutD, 1);
  glUniform1i(g_kLutT, 2);
  GLfloat vert[4][2] = {{0, 0}, {fw, 0}, {fw, fh}, {0, fh}};
  GLfloat tex[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  GLubyte idx[4] = {0, 1, 3, 2};
  glVertexAttribPointer((GLuint)g_kPos, 2, GL_FLOAT, GL_FALSE, 0, vert);
  glVertexAttribPointer((GLuint)g_kTex, 2, GL_FLOAT, GL_FALSE, 0, tex);
  glEnableVertexAttribArray((GLuint)g_kPos);
  glEnableVertexAttribArray((GLuint)g_kTex);
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);
  glDisableVertexAttribArray((GLuint)g_kPos);
  glDisableVertexAttribArray((GLuint)g_kTex);
  glDisable(GL_BLEND);
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// a GUI texture as Kodi's GUI FBO has it: RGBA8, linear, clamped to its edges, first row at the bottom
static GLuint GuiTexture(int w, int h, const uint8_t* rgba)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// a texel of the GUI, x and y from its top left as the GUI is laid out (the texture's rows the other way up)
static void Texel(uint8_t* rgba, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  if (x < 0 || y < 0 || x >= w || y >= h)
    return;
  uint8_t* t = rgba + ((size_t)(h - 1 - y) * w + x) * 4;
  t[0] = r;
  t[1] = g;
  t[2] = b;
  t[3] = a;
}

static void GuiRect(uint8_t* rgba, int w, int h, double fx0, double fy0, double fx1, double fy1, uint8_t r,
                    uint8_t g, uint8_t b, uint8_t a)
{
  for (int y = (int)(fy0 * h); y < (int)(fy1 * h); ++y)
    for (int x = (int)(fx0 * w); x < (int)(fx1 * w); ++x)
      Texel(rgba, w, h, x, y, r, g, b, a);
}

#define GUI_KINDS 9
static const char* const GUI_NAMES[GUI_KINDS] = {
    "an empty GUI",
    "the player information (Estuary, Hades' overview page)",
    "a window over a dimmed screen",
    "an OSD (a bar at the top and one at the bottom)",
    "two lines of subtitles",
    "the seek bar alone",
    "an opacity of 1 in 255 over everything",
    "every opacity and colour over everything",
    "200 random texels"};

// lines of text (white strokes) in a column of the 1920x1080 layout of Estuary, from top, of a height
static void TextLines(uint8_t* rgba, int w, int h, int left, int top, int width, int lines, int step)
{
  for (int l = 0; l < lines; ++l)
  {
    const double y0 = (top + l * step + 8) / 1080.0, y1 = (top + l * step + 26) / 1080.0;
    // words: strokes 6 of 1920 wide, 4 apart, in runs of a word
    for (int x = left; x < left + width - 8; x += 10)
      if ((x / 70 + l) % 5 != 0)
        GuiRect(rgba, w, h, x / 1920.0, y0, (x + 6) / 1920.0, y1, 255, 255, 255, 255);
  }
}

static void GuiPattern(uint8_t* rgba, int w, int h, int k)
{
  memset(rgba, 0, (size_t)w * h * 4);
  switch (k)
  {
    case 1:
      // DialogPlayerProcessInfo.xml of the tree: the overview page's background, dialogs/dialog-bg-nobo.png (one
      // texel: 16, 16, 16 at 204) over the whole width from row 120 to 955 of 1080; its heading and two columns of
      // rows of text (left 40 and 665, 590 wide, from row 236)
      GuiRect(rgba, w, h, 0, 120 / 1080.0, 1, 955 / 1080.0, 16, 16, 16, 204);
      TextLines(rgba, w, h, 40, 128, 720, 2, 50);
      TextLines(rgba, w, h, 40, 236, 590, 22, 32);
      TextLines(rgba, w, h, 665, 236, 590, 22, 32);
      break;
    case 2:
      GuiRect(rgba, w, h, 0, 0, 1, 1, 0, 0, 0, 128);
      GuiRect(rgba, w, h, 0.2, 0.15, 0.8, 0.85, 30, 30, 34, 235);
      GuiRect(rgba, w, h, 0.25, 0.2, 0.6, 0.23, 240, 240, 240, 255);
      break;
    case 3:
      GuiRect(rgba, w, h, 0, 0, 1, 0.08, 10, 10, 10, 160);
      GuiRect(rgba, w, h, 0, 0.8, 1, 1, 10, 10, 10, 200);
      GuiRect(rgba, w, h, 0.05, 0.9, 0.7, 0.91, 230, 230, 230, 255);
      break;
    case 4:
      for (int i = 0; i < 30; ++i)
      {
        GuiRect(rgba, w, h, 0.3 + i * 0.013, 0.90, 0.3 + i * 0.013 + 0.008, 0.92, 255, 255, 255, 255);
        GuiRect(rgba, w, h, 0.32 + i * 0.012, 0.935, 0.32 + i * 0.012 + 0.008, 0.955, 255, 255, 255, 255);
      }
      break;
    case 5:
      GuiRect(rgba, w, h, 0.05, 0.93, 0.95, 0.95, 40, 40, 40, 180);
      GuiRect(rgba, w, h, 0.05, 0.93, 0.5, 0.95, 220, 220, 220, 255);
      break;
    case 6:
      GuiRect(rgba, w, h, 0, 0, 1, 1, 255, 255, 255, 1);
      break;
    case 7:
      // every opacity (by column) and many colours (by row and column) over the whole raster
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          Texel(rgba, w, h, x, y, (uint8_t)(x * 7 + y), (uint8_t)(y * 3), (uint8_t)(x ^ y), (uint8_t)(1 + x % 255));
      break;
    case 9:
      // Home: an opaque background over the whole raster (a gradient, as a fanart), a column of text and a
      // translucent panel
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          Texel(rgba, w, h, x, y, (uint8_t)(20 + x * 60 / w), (uint8_t)(30 + y * 90 / h),
                (uint8_t)(60 + (x + y) * 40 / (w + h)), 255);
      TextLines(rgba, w, h, 80, 200, 600, 12, 40);
      GuiRect(rgba, w, h, 0.55, 0.3, 0.95, 0.7, 10, 10, 10, 200);
      break;
    case 10:
      // an opaque dialog with text over a dimmed screen
      GuiRect(rgba, w, h, 0, 0, 1, 1, 0, 0, 0, 128);
      GuiRect(rgba, w, h, 0.2, 0.15, 0.8, 0.85, 30, 30, 34, 255);
      TextLines(rgba, w, h, 450, 200, 1000, 10, 50);
      break;
    case 8:
      for (int i = 0; i < 200; ++i)
        Texel(rgba, w, h, (int)(Random() % (unsigned)w), (int)(Random() % (unsigned)h), 200, 100, 50,
              (uint8_t)(1 + Random() % 255));
      break;
    default:
      break;
  }
}

// ---- the frames
struct gl
{
  pl_opengl gl;
  pl_log log;
  pl_cache cache;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_packer* packer;
  GLuint fbo, color;
  pl_tex scanout;
  uint8_t* bytes;
};

static int Import(struct gl* g, const struct picture* p, struct dvbridge_gl_frame* b, struct dvbridge_gl_frame* e)
{
  if (!dvbridge_gl_frame_import(b, g->gl->gpu, DVBRIDGE_P010, p->bl.w, p->bl.h, p->bl.planes,
                                (enum AVChromaLocation)p->location))
    return 0;
  if (p->layered)
  {
    if (!dvbridge_gl_frame_import(e, g->gl->gpu, DVBRIDGE_P010, p->el.w, p->el.h, p->el.planes,
                                  (enum AVChromaLocation)p->location))
      return 0;
    b->frame.enhancement_layer = &e->frame;
  }
  return 1;
}

static int ReadInto(GLuint fbo, uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

static uint64_t Hash(const uint8_t* p, size_t n)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < n; ++i)
  {
    h ^= p[i];
    h *= UINT64_C(1099511628211);
  }
  return h;
}

static double g_pts; // microseconds, a frame period apart, as Kodi passes them
static char g_dumpPicture[1024]; // where the picture drawn over (RGBA32F) of the next frame is written, if set
static int g_how;                // how a frame is drawn: 0 as Kodi draws it; 1, 2, 3: see GuiFrame()
static float* g_picture;         // where the picture drawn over is read into, if set (SimplifyPart())

// a frame with GUI elements as Kodi draws it; part: the render with the picture, the composite, the packing (ms)
static int GuiFrame(struct gl* g, const struct picture* p, GLuint gui, int w, int h, int cover, uint8_t* bytes,
                    double part[3], int* source)
{
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!Import(g, p, &b, &e))
    return 0;
  g_pts += 41708.0;
  const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
  static GLuint fbOf, fb;
  // (g_how 1: what is drawn over is not known, the whole picture is made, as before the tiles)
  if (dvbridge_render_set_drawn_key)
    dvbridge_render_set_drawn_key(g->renderer, g_key);
  dvbridge_render_set_drawn(g->renderer, g_how == 1 ? 0 : gui, w, h);
  glFinish();
  double t0 = Now();
  int ok = dvbridge_render_rgb(g->renderer, &b.frame, p->meta, p->bytes, g_pts, p->layered ? g_pts : NAN, full);
  glFinish();
  part[0] = (Now() - t0) * 1000.0;
  pl_tex tex = ok ? dvbridge_render_texture(g->renderer) : NULL;
  const GLuint picture = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  ok = ok && picture;
  if (ok && picture != fbOf)
  {
    glDeleteFramebuffers(1, &fb);
    fb = FramebufferOf(picture);
    fbOf = picture;
  }
  ok = ok && fb;
  if (source)
    *source = ok && dvbridge_render_source(g->renderer, NULL);
  // (g_how 2: the picture made whole before the GUI is composited, and PQ subtitles drawn into it over a
  // rectangle that reaches beyond what the GUI covers, as Kodi draws them)
  if (ok && g_how == 2)
    ok = dvbridge_render_complete(g->renderer);
  if (ok && g_how == 2)
    Rect(fb, 700, 1500, 3100, 2100, 0.7f, 0.7f, 0.2f, 0.8f);
  t0 = Now();
  if (ok)
  {
    KodiComposite(fb, gui, w, h);
    dvbridge_render_texture_drawn(g->renderer);
  }
  glFinish();
  part[1] = (Now() - t0) * 1000.0;
  if (ok && g_dumpPicture[0])
  {
    // (the picture with the GUI composited over it, rows from the raster's top, as the packer reads it)
    float* floats = malloc((size_t)W * H * 16);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, floats);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    FILE* o = fopen(g_dumpPicture, "wb");
    if (o)
    {
      fwrite(floats, 16, (size_t)W * H, o);
      fclose(o);
    }
    free(floats);
    g_dumpPicture[0] = 0;
  }
  if (ok && g_picture)
  {
    // (the same, kept for SimplifyPart(): which pixels were drawn over)
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, g_picture);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  t0 = Now();
  // (g_how 3: packed the right way up, as into a texture)
  ok = ok && dvbridge_render_pack(g->renderer, g->packer, g->fbo, W, H, g_how != 3, cover);
  glFinish();
  part[2] = (Now() - t0) * 1000.0;
  if (ok && bytes)
    ok = ReadInto(g->fbo, bytes);
  ok = ok && dvbridge_render_commit(g->renderer);
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
  return ok;
}

// a name in a list of names separated by commas (no list: every name)
static int Listed(const char* list, const char* name)
{
  if (!list)
    return 1;
  const size_t n = strlen(name);
  for (const char* s = list; *s;)
  {
    const char* e = strchr(s, ',');
    const size_t l = e ? (size_t)(e - s) : strlen(s);
    if (l == n && !strncmp(s, name, n))
      return 1;
    s += l + (e ? 1 : 0);
  }
  return 0;
}

// (the frames of whose stream the renderer noted that the pass of the pairs was missed)
static long g_pairsMissed;
static void TakeNotes(struct gl* g)
{
  struct dvbridge_render_notes notes;
  memset(&notes, 0, sizeof(notes));
  if (dvbridge_render_take_notes(g->renderer, &notes))
  {
#ifdef DVBRIDGE_RENDER_PAIRS_NOTE
    g_pairsMissed += notes.pairs_missed;
#endif
  }
}

static void BytesPart(struct gl* g, struct picture* pictures, unsigned count)
{
  static const int SIZES[][2] = {{3840, 2160}, {1920, 1080}};
  long frames = 0, failed = 0, notSource = 0;
  for (unsigned i = 0; i < count; ++i)
    for (int s = 0; s < 2; ++s)
    {
      const int w = SIZES[s][0], h = SIZES[s][1];
      uint8_t* rgba = malloc((size_t)w * h * 4);
      for (int k = 0; k < GUI_KINDS; ++k)
      {
        g_seed = 77u + (unsigned)(s * 31 + i * 7 + k);
        GuiPattern(rgba, w, h, k);
        const GLuint gui = GuiTexture(w, h, rgba);
        // (each frame the first of its stream: its metadata does not depend on the frames before)
        dvbridge_renderer_reset(g->renderer);
        double part[3];
        int source = 0;
        const char* dumpDir = getenv("P5GUI_DUMP");
        const char* dumpOnly = getenv("P5GUI_DUMP_ONLY");
        char name[128];
        snprintf(name, sizeof(name), "%s-gui%d-%d", pictures[i].name, k, w);
        if (dumpDir && getenv("P5GUI_DUMP_PICTURE") && Listed(dumpOnly, name))
          snprintf(g_dumpPicture, sizeof(g_dumpPicture), "%s/%s.f32", dumpDir, name);
        const int ok = GuiFrame(g, &pictures[i], gui, w, h, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
        TakeNotes(g);
        ++frames;
        failed += !ok;
        notSource += ok && !source;
        printf("frame: %s gui %d %dx%d: %s %016llx\n", pictures[i].name, k, w, h, ok ? "packed" : "NOT DRAWN",
               ok ? (unsigned long long)Hash(g->bytes, (size_t)W * H * 4) : 0ull);
        // (P5GUI_DUMP=<dir>: the packed bytes of the frames whose names contain P5GUI_DUMP_ONLY, for a
        // comparison of the codes outside)
        const char* dump = getenv("P5GUI_DUMP");
        const char* only = getenv("P5GUI_DUMP_ONLY");
        char stem[128];
        snprintf(stem, sizeof(stem), "%s-gui%d-%d", pictures[i].name, k, w);
        if (ok && dump && Listed(only, stem))
        {
          char path[1024];
          snprintf(path, sizeof(path), "%s/%s.rgba", dump, stem);
          FILE* o = fopen(path, "wb");
          if (o)
          {
            fwrite(g->bytes, 1, (size_t)W * H * 4, o);
            fclose(o);
          }
        }
        glDeleteTextures(1, &gui);
      }
      free(rgba);
    }
  // the other ways a frame is drawn, with drawings over most of the raster: what is drawn over not known (the
  // whole picture made, no tiles), the picture made whole before the GUI (PQ subtitles, a capture), and packed
  // the right way up
  static const int OTHER[] = {1, 2, 7};
  uint8_t* rgba = malloc((size_t)W * H * 4);
  for (unsigned i = 0; i < count; ++i)
    for (int how = 1; how <= 3; ++how)
      for (int oi = 0; oi < 3; ++oi)
      {
        g_seed = 177u + (unsigned)(i * 7 + how * 3 + oi);
        GuiPattern(rgba, W, H, OTHER[oi]);
        const GLuint gui = GuiTexture(W, H, rgba);
        dvbridge_renderer_reset(g->renderer);
        double part[3];
        int source = 0;
        g_how = how;
        const int ok = GuiFrame(g, &pictures[i], gui, W, H, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
        TakeNotes(g);
        g_how = 0;
        ++frames;
        failed += !ok;
        notSource += ok && !source;
        printf("frame: %s gui %d %dx%d how %d: %s %016llx\n", pictures[i].name, OTHER[oi], W, H, how,
               ok ? "packed" : "NOT DRAWN", ok ? (unsigned long long)Hash(g->bytes, (size_t)W * H * 4) : 0ull);
        glDeleteTextures(1, &gui);
      }
  free(rgba);
  printf("bytes: %ld frames, %ld not drawn, %ld not in the source transport\n", frames, failed, notSource);
  printf("notes: %ld frames of whose stream the renderer noted the pass of the pairs missed\n", g_pairsMissed);
  Check(frames > 0 && !failed && !notSource, "every frame is drawn and packed in the source transport");
}

// The GUI cache: sequences of frames under one GUI texture, as Kodi draws a GUI that stays with the film
// going on under it: for every drawing (Home, an opaque dialog over a dimmed screen, the player information, every
// opacity), GUI size and kind of picture (profile 5, FEL, 8.1), twelve frames over the pictures of that kind: the
// cache takes the tiles from the third; at the fourth a part of the GUI changes; at the seventh the composite's
// table (another GUI white, another key); the tenth has PQ subtitles drawn into the picture (made whole), the
// eleventh is packed the right way up, the twelfth does not say what is drawn over it. The bytes of every frame
// (compared outside with a bridge without the cache: the same), and what the cache did (cachestat).
static void CachePart(struct gl* g, struct picture* pictures, unsigned count)
{
  (void)count;
  static const int KINDS[] = {9, 10, 1, 7};
  static const int SIZES[][2] = {{3840, 2160}, {1920, 1080}};
  static const int GROUPS[][3] = {{0, 1, 2}, {3, 4, -1}, {5, -1, -1}};
  const char* only = getenv("P5GUI_CACHE_ONLY"); // (a kind alone, for mutations)
  long frames = 0, failed = 0, converted = 0;
  for (int k = 0; k < 4; ++k)
    for (int s = 0; s < 2; ++s)
    {
      if (only && atoi(only) != KINDS[k])
        continue;
      const int w = SIZES[s][0], h = SIZES[s][1];
      uint8_t* rgba = malloc((size_t)w * h * 4);
      for (int gi = 0; gi < 3; ++gi)
      {
        int n = 0;
        while (n < 3 && GROUPS[gi][n] >= 0)
          ++n;
        g_seed = 300u + (unsigned)(k * 7 + s * 3 + gi);
        GuiPattern(rgba, w, h, KINDS[k]);
        const GLuint gui = GuiTexture(w, h, rgba);
        dvbridge_renderer_reset(g->renderer);
        g_key = 1;
        g_lutTFUsed = g_lutTF;
        for (int f = 0; f < 12; ++f)
        {
          const struct picture* p = &pictures[GROUPS[gi][f % n]];
          if (f == 3)
          {
            GuiRect(rgba, w, h, 0.1, 0.3, 0.3, 0.5, 200, 60, 40, 255);
            glBindTexture(GL_TEXTURE_2D, gui);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glBindTexture(GL_TEXTURE_2D, 0);
          }
          if (f == 6)
          {
            g_key = 2;
            g_lutTFUsed = g_lutTF2;
          }
          g_how = f == 9 ? 2 : f == 10 ? 3 : f == 11 ? 1 : 0;
          double part[3];
          int source = 0;
          const int ok = GuiFrame(g, p, gui, w, h, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
          g_how = 0;
          TakeNotes(g);
          ++frames;
          // a frame with GUI elements over a picture without an enhancement layer goes out converted at a
          // converting GUI level (P5GUI_LEVEL 1 or above), by design: dvbridge_render_gui_converts() says that the
          // picture is one of those, whatever the level; every other frame, and every frame at level 0, in the
          // source transport
          const int converts = dvbridge_render_gui_converts(g->renderer) && g_guiLevel >= 1;
          converted += converts;
          failed += !ok || (converts ? source || p->layered : !source);
          printf("frame: cache gui %d %dx%d group %d f%d %s: %s %016llx\n", KINDS[k], w, h, gi, f, p->name,
                 ok ? "packed" : "NOT DRAWN", ok ? (unsigned long long)Hash(g->bytes, (size_t)W * H * 4) : 0ull);
          unsigned c[2], t4[4];
          if (dvbridge_render_gui_cache_read && dvbridge_render_gui_cache_read(g->renderer, c))
            printf("cachestat: gui %d %dx%d group %d f%d: copied %u packed %u\n", KINDS[k], w, h, gi, f, c[0], c[1]);
          if (dvbridge_render_gui_cache_tiles && dvbridge_render_gui_cache_tiles(g->renderer, t4) && t4[3])
          {
            printf("cachecheck: gui %d %dx%d group %d f%d: copied %u differ %u\n", KINDS[k], w, h, gi, f, t4[0],
                   t4[2]);
            if (t4[2])
              ++g_failures;
          }
        }
        g_key = 1;
        g_lutTFUsed = g_lutTF;
        glDeleteTextures(1, &gui);
      }
      free(rgba);
    }
  printf("cache: %ld frames, %ld converted by the GUI level, %ld not drawn or not in the transport they must be "
         "in\n", frames, converted, failed);
  Check(frames > 0 && !failed,
        "every frame of the sequences is drawn and packed in the source transport, or converted where the GUI level "
        "converts a picture without an enhancement layer");
}

// one GUI (Home, 3840x2160) and one composite over pictures of every kind in turn: the metadata's
// representation changes under a GUI that does not
static void CacheMixPart(struct gl* g, struct picture* pictures)
{
  static const int ORDER[] = {0, 0, 0, 3, 3, 4, 5, 5, 5, 1, 1, 1, 4, 4, 2, 2, 3, 3};
  uint8_t* rgba = malloc((size_t)W * H * 4);
  GuiPattern(rgba, W, H, 9);
  const GLuint gui = GuiTexture(W, H, rgba);
  dvbridge_renderer_reset(g->renderer);
  g_key = 1;
  g_lutTFUsed = g_lutTF;
  long frames = 0, failed = 0;
  for (unsigned f = 0; f < sizeof(ORDER) / sizeof(*ORDER); ++f)
  {
    const struct picture* p = &pictures[ORDER[f]];
    double part[3];
    int source = 0;
    const int ok = GuiFrame(g, p, gui, W, H, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
    TakeNotes(g);
    ++frames;
    failed += !ok || !source;
    printf("frame: mix f%u %s: %s %016llx\n", f, p->name, ok ? "packed" : "NOT DRAWN",
           ok ? (unsigned long long)Hash(g->bytes, (size_t)W * H * 4) : 0ull);
    unsigned c[2];
    if (dvbridge_render_gui_cache_read && dvbridge_render_gui_cache_read(g->renderer, c))
      printf("cachestat: mix f%u: copied %u packed %u\n", f, c[0], c[1]);
  }
  glDeleteTextures(1, &gui);
  free(rgba);
  printf("cache: %ld frames, %ld not drawn or not in the source transport\n", frames, failed);
  Check(frames > 0 && !failed, "every frame of the mixed sequence is drawn and packed in the source transport");
}

static void TimePart(struct gl* g, struct picture* pictures, unsigned count)
{
  uint8_t* rgba = malloc((size_t)W * H * 4);
  static const int timed[] = {0, 1, 2, 3, 4, 5};
  int all = 1;
  for (unsigned i = 0; i < count; ++i)
  {
    dvbridge_renderer_reset(g->renderer);
    {
      struct dvbridge_gl_frame b = {0}, e = {0};
      double t[9];
      int n = 0;
      for (; n < 9 && Import(g, &pictures[i], &b, &e); ++n)
      {
        g_pts += 41708.0;
        const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
        glFinish();
        const double t0 = Now();
        all &= dvbridge_render_packed(g->renderer, &b.frame, pictures[i].meta, pictures[i].bytes, g_pts,
                                      pictures[i].layered ? g_pts : NAN, full, g->scanout, true);
        glFinish();
        t[n] = (Now() - t0) * 1000.0;
        dvbridge_render_commit(g->renderer);
        dvbridge_gl_frame_release(&e);
        dvbridge_gl_frame_release(&b);
      }
      all &= n == 9;
      qsort(t, 9, sizeof(double), Compare);
      printf("time: %s: no GUI, drawn directly: %.1f ms (median of 9; least %.1f)\n", pictures[i].name, t[4], t[0]);
    }
    for (unsigned ti = 0; ti < sizeof(timed) / sizeof(*timed); ++ti)
    {
      GuiPattern(rgba, W, H, timed[ti]);
      const GLuint gui = GuiTexture(W, H, rgba);
      double t[2][4][9];
      for (int n = 0; n < 9; ++n)
        for (int c = 0; c < 2; ++c)
        {
          double part[3];
          glFinish();
          const double t0 = Now();
          all &= GuiFrame(g, &pictures[i], gui, W, H, c ? DVBRIDGE_COVER_BARS : DVBRIDGE_COVER_MEASURE, NULL, part,
                          NULL);
          t[c][3][n] = (Now() - t0) * 1000.0;
          for (int k = 0; k < 3; ++k)
            t[c][k][n] = part[k];
        }
      for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 4; ++k)
          qsort(t[c][k], 9, sizeof(double), Compare);
      printf("time: %s: %s: %.1f ms (the render with the picture %.1f, the GUI's composite %.1f, the packing %.1f); "
             "every edge with bars %.1f ms (packing %.1f) (medians of 9)\n",
             pictures[i].name, GUI_NAMES[timed[ti]], t[0][3][4], t[0][0][4], t[0][1][4], t[0][2][4], t[1][3][4],
             t[1][2][4]);
      glDeleteTextures(1, &gui);
    }
  }
  free(rgba);
  Check(all, "every frame timed is drawn and packed");
}

// the fidelity step-down levels (dvbridge_render_set_step(), DVStepDown.h) with the player information over a
// picture: the bytes at each level against those at level 0, and the time of a frame
static void StepsPart(struct gl* g, struct picture* pictures, unsigned count)
{
  uint8_t* rgba = malloc((size_t)W * H * 4);
  GuiPattern(rgba, W, H, 1);
  const GLuint gui = GuiTexture(W, H, rgba);
  uint8_t* level0 = malloc((size_t)W * H * 4);
  int all = 1;
  for (unsigned i = 0; i < count; ++i)
    for (int level = 0; level <= 3; ++level)
    {
      dvbridge_renderer_reset(g->renderer);
      dvbridge_render_set_step(g->renderer, level);
      double part[3], t[9];
      int ok = GuiFrame(g, &pictures[i], gui, W, H, DVBRIDGE_COVER_MEASURE, g->bytes, part, NULL);
      long differ = 0;
      if (!level)
        memcpy(level0, g->bytes, (size_t)W * H * 4);
      else
        for (size_t b = 0; b < (size_t)W * H * 4; ++b)
          differ += g->bytes[b] != level0[b];
      for (int n = 0; n < 9; ++n)
      {
        const double t0 = Now();
        ok &= GuiFrame(g, &pictures[i], gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, NULL);
        t[n] = (Now() - t0) * 1000.0;
      }
      qsort(t, 9, sizeof(double), Compare);
      all &= ok;
      printf("step: %s, the player information, level %d: %ld bytes not those of level 0; %.1f ms (median of 9)\n",
             pictures[i].name, level, differ, t[4]);
    }
  dvbridge_render_set_step(g->renderer, 0);
  glDeleteTextures(1, &gui);
  free(rgba);
  free(level0);
  Check(all, "every frame of the step-down levels is drawn and packed");
}

#ifdef DVBRIDGE_RENDER_GUI_LEVELS
// The levels of the conversion under the GUI (dvbridge_render_gui_level()): every frame of BytesPart() drawn at
// level 0, at level 1 and at level 2, compared code by code: at level 1, the same codes; at level 2, where they
// differ, by one code at most, under a pixel drawn over (its luma; its chroma, under it or under the even pixel of
// its pair), and in profile 5 frames only; and the frames without GUI elements, drawn directly, the same with it
// asked for.
static long g_simpFrames, g_simpP5, g_simpDone, g_simpOtherDone, g_simpOtherChanged, g_simpCodes, g_simpLarge,
    g_simpOutside, g_simpFailed, g_simpPixels, g_l0Other, g_l1Done, g_l1Changed;

// the pixels whose codes differ between two packed frames (the lowest chroma bit of the pixels of the metadata
// packets carries their bits, which the serializer makes of what it sent before as well: the codes are compared
// without it), those apart by more than one, and those outside the pixels drawn over (pic)
static long CodesApart(const uint8_t* exact, const uint8_t* bytes, int flip, const float* pic, long* large,
                       long* outside)
{
  long changed = 0;
  for (int r = 0; r < H; ++r)
    for (int x = 0; x < W; ++x)
    {
      const size_t at = ((size_t)r * W + x) * 4;
      if (!memcmp(exact + at, bytes + at, 4))
        continue;
      const uint8_t *a = exact + at, *b = bytes + at;
      const int ya = a[1] << 4 | (a[2] & 15), yb = b[1] << 4 | (b[2] & 15);
      int ca = a[0] << 4 | a[2] >> 4, cb = b[0] << 4 | b[2] >> 4;
      const int y = flip ? H - 1 - r : r;
      if ((size_t)y * W + x < (size_t)16 * 3072)
      {
        ca >>= 1;
        cb >>= 1;
      }
      if (ya == yb && ca == cb)
        continue;
      ++changed;
      if (abs(ya - yb) > 1 || abs(ca - cb) > 1)
        ++*large;
      const int ex = x & ~1;
      const int over = pic[((size_t)y * W + x) * 4 + 3] != 2.0f;
      const int evenOver = pic[((size_t)y * W + ex) * 4 + 3] != 2.0f;
      if ((ya != yb && !over) || (ca != cb && !over && !evenOver))
        ++*outside;
    }
  return changed;
}

static void SimplifyFrame(struct gl* g, struct picture* p, GLuint gui, int w, int h, int how, uint8_t* exact,
                          float* pic)
{
  const int p5 = !strncmp(p->name, "p5-", 3);
  double part[3];
  int source = 0;
  g_how = how;
  // (all three the first frame of their stream, at the same time: the same metadata)
  const double pts = g_pts;
  dvbridge_render_gui_level(g->renderer, 0);
  dvbridge_renderer_reset(g->renderer);
  g_picture = pic;
  int ok = GuiFrame(g, p, gui, w, h, DVBRIDGE_COVER_MEASURE, exact, part, &source);
  g_picture = NULL;
  const int drawn0 = dvbridge_render_gui_drawn(g->renderer);
  g_pts = pts;
  dvbridge_render_gui_level(g->renderer, 1);
  dvbridge_renderer_reset(g->renderer);
  ok = ok && GuiFrame(g, p, gui, w, h, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
  const int drawn1 = dvbridge_render_gui_drawn(g->renderer);
  long large1 = 0, outside1 = 0;
  const long changed1 = ok ? CodesApart(exact, g->bytes, how != 3, pic, &large1, &outside1) : 0;
  g_pts = pts;
  dvbridge_render_gui_level(g->renderer, 2);
  dvbridge_renderer_reset(g->renderer);
  ok = ok && GuiFrame(g, p, gui, w, h, DVBRIDGE_COVER_MEASURE, g->bytes, part, &source);
  const int drawn2 = dvbridge_render_gui_drawn(g->renderer);
  dvbridge_render_gui_level(g->renderer, 0);
  g_how = 0;
  ++g_simpFrames;
  g_simpFailed += !ok;
  if (!ok)
    return;
  g_simpP5 += p5;
  g_l0Other += drawn0 != 0;
  g_l1Done += p5 && drawn1 == 1;
  g_l1Changed += changed1 != 0;
  g_simpDone += p5 && drawn2 == 2;
  g_simpOtherDone += !p5 && (drawn1 != 0 || drawn2 != 0);
  long large = 0, outside = 0;
  const long changed = CodesApart(exact, g->bytes, how != 3, pic, &large, &outside);
  g_simpCodes += changed;
  g_simpLarge += large;
  g_simpOutside += outside;
  if (!p5)
    g_simpOtherChanged += changed != 0;
  for (size_t n = 0; n < (size_t)W * H; ++n)
    g_simpPixels += p5 && pic[n * 4 + 3] != 2.0f;
}

// (quick, for the mutations: the GUI of the size of the raster only; every kind of GUI and every way of
// drawing over a profile 5 picture, the player information alone over the others)
static void SimplifyPart(struct gl* g, struct picture* pictures, unsigned count, int quick)
{
  static const int SIZES[][2] = {{3840, 2160}, {1920, 1080}};
  static const int OTHER[] = {1, 2, 7};
  uint8_t* exact = malloc((size_t)W * H * 4);
  float* pic = malloc((size_t)W * H * 16);
  for (unsigned i = 0; i < count; ++i)
  {
    const int p5 = !strncmp(pictures[i].name, "p5-", 3);
    for (int s = 0; s < (quick ? 1 : 2); ++s)
    {
      const int w = SIZES[s][0], h = SIZES[s][1];
      uint8_t* rgba = malloc((size_t)w * h * 4);
      for (int k = 0; k < GUI_KINDS; ++k)
      {
        if (quick && !p5 && k != 1)
          continue;
        g_seed = 77u + (unsigned)(s * 31 + i * 7 + k);
        GuiPattern(rgba, w, h, k);
        const GLuint gui = GuiTexture(w, h, rgba);
        SimplifyFrame(g, &pictures[i], gui, w, h, 0, exact, pic);
        glDeleteTextures(1, &gui);
      }
      free(rgba);
    }
    uint8_t* rgba = malloc((size_t)W * H * 4);
    for (int how = 1; how <= 3; ++how)
      for (int oi = 0; oi < (quick ? (p5 ? 1 : 0) : 3); ++oi)
      {
        g_seed = 177u + (unsigned)(i * 7 + how * 3 + oi);
        GuiPattern(rgba, W, H, OTHER[oi]);
        const GLuint gui = GuiTexture(W, H, rgba);
        SimplifyFrame(g, &pictures[i], gui, W, H, how, exact, pic);
        glDeleteTextures(1, &gui);
      }
    free(rgba);
  }
  // the frames without GUI elements, drawn directly: the same with the simplified rounding asked for
  long directSame = 0, directFrames = 0;
  for (unsigned i = 0; i < count; ++i)
    for (int simplify = 0; simplify < 2; ++simplify)
    {
      // (both at the same time: the same metadata)
      if (simplify)
        g_pts -= 41708.0;
      struct dvbridge_gl_frame b = {0}, e = {0};
      if (!Import(g, &pictures[i], &b, &e))
        continue;
      dvbridge_render_gui_level(g->renderer, simplify ? 2 : 0);
      dvbridge_renderer_reset(g->renderer);
      g_pts += 41708.0;
      const struct dvbridge_geometry full = {W, H, 0, 0, W, H};
      const int ok = dvbridge_render_packed(g->renderer, &b.frame, pictures[i].meta, pictures[i].bytes, g_pts,
                                            pictures[i].layered ? g_pts : NAN, full, g->scanout, true) &&
                     ReadInto(g->fbo, simplify ? g->bytes : exact);
      dvbridge_render_commit(g->renderer);
      dvbridge_gl_frame_release(&e);
      dvbridge_gl_frame_release(&b);
      if (simplify)
      {
        // (the codes, without the bits of the metadata packets, as above)
        long differ = 0;
        for (size_t n = 0; ok && n < (size_t)W * H; ++n)
        {
          const uint8_t *a = exact + n * 4, *b = g->bytes + n * 4;
          const size_t raster = ((size_t)H - 1 - n / W) * W + n % W;
          const int mask = raster < (size_t)16 * 3072 ? 0xfe : 0xff;
          differ += a[1] != b[1] || (a[2] & 15) != (b[2] & 15) || a[0] != b[0] ||
                    ((a[2] >> 4) & mask) != ((b[2] >> 4) & mask);
        }
        ++directFrames;
        directSame += ok && !differ;
      }
    }
  dvbridge_render_gui_level(g->renderer, 0);
  free(exact);
  free(pic);
  printf("levels: %ld frames, %ld not drawn; at level 0, %ld drawn at another; profile 5: %ld; level 1: %ld drawn at "
         "it, %ld frames with a code changed; level 2: %ld simplified; other profiles drawn at another level %ld, "
         "changed %ld; %ld codes changed, %ld by more than one, %ld outside the pixels drawn over (of %ld drawn over "
         "in profile 5 frames); frames without GUI elements drawn directly: %ld of %ld the same\n",
         g_simpFrames, g_simpFailed, g_l0Other, g_simpP5, g_l1Done, g_l1Changed, g_simpDone, g_simpOtherDone,
         g_simpOtherChanged, g_simpCodes, g_simpLarge, g_simpOutside, g_simpPixels, directSame, directFrames);
  Check(!g_simpFailed, "every frame is drawn at the three levels");
  Check(!g_l0Other, "level 0 asked for: every frame drawn at it");
  Check(g_l1Done == g_simpP5 && !g_l1Changed, "level 1: every profile 5 frame with GUI elements drawn at it, the same codes");
  Check(g_simpDone == g_simpP5 && g_simpP5 > 0, "level 2: every profile 5 frame with GUI elements drawn the simplified way");
  Check(!g_simpOtherDone && !g_simpOtherChanged, "no frame of profile 7 FEL or 8.1 is drawn another way");
  Check(!g_simpLarge, "no code changes by more than one");
  Check(!g_simpOutside, "codes change only under the pixels drawn over");
  Check(directFrames == (long)count && directSame == directFrames, "the frames drawn directly are the same");
}
#endif

#ifdef DVBRIDGE_RENDER_GUI_LEVELS
static void MemoryPart(struct gl* g, struct picture* pictures)
{
  uint8_t* rgba = malloc((size_t)W * H * 4);
  g_seed = 5;
  GuiPattern(rgba, W, H, 1);
  const GLuint gui = GuiTexture(W, H, rgba);
  GuiPattern(rgba, W, H, 0);
  const GLuint empty = GuiTexture(W, H, rgba);
  free(rgba);
  double part[3];
  int source = 0;
  unsigned names[2], made[2];
  const struct picture* p5 = &pictures[0];
  const struct picture* fel = &pictures[3];
  dvbridge_render_gui_release(g->renderer);
  dvbridge_render_gui_targets(g->renderer, names);
  Check(!names[0] && !names[1], "memory: no target of levels 1 and 2 before they are asked for");
  // level 0: frames with GUI elements, and with PQ subtitles over a picture made whole and nothing else
  dvbridge_render_gui_level(g->renderer, 0);
  int ok = 1, drawn = 0;
  for (int how = 0; how <= 2; ++how)
  {
    g_how = how;
    ok &= GuiFrame(g, p5, how == 2 ? empty : gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source);
    drawn |= dvbridge_render_gui_drawn(g->renderer);
  }
  g_how = 0;
  dvbridge_render_gui_targets(g->renderer, names);
  Check(ok && !drawn && !names[0] && !names[1],
        "memory: at level 0 nothing of levels 1 and 2 is drawn or made (PQ subtitles over a picture made whole too)");
  dvbridge_render_gui_level(g->renderer, 1);
  ok = GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source);
  dvbridge_render_gui_targets(g->renderer, names);
  Check(ok && dvbridge_render_gui_drawn(g->renderer) == 1 && names[0] && !names[1],
        "memory: level 1 makes its target, and only it");
  dvbridge_render_gui_level(g->renderer, 2);
  ok = GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source);
  dvbridge_render_gui_targets(g->renderer, made);
  Check(ok && dvbridge_render_gui_drawn(g->renderer) == 2 && made[0] && made[1], "memory: level 2 makes its target");
  // the level drawn is that of the frame packed: a FEL frame, a frame in the BT.2020 transport, a pack that fails
  ok = GuiFrame(g, fel, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source);
  Check(ok && dvbridge_render_gui_drawn(g->renderer) == 0, "drawn: a FEL frame after a frame at level 2, level 0");
  ok = GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source) &&
       dvbridge_render_gui_drawn(g->renderer) == 2;
  dvbridge_render_use_source_transport(g->renderer, 0);
  ok = ok && GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source) && !source;
  Check(ok && dvbridge_render_gui_drawn(g->renderer) == 0,
        "drawn: a frame in the BT.2020 transport after a frame at level 2, level 0");
  dvbridge_render_use_source_transport(g->renderer, 1);
  ok = GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source) &&
       dvbridge_render_gui_drawn(g->renderer) == 2;
  // (the frame packed again into a raster that the source transport does not pack: refused)
  const int failed = !dvbridge_render_pack(g->renderer, g->packer, g->fbo, W / 2, H / 2, true, DVBRIDGE_COVER_NONE);
  Check(ok && failed && dvbridge_render_gui_drawn(g->renderer) == 0, "drawn: a pack that fails, level 0");
  // released: both textures deleted; made again when asked
  dvbridge_render_gui_targets(g->renderer, made);
  glFinish();
  dvbridge_render_gui_release(g->renderer);
  dvbridge_render_gui_targets(g->renderer, names);
  Check(made[0] && made[1] && !names[0] && !names[1] && !glIsTexture(made[0]) && !glIsTexture(made[1]),
        "memory: released, the textures of both targets are deleted (132.7 and 8.3 MB)");
  ok = GuiFrame(g, p5, gui, W, H, DVBRIDGE_COVER_MEASURE, NULL, part, &source);
  Check(ok && dvbridge_render_gui_drawn(g->renderer) == 2, "memory: after the release, level 2 again");
  dvbridge_render_gui_level(g->renderer, 0);
  dvbridge_render_gui_release(g->renderer);
  glDeleteTextures(1, &gui);
  glDeleteTextures(1, &empty);
}
#endif

int main(int argc, char** argv)
{
  if (argc < 6)
    return 2;
  const char* mode = argv[2];
  const char* shaders = argv[3];
  const char* pics = argv[4];
  const char* metas = argv[5];
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
  EGLint pbAttr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
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
  g.cache = pl_cache_create(pl_cache_params(.log = g.log, .max_total_size = 64 << 20));
  if (g.cache)
    pl_gpu_set_cache(g.gl->gpu, g.cache);
  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  g.fbo = FramebufferOf(g.color);
  g.scanout = pl_opengl_wrap(g.gl->gpu, pl_opengl_wrap_params(.texture = g.color, .target = GL_TEXTURE_2D,
                                                             .iformat = GL_RGBA8, .width = W, .height = H));
  g.bytes = malloc((size_t)W * H * 4);
  Check(g.renderer && g.packer && g.fbo && g.scanout && g.bytes && SetUpKodi(shaders),
        "the renderer, the packer, the scanout and Kodi's composite are made");
  if (g_failures)
  {
    printf("FAILED\n");
    return 1;
  }
  dvbridge_render_use_source_transport(g.renderer, 1);
  // (the GUI cache on, as Kodi turns it on, unless P5GUI_GCACHE=0; P5GUI_GCHECK=1 its check)
  if (dvbridge_render_use_gui_cache)
    dvbridge_render_use_gui_cache(g.renderer, !getenv("P5GUI_GCACHE") || atoi(getenv("P5GUI_GCACHE")));
  if (dvbridge_render_gui_cache_check)
    dvbridge_render_gui_cache_check(g.renderer, getenv("P5GUI_GCHECK") && atoi(getenv("P5GUI_GCHECK")));
#ifdef DVBRIDGE_RENDER_GUI_LEVELS
  // (P5GUI_LEVEL: the bytes and the times at that level of the conversion under the GUI)
  if (getenv("P5GUI_LEVEL"))
  {
    g_guiLevel = atoi(getenv("P5GUI_LEVEL"));
    dvbridge_render_gui_level(g.renderer, g_guiLevel);
  }
#endif

  // the pictures: profile 5 (three), profile 7 FEL (two), profile 8.1 (one)
  enum { COUNT = 6 };
  struct picture* pictures = calloc(COUNT, sizeof(*pictures));
  static const char* const P5[3] = {"96", "51", "chart"};
  int loaded = 1;
  char a[1024], b[1024];
  for (int i = 0; i < 3; ++i)
  {
    snprintf(a, sizeof(a), "%s/p5-%s.meta", metas, P5[i]);
    snprintf(b, sizeof(b), "%s/p5-%s-bl.yuv", pics, P5[i]);
    char name[32];
    snprintf(name, sizeof(name), "p5-%s", P5[i]);
    loaded &= LoadRaw(&pictures[i], a, b, AVCHROMA_LOC_LEFT, name);
  }
  loaded &= Decode(argv[1], &pictures[3], 2);
  snprintf(a, sizeof(a), "%s/profile8.meta", metas);
  snprintf(b, sizeof(b), "%s/hdr10bl-96.yuv", pics);
  loaded &= LoadRaw(&pictures[5], a, b, AVCHROMA_LOC_LEFT, "p8.1-96");
  Check(loaded, "the profile 5, FEL and 8.1 pictures are loaded with their metadata");
  if (loaded)
  {
    if (!strcmp(mode, "bytes"))
      BytesPart(&g, pictures, COUNT);
    else if (!strcmp(mode, "cache"))
      CachePart(&g, pictures, COUNT);
    else if (!strcmp(mode, "cachemix"))
      CacheMixPart(&g, pictures);
    else if (!strcmp(mode, "time"))
    {
      // one of each: a profile 5 picture, a FEL one and the 8.1 one
      struct picture timed[3] = {pictures[0], pictures[3], pictures[5]};
      TimePart(&g, timed, 3);
    }
#ifdef DVBRIDGE_RENDER_GUI_LEVELS
    else if (!strcmp(mode, "simplify"))
      SimplifyPart(&g, pictures, COUNT, 0);
    else if (!strcmp(mode, "simplify-quick"))
    {
      // a profile 5 picture, a FEL one and the 8.1 one
      struct picture quick[3] = {pictures[0], pictures[3], pictures[5]};
      SimplifyPart(&g, quick, 3, 1);
    }
    else if (!strcmp(mode, "memory"))
      MemoryPart(&g, pictures);
#endif
    else if (!strcmp(mode, "steps"))
    {
      struct picture stepped[2] = {pictures[0], pictures[3]};
      StepsPart(&g, stepped, 2);
    }
  }
  printf("libplacebo: %ld warnings and errors\n", g_messages);
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  fflush(stdout);
  _Exit(g_failures != 0);
}
