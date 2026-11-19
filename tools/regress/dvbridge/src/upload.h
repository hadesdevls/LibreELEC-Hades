// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: a layer of fid_picture uploaded as P010 (the layout of the VAAPI surfaces in
// Kodi: luma in one 16-bit texture, the chroma interleaved in a second one, the 10 bits in the high bits), as the
// bridge's render tests upload an AVFrame. With step 2, every second sample of every second row (a 1920x1080
// picture from a 3840x2160 one).
#ifndef FID_UPLOAD_H
#define FID_UPLOAD_H
#include "layers.h"

struct fid_gl_layer
{
  GLuint tex[2];
  int w, h;
  int loc;
};

static int fid_upload(struct fid_gl_layer* l, const struct fid_plane* p, int loc, int step)
{
  const int w = p[0].w / step, h = p[0].h / step, cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = malloc((size_t)w * h * 2);
  uint16_t* c = malloc((size_t)cw * ch * 4);
  if (!y || !c)
    return 0;
  for (int r = 0; r < h; ++r)
    for (int x = 0; x < w; ++x)
      y[(size_t)r * w + x] = (uint16_t)(p[0].v[(size_t)r * step * p[0].w + (size_t)x * step] << 6);
  for (int r = 0; r < ch; ++r)
    for (int x = 0; x < cw; ++x)
    {
      c[((size_t)r * cw + x) * 2] = (uint16_t)(p[1].v[(size_t)r * step * p[1].w + (size_t)x * step] << 6);
      c[((size_t)r * cw + x) * 2 + 1] = (uint16_t)(p[2].v[(size_t)r * step * p[2].w + (size_t)x * step] << 6);
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
  l->loc = loc;
  return glGetError() == GL_NO_ERROR;
}
#endif
