// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: a host test of Kodi's Dolby Vision renderer (tools/dvbridge: dvbridge_render.c, dvbridge_gl_pack.c,
// dvbridge_gl_frame.c, dvbridge_core.c, dvbridge_placebo.c, the files themselves) on a software OpenGL ES:
// it draws pictures of a real profile 7 FEL stream the two ways Kodi draws them.
//
//   composed: reconstructed into the RGBA32F picture, then packed (what a frame with GUI elements gets)
//   direct  : the shader writes the transport into the output (needs shader subgroups: zink on lavapipe)
//
// For every picture drawn and every set of margins it prints a hash of the 3840x2160 RGBA8 output of each
// way and of the RGBA32F picture, so that runs with another libplacebo or another dvbridge can be compared
// line by line, and checks:
//   - the RGBA32F picture holds only values within [0, 1];
//   - each way gives the same result with and without writing the area outside the active picture before
//     the reconstruction: the same floats, the same bytes (turned off with the renderer's own switch,
//     dvbridge_render_allow_early_mask());
//   - the direct and the composed way differ by at most one code of 4096, in few pixels, never outside
//     the active picture, never in the metadata bits, never in alpha. (They are two shaders that round the
//     same value: on this renderer under 0.1 % of the pixels land on the other side of a code.)
// Without shader subgroups (llvmpipe) only the composed way runs and the test says SKIPPED (exit 77).
//
// The layers are decoded in software and uploaded as P010 (16-bit, 10 bits in the high bits), the layout of
// the VAAPI surfaces in Kodi. Pairing is by dvbridge_fel.c, as in Kodi. Where the bridge takes the chroma
// location of a layer (DVBRIDGE_GL_FRAME_CHROMA), each layer is imported with that of its decoded picture, as
// Kodi imports it: for the base layer Kodi passes VideoPicture::chroma_position, which is the decoder's
// chroma_sample_location, and the test checks that the picture has that one; for the enhancement layer, the
// AVFrame's. Where the packer packs with quad operations (DVBRIDGE_GL_PACKER_QUAD), its bytes are checked
// against the packing with two fetches.
//
// Usage: dvrender_test <file> <first picture> <step> <pictures> [half]
//   half: every second sample of the layers is uploaded, a 1920x1080 picture that the renderer scales. Then
//         nothing is written before the reconstruction, and the two ways are only described.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
// the composer switch of the bridge, where it has one (weak: a bridge without it
// links, and composes by libplacebo's reshaping, the only way it has); DV_CCM=0 asks for the reshaping
__attribute__((weak)) void dvbridge_render_use_ccm_composer(struct dvbridge_renderer* renderer, bool use);
__attribute__((weak)) bool dvbridge_render_ccm_composer(const struct dvbridge_renderer* renderer);
static void CcmSwitch(struct dvbridge_renderer* r, const char* name)
{
  const char* e = getenv("DV_CCM");
  if (e && !strcmp(e, "0") && dvbridge_render_use_ccm_composer)
    dvbridge_render_use_ccm_composer(r, false);
  printf("TEST: composer of %s: %s\n", name,
         !dvbridge_render_ccm_composer ? "libplacebo's reshaping (the bridge has no other)"
         : dvbridge_render_ccm_composer(r) ? "ETSI GS CCM 001" : "libplacebo's reshaping (DV_CCM=0)");
}

// the chroma location of a layer, for a bridge whose import takes it
#ifdef DVBRIDGE_GL_FRAME_CHROMA
#define IMPORT_CHROMA(l) , (l)->loc
#else
#define IMPORT_CHROMA(l)
#endif

#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI

#include "pack_sources.h" // PACK_VERTEX, PACK_FRAGMENT: pack_gles300.vert/.frag as strings

#define W 3840
#define H 2160

static int g_failures;
static int g_half;
// over all drawings
static long g_drawings, g_withMargins, g_identical;
static long g_earlyUsed[2], g_earlyGivenUp[2], g_earlyDiffering[2]; // [0] composed, [1] direct
static long g_outside, g_notFinite; // values of the RGBA32F picture
static long g_alphaOther; // alpha values of the float picture that are not 2
static long g_mostPixels, g_mostCode, g_inBars, g_alpha;
static long g_bits; // metadata bits that read back differently from the two ways
static long g_locPictures, g_locMismatch; // pictures, and those whose chroma location is not the decoder's

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
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

struct layer
{
  GLuint tex[2];
  int w, h;
  enum AVChromaLocation loc; // of the decoded picture
  struct dvbridge_gl_plane planes[3];
};

// yuv420p10le to P010: luma in one 16-bit texture, the two chroma planes interleaved in a second one,
// the 10 bits in the high bits. With step 2, every second sample of every second row.
static int Upload(struct layer* l, const AVFrame* f, int step)
{
  if (f->format != AV_PIX_FMT_YUV420P10LE)
    return 0;
  const int w = f->width / step, h = f->height / step, cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
  if (!y || !c)
    return 0;
  for (int r = 0; r < h; ++r)
  {
    const uint16_t* s = (const uint16_t*)(f->data[0] + (ptrdiff_t)r * step * f->linesize[0]);
    for (int x = 0; x < w; ++x)
      y[(size_t)r * w + x] = (uint16_t)(s[x * step] << 6);
  }
  for (int r = 0; r < ch; ++r)
  {
    const uint16_t* u = (const uint16_t*)(f->data[1] + (ptrdiff_t)r * step * f->linesize[1]);
    const uint16_t* v = (const uint16_t*)(f->data[2] + (ptrdiff_t)r * step * f->linesize[2]);
    for (int x = 0; x < cw; ++x)
    {
      c[((size_t)r * cw + x) * 2] = (uint16_t)(u[x * step] << 6);
      c[((size_t)r * cw + x) * 2 + 1] = (uint16_t)(v[x * step] << 6);
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
  free(y);
  free(c);
  l->w = w;
  l->h = h;
  // (with every second sample of every second row a left or top-left chroma sample is still on the luma
  // sample it was on)
  l->loc = f->chroma_location;
  l->planes[0] = (struct dvbridge_gl_plane){l->tex[0], w, h};
  l->planes[1] = (struct dvbridge_gl_plane){l->tex[1], cw, ch};
  l->planes[2] = (struct dvbridge_gl_plane){0, 0, 0};
  return glGetError() == GL_NO_ERROR;
}

// A copy of the parsed metadata with other level 5 offsets (the active picture), in a buffer of its own.
static AVDOVIMetadata* WithMargins(const AVDOVIMetadata* in, size_t bytes, const int m[4], size_t* out_bytes)
{
  const size_t extra = in->ext_block_size;
  uint8_t* buf = av_mallocz(bytes + extra);
  if (!buf)
    return NULL;
  memcpy(buf, in, bytes);
  AVDOVIMetadata* out = (AVDOVIMetadata*)buf;
  AVDOVIDmData* l5 = NULL;
  for (int i = 0; i < out->num_ext_blocks; ++i)
  {
    AVDOVIDmData* e = (AVDOVIDmData*)(buf + out->ext_block_offset + (size_t)i * out->ext_block_size);
    if (e->level == 5)
      l5 = e;
  }
  *out_bytes = bytes;
  if (!l5)
  {
    // the blocks are the last thing in the buffer
    if (out->ext_block_offset + (size_t)out->num_ext_blocks * out->ext_block_size != bytes)
    {
      av_free(buf);
      return NULL;
    }
    l5 = (AVDOVIDmData*)(buf + bytes);
    l5->level = 5;
    out->num_ext_blocks++;
    *out_bytes = bytes + extra;
  }
  l5->l5.left_offset = m[0];
  l5->l5.right_offset = m[1];
  l5->l5.top_offset = m[2];
  l5->l5.bottom_offset = m[3];
  return out;
}

struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* renderer;
  struct dvbridge_gl_packer* packer;
#ifdef DVBRIDGE_GL_PACKER_QUAD
  // the packing with two fetches, and one with quad operations whose fetch of the even pixel gives a wrong value
  struct dvbridge_gl_packer *plainPacker, *strictPacker;
#endif
  GLuint fbo, color;
  pl_tex scanout;
  uint8_t* composed;
  uint8_t* direct;
  uint8_t* plain; // the direct way without the early hook
  float* floats; // the RGBA32F picture of the composed way
  float* floatsPlain; // ... without the early hook
  int subgroups;
  float* hdr10; // the picture for output as HDR10
  uint32_t* copy; // ... copied to a 10-bit framebuffer
  GLuint fbo10, color10, fbo16, color16;
  struct dvbridge_gl_copier* copier;
  pl_opengl gl10;                       // a second pl_opengl on the context, as Kodi has for output as HDR10
  struct dvbridge_renderer* renderer10; // made for the picture as HDR10 alone, as Kodi makes it
  int windowBits;                       // of the default framebuffer, which is 3840x2160 like a window
  GLuint oddProgram, oddVao, oddBuffer, oddTextures[4]; // GL objects that a caller has bound
};

#ifdef DVBRIDGE_RENDER_HDR10
// The picture for output as HDR10 (dvbridge_render_hdr10(), dvbridge_gl_copy())
static long g_hdr10Drawn, g_hdr10Failed, g_hdr10Outside, g_hdr10NotFinite;
static double g_hdr10Most; // largest difference to the picture that the composed way packs, in codes of 1023
static long g_hdr10Over;   // values more than half a code of 1023 from it
static long g_copies, g_copyWrong[2]; // values of a 10-bit copy that are not the nearest code [flipped]
static long g_floatCopies;
static double g_floatCopyMost; // largest difference of a copy to a half-float framebuffer
static long g_refused, g_refusedKept; // frames that are not drawn, and the picture before them still there
static long g_parts;
static double g_partMost; // a part of the source drawn alone against that part of the whole picture
static long g_ownDrawn, g_ownSame; // the renderer made for HDR10 alone gives the same picture
static long g_windowCopies, g_windowWrong; // copies into the default framebuffer, as Kodi makes them
static long g_stateRuns, g_stateChanged, g_stateLeft; // GL state values changed by a render, and not put back
static long g_wideValues, g_wideChanged, g_wideLeft;  // ... of the wider look at the state
static long g_setupValues, g_setupChanged, g_setupLeft; // ... and by the set-up
static int g_staticRuns, g_staticOk; // dvbridge_hdr10_static()
#endif

static int Read(struct gl* g, uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

// The picture as it is in the RGBA32F texture, which is what the GUI is blended over.
static int ReadFloats(GLuint texture, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, into);
  const int read = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return read;
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

static GLuint Texture(struct gl* g)
{
  pl_tex tex = dvbridge_render_texture(g->renderer);
  return tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
}

#ifdef DVBRIDGE_RENDER_HDR10
static GLuint Hdr10Texture(struct gl* g)
{
  pl_tex tex = dvbridge_render_hdr10_texture(g->renderer);
  return tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
}

#ifdef DVBRIDGE_GL_STATE_UNITS
// The GL state that Kodi goes on drawing with, asked with queries of this test's own.
#define STATE_VALUES 40
static void StateProbe(int v[STATE_VALUES])
{
  static const GLenum caps[9] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_DITHER, GL_CULL_FACE,
                                 GL_RASTERIZER_DISCARD, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
  GLboolean mask[4];
  int n = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &v[n++]);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &v[n++]);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v[n++]);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v[n++]);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &v[n++]);
  glGetIntegerv(GL_VIEWPORT, &v[n]);
  n += 4;
  glGetIntegerv(GL_SCISSOR_BOX, &v[n]);
  n += 4;
  glGetIntegerv(GL_BLEND_SRC_RGB, &v[n++]);
  glGetIntegerv(GL_BLEND_DST_RGB, &v[n++]);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &v[n++]);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &v[n++]);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &v[n++]);
  int active = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  v[n++] = active;
  for (int unit = 0; unit < 4; ++unit)
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v[n++]);
    glGetIntegerv(GL_SAMPLER_BINDING, &v[n++]);
  }
  glActiveTexture(active);
  glGetBooleanv(GL_COLOR_WRITEMASK, mask);
  for (int i = 0; i < 4; ++i)
    v[n++] = mask[i];
  for (int i = 0; i < 9; ++i)
    v[n++] = glIsEnabled(caps[i]);
  if (n != STATE_VALUES)
    abort();
}

// What Kodi has set when it draws the video, and more: its own program, vertex array and buffer, other
// framebuffers, a viewport and a scissor rectangle that are not the whole target, blending, a texture on each
// of the first units.
static void OddState(struct gl* g)
{
  glUseProgram(g->oddProgram);
  glBindVertexArray(g->oddVao);
  glBindBuffer(GL_ARRAY_BUFFER, g->oddBuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->fbo16);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, g->fbo10);
  glViewport(11, 13, 1000, 900);
  glScissor(5, 7, 300, 200);
  glEnable(GL_SCISSOR_TEST);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int unit = 0; unit < 4; ++unit)
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, g->oddTextures[unit]);
  }
  glActiveTexture(GL_TEXTURE2);
}

// A wider look at the GL state than what the module keeps, so that what it does not keep is seen as well:
// the textures and samplers of 32 units, the other buffer bindings, how pixels are stored, blending, clearing,
// depth, stencil and rasterizer state, and the vertex attributes of the caller's vertex array and of the
// default one, which Kodi draws its GUI with. Returns how many values.
#define WIDE_UNITS 32
#define WIDE_MAX 512
static int WideProbe(int v[WIDE_MAX])
{
  static const GLenum unitState[] = {GL_TEXTURE_BINDING_2D, GL_SAMPLER_BINDING, GL_TEXTURE_BINDING_3D,
                                     GL_TEXTURE_BINDING_2D_ARRAY, GL_TEXTURE_BINDING_CUBE_MAP};
  static const GLenum integers[] = {
      GL_PIXEL_PACK_BUFFER_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING, GL_UNIFORM_BUFFER_BINDING,
      GL_COPY_READ_BUFFER_BINDING, GL_COPY_WRITE_BUFFER_BINDING, GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
      GL_RENDERBUFFER_BINDING, GL_TRANSFORM_FEEDBACK_BINDING,
      GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_UNPACK_ROW_LENGTH,
      GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES,
      GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA, GL_STENCIL_CLEAR_VALUE, GL_DEPTH_FUNC, GL_DEPTH_WRITEMASK,
      GL_STENCIL_FUNC, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK, GL_STENCIL_WRITEMASK, GL_STENCIL_FAIL,
      GL_STENCIL_PASS_DEPTH_FAIL, GL_STENCIL_PASS_DEPTH_PASS, GL_STENCIL_BACK_FUNC, GL_STENCIL_BACK_REF,
      GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_BACK_WRITEMASK, GL_STENCIL_BACK_FAIL,
      GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_STENCIL_BACK_PASS_DEPTH_PASS, GL_CULL_FACE_MODE, GL_FRONT_FACE,
      GL_SAMPLE_COVERAGE_INVERT, GL_GENERATE_MIPMAP_HINT, GL_FRAGMENT_SHADER_DERIVATIVE_HINT};
  // (value, how many floats): kept in thousandths
  static const struct { GLenum name; int count; } floats[] = {
      {GL_BLEND_COLOR, 4}, {GL_COLOR_CLEAR_VALUE, 4}, {GL_DEPTH_CLEAR_VALUE, 1}, {GL_DEPTH_RANGE, 2},
      {GL_LINE_WIDTH, 1}, {GL_POLYGON_OFFSET_FACTOR, 1}, {GL_POLYGON_OFFSET_UNITS, 1},
      {GL_SAMPLE_COVERAGE_VALUE, 1}};
  static const GLenum attribute[] = {GL_VERTEX_ATTRIB_ARRAY_ENABLED, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                                     GL_VERTEX_ATTRIB_ARRAY_TYPE, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                                     GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING};
  int n = 0, active = 0, array = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  for (int unit = 0; unit < WIDE_UNITS; ++unit)
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    for (unsigned i = 0; i < sizeof(unitState) / sizeof(*unitState); ++i)
      glGetIntegerv(unitState[i], &v[n++]);
  }
  glActiveTexture(active);
  for (unsigned i = 0; i < sizeof(integers) / sizeof(*integers); ++i)
    glGetIntegerv(integers[i], &v[n++]);
  for (unsigned i = 0; i < sizeof(floats) / sizeof(*floats); ++i)
  {
    GLfloat f[4] = {0};
    glGetFloatv(floats[i].name, f);
    for (int k = 0; k < floats[i].count; ++k)
      v[n++] = (int)lrintf(f[k] * 1000);
  }
  v[n++] = glIsEnabled(GL_POLYGON_OFFSET_FILL);
  v[n++] = glIsEnabled(GL_PRIMITIVE_RESTART_FIXED_INDEX);
  // the vertex attributes and the element buffer of the vertex array that is bound, then of the default one
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
  for (int which = 0; which < 2; ++which)
  {
    if (which)
      glBindVertexArray(0);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &v[n++]);
    for (int index = 0; index < 8; ++index)
      for (unsigned i = 0; i < sizeof(attribute) / sizeof(*attribute); ++i)
        glGetVertexAttribiv(index, attribute[i], &v[n++]);
  }
  glBindVertexArray(array);
  if (n > WIDE_MAX || glGetError() != GL_NO_ERROR)
    abort();
  return n;
}

// More of what a caller may have set, where it does no harm to what is drawn here: a texture on each of
// sixteen units, and clearing, depth, stencil and rasterizer state that is not the default.
static void OddWideState(struct gl* g)
{
  for (int unit = 0; unit < 16; ++unit)
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, g->oddTextures[unit % 4]);
  }
  glActiveTexture(GL_TEXTURE2);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
  glClearDepthf(0.5f);
  glClearStencil(3);
  glBlendColor(0.4f, 0.3f, 0.2f, 0.1f);
  glDepthFunc(GL_ALWAYS);
  glDepthMask(GL_FALSE);
  glStencilFunc(GL_NOTEQUAL, 2, 0x0f);
  glStencilMask(0x33);
  glStencilOp(GL_REPLACE, GL_INCR, GL_DECR);
  glCullFace(GL_FRONT);
  glFrontFace(GL_CW);
  glPolygonOffset(1.5f, 2.5f);
}

// ... and the defaults again, for what the test draws and reads afterwards
static void PlainWideState(void)
{
  for (int unit = 0; unit < 16; ++unit)
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glActiveTexture(GL_TEXTURE0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glClearColor(0, 0, 0, 0);
  glClearDepthf(1);
  glClearStencil(0);
  glBlendColor(0, 0, 0, 0);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glStencilFunc(GL_ALWAYS, 0, ~0u);
  glStencilMask(~0u);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glPolygonOffset(0, 0);
}
#endif

// dvbridge_hdr10_static() with the metadata of a picture, and with copies of it whose level 6 block and
// source range are changed: what an HDR10 output is told.
static void Static(const AVDOVIMetadata* meta, size_t bytes)
{
  struct dvbridge_hdr10_static s;
  const int had = dvbridge_hdr10_static(&s, meta, bytes);
  printf("static metadata of the picture: %s, mastering display %u to %.4f cd/m2, MaxCLL %u, MaxFALL %u\n",
         !had ? "none" : s.level6 ? "level 6" : "the range of the source", had ? s.max_luminance : 0,
         had ? s.min_luminance / 10000.0 : 0.0, had ? s.max_cll : 0, had ? s.max_fall : 0);
  ++g_staticRuns;
  int ok = had && s.max_luminance >= 1 && s.max_luminance <= 10000 && s.min_luminance <= 65535 && s.max_cll <= 10000 &&
           s.max_fall <= (s.max_cll ? s.max_cll : 10000);
  AVDOVIMetadata* copy = av_malloc(bytes);
  if (!copy)
    return;
  memcpy(copy, meta, bytes);
  AVDOVIDmData* l6 = av_dovi_find_level(copy, 6);
  if (l6 && had && s.level6)
  {
    struct dvbridge_hdr10_static t;
    // a light level above the peak of the mastering display is still the light level of the content
    l6->l6.max_cll = (uint16_t)(l6->l6.max_luminance + 400 <= 10000 ? l6->l6.max_luminance + 400 : 10000);
    l6->l6.max_fall = 200;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && t.level6 && t.max_cll == l6->l6.max_cll && t.max_fall == 200 &&
          t.max_luminance == s.max_luminance;
    // a frame average above the brightest pixel is not one; the brightest pixel stays
    l6->l6.max_fall = (uint16_t)(l6->l6.max_cll + 1);
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && t.max_cll == l6->l6.max_cll && t.max_fall == 0;
    // a frame average may be given without the brightest pixel (0: not known); more than the signal
    // carries is none
    l6->l6.max_cll = 0;
    l6->l6.max_fall = 300;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && t.level6 && t.max_cll == 0 && t.max_fall == 300;
    l6->l6.max_fall = 10001;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && t.level6 && t.max_cll == 0 && t.max_fall == 0;
    // more than the signal carries is not a light level
    l6->l6.max_cll = 10001;
    l6->l6.max_fall = 100;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && t.level6 && t.max_cll == 0 && t.max_fall == 0;
    // without a usable level 6: the range of the source, and a black level that fits its field
    l6->l6.max_luminance = 0;
    AVDOVIColorMetadata* color = av_dovi_get_color(copy);
    color->source_min_pq = 2081;
    color->source_max_pq = 3079;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && !t.level6 && t.max_luminance >= 900 && t.max_luminance <= 1100 &&
          t.min_luminance == 65535 && t.max_cll == 0 && t.max_fall == 0;
    color->source_min_pq = 62;
    ok &= dvbridge_hdr10_static(&t, copy, bytes) && !t.level6 && t.min_luminance >= 40 && t.min_luminance <= 60;
    color->source_max_pq = 0;
    ok &= !dvbridge_hdr10_static(&t, copy, bytes);
    color->source_max_pq = 62; // not above the black level
    ok &= !dvbridge_hdr10_static(&t, copy, bytes);
  }
  else
    ok = 0;
  av_free(copy);
  g_staticOk += ok;
}

// The picture for output as HDR10, against the float picture of the composed way (g->floats, just read):
// inside the active picture it is the same picture, to the precision of its half-float texture. With more:
// its copy to a 10-bit framebuffer, both ways up, and a part of the source drawn alone.
static void Convert(struct gl* g, const struct pl_frame* source, const struct layer* bl, const struct layer* el,
                    const AVDOVIMetadata* meta, size_t bytes, double pts, double elPts, const unsigned margins[4],
                    const char* label, int more)
{
  const struct dvbridge_view whole = {0, 0, (float)bl->w, (float)bl->h, 0, 0, W, H};
  glEnable(0x7fff); // a pending GL error, as before the other renders
  if (!dvbridge_render_hdr10(g->renderer, source, meta, bytes, pts, elPts, whole, W, H) || !Hdr10Texture(g) ||
      !ReadFloats(Hdr10Texture(g), g->hdr10))
  {
    printf("%s: not drawn for output as HDR10\n", label);
    ++g_hdr10Failed;
    return;
  }
  ++g_hdr10Drawn;
  double most = 0;
  long over = 0, outside = 0, strange = 0;
  for (int y = (int)margins[2]; y < H - (int)margins[3]; ++y)
    for (int x = (int)margins[0]; x < W - (int)margins[1]; ++x)
      for (int c = 0; c < 3; ++c)
      {
        const size_t i = ((size_t)y * W + x) * 4 + c;
        const float v = g->hdr10[i];
        if (!(v == v) || v - v != 0)
        {
          ++strange;
          continue;
        }
        outside += v < 0 || v > 1;
        const double d = fabs((double)g->floats[i] - v) * 1023;
        if (d > most)
          most = d;
        over += d > 0.5;
      }
  if (most > g_hdr10Most)
    g_hdr10Most = most;
  g_hdr10Over += over;
  g_hdr10Outside += outside;
  g_hdr10NotFinite += strange;
  printf("%s: for output as HDR10: largest difference to the picture of the composed way %.4f codes of 1023, "
         "%ld values over half a code, outside [0, 1] %ld, not finite %ld\n", label, most, over, outside, strange);
  if (!more)
    return;

  // Copied to a 10-bit framebuffer: each value becomes the nearest code, in its place. First the whole
  // picture, then a part of it into a framebuffer filled with another value, which has to stay around it.
  // A scissor rectangle is set, as the GUI leaves one: it must not cut the copy, and has to be there after it.
  // The last four passes: the same into the limited range, as Kodi's video goes when its output is set to it.
  const GLuint texture = Hdr10Texture(g);
  for (int pass = 0; pass < 8; ++pass)
  {
    const int limited = pass >= 4, flip = pass & 1, part = pass >> 1 & 1;
    const int rx = part ? 1001 : 0, ry = part ? 307 : 0, rw = part ? 1280 : W, rh = part ? 721 : H;
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbo10);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.5f, 0.5f, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(100, 100, 200, 200);
    const int copied = dvbridge_gl_copy(g->copier, texture, g->fbo10, W, H, rx, ry, rw, rh, flip, limited);
    GLint box[4] = {0};
    glGetIntegerv(GL_SCISSOR_BOX, box);
    const int scissor = glIsEnabled(GL_SCISSOR_TEST) && box[0] == 100 && box[1] == 100 && box[2] == 200 && box[3] == 200;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbo10);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, g->copy);
    const int read = glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    long wrong = 0, outsideRect = 0;
    if (copied && read && scissor)
    {
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
          const uint32_t word = g->copy[(size_t)(flip ? H - 1 - y : y) * W + x];
          const float* v = g->hdr10 + ((size_t)y * W + x) * 4;
          const int inside = x >= rx && x < rx + rw && y >= ry && y < ry + rh;
          for (int c = 0; c < 3; ++c)
          {
            const unsigned code = word >> (10 * c) & 1023;
            if (!inside)
            {
              outsideRect += code != 511 && code != 512; // what the framebuffer was filled with
              continue;
            }
            const double want = limited ? (double)v[c] * 876 + 64 : (double)v[c] * 1023;
            const int off = fabs((double)code - want) > 0.5005;
            if (off && wrong < 4)
              printf("%s: copy at %d,%d component %d: value %.6f is %.4f codes, the framebuffer has %u\n", label,
                     x, y, c, v[c], want, code);
            wrong += off;
          }
        }
    }
    else
      wrong = -1;
    ++g_copies;
    g_copyWrong[flip] += wrong < 0 ? 1 : wrong + outsideRect;
    printf("%s: %s copied to a 10-bit framebuffer%s%s: %s, %ld values that are not the nearest code, %ld written "
           "outside the place of the copy\n", label, part ? "a part" : "the picture",
           limited ? " in the limited range" : "", flip ? " with the first row at the bottom" : "",
           copied && read ? (scissor ? "ok" : "the scissor rectangle was not put back") : "failed", wrong,
           outsideRect);
  }
#ifdef DVBRIDGE_GL_STATE_UNITS
  // As Kodi does it. The picture from a renderer made for this alone (dvbridge_renderer_create_hdr10()), with
  // the GL state of a caller around it: saved before, put back after, and looked at with queries of this
  // test's own. Then that picture copied into the default framebuffer, which is what a window is: first row at
  // the bottom, limited range, whole and at the place of a picture between two bars.
  {
    // CDVBridgeGLES::PrepareConversion(): the layers are imported, the picture is reconstructed and the
    // layers are released, all of it between the state saved and put back.
    struct dvbridge_gl_state plain, kept;
    int before[STATE_VALUES], between[STATE_VALUES], after[STATE_VALUES];
    static int wideBefore[WIDE_MAX], wideBetween[WIDE_MAX], wideAfter[WIDE_MAX];
    dvbridge_gl_state_save(&plain);
    OddState(g);
    OddWideState(g);
    StateProbe(before);
    const int wide = WideProbe(wideBefore);
    dvbridge_gl_state_save(&kept);
    struct dvbridge_gl_frame b = {0}, e = {0};
    int drawn = dvbridge_gl_frame_import(&b, g->gl10->gpu, DVBRIDGE_P010, bl->w, bl->h, bl->planes IMPORT_CHROMA(bl));
    const int layered = drawn && el && dvbridge_gl_frame_import(&e, g->gl10->gpu, DVBRIDGE_P010, el->w, el->h,
                                                                el->planes IMPORT_CHROMA(el));
    if (layered)
      b.frame.enhancement_layer = &e.frame;
    drawn = drawn && (!el || layered) &&
            dvbridge_render_hdr10(g->renderer10, &b.frame, meta, bytes, pts, elPts, whole, W, H);
    if (layered)
      dvbridge_gl_frame_release(&e);
    dvbridge_gl_frame_release(&b);
    StateProbe(between);
    WideProbe(wideBetween);
    dvbridge_gl_state_restore(&kept);
    StateProbe(after);
    WideProbe(wideAfter);
    PlainWideState();
    dvbridge_gl_state_restore(&plain);
    long changed = 0, left = 0, wideChanged = 0, wideLeft = 0;
    for (int i = 0; i < STATE_VALUES; ++i)
    {
      changed += between[i] != before[i];
      if (after[i] != before[i])
      {
        ++left;
        printf("%s: GL state value %d was %d and is %d after the render\n", label, i, before[i], after[i]);
      }
    }
    for (int i = 0; i < wide; ++i)
    {
      wideChanged += wideBetween[i] != wideBefore[i];
      if (wideAfter[i] != wideBefore[i])
      {
        ++wideLeft;
        if (i < WIDE_UNITS * 5)
          printf("%s: of the wider GL state, value %d of texture unit %d was %d and is %d after the render\n", label,
                 i % 5, i / 5, wideBefore[i], wideAfter[i]);
        else
          printf("%s: of the wider GL state, value %d was %d and is %d after the render\n", label, i, wideBefore[i],
                 wideAfter[i]);
      }
    }
    ++g_stateRuns;
    g_stateChanged += changed;
    g_stateLeft += left;
    g_wideValues = wide;
    g_wideChanged += wideChanged;
    g_wideLeft += wideLeft;
    pl_tex own = drawn ? dvbridge_render_hdr10_texture(g->renderer10) : NULL;
    const GLuint ownId = own ? pl_opengl_unwrap(g->gl10->gpu, own, NULL, NULL, NULL) : 0;
    const int same = ownId && ReadFloats(ownId, g->floatsPlain) && !memcmp(g->hdr10, g->floatsPlain, (size_t)W * H * 16);
    ++g_ownDrawn;
    g_ownSame += same;
    printf("%s: from a renderer made for HDR10 alone, in a caller's GL state: %s, %s; %ld of %d state values "
           "changed by the render, %ld not put back; of %d more, %ld changed and %ld not put back\n", label,
           drawn ? "drawn" : "not drawn", same ? "the same picture" : "another picture", changed, STATE_VALUES, left,
           wide, wideChanged, wideLeft);
    const int levels = (1 << g->windowBits) - 1;
    for (int part = 0; part < 2 && ownId; ++part)
    {
      const int ry = part ? 276 : 0, rh = part ? H - 2 * 276 : H;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDisable(GL_SCISSOR_TEST);
      glClearColor(0.5f, 0.5f, 0.5f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      const int copied = dvbridge_gl_copy(g->copier, ownId, 0, W, H, 0, ry, W, rh, 1, 1);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glReadPixels(0, 0, W, H, GL_RGBA, g->windowBits == 10 ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE,
                   g->copy);
      const int read = glGetError() == GL_NO_ERROR;
      long wrong = 0, outsideRect = 0;
      if (copied && read)
      {
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x)
          {
            const uint32_t word = g->copy[(size_t)(H - 1 - y) * W + x];
            const float* v = g->hdr10 + ((size_t)y * W + x) * 4;
            for (int c = 0; c < 3; ++c)
            {
              const unsigned code = g->windowBits == 10 ? word >> (10 * c) & 1023 : word >> (8 * c) & 255;
              if (y < ry || y >= ry + rh)
              {
                outsideRect += code != (unsigned)(levels / 2) && code != (unsigned)(levels / 2 + 1);
                continue;
              }
              const double want = ((double)v[c] * 876 + 64) / 1023 * levels;
              // (Hades: into 8 bits the copy dithers, dvbridge_gl_pack.c: the level below the value or
              // the one above it; the nearest at 10 bits)
              wrong += fabs((double)code - want) > (g->windowBits == 8 ? 0.9995 : 0.5005);
            }
          }
      }
      else
        wrong = -1;
      ++g_windowCopies;
      g_windowWrong += wrong < 0 ? 1 : wrong + outsideRect;
      printf("%s: %s copied into the %d-bit default framebuffer, first row at the bottom, limited range: %s, %ld "
             "values that are not the nearest code, %ld written outside the place of the copy\n", label,
             part ? "the picture between two bars" : "the picture", g->windowBits,
             copied && read ? "ok" : "failed", wrong, outsideRect);
    }
  }
#endif
  // ... and to a half-float framebuffer: the values, to what its format holds (1/2048 between 0.5 and 1)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbo16);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const int copied = dvbridge_gl_copy(g->copier, texture, g->fbo16, W, H, 0, 0, W, H, 0, 0) &&
                       ReadFloats(g->color16, g->floatsPlain);
    double most = -1;
    if (copied)
    {
      most = 0;
      for (size_t i = 0; i < (size_t)W * H * 4; ++i)
        if (i % 4 != 3 && fabs((double)g->hdr10[i] - g->floatsPlain[i]) > most)
          most = fabs((double)g->hdr10[i] - g->floatsPlain[i]);
    }
    ++g_floatCopies;
    if (most < 0 || most > g_floatCopyMost)
      g_floatCopyMost = most < 0 ? 1 : most;
    printf("%s: copied to a half-float framebuffer: %s, largest difference %.7f\n", label,
           copied ? "ok" : "failed", most);
  }

  // the left half of the source drawn alone, in the left half of the target: the same as in the whole
  // picture, away from the edge that the filter reaches across
  const struct dvbridge_view part = {0, 0, (float)(bl->w / 2), (float)bl->h, 0, 0, W / 2, H};
  if (dvbridge_render_hdr10(g->renderer, source, meta, bytes, pts, elPts, part, W, H) && Hdr10Texture(g) &&
      ReadFloats(Hdr10Texture(g), g->floatsPlain))
  {
    double partMost = 0;
    long differing = 0;
    int px = 0, py = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W / 2 - 32; ++x)
        for (int c = 0; c < 3; ++c)
        {
          const size_t i = ((size_t)y * W + x) * 4 + c;
          const double d = fabs((double)g->hdr10[i] - g->floatsPlain[i]) * 1023;
          differing += d > 0.01;
          if (d > partMost)
          {
            partMost = d;
            px = x;
            py = y;
          }
        }
    ++g_parts;
    if (partMost > g_partMost)
      g_partMost = partMost;
    printf("%s: the left half of the source drawn alone: largest difference to the whole picture %.4f codes of "
           "1023 (at %d,%d), %ld values more than a hundredth of a code away\n", label, partMost, px, py, differing);
  }
  else
  {
    printf("%s: a part of the source was not drawn for output as HDR10\n", label);
    ++g_hdr10Failed;
  }

  // A frame whose metadata is refused: it is not drawn, and the picture before it is still there, whole.
  const GLuint before = Hdr10Texture(g);
  const int drawn = dvbridge_render_hdr10(g->renderer, source, NULL, 0, pts, elPts, whole, W, H);
  pl_tex last = dvbridge_render_hdr10_last(g->renderer);
  const GLuint kept = last ? pl_opengl_unwrap(g->gl->gpu, last, NULL, NULL, NULL) : 0;
  const int same = !drawn && !Hdr10Texture(g) && kept && kept == before && ReadFloats(kept, g->hdr10) &&
                   !memcmp(g->hdr10, g->floatsPlain, (size_t)W * H * 16);
  ++g_refused;
  g_refusedKept += same;
  printf("%s: a frame without metadata: %s, the picture before it %s\n", label, drawn ? "drawn" : "not drawn",
         same ? "is kept" : "is not kept");
}
#endif

#ifdef DVBRIDGE_GL_PACKER_QUAD
static long g_quadChecks, g_quadPlainDiff, g_quadStrictDiff;

// the fragment shader with `from` replaced by `to`, which has to be there `times` times
static const char* Edited(const char* src, const char* from, const char* to, int times)
{
  static char out[2][65536];
  static int which;
  char* o = out[which++ & 1];
  size_t n = 0;
  int found = 0;
  for (const char* s = src; *s;)
  {
    if (!strncmp(s, from, strlen(from)))
    {
      memcpy(o + n, to, strlen(to));
      n += strlen(to);
      s += strlen(from);
      ++found;
    }
    else
      o[n++] = *s++;
    if (n > sizeof(out[0]) - 256)
      abort();
  }
  o[n] = 0;
  if (found != times)
  {
    printf("FAIL: the pack shader has '%s' %d times, not %d\n", from, found, times);
    abort();
  }
  return o;
}

static long Differing(const uint8_t* a, const uint8_t* b)
{
  long d = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    d += a[i] != b[i];
  return d;
}

// The composed picture packed by the two other packers: the same bytes as by the packer of Kodi
static void QuadCheck(struct gl* g, GLuint id, const uint32_t* packets, unsigned count)
{
  ++g_quadChecks;
  if (!dvbridge_gl_pack(g->plainPacker, id, g->fbo, W, H, packets, count, true) || !Read(g, g->plain))
    ++g_quadPlainDiff;
  else
    g_quadPlainDiff += Differing(g->composed, g->plain);
  if (!dvbridge_gl_pack(g->strictPacker, id, g->fbo, W, H, packets, count, true) || !Read(g, g->plain))
    ++g_quadStrictDiff;
  else
    g_quadStrictDiff += Differing(g->composed, g->plain);
}
#endif

// Draws one picture both ways with the given metadata, and each way once more without the early hook when
// it was used. Returns 0, -1 (an output is missing) or -3 (the renderer refuses this metadata either way).
static int Draw(struct gl* g, const struct layer* bl, const struct layer* el, const AVDOVIMetadata* meta,
                size_t bytes, double pts, const char* label, int commit)
{
  struct dvbridge_geometry geometry = {bl->w, bl->h, 0, 0, W, H};
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!dvbridge_gl_frame_import(&b, g->gl->gpu, DVBRIDGE_P010, bl->w, bl->h, bl->planes IMPORT_CHROMA(bl)))
  {
    printf("%s: base layer not imported\n", label);
    return -1;
  }
  double elPts = NAN;
  if (el)
  {
    if (!dvbridge_gl_frame_import(&e, g->gl->gpu, DVBRIDGE_P010, el->w, el->h, el->planes IMPORT_CHROMA(el)))
    {
      printf("%s: enhancement layer not imported\n", label);
      dvbridge_gl_frame_release(&b);
      return -1;
    }
    b.frame.enhancement_layer = &e.frame;
    elPts = pts;
  }
  long differing = -1;
  unsigned margins[4] = {0}, count = 0;
  uint64_t hc = 0, hd = 0, hf = 0;
  int composed = 0, direct = 0, early[2] = {0, 0}, omitted = 0;

  // --- composed
  Fill(g, 0.5f);
  // A GL error is left pending, as unrelated GL work before a render may leave one: the render must not
  // fail on it (the first GL object that libplacebo makes would), nor count it against the early hook.
  glEnable(0x7fff);
  if (dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry))
  {
    early[0] = dvbridge_render_early_mask(g->renderer, false);
    omitted = dvbridge_render_residual_omitted(g->renderer);
    const struct dvbridge_candidate* candidate = dvbridge_render_candidate(g->renderer);
    const uint32_t* packets = dvbridge_packets(candidate, &count);
    dvbridge_active_area(candidate, margins);
    const GLuint id = Texture(g);
    const int floats = id && ReadFloats(id, g->floats);
    composed = floats && dvbridge_gl_pack(g->packer, id, g->fbo, W, H, packets, count, true) &&
               Read(g, g->composed);
#ifdef DVBRIDGE_GL_PACKER_QUAD
    if (composed)
      QuadCheck(g, id, packets, count);
#endif
    if (floats)
    {
      hf = Hash((const uint8_t*)g->floats, (size_t)W * H * 16);
      float lo = g->floats[0], hi = g->floats[0];
      long outside = 0, strange = 0, alphaOther = 0;
      for (size_t i = 0; i < (size_t)W * H * 4; ++i)
      {
        const float v = g->floats[i];
        if (!(v == v) || v - v != 0)
        {
          ++strange; // NaN or infinite
          continue;
        }
        // alpha is PICTURE_ALPHA, 2, where nothing is drawn over the picture
        // (dvbridge_render.c, since kodi-9302): exactly that, nothing being drawn over it here; the
        // colour, R, G and B, within [0, 1] (K24: the bridge clamps it)
        if (i % 4 == 3)
        {
          alphaOther += v != 2.0f;
          continue;
        }
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        outside += v < 0 || v > 1;
      }
      if (outside || strange || alphaOther)
        printf("%s: float picture: colour %.9g .. %.9g, outside [0, 1]: %ld, not finite: %ld, alpha not 2: %ld\n",
               label, lo, hi, outside, strange, alphaOther);
      g_outside += outside;
      g_notFinite += strange;
      g_alphaOther += alphaOther;
#ifdef DVBRIDGE_RENDER_HDR10
      // the same picture for output as HDR10; its copy and a part of it, once for each picture of the stream
      Convert(g, &b.frame, bl, el, meta, bytes, pts, elPts, margins, label, !commit && !strstr(label, "bars"));
#endif
    }
    if (composed)
      hc = Hash(g->composed, (size_t)W * H * 4);
    if (composed && early[0] == 1)
    {
      // once more without the early hook: the same floats
      dvbridge_render_allow_early_mask(g->renderer, false);
      const int plain = dvbridge_render_rgb(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry) &&
                        Texture(g) && ReadFloats(Texture(g), g->floatsPlain);
      dvbridge_render_allow_early_mask(g->renderer, true);
      long d = -1;
      if (plain)
        d = memcmp(g->floats, g->floatsPlain, (size_t)W * H * 16) ? 0 : -2;
      if (d == 0)
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
          d += memcmp(g->floats + i, g->floatsPlain + i, 4) != 0;
      if (d != -2)
      {
        printf("%s: composed, with and without the early hook: %ld differing values\n", label, d);
        g_earlyDiffering[0] += d < 0 ? 1 : d;
      }
    }
  }

  // --- direct
  if (g->subgroups)
  {
    Fill(g, 0.25f);
    glEnable(0x7fff); // a pending GL error, as above
    direct = dvbridge_render_packed(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry, g->scanout, true) &&
             Read(g, g->direct);
    early[1] = dvbridge_render_early_mask(g->renderer, true);
    if (direct)
      hd = Hash(g->direct, (size_t)W * H * 4);
    if (direct && early[1] == 1)
    {
      // once more without the early hook: the same bytes
      dvbridge_render_allow_early_mask(g->renderer, false);
      Fill(g, 0.75f);
      const int plain = dvbridge_render_packed(g->renderer, &b.frame, meta, bytes, pts, elPts, geometry,
                                               g->scanout, true) && Read(g, g->plain);
      dvbridge_render_allow_early_mask(g->renderer, true);
      long d = -1;
      if (plain)
      {
        d = 0;
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
          d += g->direct[i] != g->plain[i];
      }
      if (d)
      {
        printf("%s: direct, with and without the early hook: %ld differing bytes\n", label, d);
        g_earlyDiffering[1] += d < 0 ? 1 : d;
      }
    }
  }

  ++g_drawings;
  const int hasMargins = margins[0] || margins[1] || margins[2] || margins[3];
  g_withMargins += hasMargins && (composed || direct);
  for (int way = 0; way < 2; ++way)
  {
    g_earlyUsed[way] += early[way] == 1;
    g_earlyGivenUp[way] += early[way] == 2;
  }

  // --- the two ways against each other
  if (composed && direct)
  {
    differing = 0;
    long first = -1;
    for (size_t i = 0; i < (size_t)W * H * 4; ++i)
      if (g->composed[i] != g->direct[i])
      {
        if (first < 0)
          first = (long)i;
        ++differing;
      }
    if (!differing)
      ++g_identical;
    else
    {
      // In the 12-bit luma and chroma codes the bytes carry: how many pixels, by how much, where.
      long pixels = 0, luma = 0, chroma = 0, inBars = 0, inMeta = 0, maxY = 0, maxC = 0, alpha = 0, bits = 0;
      int x0 = W, x1 = -1, y0 = H, y1 = -1;
      // y is the row of the HDMI raster: the output was written flipped, and glReadPixels gives the rows
      // of the framebuffer from the bottom
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
          const uint8_t* a = g->composed + ((size_t)(H - 1 - y) * W + x) * 4;
          const uint8_t* d = g->direct + ((size_t)(H - 1 - y) * W + x) * 4;
          if (!memcmp(a, d, 4))
            continue;
          ++pixels;
          const int ya = a[1] << 4 | (a[2] & 15), yd = d[1] << 4 | (d[2] & 15);
          const int ca = a[0] << 4 | a[2] >> 4, cd = d[0] << 4 | d[2] >> 4;
          luma += ya != yd;
          alpha += a[3] != d[3];
          // A pixel of the first 3072 * count positions carries a metadata bit: the lowest bit of its
          // chroma code is that bit xor the parity of the rest. There the bit read back has to be the
          // same; the lowest bit of the code follows the parity and says nothing more.
          if ((size_t)y * W + x < (size_t)3072 * count)
          {
            ++inMeta;
            bits += (__builtin_parity((unsigned)ca) ^ __builtin_parity((unsigned)ya)) !=
                    (__builtin_parity((unsigned)cd) ^ __builtin_parity((unsigned)yd));
            if (labs((ca >> 1) - (cd >> 1)) > maxC) maxC = labs((ca >> 1) - (cd >> 1));
          }
          else
          {
            chroma += ca != cd;
            if (labs(ca - cd) > maxC) maxC = labs(ca - cd);
          }
          if (labs(ya - yd) > maxY) maxY = labs(ya - yd);
          // outside the active picture counts only when the whole pair of pixels is outside: a pair
          // that the edge divides has the chroma of both
          const int ex = x & ~1;
          inBars += ex + 1 < (int)margins[0] || ex >= W - (int)margins[1] || y < (int)margins[2] ||
                    y >= H - (int)margins[3];
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
      printf("%s: the two ways: differing pixels %ld (luma %ld, chroma %ld, alpha %ld), largest difference "
             "luma %ld chroma %ld codes of 4096; in the bars %ld, in the metadata positions %ld (other bit read "
             "back: %ld); box x %d..%d y %d..%d\n",
             label, pixels, luma, chroma, alpha, maxY, maxC, inBars, inMeta, bits, x0, x1, y0, y1);
      g_bits += bits;
      if (pixels > g_mostPixels) g_mostPixels = pixels;
      if (maxY > g_mostCode) g_mostCode = maxY;
      if (maxC > g_mostCode) g_mostCode = maxC;
      g_inBars += inBars;
      g_alpha += alpha;
    }
  }
  printf("%s: margins %u %u %u %u packets %u layer %s%s | float %016llx | composed %s %016llx early %d | "
         "direct %s %016llx early %d | differing bytes %ld\n",
         label, margins[0], margins[1], margins[2], margins[3], count, el ? "yes" : "no",
         omitted ? " (residual omitted)" : "", (unsigned long long)hf, composed ? "ok" : "--",
         (unsigned long long)hc, early[0], direct ? "ok" : "--", (unsigned long long)hd, early[1], differing);
  if (commit)
  {
    // the transport state advances as after a presented frame (the last candidate prepared)
    if (!dvbridge_render_commit(g->renderer))
      printf("%s: commit refused\n", label);
  }
  dvbridge_gl_frame_release(&e);
  dvbridge_gl_frame_release(&b);
  if (!composed && !direct)
    return -3;
  return composed && (direct || !g->subgroups) ? 0 : -1;
}

int main(int argc, char** argv)
{
  if (argc != 5 && argc != 6)
    return 2;
  const unsigned first = (unsigned)atoi(argv[2]), step = (unsigned)atoi(argv[3]), wanted = (unsigned)atoi(argv[4]);
  g_half = argc == 6 && !strcmp(argv[5], "half");
  av_log_set_level(AV_LOG_ERROR);

  // --- a surfaceless OpenGL ES context
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  // The default framebuffer stands for a window: 3840x2160, of 10 bits when this OpenGL ES has such a
  // configuration, else of 8.
  EGLConfig cfg = NULL;
  int windowBits = 0;
  {
    EGLint deepAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                         EGL_RED_SIZE, 10, EGL_GREEN_SIZE, 10, EGL_BLUE_SIZE, 10, EGL_NONE};
    EGLConfig found[64];
    EGLint count = 0;
    if (eglChooseConfig(dpy, deepAttr, found, 64, &count))
      for (EGLint i = 0; i < count && !windowBits; ++i)
      {
        EGLint red = 0, green = 0, blue = 0, samples = 0;
        eglGetConfigAttrib(dpy, found[i], EGL_RED_SIZE, &red);
        eglGetConfigAttrib(dpy, found[i], EGL_GREEN_SIZE, &green);
        eglGetConfigAttrib(dpy, found[i], EGL_BLUE_SIZE, &blue);
        eglGetConfigAttrib(dpy, found[i], EGL_SAMPLES, &samples);
        if (red == 10 && green == 10 && blue == 10 && samples <= 1)
        {
          cfg = found[i];
          windowBits = 10;
        }
      }
  }
  if (!windowBits)
  {
    EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
      return 1;
    windowBits = 8;
  }
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s; the default framebuffer has %d bits\n", glGetString(GL_RENDERER),
         glGetString(GL_VERSION), windowBits);

  struct gl g = {0};
  g.windowBits = windowBits;
  g.log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  g.gl = pl_opengl_create(g.log, pl_opengl_params(
      .get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
      .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  Check(g.gl != NULL, "libplacebo takes the OpenGL ES context");
  if (!g.gl)
    return 1;
  Check(pl_opengl_has_ext(g.gl, "GL_EXT_float_blend") && pl_opengl_has_ext(g.gl, "GL_OES_texture_float_linear"),
        "float blending and linear filtering of float textures, as Kodi demands");
  g.renderer = dvbridge_renderer_create(g.gl->gpu, g.log);
  CcmSwitch(g.renderer, "the renderer");
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
#ifdef DVBRIDGE_GL_PACKER_QUAD
  // (the form with quad operations does not compile with the first, and the packer makes it as written)
  g.plainPacker = dvbridge_gl_packer_create(
      PACK_VERTEX, Edited(PACK_FRAGMENT, "precision highp float;\n",
                          "#ifdef DVBRIDGE_PACK_QUAD\n#error the reference has two fetches\n#endif\nprecision highp float;\n", 1));
  g.strictPacker = dvbridge_gl_packer_create(
      PACK_VERTEX, Edited(PACK_FRAGMENT, "            e = clamp(picture_alone(pe), 0.0, 1.0);",
                          "            e = vec3(0.5, -1.0, 2.0);", 1));
  printf("packers: Kodi's %s; to check it against: %s, %s\n",
         dvbridge_gl_packer_quad(g.packer) ? "with quad operations" : "with two fetches",
         g.plainPacker && !dvbridge_gl_packer_quad(g.plainPacker) ? "two fetches" : "?",
         g.strictPacker && dvbridge_gl_packer_quad(g.strictPacker) ? "quad operations without the fetch" : "?");
  Check(g.plainPacker && !dvbridge_gl_packer_quad(g.plainPacker) &&
        (!dvbridge_gl_packer_quad(g.packer) || (g.strictPacker && dvbridge_gl_packer_quad(g.strictPacker))),
        "the packers that the packing is checked against are made");
#endif
  Check(g.renderer && g.packer, "the renderer and the packer are created");
  if (!g.renderer || !g.packer)
    return 1;
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g.scanout = pl_opengl_wrap(g.gl->gpu, pl_opengl_wrap_params(
      .texture = g.color, .target = GL_TEXTURE_2D, .iformat = GL_RGBA8, .width = W, .height = H));
  Check(complete && g.scanout, "an RGBA8 3840x2160 output");
  g.composed = malloc((size_t)W * H * 4);
  g.direct = malloc((size_t)W * H * 4);
  g.plain = malloc((size_t)W * H * 4);
  g.floats = malloc((size_t)W * H * 16);
  g.floatsPlain = malloc((size_t)W * H * 16);
  if (!complete || !g.scanout || !g.composed || !g.direct || !g.plain || !g.floats || !g.floatsPlain)
    return 1;
#ifdef DVBRIDGE_RENDER_HDR10
  g.hdr10 = malloc((size_t)W * H * 16);
  g.copy = malloc((size_t)W * H * 4);
  glGenTextures(1, &g.color10);
  glBindTexture(GL_TEXTURE_2D, g.color10);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g.fbo10);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fbo10);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color10, 0);
  const int complete10 = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glGenTextures(1, &g.color16);
  glBindTexture(GL_TEXTURE_2D, g.color16);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g.fbo16);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fbo16);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color16, 0);
  const int complete16 = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g.copier = dvbridge_gl_copier_create();
  Check(complete10 && complete16 && g.hdr10 && g.copy && g.copier,
        "a 10-bit and a half-float 3840x2160 framebuffer, and the copy to them, for the picture as HDR10");
  if (!complete10 || !complete16 || !g.hdr10 || !g.copy || !g.copier)
    return 1;
#ifdef DVBRIDGE_GL_STATE_UNITS
  {
    // a renderer for the picture as HDR10 alone, and GL objects of a caller: a program, a vertex array, a
    // buffer and four textures
    static const char* const vertex = "#version 300 es\nvoid main() { gl_Position = vec4(0.0); }\n";
    static const char* const fragment =
        "#version 300 es\nprecision mediump float;\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n";
    const GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vertex, NULL);
    glCompileShader(vs);
    glShaderSource(fs, 1, &fragment, NULL);
    glCompileShader(fs);
    g.oddProgram = glCreateProgram();
    glAttachShader(g.oddProgram, vs);
    glAttachShader(g.oddProgram, fs);
    glLinkProgram(g.oddProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(g.oddProgram, GL_LINK_STATUS, &linked);
    glGenVertexArrays(1, &g.oddVao);
    glGenBuffers(1, &g.oddBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, g.oddBuffer);
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenTextures(4, g.oddTextures);
    for (int i = 0; i < 4; ++i)
    {
      glBindTexture(GL_TEXTURE_2D, g.oddTextures[i]);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    const int objects = linked && glGetError() == GL_NO_ERROR;
    // CDVBridgeGLES::InitializeConversion(): a pl_opengl of its own on the context, the renderer for the
    // picture as HDR10 alone and a copier, made between the state of a caller saved and put back.
    struct dvbridge_gl_state plain, kept;
    int before[STATE_VALUES], between[STATE_VALUES], after[STATE_VALUES];
    static int wideBefore[WIDE_MAX], wideBetween[WIDE_MAX], wideAfter[WIDE_MAX];
    dvbridge_gl_state_save(&plain);
    OddState(&g);
    OddWideState(&g);
    StateProbe(before);
    const int wide = WideProbe(wideBefore);
    dvbridge_gl_state_save(&kept);
    g.gl10 = pl_opengl_create(g.log, pl_opengl_params(
        .get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
    g.renderer10 = g.gl10 ? dvbridge_renderer_create_hdr10(g.gl10->gpu, g.log) : NULL;
    if (g.renderer10)
      CcmSwitch(g.renderer10, "the renderer for HDR10");
    struct dvbridge_gl_copier* copier10 = dvbridge_gl_copier_create();
    StateProbe(between);
    WideProbe(wideBetween);
    dvbridge_gl_state_restore(&kept);
    StateProbe(after);
    WideProbe(wideAfter);
    PlainWideState();
    dvbridge_gl_state_restore(&plain);
    for (int i = 0; i < STATE_VALUES; ++i)
    {
      g_setupChanged += between[i] != before[i];
      if (after[i] != before[i])
      {
        ++g_setupLeft;
        printf("set-up: GL state value %d was %d and is %d after it\n", i, before[i], after[i]);
      }
    }
    for (int i = 0; i < wide; ++i)
    {
      g_setupChanged += wideBetween[i] != wideBefore[i];
      if (wideAfter[i] != wideBefore[i])
      {
        ++g_setupLeft;
        printf("set-up: of the wider GL state, value %d was %d and is %d after it\n", i, wideBefore[i], wideAfter[i]);
      }
    }
    g_setupValues = STATE_VALUES + wide;
    const int made = objects && g.gl10 && g.renderer10 && copier10 && glGetError() == GL_NO_ERROR;
    dvbridge_gl_copier_destroy(copier10);
    printf("set-up for output as HDR10 in a caller's GL state: %ld of %ld state values changed by it, %ld not put "
           "back\n", g_setupChanged, g_setupValues, g_setupLeft);
    Check(made, "a second pl_opengl with a renderer for the picture as HDR10 alone, and the GL objects of a caller");
    if (!made)
      return 1;
  }
#endif
#endif
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
    printf("shader subgroups for the direct way: %s (stages 0x%x)\n", g.subgroups ? "yes" : "no (composed only)",
           (unsigned)stages);
  }

  // --- the stream
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (index < 0)
    return 1;
  AVStream* stream = fc->streams[index];
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  if (!codec || !base || avcodec_parameters_to_context(base, stream->codecpar) < 0)
    return 1;
  base->pkt_timebase = stream->time_base;
  base->thread_count = 16;
  base->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 1;
  struct dvbridge_fel* fel = dvbridge_fel_create(stream->codecpar, stream->time_base);
  if (!fel)
    return 1;
  fel->decoder->get_format = avcodec_default_get_format;
  fel->decoder->thread_count = 16;
  fel->decoder->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(fel->decoder, codec, NULL) < 0)
    return 1;
  fel->opened = true;

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* pending[64];
  unsigned numPending = 0, pictures = 0, drawn = 0;
  struct layer bl = {0}, el = {0};
  int end = 0, ok = 1, incomplete = 0;
  static const int MARGINS[][4] = {{0, 0, 276, 276}, {240, 240, 0, 0}, {241, 239, 0, 0}, {3, 5, 7, 9}};

  while (!end && ok && drawn < wanted)
  {
    const int got = av_read_frame(fc, packet);
    if (got >= 0 && packet->stream_index != index)
    {
      av_packet_unref(packet);
      continue;
    }
    end = got < 0;
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
      if (numPending == 64)
      {
        ok = 0;
        break;
      }
      pending[numPending++] = av_frame_clone(frame);
      av_frame_unref(frame);
      while (numPending && drawn < wanted)
      {
        AVFrame* b = pending[0];
        AVFrame* layer = NULL;
        const int result = dvbridge_fel_take(fel, b->pts, &layer);
        if (result == 0 && !end)
          break;
        const unsigned number = pictures++;
        if (number >= first && (number - first) % step == 0)
        {
          // the metadata of the picture: the one of its enhancement picture when that brings its own
          const AVFrame* from = b;
          if (layer && av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_RPU_BUFFER) &&
              av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_METADATA))
            from = layer;
          const AVFrameSideData* sd = av_frame_get_side_data(from, AV_FRAME_DATA_DOVI_METADATA);
          const double pts = b->pts * av_q2d(stream->time_base);
          const int scale = g_half ? 2 : 1;
          char label[64];
          if (sd && Upload(&bl, b, scale) && (!layer || Upload(&el, layer, scale)))
          {
            // Kodi gives the base layer the decoder's chroma_sample_location (VideoPicture::chroma_position)
            ++g_locPictures;
            g_locMismatch += bl.loc != base->chroma_sample_location;
            if (g_locPictures == 1)
              printf("chroma locations: base layer %s (the decoder's: %s), enhancement layer %s\n",
                     av_chroma_location_name(bl.loc), av_chroma_location_name(base->chroma_sample_location),
                     layer ? av_chroma_location_name(el.loc) : "none");
#ifdef DVBRIDGE_RENDER_HDR10
            if (!drawn)
              Static((const AVDOVIMetadata*)sd->data, sd->size);
#endif
            snprintf(label, sizeof(label), "picture %u", number);
            if (Draw(&g, &bl, layer ? &el : NULL, (const AVDOVIMetadata*)sd->data, sd->size, pts, label, 0))
              incomplete = 1;
            for (unsigned m = 0; m < sizeof(MARGINS) / sizeof(*MARGINS); ++m)
            {
              size_t bytes;
              AVDOVIMetadata* edited = WithMargins((const AVDOVIMetadata*)sd->data, sd->size, MARGINS[m], &bytes);
              if (!edited)
              {
                printf("%s: the metadata could not be given margins\n", label);
                incomplete = 1;
                continue;
              }
              snprintf(label, sizeof(label), "picture %u bars %u", number, m);
              if (Draw(&g, &bl, layer ? &el : NULL, edited, bytes, pts, label, 0))
                incomplete = 1;
              av_free(edited);
            }
            // once more as authored, committed: the next picture drawn starts from this transport state
            snprintf(label, sizeof(label), "picture %u committed", number);
            if (Draw(&g, &bl, layer ? &el : NULL, (const AVDOVIMetadata*)sd->data, sd->size, pts, label, 1))
              incomplete = 1;
            ++drawn;
          }
          else
          {
            printf("picture %u: no metadata or no upload\n", number);
            incomplete = 1;
          }
          fflush(stdout);
        }
        av_frame_free(&layer);
        av_frame_free(&b);
        memmove(pending, pending + 1, --numPending * sizeof(*pending));
      }
    }
  }
  Check(ok, "the stream was decoded and paired");
  Check(drawn == wanted, "every picture asked for was drawn");
  Check(!incomplete, "every drawing gave an output");
  Check(g_outside == 0 && g_notFinite == 0, "the colour of the float picture holds only values within [0, 1]");
  Check(g_alphaOther == 0, "its alpha is 2 (PICTURE_ALPHA) on every pixel: nothing is drawn over it here");
#ifdef DVBRIDGE_GL_FRAME_CHROMA
  Check(g_locPictures > 0 && !g_locMismatch,
        "every picture was imported with the chroma location that Kodi gives it (the base layer's is the decoder's)");
#endif
  printf("pictures drawn %u, drawings %ld (with margins %ld); early hook: composed way used %ld, given up %ld; "
         "direct way used %ld, given up %ld\n",
         drawn, g_drawings, g_withMargins, g_earlyUsed[0], g_earlyGivenUp[0], g_earlyUsed[1], g_earlyGivenUp[1]);
  if (g_half)
    Check(!g_earlyUsed[0] && !g_earlyUsed[1] && !g_earlyGivenUp[0] && !g_earlyGivenUp[1],
          "a picture that is scaled is never drawn with the early hook");
  else
  {
    Check(g_withMargins > 0 && g_earlyUsed[0] == g_withMargins && !g_earlyGivenUp[0],
          "composed way: every picture with margins was drawn with the early hook, which never failed");
    Check(g_earlyDiffering[0] == 0, "composed way: the same float picture with and without the early hook");
  }
#ifdef DVBRIDGE_RENDER_HDR10
  printf("for output as HDR10: %ld drawings, %ld failed; against the picture that the composed way packs, inside "
         "the active picture: largest difference %.4f codes of 1023, %ld values over half a code; %ld copies to a "
         "10-bit framebuffer, values that are not the nearest code %ld and %ld; %ld parts of a source, largest "
         "difference to the whole picture %.4f codes\n",
         g_hdr10Drawn, g_hdr10Failed, g_hdr10Most, g_hdr10Over, g_copies, g_copyWrong[0], g_copyWrong[1], g_parts,
         g_partMost);
  printf("copies to a half-float framebuffer: %ld, largest difference %.7f\n", g_floatCopies, g_floatCopyMost);
  Check(g_hdr10Drawn == g_drawings && !g_hdr10Failed, "every picture was also drawn for output as HDR10");
  Check(g_hdr10Most <= 0.01 && !g_hdr10Over,
        "... and is the picture that the transport is packed from (within a hundredth of a code of 1023)");
  Check(!g_hdr10Outside && !g_hdr10NotFinite, "... within [0, 1]");
  Check(g_copies > 0 && !g_copyWrong[0] && !g_copyWrong[1],
        "a 10-bit framebuffer gets the nearest code of every value, with its first row at the top or at the bottom, "
        "whole or in part, in the full or the limited range, and nothing outside the place of the copy, whatever "
        "scissor rectangle was set");
  Check(g_refused > 0 && g_refusedKept == g_refused,
        "a frame whose metadata is refused is not drawn, and the picture before it is still there for its place");
  Check(g_floatCopies > 0 && g_floatCopyMost <= 0.0005,
        "a half-float framebuffer gets the values, to the precision of its format");
  // Not the same values to the last digit: the filter that makes full-size chroma takes its places from the
  // part that is drawn, and they come out a little different. A layer or a plane out of place would be
  // whole codes off.
  Check(g_parts > 0 && g_partMost <= 1.0,
        "a part of the source drawn alone is that part of the whole picture, to within a code of 1023");
  Check(g_staticRuns == 1 && g_staticOk == 1,
        "what an HDR10 output is told of the picture: the level 6 block as it is, with light levels above the peak "
        "of the mastering display or one of the two not known, and without one the range of the source; every "
        "value fits its 16 bits");
#ifdef DVBRIDGE_GL_STATE_UNITS
  printf("as Kodi draws it: %ld pictures from a renderer made for HDR10 alone, %ld the same; %ld copies into the "
         "default framebuffer, %ld values wrong; %ld of %ld state values changed by the renders, %ld not put back\n",
         g_ownDrawn, g_ownSame, g_windowCopies, g_windowWrong, g_stateChanged, g_stateRuns * STATE_VALUES, g_stateLeft);
  Check(g_ownDrawn > 0 && g_ownSame == g_ownDrawn,
        "a renderer made for HDR10 alone, without the picture of the transport, gives the same picture");
  Check(g_windowCopies > 0 && !g_windowWrong,
        "the default framebuffer, which is what a window is, gets the nearest code of every value with its first "
        "row at the bottom and in the limited range, whole and between two bars");
  Check(g_stateRuns > 0 && g_stateChanged > 0 && !g_stateLeft,
        "the GL state that a caller draws on with is changed by the render and put back whole: program, vertex "
        "array, buffer, framebuffers, viewport, scissor, blending, the textures and samplers of the first units");
  printf("the wider look at the GL state: of %ld values, %ld changed by the renders and %ld not put back; the "
         "set-up changed %ld of %ld and left %ld\n", g_wideValues, g_wideChanged, g_wideLeft, g_setupChanged,
         g_setupValues, g_setupLeft);
  Check(g_wideValues > 250 && g_setupChanged > 0 && !g_wideLeft && !g_setupLeft,
        "and nothing else of it is left changed, by the set-up or by a picture with its layers imported and "
        "released: the textures and samplers of 32 units, the other buffer bindings, how pixels are stored, "
        "blending, clearing, depth, stencil and rasterizer state, the vertex attributes of the caller's vertex "
        "array and of the default one (seventh review: the textures of three more units were left unbound)");
#endif
#endif
  if (!g.subgroups)
  {
    printf("SKIPPED: this OpenGL ES has no shader subgroups for the direct way (run through zink on lavapipe)\n");
    fflush(stdout);
    _Exit(g_failures ? 1 : 77);
  }
  printf("the two ways: byte-identical in %ld drawings; most differing pixels %ld of %d, largest difference %ld "
         "codes of 4096, in the bars %ld, alpha %ld, metadata bits %ld\n",
         g_identical, g_mostPixels, W * H, g_mostCode, g_inBars, g_alpha, g_bits);
  if (!g_half)
  {
    Check(g_earlyUsed[1] == g_withMargins && !g_earlyGivenUp[1],
          "direct way: every picture with margins was drawn with the early hook, which never failed");
    Check(g_earlyDiffering[1] == 0, "direct way: the same bytes with and without the early hook");
    Check(g_mostCode <= 1, "the direct and the composed way never differ by more than one code of 4096");
    Check(g_mostPixels <= (long)W * H / 200, "in at most 0.5 % of the pixels of a picture");
    Check(g_inBars == 0 && g_alpha == 0,
          "and never in alpha or in a pair of pixels that is outside the active picture");
  }
  Check(g_bits == 0, "every metadata bit reads back the same from the two ways");
#ifdef DVBRIDGE_GL_PACKER_QUAD
  printf("the packing of %ld composed drawings by Kodi's packer: %ld bytes differ from the packing with two fetches, %ld "
         "from the one whose fetch of the even pixel gives a wrong value\n", g_quadChecks, g_quadPlainDiff,
         g_quadStrictDiff);
  Check(g_quadChecks > 0 && !g_quadPlainDiff && (!dvbridge_gl_packer_quad(g.packer) || !g_quadStrictDiff),
        "Kodi's packer gives the bytes of the packing with two fetches; with quad operations, every odd pixel takes "
        "the even one from its quad");
#endif
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  fflush(stdout);
  // the process ends here: nothing of the software GL is torn down in a particular order
  _Exit(g_failures != 0);
}
