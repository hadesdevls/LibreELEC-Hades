// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the decoded layers and the parsed metadata of a picture as ref_dump writes
// them (<dir>/{bl,el}_{y,u,v}.u16: 10-bit codes, one uint16 a sample; meta.txt; meta.bin: the AVDOVIMetadata
// side data as FFmpeg gave it), so that every renderer measured is given the same inputs.
#ifndef FID_LAYERS_H
#define FID_LAYERS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fid_plane
{
  int w, h;
  uint16_t* v; // codes
};

struct fid_picture
{
  struct fid_plane bl[3], el[3];
  int bl_loc, el_loc; // AVChromaLocation
  long long bl_pts, el_pts;
  void* meta;
  size_t meta_size;
};

static int fid_meta_int(const char* dir, const char* key, long long* out)
{
  char path[1024], line[512];
  snprintf(path, sizeof path, "%s/meta.txt", dir);
  FILE* f = fopen(path, "r");
  if (!f)
    return 0;
  const size_t n = strlen(key);
  int found = 0;
  while (!found && fgets(line, sizeof line, f))
    if (!strncmp(line, key, n) && line[n] == ' ')
    {
      *out = atoll(line + n + 1);
      found = 1;
    }
  fclose(f);
  return found;
}

static int fid_read(const char* dir, const char* name, size_t bytes, void** out)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE* f = fopen(path, "rb");
  if (!f)
    return 0;
  if (!bytes)
  {
    fseek(f, 0, SEEK_END);
    bytes = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
  }
  // the metadata is read into memory aligned for any of its structs
  void* p = aligned_alloc(64, (bytes + 63) / 64 * 64);
  const int ok = p && fread(p, 1, bytes, f) == bytes;
  fclose(f);
  if (!ok)
  {
    free(p);
    return 0;
  }
  *out = p;
  return (int)(bytes > 0);
}

static int fid_load(const char* dir, struct fid_picture* p)
{
  memset(p, 0, sizeof(*p));
  long long bw, bh, ew, eh, bl, el;
  if (!fid_meta_int(dir, "bl.width", &bw) || !fid_meta_int(dir, "bl.height", &bh) ||
      !fid_meta_int(dir, "el.width", &ew) || !fid_meta_int(dir, "el.height", &eh) ||
      !fid_meta_int(dir, "bl.chroma_location", &bl) || !fid_meta_int(dir, "el.chroma_location", &el) ||
      !fid_meta_int(dir, "bl.pts", &p->bl_pts) || !fid_meta_int(dir, "el.pts", &p->el_pts))
    return 0;
  p->bl_loc = (int)bl;
  p->el_loc = (int)el;
  static const char* const names[2][3] = {{"bl_y.u16", "bl_u.u16", "bl_v.u16"}, {"el_y.u16", "el_u.u16", "el_v.u16"}};
  for (int l = 0; l < 2; ++l)
    for (int c = 0; c < 3; ++c)
    {
      struct fid_plane* q = l ? &p->el[c] : &p->bl[c];
      const int w = (int)(l ? ew : bw), h = (int)(l ? eh : bh);
      q->w = c ? (w + 1) / 2 : w;
      q->h = c ? (h + 1) / 2 : h;
      void* v = NULL;
      if (!fid_read(dir, names[l][c], (size_t)q->w * q->h * 2, &v))
        return 0;
      q->v = v;
    }
  void* m = NULL;
  if (!fid_read(dir, "meta.bin", 0, &m))
    return 0;
  p->meta = m;
  char path[1024];
  snprintf(path, sizeof path, "%s/meta.bin", dir);
  FILE* f = fopen(path, "rb");
  fseek(f, 0, SEEK_END);
  p->meta_size = (size_t)ftell(f);
  fclose(f);
  return 1;
}

static void fid_free(struct fid_picture* p)
{
  for (int c = 0; c < 3; ++c)
  {
    free(p->bl[c].v);
    free(p->el[c].v);
  }
  free(p->meta);
}
#endif
