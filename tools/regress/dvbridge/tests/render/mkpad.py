# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: fidbench and its upload with padded textures.
# usage: mkpad.py <fidbench.c> <upload.h> <out dir> (each text it changes must be there exactly once)
# FB_PADW / FB_PADH: the textures of every layer are that many samples wider / taller than the picture (luma; the
# chroma texture is that of the luma's size, as a decoder's surface is), the extra samples holding a code that no
# picture sample near them has (luma 1023, chroma 0 and 1023 by turns); the import is told the picture's size and
# the textures' own, as a surface of a decoder's pool larger than its picture would be.
import sys, os
fb, up, out = sys.argv[1:4]
t = open(fb).read()
def rep(s, old, new):
    assert s.count(old) == 1, old
    return s.replace(old, new)
t = rep(t, '''  struct dvbridge_gl_plane bp[3] = {{bl->tex[0], bl->w, bl->h}, {bl->tex[1], (bl->w + 1) / 2, (bl->h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_plane ep[3] = {{el->tex[0], el->w, el->h}, {el->tex[1], (el->w + 1) / 2, (el->h + 1) / 2}, {0, 0, 0}};''',
'''  struct dvbridge_gl_plane bp[3] = {{bl->tex[0], bl->tw, bl->th}, {bl->tex[1], (bl->tw + 1) / 2, (bl->th + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_plane ep[3] = {{el->tex[0], el->tw, el->th}, {el->tex[1], (el->tw + 1) / 2, (el->th + 1) / 2}, {0, 0, 0}};''')
t = rep(t, '#include "upload.h"', '#include "upload_pad.h"')
open(os.path.join(out, 'fidbench_pad.c'), 'w').write(t)

u = open(up).read()
u = rep(u, '''  GLuint tex[2];
  int w, h;
  int loc;
};''', '''  GLuint tex[2];
  int w, h;
  int loc;
  int tw, th; /* the textures' own size */
};''')
u = rep(u, '''  const int w = p[0].w / step, h = p[0].h / step, cw = (w + 1) / 2, ch = (h + 1) / 2;
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
    }''', '''  const int w = p[0].w / step, h = p[0].h / step, cw = (w + 1) / 2, ch = (h + 1) / 2;
  const char* ew = getenv("FB_PADW");
  const char* eh = getenv("FB_PADH");
  const int tw = w + (ew ? atoi(ew) : 0), th = h + (eh ? atoi(eh) : 0);
  const int tcw = (tw + 1) / 2, tch = (th + 1) / 2;
  uint16_t* y = malloc((size_t)tw * th * 2);
  uint16_t* c = malloc((size_t)tcw * tch * 4);
  if (!y || !c)
    return 0;
  for (int r = 0; r < th; ++r)
    for (int x = 0; x < tw; ++x)
      y[(size_t)r * tw + x] = r < h && x < w ? (uint16_t)(p[0].v[(size_t)r * step * p[0].w + (size_t)x * step] << 6)
                                             : (uint16_t)(1023 << 6);
  for (int r = 0; r < tch; ++r)
    for (int x = 0; x < tcw; ++x)
    {
      const int in = r < ch && x < cw, odd = (r + x) & 1;
      c[((size_t)r * tcw + x) * 2] = in ? (uint16_t)(p[1].v[(size_t)r * step * p[1].w + (size_t)x * step] << 6)
                                        : (uint16_t)(odd ? 1023 << 6 : 0);
      c[((size_t)r * tcw + x) * 2 + 1] = in ? (uint16_t)(p[2].v[(size_t)r * step * p[2].w + (size_t)x * step] << 6)
                                            : (uint16_t)(odd ? 0 : 1023 << 6);
    }''')
u = rep(u, '''  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, y);''',
           '''  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, tw, th, 0, GL_RED, GL_UNSIGNED_SHORT, y);''')
u = rep(u, '''  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, cw, ch, 0, GL_RG, GL_UNSIGNED_SHORT, c);''',
           '''  glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, tcw, tch, 0, GL_RG, GL_UNSIGNED_SHORT, c);''')
u = rep(u, '''  l->w = w;
  l->h = h;''', '''  l->w = w;
  l->h = h;
  l->tw = tw;
  l->th = th;''')
u = u.replace('FID_UPLOAD_H', 'FID_UPLOAD_PAD_H')
open(os.path.join(out, 'upload_pad.h'), 'w').write(u)
print('made fidbench_pad.c and upload_pad.h')
