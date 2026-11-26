// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): the 3D LUT of a display in the video shaders of
// Kodi's own renderer (system/shaders/GLES/2.0/gles_lut3d.frag and gles_lut3d_body.frag, VideoShaders/Lut3dGLES.h,
// tools/dvbridge/dvbridge_lut_atlas.h), the way SDR videos that Kodi draws itself take it, on the software OpenGL ES
// (zink on lavapipe). A picture of an SDR clip (H.264, 8 bits, decoded here with the FFmpeg of the build) is drawn by
// the shaders of the tree, put together as Kodi's renderer puts them (CShader::LoadSource() and InsertSource(),
// YUV2RGBProgressiveShader, YUV2RGBBobShader, YUV2RGBFilterShader and ConvolutionFilterShader: the LUT's files only
// where a LUT is given, its call before the dither), with the LUT files of lut3d_check.py read by
// dvbridge_lut_parse(), laid out by dvbridge_lut_atlas_layout() and put on the GPU a few slices of blue at a time
// straight from the LUT (dvbridge_lut_atlas_slice()), as CLinuxRendererGLES::Lut3dTake() and CLut3dGLES::Begin() and
// Put() do:
//
//   - the ways Kodi draws a picture: the YUV shader of GLES 2.0 in one pass (a software picture of three planes,
//     with the chroma filter of Hades, Lanczos 3; a VAAPI picture of two, with Kodi's own chroma, to an output of
//     limited range), the filter shader of GLES 3.1, the field of an interlaced picture, HDR tone mapped to SDR by
//     Kodi's shaders, and two passes with Kodi's scalers (6x6 into RGBA16F, 4x4 into RGBA8), where the LUT is in the
//     second;
//   - each with no LUT, with an identity LUT (17 points; 33 and 65 and a 1D identity on the first way) and with a
//     known LUT (in each format on the first way: .cube full range and video levels, .3dl of Nuke's shaper and
//     uniform, madVR's, and the 1D LUTs: ArgyllCMS's .cal, a .cube of LUT_1D_SIZE alone, steep and of 5000 points (two
//     rows of its texture), Resolve's shaper before a 3D LUT: gles_lut3d_curve.frag put in first); written for
//     lutsdr_check.py, which applies the LUT to the picture drawn without it in float64;
//   - an identity LUT gives every byte of RGBA8 and every code of RGB10_A2 of the picture without it;
//   - a shader made with the LUT but told not to apply it (a capture) gives the picture without it, to the bit;
//   - the shaders put together without a LUT have nothing of it; with one, its call comes after the clamp of the
//     output and before the dither;
//   - the table: every grid point of the 2D texture as dvbridge_lut_point() gives it; one larger than the GPU takes is
//     refused with the reason; the layout without a copy (dvbridge_lut_atlas_layout()) is that of the copy, and each
//     slice put straight from the LUT holds the texels of the copy;
//   - the time of a picture with the LUT and without it (informative).
//
// Usage: lutsdr_test <Kodi tree> <clip> <LUT folder> <out folder>
// Prints "ok:"/"FAIL:" lines, "pair"/"time" lines, and "PASSED|FAILED: <n> failed".
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_lut.h"
#include "dvbridge_lut_atlas.h"

// every STRIDE-th pixel of every STRIDE-th row is written for lutsdr_check.py
#define STRIDE 2
#define LUT_UNIT 5
#define CURVE_UNIT 6

static int g_failures;

static void Check(int ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void Check(int ok, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  printf("%s ", ok ? "ok:  " : "FAIL:");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  if (!ok)
    ++g_failures;
}

static double NowMs(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static char* Slurp(const char* path, size_t* size)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* b = malloc((size_t)n + 1);
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n)
  {
    free(b);
    b = NULL;
  }
  fclose(f);
  if (b)
    b[n] = 0;
  if (size)
    *size = (size_t)n;
  return b;
}

// ------------------------------------------------------------------------------------------------- the clip
static int g_w, g_h;
static unsigned char *g_y, *g_u, *g_v;

// picture number want (or the last one) of the clip, 8-bit 4:2:0
static int Decode(const char* path, int want)
{
  AVFormatContext* fmt = NULL;
  if (avformat_open_input(&fmt, path, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
    return 0;
  const int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (s < 0)
    return 0;
  const AVCodec* codec = avcodec_find_decoder(fmt->streams[s]->codecpar->codec_id);
  AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : NULL;
  if (!ctx || avcodec_parameters_to_context(ctx, fmt->streams[s]->codecpar) < 0 || avcodec_open2(ctx, codec, NULL) < 0)
    return 0;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int got = 0, n = 0, eof = 0;
  while (n <= want && !eof)
  {
    if (av_read_frame(fmt, pkt) < 0)
    {
      eof = 1;
      avcodec_send_packet(ctx, NULL);
    }
    else if (pkt->stream_index != s)
    {
      av_packet_unref(pkt);
      continue;
    }
    else
    {
      avcodec_send_packet(ctx, pkt);
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, frame) == 0)
    {
      if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P)
        continue;
      g_w = frame->width & ~1;
      g_h = frame->height & ~1;
      free(g_y);
      free(g_u);
      free(g_v);
      g_y = malloc((size_t)g_w * g_h);
      g_u = malloc((size_t)g_w * g_h / 4);
      g_v = malloc((size_t)g_w * g_h / 4);
      for (int y = 0; y < g_h; ++y)
        memcpy(g_y + (size_t)y * g_w, frame->data[0] + (size_t)y * frame->linesize[0], (size_t)g_w);
      for (int y = 0; y < g_h / 2; ++y)
      {
        memcpy(g_u + (size_t)y * g_w / 2, frame->data[1] + (size_t)y * frame->linesize[1], (size_t)g_w / 2);
        memcpy(g_v + (size_t)y * g_w / 2, frame->data[2] + (size_t)y * frame->linesize[2], (size_t)g_w / 2);
      }
      got = 1;
      if (++n > want)
        break;
    }
  }
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  printf("clip: picture %d of %s, %dx%d, 8-bit 4:2:0\n", n - 1, path, g_w, g_h);
  return got;
}

// ------------------------------------------------------------------------------------------------- the shaders
enum kind
{
  BASIC,
  BOB,
  FILTER,
  CONV4,
  CONV6
};
static const char* KIND_NAMES[] = {"basic", "bob", "filter (3.1)", "convolution 4x4", "convolution 6x6"};

static void InsertAt(char** s, size_t pos, const char* t)
{
  const size_t n = strlen(*s), tl = strlen(t);
  *s = realloc(*s, n + tl + 1);
  memmove(*s + pos + tl, *s + pos, n - pos + 1);
  memcpy(*s + pos, t, tl);
}

// a file as CRenderSystemGLES::GetShaderPath() finds it on OpenGL ES 3.1: GLES/3.1 where it is there, else GLES/2.0
static char* Resolve(const char* tree, const char* name)
{
  char path[4096];
  snprintf(path, sizeof path, "%s/system/shaders/GLES/3.1/%s", tree, name);
  char* s = Slurp(path, NULL);
  if (s)
    return s;
  snprintf(path, sizeof path, "%s/system/shaders/GLES/2.0/%s", tree, name);
  return Slurp(path, NULL);
}

// CShader::LoadSource(): the defines after the #version line
static char* Load(const char* tree, const char* name, const char* defines)
{
  char* s = Resolve(tree, name);
  if (!s)
    return NULL;
  char* v = strstr(s, "#version");
  size_t pos = 0;
  if (v && strchr(v, '\n'))
    pos = (size_t)(strchr(v, '\n') - s) + 1;
  InsertAt(&s, pos, defines);
  return s;
}

// CShader::InsertSource(): the file before the first place of loc
static int Insert(char** s, const char* tree, const char* name, const char* loc)
{
  char* t = Resolve(tree, name);
  char* at = t ? strstr(*s, loc) : NULL;
  if (!at)
  {
    free(t);
    return 0;
  }
  InsertAt(s, (size_t)(at - *s), t);
  free(t);
  return 1;
}

static GLuint Compile(GLenum type, const char* src, const char* what)
{
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    Check(0, "%s does not compile: %.800s", what, log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

struct way
{
  const char* name;
  enum kind kind;
  int nv12;    // the two planes of a VAAPI picture (R8 + RG8: XBMC_NV12_RRG), else three of a software one (YV12)
  int chroma;  // Hades' chroma filter, Lanczos 3 (KODI_CHROMA), else Kodi's own
  int tonemap; // HDR tone mapped to SDR by Kodi's shaders (XBMC_COL_CONVERSION, Hable)
  int limited; // an output of limited range
  int field;   // the bob shader: 1 the top field, 0 the bottom one
  GLenum fbo;  // two passes: the format of the first pass's picture
};

// The program of a way, put together as the shader classes of Kodi put it (YUV2RGBShaderGLES.cpp,
// VideoFilterShaderGLES.cpp); lut: with the LUT's files (2: and those of its 1D LUT, first: CLut3dGLES::Insert()).
// *source: the pixel shader as compiled.
static GLuint Program(const char* tree, const struct way* w, enum kind kind, int lut, char** source)
{
  char defines[512] = "";
  const int yuv = kind == BASIC || kind == BOB || kind == FILTER;
  if (yuv)
    snprintf(defines, sizeof defines, "%s%s%s%s", w->nv12 ? "#define XBMC_NV12_RRG\n" : "#define XBMC_YV12\n",
             w->tonemap ? "#define XBMC_COL_CONVERSION\n#define KODI_TONE_MAPPING_HABLE\n" : "",
             w->chroma && kind != BOB ? "#define KODI_CHROMA\n#define KODI_CHROMA_LANCZOS3\n" : "", "");
  else
    snprintf(defines, sizeof defines, "#define HAS_FLOAT_TEXTURE\n");
  const char* vsName = kind == FILTER ? "gles310_yuv2rgb.vert" : yuv ? "gles_yuv2rgb.vert" : "gles_videofilter.vert";
  const char* fsName = kind == BASIC    ? "gles_yuv2rgb_basic.frag"
                       : kind == BOB    ? "gles_yuv2rgb_bob.frag"
                       : kind == FILTER ? "gles310_yuv2rgb_filter.frag"
                       : kind == CONV4  ? "gles_convolution-4x4.frag"
                                        : "gles_convolution-6x6.frag";
  // (the vertex shader of the YUV shaders takes the base's defines; that of GLES 3.1 and of the scalers none)
  char* vs = Load(tree, vsName, kind == BASIC || kind == BOB ? defines : "");
  char* fs = Load(tree, fsName, defines);
  const char* out = kind == FILTER ? "fragColor = rgb" : "gl_FragColor";
  int ok = vs && fs;
  if (ok && yuv && w->chroma && kind != BOB)
    ok = Insert(&fs, tree, "gles_yuv2rgb_chroma.frag", "void main()");
  if (ok && yuv)
    ok = Insert(&fs, tree, "gles_tonemap.frag", "void main()");
  ok = ok && Insert(&fs, tree, "gles_dither_uniforms.frag", "void main()");
  // (Lut3dGLES.cpp CLut3dGLES::Insert(): before the dither is put in)
  if (ok && lut == 2)
    ok = Insert(&fs, tree, "gles_lut3d_curve.frag", "void main()");
  if (ok && lut)
    ok = Insert(&fs, tree, "gles_lut3d.frag", "void main()") && Insert(&fs, tree, "gles_lut3d_body.frag", out);
  ok = ok && Insert(&fs, tree, "gles_dither_body.frag", out);
  char what[256];
  snprintf(what, sizeof what, "%s: %s%s", w->name, KIND_NAMES[kind], lut ? " with the LUT" : "");
  if (!ok)
  {
    Check(0, "%s: the shader files could not be put together", what);
    free(vs);
    free(fs);
    return 0;
  }
  GLuint v = Compile(GL_VERTEX_SHADER, vs, what), f = Compile(GL_FRAGMENT_SHADER, fs, what);
  free(vs);
  if (source)
    *source = fs;
  else
    free(fs);
  if (!v || !f)
    return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint linked = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &linked);
  if (!linked)
  {
    Check(0, "%s does not link", what);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

// ------------------------------------------------------------------------------------------------- the LUT on the GPU
struct lutgl
{
  GLuint tex;
  GLuint curveTex; // its 1D LUT (0: none)
  struct dvbridge_lut_atlas atlas;
  int apply;
};

static int LutUpload(const struct dvbridge_lut* lut, struct lutgl* g, char* why, size_t whySize)
{
  memset(g, 0, sizeof *g);
  GLint max = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
  if (!dvbridge_lut_atlas_layout(lut, max, &g->atlas, why, whySize))
    return 0;
  // (as CLut3dGLES::Begin() and Put(): the texture made empty, then the slices of blue straight from the LUT, a few at a
  // time as Kodi's renderer puts them in its frames)
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glGenTextures(1, &g->tex);
  glActiveTexture(GL_TEXTURE0 + LUT_UNIT);
  glBindTexture(GL_TEXTURE_2D, g->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, g->atlas.width, g->atlas.height, 0, GL_RGB, GL_FLOAT, NULL);
  const int n = g->atlas.size;
  for (int b = 0; b < n; ++b)
  {
    int x0 = 0, y0 = 0;
    dvbridge_lut_atlas_slice(&g->atlas, b, &x0, &y0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, n, n, GL_RGB, GL_FLOAT, dvbridge_lut_atlas_slice_data(lut, b));
  }
  // (as CLut3dGLES::PutCurve(): its 1D LUT, the full rows and then the points of the last, straight from the LUT)
  if (g->atlas.curve_size)
  {
    const int c = g->atlas.curve_size, cw = g->atlas.curve_width, full = c / cw, rest = c - full * cw;
    glGenTextures(1, &g->curveTex);
    glActiveTexture(GL_TEXTURE0 + CURVE_UNIT);
    glBindTexture(GL_TEXTURE_2D, g->curveTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, cw, g->atlas.curve_height, 0, GL_RGB, GL_FLOAT, NULL);
    if (full > 0)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, full, GL_RGB, GL_FLOAT, lut->curve);
    if (rest > 0)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, full, rest, 1, GL_RGB, GL_FLOAT, lut->curve + (size_t)full * cw * 3);
  }
  glActiveTexture(GL_TEXTURE0);
  g->apply = 1;
  return glGetError() == GL_NO_ERROR;
}

static void LutFree(struct lutgl* g)
{
  if (g->tex)
    glDeleteTextures(1, &g->tex);
  if (g->curveTex)
    glDeleteTextures(1, &g->curveTex);
  dvbridge_lut_atlas_free(&g->atlas);
  memset(g, 0, sizeof *g);
}

// (as CLut3dGLES::Bind())
static void LutBind(GLuint p, const struct lutgl* g, int limited)
{
  glActiveTexture(GL_TEXTURE0 + LUT_UNIT);
  glBindTexture(GL_TEXTURE_2D, g->tex);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(p, "m_lut3d"), LUT_UNIT);
  glUniform4fv(glGetUniformLocation(p, "m_lut3dGrid"), 1, g->atlas.grid);
  glUniform2fv(glGetUniformLocation(p, "m_lut3dIn"), 1, g->atlas.in);
  glUniform4fv(glGetUniformLocation(p, "m_lut3dShaper"), 1, g->atlas.shaper);
  glUniform2fv(glGetUniformLocation(p, "m_lut3dOut"), 1, g->atlas.out);
  glUniform3f(glGetUniformLocation(p, "m_lut3dSignal"), limited ? (940.0f - 64.0f) / 1023.0f : 1.0f,
              limited ? 64.0f / 1023.0f : 0.0f, g->apply ? 1.0f : 0.0f);
  if (g->curveTex)
  {
    glActiveTexture(GL_TEXTURE0 + CURVE_UNIT);
    glBindTexture(GL_TEXTURE_2D, g->curveTex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p, "m_lut3dCurveTable"), CURVE_UNIT);
    glUniform4fv(glGetUniformLocation(p, "m_lut3dCurve"), 1, g->atlas.curve);
  }
}

// ------------------------------------------------------------------------------------------------- drawing
struct target
{
  GLuint fbo, tex;
  int w, h;
  GLenum format;
};

static int Target(struct target* t, int w, int h, GLenum format)
{
  t->w = w;
  t->h = h;
  t->format = format;
  glGenTextures(1, &t->tex);
  glBindTexture(GL_TEXTURE_2D, t->tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
  const int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return ok;
}

static void TargetFree(struct target* t)
{
  glDeleteFramebuffers(1, &t->fbo);
  glDeleteTextures(1, &t->tex);
  memset(t, 0, sizeof *t);
}

static GLuint g_planes[3]; // the picture's textures (Y, U, V; or Y, UV, UV)
static GLuint g_kernel4, g_kernel6, g_kernel310;

// the planes as Kodi's renderer has them: a software picture LUMINANCE, LUMINANCE, ALPHA; a VAAPI one R8 and RG8;
// the chroma read GL_NEAREST by a chroma filter of more than one sample (CLinuxRendererGLES::SetChroma())
static void Planes(int nv12, int chromaFilter)
{
  glDeleteTextures(3, g_planes);
  glGenTextures(3, g_planes);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const int cw = g_w / 2, ch = g_h / 2;
  glBindTexture(GL_TEXTURE_2D, g_planes[0]);
  if (nv12)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g_w, g_h, 0, GL_RED, GL_UNSIGNED_BYTE, g_y);
  else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, g_w, g_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g_y);
  if (nv12)
  {
    unsigned char* uv = malloc((size_t)cw * ch * 2);
    for (size_t i = 0; i < (size_t)cw * ch; ++i)
    {
      uv[2 * i] = g_u[i];
      uv[2 * i + 1] = g_v[i];
    }
    glBindTexture(GL_TEXTURE_2D, g_planes[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, cw, ch, 0, GL_RG, GL_UNSIGNED_BYTE, uv);
    free(uv);
    glDeleteTextures(1, &g_planes[2]);
    g_planes[2] = g_planes[1];
  }
  else
  {
    glBindTexture(GL_TEXTURE_2D, g_planes[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, cw, ch, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g_u);
    glBindTexture(GL_TEXTURE_2D, g_planes[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, cw, ch, 0, GL_ALPHA, GL_UNSIGNED_BYTE, g_v);
  }
  for (int p = 0; p < 3; ++p)
  {
    const GLint filter = p > 0 && chromaFilter ? GL_NEAREST : GL_LINEAR;
    glBindTexture(GL_TEXTURE_2D, g_planes[p]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

// a kernel texture of 256 positions (RGBA16F), laid out as Kodi's CConvolutionKernel (the shaders normalize the taps):
// positive weights that change with the position, so that the scaled picture is not made of simple fractions of its
// codes (a box of 16 taps puts one value in 16 on a half of a code, which no kernel of Kodi does); what the LUT is
// checked against is the picture of the same scaler without it, whatever its kernel
static GLuint Kernel(void)
{
  float k[256 * 4];
  for (int i = 0; i < 256; ++i)
    for (int c = 0; c < 4; ++c)
      k[i * 4 + c] = (float)(0.6 + 0.35 * sin(0.0731 * i + 1.7 * c));
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 1, 0, GL_RGBA, GL_FLOAT, k);
  return t;
}

static GLuint g_vao, g_vbo[2], g_ibo;

static void Quad(GLuint p, int w, int h, int yuv)
{
  const GLfloat vert[4][3] = {{0, 0, 0}, {(float)w, 0, 0}, {(float)w, (float)h, 0}, {0, (float)h, 0}};
  const GLfloat tc[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  const GLubyte idx[4] = {0, 1, 3, 2};
  if (!g_vao)
  {
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(2, g_vbo);
    glGenBuffers(1, &g_ibo);
  }
  glBindVertexArray(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof vert, vert, GL_STATIC_DRAW);
  GLint lv = glGetAttribLocation(p, "m_attrpos");
  glVertexAttribPointer((GLuint)lv, 3, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray((GLuint)lv);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo[1]);
  glBufferData(GL_ARRAY_BUFFER, sizeof tc, tc, GL_STATIC_DRAW);
  const char* yuvAttr[3] = {"m_attrcordY", "m_attrcordU", "m_attrcordV"};
  const char* filterAttr[1] = {"m_attrcord"};
  for (int a = 0; a < (yuv ? 3 : 1); ++a)
  {
    GLint la = glGetAttribLocation(p, yuv ? yuvAttr[a] : filterAttr[a]);
    if (la >= 0)
    {
      glVertexAttribPointer((GLuint)la, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray((GLuint)la);
    }
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
  glBindVertexArray(0);
}

static void Matrices(GLuint p, int w, int h)
{
  const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const float proj[16] = {2.0f / w, 0, 0, 0, 0, 2.0f / h, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
  glUniformMatrix4fv(glGetUniformLocation(p, "m_proj"), 1, GL_FALSE, proj);
  glUniformMatrix4fv(glGetUniformLocation(p, "m_model"), 1, GL_FALSE, ident);
}

// the YUV shader of a way into a target: BT.709 of limited range from 8-bit textures (CConvertMatrix's), to an output
// of full range, or of limited range (64 to 940 of 1023, as CConvertMatrix::GetYuvMat())
static void DrawYuv(GLuint p, const struct way* w, enum kind kind, const struct target* t, const struct lutgl* lut)
{
  const double kr = 0.2126, kb = 0.0722, kg = 1 - kr - kb;
  const double ys = 255.0 / 219.0, cs = 255.0 / 224.0;
  // rows: R, G, B of (Y, Cb, Cr, 1)
  double m[3][4] = {{ys, 0, 2 * (1 - kr) * cs, 0},
                    {ys, -2 * kb * (1 - kb) / kg * cs, -2 * kr * (1 - kr) / kg * cs, 0},
                    {ys, 2 * (1 - kb) * cs, 0, 0}};
  for (int r = 0; r < 3; ++r)
    m[r][3] = -16.0 / 255.0 * m[r][0] - 128.0 / 255.0 * (m[r][1] + m[r][2]);
  const double sc = w->limited ? (940.0 - 64.0) / 1023.0 : 1.0, of = w->limited ? 64.0 / 1023.0 : 0.0;
  float mat[16] = {0};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      mat[c * 4 + r] = (float)(m[r][c] * sc + (c == 3 ? of : 0.0));
  mat[15] = 1;
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glViewport(0, 0, t->w, t->h);
  glDisable(GL_BLEND);
  glUseProgram(p);
  for (int i = 0; i < 3; ++i)
  {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, g_planes[i]);
  }
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, g_kernel310);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(p, "m_sampY"), 0);
  glUniform1i(glGetUniformLocation(p, "m_sampU"), 1);
  glUniform1i(glGetUniformLocation(p, "m_sampV"), 2);
  glUniform1i(glGetUniformLocation(p, "m_kernelTex"), 3);
  glUniform2f(glGetUniformLocation(p, "m_step"), 1.0f / g_w, 1.0f / g_h);
  glUniformMatrix4fv(glGetUniformLocation(p, "m_yuvmat"), 1, GL_FALSE, mat);
  glUniform1f(glGetUniformLocation(p, "m_alpha"), 1.0f);
  glUniform4f(glGetUniformLocation(p, "m_chromaSize"), (float)(g_w / 2), (float)(g_h / 2), 2.0f / g_w, 2.0f / g_h);
  glUniform2f(glGetUniformLocation(p, "m_chromaShift"), 0.25f, 0.0f); // (H.264's chroma: left)
  glUniform1f(glGetUniformLocation(p, "m_stepX"), 1.0f / g_w);
  glUniform1f(glGetUniformLocation(p, "m_stepY"), 1.0f / g_h);
  glUniform1i(glGetUniformLocation(p, "m_field"), w->field);
  if (w->tonemap)
  {
    // (the picture taken as PQ of BT.2020 and tone mapped by Hable to BT.709, as YUV2RGBShaderGLES does: 1000 cd/m2)
    const float prim[9] = {1.6605f, -0.1246f, -0.0182f, -0.5876f, 1.1329f, -0.1006f, -0.0728f, -0.0083f, 1.1187f};
    glUniformMatrix3fv(glGetUniformLocation(p, "m_primMat"), 1, GL_FALSE, prim);
    glUniform1f(glGetUniformLocation(p, "m_gammaSrc"), 2.4f);
    glUniform1f(glGetUniformLocation(p, "m_gammaDstInv"), 1.0f / 2.2f);
    glUniform1f(glGetUniformLocation(p, "m_luminance"), 1000.0f);
    glUniform1f(glGetUniformLocation(p, "m_toneP1"), (10000.0f / 1000.0f) * 2.0f);
  }
  Matrices(p, t->w, t->h);
  if (lut)
    LutBind(p, lut, w->limited);
  (void)kind;
  Quad(p, t->w, t->h, 1);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// the scaler of a second pass, from the picture of the first (read GL_NEAREST, ConvolutionFilterShader)
static void DrawScaler(GLuint p, enum kind kind, GLuint picture, const struct target* t, const struct lutgl* lut,
                       int limited)
{
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glViewport(0, 0, t->w, t->h);
  glDisable(GL_BLEND);
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, picture);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, kind == CONV6 ? g_kernel6 : g_kernel4);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(p, "img"), 0);
  glUniform1i(glGetUniformLocation(p, "kernelTex"), 2);
  glUniform2f(glGetUniformLocation(p, "stepxy"), 1.0f / g_w, 1.0f / g_h);
  glUniform1f(glGetUniformLocation(p, "m_alpha"), 1.0f);
  Matrices(p, t->w, t->h);
  if (lut)
    LutBind(p, lut, limited);
  Quad(p, t->w, t->h, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ------------------------------------------------------------------------------------------------- reading
// RGBA floats of a target (RGBA32F: as drawn; RGBA8 and RGB10_A2: their codes)
static float* Read(const struct target* t)
{
  const size_t n = (size_t)t->w * t->h;
  float* f = malloc(n * 4 * sizeof(float));
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  if (t->format == GL_RGBA32F)
    glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_FLOAT, f);
  else if (t->format == GL_RGBA8)
  {
    unsigned char* b = malloc(n * 4);
    glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, b);
    for (size_t i = 0; i < n * 4; ++i)
      f[i] = (float)b[i];
    free(b);
  }
  else
  {
    uint32_t* w = malloc(n * 4);
    glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, w);
    for (size_t i = 0; i < n; ++i)
    {
      f[i * 4] = (float)(w[i] & 1023u);
      f[i * 4 + 1] = (float)((w[i] >> 10) & 1023u);
      f[i * 4 + 2] = (float)((w[i] >> 20) & 1023u);
      f[i * 4 + 3] = 0;
    }
    free(w);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (glGetError() != GL_NO_ERROR)
  {
    free(f);
    return NULL;
  }
  return f;
}

static int Write(const char* dir, const char* name, const float* p, int w, int h)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.f32", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  int ok = 1;
  for (int y = 0; y < h && ok; y += STRIDE)
    for (int x = 0; x < w && ok; x += STRIDE)
      ok = fwrite(p + ((size_t)y * w + x) * 4, sizeof(float), 3, f) == 3;
  return fclose(f) == 0 && ok;
}

// the largest difference of the RGB of two pictures, each held within [0, 1] first (an output of fixed point holds
// them so; the LUT takes [0, 1])
static double Apart(const float* a, const float* b, size_t n, int clampBoth)
{
  double worst = 0;
  for (size_t i = 0; i < n; ++i)
    for (int c = 0; c < 3; ++c)
    {
      double x = a[i * 4 + c], y = b[i * 4 + c];
      if (clampBoth)
      {
        x = x < 0 ? 0 : x > 1 ? 1 : x;
        y = y < 0 ? 0 : y > 1 ? 1 : y;
      }
      const double d = fabs(x - y);
      worst = d > worst ? d : worst;
    }
  return worst;
}

static size_t Differ(const float* a, const float* b, size_t n)
{
  size_t k = 0;
  for (size_t i = 0; i < n; ++i)
    k += a[i * 4] != b[i * 4] || a[i * 4 + 1] != b[i * 4 + 1] || a[i * 4 + 2] != b[i * 4 + 2];
  return k;
}

// the pixels of two pictures of codes (a: with an identity LUT, b: without it) that differ by more than one code, or by
// one where their values (fa, fb: the same pictures in floats) are more than two units of the last place of a float
// apart (2.4e-7 below 1). Within that, two values may be on either side of a rounding boundary of the GPU's
// conversion to fixed point, wherever it puts it (Mesa's lavapipe makes RGB10_A2 from 16 bits: its boundaries are at
// the halves of a code of 16 bits, not of 10): *halves counts those.
static size_t DifferBeyondHalf(const float* a, const float* b, const float* fa, const float* fb, size_t n,
                               double levels, size_t* halves)
{
  size_t k = 0;
  *halves = 0;
  for (size_t i = 0; i < n; ++i)
  {
    int bad = 0, half = 0;
    for (int c = 0; c < 3; ++c)
    {
      const double d = fabs((double)a[i * 4 + c] - (double)b[i * 4 + c]);
      if (d == 0)
        continue;
      double va = fa[i * 4 + c], vb = fb[i * 4 + c];
      va = va < 0 ? 0 : va > 1 ? 1 : va;
      vb = vb < 0 ? 0 : vb > 1 ? 1 : vb;
      const double xa = va * levels, xb = vb * levels;
      if (d <= 1 && fabs(va - vb) <= 2.4e-7)
        half = 1;
      else
      {
        if (!bad && k < 5)
          printf("   at pixel %zu, channel %d: codes %g and %g, values %.9g and %.9g (x %g: %.6f and %.6f)\n", i, c,
                 (double)a[i * 4 + c], (double)b[i * 4 + c], va, vb, levels, xa, xb);
        bad = 1;
      }
    }
    k += bad;
    *halves += !bad && half;
  }
  return k;
}

static int SameBits(const float* a, const float* b, size_t n)
{
  return memcmp(a, b, n * 4 * sizeof(float)) == 0;
}

// ------------------------------------------------------------------------------------------------- the LUT files
static struct dvbridge_lut* Parse(const char* dir, const char* name, struct dvbridge_lut_range range)
{
  char path[1024], why[256];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  size_t size = 0;
  char* bytes = Slurp(path, &size);
  struct dvbridge_lut* lut = bytes ? dvbridge_lut_parse(bytes, size, name, range, why, sizeof why) : NULL;
  free(bytes);
  if (!lut)
    Check(0, "%s: not read (%s)", name, bytes ? why : "no file");
  return lut;
}

struct lutfile
{
  const char* file;
  const char* key; // lut3d_check.py tables()
  int video;       // read with the range of video levels (videoscreen.hadeslut3drange)
};
static const struct lutfile TINT = {"tint17.cube", "tint", 0};
static const struct lutfile MORE[] = {{"video17.cube", "video", 1},  {"nuke17.3dl", "nuke", 0},
                                      {"unif9.3dl", "unif", 0},       {"madvr16.3dlut", "madvr", 0},
                                      {"vcgt256.cal", "cal", 0},      {"steep1d.cube", "steep1d", 0},
                                      {"long1d.cube", "long1d", 0},   {"shaper.cube", "shaper", 0}};
static const struct lutfile IDS[] = {{"id17.cube", "id17", 0}, {"id33.cube", "id33", 0}, {"id65.cube", "id65", 0},
                                     {"id1d256.cube", "id1d256", 0}};

// ------------------------------------------------------------------------------------------------- one way
static const char* g_tree;
static const char* g_luts;
static const char* g_out;

// draws a way once: with no LUT (lut NULL), or with one; into a target of format; the floats (caller frees)
static float* Picture(const struct way* w, const struct lutgl* lut, GLenum format, int* ow, int* oh, char** source)
{
  const int two = w->fbo != 0;
  *ow = two ? g_w * 5 / 4 : g_w;
  *oh = two ? g_h * 5 / 4 : g_h;
  struct target t;
  if (!Target(&t, *ow, *oh, format))
    return NULL;
  float* f = NULL;
  if (!two)
  {
    GLuint p = Program(g_tree, w, w->kind, lut ? (lut->curveTex ? 2 : 1) : 0, source);
    if (p)
    {
      DrawYuv(p, w, w->kind, &t, lut);
      f = Read(&t);
      glDeleteProgram(p);
    }
  }
  else
  {
    // the first pass, without the LUT, at the picture's size (CLinuxRendererGLES::RenderToFBO()), then the scaler
    struct target first;
    GLuint p1 = Program(g_tree, w, BASIC, 0, NULL);
    GLuint p2 = Program(g_tree, w, w->kind, lut ? (lut->curveTex ? 2 : 1) : 0, source);
    if (p1 && p2 && Target(&first, g_w, g_h, w->fbo))
    {
      DrawYuv(p1, w, BASIC, &first, NULL);
      DrawScaler(p2, w->kind, first.tex, &t, lut, w->limited);
      f = Read(&t);
      TargetFree(&first);
    }
    if (p1)
      glDeleteProgram(p1);
    if (p2)
      glDeleteProgram(p2);
  }
  TargetFree(&t);
  return f;
}

// the time of the draw of a picture of a way of one pass into RGBA8, without the LUT and with it: the median of 7, each
// waited for (glFinish()), the shader made before (informative: on the software renderer the processor does the
// GPU's work)
static int CompareMs(const void* a, const void* b)
{
  const double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

static void Time(const struct way* w, const struct lutgl* g, int W, int H)
{
  double ms[2] = {0, 0};
  for (int with = 0; with < 2; ++with)
  {
    struct target t;
    GLuint p = Program(g_tree, w, w->kind, with ? (g->curveTex ? 2 : 1) : 0, NULL);
    if (!p || !Target(&t, W, H, GL_RGBA8))
      return;
    DrawYuv(p, w, w->kind, &t, with ? g : NULL);
    glFinish();
    double each[7];
    for (int k = 0; k < 7; ++k)
    {
      const double t0 = NowMs();
      DrawYuv(p, w, w->kind, &t, with ? g : NULL);
      glFinish();
      each[k] = NowMs() - t0;
    }
    qsort(each, 7, sizeof *each, CompareMs);
    ms[with] = each[3];
    TargetFree(&t);
    glDeleteProgram(p);
  }
  printf("time %s: %.1f ms a picture drawn without the LUT, %.1f with it (%dx%d into RGBA8, the median of 7, on the "
         "software renderer)\n",
         w->name, ms[0], ms[1], W, H);
}

static void Way(const struct way* w, int all)
{
  Planes(w->nv12, w->chroma);
  int W = 0, H = 0;
  char* plain = NULL;
  float* off = Picture(w, NULL, GL_RGBA32F, &W, &H, &plain);
  float* off8 = Picture(w, NULL, GL_RGBA8, &W, &H, NULL);
  float* off10 = Picture(w, NULL, GL_RGB10_A2, &W, &H, NULL);
  if (!off || !off8 || !off10)
  {
    Check(0, "%s: not drawn without a LUT", w->name);
    return;
  }
  const size_t n = (size_t)W * H;
  char name[256];
  snprintf(name, sizeof name, "%s-off", w->name);
  Write(g_out, name, off, W, H);
  Check(plain && !strstr(plain, "KodiLut3d") && !strstr(plain, "m_lut3d"),
        "%s: the shader put together without a LUT has nothing of it", w->name);
  free(plain);
  // the known LUT, and on the first way every format
  const struct lutfile* files[1 + sizeof MORE / sizeof *MORE];
  int nf = 0;
  files[nf++] = &TINT;
  for (size_t i = 0; all && i < sizeof MORE / sizeof *MORE; ++i)
    files[nf++] = &MORE[i];
  for (int i = 0; i < nf; ++i)
  {
    const struct dvbridge_lut_range range = files[i]->video ? (struct dvbridge_lut_range){16.0f, 235.0f}
                                                            : (struct dvbridge_lut_range){0.0f, 255.0f};
    struct dvbridge_lut* lut = Parse(g_luts, files[i]->file, range);
    struct lutgl g;
    char why[256] = "";
    if (!lut || !LutUpload(lut, &g, why, sizeof why))
    {
      Check(0, "%s: %s: no table on the GPU (%s)", w->name, files[i]->file, why);
      dvbridge_lut_free(&lut);
      continue;
    }
    char* src = NULL;
    int w2, h2;
    float* got = Picture(w, &g, GL_RGBA32F, &w2, &h2, &src);
    if (!got)
      Check(0, "%s: %s: not drawn", w->name, files[i]->file);
    else
    {
      snprintf(name, sizeof name, "%s-%s", w->name, files[i]->key);
      Write(g_out, name, got, W, H);
      printf("pair %s %s %s-off %s %d %d %d %s\n", w->name, files[i]->key, w->name, name, W, H, w->limited,
             w->kind == BASIC && !w->fbo ? "mediump" : "highp");
      // the place of the LUT in the shader: after the clamp of the output (where there is one) and before the dither
      const char* call = src ? strstr(src, "rgb.rgb = KodiLut3d(rgb.rgb);") : NULL;
      const char* dither = src ? strstr(src, "#if defined(XBMC_DITHER)\n  if (m_ditherEnabled") : NULL;
      const char* clamp = src ? strstr(src, "rgb = clamp(rgb, 0.0, 1.0);") : NULL;
      const char* head = src ? strstr(src, "highp vec3 KodiLut3d(highp vec3 signal)") : NULL;
      const char* mainAt = src ? strstr(src, "void main()") : NULL;
      Check(call && dither && call < dither && (!clamp || clamp < call) && head && mainAt && head < mainAt,
            "%s: %s: the LUT's call after the clamp of the output and before the dither, its functions before main()",
            w->name, files[i]->file);
      // (a 1D LUT: its functions before those of the table, which call them; none of them without one)
      const char* curve = src ? strstr(src, "highp vec3 KodiLut3dCurve(highp vec3 c)") : NULL;
      Check(g.curveTex ? curve && head && curve < head : !curve && src && !strstr(src, "m_lut3dCurve"),
            "%s: %s: %s", w->name, files[i]->file,
            g.curveTex ? "the functions of its 1D LUT before those of the table"
                       : "no function or uniform of a 1D LUT in the shader of a LUT without one");
      free(src);
      if (i == 0)
      {
        // made with the LUT and told not to apply it (a picture drawn elsewhere than into the window): the picture
        // without it, to the bit
        g.apply = 0;
        float* quiet = Picture(w, &g, GL_RGBA32F, &w2, &h2, NULL);
        Check(quiet && SameBits(quiet, off, n),
              "%s: a shader with the LUT told not to apply it draws the picture without it, to the bit (%zu pixels "
              "differ)",
              w->name, quiet ? Differ(quiet, off, n) : n);
        free(quiet);
        g.apply = 1;
        // the time of a picture with it and without it (informative: the software renderer)
        if (all)
          Time(w, &g, W, H);
      }
    }
    free(got);
    LutFree(&g);
    dvbridge_lut_free(&lut);
  }
  // the identity LUTs: every byte of RGBA8 and every code of RGB10_A2 of the picture without it
  for (size_t i = 0; i < (all ? sizeof IDS / sizeof *IDS : 1); ++i)
  {
    struct dvbridge_lut* lut = Parse(g_luts, IDS[i].file, (struct dvbridge_lut_range){0.0f, 255.0f});
    struct lutgl g;
    char why[256] = "";
    if (!lut || !LutUpload(lut, &g, why, sizeof why))
    {
      Check(0, "%s: %s: no table on the GPU (%s)", w->name, IDS[i].file, why);
      dvbridge_lut_free(&lut);
      continue;
    }
    int w2, h2;
    float* f32 = Picture(w, &g, GL_RGBA32F, &w2, &h2, NULL);
    float* f8 = Picture(w, &g, GL_RGBA8, &w2, &h2, NULL);
    float* f10 = Picture(w, &g, GL_RGB10_A2, &w2, &h2, NULL);
    const double apart = f32 ? Apart(f32, off, n, !w->limited) : 1;
    size_t h8 = 0, h10 = 0;
    const size_t d8 = f8 ? Differ(f8, off8, n) : n, d10 = f10 ? Differ(f10, off10, n) : n;
    const size_t b8 = f8 && f32 ? DifferBeyondHalf(f8, off8, f32, off, n, 255.0, &h8) : n;
    const size_t b10 = f10 && f32 ? DifferBeyondHalf(f10, off10, f32, off, n, 1023.0, &h10) : n;
    printf("identity %s %s: floats within %.3g, %zu of %zu pixels of RGBA8 and %zu of RGB10_A2 differ (by one code, "
           "their floats on either side of a rounding boundary: %zu and %zu)\n",
           w->name, IDS[i].file, apart, d8, n, d10, h8, h10);
    Check(f8 && f10 && b8 == 0 && b10 == 0 && apart <= 2.4e-7,
          "%s: %s: the floats within %.3g of the picture without it%s, and every byte of RGBA8 and every code of "
          "RGB10_A2 its own but where those floats are on either side of a rounding boundary of the GPU (%zu and %zu "
          "pixels, by one code)",
          w->name, IDS[i].file, apart, w->limited ? " (also below black and above white)" : "", h8, h10);
    free(f32);
    free(f8);
    free(f10);
    LutFree(&g);
    dvbridge_lut_free(&lut);
  }
  free(off);
  free(off8);
  free(off10);
}

// ------------------------------------------------------------------------------------------------- the table
static void Table(void)
{
  struct dvbridge_lut* lut = Parse(g_luts, "tint17.cube", (struct dvbridge_lut_range){0.0f, 255.0f});
  if (!lut)
    return;
  struct dvbridge_lut_atlas a;
  char why[256] = "";
  if (!dvbridge_lut_atlas_make(lut, 16384, &a, why, sizeof why))
  {
    Check(0, "the table of tint17.cube: not made (%s)", why);
    dvbridge_lut_free(&lut);
    return;
  }
  // every grid point at its place: slice b at column b % cols and row b / cols of slices, red along x, green along y
  size_t bad = 0;
  const int n = lut->size;
  for (int b = 0; b < n; ++b)
    for (int g = 0; g < n; ++g)
      for (int r = 0; r < n; ++r)
      {
        float want[3];
        dvbridge_lut_point(lut, r, g, b, want);
        const float* at = a.rgb + ((size_t)((b / a.cols) * n + g) * a.width + (size_t)(b % a.cols) * n + r) * 3;
        bad += memcmp(at, want, sizeof want) != 0;
      }
  Check(bad == 0 && a.cols * a.cols >= n && (a.cols & (a.cols - 1)) == 0 && a.width == a.cols * n &&
            a.height == a.rows * n,
        "the table of tint17.cube in a 2D texture of %dx%d (%d slices a row): every one of its %d grid points as "
        "dvbridge_lut_point() gives it (%zu not)",
        a.width, a.height, a.cols, n * n * n, bad);
  // the layout without a copy is that of the copy, and each slice straight from the LUT holds the copy's texels
  struct dvbridge_lut_atlas l;
  int laid = dvbridge_lut_atlas_layout(lut, 16384, &l, why, sizeof why);
  Check(laid && l.rgb == NULL && l.size == a.size && l.cols == a.cols && l.rows == a.rows && l.width == a.width &&
            l.height == a.height && !memcmp(l.grid, a.grid, sizeof l.grid) && !memcmp(l.in, a.in, sizeof l.in) &&
            !memcmp(l.shaper, a.shaper, sizeof l.shaper) && !memcmp(l.out, a.out, sizeof l.out),
        "the layout without a copy is that of the copy (%dx%d, %d slices a row), and makes none", l.width, l.height,
        l.cols);
  size_t off = 0;
  for (int b = 0; laid && b < n; ++b)
  {
    int x0 = 0, y0 = 0;
    dvbridge_lut_atlas_slice(&l, b, &x0, &y0);
    const float* slice = dvbridge_lut_atlas_slice_data(lut, b);
    for (int g = 0; g < n; ++g)
      off += memcmp(a.rgb + ((size_t)(y0 + g) * a.width + x0) * 3, slice + (size_t)g * n * 3, (size_t)n * 3 * sizeof(float)) != 0;
  }
  Check(laid && off == 0, "each slice put straight from the LUT holds the texels of the copy (%zu rows not)", off);
  dvbridge_lut_atlas_free(&a);
  const int made = dvbridge_lut_atlas_make(lut, 100, &a, why, sizeof why);
  Check(!made && a.rgb == NULL && strstr(why, "takes at most 100"),
        "a table larger than the GPU takes is refused with the reason (%s)", why);
  dvbridge_lut_atlas_free(&a);
  Check(l.curve_size == 0 && l.curve_width == 0, "a LUT without a 1D LUT has no texture of one");
  dvbridge_lut_free(&lut);
  // a 1D LUT of 5000 points: rows of 4096 texels, the last filled as far as they go; the uniform of its shader
  lut = Parse(g_luts, "long1d.cube", (struct dvbridge_lut_range){0.0f, 255.0f});
  if (!lut)
    return;
  laid = dvbridge_lut_atlas_layout(lut, 16384, &l, why, sizeof why);
  Check(laid && l.size == 2 && l.curve_size == 5000 && l.curve_width == DVBRIDGE_LUT_CURVE_ROW && l.curve_height == 2 &&
            l.curve[0] == 4999.0f && l.curve[1] == (float)DVBRIDGE_LUT_CURVE_ROW &&
            l.curve[2] == (float)(1.0 / DVBRIDGE_LUT_CURVE_ROW) && l.curve[3] == 0.5f,
        "a 1D LUT of 5000 points: a texture of %dx%d, its uniform %g %g %g %g (the identity of 2 points its table)",
        l.curve_width, l.curve_height, (double)l.curve[0], (double)l.curve[1], (double)l.curve[2], (double)l.curve[3]);
  laid = dvbridge_lut_atlas_layout(lut, 2048, &l, why, sizeof why);
  Check(!laid && strstr(why, "1D LUT of 5000 points") && strstr(why, "takes at most 2048"),
        "a 1D LUT wider than the GPU takes is refused with the reason (%s)", why);
  dvbridge_lut_free(&lut);
}

int main(int argc, char** argv)
{
  if (argc != 5)
  {
    fprintf(stderr, "usage: lutsdr_test <Kodi tree> <clip> <LUT folder> <out folder>\n");
    return 2;
  }
  g_tree = argv[1];
  g_luts = argv[3];
  g_out = argv[4];
  if (!Decode(argv[2], 40))
  {
    printf("FAIL: the clip could not be decoded\nFAILED: 1 failed\n");
    return 1;
  }
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  EGLConfig cfg;
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API) ||
      !eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
  {
    printf("FAIL: no EGL display of OpenGL ES 3\nFAILED: 1 failed\n");
    return 1;
  }
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
  {
    printf("FAIL: no OpenGL ES 3.1 context\nFAILED: 1 failed\n");
    return 1;
  }
  printf("GL_RENDERER %s\n", (const char*)glGetString(GL_RENDERER));
  g_kernel4 = Kernel();
  g_kernel6 = Kernel();
  g_kernel310 = Kernel();
  Table();
  // the ways Kodi's renderer draws a picture itself
  const struct way ways[] = {
      {"basic-yv12", BASIC, 0, 1, 0, 0, 1, 0},       // software decoding, one pass, Hades' chroma (Lanczos 3)
      {"basic-nv12-limited", BASIC, 1, 0, 0, 1, 1, 0}, // VAAPI, Kodi's own chroma, an output of limited range
      {"filter310", FILTER, 1, 1, 0, 0, 1, 0},       // the fast scalers of GLES 3.1 in one pass
      {"bob", BOB, 0, 0, 0, 0, 1, 0},                // the top field of an interlaced picture
      {"tonemap", BASIC, 0, 1, 1, 0, 1, 0},          // HDR tone mapped to SDR by Kodi's shaders (Hable)
      {"conv6", CONV6, 0, 1, 0, 0, 1, GL_RGBA16F},   // two passes: Lanczos 3 of Kodi, the first in RGBA16F
      {"conv4", CONV4, 0, 1, 0, 1, 1, GL_RGBA8},     // ... a scaler of 4x4, the first in RGBA8, limited range
  };
  for (size_t i = 0; i < sizeof ways / sizeof *ways; ++i)
    Way(&ways[i], i == 0);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
