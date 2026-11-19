// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image).
//
// The frame that sets the Dolby Vision signal up with the change of mode of a video that opens
// (CDVBridgeGLES::DrawStartSignal(), CDVBridgeGLES::StartSignalPresented()), drawn by the bridge of
// the branch under test on a software OpenGL ES (zink on lavapipe), as those two methods draw it
// (they are C++ with Kodi around them: this test makes the same calls of the bridge, in the same
// order, with the same arguments), and read back:
//   1. the metadata of black (dvbridge_profile81_metadata(0, 2081, 819, ...)) prepared in the
//      renderer's serializer (dvbridge_render_signal()), its packets and the codes of black that
//      their DM header gives (dvbridge_eos_packets());
//   2. the transport drawn with them (dvbridge_gl_eos_draw(), EOS 0), read by the independent
//      reader of ETSI GS CCM 001 (ccm001_oracle.h): a transport as the clause says,
//      EOS 0, 12 bits, the codes of black in every pixel (luma, and chroma bits 11..1);
//   3. the serializer after it: committed (dvbridge_render_commit()) and reset
//      (dvbridge_renderer_reset()), the next frame (the metadata of a picture, as the first frame of
//      the video) takes another metadata id and refreshes the scene; and the renderer has no picture
//      of the frame (dvbridge_render_texture() NULL).
// TEST_NO_COMMIT=1 leaves the commit out (what the check of 3 must catch, after a native output
// before it, TEST_AFTER_SESSION=1: the same id).
// Usage: start_signal_test
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ccm001_oracle.h"
#include "dvbridge_core.h"
#include "dvbridge_eos.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_hdr10plus.h"
#include "dvbridge_render.h"

#define W 3840
#define H 2160

static int g_checks, g_failed;
static void Check(int ok, const char* what)
{
  ++g_checks;
  if (!ok)
    ++g_failed;
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static AVDOVIMetadata* Black(size_t* size)
{
  AVMasteringDisplayMetadata mastering;
  memset(&mastering, 0, sizeof(mastering));
  mastering.has_luminance = 1;
  mastering.max_luminance = (AVRational){1000, 1};
  mastering.min_luminance = (AVRational){50, 10000};
  return dvbridge_profile81_metadata(0, 2081, 819, NULL, &mastering, NULL, 1, size);
}

// a picture's metadata, as the first frame of the video would carry (another level 1)
static AVDOVIMetadata* Picture(size_t* size)
{
  AVMasteringDisplayMetadata mastering;
  memset(&mastering, 0, sizeof(mastering));
  mastering.has_luminance = 1;
  mastering.max_luminance = (AVRational){1000, 1};
  mastering.min_luminance = (AVRational){50, 10000};
  return dvbridge_profile81_metadata(7, 3079, 1690, NULL, &mastering, NULL, 0, size);
}

int main(void)
{
  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, nc = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLConfig cfg = NULL;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nc) || !nc)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  struct dvbridge_renderer* r = gl ? dvbridge_renderer_create(gl->gpu, log) : NULL;
  if (!r)
    return 1;
  GLuint color = 0, fbo = 0;
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  // (something else in it before: the frame has to write every pixel)
  glClearColor(0.3f, 0.6f, 0.9f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // (TEST_AFTER_SESSION=1: the renderer as a native output before this one left it, reset by
  // EndDVBridge(): its serializer starts again, and its next frame takes the next metadata id)
  if (getenv("TEST_AFTER_SESSION") && strcmp(getenv("TEST_AFTER_SESSION"), "1") == 0)
  {
    printf("     (TEST_AFTER_SESSION=1: the renderer reset as after a native output)\n");
    dvbridge_renderer_reset(r);
  }
  // 1. the metadata of black in the renderer's serializer, as DrawStartSignal() prepares them
  size_t size = 0;
  AVDOVIMetadata* black = Black(&size);
  Check(black != NULL, "the metadata of a black picture of profile 8.1 are made");
  const char* why = NULL;
  const int prepared = black && dvbridge_render_signal(r, black, size, &why);
  if (!prepared)
    printf("     dvbridge_render_signal(): %s\n", why ? why : "(no reason)");
  Check(prepared, "dvbridge_render_signal() prepares them in the renderer's serializer");
  av_free(black);
  const struct dvbridge_candidate* candidate = dvbridge_render_candidate(r);
  Check(candidate != NULL, "they are the renderer's prepared frame (dvbridge_render_candidate())");
  Check(dvbridge_render_texture(r) == NULL, "the renderer has no picture of that frame");
  unsigned count = 0;
  const uint32_t* words = candidate ? dvbridge_packets(candidate, &count) : NULL;
  Check(words && count >= 1 && count <= 16, "they give transport packets");
  struct dvbridge_reading first = {0};
  Check(candidate && dvbridge_candidate_reading(candidate, &first), "the serializer says how it read them");
  printf("     packets %u, metadata id %u, scene refresh %d, level 5 %u %u %u %u\n", count, first.metadata_id,
         first.scene_refresh, first.margins[0], first.margins[1], first.margins[2], first.margins[3]);
  uint32_t marked[512];
  unsigned codes[3] = {0, 0, 0};
  const int fit = words && dvbridge_eos_packets(words, count, marked, codes);
  Check(fit, "their packets are those of a frame, and give the codes of black (dvbridge_eos_packets())");
  printf("     codes of black: %u %u %u\n", codes[0], codes[1], codes[2]);
  Check(codes[0] == 256 && codes[1] == 2048 && codes[2] == 2048,
        "the codes of black are those of BT.2020 YCbCr in 12 bits: 256, 2048, 2048");
  // (the packets that are drawn are the ones of the candidate, not the copy marked with EOS)
  int differ = 0;
  for (unsigned i = 0; words && i < count * 32; ++i)
    differ |= words[i] != marked[i];
  Check(differ, "the copy marked with EOS differs from the packets drawn (they go out with EOS 0)");

  // 2. the frame as DrawStartSignal() draws it, read back and read as CCM 001 says
  struct dvbridge_gl_eos* eos = dvbridge_gl_eos_create();
  Check(eos != NULL, "the program of the frame is made (dvbridge_gl_eos_create())");
  const int drawn = eos && words && dvbridge_gl_eos_draw(eos, fbo, W, H, words, count, codes, true);
  Check(drawn, "the frame is drawn (dvbridge_gl_eos_draw(), flipped as into Kodi's window)");
  uint8_t* rgba = malloc((size_t)W * H * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  Check(glGetError() == GL_NO_ERROR, "the frame is read back");
  struct ccm_oracle_opts o;
  ccm_oracle_defaults(&o);
  o.depth = 12;
  o.eos = 0;
  static struct ccm_oracle_result res;
  const int read = ccm_oracle_read(rgba, &o, &res);
  char text[512];
  ccm_oracle_describe(&res, text, sizeof(text));
  printf("     %s\n", text);
  Check(read, "the transport reads as ETSI GS CCM 001 says it is sent (independent reader), with EOS 0");
  Check(res.black[0] == 256 && res.black[1] == 2048 && res.black[2] == 2048,
        "its DM header gives the codes of black that were drawn");
  Check(res.l1 == 1 && res.l5 == 1, "it has one level 1 block and one level 5 block");
  Check(res.l5_offsets[0] == 0 && res.l5_offsets[1] == 0 && res.l5_offsets[2] == 0 && res.l5_offsets[3] == 0,
        "its level 5 block is the whole raster");
  long notBlack = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
    {
      unsigned Y, C;
      ccm_oracle_sample(rgba, &o, x, y, &Y, &C);
      const unsigned want = codes[(x & 1) ? 2 : 1];
      notBlack += Y != codes[0] || (C >> 1) != (want >> 1);
    }
  printf("     pixels not black: %ld of %d\n", notBlack, W * H);
  Check(notBlack == 0, "every pixel is black: its luma the code of black, its chroma bits 11..1 those of black");
  free(rgba);

  // 3. what follows its presentation (StartSignalPresented(true)): commit, then a new stream
  if (!getenv("TEST_NO_COMMIT") || strcmp(getenv("TEST_NO_COMMIT"), "1") != 0)
    Check(dvbridge_render_commit(r), "its metadata are committed (dvbridge_render_commit())");
  else
    printf("     (TEST_NO_COMMIT=1: not committed)\n");
  dvbridge_renderer_reset(r);
  Check(dvbridge_render_candidate(r) == NULL, "after the reset the renderer has no prepared frame");
  size_t psize = 0;
  AVDOVIMetadata* picture = Picture(&psize);
  const int next = picture && dvbridge_render_signal(r, picture, psize, &why);
  av_free(picture);
  struct dvbridge_reading second = {0};
  Check(next && dvbridge_candidate_reading(dvbridge_render_candidate(r), &second),
        "the next frame is prepared in the same serializer");
  printf("     next frame: metadata id %u, scene refresh %d\n", second.metadata_id, second.scene_refresh);
  Check(second.metadata_id != first.metadata_id,
        "the first frame of the video takes another metadata id than the frame of black (no metadata of "
        "black kept by the display for the video)");
  Check(second.scene_refresh, "the first frame of the video refreshes the scene");
  dvbridge_renderer_recover(r);

  dvbridge_gl_eos_destroy(eos);
  printf("%s: %d checks, %d failed\n", g_failed ? "START-SIGNAL-FAILED" : "START-SIGNAL-OK", g_checks, g_failed);
  fflush(stdout);
  _Exit(g_failed ? 1 : 0);
}
