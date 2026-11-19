// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades (1.0 RC4), host test of what the converted render of the bridge
// (render() of tools/dvbridge/dvbridge_render.c, through dvbridge_render_rgb(), as Kodi draws a frame of native
// output the composed way) says of a render that failed: dvbridge_render_base_refused() and
// dvbridge_render_composed(). The render is failed after libplacebo drew it, by an error that libplacebo reports
// for it (pl_renderer_get_errors() wrapped by the linker: --wrap=pl_renderer_get_errors), as an enhancement layer
// that cannot be sampled fails it ("An EL sampling failure can otherwise return a successfully rendered BL-only
// frame"). Before the fix, the reason and the composition were read before that error turned the render into a
// failed one: a picture whose base layer is in SDR or HLG and that libplacebo's reshaping drew failed with
// DVBRIDGE_BASE_PQ (no "sdrbase"/"hlgbase" reason) and DVBRIDGE_COMPOSED_RESHAPING.
// On a software OpenGL ES (zink over lavapipe), with the first base-layer picture of a real profile 7 stream (its
// RPU made to declare its picture in PQ as CCM 001 sends one, libplacebo-9975), told to be over PQ, SDR and HLG
// (dvbridge_render_set_base_transfer()), with the composer of
// ETSI GS CCM 001 and with libplacebo's reshaping (dvbridge_render_use_ccm_composer()), the source transport off:
// each render as it comes, then the same with the error. A picture that the reshaping draws over HLG (the
// composer of the clause does not compose it: the RPU declares PQ) is the case that the order of the fix changes.
// Usage: render_refused_test <file>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libplacebo/opengl.h>
#include <libplacebo/renderer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_render.h"

static int g_failures, g_inject;

struct pl_render_errors __real_pl_renderer_get_errors(pl_renderer rr);
struct pl_render_errors __wrap_pl_renderer_get_errors(pl_renderer rr)
{
  struct pl_render_errors errors = __real_pl_renderer_get_errors(rr);
  if (g_inject)
    errors.errors |= PL_RENDER_ERR_SAMPLING;
  return errors;
}

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_ERR)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

// yuv420p10le to P010, the layout of Kodi's VAAPI surfaces (as conv_test.c uploads it)
static int Upload(GLuint tex[2], const AVFrame* f, struct dvbridge_gl_plane planes[3])
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
  free(y);
  free(c);
  planes[0] = (struct dvbridge_gl_plane){tex[0], w, h};
  planes[1] = (struct dvbridge_gl_plane){tex[1], cw, ch};
  planes[2] = (struct dvbridge_gl_plane){0, 0, 0};
  return glGetError() == GL_NO_ERROR;
}

static const char* BaseName(int b)
{
  return b == DVBRIDGE_BASE_SDR ? "SDR" : b == DVBRIDGE_BASE_HLG ? "HLG" : "PQ";
}
static const char* ComposedName(int c)
{
  return c == DVBRIDGE_COMPOSED_CCM         ? "CCM"
         : c == DVBRIDGE_COMPOSED_RESHAPING ? "reshaping"
                                            : "none";
}

int main(int argc, char** argv)
{
  if (argc != 2)
    return 2;
  av_log_set_level(AV_LOG_ERROR);
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_ERR));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t(*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  struct dvbridge_renderer* r = gl ? dvbridge_renderer_create(gl->gpu, log) : NULL;
  Check(gl && r, "a renderer of native output, as Kodi makes it");
  if (!r)
    return 1;
  dvbridge_render_use_ccm_composer(r, false);
  dvbridge_render_use_source_transport(r, false);

  // the first picture of the base layer that has its metadata
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (index < 0)
    return 1;
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  if (!codec || !dec || avcodec_parameters_to_context(dec, fc->streams[index]->codecpar) < 0 ||
      avcodec_open2(dec, codec, NULL) < 0)
    return 1;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrameSideData* sd = NULL;
  int have = 0;
  for (int reads = 0; !have && reads < 400 && av_read_frame(fc, packet) >= 0; ++reads)
  {
    if (packet->stream_index == index)
      avcodec_send_packet(dec, packet);
    av_packet_unref(packet);
    while (!have && avcodec_receive_frame(dec, frame) >= 0)
    {
      sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
      if (sd)
        have = 1;
      else
        av_frame_unref(frame);
    }
  }
  Check(have, "a picture of the base layer with its Dolby Vision metadata");
  if (!have)
    return 1;
  GLuint tex[2] = {0, 0};
  struct dvbridge_gl_plane planes[3];
  struct dvbridge_gl_frame b = {0};
  if (!Upload(tex, frame, planes) ||
      !dvbridge_gl_frame_import(&b, gl->gpu, DVBRIDGE_P010, frame->width, frame->height, planes,
                                frame->chroma_location))
  {
    Check(0, "the picture uploaded and imported");
    return 1;
  }
  // The RPU made to declare its HDR picture in SMPTE ST 2084 at hdr_bit_depth, as CCM 001 sends one
  // (libplacebo-9975: signal_eotf 0xFFFF with its three parameters 0, signal_bit_depth = vdr_bit_depth): over a
  // base layer in HLG, the composer of the clause composes it as one in PQ and libplacebo's reshaping draws it
  uint8_t* metadata = malloc(sd->size);
  if (!metadata)
    return 1;
  memcpy(metadata, sd->data, sd->size);
  {
    AVDOVIMetadata* m = (AVDOVIMetadata*)metadata;
    AVDOVIColorMetadata* color = av_dovi_get_color(m);
    const AVDOVIRpuDataHeader* header = av_dovi_get_header(m);
    printf("the RPU declares: signal_eotf %u (%u %u %u), signal_bit_depth %u, vdr_bit_depth %u\n",
           color->signal_eotf, color->signal_eotf_param0, color->signal_eotf_param1,
           color->signal_eotf_param2, color->signal_bit_depth, header->vdr_bit_depth);
    color->signal_eotf = 0xFFFF;
    color->signal_eotf_param0 = color->signal_eotf_param1 = 0;
    color->signal_eotf_param2 = 0;
    color->signal_bit_depth = header->vdr_bit_depth;
  }
  const struct dvbridge_geometry full = {frame->width, frame->height, 0, 0, 3840, 2160};
  const int bases[] = {DVBRIDGE_BASE_PQ, DVBRIDGE_BASE_SDR, DVBRIDGE_BASE_HLG};
  double pts = 1.0;
  int reached = 0; // renders made by libplacebo's reshaping over a base layer that is not in PQ
  for (int ccm = 1; ccm >= 0; --ccm)
  {
    dvbridge_render_use_ccm_composer(r, ccm);
    for (int i = 0; i < 3; ++i)
    {
      int plain = DVBRIDGE_COMPOSED_NONE; // how the render as it comes composed the picture
      for (int inject = 0; inject < 2; ++inject)
      {
        dvbridge_render_set_base_transfer(r, bases[i]);
        g_inject = inject;
        const int ok = dvbridge_render_rgb(r, &b.frame, metadata, sd->size, pts, NAN, full);
        pts += 1.0 / 24;
        g_inject = 0;
        const int refused = dvbridge_render_base_refused(r);
        const int composed = dvbridge_render_composed(r);
        char what[320];
        printf("%s, base %s, %s: render %s, base refused %s, composed %s\n",
               ccm ? "composer of the clause" : "libplacebo's reshaping", BaseName(bases[i]),
               inject ? "an error after the drawing" : "as it comes", ok ? "made" : "failed",
               BaseName(refused), ComposedName(composed));
        if (!inject)
        {
          plain = ok ? composed : DVBRIDGE_COMPOSED_NONE;
          snprintf(what, sizeof(what),
                   "%s, base %s, as it comes: a render made composed its picture and refused nothing; one "
                   "that failed composed nothing",
                   ccm ? "composer" : "reshaping", BaseName(bases[i]));
          Check(ok ? refused == DVBRIDGE_BASE_PQ && composed != DVBRIDGE_COMPOSED_NONE
                   : composed == DVBRIDGE_COMPOSED_NONE,
                what);
        }
        else
        {
          // a picture that the composer of the clause composed was not refused for its base layer; one that
          // the reshaping drew over a base layer that is not in PQ is, once its render fails
          const int want = plain == DVBRIDGE_COMPOSED_CCM ? DVBRIDGE_BASE_PQ : bases[i];
          reached += plain == DVBRIDGE_COMPOSED_RESHAPING && bases[i] != DVBRIDGE_BASE_PQ;
          snprintf(what, sizeof(what),
                   "%s, base %s, failed by its errors after the drawing: nothing composed, the reason %s",
                   ccm ? "composer" : "reshaping", BaseName(bases[i]), BaseName(want));
          Check(!ok && composed == DVBRIDGE_COMPOSED_NONE && refused == want, what);
        }
        if (!ok)
          dvbridge_renderer_recover(r); // as CDVBridgeGLES does after a failure
      }
    }
  }
  Check(reached > 0, "a picture over a base layer that is not in PQ, drawn by the reshaping, failed after it");
  free(metadata);
  dvbridge_gl_frame_release(&b);
  glDeleteTextures(2, tex);
  dvbridge_renderer_destroy(r);
  pl_opengl_destroy(&gl);
  pl_log_destroy(&log);
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&dec);
  avformat_close_input(&fc);
  printf("%s (%d failed)\n", g_failures ? "RENDER-REFUSED FAILED" : "RENDER-REFUSED PASSED", g_failures);
  return g_failures != 0;
}
