// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: an independent reader of the Dolby Vision tunnel, written from the text
// of ETSI GS CCM 001 V1.1.1 (2017-02), clauses 6.2 to 6.4 (a plain-text copy of the PDF: "CCM l.N"
// below), and from nothing of the bridge. It takes the packed pixels of a transport (3840x2160 RGBA8, as a test
// reads them back) and says whether they carry what the clause asks a source to send:
//   - the bits of each packet in the LSB of the chroma samples, in raster order, MSB of each byte first,
//     i = P*3072 + copy*1024 + n*8 + (7 - m) (CCM l.1465-1476, 1500-1533, 1548-1550), unscrambled with the parity
//     of chroma bits 11..1 and of the luma bits 11..0 of the same pixel (CCM l.1477-1499, table 14);
//   - three copies of every packet, each with a good CRC-32 (ISO/IEC 13818-1 annex A: polynomial 0x04C11DB7,
//     initial value all ones, no reflection, no final inversion) over its first 992 bits in its tail, and the
//     three the same bytes (CCM l.1418-1428, 1500-1539);
//   - the header of every packet: packet_type in sequence (0b00 alone; 0b01, 0b10..., 0b11), metadata_type 0,
//     metadata_version 0, no_md 0, affected_metadata_id = current_metadata_id (the metadata of the picture that
//     carries them) and the same in every packet, the reserved bits of byte 2 zero, EOS as asked (CCM l.1302-1369);
//   - the body: the length (N <= 119 alone, 119 < L <= 0x2F00 split), 119 bytes in the first packet, 121 in the
//     others, M in the last, and every trailing byte 0x00 (CCM l.1372-1417);
//   - the structure (table 3, CCM l.981-1024): the reserved bytes 0x00 / 0xFF 0xFF / eight 0x00 / 0x01 0x01 /
//     0x00 0x2A at their places, scene_refresh_flag 0 or 1, signal_color_space 0 or 2 (table 6), source PQ
//     within 12 bits, num_ext_blocks <= 254, and the blocks (tables 4, 5 and 7): lengths 6, 14 and 8 for levels 1,
//     2 and 5, their values within their ranges (ms_weight all ones when unspecified), no two level 2 blocks for
//     one target, the order of levels 1, 2 and 5 (CCM l.1171-1178), the active area within the picture
//     (CCM l.1245-1280), and the blocks filling the length exactly;
//   - outside the active area of the level 5 block, black: every luma sample and chroma bits 11..1 the black
//     that the YCCtoRGB offsets of the structure give.
// What the clause does not say and the reader takes as given:
//   - the container: a pixel of the 12-bit YCbCr 4:2:2 signal in the three bytes of an RGB888 pixel, R = C[11:4],
//     G = Y[11:4], B = Y[3:0] | C[3:0] << 4 (the layout of the HDMI tunnel that the bridge writes,
//     pack_gles300.frag; this is the one thing the reader takes from Hades);
//   - the scale of the YCCtoRGB offsets: 2^28 is the whole range of a code (the denominator of the offsets of the
//     RPU, and the convention of the transport as Hades and PGenerator send it); the clause gives no unit, and its
//     defaults [2^26, 2^29, 2^29] are four times that convention (offset_shift = 30 reads them so).
// Levels that table 7 reserves are what CM v4.0 sends to a display of display management 4 or later (3, 4, 8 to
// 11, 254, outside the clause); with allow_cm40 they are counted and their lengths taken as they are, else any
// reserved level fails.
#ifndef HADES_CCM001_ORACLE_H
#define HADES_CCM001_ORACLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CCM_ORACLE_MAX_PACKETS 96 /* 0x2F00 bytes split: 1 + ceil((12032 - 119) / 121) = 100; 96 * 3072 < 3840 * 2160 / 28 */
#define CCM_ORACLE_MAX_LENGTH 0x2F00

struct ccm_oracle_opts
{
  int width, height;   /* of the transport: 3840 x 2160 */
  int bottom_up;       /* row 0 of the buffer is the bottom row of the picture (glReadPixels) */
  int allow_cm40;      /* levels of CM v4.0 (3, 4, 8-11, 254) are counted, not failed */
  int eos;             /* the EOS bit that every packet must carry (0 or 1) */
  int depth;           /* signal_bit_depth that the structure must carry (0: 8 to 16) */
  int offset_shift;    /* 28 (Hades, PGenerator, the RPU) or 30 (the defaults of table 3) */
  int no_bars;         /* do not check the area outside the level 5 block */
};

struct ccm_oracle_result
{
  int ok;
  char why[200];
  int packets, length;
  int current_id, scene_refresh, color_space, depth;
  int blocks, l1, l2, l5, cm40;
  unsigned l5_offsets[4];  /* left, right, top, bottom */
  int black[3];
  long bar_pixels;         /* pixels outside the active area that were checked */
  uint8_t structure[CCM_ORACLE_MAX_LENGTH];
};

static inline void ccm_oracle_defaults(struct ccm_oracle_opts *o)
{
  memset(o, 0, sizeof(*o));
  o->width = 3840;
  o->height = 2160;
  o->bottom_up = 1;
  o->allow_cm40 = 1;
  o->offset_shift = 28;
}

/* CRC-32 of ISO/IEC 13818-1 annex A, bit by bit as its model shifts it */
static inline uint32_t ccm_oracle_crc(const uint8_t *p, size_t n)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++)
    for (int b = 7; b >= 0; b--)
    {
      const uint32_t in = (uint32_t)(p[i] >> b) & 1u;
      const uint32_t top = crc >> 31;
      crc <<= 1;
      if (top ^ in)
        crc ^= 0x04C11DB7u;
    }
  return crc;
}

static inline unsigned ccm_oracle_parity(unsigned v)
{
  unsigned p = 0;
  while (v)
  {
    p ^= v & 1u;
    v >>= 1;
  }
  return p;
}

/* the 12-bit luma and chroma samples of pixel (x, y) of the picture (row 0 at the top) */
static inline void ccm_oracle_sample(const uint8_t *rgba, const struct ccm_oracle_opts *o, int x, int y,
                                     unsigned *Y, unsigned *C)
{
  const int row = o->bottom_up ? o->height - 1 - y : y;
  const uint8_t *px = rgba + ((size_t)row * (size_t)o->width + (size_t)x) * 4;
  *Y = (unsigned)px[1] << 4 | (px[2] & 15u);
  *C = (unsigned)px[0] << 4 | (unsigned)px[2] >> 4;
}

static inline int ccm_oracle_fail(struct ccm_oracle_result *r, const char *what, long a, long b)
{
  if (r->ok)
    snprintf(r->why, sizeof(r->why), "%s (%ld, %ld)", what, a, b);
  r->ok = 0;
  return 0;
}

/* packet P, copy k: its 128 bytes */
static inline void ccm_oracle_packet(const uint8_t *rgba, const struct ccm_oracle_opts *o, int P, int k,
                                     uint8_t out[128])
{
  memset(out, 0, 128);
  for (int n = 0; n < 128; n++)
    for (int m = 7; m >= 0; m--)
    {
      const long i = (long)P * 3072 + (long)k * 1024 + n * 8 + (7 - m);
      unsigned Y, C;
      ccm_oracle_sample(rgba, o, (int)(i % o->width), (int)(i / o->width), &Y, &C);
      const unsigned bit = (C & 1u) ^ ccm_oracle_parity(C >> 1) ^ ccm_oracle_parity(Y);
      out[n] |= (uint8_t)(bit << m);
    }
}

static inline unsigned ccm_oracle_u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static inline unsigned long ccm_oracle_u32(const uint8_t *p)
{
  return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static inline int ccm_oracle_cm40_level(unsigned level)
{
  return level == 3 || level == 4 || (level >= 8 && level <= 11) || level == 254;
}

/* Reads the transport; returns r->ok (1: everything as the clause says) */
static inline int ccm_oracle_read(const uint8_t *rgba, const struct ccm_oracle_opts *o, struct ccm_oracle_result *r)
{
  memset(r, 0, sizeof(*r));
  r->ok = 1;
  static uint8_t copies[3][128], packet[128];
  int expected = 1, length = -1;
  for (int P = 0; P < expected; P++)
  {
    if (P >= CCM_ORACLE_MAX_PACKETS || (long)(P + 1) * 3072 > (long)o->width * o->height)
      return ccm_oracle_fail(r, "more packets than the picture holds", P, expected);
    for (int k = 0; k < 3; k++)
    {
      ccm_oracle_packet(rgba, o, P, k, copies[k]);
      /* the CRC of the first 992 bits is the tail; over all 128 bytes the remainder is then zero */
      const uint32_t crc = ccm_oracle_crc(copies[k], 124);
      if (crc != (uint32_t)ccm_oracle_u32(&copies[k][124]))
        return ccm_oracle_fail(r, "a copy of a packet has a CRC that is not its own (packet, copy)", P, k);
      if (ccm_oracle_crc(copies[k], 128) != 0)
        return ccm_oracle_fail(r, "the CRC over the whole packet is not zero (packet, copy)", P, k);
    }
    if (memcmp(copies[0], copies[1], 128) || memcmp(copies[0], copies[2], 128))
      return ccm_oracle_fail(r, "the three copies of a packet differ (packet, 0)", P, 0);
    memcpy(packet, copies[0], 128);
    const unsigned type = packet[0] >> 6, mtype = (packet[0] >> 4) & 3u, version = (packet[0] >> 1) & 7u,
                   no_md = packet[0] & 1u;
    const unsigned affected = packet[1] >> 4, current = packet[1] & 15u;
    if (mtype || version || no_md)
      return ccm_oracle_fail(r, "metadata_type, metadata_version or no_md is not 0 (packet, byte 0)", P, packet[0]);
    if (affected != current)
      return ccm_oracle_fail(r, "affected_metadata_id is not current_metadata_id (affected, current)", affected,
                             current);
    if (P == 0)
      r->current_id = (int)current;
    else if ((int)current != r->current_id)
      return ccm_oracle_fail(r, "a packet of the picture has another current_metadata_id (packet, id)", P, current);
    if ((packet[2] >> 1) != 0)
      return ccm_oracle_fail(r, "the reserved bits of header byte 2 are not 0 (packet, byte)", P, packet[2]);
    if ((int)(packet[2] & 1u) != o->eos)
      return ccm_oracle_fail(r, "EOS is not what was asked (packet, EOS)", P, packet[2] & 1u);
    /* packet_type and the body */
    int start = 3, room = 121;
    if (P == 0)
    {
      length = (int)ccm_oracle_u16(&packet[3]);
      start = 5;
      room = 119;
      if (type == 0)
      {
        if (length > 119)
          return ccm_oracle_fail(r, "a single packet with more than 119 bytes (type, N)", type, length);
        expected = 1;
      }
      else if (type == 1)
      {
        if (length <= 119 || length > CCM_ORACLE_MAX_LENGTH)
          return ccm_oracle_fail(r, "a first packet whose L is not within 119 < L <= 0x2F00 (type, L)", type, length);
        expected = 1 + (length - 119 + 120) / 121;
      }
      else
        return ccm_oracle_fail(r, "the first packet is neither 0b00 nor 0b01 (packet, type)", P, type);
    }
    else
    {
      const unsigned want = P + 1 == expected ? 3u : 2u;
      if (type != want)
        return ccm_oracle_fail(r, "packet_type out of sequence (packet, type)", P, type);
    }
    const int done = P == 0 ? 0 : 119 + (P - 1) * 121;
    int take = length - done;
    if (take > room)
      take = room;
    if (take < 0)
      take = 0;
    memcpy(r->structure + done, packet + start, (size_t)take);
    for (int j = start + take; j < 124; j++)
      if (packet[j])
        return ccm_oracle_fail(r, "a trailing byte is not 0x00 (packet, byte)", P, j);
  }
  r->packets = expected;
  r->length = length;
  /* past the last packet the chroma LSB is the picture's own: nothing to read there */

  /* the structure: table 3 */
  const uint8_t *s = r->structure;
  if (length < 71)
    return ccm_oracle_fail(r, "the structure is shorter than its 71 fixed bytes (length, 71)", length, 71);
  if (s[0] != 0x00)
    return ccm_oracle_fail(r, "reserved_0x00 at byte 0", 0, s[0]);
  if (s[1] > 1)
    return ccm_oracle_fail(r, "scene_refresh_flag is not 0 or 1 (byte 1)", 1, s[1]);
  r->scene_refresh = s[1];
  /* 2..19 YCCtoRGB_coef (any Short), 20..31 YCCtoRGB_offset, 32..49 RGBtoLMS_coef */
  for (int c = 0; c < 3; c++)
  {
    const unsigned long off = ccm_oracle_u32(&s[20 + 4 * c]);
    const unsigned long long code = ((unsigned long long)off * 4096u + (1ull << (o->offset_shift - 1))) >> o->offset_shift;
    if (code > 4095)
      return ccm_oracle_fail(r, "a YCCtoRGB offset is not a code of 12 bits in the convention asked (component, code)",
                             c, (long)code);
    r->black[c] = (int)code;
  }
  if (s[50] != 0xFF || s[51] != 0xFF)
    return ccm_oracle_fail(r, "reserved_0xff at bytes 50-51", s[50], s[51]);
  for (int j = 52; j < 60; j++)
    if (s[j] != 0x00)
      return ccm_oracle_fail(r, "reserved_0x00 at bytes 52-59 (byte, value)", j, s[j]);
  r->depth = s[60];
  if (o->depth ? s[60] != o->depth : (s[60] < 8 || s[60] > 16))
    return ccm_oracle_fail(r, "signal_bit_depth (value, asked)", s[60], o->depth);
  r->color_space = s[61];
  if (s[61] != 0 && s[61] != 2)
    return ccm_oracle_fail(r, "signal_color_space is a reserved value (table 6)", 61, s[61]);
  if (s[62] != 0x01 || s[63] != 0x01)
    return ccm_oracle_fail(r, "reserved_0x01 at bytes 62-63", s[62], s[63]);
  if (ccm_oracle_u16(&s[64]) > 4095 || ccm_oracle_u16(&s[66]) > 4095)
    return ccm_oracle_fail(r, "source_min_PQ or source_max_PQ beyond 12 bits", ccm_oracle_u16(&s[64]),
                           ccm_oracle_u16(&s[66]));
  if (s[68] != 0x00 || s[69] != 0x2A)
    return ccm_oracle_fail(r, "reserved_0x00 and reserved_0x2a at bytes 68-69", s[68], s[69]);
  const int blocks = s[70];
  if (blocks > 254)
    return ccm_oracle_fail(r, "num_ext_blocks beyond 254", blocks, 254);
  r->blocks = blocks;

  /* the blocks: tables 4, 5 and 7, and the order of CCM l.1171-1178 */
  int pos = 71, l12_since_l5 = 0, l12_after_last_l5 = 0;
  unsigned targets[256];
  int ntargets = 0;
  for (int b = 0; b < blocks; b++)
  {
    if (pos + 5 > length)
      return ccm_oracle_fail(r, "a block's header runs past the structure (block, position)", b, pos);
    const unsigned long blen = ccm_oracle_u32(&s[pos]);
    const unsigned level = s[pos + 4];
    pos += 5;
    if (blen > (unsigned long)(length - pos))
      return ccm_oracle_fail(r, "a block runs past the structure (block, length)", b, (long)blen);
    const uint8_t *v = &s[pos];
    if (level == 1)
    {
      if (blen != 6)
        return ccm_oracle_fail(r, "a level 1 block whose length is not 6 (block, length)", b, (long)blen);
      for (int j = 0; j < 3; j++)
        if (ccm_oracle_u16(&v[2 * j]) > 4095)
          return ccm_oracle_fail(r, "a level 1 value beyond 12 bits (block, value)", b, ccm_oracle_u16(&v[2 * j]));
      r->l1++;
      l12_since_l5++;
      l12_after_last_l5 = 1;
    }
    else if (level == 2)
    {
      if (blen != 14)
        return ccm_oracle_fail(r, "a level 2 block whose length is not 14 (block, length)", b, (long)blen);
      for (int j = 0; j < 6; j++)
        if (ccm_oracle_u16(&v[2 * j]) > 4095)
          return ccm_oracle_fail(r, "a level 2 value beyond 12 bits (block, value)", b, ccm_oracle_u16(&v[2 * j]));
      const unsigned ms = ccm_oracle_u16(&v[12]);
      if ((v[12] & 0x80) ? ms != 0xFFFF : ms > 4095)
        return ccm_oracle_fail(r, "ms_weight neither all ones (unspecified) nor within 12 bits (block, value)", b, ms);
      for (int j = 0; j < ntargets; j++)
        if (targets[j] == ccm_oracle_u16(&v[0]))
          return ccm_oracle_fail(r, "two level 2 blocks for one target_max_PQ (block, target)", b, targets[j]);
      targets[ntargets++] = ccm_oracle_u16(&v[0]);
      r->l2++;
      l12_since_l5++;
      l12_after_last_l5 = 1;
    }
    else if (level == 5)
    {
      if (blen != 8)
        return ccm_oracle_fail(r, "a level 5 block whose length is not 8 (block, length)", b, (long)blen);
      if (!l12_since_l5)
        return ccm_oracle_fail(r, "a level 5 block not preceded by a level 1 or 2 block since the last one (block, 5)",
                               b, 5);
      unsigned off[4];
      for (int j = 0; j < 4; j++)
      {
        off[j] = ccm_oracle_u16(&v[2 * j]);
        if (off[j] > 8191)
          return ccm_oracle_fail(r, "an active area offset beyond 8191 (block, value)", b, off[j]);
      }
      if ((long)off[0] + off[1] > o->width - 1 || (long)off[2] + off[3] > o->height - 1)
        return ccm_oracle_fail(r, "the active area is empty: its bottom right is above or left of its top left", b,
                               (long)off[0] + off[1]);
      memcpy(r->l5_offsets, off, sizeof(off));
      r->l5++;
      l12_since_l5 = 0;
      l12_after_last_l5 = 0;
    }
    else if (o->allow_cm40 && ccm_oracle_cm40_level(level))
      r->cm40++;
    else
      return ccm_oracle_fail(r, "a block of a level that table 7 reserves (block, level)", b, level);
    pos += (int)blen;
  }
  if (pos != length)
    return ccm_oracle_fail(r, "the blocks do not fill the structure exactly (end of the blocks, length)", pos, length);
  if (r->l5 && l12_after_last_l5)
    return ccm_oracle_fail(r, "a level 1 or 2 block after the last level 5 block", r->l1, r->l2);

  /* black outside the active area */
  if (r->l5 && !o->no_bars)
  {
    const int x0 = (int)r->l5_offsets[0], x1 = o->width - 1 - (int)r->l5_offsets[1];
    const int y0 = (int)r->l5_offsets[2], y1 = o->height - 1 - (int)r->l5_offsets[3];
    for (int y = 0; y < o->height; y++)
    {
      const int bar_row = y < y0 || y > y1;
      for (int x = 0; x < o->width; x++)
      {
        if (!bar_row && x >= x0 && x <= x1)
        {
          x = x1; /* (the row's active part: on to its right bar) */
          continue;
        }
        unsigned Y, C;
        ccm_oracle_sample(rgba, o, x, y, &Y, &C);
        const unsigned want = (unsigned)r->black[(x & 1) ? 2 : 1];
        /* the chroma sample of a pair of pixels (4:2:2: Cb on the even one, Cr on the odd one) that the edge
         * of the active area divides is the pair's, not the bar's: there only the luma is black */
        const int partner = x ^ 1;
        const int split = !bar_row && partner >= x0 && partner <= x1;
        if (Y != (unsigned)r->black[0] || (!split && (C >> 1) != (want >> 1)))
        {
          char what[96];
          snprintf(what, sizeof(what), "not black outside the active area at (%d, %d): luma, chroma", x, y);
          return ccm_oracle_fail(r, what, Y, C);
        }
        r->bar_pixels++;
      }
    }
  }
  return r->ok;
}

/* A line for a log: what the transport carries */
static inline void ccm_oracle_describe(const struct ccm_oracle_result *r, char *out, size_t size)
{
  snprintf(out, size,
           "%s: %d packet%s, %d bytes, id %d, scene refresh %d, depth %d, colour space %d, blocks %d (L1 %d L2 %d "
           "L5 %d v4.0 %d), active area %u %u %u %u, black %d %d %d, %ld bar pixels black%s%s",
           r->ok ? "CCM 001 transport" : "NOT A CCM 001 TRANSPORT", r->packets, r->packets == 1 ? "" : "s", r->length,
           r->current_id, r->scene_refresh, r->depth, r->color_space, r->blocks, r->l1, r->l2, r->l5, r->cm40,
           r->l5_offsets[0], r->l5_offsets[1], r->l5_offsets[2], r->l5_offsets[3], r->black[0], r->black[1],
           r->black[2], r->bar_pixels, r->ok ? "" : ": ", r->ok ? "" : r->why);
}

#endif
