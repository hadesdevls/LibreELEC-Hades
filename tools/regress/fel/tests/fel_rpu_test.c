// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of Kodi's enhancement-layer module (tools/dvbridge/dvbridge_fel.c, the file itself) with a real
// dual-layer Dolby Vision stream of one track, decoded in software.
//
// Every base picture is paired with its enhancement picture the way Kodi does it, and the module's rule
// (dvbridge_fel_layer_metadata(), the function Kodi calls) says whether the picture takes the Dolby Vision
// metadata that its enhancement picture brings instead of the one FFmpeg attached to the base picture.
// The level 1 metadata (minimum, maximum and average brightness) changes a few times in the stream, so over
// the pictures in presentation order each value has to come in one run. With the metadata the rule gives it
// does; with the RPU of the packet of the base picture, in a stream whose layers are coded in different
// picture order, it does not.
//
// What this does not show: that the RPU of an enhancement access unit belongs to the picture that access
// unit presents. A constant shift between the two would pass as well, and the test stream has no cut in its
// picture to tell. That rests on how the two-track form of the stream times its access units.
//
// The module's decoder runs with the thread count the module gives it (one), in software instead of VAAPI.
//
// Options, in any order after the file:
//   strip   the enhancement units of the last packet that has them are taken out before the module sees it,
//           which leaves a packet with a base picture and an RPU only. Its base picture has to go out
//           without the layer, and nothing else may change.
//   annexb  the packets are given with start codes instead of lengths (FFmpeg's hevc_mp4toannexb), the
//           framing of a stream that comes in two tracks.
//   base1   the base layer is decoded with one thread too (slow), as with a hardware decoder.
//   hole=N  the enhancement units of packet N are taken out: a hole in the layer, in the middle of the
//           stream. The base pictures whose enhancement picture cannot be had go out without it, as in Kodi,
//           and every enhancement picture that is still paired has to be, sample for sample, the one of a
//           run without the hole (FEL_CLEAN names the file such a run wrote with FEL_HASHES): the decoder
//           must not give a picture made from a reference that is missing. The other checks are left out.
//
//
//   seek=A:B  after packet A, a seek to the time of packet B, as Kodi's (both decoders flushed, the layer reset,
//             dvbridge_fel_base_flushed()); start=N: the file starts with packet N (the ones before are not played).
//             Either goes with kodi.
//   kodi      the base pictures wait for their enhancement pictures as in Kodi's GetPairedPicture() (while fewer than
//             six wait; a failure starts the layer again), with the metadata Kodi gives each one
//             (UseLayerDVMetadata(), the answers 3 and 4 of the module, and dvbridge_fel_layer_rpu()).
// FEL_TRUTH names a file of "o m" lines: the packet m whose enhancement access unit the stream presents at the time
// of the base picture of packet o (made from the stream in two tracks, whose packets are these). With it, every
// picture is checked against the RPU of its own access unit. FEL_WHOLE names the pictures of a whole run of the
// same file (FEL_PICTURES of it): every picture paired in this run has to have the enhancement picture and the
// metadata it has there. FEL_PICTURES: the pictures of this run, one a line. FEL_RPU_IN_BASE: the file carries the
// RPU of each picture in the base access unit of that picture (samples/one_track_swap.mkv), not in its enhancement
// one; its FEL_TRUTH is then each packet for itself.
//
//   device=N[:other]  after packet N, the base decoder is on a new device context of VAAPI, of the same display (as
//             Kodi gives one each time its base decoder asks for a format again) or of another one: the layer goes on
//             on the same display, and starts again on another. With kodi.
// The access units the module gives its decoder are checked against those of the stream, split here (the framing of
// a packet is never changed on its way).
//
// Usage: fel_rpu_test <file with one HEVC track of profile 7> [strip] [annexb] [base1] [hole=N] [kodi] [seek=A:B]
//        [start=N] [device=N[:other]]
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/hwcontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// what the module gives its decoder, access unit by access unit (a hash of the bytes), to compare with the
// access units of the layer as the stream has them: the framing of a packet is never changed on its way.
#define SENT_MAX 32768
static uint64_t g_sent[SENT_MAX];
static unsigned g_numSent;
// elnorpu=K: about one access unit in K reaches the layer's decoder without its RPU unit (chosen by
// its time, so that one given again is given alike), as one whose RPU that decoder could not read, or whose mapping
// it did not see: FFmpeg attaches the last one it read again, without the coded RPU
static unsigned g_elNoRpu, g_elStripped;
static int g_layerLength = -1; // the framing of the access units the module gives its decoder
static AVPacket* StripRpu(const AVPacket* p);
static int SentToLayer(AVCodecContext* c, const AVPacket* p)
{
  if (p && p->size > 0 && g_numSent < SENT_MAX)
  {
    uint64_t h = UINT64_C(14695981039346656037);
    for (int i = 0; i < p->size; ++i)
    {
      h ^= p->data[i];
      h *= UINT64_C(1099511628211);
    }
    g_sent[g_numSent++] = h;
  }
  if (p && p->size > 0 && g_elNoRpu && g_layerLength >= 0 &&
      (((uint64_t)p->pts * UINT64_C(2654435761)) >> 13) % g_elNoRpu == 0)
  {
    AVPacket* q = StripRpu(p);
    if (q)
    {
      const int r = avcodec_send_packet(c, q);
      g_elStripped += r >= 0;
      av_packet_free(&q);
      return r;
    }
  }
  return avcodec_send_packet(c, p);
}
#define avcodec_send_packet SentToLayer
// the surfaces of the layer's decoder. Each picture it makes is held, as a VAAPI surface is, until
// nothing holds the picture any more (the decoder, the module's queue, the pictures on their way to the screen):
// FEL_POOL=N makes the pool a fixed one of N, whose next picture finds no surface while N are held, as a fixed VAAPI
// pool (FFmpeg's vaapi_pool_alloc()); without it the pool has no bound, and is counted all the same. Where the module
// takes its decoder's buffers itself (it counts them), it is this pool that it takes them from.
static int TestPoolBuffer(AVCodecContext* c, AVFrame* f, int flags);
#define avcodec_default_get_buffer2 TestPoolBuffer
// what nvidia-vaapi-driver says it is, for the module's choice of a pool (FEL_VENDOR)
static const char* g_vendor = "Mesa Gallium driver 26.2.5 for AMD Radeon Graphics (radeonsi, gfx1036, LLVM 23.1.3, DRM 3.64, 7.2.11)";
#define vaQueryVendorString(display) ((void)(display), g_vendor)
// The module only takes VAAPI surfaces from its decoder. Here the decoder runs in software.
#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI
#undef avcodec_send_packet
#undef avcodec_default_get_buffer2
#include <libavutil/hwcontext_vaapi.h> // (with tests/stub/va/va.h on the host)

// the units of an access unit but its RPU (the one FFmpeg's decoder takes), framed as they came
struct units
{
  const uint8_t* data[512];
  size_t size[512];
  unsigned n;
};
static bool TakeUnit(const uint8_t* unit, size_t size, void* context)
{
  struct units* u = context;
  if (u->n < 512)
  {
    u->data[u->n] = unit;
    u->size[u->n++] = size;
  }
  return true;
}
static AVPacket* StripRpu(const AVPacket* p)
{
  struct dvbridge_rpu_find find = {0};
  dvbridge_rpu_units(p->data, (size_t)p->size, g_layerLength, dvbridge_rpu_find_unit, &find);
  struct units u = {0};
  dvbridge_rpu_units(p->data, (size_t)p->size, g_layerLength, TakeUnit, &u);
  if (!find.rpu || u.n >= 512)
    return NULL;
  AVPacket* q = av_packet_alloc();
  if (!q || av_new_packet(q, p->size) < 0)
  {
    av_packet_free(&q);
    return NULL;
  }
  uint8_t* out = q->data;
  for (unsigned i = 0; i < u.n; ++i)
  {
    if (u.data[i] == find.rpu)
      continue;
    const unsigned prefix = g_layerLength ? (unsigned)g_layerLength : 4;
    for (unsigned k = 0; k < prefix; ++k)
      *out++ = g_layerLength ? (uint8_t)(u.size[i] >> (8 * (prefix - 1 - k))) : (k == 3);
    memcpy(out, u.data[i], u.size[i]);
    out += u.size[i];
  }
  av_shrink_packet(q, (int)(out - q->data));
  q->pts = p->pts;
  q->dts = p->dts;
  return q;
}

static unsigned g_pool, g_poolLive, g_poolPeak, g_poolShort;
static void TestPoolFree(void* opaque, uint8_t* data)
{
  (void)data;
  AVBufferRef* inner = opaque;
  --g_poolLive;
  av_buffer_unref(&inner);
}
static int TestPoolBuffer(AVCodecContext* c, AVFrame* f, int flags)
{
  if (g_pool && g_poolLive >= g_pool)
  {
    ++g_poolShort;
    return AVERROR(ENOMEM);
  }
  const int r = avcodec_default_get_buffer2(c, f, flags);
  if (r < 0)
    return r;
  AVBufferRef* inner = f->buf[0];
  AVBufferRef* outer = av_buffer_create(inner->data, inner->size, TestPoolFree, inner, 0);
  if (!outer)
    return AVERROR(ENOMEM);
  f->buf[0] = outer;
  if (++g_poolLive > g_poolPeak)
    g_poolPeak = g_poolLive;
  return r;
}

// FEL_HOLD=H: the enhancement pictures of the last H base pictures that went out stay held, as in Kodi until the
// screen is done with them (each base picture that goes out moves the line on, with its layer or without it)
#define HOLD_MAX 64
static AVFrame* g_held[HOLD_MAX];
static unsigned g_hold, g_numHeld;
static void Hold(AVFrame** frame)
{
  if (!g_hold)
  {
    av_frame_free(frame);
    return;
  }
  if (g_numHeld == g_hold)
  {
    av_frame_free(&g_held[0]);
    memmove(g_held, g_held + 1, --g_numHeld * sizeof(*g_held));
  }
  g_held[g_numHeld++] = *frame;
  *frame = NULL;
}

// why the module stopped its layer, each time (as Kodi logs it in ResyncDVEnhancement())
#define REASONS_MAX 16
static char g_reasons[REASONS_MAX][256];
static unsigned g_reasonCount[REASONS_MAX], g_numReasons;
static void NoteReason(const char* reason)
{
  unsigned i = 0;
  while (i < g_numReasons && strcmp(g_reasons[i], reason))
    ++i;
  if (i == g_numReasons && g_numReasons < REASONS_MAX)
    snprintf(g_reasons[g_numReasons++], sizeof(g_reasons[0]), "%s", reason);
  if (i < g_numReasons)
    ++g_reasonCount[i];
}

// device contexts of VAAPI, made here with nothing behind them but the display (a value): what Kodi's base
// decoder gives the module (dvbridge_fel_device()). The module's decoder decodes in software all the same.
static AVVAAPIDeviceContext g_fakeVaapi[4];
static unsigned g_numFake;
static AVBufferRef* FakeDevice(uintptr_t display)
{
  AVBufferRef* b = g_numFake < 4 ? av_buffer_allocz(sizeof(AVHWDeviceContext)) : NULL;
  if (!b)
    return NULL;
  AVHWDeviceContext* c = (AVHWDeviceContext*)b->data;
  c->type = AV_HWDEVICE_TYPE_VAAPI;
  g_fakeVaapi[g_numFake].display = (VADisplay)display;
  c->hwctx = &g_fakeVaapi[g_numFake++];
  return b;
}

#define PICTURES_MAX 8192
#define RPU_MAX 2048
#ifdef RING_MAX
#define HAVE_RING 1
#else
#define HAVE_RING 0
#endif
#ifdef RANK_SPAN
#define HAVE_OWN_RPU 1
#else
#define HAVE_OWN_RPU 0
#endif
#define UNITS_MAX 8192
// tools/regress: the module under test has every part that this test checks; a build against a module without
// one of them fails here instead of passing with fewer checks
#if !HAVE_RING || !HAVE_OWN_RPU || !defined(DVBRIDGE_FEL_WAITS_FOR_SURFACES) || !defined(DVBRIDGE_FEL_SURFACES_MAX) || \
    !defined(DVBRIDGE_FEL_RPUS)
#error "the module under test lacks a part that this test checks"
#endif

struct metadata
{
  int have;                  // parsed metadata is attached
  int own;                   // and the RPU it was parsed from: it is the RPU of this access unit
  int l1[3];                 // minimum, maximum, average (PQ, 12 bits), -1 without level 1
  uint8_t rpu[RPU_MAX];      // as coded
  size_t coded;              // its size as attached
  size_t size;               // without trailing zero bytes
};

struct picture
{
  int64_t pts;
  struct metadata base;      // what FFmpeg attached to the base picture
  struct metadata layer;     // what the paired enhancement picture brought
  int paired;
  int takes;                 // the module's rule: the picture takes the metadata of the layer
  uint64_t layerHash;        // of the samples of the paired enhancement picture
  unsigned n;                // its packet, by number from the start of the file
  uint64_t meta;             // the RPU it goes out with (a hash of it as coded; 0: none)
  int how;                   // 0 the one carried with it, 1 its enhancement picture's, 2 an answer 3 or 4, 3 the call
  int late;                  // it went out because Kodi stopped waiting (six waited) while the module waited
};

static unsigned g_seekAfter, g_seekTo, g_start;
static int g_kodi;
static int64_t g_unitPts[UNITS_MAX];   // by packet number from the start: its time
static uint64_t g_unitRpu[UNITS_MAX];  // ... and the RPU it carries, as the decoders take it (a hash)
static unsigned g_numUnits;
static unsigned g_truth[UNITS_MAX];    // FEL_TRUTH; 0: none
static unsigned g_haveTruth;
static unsigned char g_given[UNITS_MAX]; // packets given to the module
static unsigned g_late, g_resyncs;
static unsigned g_seekAt = 0xffffffffu; // the first picture that went out after the seek
static unsigned g_deviceAt;  // device=N[:other]: after packet N the base decoder is on a new device context
static int g_deviceOther;    // ... of another display (else of the same)
static int g_deviceReset = -1; // whether the module started its layer again for it
static unsigned g_deviceAtPicture = 0xffffffffu; // the first picture that went out after the switch
static int g_rpuInBase;       // FEL_RPU_IN_BASE: the RPU of each picture travels in its own base access unit
static int g_noDelay;         // nodelay
static unsigned g_badTerm;    // badterm=N
static unsigned g_wrongGiven;  // pictures that the module gave another RPU than their own

static uint64_t RpuHash(const uint8_t* data, size_t size)
{
  while (size && data && !data[size - 1])
    --size;
  if (!data || !size)
    return 0;
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; ++i)
  {
    h ^= data[i];
    h *= UINT64_C(1099511628211);
  }
  return h ? h : 1;
}

static uint64_t PacketRpu(const AVPacket* packet, int lengthSize)
{
  struct dvbridge_rpu_find find = {0};
  dvbridge_rpu_units(packet->data, (size_t)packet->size, lengthSize, dvbridge_rpu_find_unit, &find);
  return find.rpu ? RpuHash(find.rpu + 2, find.size - 2) : 0;
}

// the RPU a picture goes out with when the rule of the module takes the one of frame
static void TakeRule(struct picture* p, const AVFrame* frame)
{
  const AVFrameSideData* coded = NULL;
  const AVFrameSideData* parsed = NULL;
  if (dvbridge_fel_layer_metadata(frame, p->base.own ? p->base.rpu : NULL, p->base.coded, &coded, &parsed))
    p->meta = RpuHash(coded->data, coded->size);
}

static struct picture g_pictures[PICTURES_MAX];
static unsigned g_count;     // pictures that went out, in presentation order
static struct picture g_pending[64];
static unsigned g_numPending;
static unsigned g_maxPending;
static unsigned g_tooLong;   // RPUs that do not fit in RPU_MAX
static int g_failures;
static unsigned g_hole;      // hole=N: the enhancement units of packet N are taken out
static char g_stopped[128];  // why the module stopped, if it did

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

// The samples of a decoded picture (10 bits in 16, three planes).
static uint64_t FrameHash(const AVFrame* f)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (int plane = 0; plane < 3 && f->data[plane]; ++plane)
  {
    const int rows = plane ? (f->height + 1) / 2 : f->height;
    const int bytes = (plane ? (f->width + 1) / 2 : f->width) * 2;
    for (int y = 0; y < rows; ++y)
    {
      const uint8_t* row = f->data[plane] + (ptrdiff_t)y * f->linesize[plane];
      for (int x = 0; x < bytes; ++x)
      {
        h ^= row[x];
        h *= UINT64_C(1099511628211);
      }
    }
  }
  return h;
}

static void Read(const AVFrame* frame, struct metadata* m)
{
  memset(m, 0, sizeof(*m));
  m->l1[0] = m->l1[1] = m->l1[2] = -1;
  const AVFrameSideData* parsed = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
  const AVFrameSideData* coded = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
  if (parsed)
  {
    m->have = 1;
    const AVDOVIDmData* l1 = av_dovi_find_level((const AVDOVIMetadata*)parsed->data, 1);
    if (l1)
    {
      m->l1[0] = l1->l1.min_pq;
      m->l1[1] = l1->l1.max_pq;
      m->l1[2] = l1->l1.avg_pq;
    }
  }
  if (parsed && coded && coded->size > RPU_MAX)
    ++g_tooLong;
  if (parsed && coded && coded->size <= RPU_MAX)
  {
    m->own = 1;
    m->coded = m->size = coded->size;
    while (m->size && !coded->data[m->size - 1])
      --m->size;
    memcpy(m->rpu, coded->data, m->coded);
  }
}

// The pictures that wait go out as their enhancement pictures arrive, the oldest first (Kodi's
// GetPairedPicture(), without its limit on waiting pictures: nothing is shown here).
static int Emit(struct dvbridge_fel* fel, int ended)
{
  while (g_numPending)
  {
    struct picture* p = &g_pending[0];
    AVFrame* layer = NULL;
    int result = dvbridge_fel_take(fel, p->pts, &layer);
    // as Kodi's UseLayerDVMetadata(): the metadata the picture goes out with
    p->meta = p->base.own ? RpuHash(p->base.rpu, p->base.coded) : 0;
    p->how = 0;
    if (result == 3 || result == 4)
    {
      TakeRule(p, layer);
      av_frame_free(&layer);
      p->how = 2;
      result = 2;
    }
    // With a hole in the layer, as Kodi's GetPairedPicture(): a base picture whose enhancement picture is
    // late or missing goes out without it, and the ones after it are paired with what the decoder gives.
    if (g_hole && !dvbridge_fel_failed(fel) && (result < 0 || (result == 0 && g_numPending >= 16)))
      result = 2;
    // as Kodi: a picture waits while fewer than six do (DV_EL_LATE_PENDING), and a failure starts the
    // layer again
    if (g_kodi)
    {
      if (result == 0 && !ended && g_numPending < 6)
        return 1;
      if (result == 0)
      {
        g_late += !ended;
        p->late = !ended;
        result = 2;
      }
      if (result < 0 && dvbridge_fel_exhausted(fel))
        result = 2;
      else if (result < 0)
      {
        ++g_resyncs;
        NoteReason(dvbridge_fel_failed(fel) ? dvbridge_fel_reason(fel) : "an enhancement picture is missing");
        dvbridge_fel_reset(fel);
        result = 2;
      }
#if HAVE_OWN_RPU
      if (result == 2 && !p->how)
      {
        AVFrame* own = NULL;
        if (dvbridge_fel_layer_rpu(fel, p->pts, &own) == 1)
        {
          TakeRule(p, own);
          p->how = 3;
        }
        av_frame_free(&own);
      }
#endif
    }
    if (result == 0 && !ended)
      return 1;
    if (result < 0 && !dvbridge_fel_exhausted(fel))
    {
      printf("%s the enhancement layer failed at %lld: %s\n", g_hole ? "note:" : "FAIL:", (long long)p->pts,
             dvbridge_fel_reason(fel));
      snprintf(g_stopped, sizeof(g_stopped), "%s", dvbridge_fel_reason(fel));
      return 0;
    }
    if (result == 1)
    {
      p->paired = layer->pts == p->pts;
      p->layerHash = FrameHash(layer);
      Read(layer, &p->layer);
      // as Kodi's UseLayerDVMetadata(): the RPU the base picture came with, when it had its own
      const AVFrameSideData* coded = NULL;
      const AVFrameSideData* parsed = NULL;
      p->takes = dvbridge_fel_layer_metadata(layer, p->base.own ? p->base.rpu : NULL, p->base.coded, &coded,
                                             &parsed);
      if (p->takes && (!coded || !parsed))
      {
        printf("FAIL: the rule gives metadata that is not there at %lld\n", (long long)p->pts);
        av_frame_free(&layer);
        return 0;
      }
      TakeRule(p, layer);
      p->how = 1;
    }
    // the enhancement picture, if it has one, goes on to the screen with it (FEL_HOLD)
    Hold(&layer);
    if (g_count == PICTURES_MAX)
      return 0;
    g_pictures[g_count++] = *p;
    memmove(g_pending, g_pending + 1, --g_numPending * sizeof(*g_pending));
  }
  return 1;
}

// Runs of equal level 1 values, and distinct values. chosen: with the metadata the rule gives each picture;
// otherwise with the RPU of its packet. all: also over the pictures that went out without their enhancement
// picture, which keep the RPU of their packet whichever way.
static void Runs(int chosen, int all, int print, unsigned* runs, unsigned* distinct)
{
  static int seen[PICTURES_MAX][3];
  unsigned numSeen = 0;
  const int* previous = NULL;
  *runs = 0;
  for (unsigned i = 0; i < g_count; ++i)
  {
    const struct picture* p = &g_pictures[i];
    if (!p->paired && !all)
      continue;
    const struct metadata* m = chosen && p->paired && p->takes ? &p->layer : &p->base;
    if (!m->have || m->l1[0] < 0)
      continue;
    if (previous && !memcmp(previous, m->l1, sizeof(m->l1)))
      continue;
    previous = m->l1;
    ++*runs;
    if (print)
      printf("  run %u starts at picture %u (pts %lld): level 1 %d %d %d\n", *runs, i, (long long)p->pts,
             m->l1[0], m->l1[1], m->l1[2]);
    unsigned k = 0;
    while (k < numSeen && memcmp(seen[k], m->l1, sizeof(m->l1)))
      ++k;
    if (k == numSeen)
      memcpy(seen[numSeen++], m->l1, sizeof(m->l1));
  }
  *distinct = numSeen;
}

// The units of a packet of the file: a length of lengthSize bytes before each.
// Returns whether the packet has units of the enhancement layer (type 63); with strip, takes them out.
static int LayerUnits(AVPacket* packet, int lengthSize, int strip)
{
  uint8_t* in = packet->data;
  uint8_t* out = packet->data;
  const uint8_t* end = packet->data + packet->size;
  int found = 0;
  while (end - in > lengthSize)
  {
    size_t length = 0;
    for (int i = 0; i < lengthSize; ++i)
      length = length << 8 | in[i];
    if (!length || length > (size_t)(end - in - lengthSize))
      break;
    const int layer = (in[lengthSize] >> 1 & 0x3f) == 63;
    found |= layer;
    if (strip && !layer)
      memmove(out, in, lengthSize + length);
    if (!layer)
      out += lengthSize + length;
    in += lengthSize + length;
  }
  if (strip)
    packet->size = (int)(out - packet->data);
  return found;
}

static void UnitChecks(void)
{
  static const uint8_t layerAndRpu[] = {0, 0, 0, 1, 0x40, 1, 9, 0, 0, 0, 1, 0x7c, 1, 0x19, 8};
  static const uint8_t rpuOnly[] = {0, 0, 0, 1, 0x7c, 1, 0x19, 8, 0x80};
  static const uint8_t rpuThenLayer[] = {0, 0, 0, 1, 0x7c, 1, 0x19, 0, 0, 0, 1, 0x02, 1, 7};
  static const uint8_t lengths[] = {0, 0, 0, 3, 0x7c, 1, 0x19, 0, 0, 0, 2, 0x02, 1};
  static const uint8_t lengthsRpu[] = {0, 0, 0, 3, 0x7c, 1, 0x19};
  static const uint8_t lengthsBad[] = {0, 0, 0, 9, 0x7c, 1, 0x19};
  static const uint8_t hvcc[23] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0};
  static const uint8_t annexb[23] = {0, 0, 0, 1, 0x40};
#define PACKET(a) &(AVPacket){.data = (uint8_t*)(a), .size = sizeof(a)}
  int ok = has_layer_unit(PACKET(layerAndRpu), 0) && !has_layer_unit(PACKET(rpuOnly), 0) &&
           has_layer_unit(PACKET(rpuThenLayer), 0) && has_layer_unit(PACKET(lengths), 4) &&
           !has_layer_unit(PACKET(lengthsRpu), 4) && has_layer_unit(PACKET(lengthsBad), 4) &&
           !has_layer_unit(&(AVPacket){.data = (uint8_t*)rpuOnly, .size = 0}, 0) &&
           !has_layer_unit(&(AVPacket){.data = (uint8_t*)rpuOnly, .size = 4}, 0);
#undef PACKET
  Check(ok, "a split packet with only an RPU is told from one with enhancement units, in both framings");
  Check(unit_length_size(hvcc, sizeof(hvcc)) == 4 && unit_length_size(annexb, sizeof(annexb)) == 0 &&
            unit_length_size(hvcc, 22) == 0 && unit_length_size(NULL, 0) == 0,
        "the framing of the split packets is read from the configuration of the enhancement layer");
#ifdef DVBRIDGE_FEL_WAITS_FOR_SURFACES
  // the pool of the layer's decoder, by what the driver says it is (as nvidia-vaapi-driver, Mesa and iHD say it)
  Check(fixed_pool_vendor("VA-API NVDEC driver [direct backend]") && fixed_pool_vendor("VA-API NVDEC driver [egl backend]") &&
            fixed_pool_vendor(NULL) &&
            !fixed_pool_vendor("Mesa Gallium driver 26.2.5 for AMD Radeon Graphics (radeonsi, gfx1036, LLVM 23.1.3, DRM 3.64, 7.2.11)") &&
            !fixed_pool_vendor("Intel iHD driver for Intel(R) Gen Graphics - 26.4.0 ()"),
        "a fixed pool with nvidia-vaapi-driver (and a driver that does not say what it is); one that grows with Mesa's "
        "and Intel's");
#endif
}

// A frame with the side data of an enhancement picture: the RPU as coded (or none) and parsed metadata.
static AVFrame* Layer(const uint8_t* rpu, size_t size, int parsed)
{
  AVFrame* frame = av_frame_alloc();
  if (!frame)
    return NULL;
  if (rpu)
  {
    AVFrameSideData* sd = av_frame_new_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER, size);
    if (sd)
      memcpy(sd->data, rpu, size);
  }
  if (parsed)
    av_frame_new_side_data(frame, AV_FRAME_DATA_DOVI_METADATA, 16);
  return frame;
}

// The rule itself, on frames made here.
static void RuleChecks(void)
{
  static const uint8_t a[] = {0x19, 8, 9, 0x80};
  static const uint8_t aZeros[] = {0x19, 8, 9, 0x80, 0, 0};
  static const uint8_t b[] = {0x19, 8, 7, 0x80};
  static const uint8_t aLonger[] = {0x19, 8, 9, 0x80, 1};
  const AVFrameSideData* coded = (const AVFrameSideData*)a;
  const AVFrameSideData* parsed = (const AVFrameSideData*)a;
  AVFrame* own = Layer(a, sizeof(a), 1);
  AVFrame* ownZeros = Layer(aZeros, sizeof(aZeros), 1);
  AVFrame* repeated = Layer(NULL, 0, 1);
  AVFrame* unparsed = Layer(a, sizeof(a), 0);
  AVFrame* bare = Layer(NULL, 0, 0);
  if (!own || !ownZeros || !repeated || !unparsed || !bare)
  {
    Check(0, "frames for the checks of the rule");
    return;
  }
  int ok = 1;
  // the same RPU, with or without zero bytes after it on either side: nothing changes
  ok &= !dvbridge_fel_layer_metadata(own, a, sizeof(a), &coded, &parsed) && coded && parsed;
  ok &= !dvbridge_fel_layer_metadata(own, aZeros, sizeof(aZeros), &coded, &parsed);
  ok &= !dvbridge_fel_layer_metadata(ownZeros, a, sizeof(a), &coded, &parsed);
  // another RPU, one that only continues, and a base picture that had none of its own: taken
  ok &= dvbridge_fel_layer_metadata(own, b, sizeof(b), &coded, &parsed) && coded && parsed &&
        coded->size == sizeof(a) && !memcmp(coded->data, a, sizeof(a));
  ok &= dvbridge_fel_layer_metadata(own, aLonger, sizeof(aLonger), &coded, &parsed);
  ok &= dvbridge_fel_layer_metadata(own, a, sizeof(a) - 1, &coded, &parsed);
  ok &= dvbridge_fel_layer_metadata(own, NULL, 0, &coded, &parsed) && coded && parsed;
  // an enhancement picture without an RPU of its own (FFmpeg repeated the last one), without parsed
  // metadata, without anything, and no enhancement picture: nothing to take
  coded = parsed = (const AVFrameSideData*)a;
  ok &= !dvbridge_fel_layer_metadata(repeated, b, sizeof(b), &coded, &parsed) && !coded && !parsed;
  ok &= !dvbridge_fel_layer_metadata(unparsed, b, sizeof(b), &coded, &parsed) && !coded && !parsed;
  ok &= !dvbridge_fel_layer_metadata(bare, NULL, 0, &coded, &parsed) && !coded && !parsed;
  coded = parsed = (const AVFrameSideData*)a;
  ok &= !dvbridge_fel_layer_metadata(NULL, b, sizeof(b), &coded, &parsed) && !coded && !parsed;
  Check(ok, "the rule: an own RPU of the enhancement picture that is not the one of the base picture is taken");
  av_frame_free(&own);
  av_frame_free(&ownZeros);
  av_frame_free(&repeated);
  av_frame_free(&unparsed);
  av_frame_free(&bare);
}

// the surfaces of the layer, as this test held them and as the module counted them.
// FEL_EXPECT=named: the pool runs out (FEL_POOL), and each time the module says so in its reason;
// FEL_EXPECT=enough: with the pool of FEL_POOL as the module's limit it never runs out (it waits instead).
static void SurfaceChecks(struct dvbridge_fel* fel)
{
  printf("surfaces of the layer's decoder: at most %u in use, %u in use now, %u times none was free (pool %s%u, "
         "pictures held as by the screen %u)\n",
         g_poolPeak, g_poolLive, g_poolShort, g_pool ? "fixed of " : "without bound ", g_pool, g_hold);
  for (unsigned i = 0; i < g_numReasons; ++i)
    printf("the layer stopped %u times: %s\n", g_reasonCount[i], g_reasons[i]);
  const char* expect = getenv("FEL_EXPECT") ? getenv("FEL_EXPECT") : "";
#ifdef DVBRIDGE_FEL_SURFACES_MAX
  struct dvbridge_fel_surfaces s;
  dvbridge_fel_surface_counts(fel, &s);
  printf("the module counts %u in use now and %u at most; pool %u, %u times none free, %u uncounted\n", s.live,
         s.peak, s.pool, s.none_free, s.uncounted);
#ifdef DVBRIDGE_FEL_WAITS_FOR_SURFACES
  printf("the module's limit %u; %u access units waited for a free surface\n", s.limit, s.waits);
#endif
  Check(s.peak == g_poolPeak && s.live == g_poolLive && !s.uncounted,
        "the module counts the surfaces of its layer as they are held, at most and now");
#else
  (void)fel;
  Check(0, "the module counts the surfaces of its layer as they are held, at most and now");
#endif
  unsigned named = 0, generic = 0;
  for (unsigned i = 0; i < g_numReasons; ++i)
  {
    named += strstr(g_reasons[i], "no free surface for the enhancement layer") ? g_reasonCount[i] : 0;
    generic += !strcmp(g_reasons[i], "enhancement decoder error") ? g_reasonCount[i] : 0;
  }
  if (!strcmp(expect, "named"))
    Check(g_poolShort > 0 && named > 0 && !generic,
          "the pool runs out, and every time the layer stops for it the module says so (not a decoder error)");
  if (!strcmp(expect, "enough"))
    Check(!g_poolShort && !named && !generic && g_poolPeak <= g_pool,
          "with a fixed pool as the module's limit the layer never runs out of surfaces: it waits for one");
}

// the RPUs, as the module counted them
static void RpuChecks(struct dvbridge_fel* fel)
{
  const char* lifted = "a paired picture whose decoder gave no coded RPU takes the base decoder's reading of the "
                       "RPU of its own access unit";
#ifdef DVBRIDGE_FEL_RPUS
  struct dvbridge_fel_rpus r;
  dvbridge_fel_rpu_counts(fel, &r);
  printf("RPUs read %u, using a mapping sent before %u, with compressed metadata %u; paired pictures that took "
         "the base decoder's reading of their RPU %u, that could not %u, read differently by the two decoders %u; RPUs "
         "the base decoder could not read %u; access units given to the layer's decoder without their RPU %u\n",
         r.read, r.use_prev, r.compressed, r.layer_from_base, r.layer_none, r.layer_differ, r.lost, g_elStripped);
  Check(!r.layer_differ, "the two decoders read the RPU of every paired picture alike");
  if (g_elNoRpu)
    Check(g_elStripped > 0 && r.layer_from_base > 0 && !r.layer_none, lifted);
#else
  if (g_elNoRpu)
    Check(0, lifted);
#endif
  if (g_badTerm)
  {
    printf("the layer is decoded for good: %s\n", fel->need.kept ? fel->need.kept_why : "no");
    Check(fel->need.kept, "an RPU that the base decoder could not read and the module did makes the module "
                          "forget its mappings, and the layer is decoded for good in the stream");
  }
}

int main(int argc, char** argv)
{
  if (argc < 2)
    return 2;
  g_pool = getenv("FEL_POOL") ? (unsigned)atoi(getenv("FEL_POOL")) : 0;
  g_hold = getenv("FEL_HOLD") ? (unsigned)atoi(getenv("FEL_HOLD")) : 0;
  if (g_hold > HOLD_MAX)
    return 2;
  if (getenv("FEL_VENDOR"))
    g_vendor = getenv("FEL_VENDOR");
  int strip = 0, annexbWanted = 0, base1 = 0;
  for (int i = 2; i < argc; ++i)
  {
    if (!strcmp(argv[i], "strip"))
      strip = 1;
    else if (!strcmp(argv[i], "annexb"))
      annexbWanted = 1;
    else if (!strcmp(argv[i], "base1"))
      base1 = 1;
    else if (!strncmp(argv[i], "hole=", 5))
      g_hole = (unsigned)atoi(argv[i] + 5);
    else if (!strcmp(argv[i], "kodi"))
      g_kodi = 1;
    else if (!strncmp(argv[i], "seek=", 5))
    {
      if (sscanf(argv[i] + 5, "%u:%u", &g_seekAfter, &g_seekTo) != 2)
        return 2;
    }
    else if (!strncmp(argv[i], "start=", 6))
      g_start = (unsigned)atoi(argv[i] + 6);
    else if (!strncmp(argv[i], "device=", 7))
    {
      g_deviceAt = (unsigned)atoi(argv[i] + 7);
      g_deviceOther = strstr(argv[i], ":other") != NULL;
    }
    // the module reads the base layer's reordering as none (has_b_frames 0 in the context it made
    // of the stream's parameters, as from a stream without a configuration, whose parameter sets come in its packets)
    else if (!strcmp(argv[i], "nodelay"))
      g_noDelay = 1;
    // elnorpu=K (above); badterm=N: the RPU of packet N has a terminator FFmpeg does not take (0x81 for 0x80) and a
    // CRC32 that does not hold (one bit of its last byte flipped; ffmpeg-9935 keeps an RPU without its terminator
    // where the CRC32 holds),
    // which it reads after the NLQ: the base decoder cannot read that RPU, and the module's reader stops before it
    else if (!strncmp(argv[i], "elnorpu=", 8))
      g_elNoRpu = (unsigned)atoi(argv[i] + 8);
    else if (!strncmp(argv[i], "badterm=", 8))
      g_badTerm = (unsigned)atoi(argv[i] + 8);
    else
      return 2;
  }
  if ((g_seekAfter || g_start || g_deviceAt) && !g_kodi)
    return 2;
  av_log_set_level(AV_LOG_ERROR);
  printf("module: %s%s\n", HAVE_RING ? "keeps the metadata of the base pictures" : "without the metadata of the base pictures",
         HAVE_OWN_RPU ? "; the RPU of each picture's own access unit" : "");
  UnitChecks();
  RuleChecks();
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (index < 0)
    return 1;
  AVStream* stream = fc->streams[index];
  // the file frames its units with lengths (hvcC)
  const AVCodecParameters* par = stream->codecpar;
  const int lengthSize = par->extradata_size >= 23 && par->extradata[0] == 1 ? (par->extradata[21] & 3) + 1 : 0;
  Check(lengthSize > 0, "the file has an hvcC configuration");
  if (!lengthSize)
    return 1;
  // a first pass over the file: which packets have enhancement units
  unsigned lastWithLayer = 0, withLayer = 0;
  {
    AVPacket* scan = av_packet_alloc();
    unsigned n = 0;
    while (av_read_frame(fc, scan) >= 0)
    {
      if (scan->stream_index == index && n + 1 < UNITS_MAX)
      {
        // its time and its RPU, by its number
        g_unitPts[n + 1] = scan->pts;
        g_unitRpu[n + 1] = PacketRpu(scan, lengthSize);
        g_numUnits = n + 1;
      }
      if (scan->stream_index == index && ++n && LayerUnits(scan, lengthSize, 0))
      {
        lastWithLayer = n;
        ++withLayer;
      }
      av_packet_unref(scan);
    }
    av_packet_free(&scan);
    avformat_close_input(&fc);
    if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0 ||
        av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0) != index)
      return 1;
    stream = fc->streams[index];
    par = stream->codecpar;
  }
  printf("packets with enhancement units %u, the last of them is packet %u%s\n", withLayer, lastWithLayer,
         strip ? "; its enhancement units are taken out" : "");
  g_rpuInBase = getenv("FEL_RPU_IN_BASE") && *getenv("FEL_RPU_IN_BASE");
  // the packet whose enhancement access unit is presented at the time of each base picture
  if (getenv("FEL_TRUTH") && *getenv("FEL_TRUTH"))
  {
    FILE* in = fopen(getenv("FEL_TRUTH"), "r");
    unsigned o, m;
    while (in && fscanf(in, "%u %u", &o, &m) == 2)
      if (o < UNITS_MAX && m < UNITS_MAX)
      {
        g_truth[o] = m;
        g_haveTruth += m > 0;
      }
    if (in)
      fclose(in);
    Check(g_haveTruth > 2000, "the packet of the enhancement access unit of each base picture is read (FEL_TRUTH)");
  }
  // with start codes: what the decoders and the module get is what the filter gives
  AVBSFContext* annexb = NULL;
  if (annexbWanted)
  {
    const AVBitStreamFilter* filter = av_bsf_get_by_name("hevc_mp4toannexb");
    if (!filter || av_bsf_alloc(filter, &annexb) < 0 || avcodec_parameters_copy(annexb->par_in, par) < 0)
      return 1;
    annexb->time_base_in = stream->time_base;
    if (av_bsf_init(annexb) < 0)
      return 1;
    par = annexb->par_out;
    Check(unit_length_size(par->extradata, par->extradata_size) == 0, "the packets are given with start codes");
  }
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  if (!codec || !base || avcodec_parameters_to_context(base, par) < 0)
    return 1;
  base->pkt_timebase = stream->time_base;
  base->thread_count = base1 ? 1 : 8;
  base->thread_type = FF_THREAD_FRAME;
  base->flags |= AV_CODEC_FLAG_COPY_OPAQUE; // each picture knows its packet
  if (avcodec_open2(base, codec, NULL) < 0)
    return 1;

  struct dvbridge_fel* fel = dvbridge_fel_create(par, stream->time_base);
  Check(fel != NULL, "the module takes the stream");
  if (!fel)
    return 1;
  Check((fel->layer_length_size == 0) == annexbWanted, "and gives its decoder the framing it was given");
  printf("the base layer's reordering as the module read it from the stream: %d%s\n",
         fel->base_order.context->has_b_frames, g_noDelay ? "; made 0 here (nodelay)" : "");
  if (g_noDelay)
    fel->base_order.context->has_b_frames = 0;
  g_layerLength = fel->layer_length_size;
  // what dvbridge_fel_device() does with a VAAPI device, in software; the thread count stays the module's
  fel->decoder->get_format = avcodec_default_get_format;
#ifndef DVBRIDGE_FEL_SURFACES_MAX
  // a module that does not take its decoder's buffers itself has them from this test's pool directly
  fel->decoder->get_buffer2 = TestPoolBuffer;
#endif
#ifdef DVBRIDGE_FEL_WAITS_FOR_SURFACES
  // the limit of the layer's surfaces, which the module has from the pool of its decoder (hardware_format())
  if (g_pool)
    fel->surface_limit = g_pool;
#endif
  // as on the device of the base decoder (dvbridge_fel_device() opens it so)
  if (g_deviceAt && !(fel->decoder->hw_device_ctx = FakeDevice(0x1000)))
    return 1;
  const int threads = fel->decoder->thread_count;
  if (avcodec_open2(fel->decoder, codec, NULL) < 0)
    return 1;
  fel->opened = true;
  printf("decoder threads: base layer %d, enhancement layer %d\n", base->thread_count, threads);
  // the access units of the layer as the stream has them, split here as the module splits them
  AVBSFContext* split = NULL;
  if (av_bsf_list_parse_str("dovi_split=mode=el_rpu", &split) < 0 || avcodec_parameters_copy(split->par_in, par) < 0)
    return 1;
  split->time_base_in = stream->time_base;
  if (av_bsf_init(split) < 0)
    return 1;
  const int splitLength = unit_length_size(split->par_out->extradata, split->par_out->extradata_size);
  static uint64_t streamUnits[SENT_MAX];
  unsigned numStreamUnits = 0;

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  unsigned packets = 0;
  int end = 0, ok = 1;
  unsigned next = 0, read = 0, seeks = 0; // the number of the packet that comes, and the packets read
  while (!end && ok)
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
      // which packet of the file this is (by its time: after a seek the count goes on elsewhere)
      unsigned u = next + 1;
      if (u > g_numUnits || g_unitPts[u] != packet->pts)
        for (u = 1; u <= g_numUnits && g_unitPts[u] != packet->pts; ++u)
          ;
      next = u;
      ++read;
      if (g_start && u < g_start)
      {
        av_packet_unref(packet);
        continue;
      }
      if (u < UNITS_MAX)
        g_given[u] = 1;
      packet->opaque = (void*)(intptr_t)u;
      ++packets;
      if ((strip && packets == lastWithLayer) || (g_hole && packets == g_hole))
      {
        const int before = packet->size;
        if (av_packet_make_writable(packet) < 0)
          return 1;
        LayerUnits(packet, lengthSize, 1);
        memset(packet->data + packet->size, 0, before - packet->size);
        if (u < UNITS_MAX)
          g_given[u] = 0; // (its enhancement access unit is not given to the module)
        printf("packet %u: %d bytes of %d left, without its enhancement units\n", packets, packet->size, before);
      }
      if (g_badTerm && u == g_badTerm)
      {
        struct dvbridge_rpu_find find = {0};
        if (av_packet_make_writable(packet) < 0)
          return 1;
        dvbridge_rpu_units(packet->data, (size_t)packet->size, lengthSize, dvbridge_rpu_find_unit, &find);
        size_t last = find.rpu ? find.size : 0;
        while (last && !find.rpu[last - 1])
          --last;
        const int done = last >= 5 && find.rpu[last - 1] == 0x80;
        if (done)
        {
          ((uint8_t*)find.rpu)[last - 1] = 0x81;
          ((uint8_t*)find.rpu)[last - 2] ^= 0x01;
        }
        printf("packet %u: the terminator of its RPU %s\n", u, done ? "made 0x81, and its CRC32 not holding" : "NOT FOUND");
        if (!done)
          return 1;
      }
      if (annexb && (av_bsf_send_packet(annexb, packet) < 0 || av_bsf_receive_packet(annexb, packet) < 0))
      {
        printf("FAIL: packet %u could not be given start codes\n", packets);
        ok = 0;
        break;
      }
      if (avcodec_send_packet(base, packet) < 0)
        printf("the base decoder refused a packet\n");
      {
        AVPacket* c = av_packet_clone(packet);
        AVPacket* o = av_packet_alloc();
        if (c && o && av_bsf_send_packet(split, c) >= 0)
          while (av_bsf_receive_packet(split, o) >= 0)
          {
            if (has_layer_unit(o, splitLength) && numStreamUnits < SENT_MAX)
            {
              uint64_t h = UINT64_C(14695981039346656037);
              for (int i = 0; i < o->size; ++i)
              {
                h ^= o->data[i];
                h *= UINT64_C(1099511628211);
              }
              streamUnits[numStreamUnits++] = h;
            }
            av_packet_unref(o);
          }
        av_packet_free(&c);
        av_packet_free(&o);
      }
      if (!dvbridge_fel_submit(fel, packet))
      {
        printf("%s packet %u was not taken: %s\n", g_hole || g_kodi ? "note:" : "FAIL:", packets,
               dvbridge_fel_reason(fel));
        snprintf(g_stopped, sizeof(g_stopped), "%s", dvbridge_fel_reason(fel));
        // as Kodi, the layer starts again (ResyncDVEnhancement())
        if (g_kodi)
        {
          ++g_resyncs;
          NoteReason(dvbridge_fel_reason(fel));
          dvbridge_fel_reset(fel);
        }
        else
          ok = 0;
      }
      av_packet_unref(packet);
      // the base decoder on a new device context (Kodi's CDecoder::Open()), told as Kodi tells it
      if (g_deviceAt && read == g_deviceAt)
      {
        AVBufferRef* device = FakeDevice(g_deviceOther ? 0x2000 : 0x1000);
        const AVCodecContext* before = fel->decoder;
        const bool taken = device && dvbridge_fel_device(fel, device, true);
        g_deviceReset = fel->decoder != before || fel->synchronizing;
        g_deviceAtPicture = g_count;
        printf("after packet %u, a new device context of %s display: the module %s its decoder and %s its layer%s\n",
               read, g_deviceOther ? "another" : "the same", fel->decoder != before ? "opened anew" : "kept",
               fel->synchronizing ? "started again" : "went on with", taken ? "" : " (it failed)");
        av_buffer_unref(&device);
        if (!taken)
        {
          ++g_resyncs;
          dvbridge_fel_reset(fel);
        }
      }
      // Kodi's seek: to the time of the packet seekTo, backwards to a key frame; CDVDVideoCodecFFmpeg::Reset()
      if (g_seekAfter && !seeks && read == g_seekAfter && g_seekTo <= g_numUnits)
      {
        ++seeks;
        if (av_seek_frame(fc, index, g_unitPts[g_seekTo], AVSEEK_FLAG_BACKWARD) < 0)
          return 1;
        avcodec_flush_buffers(base);
        g_numPending = 0;
        dvbridge_fel_reset(fel);
#if HAVE_RING
        dvbridge_fel_base_flushed(fel);
#endif
        next = 0;
        g_seekAt = g_count;
        printf("seek after packet %u to the time of packet %u\n", read, g_seekTo);
      }
    }
    else
    {
      avcodec_send_packet(base, NULL);
      if (!dvbridge_fel_drain(fel))
      {
        printf("FAIL: the enhancement layer could not be drained: %s\n", dvbridge_fel_reason(fel));
        ok = 0;
      }
    }
    while (ok && avcodec_receive_frame(base, frame) >= 0)
    {
      if (g_numPending == 64)
      {
        printf("FAIL: 64 base pictures wait for their enhancement pictures\n");
        ok = 0;
        break;
      }
      struct picture* p = &g_pending[g_numPending++];
      memset(p, 0, sizeof(*p));
      p->pts = frame->pts;
      p->n = (unsigned)(intptr_t)frame->opaque;
      Read(frame, &p->base);
      // as Kodi's GetPairedPicture(): what the module is told of each base picture
      if (g_kodi)
      {
#if HAVE_OWN_RPU
        if (frame->pts != AV_NOPTS_VALUE)
#ifdef DVBRIDGE_FEL_RPUS
          // and whether it came with its coded RPU (Kodi: with its RPU and its metadata)
          dvbridge_fel_base_picture_rpu(fel, frame->pts,
                                        av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER) &&
                                            av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA));
#else
          dvbridge_fel_base_picture(fel, frame->pts);
#endif
#endif
#if HAVE_RING
        const AVFrameSideData* parsed = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        const AVFrameSideData* coded = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
        if (parsed && coded)
          dvbridge_fel_base_metadata(fel, coded->data, coded->size, parsed->data, parsed->size);
#endif
      }
      av_frame_unref(frame);
      if (g_numPending > g_maxPending)
        g_maxPending = g_numPending;
      ok = Emit(fel, 0);
    }
    if (ok && end)
    {
      // the enhancement decoder gives its last pictures as it is asked for them
      for (int i = 0; i < 64 && g_numPending && ok; ++i)
        ok = Emit(fel, i == 63);
    }
  }
  // every access unit the module gave its decoder is one of the stream, as it is there
  {
    unsigned found = 0;
    for (unsigned i = 0; i < g_numSent; ++i)
    {
      unsigned k = 0;
      while (k < numStreamUnits && streamUnits[k] != g_sent[i])
        ++k;
      found += k < numStreamUnits;
    }
    printf("access units given to the decoder of the layer %u, %u of them as the stream has them (%u in the "
           "stream)\n", g_numSent, found, numStreamUnits);
    Check(g_numSent > 0 && found == g_numSent, "the module gives its decoder the access units of the layer as "
                                              "the stream has them: framing and bytes");
  }
  if (g_deviceAt)
    Check(g_deviceReset == g_deviceOther, g_deviceOther ? "the base decoder on another display: the layer "
                                                          "starts again"
                                                        : "the base decoder on a new device context of the "
                                                          "same display: the layer goes on");
  // the pictures of this run, one a line: pts packet paired layer-hash metadata how
  if (getenv("FEL_PICTURES"))
  {
    FILE* out = fopen(getenv("FEL_PICTURES"), "w");
    for (unsigned i = 0; out && i < g_count; ++i)
      fprintf(out, "%lld %u %d %016llx %016llx %d\n", (long long)g_pictures[i].pts, g_pictures[i].n,
              g_pictures[i].paired, (unsigned long long)g_pictures[i].layerHash,
              (unsigned long long)g_pictures[i].meta, g_pictures[i].how);
    if (out)
      fclose(out);
  }
  // each picture against the RPU of its own access unit (FEL_TRUTH), and against a whole run (FEL_WHOLE)
  if (g_haveTruth && g_kodi) // (as Kodi gives each picture its metadata)
  {
    unsigned pairedOwn = 0, pairedOther = 0, withoutOwn = 0, withoutOther = 0, withoutBefore = 0, unseen = 0,
             unknown = 0, carriedOther = 0, paired = 0, fromTake = 0, fromCall = 0, otherLate = 0,
             otherAfterDevice = 0;
    for (unsigned i = 0; i < g_count; ++i)
    {
      const struct picture* p = &g_pictures[i];
      const unsigned m = p->n < UNITS_MAX ? g_truth[p->n] : 0;
      const uint64_t own = m ? g_unitRpu[m] : 0;
      if (i == g_seekAt)
        paired = 0; // (the layer starts again after the seek)
      if (!m)
        unknown += !p->paired;
      else if (p->paired)
        ++*(p->meta == own ? &pairedOwn : &pairedOther);
      else if (p->meta == own)
        ++withoutOwn;
      else if (!g_given[m])
        ++unseen;
      else if (g_deviceOther && i >= g_deviceAtPicture)
        ++otherAfterDevice;
      else if (p->late && paired)
        ++otherLate;
      else
        ++*(paired ? &withoutOther : &withoutBefore);
      carriedOther += m && !p->paired && (p->base.own ? RpuHash(p->base.rpu, p->base.coded) : 0) != own;
      g_wrongGiven += m && !p->paired && (p->how == 2 || p->how == 3) && p->meta != own;
      paired += p->paired;
      fromTake += p->how == 2;
      fromCall += p->how == 3;
    }
    unsigned ranked = 0, ownCount = 0;
    (void)ownCount;
#if HAVE_OWN_RPU
    ownCount = dvbridge_fel_own_rpus(fel);
    ranked = dvbridge_fel_ranked(fel);
#endif
    printf("%u pictures, %u paired (%u with their own RPU, %u with another one); without their enhancement "
           "picture %u with their own RPU (%u by an answer 3 or 4, %u by dvbridge_fel_layer_rpu()), %u with another one "
           "though the module was given their access unit (and %u before the first paired picture), %u whose access "
           "unit it was not given, %u of no access unit; %u of them carried another one than their own; late %u, "
           "resyncs %u; starts at a counted place %u\n",
           g_count, paired, pairedOwn, pairedOther, withoutOwn, fromTake, fromCall, withoutOther, withoutBefore, unseen,
           unknown, carriedOther, g_late, g_resyncs, ranked);
    if (otherLate)
      printf("... and %u with another one that went out late (Kodi stopped waiting while the module waited "
             "for their enhancement picture, whose access unit it had not decoded)\n", otherLate);
    if (otherAfterDevice)
      printf("... and %u with another one after the switch to another display (this test's device is no "
             "device: the decoder the module opens on it fails, and the layer cannot come back here)\n",
             otherAfterDevice);
    Check(!pairedOther, "every paired picture has the RPU of its own enhancement access unit");
    printf("pictures that the module gave another RPU than their own: %u\n", g_wrongGiven);
    Check(!g_wrongGiven, "the module never gives a picture another RPU than its own");
    Check(!withoutOther, "once the layer has started, every picture that goes out without its enhancement "
                         "picture has the RPU of its own access unit");
  }
  if (g_kodi)
  {
    unsigned paired = 0, first = g_count;
    for (unsigned i = g_seekAt == 0xffffffffu ? 0 : g_seekAt; i < g_count; ++i)
    {
      paired += g_pictures[i].paired;
      if (g_pictures[i].paired && first == g_count)
        first = i;
    }
    printf("as Kodi, %u pictures%s, %u of them paired, the first one picture %u of them; late %u, resyncs %u\n",
           g_count - (g_seekAt == 0xffffffffu ? 0 : g_seekAt), g_seekAt == 0xffffffffu ? "" : " after the seek",
           paired, first - (g_seekAt == 0xffffffffu ? 0 : g_seekAt), g_late, g_resyncs);
    unsigned compared = 0, differ = 0, absent = 0;
    FILE* in = getenv("FEL_WHOLE") ? fopen(getenv("FEL_WHOLE"), "r") : NULL;
    if (in)
    {
      static long long wPts[PICTURES_MAX];
      static unsigned long long wHash[PICTURES_MAX], wMeta[PICTURES_MAX];
      static int wPaired[PICTURES_MAX];
      unsigned wCount = 0, n;
      int how;
      while (wCount < PICTURES_MAX && fscanf(in, "%lld %u %d %llx %llx %d", &wPts[wCount], &n, &wPaired[wCount],
                                             &wHash[wCount], &wMeta[wCount], &how) == 6)
        ++wCount;
      fclose(in);
      for (unsigned i = 0; i < g_count; ++i)
      {
        const struct picture* p = &g_pictures[i];
        if (!p->paired)
          continue;
        unsigned k = 0;
        while (k < wCount && wPts[k] != p->pts)
          ++k;
        if (k == wCount || !wPaired[k])
          ++absent;
        else
        {
          ++compared;
          differ += wHash[k] != p->layerHash || wMeta[k] != p->meta;
        }
      }
      printf("paired pictures compared with the whole run %u, different %u, not paired there %u\n", compared,
             differ, absent);
      Check(compared > 0 && !differ && !absent, "every paired picture has the enhancement picture and the "
                                                "metadata it has in the whole run");
    }
  }
  for (unsigned i = 0; i < g_numHeld; ++i)
    av_frame_free(&g_held[i]);
  g_numHeld = 0;
  SurfaceChecks(fel);
  RpuChecks(fel);
  if (g_seekAfter || g_start || g_deviceAt)
  {
    printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
    return g_failures != 0;
  }
  // FEL_HASHES: the paired enhancement pictures of this run, one "pts hash" a line
  if (getenv("FEL_HASHES"))
  {
    FILE* out = fopen(getenv("FEL_HASHES"), "w");
    for (unsigned i = 0; out && i < g_count; ++i)
      if (g_pictures[i].paired)
        fprintf(out, "%lld %016llx\n", (long long)g_pictures[i].pts, (unsigned long long)g_pictures[i].layerHash);
    if (out)
      fclose(out);
  }
  if (g_hole)
  {
    // FEL_CLEAN: the hashes of a run without the hole. Every enhancement picture that is paired with the hole
    // has to be the one of that run: the decoder must not give a picture made from a reference that is
    // missing, which would add a wrong residual to the base picture.
    unsigned without = 0, first = 0, last = 0, compared = 0, different = 0, absent = 0, pairedAfter = 0;
    FILE* in = getenv("FEL_CLEAN") ? fopen(getenv("FEL_CLEAN"), "r") : NULL;
    long long cleanPts = 0;
    unsigned long long cleanHash = 0;
    int have = in && fscanf(in, "%lld %llx", &cleanPts, &cleanHash) == 2;
    for (unsigned i = 0; i < g_count; ++i)
    {
      const struct picture* p = &g_pictures[i];
      if (!p->paired)
      {
        if (!without++)
          first = i;
        last = i;
        continue;
      }
      pairedAfter += without > 0;
      while (have && cleanPts < p->pts)
        have = fscanf(in, "%lld %llx", &cleanPts, &cleanHash) == 2;
      if (!have || cleanPts != p->pts)
        ++absent;
      else
      {
        ++compared;
        different += cleanHash != p->layerHash;
      }
    }
    if (in)
      fclose(in);
    printf("hole at packet %u: %u pictures went out, %u of them without their enhancement picture (pictures %u "
           "to %u), %u paired after the first of those%s%s\n",
           g_hole, g_count, without, first, last, pairedAfter, ok ? "" : "; the module stopped: ",
           ok ? "" : g_stopped);
    printf("paired enhancement pictures compared with the run without the hole: %u, different %u, not in that "
           "run %u\n", compared, different, absent);
    Check(ok || !strcmp(g_stopped, "enhancement decoder error"),
          "a hole in the layer: the pictures that cannot have their enhancement picture go out without it");
    Check(in && compared > 0 && !different && !absent,
          "every enhancement picture paired with the hole is the picture of the stream without it: none was "
          "made from a reference that is missing");
    printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
    return g_failures != 0;
  }
  Check(ok, "every packet was taken and no pairing failed");

  unsigned paired = 0, ownLayer = 0, ownBase = 0, taken = 0, agree = 0, monotone = 1;
  unsigned mismatches = dvbridge_fel_order_mismatches(fel, NULL);
  for (unsigned i = 0; i < g_count; ++i)
  {
    const struct picture* p = &g_pictures[i];
    paired += p->paired;
    ownLayer += p->paired && p->layer.own;
    ownBase += p->base.own;
    taken += p->paired && p->takes;
    // the comparison made here, apart from the module's: the two RPUs without their trailing zero bytes
    const int same = p->layer.own && p->base.own && p->layer.size == p->base.size &&
                     !memcmp(p->layer.rpu, p->base.rpu, p->base.size);
    agree += p->paired && p->takes == (p->layer.own && !same);
    if (i && p->pts <= g_pictures[i - 1].pts)
      monotone = 0;
  }
  unsigned runsLayer, distinctLayer, runsBase, distinctBase, runsAll, distinctAll, runsAllBase, distinctAllBase;
  Runs(1, 0, 1, &runsLayer, &distinctLayer);
  Runs(0, 0, 0, &runsBase, &distinctBase);
  Runs(1, 1, 0, &runsAll, &distinctAll);
  Runs(0, 1, 0, &runsAllBase, &distinctAllBase);
  printf("packets %u, pictures %u, paired %u, most base pictures waiting %u\n", packets, g_count, paired,
         g_maxPending);
  printf("with an RPU of their own: base pictures %u, paired enhancement pictures %u\n", ownBase, ownLayer);
  printf("paired pictures that keep the RPU of their packet (the same) %u, that take the one of their "
         "enhancement picture %u\n", ownLayer - taken, taken);
  printf("enhancement pictures of another picture order than the base picture of their packet: %u\n",
         mismatches);
  printf("level 1 over the paired pictures, metadata the rule gives: %u values in %u runs\n", distinctLayer,
         runsLayer);
  printf("level 1 over the paired pictures, RPU of the packet of the base picture: %u values in %u runs\n",
         distinctBase, runsBase);
  printf("level 1 over all %u pictures (%u went out without their enhancement picture and keep the RPU of "
         "their packet): %u values in %u runs; with the RPU of the packet for all, %u values in %u runs\n",
         g_count, g_count - paired, distinctAll, runsAll, distinctAllBase, runsAllBase);

  Check(monotone, "the pictures went out in presentation order");
  Check(g_count == packets, "every packet gave its picture");
  Check(!g_tooLong, "every RPU fits in the buffer of this test");
  Check(paired == withLayer - (strip ? 1 : 0),
        strip ? "every picture is paired with its layer but the one whose packet lost it"
              : "every picture whose packet has enhancement units is paired with its layer");
  if (g_rpuInBase)
    Check(ownLayer == 0, "FEL_RPU_IN_BASE: no enhancement picture brings an RPU (each one travels in the base "
                         "access unit of its picture)");
  else
    Check(ownLayer == paired, "every paired enhancement picture brings the RPU of its own access unit");
  Check(agree == paired, "the rule decides as a comparison of the two RPUs made here");
  Check(distinctLayer > 1 && runsLayer == distinctLayer,
        "with the metadata the rule gives, each level 1 value comes in one run");
  if (g_rpuInBase)
  {
    Check(mismatches > 0, "FEL_RPU_IN_BASE: the layers differ in picture order");
    Check(taken == 0, "FEL_RPU_IN_BASE: no picture takes another RPU than the one of its packet, its own");
  }
  else if (mismatches)
  {
    Check(runsBase > distinctBase,
          "with the RPU of the base picture's packet it does not (the layers differ in picture order)");
    Check(taken > g_count / 2, "and most pictures get another RPU than the one of their packet");
  }
  else
  {
    Check(taken == 0, "the layers share their picture order: no picture takes another RPU");
  }
  dvbridge_fel_destroy(fel);
  avcodec_free_context(&base);
  av_bsf_free(&annexb);
  av_bsf_free(&split);
  av_packet_free(&packet);
  av_frame_free(&frame);
  avformat_close_input(&fc);
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
