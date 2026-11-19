// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: the frame that marks the end of the Dolby Vision signal (video/dveos).
//   E1 dvbridge_eos_packets() (tools/dvbridge/dvbridge_eos.h), without GL: EOS set in every packet and nothing else
//      changed but the CRC, which checks (CRC-32/MPEG-2: its check value first); the codes of black from the
//      offsets of the first packet; packets that are not those of a frame refused, with nothing written.
//   E2 dvbridge_gl_eos_draw() (tools/dvbridge/dvbridge_gl_pack.c) on a software OpenGL ES (zink on lavapipe):
//      with the codes of black of the converted transport and the packets as they are, the same bytes as
//      dvbridge_gl_pack() of a black picture (so its bits are where every packer puts them); with the EOS
//      packets, every metadata bit read back from the chroma LSBs is that of the EOS packets, every other code is
//      black, and the GL state is as it was.
// usage: eos_test
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_eos.h"
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

static void Words(const uint8_t p[][128], unsigned count, uint32_t* words)
{
  for (unsigned i = 0; i < count; i++)
    for (unsigned j = 0; j < 32; j++)
      words[i * 32 + j] = (uint32_t)p[i][4 * j] << 24 | (uint32_t)p[i][4 * j + 1] << 16 |
                          (uint32_t)p[i][4 * j + 2] << 8 | p[i][4 * j + 3];
}

static void Bytes(const uint32_t* words, unsigned i, uint8_t* p)
{
  for (unsigned j = 0; j < 32; j++)
  {
    const uint32_t w = words[i * 32 + j];
    p[4 * j] = w >> 24;
    p[4 * j + 1] = w >> 16;
    p[4 * j + 2] = w >> 8;
    p[4 * j + 3] = w;
  }
}

// packets as the serializer lays them out (dvbridge_metadata.h): count of them, metadata id 5, a structure of
// `size` bytes whose offsets (bytes 20 to 31) are those given
static void Make(uint8_t p[][128], unsigned count, unsigned size, const uint32_t offsets[3])
{
  uint8_t payload[512] = {0};
  for (unsigned i = 0; i < size; i++)
    payload[i] = (uint8_t)(i * 37 + 11);
  payload[0] = 0;
  for (int k = 0; k < 3; k++)
    for (int b = 0; b < 4; b++)
      payload[20 + 4 * k + b] = (uint8_t)(offsets[k] >> (24 - 8 * b));
  unsigned off = 0;
  memset(p, 0, (size_t)count * 128);
  for (unsigned i = 0; i < count; i++)
  {
    p[i][0] = (count == 1 ? 0 : i == 0 ? 1 : i + 1 == count ? 3 : 2) << 6;
    p[i][1] = 5 << 4 | 5;
    const unsigned start = i ? 3 : 5, room = i ? 121 : 119;
    unsigned take = size - off;
    if (take > room)
      take = room;
    if (!i)
    {
      p[i][3] = size >> 8;
      p[i][4] = size;
    }
    memcpy(p[i] + start, payload + off, take);
    off += take;
    const uint32_t crc = dvbridge_eos_crc(p[i], 124);
    for (int j = 0; j < 4; j++)
      p[i][124 + j] = crc >> (24 - j * 8);
  }
}

static unsigned Parity(unsigned v)
{
  v ^= v >> 16;
  v ^= v >> 8;
  v ^= v >> 4;
  v ^= v >> 2;
  v ^= v >> 1;
  return v & 1;
}

int main(void)
{
  setvbuf(stdout, NULL, _IOLBF, 0);
  // E1
  Check(dvbridge_eos_crc((const uint8_t*)"123456789", 9) == 0x0376E6E7u, "E1 the CRC is CRC-32/MPEG-2 (its check value)");
  static uint8_t p[16][128], q[16][128];
  uint32_t in[512], out[512];
  unsigned black[3];
  const uint32_t converted[3] = {16777216u, 134217728u, 134217728u};
  for (unsigned count = 1; count <= 5; count++)
  {
    const unsigned size = count == 1 ? 95 : 119 + (count - 2) * 121 + 30;
    Make(p, count, size, converted);
    Words((const uint8_t(*)[128])p, count, in);
    memset(out, 0xAB, sizeof(out));
    int ok = dvbridge_eos_packets(in, count, out, black);
    int same = ok, eos = ok, crc = ok;
    for (unsigned i = 0; ok && i < count; i++)
    {
      Bytes(out, i, q[i]);
      eos &= q[i][2] == 1;
      crc &= dvbridge_eos_crc(q[i], 128) == 0;
      for (unsigned j = 0; j < 124; j++)
        same &= j == 2 || q[i][j] == p[i][j];
    }
    char what[160];
    snprintf(what, sizeof(what), "E1 %u packet(s): EOS set in each, the CRC checks, every other byte as it was", count);
    Check(ok && same && eos && crc && out[count * 32] == 0xABABABABu, what);
    snprintf(what, sizeof(what), "E1 %u packet(s): black of the converted transport is 256, 2048, 2048", count);
    Check(ok && black[0] == 256 && black[1] == 2048 && black[2] == 2048, what);
  }
  {
    const uint32_t other[3] = {0u, 134217728u, 134217728u};
    Make(p, 2, 150, other);
    Words((const uint8_t(*)[128])p, 2, in);
    Check(dvbridge_eos_packets(in, 2, out, black) && black[0] == 0 && black[1] == 2048 && black[2] == 2048,
          "E1 black from the offsets of the RPU (0, 2^27, 2^27): 0, 2048, 2048");
    // (a code is 65536 of these units, half a code 32768)
    const uint32_t odd[3] = {16777216u + 32767u, 134217728u - 32767u, 268369920u};
    Make(p, 1, 95, odd);
    Words((const uint8_t(*)[128])p, 1, in);
    Check(dvbridge_eos_packets(in, 1, out, black) && black[0] == 256 && black[1] == 2048 && black[2] == 4095,
          "E1 an offset that is not a whole code goes to the nearest");
  }
  {
    Make(p, 2, 150, converted);
    Words((const uint8_t(*)[128])p, 2, in);
    memset(out, 0xCD, sizeof(out));
    uint32_t bad[512];
    memcpy(bad, in, sizeof(bad));
    bad[40] ^= 1;
    unsigned b[3] = {7, 7, 7};
    Check(!dvbridge_eos_packets(bad, 2, out, b) && out[0] == 0xCDCDCDCDu && b[0] == 7,
          "E1 a packet whose CRC does not check: refused, nothing written");
    Check(!dvbridge_eos_packets(in, 0, out, b) && !dvbridge_eos_packets(in, 17, out, b),
          "E1 no packet, or more than 16: refused");
    Make(p, 3, 300, converted);
    Words((const uint8_t(*)[128])p + 1, 2, in); // starting at the middle packet
    Check(!dvbridge_eos_packets(in, 2, out, b), "E1 packets that start in the middle of a structure: refused");
    Make(p, 1, 95, converted);
    p[0][0] |= 1; // no_md
    const uint32_t crc = dvbridge_eos_crc(p[0], 124);
    for (int j = 0; j < 4; j++)
      p[0][124 + j] = crc >> (24 - j * 8);
    Words((const uint8_t(*)[128])p, 1, in);
    Check(!dvbridge_eos_packets(in, 1, out, b), "E1 a header of another type, version, or no_md: refused");
    const uint32_t beyond[3] = {268435456u, 134217728u, 134217728u};
    Make(p, 1, 95, beyond);
    Words((const uint8_t(*)[128])p, 1, in);
    Check(!dvbridge_eos_packets(in, 1, out, b), "E1 an offset beyond the codes of the signal: refused");
  }

  // E2
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
  printf("renderer: %s\n", glGetString(GL_RENDERER));
  struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create_plain(PACK_VERTEX, PACK_FRAGMENT);
  struct dvbridge_gl_eos* eos = dvbridge_gl_eos_create();
  Check(packer && eos, "E2 the packer and the EOS program are made");
  if (!packer || !eos)
    return 1;
  GLuint picture, target, fbo;
  glGenTextures(1, &picture);
  glBindTexture(GL_TEXTURE_2D, picture);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
  float* rows = calloc((size_t)4 * W * 16, sizeof(float));
  for (int y = 0; y < H; y += 16)
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, W, 16, GL_RGBA, GL_FLOAT, rows);
  glGenTextures(1, &target);
  glBindTexture(GL_TEXTURE_2D, target);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  uint8_t* a = malloc((size_t)W * H * 4);
  uint8_t* b = malloc((size_t)W * H * 4);
  const unsigned conv[3] = {256, 2048, 2048};
  for (int flip = 0; flip <= 1; flip++)
  {
    for (unsigned count = 1; count <= 5; count += 2)
    {
      const unsigned size = count == 1 ? 95 : 119 + (count - 2) * 121 + 30;
      Make(p, count, size, converted);
      Words((const uint8_t(*)[128])p, count, in);
      int ok = dvbridge_gl_pack(packer, picture, fbo, W, H, in, count, flip);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
      ok &= dvbridge_gl_eos_draw(eos, fbo, W, H, in, count, conv, flip);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      char what[200];
      snprintf(what, sizeof(what),
               "E2 %u packet(s), %s: black with the packets as they are, the same bytes as the pack of a black "
               "picture", count, flip ? "flipped" : "not flipped");
      Check(ok && memcmp(a, b, (size_t)W * H * 4) == 0, what);

      // with the EOS packets
      Check(dvbridge_eos_packets(in, count, out, black), "E2 the EOS packets are made");
      struct dvbridge_gl_state before, after;
      memset(&before, 0, sizeof(before));
      memset(&after, 0, sizeof(after));
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      dvbridge_gl_state_save(&before);
      ok = dvbridge_gl_eos_draw(eos, fbo, W, H, out, count, black, flip);
      dvbridge_gl_state_save(&after);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      long wrongBits = 0, wrongCodes = 0;
      for (int row = 0; row < H; row++)
        for (int x = 0; x < W; x++)
        {
          const uint8_t* px = b + ((size_t)row * W + x) * 4;
          const int y = flip ? H - 1 - row : row; // the row of the transport
          const unsigned c = (unsigned)px[0] << 4 | px[2] >> 4, yy = (unsigned)px[1] << 4 | (px[2] & 15);
          const unsigned want = (x & 1) ? black[2] : black[1];
          const unsigned index = (unsigned)(y * W + x), packet = index / 3072;
          if (yy != black[0] || (c & 4094) != (want & 4094))
            ++wrongCodes;
          if (packet < count)
          {
            const unsigned bit = index % 1024, word = packet * 32 + bit / 32;
            const unsigned value = (out[word] >> (31 - bit % 32)) & 1;
            if (((c & 1) ^ Parity(c >> 1) ^ Parity(yy)) != value)
              ++wrongBits;
          }
          else if ((c & 1) != (want & 1))
            ++wrongCodes;
        }
      snprintf(what, sizeof(what),
               "E2 %u EOS packet(s), %s: every bit read back is that of the EOS packets (%ld wrong), every code "
               "black (%ld wrong)", count, flip ? "flipped" : "not flipped", wrongBits, wrongCodes);
      Check(ok && !wrongBits && !wrongCodes, what);
      Check(memcmp(&before, &after, sizeof(before)) == 0, "E2 the GL state as it was");
    }
  }
  dvbridge_gl_eos_destroy(eos);
  dvbridge_gl_packer_destroy(packer);
  printf("eos: %d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
