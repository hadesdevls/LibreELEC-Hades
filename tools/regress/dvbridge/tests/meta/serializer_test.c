// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of the Dolby Vision metadata serializer of Kodi's dvbridge (dvbridge_core.c, dvbridge_metadata.h,
// mpv_dvbridge_cm4.h) and of the readability check of dvbridge_placebo.c, without any GL: prepare, commit
// and reset sequences with metadata made here, the margins of level 5, the transport packets read back
// byte by byte (CRC, header, payload), the scene refresh flag and the metadata id.
//
// Each check says what it is about. The runner (tools/regress/dvbridge/run-dvbridge-meta.sh) proves them with
// mutants of the bridge, each of which takes out one thing that the checks protect.
//
// Every frame prepared prints a line "BYTES <n> ..." with the size and a CRC of its payload, or "refused": the runner
// compares those lines with a reference (ref/meta/serializer-bytes.txt), so that a change shows every payload that it
// changes, and only those.
//
// With a bridge that leaves aside the level 5 offsets that fit no reading of the picture (it defines
// DVBRIDGE_DV_L5_NO_PICTURE), such a frame is made with the place of the picture and is not refused: the two checks of
// the refusal check that instead, and section 10 checks it closely (the reading says so and why, the payload is byte
// for byte that of the frame with a level 5 block of the place, it holds for the rest of the video, other offsets in
// that video are read as their own). No BYTES line is added or taken away by those checks, so that the payloads stay
// comparable line by line.
//
// Where the RPU has no level 5 block and the picture fills the raster, none is sent (the bridge defines
// DVBRIDGE_SENT_NO_L5); section 10 and the checks of the offsets that fit no reading say so. A bridge that sent a
// level 5 block of zeros there gives BYTES lines 13 bytes longer in the frames of that case only.
//
// With a bridge that can send every frame as the first picture of a scene (it defines DVBRIDGE_REFRESH_ALL:
// advancedsettings.xml, video/dvscenerefreshall), section 12 checks that switch; its frames are the last BYTES lines.
//
// With a bridge that has the form of the metadata (DVBRIDGE_FORM: dvbridge_set_form()), its last section checks that form
// against what a licensed core sent, byte for byte, and each part of it alone; it prints no BYTES line.
//
// Usage: serializer_test (exit 0 when every check passed)
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_core.h"
#include "dvbridge_metadata.h"
#include "dvbridge_placebo.h"

static int g_failures, g_checks, g_frames;

static void Check(int ok, const char* what)
{
  ++g_checks;
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

// ---------------------------------------------------------------- metadata made here
struct md
{
  AVDOVIMetadata* m;
  size_t size;
};

static struct md NewMetadata(void)
{
  struct md d = {0};
  d.m = av_dovi_metadata_alloc(&d.size);
  if (!d.m)
    abort();
  AVDOVIRpuDataHeader* h = av_dovi_get_header(d.m);
  h->rpu_type = 2;
  h->rpu_format = 18;
  h->vdr_rpu_profile = 1;
  h->bl_bit_depth = 10;
  h->el_bit_depth = 10;
  h->vdr_bit_depth = 12;
  h->coef_log2_denom = 23;
  h->disable_residual_flag = 1;
  AVDOVIDataMapping* map = av_dovi_get_mapping(d.m);
  for (int c = 0; c < 3; ++c)
  {
    map->curves[c].num_pivots = 2;
    map->curves[c].pivots[0] = 0;
    map->curves[c].pivots[1] = 1023;
    map->curves[c].mapping_idc[0] = AV_DOVI_MAPPING_POLYNOMIAL;
    map->curves[c].poly_order[0] = 1;
    map->curves[c].poly_coef[0][1] = 1 << 23;
  }
  AVDOVIColorMetadata* color = av_dovi_get_color(d.m);
  color->signal_eotf = 65535;
  color->signal_bit_depth = 12;
  color->source_min_pq = 62;
  color->source_max_pq = 3696;
  for (int i = 0; i < 9; ++i)
  {
    color->ycc_to_rgb_matrix[i] = (AVRational){i % 4 == 0, 1};
    color->rgb_to_lms_matrix[i] = (AVRational){i % 4 == 0, 1};
  }
  for (int i = 0; i < 3; ++i)
    color->ycc_to_rgb_offset[i] = (AVRational){0, 1};
  d.m->num_ext_blocks = 0;
  return d;
}

static AVDOVIDmData* Add(struct md* d, int level)
{
  AVDOVIDmData* e = av_dovi_get_ext(d->m, d->m->num_ext_blocks++);
  memset(e, 0, sizeof(*e));
  e->level = level;
  return e;
}

static void AddL1(struct md* d, int min, int avg, int max)
{
  AVDOVIDmData* e = Add(d, 1);
  e->l1.min_pq = min;
  e->l1.avg_pq = avg;
  e->l1.max_pq = max;
}

static void AddL2(struct md* d, int target)
{
  AVDOVIDmData* e = Add(d, 2);
  e->l2.target_max_pq = target;
  e->l2.trim_slope = 2048;
  e->l2.trim_offset = 2048;
  e->l2.trim_power = 2048;
  e->l2.trim_chroma_weight = 2048;
  e->l2.trim_saturation_gain = 2048;
  e->l2.ms_weight = 2048;
}

static void AddL5(struct md* d, int l, int r, int t, int b)
{
  AVDOVIDmData* e = Add(d, 5);
  e->l5.left_offset = l;
  e->l5.right_offset = r;
  e->l5.top_offset = t;
  e->l5.bottom_offset = b;
}

static void AddRaw(struct md* d, int level, const uint8_t* bytes, unsigned length)
{
  AVDOVIDmData* e = Add(d, level);
  e->dvbridge_raw_magic = 0x41424456;
  e->dvbridge_original_length = length;
  memcpy(e->dvbridge_original_bytes, bytes, length);
}

static void Free(struct md* d)
{
  av_free(d->m);
  d->m = NULL;
}

// ---------------------------------------------------------------- the transport, read back
struct frame
{
  int ok;
  unsigned count, size, id, refresh;
  unsigned margins[4];
  unsigned l5[4];        // the level 5 block of the payload
  int hasL5;
  uint8_t payload[2048];
  int crcs, headers;     // packets whose CRC holds, and whose header bytes are right
  unsigned blocks[40];   // levels of the extension blocks in the payload
  unsigned numBlocks;
  uint8_t l11[4];
  int hasL11;
};

static unsigned PacketByte(const uint32_t* words, unsigned packet, unsigned j)
{
  return (words[packet * 32 + j / 4] >> (24 - 8 * (j % 4))) & 255;
}

static struct frame Read(const struct dvbridge_candidate* c)
{
  struct frame f;
  memset(&f, 0, sizeof(f));
  if (!c)
    return f;
  const uint32_t* w = dvbridge_packets(c, &f.count);
  dvbridge_active_area(c, f.margins);
  unsigned off = 0, total = 0;
  for (unsigned i = 0; i < f.count; ++i)
  {
    uint8_t p[128];
    for (unsigned j = 0; j < 128; ++j)
      p[j] = PacketByte(w, i, j);
    f.crcs += dvbridge_dv_crc(p, 128) == 0;
    const unsigned type = f.count == 1 ? 0 : i == 0 ? 1 : i + 1 == f.count ? 3 : 2;
    f.headers += (p[0] >> 6) == type && (p[1] >> 4) == (p[1] & 15);
    f.id = p[1] & 15;
    if (!i)
      total = p[3] << 8 | p[4];
    const unsigned start = i ? 3 : 5, room = i ? 121 : 119;
    for (unsigned j = 0; j < room && off < total; ++j)
      f.payload[off++] = p[start + j];
  }
  f.size = total;
  f.refresh = f.payload[1];
  // the extension blocks
  unsigned pos = 71;
  const unsigned blocks = f.payload[70];
  for (unsigned b = 0; b < blocks && pos + 5 <= total && f.numBlocks < 40; ++b)
  {
    const unsigned length = (unsigned)f.payload[pos] << 24 | f.payload[pos + 1] << 16 | f.payload[pos + 2] << 8 |
                            f.payload[pos + 3];
    const unsigned level = f.payload[pos + 4];
    f.blocks[f.numBlocks++] = level;
    if (level == 5 && length == 8)
    {
      f.hasL5 = 1;
      for (int k = 0; k < 4; ++k)
        f.l5[k] = f.payload[pos + 5 + 2 * k] << 8 | f.payload[pos + 6 + 2 * k];
    }
    if (level == 11 && length == 4)
    {
      f.hasL11 = 1;
      memcpy(f.l11, f.payload + pos + 5, 4);
    }
    pos += 5 + length;
  }
  f.ok = 1;
  return f;
}

static int Full(const struct frame* f)
{
  return f->ok && f->crcs == (int)f->count && f->headers == (int)f->count && f->size > 70;
}

static const struct dvbridge_geometry RASTER = {3840, 2160, 0, 0, 3840, 2160};

// prepare one frame and read it back; commit it when asked
static struct frame Frame(struct dvbridge_context* ctx, const struct md* d, double pts, struct dvbridge_geometry g,
                          int commit)
{
  struct dvbridge_candidate* c = dvbridge_prepare(ctx, d->m, d->size, pts, g, false);
  struct frame f = Read(c);
  if (c && commit && !dvbridge_commit(ctx, c))
    f.ok = 0;
  dvbridge_candidate_destroy(c);
  if (f.ok)
    printf("BYTES %d size %u packets %u crc %08x\n", ++g_frames, f.size, f.count,
           (unsigned)dvbridge_dv_crc(f.payload, f.size));
  else
    printf("BYTES %d refused\n", ++g_frames);
  return f;
}

static void Margins(const char* what, struct dvbridge_geometry g, int l, int r, int t, int b, const unsigned want[4],
                    const char* why)
{
  struct dvbridge_context* ctx = dvbridge_create();
  struct md d = NewMetadata();
  AddL1(&d, 62, 1000, 3000);
  if (l >= 0)
    AddL5(&d, l, r, t, b);
  struct frame f = Frame(ctx, &d, 0, g, 1);
  char line[512];
  if (want)
  {
    snprintf(line, sizeof line,
             "margins, %s (picture %dx%d at %d,%d %dx%d, offsets %d %d %d %d): %u %u %u %u, level 5 block %u %u %u %u; "
             "want %u %u %u %u (%s)",
             what, g.source_width, g.source_height, g.x, g.y, g.width, g.height, l, r, t, b, f.margins[0],
             f.margins[1], f.margins[2], f.margins[3], f.l5[0], f.l5[1], f.l5[2], f.l5[3], want[0], want[1], want[2],
             want[3], why);
    Check(Full(&f) && f.hasL5 && !memcmp(f.margins, want, 16) && !memcmp(f.l5, want, 16), line);
  }
  else
  {
    snprintf(line, sizeof line, "margins, %s (offsets %d %d %d %d): the frame is refused (%s)", what, l, r, t, b, why);
    Check(!f.ok, line);
  }
  if (want)
  {
    struct dvbridge_candidate* c = dvbridge_prepare(ctx, d.m, d.size, 1000000, g, false);
    struct dvbridge_reading reading;
    if (c && dvbridge_candidate_reading(c, &reading))
      printf("      reading: l5_master %d, as given %u %u %u %u, margins %u %u %u %u\n", reading.l5_master,
             reading.as_given[0], reading.as_given[1], reading.as_given[2], reading.as_given[3], reading.margins[0],
             reading.margins[1], reading.margins[2], reading.margins[3]);
    dvbridge_candidate_destroy(c);
  }
  Free(&d);
  dvbridge_destroy(ctx);
}

// ---------------------------------------------------------------- content mapping v4.0
// Raw bytes of the blocks of the v4.0 part (and level 4), as the RPU codes them and dvbridge_cm4_wire()
// takes them (the lengths of dovi_tool, main 41dae646: level8.rs, cmv40.rs)
static const uint8_t L3[5] = {0x80, 0x08, 0x00, 0x80, 0x00};     // three 12-bit offsets, 4 bits of padding
static const uint8_t L4[3] = {0x12, 0x34, 0x56};                 // two 12-bit values
static const uint8_t L9_17[17] = {255, 0x80, 0x00, 0x40, 0x00, 0x20, 0x00, 0x10, 0x00,
                                  0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00}; // primaries given
static const uint8_t L9_1[1] = {2};                              // a primaries index
static const uint8_t L11[4] = {1, 0x00, 0, 0};
static const uint8_t L254[2] = {0, 2};

// a level 8 block of `length` bytes of a target display index t (bytes after the index: a pattern)
static void AddL8(struct md* d, uint8_t t, unsigned length)
{
  uint8_t b[32] = {0};
  b[0] = t;
  for (unsigned j = 1; j < length && j < sizeof(b); ++j)
    b[j] = (uint8_t)(0x11 * j);
  if (length == 12)
    b[11] &= 0xF0; // (92 bits of fields: the last 4 are padding, 0)
  AddRaw(d, 8, b, length);
}

// a level 10 block of 21 bytes (primaries given: the index, byte 4, is 255) or of 5
static void AddL10(struct md* d, uint8_t t, unsigned length)
{
  uint8_t b[21] = {t, 0x40, 0x00, 0x80, length == 21 ? 255 : 1};
  for (unsigned j = 5; j < length; ++j)
    b[j] = (uint8_t)(0x10 + j);
  AddRaw(d, 10, b, length);
}

// the metadata of one frame with dovi_tool's most blocks (cmv29.rs: eight level 2; cmv40.rs: five level 8,
// four level 10, and levels 3, 9, 11, 254), and level 4: a payload of 583 bytes, 256 of them the v2.9 part
static struct md Maxima(int with40)
{
  struct md d = NewMetadata();
  AddL1(&d, 62, 1000, 3000);
  for (int t = 0; t < 8; ++t)
    AddL2(&d, 1000 + 300 * t);
  AddRaw(&d, 4, L4, 3);
  AddL5(&d, 0, 0, 276, 276);
  if (with40)
  {
    AddRaw(&d, 3, L3, 5);
    for (int t = 0; t < 5; ++t)
      AddL8(&d, (uint8_t)(t + 1), 25);
    AddRaw(&d, 9, L9_17, 17);
    for (int t = 0; t < 4; ++t)
      AddL10(&d, (uint8_t)(t + 1), 21);
    AddRaw(&d, 11, L11, 4);
    AddRaw(&d, 254, L254, 2);
  }
  return d;
}

// whether the payload of a frame carries a block of the v4.0 part
static int HasV40(const struct frame* f)
{
  for (unsigned b = 0; b < f->numBlocks; ++b)
  {
    const unsigned l = f->blocks[b];
    if (l == 3 || (l >= 8 && l <= 11) || l == 254)
      return 1;
  }
  return 0;
}

static int SamePayload(const struct frame* a, const struct frame* b)
{
  return a->ok && b->ok && a->size == b->size && !memcmp(a->payload, b->payload, a->size);
}

// ---------------------------------------------------------------- the reason of a refusal
// Why a frame is refused, as dvbridge_prepare_transport() says it; NULL when the frame is made, "" when it is
// refused and no reason is given (the bridge before gave none)
static const char* Why(struct dvbridge_context* ctx, const struct md* d, struct dvbridge_geometry g)
{
  const char* why = NULL;
  struct dvbridge_candidate* c = dvbridge_prepare_why(ctx, d->m, d->size, 0, g, false, false, &why);
  const int made = c != NULL;
  dvbridge_candidate_destroy(c);
  return made ? NULL : why ? why : "";
}

static void Refused(const char* what, struct dvbridge_context* ctx, const struct md* d, struct dvbridge_geometry g,
                    const char* word)
{
  const char* why = Why(ctx, d, g);
  char line[512];
  snprintf(line, sizeof line, "%s: refused, and why: \"%s\"", what, why ? why : "(made)");
  Check(why && strstr(why, word) != NULL, line);
}

// ---------------------------------------------------------------- the form of the metadata (dvbridge_set_form())
// With a bridge that has it (DVBRIDGE_FORM): what a licensed core sends, read on the user's Amlogic player with Dolby's
// own core (its "reversed md_reg3" of the first frame of two captures, 24 November 2026), byte for
// byte from the RPUs of those frames (their "original md_buf") with the licensed form asked for; and each part of the
// form alone, against Hades' own. No BYTES line is printed here, so that the payloads of the sections above stay
// comparable with ref/meta/serializer-bytes.txt line by line.
#ifdef DVBRIDGE_FORM
// the fields of an extension block packed as an RPU has them: most significant bit first, zero bits to a whole byte
static unsigned PackFields(uint8_t* out, const unsigned* values, const unsigned* widths, unsigned n)
{
  unsigned bit = 0;
  memset(out, 0, 32);
  for (unsigned k = 0; k < n; ++k)
    for (unsigned b = widths[k]; b-- > 0; ++bit)
      if (values[k] >> b & 1)
        out[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
  return (bit + 7) / 8;
}

static void AddPacked(struct md* d, int level, const unsigned* values, const unsigned* widths, unsigned n)
{
  uint8_t raw[32];
  const unsigned length = PackFields(raw, values, widths, n);
  AddRaw(d, level, raw, length);
}

static void AddL2Values(struct md* d, const unsigned v[7])
{
  AVDOVIDmData* e = Add(d, 2);
  e->l2.target_max_pq = v[0];
  e->l2.trim_slope = v[1];
  e->l2.trim_offset = v[2];
  e->l2.trim_power = v[3];
  e->l2.trim_chroma_weight = v[4];
  e->l2.trim_saturation_gain = v[5];
  e->l2.ms_weight = (int16_t)v[6];
}

// the colour metadata of an RPU, its matrices in the units of the RPU (13, 28 and 14 fractional bits)
static void SetColour(struct md* d, const int ycc[9], const unsigned off[3], const int lms[9], int space, int min,
                      int max)
{
  AVDOVIColorMetadata* c = av_dovi_get_color(d->m);
  for (int i = 0; i < 9; ++i)
  {
    c->ycc_to_rgb_matrix[i] = (AVRational){ycc[i], 8192};
    c->rgb_to_lms_matrix[i] = (AVRational){lms[i], 16384};
  }
  for (int i = 0; i < 3; ++i)
    c->ycc_to_rgb_offset[i] = (AVRational){(int)off[i], 1 << 28};
  c->signal_color_space = space;
  c->source_min_pq = min;
  c->source_max_pq = max;
}

static int Hex(const char* hex, uint8_t* out)
{
  int n = 0;
  for (; hex[0] && hex[1]; hex += 2)
  {
    unsigned v;
    if (sscanf(hex, "%2x", &v) != 1)
      return -1;
    out[n++] = (uint8_t)v;
  }
  return n;
}

// the second frame of a stream (the first refreshes the scene; the captured frames do not) in a transport, read back
static struct frame Second(struct dvbridge_context* ctx, const struct md* d, struct dvbridge_geometry g, int source)
{
  struct dvbridge_candidate* c = dvbridge_prepare_transport(ctx, d->m, d->size, 0, g, false, source);
  if (c)
    dvbridge_commit(ctx, c);
  dvbridge_candidate_destroy(c);
  c = dvbridge_prepare_transport(ctx, d->m, d->size, 41708, g, false, source);
  struct frame f = Read(c);
  dvbridge_candidate_destroy(c);
  return f;
}

static void BlocksText(const struct frame* f, char* out, size_t size)
{
  size_t at = 0;
  out[0] = 0;
  for (unsigned i = 0; i < f->numBlocks && at < size; ++i)
    at += (size_t)snprintf(out + at, size - at, "%s%u", i ? " " : "", f->blocks[i]);
}

// the payload of a block of the level given (the nth of that level) in a frame read back; its length, or -1
static int BlockOf(const struct frame* f, unsigned level, unsigned nth, const uint8_t** at)
{
  unsigned pos = 71;
  for (unsigned b = 0; b < f->numBlocks && pos + 5 <= f->size; ++b)
  {
    const unsigned length = (unsigned)f->payload[pos] << 24 | f->payload[pos + 1] << 16 |
                            f->payload[pos + 2] << 8 | f->payload[pos + 3];
    if (f->payload[pos + 4] == level && !nth--)
    {
      *at = f->payload + pos + 5;
      return (int)length;
    }
    pos += 5 + length;
  }
  return -1;
}

// the RPU of the first frame of the profile 5 capture (cap-p5-20261124T162808Z.txt): content mapping v4.0
static struct md P5Rpu(void)
{
  static const int ycc[9] = {8192, 799, 1681, 8192, -933, 1091, 8192, 267, -5545};
  static const int lms[9] = {17081, -349, -349, -349, 17081, -349, -349, -349, 17081};
  static const unsigned off[3] = {0, 1u << 27, 1u << 27};
  static const unsigned l2a[7] = {0x821, 0x600, 0x803, 0x4bb, 0x800, 0x78f, 0x800};
  static const unsigned l2b[7] = {0xb23, 0x7c1, 0x82d, 0x77f, 0x800, 0x7f4, 0x800};
  static const unsigned l2c[7] = {0xc07, 0x800, 0x802, 0x801, 0x800, 0x800, 0x800};
  static const unsigned w12x2[2] = {12, 12}, l4[2] = {0x100, 0x180};
  static const unsigned w12x3[3] = {12, 12, 12}, l3[3] = {0x835, 0x800, 0x505};
  static const unsigned w8[7] = {8, 12, 12, 12, 12, 12, 12}, l8[7] = {1, 0x7ee, 0x87e, 0x800, 0x800, 0x78f, 0x800};
  static const uint8_t l9[1] = {0}, l254[2] = {0, 2}, l11[4] = {1, 0, 0, 0};
  struct md d = NewMetadata();
  SetColour(&d, ycc, off, lms, 2, 7, 3079);
  AddL1(&d, 0x00c, 0x6d3, 0xbb3);
  AddL2Values(&d, l2a);
  AddL2Values(&d, l2b);
  AddL2Values(&d, l2c);
  AddPacked(&d, 4, l4, w12x2, 2);
  AddL5(&d, 0, 0, 0, 0);
  Add(&d, 6); // level 6 (never sent)
  AddPacked(&d, 3, l3, w12x3, 3);
  AddPacked(&d, 8, l8, w8, 7);
  AddRaw(&d, 9, l9, 1);
  AddRaw(&d, 254, l254, 2);
  AddRaw(&d, 11, l11, 4);
  return d;
}

// the RPU of the first frame of Top Gun: Maverick (cap-maverick-20261124T164325Z.txt): profile 7, content mapping v2.9
static struct md MaverickRpu(void)
{
  static const int ycc[9] = {9574, 0, 13802, 9574, -1540, -5348, 9574, 17610, 0};
  static const int lms[9] = {7222, 8771, 390, 2654, 12430, 1300, 0, 422, 15962};
  static const unsigned off[3] = {1u << 24, 1u << 27, 1u << 27};
  static const unsigned l2[7] = {0x821, 0x70c, 0x82b, 0x723, 0x800, 0x714, 0x000};
  static const unsigned w12x2[2] = {12, 12}, l4[2] = {0x28a, 0x192};
  struct md d = NewMetadata();
  SetColour(&d, ycc, off, lms, 0, 7, 3079);
  AddL1(&d, 0x00c, 0x333, 0x83c);
  AddL2Values(&d, l2);
  AddPacked(&d, 4, l4, w12x2, 2);
  AddL5(&d, 0, 0, 0, 0);
  Add(&d, 6);
  return d;
}

static const unsigned LICENSED = DVBRIDGE_FORM_LEVEL_ORDER | DVBRIDGE_FORM_L254_ZERO | DVBRIDGE_FORM_L3_NEUTRAL;

static void Captured(const char* what, struct md (*rpu)(void), const char* want_hex)
{
  uint8_t want[512];
  const int n = Hex(want_hex, want);
  struct md d = rpu();
  struct dvbridge_context* ctx = dvbridge_create();
  dvbridge_set_form(ctx, DVBRIDGE_L5_NONE, LICENSED);
  const struct frame f = Second(ctx, &d, RASTER, 2);
  int first = -1;
  for (int i = 0; i < n && i < (int)f.size && first < 0; ++i)
    if (f.payload[i] != want[i])
      first = i;
  char blocks[160], line[512];
  BlocksText(&f, blocks, sizeof blocks);
  snprintf(line, sizeof line,
           "the licensed form, %s: the payload of %u bytes (blocks %s) byte for byte what the licensed core sent (%d "
           "bytes)%s",
           what, f.size, blocks, n, first < 0 && (int)f.size == n ? "" : ", it is not");
  Check(Full(&f) && (int)f.size == n && first < 0, line);
  if (first >= 0)
    printf("      first byte that differs: %d (0x%02x, want 0x%02x)\n", first, f.payload[first], want[first]);
  // Hades' own form of the same frame is another payload (the default does not change)
  struct dvbridge_context* own = dvbridge_create();
  const struct frame g = Second(own, &d, RASTER, 2);
  BlocksText(&g, blocks, sizeof blocks);
  snprintf(line, sizeof line, "the licensed form, %s: Hades' own form is another payload (blocks %s, a level 5 block)",
           what, blocks);
  Check(Full(&g) && (g.size != f.size || memcmp(g.payload, f.payload, f.size)) && g.hasL5, line);
  dvbridge_destroy(own);
  dvbridge_destroy(ctx);
  Free(&d);
}

static void FormTests(void)
{
  // 1. the two captures, byte for byte (the header of the IPT tunnel, the blocks, their order and values)
  Captured("profile 5, content mapping v4.0 (level 5 and 6 left out, level 254 0000, the blocks by level)", P5Rpu,
           "00002002031f06912002fc5b04432002010bea5700000000080000000800000042b8fea4fea4fea442b8fea4fea4fea442b8"
           "ffff00000000000000000c02010100070c07002a0a0000000601000c0bb306d30000000e0208210600080304bb0800078f0800"
           "0000000e020b2307c1082d077f080007f408000000000e020c070800080208010800080008000000000603083508000505000000"
           "0404010001800000000d080107ee087e08000800078f0800000000010900000000040b0100000000000002fe0000");
  Captured("Top Gun: Maverick, profile 7, content mapping v2.9 (a neutral level 3 added, no level 5)", MaverickRpu,
           "00002002031f06912002fc5b04432002010bea5700000000080000000800000042b8fea4fea4fea442b8fea4fea4fea442b8"
           "ffff00000000000000000c02010100070c07002a040000000601000c083c03330000000e020821070c082b072308000714000000"
           "000006030800080008000000000404028a0192");

  char line[512], blocks[160];
  // 2. each part alone; Hades' own first
  struct md p5 = P5Rpu();
  struct dvbridge_context* ctx = dvbridge_create();
  struct frame own = Second(ctx, &p5, RASTER, 1);
  BlocksText(&own, blocks, sizeof blocks);
  const uint8_t* b254 = NULL;
  const int n254 = BlockOf(&own, 254, 0, &b254);
  snprintf(line, sizeof line, "Hades' own form: the blocks in the order of their rank, the v2.9 part first (%s; want 1 2 2 2 4 5 3 8 9 11 254), "
           "level 254 as the RPU has it (0002)", blocks);
  Check(Full(&own) && !strcmp(blocks, "1 2 2 2 4 5 3 8 9 11 254") && n254 == 2 && b254[0] == 0 && b254[1] == 2, line);
  dvbridge_destroy(ctx);

  ctx = dvbridge_create();
  dvbridge_set_form(ctx, DVBRIDGE_L5_PLACED, DVBRIDGE_FORM_LEVEL_ORDER);
  struct frame ordered = Second(ctx, &p5, RASTER, 1);
  struct dvbridge_candidate* c = dvbridge_prepare_transport(ctx, p5.m, p5.size, 83416, RASTER, false, 1);
  struct dvbridge_reading r;
  memset(&r, 0, sizeof r);
  const int read = c && dvbridge_candidate_reading(c, &r);
  uint8_t sent[40];
  unsigned area[4];
  const int count = dvbridge_sent_blocks(c, sent, 40, area);
  dvbridge_candidate_destroy(c);
  BlocksText(&ordered, blocks, sizeof blocks);
  // the same blocks, each with the same bytes, in another order
  int same = Full(&ordered) && ordered.size == own.size && ordered.numBlocks == own.numBlocks;
  for (unsigned level = 0; level < 256 && same; ++level)
    for (unsigned k = 0; k < 8; ++k)
    {
      const uint8_t *a = NULL, *b = NULL;
      const int la = BlockOf(&own, level, k, &a), lb = BlockOf(&ordered, level, k, &b);
      same &= la == lb && (la < 0 || !memcmp(a, b, (size_t)la));
    }
  snprintf(line, sizeof line, "the blocks in the order of their level: %s (want 1 2 2 2 3 4 5 8 9 11 254), each block "
           "byte for byte Hades' own; the reading says so (%d), the player information reads %d blocks back", blocks,
           r.level_order, count);
  Check(same && !strcmp(blocks, "1 2 2 2 3 4 5 8 9 11 254") && read && r.level_order && count == 11 &&
            sent[4] == 3 && sent[10] == 254,
        line);
  dvbridge_destroy(ctx);

  ctx = dvbridge_create();
  dvbridge_set_form(ctx, DVBRIDGE_L5_PLACED, DVBRIDGE_FORM_L254_ZERO);
  struct frame zero = Second(ctx, &p5, RASTER, 1);
  b254 = NULL;
  const int z254 = BlockOf(&zero, 254, 0, &b254);
  BlocksText(&zero, blocks, sizeof blocks);
  snprintf(line, sizeof line, "level 254 with version 0: %02x%02x (want 0000), the rest as Hades' own (%s)",
           z254 == 2 ? b254[0] : 0xff, z254 == 2 ? b254[1] : 0xff, blocks);
  Check(Full(&zero) && z254 == 2 && b254[0] == 0 && b254[1] == 0 && zero.size == own.size &&
            !memcmp(zero.payload, own.payload, (size_t)(b254 - zero.payload)),
        line);
  // ... and in the default v4.0 part, where it is added
  struct md m29 = MaverickRpu();
  dvbridge_set_cm40_default(ctx, true);
  struct frame added = Second(ctx, &m29, RASTER, 1);
  b254 = NULL;
  const int a254 = BlockOf(&added, 254, 0, &b254);
  snprintf(line, sizeof line, "level 254 with version 0 in the default v4.0 part: %02x%02x (want 0000)",
           a254 == 2 ? b254[0] : 0xff, a254 == 2 ? b254[1] : 0xff);
  Check(Full(&added) && a254 == 2 && b254[0] == 0 && b254[1] == 0, line);
  dvbridge_destroy(ctx);

  // 3. the neutral level 3: content mapping v2.9 only, a display that reads v4.0 only, one level 3 only
  ctx = dvbridge_create();
  dvbridge_set_form(ctx, DVBRIDGE_L5_PLACED, DVBRIDGE_FORM_L3_NEUTRAL);
  struct frame n29 = Second(ctx, &m29, RASTER, 1);
  const uint8_t* b3 = NULL;
  const int l3 = BlockOf(&n29, 3, 0, &b3);
  static const uint8_t neutral[6] = {0x08, 0x00, 0x08, 0x00, 0x08, 0x00};
  BlocksText(&n29, blocks, sizeof blocks);
  struct frame n40 = Second(ctx, &p5, RASTER, 1);
  const uint8_t* b3b = NULL;
  const int second3 = BlockOf(&n40, 3, 1, &b3b);
  dvbridge_set_display_management(ctx, 2);
  struct frame dm2 = Second(ctx, &m29, RASTER, 1);
  dvbridge_set_display_management(ctx, 4);
  dvbridge_set_cm40_default(ctx, true);
  struct frame withdef = Second(ctx, &m29, RASTER, 1);
  const uint8_t* b3c = NULL;
  const int twice = BlockOf(&withdef, 3, 1, &b3c);
  snprintf(line, sizeof line, "a neutral level 3: added to content mapping v2.9 (%s, level 3 %s), not to v4.0 (a "
           "second level 3: %s), not for display management 2 (%s), not twice with the default v4.0 part (%s)",
           blocks, l3 == 6 && !memcmp(b3, neutral, 6) ? "0800 0800 0800" : "wrong",
           second3 >= 0 ? "yes" : "no", BlockOf(&dm2, 3, 0, &b3b) >= 0 ? "added" : "none",
           twice >= 0 ? "twice" : "once");
  Check(Full(&n29) && l3 == 6 && !memcmp(b3, neutral, 6) && Full(&n40) && second3 < 0 && n40.size == own.size &&
            Full(&dm2) && BlockOf(&dm2, 3, 0, &b3b) < 0 && Full(&withdef) && twice < 0,
        line);
  dvbridge_destroy(ctx);
  Free(&m29);

  // 4. the level 5 block: the four forms, with bars inside the picture, drawn over, and bars of the player around a
  // picture whose RPU has a level 5 block of zeros, or none
  struct md bars = NewMetadata();
  AddL1(&bars, 62, 1000, 3000);
  AddL5(&bars, 0, 0, 276, 276);
  struct md zeros = NewMetadata();
  AddL1(&zeros, 62, 1000, 3000);
  AddL5(&zeros, 0, 0, 0, 0);
  struct md none = NewMetadata();
  AddL1(&none, 62, 1000, 3000);
  const struct dvbridge_geometry SCOPE = {3840, 1600, 0, 280, 3840, 1600};
  static const char* names[4] = {"Hades' own", "as in the video", "not sent", "Hades' own, none made"};
  static const int forms[4] = {DVBRIDGE_L5_PLACED, DVBRIDGE_L5_AS_GIVEN, DVBRIDGE_L5_NONE, DVBRIDGE_L5_UNMADE};
  // what each form sends (-1: no block) for: bars, bars drawn over (every edge), a 3840x1600 picture placed at row 280
  // with a level 5 block of zeros, and the same without a level 5 block
  static const int want[4][4][4] = {
      {{0, 0, 276, 276}, {0, 0, 0, 0}, {0, 0, 280, 280}, {0, 0, 280, 280}},
      {{0, 0, 276, 276}, {0, 0, 276, 276}, {0, 0, 0, 0}, {-1, -1, -1, -1}},
      {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
      {{0, 0, 276, 276}, {0, 0, 0, 0}, {0, 0, 280, 280}, {-1, -1, -1, -1}},
  };
  static const unsigned margins[4][4] = {{0, 0, 276, 276}, {0, 0, 276, 276}, {0, 0, 280, 280}, {0, 0, 280, 280}};
  for (int k = 0; k < 4; ++k)
  {
    ctx = dvbridge_create();
    dvbridge_set_form(ctx, forms[k], 0);
    dvbridge_reset(ctx); // (kept through a reset: the user's)
    int ok = 1;
    char got[256];
    size_t at = 0;
    for (int s = 0; s < 4; ++s)
    {
      const struct md* d = s == 3 ? &none : s == 2 ? &zeros : &bars;
      const struct dvbridge_geometry g = s >= 2 ? SCOPE : RASTER;
      struct dvbridge_candidate* cand = dvbridge_prepare_transport(ctx, d->m, d->size, 0, g, false, 0);
      if (cand && s == 1)
        ok &= dvbridge_candidate_cover(cand, 15);
      const struct frame f = Read(cand);
      unsigned sentl5[4] = {0};
      const int blocksBack = dvbridge_sent_blocks(cand, NULL, 0, sentl5);
      dvbridge_candidate_destroy(cand);
      const int has = want[k][s][0] >= 0;
      ok &= Full(&f) && f.hasL5 == has && blocksBack >= 0 && !memcmp(f.margins, margins[s], 16);
      for (int j = 0; j < 4 && has; ++j)
        ok &= f.l5[j] == (unsigned)want[k][s][j] && sentl5[j] == (unsigned)want[k][s][j];
      ok &= has || sentl5[0] == DVBRIDGE_SENT_NO_L5;
      at += (size_t)snprintf(got + at, sizeof got - at, "%s%s", s ? "; " : "",
                             f.hasL5 ? "" : "none");
      if (f.hasL5)
        at += (size_t)snprintf(got + at, sizeof got - at, "%u %u %u %u", f.l5[0], f.l5[1], f.l5[2], f.l5[3]);
    }
    snprintf(line, sizeof line, "level 5, %s: bars of 276 / drawn over / scope with a block of zeros / without: %s; the "
             "blanking margins the film's in each", names[k], got);
    Check(ok, line);
    dvbridge_destroy(ctx);
  }
  Free(&bars);
  Free(&zeros);
  Free(&none);
  Free(&p5);
}
#endif

int main(void)
{
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("serializer test, %s bridge\n",
         "the new"
  );

  // ============================================================ 1. level 5 and the margins
  {
    const unsigned a[4] = {0, 0, 276, 276};
    Margins("a 3840x2160 picture with its bars", RASTER, 0, 0, 276, 276, a, "as before");
    const unsigned b[4] = {0, 0, 280, 280};
    Margins("a crop of exactly the bars, offsets of the master kept", (struct dvbridge_geometry){3840, 1600, 0, 280, 3840, 1600},
            0, 0, 280, 280, b, "only the place of the picture; no real row black");
    Margins("the same crop scaled down to 1920x800", (struct dvbridge_geometry){1920, 800, 0, 280, 3840, 1600}, 0, 0, 280,
            280, b, "only the place of the picture");
    const unsigned d[4] = {0, 0, 276, 276};
    Margins("a 1920x1080 downscale of a 2.39:1 master, offsets of the master", (struct dvbridge_geometry){1920, 1080, 0, 0, 3840, 2160},
            0, 0, 276, 276, d, "the bars of the master, not twice them");
    const unsigned e[4] = {0, 0, 138, 138};
    Margins("a 1920x1080 picture whose RPU is of its own size", (struct dvbridge_geometry){1920, 1080, 0, 0, 3840, 2160}, 0, 0,
            138, 138, e,
            "ambiguous with the case above: the smaller reading, half of the bars left to the picture's own black");
    Margins("a 3840x1600 crop whose RPU has bars of its own", (struct dvbridge_geometry){3840, 1600, 0, 280, 3840, 1600}, 0, 0,
            40, 40, b, "ambiguous with a crop into the picture: the smaller reading");
    const unsigned g[4] = {0, 0, 270, 270};
    Margins("a crop that left 6 rows of the bars", (struct dvbridge_geometry){3840, 1620, 0, 270, 3840, 1620}, 0, 0, 276, 276,
            g, "where it was cropped is not known: the lower bound, 0 of the picture");
    const unsigned h[4] = {0, 0, 276, 276};
    Margins("a 1280x536 encode, offsets of the master that do not fit it", (struct dvbridge_geometry){1280, 536, 0, 276, 3840, 1608},
            0, 0, 276, 276, h, "taken as those of the master instead of refusing the frame");
    const unsigned i[4] = {480, 480, 0, 0};
    Margins("a 2880x2160 crop of a pillarboxed master", (struct dvbridge_geometry){2880, 2160, 480, 0, 2880, 2160}, 480, 480, 0,
            0, i, "only the place of the picture");
    const unsigned j[4] = {0, 0, 280, 280};
    Margins("a crop without a level 5 block", (struct dvbridge_geometry){3840, 1600, 0, 280, 3840, 1600}, -1, 0, 0, 0, j,
            "the place of the picture, as before");
#ifdef DVBRIDGE_DV_L5_NO_PICTURE
    const unsigned k[4] = {0, 0, 0, 0};
    Margins("offsets that leave no picture", RASTER, 0, 0, 1080, 1080, k,
            "left aside for the video: the place of the picture, as without a level 5 block");
#else
    Margins("offsets that leave no picture", RASTER, 0, 0, 1080, 1080, NULL, "as before");
#endif
  }

  // ============================================================ 2. packets: 4 bytes to a word, up to 512 bytes
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct md d = NewMetadata();
    AddL1(&d, 62, 1000, 3000);
    AddL5(&d, 0, 0, 276, 276);
    struct frame f = Frame(ctx, &d, 0, RASTER, 1);
    char line[256];
    snprintf(line, sizeof line, "a frame with L1 and L5: %u bytes in %u packet(s), CRC and header right in %d", f.size, f.count,
             f.crcs);
    Check(Full(&f) && f.size == 95 && f.count == 1, line);
    Free(&d);
    // 21 trims of level 2: 494 bytes, five packets (the old bridge took 482 at most)
    d = NewMetadata();
    AddL1(&d, 62, 1000, 3000);
    for (int t = 0; t < 21; ++t)
      AddL2(&d, 1000 + 50 * t);
    AddL5(&d, 0, 0, 276, 276);
    f = Frame(ctx, &d, 41708, RASTER, 1);
    snprintf(line, sizeof line,
             "494 bytes of payload: %u packets, CRC right in %d, header right in %d, %u blocks read back",
             f.count, f.crcs, f.headers, f.numBlocks);
    Check(Full(&f) && f.size == 494 && f.count == 5 && f.numBlocks == 23 && f.hasL5 && f.l5[2] == 276, line);
    // + L254 + L11 = 510 bytes: still taken
    const uint8_t l254[2] = {0, 2}, l11[4] = {1, 0, 0, 0}, l4[3] = {0x12, 0x34, 0x56};
    AddRaw(&d, 254, l254, 2);
    AddRaw(&d, 11, l11, 4);
    f = Frame(ctx, &d, 2 * 41708, RASTER, 1);
    snprintf(line, sizeof line, "510 bytes of payload: %u packets, %u bytes read back", f.count, f.size);
    Check(Full(&f) && f.size == 510 && f.count == 5, line);
    // + L4: 519 bytes, more than CCM 001 allows. It was refused (and 24 such frames in 2 s took native
    // output away); now the v4.0 part (levels 254 and 11) is left out: 503 bytes, the v2.9 part whole
    AddRaw(&d, 4, l4, 3);
    f = Frame(ctx, &d, 3 * 41708, RASTER, 1);
    snprintf(line, sizeof line,
             "519 bytes with the v4.0 part: sent without it (%u bytes, %u blocks, v4.0 blocks in it: %d)", f.size,
             f.numBlocks, HasV40(&f));
    Check(Full(&f) && f.size == 503 && f.numBlocks == 24 && !HasV40(&f) && f.hasL5, line);
    Free(&d);
    // 22 trims: 513 bytes, refused
    d = NewMetadata();
    AddL1(&d, 62, 1000, 3000);
    for (int t = 0; t < 22; ++t)
      AddL2(&d, 1000 + 50 * t);
    AddL5(&d, 0, 0, 276, 276);
    f = Frame(ctx, &d, 4 * 41708, RASTER, 1);
    Check(!f.ok, "513 bytes of payload: refused");
    Free(&d);
    dvbridge_destroy(ctx);
  }

  // ============================================================ 3. reset, failures and the metadata id
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct md a = NewMetadata(), b = NewMetadata();
    AddL1(&a, 62, 1000, 3000);
    AddL1(&b, 62, 1200, 3200);
    const double P = 41708; // 23.976 frames a second, in microseconds
    dvbridge_set_frame_period(ctx, P);
    struct frame f1 = Frame(ctx, &a, 0, RASTER, 1);
    struct frame f2 = Frame(ctx, &a, P, RASTER, 1);
    struct frame f3 = Frame(ctx, &b, 2 * P, RASTER, 1);
    char line[256];
    snprintf(line, sizeof line, "ids %u %u %u, refresh %u %u %u", f1.id, f2.id, f3.id, f1.refresh, f2.refresh, f3.refresh);
    // (the payload of the second frame differs from the first in the refresh flag: a new id)
    Check(f1.ok && f2.ok && f3.ok && f1.id == 0 && f2.id == 1 && f3.id == 2 && f1.refresh == 1 && f2.refresh == 0 &&
              f3.refresh == 0,
          "the first frame refreshes; the id changes with the payload only");
    printf("      %s\n", line);
    struct frame same = Frame(ctx, &b, 3 * P, RASTER, 1);
    Check(same.ok && same.id == f3.id, "the same payload again keeps the id");
    // a frame that is prepared and not presented (not committed), and one that cannot be prepared
    struct frame lost = Frame(ctx, &a, 4 * P, RASTER, 0);
    struct md bad = NewMetadata(); // no L1: not tunnel eligible
    const uint8_t l254[2] = {0, 2};
    AddRaw(&bad, 254, l254, 2);
    struct frame refused = Frame(ctx, &bad, 4 * P, RASTER, 0);
    // the same picture drawn again and presented: it carries on from the last presented frame
    struct frame f4 = Frame(ctx, &b, 4 * P, RASTER, 1);
    snprintf(line, sizeof line, "after a frame not presented and one refused: id %u (was %u), refresh %u", f4.id, f3.id,
             f4.refresh);
    Check(lost.ok && !refused.ok && f4.ok && f4.id == f3.id && f4.refresh == 0,
          "a frame that was not presented changes nothing: the next one keeps the id and the scene (same time)");
    printf("      %s\n", line);
    // a new stream: refresh, and the id goes on (it went back to 0)
    dvbridge_reset(ctx);
    struct frame f5 = Frame(ctx, &b, 0, RASTER, 1);
    snprintf(line, sizeof line, "after a reset: id %u (the last one presented was %u), refresh %u", f5.id, f4.id, f5.refresh);
    Check(f5.ok && f5.refresh == 1 && f5.id == ((f4.id + 1) & 15),
          "a new stream refreshes the scene and takes the next id, never one the display may still hold");
    printf("      %s\n", line);
    // sixteen resets in a row: the id runs through all 16 values and back
    unsigned seen = 0, id = f5.id;
    for (int k = 0; k < 16; ++k)
    {
      dvbridge_reset(ctx);
      struct frame f = Frame(ctx, &b, 0, RASTER, 1);
      seen |= 1u << f.id;
      Check(f.ok && f.id == ((id + 1) & 15), k ? "      ... and so on" : "each reset takes the next id");
      id = f.id;
    }
    Check(seen == 0xffff, "the ids of 16 streams in a row are all 16");
    Free(&a);
    Free(&b);
    Free(&bad);
    dvbridge_destroy(ctx);
  }

  // ============================================================ 6. the gap between presented frames
  {
    const double P = 41708;
    struct md a = NewMetadata();
    AddL1(&a, 62, 1000, 3000); // scene_refresh_flag 0 in the RPU
    char line[256];
    // with the period of the stream
    struct dvbridge_context* ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, P);
    double t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    int spurious = 0;
    for (int k = 0; k < 10; ++k)
      spurious += Frame(ctx, &a, t += P, RASTER, 1).refresh;
    struct frame drop = Frame(ctx, &a, t += 2 * P, RASTER, 1); // one frame missing: 83 ms
    struct frame after = Frame(ctx, &a, t += P, RASTER, 1);
    snprintf(line, sizeof line, "24p: 10 frames in a row refresh %d times; one frame missing (83 ms): refresh %u; the next %u",
             spurious, drop.refresh, after.refresh);
    Check(!spurious && drop.refresh == 1 && after.refresh == 0,
          "at 24 frames a second a frame that was not shown refreshes the scene (the 0.1 s rule did not)");
    printf("      %s\n", line);
    // the same at 60 frames a second
    dvbridge_destroy(ctx);
    ctx = dvbridge_create();
    const double Q = 16683;
    dvbridge_set_frame_period(ctx, Q);
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    spurious = 0;
    for (int k = 0; k < 10; ++k)
      spurious += Frame(ctx, &a, t += Q, RASTER, 1).refresh;
    drop = Frame(ctx, &a, t += 2 * Q, RASTER, 1);
    Check(!spurious && drop.refresh == 1, "... and at 60 frames a second (33 ms)");
    dvbridge_destroy(ctx);
    // timestamps of a 23.976 stream in milliseconds (41 and 42 ms): no refresh
    ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, P);
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    spurious = 0;
    for (int k = 1; k <= 48; ++k)
      spurious += Frame(ctx, &a, floor(k * P / 1000) * 1000, RASTER, 1).refresh;
    Check(!spurious, "timestamps rounded to milliseconds do not refresh");
    dvbridge_destroy(ctx);
    // a frame rate that is half of the real one (period too short): never a refresh of its own making
    ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, P / 2);
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    spurious = 0;
    for (int k = 0; k < 40; ++k)
      spurious += Frame(ctx, &a, t += P, RASTER, 1).refresh;
    snprintf(line, sizeof line, "a period half of the real one: %d of 40 frames refreshed", spurious);
    Check(!spurious, "a period that the gaps do not bear out is not taken (the 0.1 s rule holds)");
    printf("      %s\n", line);
    dvbridge_destroy(ctx);
    // a period twice the real one (too long): the 0.1 s rule still refreshes after 125 ms
    ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, 2 * P);
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    spurious = 0;
    for (int k = 0; k < 10; ++k)
      spurious += Frame(ctx, &a, t += P, RASTER, 1).refresh;
    struct frame g125 = Frame(ctx, &a, t += 3 * P, RASTER, 1);
    Check(!spurious && g125.refresh == 1, "a period that is too long does not take away a refresh of the 0.1 s rule");
    dvbridge_destroy(ctx);
    // frames missing all the time (every gap two frames): no refresh of its own making, as before
    ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, P);
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    spurious = 0;
    for (int k = 0; k < 40; ++k)
      spurious += Frame(ctx, &a, t += 2 * P, RASTER, 1).refresh;
    snprintf(line, sizeof line, "every second frame missing: %d of 40 frames refreshed", spurious);
    Check(spurious == 0, "with frames missing all the time the period is not taken (as before: no refresh)");
    printf("      %s\n", line);
    dvbridge_destroy(ctx);
    // the price: a frame missing in the very first gap of a stream goes by the 0.1 s rule
    ctx = dvbridge_create();
    dvbridge_set_frame_period(ctx, P);
    Frame(ctx, &a, 0, RASTER, 1);
    struct frame first = Frame(ctx, &a, 2 * P, RASTER, 1);
    Check(first.refresh == 0, "(known limit) a frame missing in the first gap of a stream does not refresh");
    dvbridge_destroy(ctx);
    // without a period: the 0.1 s rule, as before
    ctx = dvbridge_create();
    t = 0;
    Frame(ctx, &a, t, RASTER, 1);
    struct frame d83 = Frame(ctx, &a, t += 2 * P, RASTER, 1);
    struct frame d125 = Frame(ctx, &a, t += 3 * P, RASTER, 1);
    Check(d83.refresh == 0 && d125.refresh == 1, "without a period: a gap of more than 0.1 s refreshes, as before");
    // a repeat keeps the flag of the frame repeated; going back refreshes
    struct frame rep = Frame(ctx, &a, t, RASTER, 1);
    struct frame back = Frame(ctx, &a, t - P, RASTER, 1);
    Check(rep.refresh == d125.refresh && back.refresh == 1, "a repeated frame keeps its flag; a frame earlier than the last refreshes");
    dvbridge_destroy(ctx);
    Free(&a);
  }

  // ============================================================ 4. level 11
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct
    {
      uint8_t b[4];
      int taken;
      const char* what;
    } cases[] = {
        {{1, 0x00, 0, 0}, 1, "content type 1, whitepoint 0"},
        {{1, 0x01, 0, 0}, 1, "whitepoint 1 (bits 0-3 of byte 1, as dovi_tool has it)"},
        {{1, 0x07, 0, 0}, 1, "whitepoint 7"},
        {{1, 0x18, 0, 0}, 1, "reference mode (bit 4) and whitepoint 8"},
        {{2, 0xe0, 0, 0}, 1, "bits 5-7 of byte 1"},
        {{1, 0x00, 0x5a, 0}, 1, "byte 2 not 0 (free)"},
        // (these two refused the frame, and 24 refused frames in 2 s took native output away: now the v4.0
        // part, which level 11 is of, is left out of the frame, and the frame is sent)
        {{1, 0x00, 0, 1}, 0, "byte 3 not 0: not sent, and the frame is sent without the v4.0 part"},
        {{16, 0x00, 0, 0}, 0, "content type 16: not sent, and the frame is sent without the v4.0 part"},
    };
    for (unsigned k = 0; k < sizeof(cases) / sizeof(*cases); ++k)
    {
      struct md d = NewMetadata();
      AddL1(&d, 62, 1000, 3000);
      AddRaw(&d, 11, cases[k].b, 4);
      // (one that is not taken is not committed: the next case sees the stream as it was)
      struct frame f = Frame(ctx, &d, k * 41708, RASTER, cases[k].taken);
      char line[256];
      snprintf(line, sizeof line, "level 11 %02x %02x %02x %02x, %s", cases[k].b[0], cases[k].b[1], cases[k].b[2],
               cases[k].b[3], cases[k].what);
      Check(cases[k].taken ? Full(&f) && f.hasL11 && !memcmp(f.l11, cases[k].b, 4) : Full(&f) && !f.hasL11, line);
      Free(&d);
    }
    dvbridge_destroy(ctx);
  }

  // ============================================================ 5. reserved levels
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct md plain = NewMetadata(), with = NewMetadata();
    AddL1(&plain, 62, 1000, 3000);
    AddL1(&with, 62, 1000, 3000);
    Add(&with, 7);
    Add(&with, 12);
    Add(&with, 200);
    struct frame a = Frame(ctx, &plain, 0, RASTER, 0);
    struct frame b = Frame(ctx, &with, 0, RASTER, 0);
    Check(a.ok && b.ok && a.size == b.size && !memcmp(a.payload, b.payload, a.size),
          "blocks of reserved levels (7, 12, 200) are left out, not a reason to refuse the frame: the payload is the "
          "one without them");
    struct dvbridge_candidate* c = dvbridge_prepare(ctx, with.m, with.size, 0, RASTER, false);
    struct dvbridge_reading reading;
    Check(c && dvbridge_candidate_reading(c, &reading) && reading.skipped == 3 && reading.skipped_level == 7,
          "... and the reading says how many (3) and the first level (7)");
    dvbridge_candidate_destroy(c);
    Free(&plain);
    Free(&with);
    dvbridge_destroy(ctx);
  }

  // ============================================================ 7. readable, and eligible for the tunnel
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct md d = NewMetadata(); // no extension blocks at all
    d.m->ext_block_offset = d.size + 4096; // where nothing is: not read when there are no blocks
    struct dvbridge_color color;
    struct dvbridge_hdr10_static fixed;
    const int readable = dvbridge_dv_readable(d.m, d.size);
    const int mapped = dvbridge_map_color(&color, d.m, d.size, false, NULL);
    const int hdr10 = dvbridge_hdr10_static(&fixed, d.m, d.size);
    struct frame f = Frame(ctx, &d, 0, RASTER, 0);
    char line[256];
    snprintf(line, sizeof line, "an RPU without extension blocks: readable %d, mapped %d, HDR10 levels %d (%u cd/m2), tunnel %d",
             readable, mapped, hdr10, hdr10 ? fixed.max_luminance : 0, f.ok);
    Check(readable && mapped && hdr10 && !f.ok,
          "an RPU without display management blocks can be reconstructed (output as HDR10) and is not sent natively");
    printf("      %s\n", line);
    d.m->num_ext_blocks = 1;
    Check(!dvbridge_dv_readable(d.m, d.size), "... while a block where the metadata does not reach is still refused");
    d.m->num_ext_blocks = -1;
    Check(!dvbridge_dv_readable(d.m, d.size), "... and so is a negative number of blocks");
    Free(&d);
    dvbridge_destroy(ctx);
  }

  // ============================================================ 8. a v4.0 part that cannot be carried
  // It was: the frame refused (and 24 refused frames in 2 s take native output away for the title). Now: the
  // v2.9 part alone, the same bytes as the metadata without the v4.0 part, for the rest of the stream.
  {
    const double P = 41708;
    struct
    {
      int level;
      uint8_t bytes[32];
      unsigned length;
      int kept; // the decoder kept its bytes (it does up to 32)
      const char* why; // a word of the reason
      const char* what;
    } cases[] = {
        {8, {1, 0x11, 0x22}, 11, 1, "length", "a level 8 block of 11 bytes (a length that dovi_tool does not know)"},
        {8, {1, 0x11, 0x22}, 26, 1, "length", "a level 8 block of 26 bytes"},
        {8, {0}, 40, 0, "not kept", "a level 8 block of 40 bytes, whose bytes the decoder does not keep"},
        {3, {0x80, 0x08, 0x00, 0x80, 0x01}, 5, 1, "length", "a level 3 block whose padding is not 0"},
        {10, {1, 0x40, 0x00, 0x80, 7}, 21, 1, "length", "a level 10 block of 21 bytes without primaries"},
        {9, {3}, 17, 1, "length", "a level 9 block of 17 bytes without primaries"},
        {254, {0, 2, 0}, 3, 1, "length", "a level 254 block of 3 bytes"},
    };
    for (unsigned k = 0; k < sizeof(cases) / sizeof(*cases); ++k)
    {
      struct md bad = NewMetadata(), plain = NewMetadata();
      AddL1(&bad, 62, 1000, 3000);
      AddL2(&bad, 2081);
      AddL5(&bad, 0, 0, 276, 276);
      if (cases[k].kept)
        AddRaw(&bad, cases[k].level, cases[k].bytes, cases[k].length);
      else
      {
        AVDOVIDmData* e = Add(&bad, cases[k].level);
        e->dvbridge_original_length = cases[k].length; // (and no magic: FFmpeg kept no bytes)
      }
      AddRaw(&bad, 11, L11, 4); // a good block of the v4.0 part, after the one that is not
      AddL1(&plain, 62, 1000, 3000);
      AddL2(&plain, 2081);
      AddL5(&plain, 0, 0, 276, 276);
      struct dvbridge_context* ctx = dvbridge_create();
      struct dvbridge_context* ref = dvbridge_create();
      struct frame f = Frame(ctx, &bad, 0, RASTER, 0);
      struct frame want = Frame(ref, &plain, 0, RASTER, 0);
      char line[512];
      snprintf(line, sizeof line, "%s: the frame is sent (%u bytes) with the v2.9 part only (v4.0 blocks: %d), "
               "the bytes of the metadata without the v4.0 part", cases[k].what, f.size, HasV40(&f));
      Check(Full(&f) && !HasV40(&f) && SamePayload(&f, &want), line);
      struct dvbridge_candidate* c = dvbridge_prepare(ctx, bad.m, bad.size, 0, RASTER, false);
      struct dvbridge_reading r;
      const int read = c && dvbridge_candidate_reading(c, &r);
      snprintf(line, sizeof line, "... and the reading says so: %u blocks left out, a level %u block of %u bytes: %s",
               read ? r.cm40_dropped : 0, read ? r.cm40_level : 0, read ? r.cm40_length : 0,
               read && r.cm40_why ? r.cm40_why : "(no reason)");
      Check(read && r.cm40_dropped == 2 && r.cm40_level == (unsigned)cases[k].level &&
                r.cm40_length == cases[k].length && r.cm40_why && strstr(r.cm40_why, cases[k].why),
            line);
      dvbridge_candidate_destroy(c);
      dvbridge_destroy(ctx);
      dvbridge_destroy(ref);
      Free(&bad);
      Free(&plain);
    }

    // dovi_tool's most blocks: 583 bytes on the wire, more than the 512 of CCM 001
    {
      struct dvbridge_context* ctx = dvbridge_create();
      struct dvbridge_context* ref = dvbridge_create();
      struct md all = Maxima(1), v29 = Maxima(0);
      struct frame f = Frame(ctx, &all, 0, RASTER, 1);
      struct frame want = Frame(ref, &v29, 0, RASTER, 1);
      char line[512];
      snprintf(line, sizeof line, "dovi_tool's most blocks (583 bytes with the v4.0 part): sent with the v2.9 part, "
               "%u bytes in %u packets, %u blocks (the same as without the v4.0 part: %d)", f.size, f.count, f.numBlocks,
               SamePayload(&f, &want));
      Check(Full(&f) && f.size == 256 && f.numBlocks == 11 && !HasV40(&f) && SamePayload(&f, &want), line);
      struct dvbridge_candidate* c = dvbridge_prepare(ctx, all.m, all.size, P, RASTER, false);
      struct dvbridge_reading r;
      const int read = c && dvbridge_candidate_reading(c, &r);
      // (the frame before was committed: the part is left out from it on, all 13 blocks of it here)
      snprintf(line, sizeof line, "... the reading: %u blocks left out; the block that showed it: level %u, %u bytes: %s",
               read ? r.cm40_dropped : 0, read ? r.cm40_level : 0, read ? r.cm40_length : 0,
               read && r.cm40_why ? r.cm40_why : "(no reason)");
      Check(read && r.cm40_dropped == 13 && r.cm40_level == 10 && r.cm40_length == 21 && r.cm40_why &&
                strstr(r.cm40_why, "512"),
            line);
      dvbridge_candidate_destroy(c);
      // the same metadata in a stream whose frames had it all before: the first that does not fit takes the
      // whole v4.0 part out (none of its blocks stays), and the v2.9 part is never touched
      Free(&all);
      Free(&v29);
      dvbridge_destroy(ctx);
      dvbridge_destroy(ref);
    }

    // a usual v4.0 part (five level 2, three level 8 of 12 bytes, levels 3, 9, 11 and 254): sent as before
    {
      struct dvbridge_context* ctx = dvbridge_create();
      struct md d = NewMetadata();
      AddL1(&d, 62, 1000, 3000);
      for (int t = 0; t < 5; ++t)
        AddL2(&d, 1000 + 300 * t);
      AddL5(&d, 0, 0, 276, 276);
      AddRaw(&d, 3, L3, 5);
      for (int t = 0; t < 3; ++t)
        AddL8(&d, (uint8_t)(t + 1), 12);
      AddRaw(&d, 9, L9_1, 1);
      AddRaw(&d, 11, L11, 4);
      AddRaw(&d, 254, L254, 2);
      struct frame f = Frame(ctx, &d, 0, RASTER, 1);
      char line[256];
      snprintf(line, sizeof line, "a usual v4.0 part: sent with it, %u bytes, %u blocks", f.size, f.numBlocks);
      Check(Full(&f) && HasV40(&f) && f.numBlocks == 14 && f.size == 283, line);
      Free(&d);
      dvbridge_destroy(ctx);
    }

    // for the rest of the stream: a frame whose v4.0 part could be carried is sent without it, until a reset
    {
      struct dvbridge_context* ctx = dvbridge_create();
      dvbridge_set_frame_period(ctx, P);
      struct md bad = NewMetadata(), good = NewMetadata();
      AddL1(&bad, 62, 1000, 3000);
      AddL5(&bad, 0, 0, 276, 276);
      const uint8_t l8[11] = {1};
      AddRaw(&bad, 8, l8, 11);
      AddL1(&good, 62, 1000, 3000);
      AddL5(&good, 0, 0, 276, 276);
      AddRaw(&good, 11, L11, 4);
      struct frame f1 = Frame(ctx, &bad, 0, RASTER, 1);
      struct frame f2 = Frame(ctx, &good, P, RASTER, 1);
      struct frame f3 = Frame(ctx, &good, 2 * P, RASTER, 1);
      Check(f1.ok && f2.ok && f3.ok && !HasV40(&f2) && !HasV40(&f3) && !f2.hasL11,
            "after a frame whose v4.0 part could not be carried, the next ones of the stream go without it too "
            "(no switching between the two mappings)");
      struct dvbridge_candidate* c = dvbridge_prepare(ctx, good.m, good.size, 3 * P, RASTER, false);
      struct dvbridge_reading r;
      const int read = c && dvbridge_candidate_reading(c, &r);
      Check(read && r.cm40_dropped == 1 && r.cm40_level == 8 && r.cm40_length == 11 && r.cm40_why,
            "... their reading says that their v4.0 part is left out, and of which block it began");
      dvbridge_candidate_destroy(c);
      dvbridge_reset(ctx);
      struct frame f4 = Frame(ctx, &good, 0, RASTER, 1);
      Check(f4.ok && HasV40(&f4) && f4.hasL11, "a new stream (a reset) sends its v4.0 part again");
      Free(&bad);
      Free(&good);
      dvbridge_destroy(ctx);
    }

    // a frame that is not presented (not committed) changes nothing: the next one carries its v4.0 part
    {
      struct dvbridge_context* ctx = dvbridge_create();
      struct md bad = NewMetadata(), good = NewMetadata();
      AddL1(&bad, 62, 1000, 3000);
      const uint8_t l8[11] = {1};
      AddRaw(&bad, 8, l8, 11);
      AddL1(&good, 62, 1000, 3000);
      AddRaw(&good, 11, L11, 4);
      struct frame lost = Frame(ctx, &bad, 0, RASTER, 0);
      struct frame next = Frame(ctx, &good, 0, RASTER, 1);
      Check(lost.ok && next.ok && HasV40(&next),
            "a frame not presented leaves the stream as it was: the next frame sends its v4.0 part");
      Free(&bad);
      Free(&good);
      dvbridge_destroy(ctx);
    }

    // a display of display management 2: the v4.0 part is left out for it, as before, and not as "dropped"
    {
      struct dvbridge_context* ctx = dvbridge_create();
      dvbridge_set_display_management(ctx, 2);
      struct md bad = NewMetadata();
      AddL1(&bad, 62, 1000, 3000);
      const uint8_t l8[11] = {1};
      AddRaw(&bad, 8, l8, 11);
      AddRaw(&bad, 11, L11, 4);
      struct dvbridge_candidate* c = dvbridge_prepare(ctx, bad.m, bad.size, 0, RASTER, false);
      struct dvbridge_reading r;
      const int read = c && dvbridge_candidate_reading(c, &r);
      Check(read && r.cm40_left_out == 2 && r.cm40_dropped == 0,
            "for a display of display management 2 the v4.0 part is left out for the display, not dropped");
      dvbridge_candidate_destroy(c);
      Free(&bad);
      dvbridge_destroy(ctx);
    }

    // level 4 is of the v2.9 part: one that cannot be carried still refuses the frame (it is never dropped)
    {
      struct dvbridge_context* ctx = dvbridge_create();
      struct md d = NewMetadata();
      AddL1(&d, 62, 1000, 3000);
      const uint8_t l4[2] = {1, 2};
      AddRaw(&d, 4, l4, 2);
      AddRaw(&d, 11, L11, 4);
      struct frame f = Frame(ctx, &d, 0, RASTER, 0);
      Check(!f.ok, "a level 4 block of 2 bytes (of the v2.9 part) still refuses the frame");
      Free(&d);
      dvbridge_destroy(ctx);
    }
  }

  // ============================================================ 9. each refusal says why
  // (kodi.log writes it, CDVBridgeGLES::LogRefusal(); it was: no word of which check failed)
  {
    struct dvbridge_context* ctx = dvbridge_create();
    struct md d;
#define CASE(build, what, word)                                                                                  \
  do                                                                                                             \
  {                                                                                                              \
    d = NewMetadata();                                                                                           \
    build;                                                                                                       \
    Refused(what, ctx, &d, RASTER, word);                                                                        \
    Free(&d);                                                                                                    \
  } while (0)
    CASE((AddL1(&d, 62, 1000, 3000), AddL1(&d, 62, 1000, 3000), AddL5(&d, 0, 0, 0, 0)), "two level 1 blocks",
         "two level 1 blocks");
    CASE((AddL1(&d, 62, 1000, 3000), AddL1(&d, 62, 1000, 3000)), "two level 1 blocks and no level 5",
         "two level 1 blocks");
    CASE(AddL1(&d, 3000, 1000, 62), "a level 1 block whose minimum is above its maximum", "minimum is above");
    CASE((AddL1(&d, 62, 1000, 3000), AddL2(&d, 2081), AddL2(&d, 2081)), "two level 2 blocks of one target",
         "same target");
    CASE((AddL1(&d, 62, 1000, 3000), AddL5(&d, 0, 0, 0, 0), AddL5(&d, 0, 0, 10, 10)), "two level 5 blocks",
         "two level 5 blocks");
    CASE(AddL5(&d, 0, 0, 0, 0), "a level 5 block without a level 1 block", "without a level 1 block");
    CASE(AddL2(&d, 2081), "no level 1 block", "no level 1 block");
#ifdef DVBRIDGE_DV_L5_NO_PICTURE
    {
      d = NewMetadata();
      AddL1(&d, 62, 1000, 3000);
      AddL5(&d, 0, 0, 1080, 1080);
      const char* why = Why(ctx, &d, RASTER);
      char line[512];
      snprintf(line, sizeof line,
               "level 5 offsets that leave no picture: made, not refused (left aside for the video): \"%s\"",
               why ? why : "(made)");
      Check(why == NULL, line);
      Free(&d);
    }
#else
    CASE((AddL1(&d, 62, 1000, 3000), AddL5(&d, 0, 0, 1080, 1080)), "level 5 offsets that leave no picture",
         "leave no picture");
#endif
    CASE((AddL1(&d, 62, 1000, 3000), ({
            for (int t = 0; t < 22; ++t)
              AddL2(&d, 1000 + 50 * t);
          }),
          AddL5(&d, 0, 0, 276, 276)),
         "22 level 2 blocks (513 bytes)", "over 512");
    CASE((AddL1(&d, 62, 1000, 3000), av_dovi_get_color(d.m)->signal_eotf = 0), "a signal EOTF of 0",
         "signal_eotf");
    CASE((AddL1(&d, 62, 1000, 3000), av_dovi_get_color(d.m)->signal_color_space = 1), "signal colour space 1",
         "signal_color_space");
    CASE((AddL1(&d, 62, 1000, 3000), av_dovi_get_color(d.m)->signal_eotf_param0 = 7), "a parameter of the EOTF",
         "parameters of the signal EOTF");
    CASE((AddL1(&d, 62, 1000, 3000), av_dovi_get_color(d.m)->source_min_pq = 4000), "a source display upside down",
         "source display");
    CASE((void)0, "an RPU without extension blocks", "no display management blocks");
    CASE((AddL1(&d, 62, 1000, 3000), AddRaw(&d, 4, (const uint8_t[]){1, 2}, 2)), "a level 4 block of 2 bytes",
         "level 4 block of a length");
    CASE(AddL1(&d, 0, 1000, 5000), "a level 1 maximum of 5000", "over 12 bits");
    CASE((AddL1(&d, 62, 1000, 3000), av_dovi_get_header(d.m)->disable_residual_flag = 0),
         "a FEL residual that the picture was not drawn with", "residual of the enhancement layer");
    CASE((AddL1(&d, 62, 1000, 3000), d.m->num_ext_blocks = 40), "more blocks than FFmpeg has room for",
         "do not read as FFmpeg");
#undef CASE
    d = NewMetadata();
    AddL1(&d, 62, 1000, 3000);
    Refused("a picture drawn at an odd column", ctx, &d, (struct dvbridge_geometry){1920, 1080, 1, 0, 1920, 1080},
            "odd column");
    Refused("a picture not drawn at its own shape", ctx, &d, (struct dvbridge_geometry){1920, 1080, 0, 0, 3840, 1600},
            "own shape");
    Check(Why(ctx, &d, RASTER) == NULL, "a frame that is made has no reason");
    Free(&d);
    dvbridge_destroy(ctx);
  }

  // ============================================================ 10. the level 5 block made for the raster
  // (where the RPU has none) goes where an RPU's own goes: at the end of the v2.9 part, before the v4.0 part.
  // It went after the v4.0 part.
  {
    struct
    {
      struct dvbridge_geometry g;
      const char* what;
    } cases[] = {{RASTER, "the raster"},
                 {(struct dvbridge_geometry){3840, 1600, 0, 280, 3840, 1600}, "a 3840x1600 picture at row 280"}};
    for (unsigned k = 0; k < sizeof(cases) / sizeof(*cases); ++k)
    {
      const unsigned top = cases[k].g.y, bottom = 2160 - cases[k].g.y - cases[k].g.height;
      struct md made = NewMetadata(), own = NewMetadata();
      AddL1(&made, 62, 1000, 3000);
      AddRaw(&made, 3, L3, 5);
      AddRaw(&made, 11, L11, 4);
      AddL1(&own, 62, 1000, 3000);
      AddL5(&own, 0, 0, 0, 0); // (offsets of the picture: the margins are its place)
      AddRaw(&own, 3, L3, 5);
      AddRaw(&own, 11, L11, 4);
      struct dvbridge_context* a = dvbridge_create();
      struct dvbridge_context* b = dvbridge_create();
      struct frame fm = Frame(a, &made, 0, cases[k].g, 1);
      struct frame fo = Frame(b, &own, 0, cases[k].g, 1);
      char line[512];
      snprintf(line, sizeof line, "%s, no level 5 block and a v4.0 part: blocks sent %u %u %u %u, the level 5 "
               "block %u %u %u %u, the bytes of an RPU whose own level 5 block is the picture's place: %s", cases[k].what,
               fm.blocks[0], fm.blocks[1], fm.blocks[2], fm.blocks[3], fm.l5[0], fm.l5[1], fm.l5[2], fm.l5[3],
               SamePayload(&fm, &fo) ? "yes" : "no");
      if (top || bottom)
        Check(Full(&fm) && fm.numBlocks == 4 && fm.blocks[0] == 1 && fm.blocks[1] == 5 && fm.blocks[2] == 3 &&
                  fm.blocks[3] == 11 && fm.l5[2] == top && fm.l5[3] == bottom && SamePayload(&fm, &fo),
              line);
      else // (the raster, no level 5 block in the RPU: none is sent, and the v4.0 part follows level 1)
        Check(Full(&fm) && fm.numBlocks == 3 && fm.blocks[0] == 1 && fm.blocks[1] == 3 && fm.blocks[2] == 11 &&
                  !fm.hasL5 && fo.hasL5 && !SamePayload(&fm, &fo),
              line);
      Free(&made);
      Free(&own);
      dvbridge_destroy(a);
      dvbridge_destroy(b);
    }
    // without a v4.0 part, or for a display of display management 2 (which is sent none), it is the last block,
    // as it was
    struct md v29 = NewMetadata();
    AddL1(&v29, 62, 1000, 3000);
    AddL2(&v29, 2081);
    struct dvbridge_context* c = dvbridge_create();
    struct frame f = Frame(c, &v29, 0, (struct dvbridge_geometry){3840, 1600, 0, 280, 3840, 1600}, 1);
    Check(Full(&f) && f.numBlocks == 3 && f.blocks[2] == 5 && f.l5[2] == 280,
          "without a v4.0 part the made level 5 block (of the bars of a picture placed with them) is the last");
    // (no Frame(), which would add a BYTES line; Read() reads the candidate the same way)
    struct dvbridge_candidate* c2 = dvbridge_prepare(c, v29.m, v29.size, 41708, RASTER, false);
    struct frame f2 = Read(c2);
    dvbridge_candidate_destroy(c2);
    Check(Full(&f2) && f2.numBlocks == 2 && !f2.hasL5,
          "... and on the raster, without a level 5 block in the RPU, none is made");
    Free(&v29);
    dvbridge_destroy(c);
  }

#ifdef DVBRIDGE_DV_L5_NO_PICTURE
  // ============================================================ 10. level 5 offsets that fit no reading (left aside for the video)
  // (no Frame(): it would add BYTES lines; Read() reads the candidate the same way)
  {
    printf("level 5 offsets that fit no reading: left aside for the video (the bridge defines DVBRIDGE_DV_L5_NO_PICTURE)\n");
    struct dvbridge_context* a = dvbridge_create();
    struct dvbridge_context* b = dvbridge_create();
    struct md wrong = NewMetadata();
    AddL1(&wrong, 62, 1000, 3000);
    AddL5(&wrong, 0, 0, 1080, 1080);
    struct md none = NewMetadata();
    AddL1(&none, 62, 1000, 3000);
    AddL5(&none, 0, 0, 0, 0); // (the RPU has a level 5 block: the one sent is the place's, as for offsets 0)
    struct md fit = NewMetadata();
    AddL1(&fit, 62, 1000, 3000);
    AddL5(&fit, 0, 0, 276, 276);
    char line[640];
    // the first frame: made, the reading says the offsets were left aside and why, and keeps the offsets given
    struct dvbridge_candidate* c = dvbridge_prepare(a, wrong.m, wrong.size, 0, RASTER, false);
    struct frame fw = Read(c);
    struct dvbridge_reading r;
    memset(&r, 0, sizeof r);
    const int hasReading = c && dvbridge_candidate_reading(c, &r);
    const unsigned zero[4] = {0, 0, 0, 0}, given[4] = {0, 0, 1080, 1080};
    snprintf(line, sizeof line,
             "fit no reading: the frame is made (%d), margins %u %u %u %u and level 5 block %u %u %u %u (want 0 0 0 0), "
             "the reading keeps the offsets %u %u %u %u and says they were left aside: \"%s\"",
             fw.ok, fw.margins[0], fw.margins[1], fw.margins[2], fw.margins[3], fw.l5[0], fw.l5[1], fw.l5[2], fw.l5[3],
             r.l5[0], r.l5[1], r.l5[2], r.l5[3], r.l5_ignored ? r.l5_ignored : "(not said)");
    Check(Full(&fw) && fw.hasL5 && !memcmp(fw.margins, zero, 16) && !memcmp(fw.l5, zero, 16) && hasReading &&
              !memcmp(r.l5, given, 16) && !memcmp(r.margins, zero, 16) && r.l5_ignored &&
              strstr(r.l5_ignored, "fit no reading") != NULL,
          line);
    if (c && !dvbridge_commit(a, c))
      Check(0, "fit no reading: the first frame commits");
    dvbridge_candidate_destroy(c);
    // byte for byte the frame of the same metadata with a level 5 block of the place of the picture (the frame
    // without a level 5 block sent the place too where the bridge made one; it sends none now)
    c = dvbridge_prepare(b, none.m, none.size, 0, RASTER, false);
    struct frame fn = Read(c);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line,
             "fit no reading: the payload is that of the frame with a level 5 block of the place (sizes %u and %u, the same: %s)",
             fw.size, fn.size, SamePayload(&fw, &fn) ? "yes" : "no");
    Check(SamePayload(&fw, &fn), line);
    // for the rest of the video: a later frame with the same offsets, the same
    c = dvbridge_prepare(a, wrong.m, wrong.size, 1000000, RASTER, false);
    struct frame fw2 = Read(c);
    memset(&r, 0, sizeof r);
    const int hasReading2 = c && dvbridge_candidate_reading(c, &r);
    if (c && !dvbridge_commit(a, c))
      fw2.ok = 0;
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line,
             "fit no reading: a later frame of the video with them, made the same (%d, margins %u %u %u %u, said: %s)",
             fw2.ok, fw2.margins[0], fw2.margins[1], fw2.margins[2], fw2.margins[3], r.l5_ignored ? "yes" : "no");
    Check(Full(&fw2) && !memcmp(fw2.margins, zero, 16) && hasReading2 && r.l5_ignored != NULL &&
              SamePayload(&fw2, &fn),
          line);
    // other offsets in that video are read as their own (the decision is of one value of the offsets, 4166f50)
    c = dvbridge_prepare(a, fit.m, fit.size, 2000000, RASTER, false);
    struct frame ff = Read(c);
    memset(&r, 0, sizeof r);
    const int hasReading3 = c && dvbridge_candidate_reading(c, &r);
    dvbridge_candidate_destroy(c);
    const unsigned bars[4] = {0, 0, 276, 276};
    snprintf(line, sizeof line,
             "fit no reading: offsets that fit, later in that video, are taken (margins %u %u %u %u, want 0 0 276 276; "
             "left aside: %s)",
             ff.margins[0], ff.margins[1], ff.margins[2], ff.margins[3], r.l5_ignored ? "yes" : "no");
    Check(Full(&ff) && !memcmp(ff.margins, bars, 16) && !memcmp(ff.l5, bars, 16) && hasReading3 && !r.l5_ignored,
          line);
    Free(&wrong);
    Free(&none);
    Free(&fit);
    dvbridge_destroy(a);
    dvbridge_destroy(b);
  }
#endif

#ifdef DVBRIDGE_DV_L5_OF_PICTURE
  // ============================================================ 11. the level 5 offsets of an HD video read as its own
  // Where the measurement of native output found the band between the two readings of an HD picture black
  // (dvbridge_l5check.h), the video's offsets are read as offsets of its picture (dvbridge_l5_of_picture()): the
  // display is sent the whole bars. A 1920x1080 picture placed on the 3840x2160 raster, offsets 0 0 138 138 (2.39:1
  // bars of its own size): 138 sent without the decision (the smaller reading, as section 1), 276 with it, the payload
  // byte for byte that of a 3840x2160 picture with offsets 276; taken back, 138 again; of the video only (another
  // video reads its offsets as before), through dvbridge_reset(); never for a 3840x2160 picture (one reading: the
  // same bytes either way); and only where the offsets fit the picture as its own.
  // (no Frame(): it would add BYTES lines; Read() reads the candidate the same way)
  {
    printf("level 5 offsets of an HD video read as its own, where measured (the bridge defines DVBRIDGE_DV_L5_OF_PICTURE)\n");
    const struct dvbridge_geometry HD = {1920, 1080, 0, 0, 3840, 2160};
    struct md hd = NewMetadata();
    AddL1(&hd, 62, 1000, 3000);
    AddL5(&hd, 0, 0, 138, 138);
    struct md uhd = NewMetadata();
    AddL1(&uhd, 62, 1000, 3000);
    AddL5(&uhd, 0, 0, 276, 276);
    const unsigned half[4] = {0, 0, 138, 138}, whole[4] = {0, 0, 276, 276};
    char line[640];
    struct dvbridge_reading r;
    // read as before: 138
    struct dvbridge_context* a = dvbridge_create();
    struct dvbridge_candidate* c = dvbridge_prepare(a, hd.m, hd.size, 0, HD, false);
    struct frame f0 = Read(c);
    memset(&r, 0, sizeof r);
    int has = c && dvbridge_candidate_reading(c, &r);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line,
             "HD of its own, not measured: level 5 block %u %u %u %u (want 0 0 138 138), read as its own: %d (want 0)",
             f0.l5[0], f0.l5[1], f0.l5[2], f0.l5[3], r.l5_picture);
    Check(Full(&f0) && f0.hasL5 && !memcmp(f0.l5, half, 16) && !memcmp(f0.margins, half, 16) && has &&
              !r.l5_picture && r.l5_master,
          line);
    // measured: 276, the reading says so, and the payload is that of the UHD picture with its bars of 276
    dvbridge_l5_of_picture(a, 0, true);
    c = dvbridge_prepare(a, hd.m, hd.size, 0, HD, false);
    struct frame f1 = Read(c);
    memset(&r, 0, sizeof r);
    has = c && dvbridge_candidate_reading(c, &r);
    dvbridge_candidate_destroy(c);
    struct dvbridge_context* u = dvbridge_create();
    c = dvbridge_prepare(u, uhd.m, uhd.size, 0, RASTER, false);
    struct frame fu = Read(c);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line,
             "HD of its own, measured: level 5 block %u %u %u %u and margins %u %u %u %u (want 0 0 276 276), read as its "
             "own: %d, from the master: %d; the payload of the UHD picture with bars of 276: %s",
             f1.l5[0], f1.l5[1], f1.l5[2], f1.l5[3], f1.margins[0], f1.margins[1], f1.margins[2], f1.margins[3],
             r.l5_picture, r.l5_master, SamePayload(&f1, &fu) ? "yes" : "no");
    Check(Full(&f1) && f1.hasL5 && !memcmp(f1.l5, whole, 16) && !memcmp(f1.margins, whole, 16) && has &&
              r.l5_picture && !r.l5_master && SamePayload(&f1, &fu),
          line);
    // kept through a reset (it is of the video), and not for another video
    dvbridge_reset(a);
    c = dvbridge_prepare(a, hd.m, hd.size, 0, HD, false);
    struct frame f2 = Read(c);
    dvbridge_candidate_destroy(c);
    dvbridge_set_cm40_video(a, 7, false, NULL);
    c = dvbridge_prepare(a, hd.m, hd.size, 1000000, HD, false);
    struct frame f3 = Read(c);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line,
             "HD of its own, measured for its video: after a reset %u %u (want 276 276); another video %u %u (want 138 138)",
             f2.l5[2], f2.l5[3], f3.l5[2], f3.l5[3]);
    Check(Full(&f2) && !memcmp(f2.l5, whole, 16) && Full(&f3) && !memcmp(f3.l5, half, 16), line);
    // taken back for its video: 138 again
    dvbridge_set_cm40_video(a, 0, false, NULL);
    dvbridge_l5_of_picture(a, 0, false);
    c = dvbridge_prepare(a, hd.m, hd.size, 2000000, HD, false);
    struct frame f4 = Read(c);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line, "HD of its own, taken back: level 5 block %u %u %u %u (want 0 0 138 138)", f4.l5[0],
             f4.l5[1], f4.l5[2], f4.l5[3]);
    Check(Full(&f4) && !memcmp(f4.l5, half, 16), line);
    // a UHD picture: one reading, byte for byte the same with the decision
    struct dvbridge_context* v = dvbridge_create();
    dvbridge_l5_of_picture(v, 0, true);
    c = dvbridge_prepare(v, uhd.m, uhd.size, 0, RASTER, false);
    struct frame fv = Read(c);
    memset(&r, 0, sizeof r);
    has = c && dvbridge_candidate_reading(c, &r);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line, "UHD with the decision: the same payload as without it (%s), read as its own said: %d (want 0)",
             SamePayload(&fv, &fu) ? "yes" : "no", r.l5_picture);
    Check(SamePayload(&fv, &fu) && has && !r.l5_picture, line);
    // offsets that do not fit the picture as its own (a 1280x536 encode with the master's 276): the master's reading
    // still, decided or not
    const struct dvbridge_geometry small = {1280, 536, 0, 276, 3840, 1608};
    c = dvbridge_prepare(v, uhd.m, uhd.size, 1000000, small, false);
    struct frame fs = Read(c);
    dvbridge_candidate_destroy(c);
    snprintf(line, sizeof line, "offsets that do not fit the picture as its own, with the decision: %u %u (want 276 276)",
             fs.l5[2], fs.l5[3]);
    Check(Full(&fs) && !memcmp(fs.l5, whole, 16), line);
    Free(&hd);
    Free(&uhd);
    dvbridge_destroy(a);
    dvbridge_destroy(u);
    dvbridge_destroy(v);
  }
#endif

#ifdef DVBRIDGE_REFRESH_ALL
  // ============================================================ 12. every frame the first picture of a scene
  // (a comparison on a display, off by default): scene_refresh_flag 1 in every frame whatever the RPU says, the rest
  // of the payload as without it; a scene whose metadata do not change keeps its metadata id; kept by a reset; off,
  // the flag of the RPU again
  {
    const double P = 41708;
    struct md a = NewMetadata(), b = NewMetadata();
    AddL1(&a, 62, 1000, 3000); // scene_refresh_flag 0 in the RPU
    AddL2(&a, 2081);
    AddL5(&a, 0, 0, 276, 276);
    AddL1(&b, 62, 1200, 3200);
    char line[256];
    struct dvbridge_context* plain = dvbridge_create();
    struct dvbridge_context* all = dvbridge_create();
    dvbridge_set_frame_period(plain, P);
    dvbridge_set_frame_period(all, P);
    dvbridge_set_refresh_all(all, true);
    double t = 0;
    struct frame p0 = Frame(plain, &a, t, RASTER, 1);
    struct frame a0 = Frame(all, &a, t, RASTER, 1);
    int ones = 0, zeros = 0, sameRest = 1;
    unsigned id = a0.id;
    int idKept = 1;
    for (int k = 0; k < 10; ++k)
    {
      t += P;
      struct frame p = Frame(plain, &a, t, RASTER, 1);
      struct frame f = Frame(all, &a, t, RASTER, 1);
      ones += Full(&f) && f.refresh == 1;
      zeros += Full(&p) && p.refresh == 0;
      // the payload with the switch is the one without it but for the flag (byte 1)
      sameRest = sameRest && p.size == f.size && !memcmp(p.payload, f.payload, 1) &&
                 !memcmp(p.payload + 2, f.payload + 2, f.size - 2);
      idKept = idKept && f.id == id;
    }
    snprintf(line, sizeof line, "with the switch: %d of 10 frames whose RPU says 0 sent with 1; without it: %d of 10 with 0",
             ones, zeros);
    Check(Full(&a0) && Full(&p0) && a0.refresh == 1 && ones == 10 && zeros == 10, line);
    Check(sameRest, "the payload with the switch is the one without it, but for scene_refresh_flag");
    Check(idKept, "a scene whose metadata do not change keeps its metadata id with the switch (the payload is the same)");
    // other metadata: a new id, and the flag 1 still
    struct frame nb = Frame(all, &b, t += P, RASTER, 1);
    Check(Full(&nb) && nb.refresh == 1 && nb.id == ((id + 1) & 15), "... new metadata take the next id, sent with 1");
    // a repeat keeps 1
    struct frame rep = Frame(all, &b, t, RASTER, 1);
    Check(Full(&rep) && rep.refresh == 1 && rep.id == nb.id, "a repeated frame keeps the flag 1 and the id");
    // a new stream keeps the switch
    dvbridge_reset(all);
    Frame(all, &a, 0, RASTER, 1);
    struct frame r1 = Frame(all, &a, P, RASTER, 1);
    snprintf(line, sizeof line, "after a reset: the second frame of the stream sent with %u (want 1)", r1.refresh);
    Check(Full(&r1) && r1.refresh == 1, line);
    // off again: the RPU's flag from the next frame on
    dvbridge_set_refresh_all(all, false);
    struct frame off = Frame(all, &a, 2 * P, RASTER, 1);
    snprintf(line, sizeof line, "the switch off again: the next frame sent with %u (want the RPU's 0)", off.refresh);
    Check(Full(&off) && off.refresh == 0, line);
    printf("      %s\n", line);
    Free(&a);
    Free(&b);
    dvbridge_destroy(plain);
    dvbridge_destroy(all);
  }
#endif

#ifdef DVBRIDGE_FORM
  FormTests();
#endif

  printf("%d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
