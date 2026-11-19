// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of LibreELEC Hades' rule for the enhancement layer of a MEL stream (tools/dvbridge/dvbridge_mel.h)
// in Kodi's enhancement-layer module (tools/dvbridge/dvbridge_fel.c, the file itself), with the real profile 7
// test streams, decoded in software.
//
// The RPUs of the stream can be rewritten, packet by packet, before the base decoder and the module see them:
// their NLQ made the one of a MEL (offset 0, vdr_in_max 1, slope and threshold 0), or another, or their residual
// disabled. The rest of each RPU stays bit for bit (its CRC is made again), and FFmpeg's own parser, which the
// base decoder runs, reads what was written: the test checks that, and that the module's reading of each RPU
// says what libplacebo's mapping of FFmpeg's reading says (the bridge's rule, pl_map_dovi_metadata()).
//
// Usage: mel_test <file> <schedule> [tracks] [off] [seek=<packet>:<packet>] [limit=<packets>] [start=<packet>]
//   start=N:  the file starts with packet N: the packets before it are not played
//   schedule: fel (the stream as it is), mel (every RPU a MEL one), aligned (MEL, FEL from the third random-
//             access picture of the layer to the fifth, MEL again), midgop (MEL, FEL from three access units
//             after the third random-access picture to the fifth, MEL again), cross-<v> (the first 120 packets
//             with variant v: mel, offset, slope, vdr, disable; for the reading only)
//   tracks:   the file is a transport stream of two tracks, joined as Kodi joins them (CDVLayerMerge)
//   off:      the module decodes every enhancement access unit (what it did before)
//   seek=a:b: after packet a, a seek to the packet b, as Kodi's: both decoders flushed, the layer reset
// Writes MEL_OUT (if set): one line per picture, "pts result layer-hash metadata-hash".
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/bswap.h>
#include <libavutil/crc.h>
#include <libavutil/dovi_meta.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "merge_shim.h"

// The module's decoder: its calls are timed (the work of the enhancement layer).
static double g_elSeconds;
static unsigned g_elSent, g_elFrames;
static int TimedSend(AVCodecContext* c, const AVPacket* p);
static int TimedReceive(AVCodecContext* c, AVFrame* f);
#define avcodec_send_packet TimedSend
#define avcodec_receive_frame TimedReceive
// The module only takes VAAPI surfaces from its decoder. Here the decoder runs in software.
#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI
#undef avcodec_send_packet
#undef avcodec_receive_frame

static double Now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}
static int TimedSend(AVCodecContext* c, const AVPacket* p)
{
  const double t = Now();
  const int r = avcodec_send_packet(c, p);
  g_elSeconds += Now() - t;
  g_elSent += p && r >= 0;
  return r;
}
static int TimedReceive(AVCodecContext* c, AVFrame* f)
{
  const double t = Now();
  const int r = avcodec_receive_frame(c, f);
  g_elSeconds += Now() - t;
  g_elFrames += r >= 0;
  return r;
}

static int g_failures;
static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

#define PACKETS_MAX 16384
#define PICTURES_MAX 16384

// ---- the RPU, rewritten ----

enum variant { V_ASIS, V_MEL, V_OFFSET, V_SLOPE, V_VDR, V_DISABLE };

struct bitwriter
{
  uint8_t* data;
  size_t cap, bit;
};
static void Put(struct bitwriter* w, uint64_t v, unsigned n)
{
  for (unsigned i = n; i-- > 0; ++w->bit)
  {
    if (w->bit / 8 >= w->cap)
      return;
    if (!(w->bit & 7))
      w->data[w->bit / 8] = 0;
    w->data[w->bit / 8] |= (uint8_t)(((v >> i) & 1) << (7 - (w->bit & 7)));
  }
}
static void PutUe(struct bitwriter* w, uint64_t v)
{
  unsigned n = 0;
  while ((v + 1) >> (n + 1))
    ++n;
  Put(w, 0, n);
  Put(w, v + 1, n + 1);
}
static void Copy(struct bitwriter* w, const uint8_t* src, size_t from, size_t to)
{
  for (size_t b = from; b < to; ++b)
    Put(w, src[b / 8] >> (7 - (b & 7)) & 1, 1);
}

// The position after the fields of the DM metadata that come before its first byte alignment (FFmpeg's
// ff_dovi_rpu_parse() and parse_ext_blocks()): from there on everything is the same bit for bit wherever it
// starts on a byte boundary, so only what comes before needs to be written again.
static size_t DmAligned(const uint8_t* rpu, size_t size, size_t from, int dmPresent, int dmCompression)
{
  struct dvbridge_rpu_bits b = {.data = rpu, .size = size, .bit = from};
  if (dmPresent)
  {
    dvbridge_rpu_bits_ue(&b);
    dvbridge_rpu_bits_ue(&b);
    dvbridge_rpu_bits_ue(&b);
    if (!dmCompression) // the color metadata: 509 bits
      for (int i = 0; i < 144 + 96 + 144 + 48 + 32 + 5 + 2 + 2 + 2 + 12 + 12 + 10; ++i)
        dvbridge_rpu_bits_get(&b, 1);
    dvbridge_rpu_bits_ue(&b); // num_ext_blocks, then the alignment
  }
  return b.over ? 0 : b.bit;
}

// Rewrites the RPU unit (with its two header bytes, emulation prevention in it) into out. Returns its size, 0
// when it is left as it is (an RPU that uses a mapping sent before has no NLQ of its own).
static size_t Rewrite(const uint8_t* unit, size_t size, enum variant v, uint8_t* out, size_t cap, unsigned* asIs)
{
  static uint8_t raw[DVBRIDGE_RPU_BYTES], made[DVBRIDGE_RPU_BYTES];
  if (v == V_ASIS || size > DVBRIDGE_RPU_BYTES)
    return 0;
  size_t n = dvbridge_rpu_unescape(unit, size, raw);
  // the reading of the module, from its own state: where the fields are
  struct dvbridge_rpu_nlq scratch = {0};
  struct dvbridge_rpu_where w;
  const enum dvbridge_rpu_residual read = dvbridge_rpu_nlq_read(&scratch, raw, n, &w);
  if (read == DVBRIDGE_RPU_UNREAD || w.nlq_start == w.nlq_end || !w.fixed)
  {
    if (!*asIs)
      printf("note: an RPU is left as it is: read %d, NLQ bits %zu to %zu, fixed point %d, %zu bytes\n", (int)read,
             w.nlq_start, w.nlq_end, w.fixed, n);
    ++*asIs;
    return 0;
  }
  const uint8_t* rpu = raw + 1; // after the prefix 25, as the positions are
  size_t len = n - 1;
  while (len && !rpu[len - 1])
    --len;
  if (len < 6 || rpu[len - 1] != 0x80)
  {
    if (!*asIs)
      printf("note: an RPU is left as it is: it ends with %02x (%zu bytes)\n", len ? rpu[len - 1] : 0, len);
    ++*asIs;
    return 0;
  }
  const size_t crcAt = len - 5;
  // the method and the pivots of the NLQ come before the curves: where, the reading does not say; they are
  // the 3 + 2 * bl_bit_depth bits that end where num_x_partitions_minus1 begins, which is found by reading
  // the header again up to there (only the disabling needs them out)
  const size_t aligned = DmAligned(rpu, len, w.nlq_end, w.dm_present, w.dm_compression);
  if (!aligned)
  {
    if (!*asIs)
      printf("note: an RPU is left as it is: its DM metadata cannot be read\n");
    ++*asIs;
    return 0;
  }
  struct bitwriter o = {made, sizeof(made), 0};
  if (v == V_DISABLE)
  {
    Copy(&o, rpu, 0, w.disable_bit);
    Put(&o, 1, 1);
    Copy(&o, rpu, w.disable_bit + 1, w.method_start);
    Copy(&o, rpu, w.method_start + 3 + 2 * w.bl_bit_depth, w.nlq_start);
  }
  else
  {
    Copy(&o, rpu, 0, w.nlq_start);
    for (int c = 0; c < 3; ++c)
    {
      Put(&o, v == V_OFFSET && c == 0 ? 1 : 0, w.el_bit_depth); // nlq_offset
      PutUe(&o, v == V_VDR ? 2 : 1);                            // vdr_in_max: 1 (or 2)
      Put(&o, 0, w.denom);
      PutUe(&o, 0); // slope: 0 (or 2^-denom in the last component)
      Put(&o, v == V_SLOPE && c == 2 ? 1 : 0, w.denom);
      PutUe(&o, 0); // threshold: 0
      Put(&o, 0, w.denom);
    }
  }
  Copy(&o, rpu, w.nlq_end, aligned);
  while (o.bit & 7)
    Put(&o, 0, 1);
  const size_t from = (aligned + 7) / 8;
  for (size_t i = from; i < crcAt; ++i)
    Put(&o, rpu[i], 8);
  const uint32_t crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, made, o.bit / 8);
  Put(&o, av_bswap32(crc), 32);
  Put(&o, 0x80, 8);
  const size_t madeSize = o.bit / 8;
  // the unit: its header, the prefix, and the RPU with emulation prevention
  size_t k = 0, zeros = 0;
  if (cap < 3)
    return 0;
  out[k++] = unit[0];
  out[k++] = unit[1];
  out[k++] = 25;
  zeros = 0;
  for (size_t i = 0; i < madeSize && k + 2 < cap; ++i)
  {
    if (zeros >= 2 && made[i] <= 3)
    {
      out[k++] = 3;
      zeros = 0;
    }
    out[k++] = made[i];
    zeros = made[i] ? 0 : zeros + 1;
  }
  return k;
}

struct collect
{
  const uint8_t** s;
  size_t* z;
  unsigned* n;
};
static bool Collect(const uint8_t* u, size_t z, void* ctx)
{
  struct collect* cc = ctx;
  if (*cc->n == 1024)
    return false;
  cc->s[*cc->n] = u;
  cc->z[(*cc->n)++] = z;
  return true;
}

// A packet with its RPU unit(s) rewritten. lengthSize: of its framing (0: start codes).
static int RewritePacket(AVPacket* packet, int lengthSize, enum variant v, unsigned* asIs, unsigned* done)
{
  if (v == V_ASIS)
    return 1;
  static uint8_t unit[DVBRIDGE_RPU_BYTES * 2];
  uint8_t* out = av_malloc(packet->size * 2 + 4096 + AV_INPUT_BUFFER_PADDING_SIZE);
  if (!out)
    return 0;
  size_t k = 0;
  const uint8_t *p = packet->data, *end = packet->data + packet->size;
  // the units as dvbridge_rpu_units() finds them, framed again as they came
  static const uint8_t* starts[1024];
  static size_t sizes[1024];
  unsigned count = 0;
  struct collect c = {starts, sizes, &count};
  dvbridge_rpu_units(p, (size_t)(end - p), lengthSize, Collect, &c);
  for (unsigned i = 0; i < count; ++i)
  {
    const uint8_t* u = starts[i];
    size_t z = sizes[i];
    if (z > 2 && (u[0] >> 1 & 0x3f) == 62)
    {
      const size_t made = Rewrite(u, z, v, unit, sizeof(unit), asIs);
      if (made)
      {
        u = unit;
        z = made;
        ++*done;
      }
    }
    if (lengthSize)
      for (int b = lengthSize - 1; b >= 0; --b)
        out[k++] = (uint8_t)(z >> (8 * b));
    else
    {
      out[k++] = 0;
      out[k++] = 0;
      out[k++] = 0;
      out[k++] = 1;
    }
    memcpy(out + k, u, z);
    k += z;
  }
  memset(out + k, 0, AV_INPUT_BUFFER_PADDING_SIZE);
  AVBufferRef* buf = av_buffer_create(out, k + AV_INPUT_BUFFER_PADDING_SIZE, av_buffer_default_free, NULL, 0);
  if (!buf)
    return 0;
  av_buffer_unref(&packet->buf);
  packet->buf = buf;
  packet->data = out;
  packet->size = (int)k;
  return 1;
}

// ---- the stream: what a first pass finds ----

struct unitinfo
{
  int64_t pts;
  int layer, layerKey; // has enhancement units; one of them is of a random-access picture
};
static struct unitinfo g_units[PACKETS_MAX];
static unsigned g_numUnits;
static unsigned g_layerKeys[256], g_numLayerKeys;

static bool LayerScan(const uint8_t* u, size_t z, void* ctx)
{
  struct unitinfo* info = ctx;
  if (z >= 4 && (u[0] >> 1 & 0x3f) == 63)
  {
    info->layer = 1;
    const unsigned inner = u[2] >> 1 & 0x3f;
    info->layerKey |= inner >= 16 && inner <= 23;
  }
  return true;
}

static enum variant g_cross = V_ASIS;
// The variant of the RPU of the packet n (1 is the first) by the schedule.
static enum variant Variant(const char* schedule, unsigned n)
{
  const unsigned k2 = g_numLayerKeys > 2 ? g_layerKeys[2] : 0, k4 = g_numLayerKeys > 4 ? g_layerKeys[4] : 0;
  if (!strcmp(schedule, "fel"))
    return V_ASIS;
  if (!strcmp(schedule, "mel"))
    return V_MEL;
  if (!strcmp(schedule, "aligned"))
    return n >= k2 && n < k4 ? V_ASIS : V_MEL;
  if (!strcmp(schedule, "aligned2"))
  {
    const unsigned k6 = g_numLayerKeys > 6 ? g_layerKeys[6] : 0, k8 = g_numLayerKeys > 8 ? g_layerKeys[8] : 0;
    return (n >= k2 && n < k4) || (n >= k6 && n < k8) ? V_ASIS : V_MEL;
  }
  if (!strcmp(schedule, "midgop"))
    return n >= k2 + 3 && n < k4 ? V_ASIS : V_MEL;
  if (!strncmp(schedule, "cross-", 6))
    return n <= 120 ? g_cross : V_ASIS;
  return V_ASIS;
}

// ---- the pictures ----

struct picture
{
  int64_t pts;
  unsigned n;          // its packet
  int result;          // of dvbridge_fel_take(); 2 also for one that is not asked
  uint64_t layerHash;  // of the samples of the paired enhancement picture
  uint64_t metaHash;   // of the metadata it goes out with (Kodi's UseLayerDVMetadata())
  int flat;            // its enhancement picture sits at the offset of its NLQ in every sample
  uint8_t rpu[DVBRIDGE_RPU_BYTES];
  size_t rpuSize;      // the RPU of the base picture, as coded (none: 0)
  uint64_t baseMeta;   // its parsed metadata
  int ffmpegResidual;  // libplacebo's mapping of FFmpeg's parse: nlq_active (-1: no metadata)
};
static struct picture g_pictures[PICTURES_MAX];
static unsigned g_count;
static struct picture g_pending[64];
static unsigned g_numPending;
static enum dvbridge_rpu_residual g_class[PACKETS_MAX]; // what the module's reading said, by packet

static uint64_t Hash(const uint8_t* d, size_t n, uint64_t h)
{
  for (size_t i = 0; i < n; ++i)
  {
    h ^= d[i];
    h *= UINT64_C(1099511628211);
  }
  return h;
}

static uint64_t FrameHash(const AVFrame* f)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (int plane = 0; plane < 3 && f->data[plane]; ++plane)
  {
    const int rows = plane ? (f->height + 1) / 2 : f->height;
    const int bytes = (plane ? (f->width + 1) / 2 : f->width) * 2;
    for (int y = 0; y < rows; ++y)
      h = Hash(f->data[plane] + (ptrdiff_t)y * f->linesize[plane], (size_t)bytes, h);
  }
  return h;
}

// libplacebo's pl_map_dovi_metadata(): nlq_active (the bridge's rule, dvbridge_map_color())
static int NlqActive(const AVDOVIMetadata* m)
{
  const AVDOVIRpuDataHeader* h = av_dovi_get_header(m);
  const AVDOVIDataMapping* map = av_dovi_get_mapping(m);
  if (h->disable_residual_flag || map->nlq_method_idc != AV_DOVI_NLQ_LINEAR_DZ)
    return 0;
  for (int c = 0; c < 3; ++c)
  {
    const AVDOVINLQParams* q = &map->nlq[c];
    if (q->nlq_offset || q->vdr_in_max != (UINT64_C(1) << h->coef_log2_denom) || q->linear_deadzone_slope ||
        q->linear_deadzone_threshold)
      return 1;
  }
  return 0;
}

// Whether every sample of an enhancement picture (10 bits) is the offset of its component.
static int Flat(const AVFrame* f, const AVDOVIMetadata* m)
{
  if (!m || f->format != AV_PIX_FMT_YUV420P10LE)
    return 0;
  const AVDOVIDataMapping* map = av_dovi_get_mapping(m);
  for (int plane = 0; plane < 3; ++plane)
  {
    const int rows = plane ? (f->height + 1) / 2 : f->height;
    const int cols = plane ? (f->width + 1) / 2 : f->width;
    for (int y = 0; y < rows; ++y)
    {
      const uint16_t* row = (const uint16_t*)(f->data[plane] + (ptrdiff_t)y * f->linesize[plane]);
      for (int x = 0; x < cols; ++x)
        if (row[x] != map->nlq[plane].nlq_offset)
          return 0;
    }
  }
  return 1;
}

static void ReadBase(const AVFrame* frame, struct picture* p)
{
  const AVFrameSideData* parsed = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
  const AVFrameSideData* coded = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
  p->baseMeta = parsed ? Hash(parsed->data, parsed->size, 1) : 0;
  p->ffmpegResidual = parsed ? NlqActive((const AVDOVIMetadata*)parsed->data) : -1;
  p->rpuSize = 0;
  if (parsed && coded && coded->size <= sizeof(p->rpu))
  {
    memcpy(p->rpu, coded->data, coded->size);
    p->rpuSize = coded->size;
  }
}

struct run
{
  struct dvbridge_fel* fel;
  unsigned late, resyncs, endWait, marks, marksWithRpu, marksTaking, marksWanted;
};

// Kodi's GetPairedPicture(), its emit: the oldest base picture goes out with its enhancement picture, without
// it, or waits.
static int Emit(struct run* r, int ended)
{
  while (g_numPending)
  {
    struct picture* p = &g_pending[0];
    AVFrame* layer = NULL;
    int result = dvbridge_fel_take(r->fel, p->pts, &layer);
    if (result == 0 && !ended && g_numPending < 6) // Kodi: DV_EL_LATE_PENDING
      return 1;
    if (result == 0 && ended)
      ++r->endWait; // its base picture never came (the end): out without it, as Kodi's emit(true)
    if (result == 0 && !ended)
      ++r->late; // Kodi's late path: out without it
    if (result == 0)
      result = 2;
    if (result < 0 && dvbridge_fel_exhausted(r->fel))
      result = 2; // Kodi: the layer ended before the base layer
    if (result < 0)
    {
      ++r->resyncs;
      dvbridge_fel_reset(r->fel);
      result = 2;
    }
    p->result = result;
    p->metaHash = p->baseMeta;
    if (result == 3 || result == 4)
    {
      // a picture whose enhancement access unit was not decoded: what it would have brought (4: and that
      // needs it, out without it as 2)
      ++r->marks;
      r->marksWanted += result == 4;
      const AVFrameSideData* coded = NULL;
      const AVFrameSideData* parsed = NULL;
      r->marksWithRpu += av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_RPU_BUFFER) != NULL;
      if (dvbridge_fel_layer_metadata(layer, p->rpuSize ? p->rpu : NULL, p->rpuSize, &coded, &parsed))
      {
        ++r->marksTaking;
        p->metaHash = Hash(parsed->data, parsed->size, 1);
      }
      av_frame_free(&layer);
      if (result == 4)
        p->result = 2;
    }
    if (result == 1)
    {
      p->layerHash = FrameHash(layer);
      const AVFrameSideData* coded = NULL;
      const AVFrameSideData* parsed = NULL;
      if (dvbridge_fel_layer_metadata(layer, p->rpuSize ? p->rpu : NULL, p->rpuSize, &coded, &parsed))
        p->metaHash = Hash(parsed->data, parsed->size, 1);
      const AVFrameSideData* own = av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_METADATA);
      p->flat = Flat(layer, own ? (const AVDOVIMetadata*)own->data : NULL);
      av_frame_free(&layer);
    }
    if (g_count == PICTURES_MAX)
      return 0;
    g_pictures[g_count++] = *p;
    memmove(g_pending, g_pending + 1, --g_numPending * sizeof(*g_pending));
  }
  return 0;
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  const char* schedule = argv[2];
  int tracks = 0, off = 0;
  unsigned seekAfter = 0, seekTo = 0, limit = 0, startAt = 0;
  for (int i = 3; i < argc; ++i)
  {
    if (!strcmp(argv[i], "tracks"))
      tracks = 1;
    else if (!strcmp(argv[i], "off"))
      off = 1;
    else if (!strncmp(argv[i], "seek=", 5))
    {
      if (sscanf(argv[i] + 5, "%u:%u", &seekAfter, &seekTo) != 2)
        return 2;
    }
    else if (!strncmp(argv[i], "limit=", 6))
      limit = (unsigned)atoi(argv[i] + 6);
    else if (!strncmp(argv[i], "start=", 6))
      startAt = (unsigned)atoi(argv[i] + 6);
    else
      return 2;
  }
  static const char* names[] = {"asis", "mel", "offset", "slope", "vdr", "disable"};
  if (!strncmp(schedule, "cross-", 6))
    for (int v = 1; v < 6; ++v)
      if (!strcmp(schedule + 6, names[v]))
        g_cross = (enum variant)v;
  av_log_set_level(AV_LOG_ERROR);

  // the first pass: the packets, which have enhancement units, which of them a random-access picture
  struct merge_source* source = NULL;
  AVFormatContext* fc = NULL;
  int index = -1;
  for (int pass = 0; pass < 2; ++pass)
  {
    if (tracks)
    {
      if (source)
        merge_close(source);
      source = merge_open(argv[1]);
      fc = source ? merge_context(source) : NULL;
      index = source ? merge_base(source) : -1;
    }
    else
    {
      if (fc)
        avformat_close_input(&fc);
      if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
        return 1;
      index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    }
    if (!fc || index < 0)
      return 1;
    if (pass)
      break;
    const AVCodecParameters* par = fc->streams[index]->codecpar;
    const int ls = tracks ? 0 : unit_length_size(par->extradata, par->extradata_size);
    AVPacket* scan = av_packet_alloc();
    while ((tracks ? merge_read(source, scan) : av_read_frame(fc, scan)) >= 0)
    {
      if (scan->stream_index == index && g_numUnits < PACKETS_MAX - 1)
      {
        struct unitinfo* u = &g_units[++g_numUnits];
        u->pts = scan->pts;
        dvbridge_rpu_units(scan->data, (size_t)scan->size, ls, LayerScan, u);
        if (u->layerKey && g_numLayerKeys < 256)
          g_layerKeys[g_numLayerKeys++] = g_numUnits;
      }
      av_packet_unref(scan);
    }
    av_packet_free(&scan);
  }
  AVStream* stream = fc->streams[index];
  AVCodecParameters* par = avcodec_parameters_alloc();
  if (!par || avcodec_parameters_copy(par, stream->codecpar) < 0)
    return 1;
  const AVRational micro = {1, 1000000};
  const AVRational tb = tracks ? micro : stream->time_base;
  if (tracks)
  {
    AVDOVIDecoderConfigurationRecord record;
    merge_record(source, &record);
    AVPacketSideData* side = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                     AV_PKT_DATA_DOVI_CONF, sizeof(record), 0);
    if (!side)
      return 1;
    memcpy(side->data, &record, sizeof(record));
  }
  const int lengthSize = tracks ? 0 : unit_length_size(par->extradata, par->extradata_size);
  printf("%s: %u packets, %u with a random-access picture of the layer (the first ones: %u %u %u %u %u %u); "
         "framing %s; schedule %s%s\n",
         argv[1], g_numUnits, g_numLayerKeys, g_layerKeys[0], g_layerKeys[1], g_layerKeys[2], g_layerKeys[3],
         g_layerKeys[4], g_layerKeys[5], lengthSize ? "lengths" : "start codes", schedule, off ? ", layer always" : "");

  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  if (!codec || !base || avcodec_parameters_to_context(base, par) < 0)
    return 1;
  base->pkt_timebase = tb;
  base->thread_count = 8;
  base->thread_type = FF_THREAD_FRAME;
  base->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 1;
  struct run r = {0};
  r.fel = dvbridge_fel_create(par, tb);
  if (!r.fel)
    return 1;
  r.fel->need.off = off;
  // what dvbridge_fel_device() does with a VAAPI device, in software; the thread count stays the module's
  r.fel->decoder->get_format = avcodec_default_get_format;
  if (avcodec_open2(r.fel->decoder, codec, NULL) < 0)
    return 1;
  r.fel->opened = true;

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  unsigned read = 0, rewritten = 0, asIs = 0, seeks = 0, afterSeekSent = 0, sentAtSeek = 0;
  int end = 0, baseEnded = 0;
  unsigned next = 0; // the index of the packet that comes, in the first pass's numbering
  int64_t start = fc->start_time == AV_NOPTS_VALUE ? 0 : fc->start_time;
  while (!baseEnded)
  {
    if (!end)
    {
      const int got = tracks ? merge_read(source, packet) : av_read_frame(fc, packet);
      if (got >= 0 && packet->stream_index != index)
      {
        av_packet_unref(packet);
        continue;
      }
      end = got < 0 || (limit && read == limit);
      if (end && got >= 0)
        av_packet_unref(packet);
    }
    if (!end)
    {
      ++read;
      // which packet of the first pass this is (by its time: after a seek the count goes on elsewhere)
      unsigned n = next + 1;
      if (n > g_numUnits || g_units[n].pts != packet->pts)
        for (n = 1; n <= g_numUnits && g_units[n].pts != packet->pts; ++n)
          ;
      next = n;
      // a file that starts later
      if (startAt && n < startAt)
      {
        av_packet_unref(packet);
        continue;
      }
      const enum variant v = n <= g_numUnits ? Variant(schedule, n) : V_ASIS;
      if (av_packet_make_writable(packet) < 0 || !RewritePacket(packet, lengthSize, v, &asIs, &rewritten))
        return 1;
      if (tracks)
      {
        // Kodi's times: microseconds from the start of the file (what comes before it is 0)
        if (packet->pts != AV_NOPTS_VALUE)
          packet->pts = FFMAX(0, av_rescale_q(packet->pts, stream->time_base, micro) - start);
        if (packet->dts != AV_NOPTS_VALUE)
          packet->dts = FFMAX(0, av_rescale_q(packet->dts, stream->time_base, micro) - start);
      }
      packet->opaque = (void*)(intptr_t)n;
      // the module's reading of this packet's RPU, from a copy of its state as it is now
      struct dvbridge_rpu_nlq copy = r.fel->rpu;
      if (n < PACKETS_MAX)
        g_class[n] = dvbridge_rpu_nlq_packet(&copy, packet->data, (size_t)packet->size, lengthSize);
      if (avcodec_send_packet(base, packet) < 0)
        printf("note: the base decoder refused packet %u\n", n);
      if (!dvbridge_fel_submit(r.fel, packet))
        dvbridge_fel_reset(r.fel);
      av_packet_unref(packet);
      if (seekAfter && !seeks && read == seekAfter && seekTo <= g_numUnits)
      {
        // Kodi's seek: to the time of the packet seekTo, backwards to a key frame; CDVDVideoCodecFFmpeg::Reset()
        ++seeks;
        if (tracks ? merge_seek(source, (double)seekTo / g_numUnits) < 0
                   : av_seek_frame(fc, index, g_units[seekTo].pts, AVSEEK_FLAG_BACKWARD) < 0)
          return 1;
        avcodec_flush_buffers(base);
        g_numPending = 0;
        dvbridge_fel_reset(r.fel);
        dvbridge_fel_base_flushed(r.fel);
        next = 0;
        sentAtSeek = g_elSent;
      }
    }
    else
    {
      avcodec_send_packet(base, NULL);
      if (!dvbridge_fel_drain(r.fel))
        dvbridge_fel_reset(r.fel);
    }
    for (;;)
    {
      if (dvbridge_fel_failed(r.fel))
      {
        ++r.resyncs;
        printf("note: resync: %s\n", dvbridge_fel_reason(r.fel));
        dvbridge_fel_reset(r.fel);
      }
      const int got = avcodec_receive_frame(base, frame);
      if (got < 0)
      {
        if (end && got == AVERROR_EOF)
        {
          for (int i = 0; i < 64 && g_numPending; ++i)
            Emit(&r, 1);
          baseEnded = 1;
        }
        break;
      }
      if (g_numPending == 64)
      {
        Check(0, "64 base pictures wait");
        return 1;
      }
      struct picture* p = &g_pending[g_numPending++];
      memset(p, 0, sizeof(*p));
      p->pts = frame->pts;
      p->n = (unsigned)(intptr_t)frame->opaque;
      ReadBase(frame, p);
      // Kodi's GetPairedPicture(): the RPU and metadata of each base picture, for the module
      {
        const AVFrameSideData* parsed = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        const AVFrameSideData* coded = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
        if (parsed && coded)
          dvbridge_fel_base_metadata(r.fel, coded->data, coded->size, parsed->data, parsed->size);
      }
      av_frame_unref(frame);
      if (!pump(r.fel))
        dvbridge_fel_reset(r.fel);
      Emit(&r, 0);
    }
  }
  afterSeekSent = seeks ? g_elSent - sentAtSeek : 0;

  // ---- what came out ----
  struct dvbridge_fel_layer_skip skip;
  dvbridge_fel_layer_skip_counts(r.fel, &skip);
  unsigned paired = 0, notNeeded = 0, without = 0, flat = 0, agree = 0, read2 = 0, unread = 0, residualRead = 0;
  unsigned noRpu = 0;
  unsigned felWithout = 0, felPairedOrLeading = 0;
  for (unsigned i = 0; i < g_count; ++i)
  {
    const struct picture* p = &g_pictures[i];
    paired += p->result == 1;
    notNeeded += p->result == 3;
    without += p->result == 2;
    flat += p->result == 1 && p->flat;
    // what the module read of the RPU of the packet against what FFmpeg's decoder read of it: an RPU of its
    // own (the coded RPU attached) is read alike; a packet without one is none to the module
    if (p->n && p->n < PACKETS_MAX)
    {
      const enum dvbridge_rpu_residual c = g_class[p->n];
      ++read2;
      unread += c == DVBRIDGE_RPU_UNREAD;
      residualRead += c == DVBRIDGE_RPU_RESIDUAL;
      if (p->rpuSize)
        agree += (c == DVBRIDGE_RPU_RESIDUAL && p->ffmpegResidual == 1) ||
                 (c == DVBRIDGE_RPU_NO_RESIDUAL && p->ffmpegResidual == 0);
      else
      {
        agree += c == DVBRIDGE_RPU_NONE;
        ++noRpu;
      }
    }
    if (p->ffmpegResidual == 1 && p->result != 1)
      ++felWithout;
  }
  (void)felPairedOrLeading;
  printf("packets read %u, RPUs rewritten %u (%u left as they are), pictures %u: paired %u, not decoded (MEL) %u, "
         "without the layer %u; late %u, resyncs %u\n",
         read, rewritten, asIs, g_count, paired, notNeeded, without, r.late, r.resyncs);
  printf("module: decoded %u, not decoded %u, answers 3: %u, wanted: %u, stops %u, resumes %u, kept: %s\n",
         skip.decoded, skip.not_decoded, skip.not_needed, skip.wanted, skip.stops, skip.resumes,
         skip.kept ? skip.kept : "no");
  printf("marks: %u (%u of pictures that need their layer), with an RPU %u, taking it %u; waited at the end %u; "
         "anchor %lld\n",
         r.marks, r.marksWanted, r.marksWithRpu, r.marksTaking, r.endWait, (long long)r.fel->anchor_pts);
  printf("enhancement decoder: %u access units sent, %u pictures, %.3f s%s\n", g_elSent, g_elFrames, g_elSeconds,
         seeks ? "" : "");
  if (seeks)
    printf("after the seek: %u enhancement access units sent\n", afterSeekSent);
  printf("RPUs read by the module and by FFmpeg (libplacebo's rule on its parse): %u, the same %u, unread %u; "
         "residual %u\n",
         read2, agree, unread, residualRead);
  printf("paired enhancement pictures that sit at the offset in every sample (flat): %u\n", flat);
  printf("pictures whose packet had no RPU of its own: %u\n", noRpu);
  printf("pictures whose RPU (FFmpeg) carries a residual and went out without their layer: %u\n", felWithout);
  printf("order mismatches %u\n", dvbridge_fel_order_mismatches(r.fel, NULL));

  if (getenv("MEL_OUT"))
  {
    FILE* out = fopen(getenv("MEL_OUT"), "w");
    for (unsigned i = 0; out && i < g_count; ++i)
      fprintf(out, "%lld %u %d %016llx %016llx %d\n", (long long)g_pictures[i].pts, g_pictures[i].n,
              g_pictures[i].result, (unsigned long long)g_pictures[i].layerHash,
              (unsigned long long)g_pictures[i].metaHash, g_pictures[i].ffmpegResidual);
    if (out)
      fclose(out);
  }
  if (getenv("MEL_SUMMARY"))
  {
    FILE* out = fopen(getenv("MEL_SUMMARY"), "w");
    if (out)
    {
      fprintf(out, "count %u paired %u notneeded %u without %u decoded %u notdecoded %u wanted %u stops %u resumes %u "
                   "kept %d marks %u markrpu %u marktake %u elsent %u elframes %u elsec %.3f agree %u read %u unread %u flat %u felwithout %u "
                   "late %u resyncs %u rewritten %u asis %u afterseek %u mismatches %u keys %u",
              g_count, paired, notNeeded, without, skip.decoded, skip.not_decoded, skip.wanted, skip.stops,
              skip.resumes, skip.kept != NULL, r.marks, r.marksWithRpu, r.marksTaking, g_elSent, g_elFrames, g_elSeconds, agree, read2, unread,
              flat, felWithout, r.late, r.resyncs, rewritten, asIs, afterSeekSent,
              dvbridge_fel_order_mismatches(r.fel, NULL), g_numLayerKeys);
      fprintf(out, " anchor %lld markwanted %u", (long long)r.fel->anchor_pts, r.marksWanted);
      for (unsigned i = 0; i < 8 && i < g_numLayerKeys; ++i)
        fprintf(out, " k%u %u", i, g_layerKeys[i]);
      fprintf(out, "\n");
      fclose(out);
    }
  }
  Check(read2 > 0 && agree == read2, "the module reads every RPU as libplacebo's mapping of FFmpeg's parse of it");
  dvbridge_fel_destroy(r.fel);
  avcodec_free_context(&base);
  avcodec_parameters_free(&par);
  av_packet_free(&packet);
  av_frame_free(&frame);
  if (tracks)
    merge_close(source);
  else
    avformat_close_input(&fc);
  printf("%s\n", g_failures ? "FAILED" : "RUN DONE");
  return g_failures != 0;
}
