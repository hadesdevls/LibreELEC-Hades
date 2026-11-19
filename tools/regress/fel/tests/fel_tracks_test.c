// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of Kodi's enhancement-layer module (tools/dvbridge/dvbridge_fel.c, the file itself) with a Dolby
// Vision stream that comes in two tracks, read and joined as Kodi's demuxer does it (CDVLayerMerge, through
// merge_shim.cpp) and decoded in software.
//
// A joined packet says when its enhancement access unit is presented, and the module pairs the pictures by
// that time. Before that it paired them by picture order, as it still does for a stream of one track. The
// two ways know nothing of each other, and the layers of the test stream are coded in different picture
// orders, so:
// - every picture that both ways pair has to get the same enhancement picture;
// - by its times, the pictures that are paired have to be exactly the ones whose enhancement access unit was
//   given to the module from the first random-access picture of the layer on, read here from the stream
//   itself: also after seeks, where the two layers do not share a random-access picture;
// - an access unit without a time is a hole in the layer and nothing else;
// - a stream that times only some of its access units is paired by picture order from then on, and stays
//   so.
//
// Which enhancement picture a base picture got is known by the access unit, not by what the picture looks
// like (an eighth of the pictures of the test stream look like a neighbour): every packet is numbered, the
// decoders hand the number on to their pictures (AV_CODEC_FLAG_COPY_OPAQUE), and a pair is right when the
// stream presents the enhancement access unit of the one packet at the time of the base picture of the
// other, by the times of the stream as they were read, before anything was done to them.
//
// The times go the way they go in Kodi: from the stream's time base to seconds in floating point, less the
// start time, to microseconds cut off (CDVDDemuxFFmpeg::ConvertTimestamp(), CDVDVideoCodecFFmpeg::AddData()).
// Base pictures wait for their enhancement pictures as in CDVDVideoCodecFFmpeg::GetPairedPicture(), with
// its limits. That part is written again here, and so is what the demuxer and the decoder do around the
// module (they cannot be built for the host): the runs marked "as it was" leave one of those out, to show
// what it is there for.
//
// With a module that does not have those times yet, only the runs by picture order are made and shown.
//
// the RPU each picture goes out with, and the layer by picture order after a seek.
// - An RPU travels in the enhancement access unit of its picture, which in this stream is mostly carried with
//   the base picture of another one. What the stream says of every access unit (its time, read before anything
//   is done to it) gives the RPU of each picture: the one of the access unit presented at its time. Each picture
//   is checked against it, with the metadata Kodi gives it (UseLayerDVMetadata(), and the answers 4 and
//   dvbridge_fel_layer_rpu() of the module). A paired picture always has its own; one without its enhancement
//   picture has it when the module could know its access unit (by the times of the two tracks: it was given to
//   the module with its time; by picture order: once the layer has started).
// - By picture order the layer starts again after a seek at a random-access picture of its own, also one it does
//   not share with the base layer, and also where the base layer reorders its pictures (swap): as by the times,
//   but for the leading pictures of that picture.
// The pictures of the whole stream, by both ways, go to FEL_PICTURES (a file) when it is set, to compare the
// runs of two modules picture by picture.
//
// Usage: fel_tracks_test <file with the two tracks: transport stream, MP4 or Matroska> [base1] [quick] [moved]
//                        [swap] [starts] [noconfig]
//   noconfig  the module does not get the hvcC record of an MP4 or Matroska enhancement track
//   FEL_DUMP=<file> in the environment: the pictures of the run by the times, for a comparison of containers
//   base1  the base layer is decoded with one thread (slow), as with a hardware decoder: its pictures come
//          out as early as they can, and wait longest for their enhancement pictures
//   quick  without the five seeks that are played by both ways, and without the runs with damaged times
//   moved  with quick: the runs in which the reader or the player moves the times are made all the same
//   swap   the two tracks change places. The base track of the test stream is coded in the order it is
//          presented, and its enhancement track is not: swapped, the base layer is the one that is reordered
//   starts with quick: the files that start at a random-access picture of the base layer only
#include "merge_shim.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mathematics.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The module only takes VAAPI surfaces from its decoder. Here the decoder runs in software.
#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI

#ifdef DVBRIDGE_FEL_LAYER_TIME
#define HAVE_TIMES 1
#else
#define HAVE_TIMES 0
#define DVBRIDGE_FEL_LAYER_TIME ""
static bool dvbridge_fel_timed(const struct dvbridge_fel* f) { (void)f; return false; }
static unsigned dvbridge_fel_untimed(const struct dvbridge_fel* f) { (void)f; return 0; }
#endif
// DVBRIDGE_FEL_LAYER_TIME_UNUSABLE: a later picture that waits says that the one asked for was not decoded,
// a time that two base packets had is of no picture, and only what picture order would pair counts
// towards pairing by picture order.
#ifdef DVBRIDGE_FEL_LAYER_TIME_UNUSABLE
#define HAVE_TIME_UNUSABLE 1
#else
#define HAVE_TIME_UNUSABLE 0
#endif
// BASE_TIMES_MAX: a time that an earlier base packet had starts the layer again, unless it
// is 0, which pairs no picture.
#ifdef BASE_TIMES_MAX
#define HAVE_BASE_TIMES 1
#else
#define HAVE_BASE_TIMES 0
#endif
// dvbridge_fel_time_zero(): the decoder says which time stands for the 0 of the demuxer
// (dvbridge_fel_time_zero()). A build against an older module is told so with -DOLD_MODULE.
#if HAVE_BASE_TIMES && !defined(OLD_MODULE)
#define HAVE_TIME_ZERO 1
#else
#define HAVE_TIME_ZERO 0
#endif
// dvbridge_fel_base_flushed(): a layer that starts again for a failure of its own, while the base
// layer goes on, does not forget the base packets of the time that stands for 0; a flush of the base
// decoder is told (dvbridge_fel_base_flushed()). A build against the module before it is told so
// with -DBEFORE_BASE_FLUSHED_MODULE.
#if HAVE_TIME_ZERO && !defined(BEFORE_BASE_FLUSHED_MODULE)
#define HAVE_BASE_FLUSHED 1
#else
#define HAVE_BASE_FLUSHED 0
#endif
// The module keeps the metadata of the base pictures for the RPUs of access units that are
// not decoded (dvbridge_fel_base_metadata()), which Kodi gives it.
#ifdef RING_MAX
#define HAVE_RING 1
#else
#define HAVE_RING 0
#endif
// each picture without its enhancement picture takes the RPU of its own access unit where the module knows
// it, and the layer, by picture order, starts at a random-access picture whose place it counts.
#ifdef RANK_SPAN
#define HAVE_OWN_RPU 1
#else
#define HAVE_OWN_RPU 0
#endif
#define RPU_MAX 4096
// tools/regress: the module under test has every part that this test checks; a build against a module without
// one of them fails here instead of passing with fewer checks
#if !HAVE_TIMES || !HAVE_TIME_UNUSABLE || !HAVE_BASE_TIMES || !HAVE_TIME_ZERO || !HAVE_BASE_FLUSHED || !HAVE_RING || \
    !HAVE_OWN_RPU
#error "the module under test lacks a part that this test checks"
#endif

#define PICTURES_MAX 4096
#define PACKETS_MAX 4096
#define LATE_PENDING 6 // DV_EL_LATE_PENDING of DVDVideoCodecFFmpeg.cpp
#define LATE_RESYNC 3  // DV_EL_LATE_RESYNC
#define LATE_RESYNC_TIMED 96 // DV_EL_LATE_RESYNC_TIMED

struct picture
{
  int64_t pts; // microseconds, as Kodi has it; a guess of the decoder when the picture is not timed
  int own;     // the packet it was decoded from, by its number in the run
  int timed;   // the decoder gave it the time of that packet
  int paired;
  int layer;     // the packet that carried the enhancement picture it is paired with
  uint64_t hash; // of the samples of that picture
  // the RPU it goes out with (a hash of it as coded; 0: none), the one carried with its base picture,
  // and how it came to it: 0 the one carried with it, 1 its enhancement picture's, 2 an answer 4 of the module,
  // 3 dvbridge_fel_layer_rpu()
  uint64_t meta, carried;
  int how;
};

struct options
{
  int timed;             // 0: the times are taken off the packets before the module sees them
  double seek;           // the part of the duration to seek to first; negative: from the start
  unsigned limit;        // base packets to read; 0: all
  unsigned gap;          // this base packet loses the time of its enhancement unit; 0: none
  unsigned every;        // and so does every packet whose number is a multiple of this; 0: none
  unsigned sparseUntil;  // every second base packet up to this one loses it; then the stream is played
                         // again from its start, as it is
  int bare;              // the module gets no parameter sets but the ones in the stream: after a seek it
                         // cannot read the access units of the layer before its next random-access picture
  int base1;
  unsigned noPts;        // this base packet comes without its presentation time (its decoding time stays)
  unsigned unknownFrom, unknownCount; // so many packets from this one on come without any time: what the
                                      // player does while it is not sure of a jump in the times
  double startLater;     // the file starts this many seconds after its first video packet, by its start time
  int64_t startTicks;    // ... or at this time of the base track, in its time base; 0: by its start time
  unsigned resyncAfter;  // the layer starts again once this packet has been given to it (a failure of its
                         // own: the base layer goes on); 0: not
  unsigned untimedUntil; // the packets up to this one lose the time of their enhancement unit; 0: none
  unsigned skipUntil;    // the packets up to this one are not played (playing begins after them)
  unsigned replayAfter;  // after this packet the stream is played again from its start, as after a seek
                         // to it (the decoders are flushed); 0: not
  unsigned offsetFrom;   // from this packet on the player takes offsetSeconds off every time
  double offsetSeconds;
  int64_t offsetTicks;   // ... and the stream itself takes this many ticks of its time base off its times
  // as it was:
  int guess;   // a base picture without the time of its packet is paired by the time the decoder guesses,
               // and three late pictures in a row start the layer again whatever it is paired by
  int noCheck; // the demuxer does not check the time of an enhancement unit against the times it makes
  int noGuard; // the decoder does not start the layer again when the player moves the times
};

struct outcome
{
  struct picture pictures[PICTURES_MAX];
  unsigned count, paired, packets, late, resyncs, untimed, mismatches, maxPending;
  unsigned wrong;          // pictures paired with the enhancement picture of another time
  int wrongOwn, wrongLayer; // the first of them: its packet, and the packet of the picture it got
  unsigned unverified;     // pictures paired with an enhancement picture whose time the stream does not say
  unsigned unusable;       // times of enhancement units that the demuxer could not use
  unsigned ambiguous;      // pictures that went out without the layer because their time was not theirs alone
  unsigned repeats;        // resynchronizations because a base packet had the time of an earlier one
  int replayZeroPaired;    // with replayAfter: the picture of the time 0 was paired before the stream was
                           // played again
  double tick;             // the time base of the base track, in seconds
  int timed;
  char reason[160]; // of the first resynchronization
  char fallback[160]; // of the one by which the module went over to picture order
  // what the stream says, read here and not by the module
  unsigned withTime, withoutTime; // base packets with enhancement units, by whether they say its time
  int64_t firstKey;               // time of the first random-access unit of the layer that has one
  int64_t layerTimes[PICTURES_MAX]; // times of the enhancement units from that one on
  unsigned numLayerTimes;
  unsigned differentTimes; // of them, presented at another time than the base picture of their packet
  unsigned keys[8], numKeys; // the first packets with a random-access unit of the layer
  unsigned baseKeys[32], numBaseKeys; // ... and with a random-access picture of the base layer
  // the first part of a run with sparseUntil
  unsigned sparseUntimed;
  int sparseTimedAfter;
  char sparseReason[160];
  // MEL (dvbridge_fel_layer_skip_counts()): enhancement access units decoded and not, times it stopped, and
  // why the layer is decoded for good
  unsigned layerDecoded, layerNotDecoded, layerStops;
  char layerKept[160];
  // the RPU each picture went out with, against the one of the access unit that the stream presents at its
  // time (its own)
  unsigned pairedOwn, pairedOther;   // paired pictures with their own RPU, and with another one
  unsigned withoutOwn;               // pictures without their enhancement picture, with their own RPU
  unsigned withoutOther;             // ... with another one, though their access unit was given to the module, with
                                     // its time when the stream is paired by times, after the first paired picture
  unsigned withoutOtherBefore;       // ... the same before the first paired picture
  unsigned withoutUnseen;            // ... with another one, their access unit not given (it came before the seek, or
                                     // without its time)
  unsigned withoutUnknown;           // the stream presents no access unit at their time
  unsigned withoutUnread;            // ... with another one: the base picture that brought their RPU never came out of
                                     // the base decoder in this run, and nothing else reads an RPU
  unsigned fromTake, fromCall;       // own RPUs by answers 4 of the module, and by dvbridge_fel_layer_rpu()
  unsigned ranked;                   // starts of the layer at a place it counted
  unsigned own, carriedOther;        // without their enhancement picture: own RPUs counted by the module, and pictures
                                     // whose carried RPU is not their own (what they had before)
};

static int g_failures;
static int g_noConfig; // the module does not get the hvcC record of the enhancement track, as before the
                       // join took MP4 and Matroska pairs
static AVRational g_rate; // frame rate of the base track, to name a picture by its place in the stream
static int g_swap;     // the two tracks change places
static double g_start; // of the file, in seconds, as the demuxer has it
// By the number of a packet in the run: when the stream presents its base picture, and its enhancement
// access unit (AV_NOPTS_VALUE: it does not say), in the time base of the base track.
static int64_t g_own[PACKETS_MAX], g_layer[PACKETS_MAX];
// ... and how often the player had moved the times when it came (what the decoder hands on to its picture)
static unsigned g_epoch[PACKETS_MAX];
// what the stream says of every enhancement access unit, read once from the whole stream: when it is
// presented (time base of the base track) and its RPU as coded (a hash; 0: none), sorted by time. In a run, whether
// the module was given it (with its time, in a run by times).
static int64_t g_truthTime[PACKETS_MAX];
static uint64_t g_truthRpu[PACKETS_MAX];
static unsigned g_numTruth;
static unsigned char g_truthGiven[PACKETS_MAX];
// ... and, in a run, the packet that brought each of them, and the packets whose base picture the base decoder gave
static unsigned g_truthPacket[PACKETS_MAX];
static unsigned char g_outputPacket[PACKETS_MAX];
// ... and by the number of a base packet from the start of the stream: whether its base picture, and its enhancement
// access unit, are random-access pictures
static unsigned char g_streamBaseKey[PACKETS_MAX], g_streamLayerKey[PACKETS_MAX];
static unsigned g_streamPackets;
// the RPUs carried with the base pictures that wait (as coded), for the rule of the module
static unsigned g_wrongGiven; // pictures that the module gave another RPU than their own, in all the runs
static unsigned g_wrongGivenAsItWas; // ... in the runs "as it was" (a protection of Kodi's left out)
static uint8_t g_pendingRpu[64][RPU_MAX];
static size_t g_pendingRpuSize[64];
static unsigned g_rpuTooLong;

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

// a check of the RPU each picture goes out with. Not with the two tracks swapped: the RPU is then in the
// access units of the base layer and comes before the units of the enhancement layer, which FFmpeg's decoder does
// not take as the RPU of an enhancement picture; nothing of the stream says whose it is (shown, not checked).
static void CheckRpu(int ok, const char* what)
{
  if (g_swap)
    printf("not checked, the tracks swapped: %s (%s)\n", what, ok ? "it holds" : "it does not hold");
  else
    Check(ok, what);
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

// CDVDDemuxFFmpeg::ConvertTimestamp(): the time of the player, in microseconds
static double KodiDvd(int64_t pts, AVRational timeBase)
{
  double timestamp = (double)pts * timeBase.num / timeBase.den;
  if (timestamp > g_start)
    timestamp -= g_start;
  else if (timestamp + 0.5 > g_start)
    timestamp = 0;
  return timestamp * 1000000;
}

static double ConvertForMerge(int64_t pts, void* timeBase)
{
  return KodiDvd(pts, *(const AVRational*)timeBase);
}

// ... less what the player takes off it (CVideoPlayer::UpdateCorrection()), as
// CDVDVideoCodecFFmpeg::AddData() gives it to the decoder
static int64_t KodiTime(int64_t pts, AVRational timeBase, double offsetSeconds)
{
  if (pts == AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;
  const double dvd = KodiDvd(pts, timeBase) - offsetSeconds * 1000000;
  return (int64_t)(dvd / 1000000 * 1000000);
}

// The units of a joined packet (start codes, or a length of g_lengthSize bytes before each unit): whether it
// has units of the enhancement layer (type 63, the unit of the layer after two bytes), and whether one of them
// is of a random-access picture (types 16 to 23); and whether the base picture of the packet is one.
static int g_lengthSize;
static void LayerUnits(const AVPacket* packet, int* has, int* key, int* baseKey)
{
  const uint8_t* d = packet->data;
  const int n = packet->size;
  *has = *key = *baseKey = 0;
  if (g_lengthSize)
  {
    for (int i = 0; i + g_lengthSize + 3 <= n;)
    {
      unsigned length = 0;
      for (int k = 0; k < g_lengthSize; ++k)
        length = length << 8 | d[i + k];
      i += g_lengthSize;
      if (length > (unsigned)(n - i))
        break;
      if (length >= 3)
      {
        const int type = d[i] >> 1 & 0x3f;
        if (type == 63)
        {
          const int inner = d[i + 2] >> 1 & 0x3f;
          *has = 1;
          *key |= inner >= 16 && inner <= 23;
        }
        else
          *baseKey |= type >= 16 && type <= 23;
      }
      i += (int)length;
    }
    return;
  }
  for (int i = 0; i + 5 < n; ++i)
  {
    if (d[i] || d[i + 1] || d[i + 2] != 1)
      continue;
    const int type = d[i + 3] >> 1 & 0x3f;
    if (type == 63)
    {
      const int inner = d[i + 5] >> 1 & 0x3f;
      *has = 1;
      *key |= inner >= 16 && inner <= 23;
    }
    else
      *baseKey |= type >= 16 && type <= 23;
    i += 2;
  }
}

// What a joined packet says of the time of its enhancement access unit: the difference to its own.
static int LayerDifference(const AVPacket* packet, int64_t* difference, AVRational* timeBase)
{
  size_t size = 0;
  const uint8_t* data = av_packet_get_side_data(packet, AV_PKT_DATA_STRINGS_METADATA, &size);
  AVDictionary* dictionary = NULL;
  int ok = 0;
  if (data && av_packet_unpack_dictionary(data, size, &dictionary) >= 0)
  {
    const AVDictionaryEntry* entry = av_dict_get(dictionary, merge_layer_time_key(), NULL, 0);
    long long value;
    int num, den;
    if (entry && sscanf(entry->value, "%lld %d %d", &value, &num, &den) == 3 && num > 0 && den > 0)
    {
      *difference = value;
      *timeBase = (AVRational){num, den};
      ok = 1;
    }
  }
  av_dict_free(&dictionary);
  return ok;
}

// an RPU as coded, without the zero bytes after it (a hash; 0: none)
static uint64_t RpuHash(const uint8_t* data, size_t size)
{
  while (size && !data[size - 1])
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

// ... the one of a joined packet, as FFmpeg's HEVC decoder takes it (dvbridge_mel.h); its units
// framed as the join frames them: start codes, or the length of an MP4 or Matroska track (g_lengthSize)
static uint64_t PacketRpu(const AVPacket* packet)
{
  struct dvbridge_rpu_find find = {0};
  dvbridge_rpu_units(packet->data, (size_t)packet->size, g_lengthSize, dvbridge_rpu_find_unit, &find);
  return find.rpu ? RpuHash(find.rpu + 2, find.size - 2) : 0;
}

static int TruthIndex(int64_t time)
{
  unsigned lo = 0, hi = g_numTruth;
  while (lo < hi)
  {
    const unsigned mid = (lo + hi) / 2;
    if (g_truthTime[mid] < time)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo < g_numTruth && g_truthTime[lo] == time ? (int)lo : -1;
}

// The whole stream, read as it is: the time and the RPU of every enhancement access unit that says its time.
static int BuildTruth(const char* path)
{
  struct merge_source* source = merge_open(path);
  if (!source)
    return 0;
  if (g_swap)
    merge_swap(source);
  const int index = merge_base(source);
  const AVStream* stream = merge_context(source)->streams[index];
  AVPacket* packet = av_packet_alloc();
  // (the framing of this source's units, before anything reads them)
  g_lengthSize = merge_length_size(source);
  g_numTruth = 0;
  g_streamPackets = 0;
  while (merge_read(source, packet) >= 0)
  {
    int has, key, baseKey;
    int64_t difference;
    AVRational timeBase;
    LayerUnits(packet, &has, &key, &baseKey);
    if (packet->stream_index == index && g_streamPackets < PACKETS_MAX - 1)
    {
      ++g_streamPackets;
      g_streamBaseKey[g_streamPackets] = (unsigned char)baseKey;
      g_streamLayerKey[g_streamPackets] = (unsigned char)(has && key);
    }
    if (packet->stream_index == index && has && packet->pts != AV_NOPTS_VALUE &&
        LayerDifference(packet, &difference, &timeBase) && g_numTruth < PACKETS_MAX)
    {
      const int64_t time = packet->pts + av_rescale_q(difference, timeBase, stream->time_base);
      unsigned k = g_numTruth++;
      while (k && g_truthTime[k - 1] > time)
      {
        g_truthTime[k] = g_truthTime[k - 1];
        g_truthRpu[k] = g_truthRpu[k - 1];
        --k;
      }
      g_truthTime[k] = time;
      g_truthRpu[k] = PacketRpu(packet);
    }
    av_packet_unref(packet);
  }
  av_packet_free(&packet);
  merge_close(source);
  for (unsigned i = 1; i < g_numTruth; ++i)
    if (g_truthTime[i] == g_truthTime[i - 1])
      return 0;
  return g_numTruth > 0;
}

struct state
{
  struct dvbridge_fel* fel;
  struct outcome* out;
  const struct options* o;
  struct picture pending[64];
  unsigned numPending;
  int failed; // m_dvFailed
  unsigned lateInRow;
  double offset;  // what the player took off the times of the last packet
  unsigned epoch; // m_dvEpoch: how often that has changed, from 1
};

// CDVDVideoCodecFFmpeg::ResyncDVEnhancement()
static void Resync(struct state* s, const char* reason)
{
  if (!s->out->resyncs++)
    snprintf(s->out->reason, sizeof(s->out->reason), "%s", reason);
  if (strstr(reason, "paired by picture order") && !s->out->fallback[0])
    snprintf(s->out->fallback, sizeof(s->out->fallback), "%s", reason);
  if (strstr(reason, "time of an earlier one"))
    ++s->out->repeats;
  dvbridge_fel_reset(s->fel);
  s->failed = 0;
  s->lateInRow = 0;
}

// The lambda of CDVDVideoCodecFFmpeg::GetPairedPicture(): the oldest waiting base picture goes out when its
// enhancement picture is there, or cannot be had. Returns whether one went out.
static int Emit(struct state* s, int baseEnded)
{
  if (!s->numPending)
    return 0;
  struct picture* p = &s->pending[0];
  AVFrame* layer = NULL;
  const int timed = dvbridge_fel_timed(s->fel);
  const int sameTimes = s->o->noGuard || !timed ||
                        (p->own > 0 && p->own < PACKETS_MAX && g_epoch[p->own] == s->epoch);
  const int usable = p->pts != AV_NOPTS_VALUE && (p->timed || s->o->guess) && sameTimes;
  int result = usable ? dvbridge_fel_take(s->fel, p->pts, &layer) : 2;
  // as Kodi's UseLayerDVMetadata(): the metadata the picture goes out with
  p->carried = RpuHash(g_pendingRpu[0], g_pendingRpuSize[0]);
  p->meta = p->carried;
  p->how = 0;
  if (result == 3 || result == 4)
  {
    const AVFrameSideData* coded = NULL;
    const AVFrameSideData* parsed = NULL;
    if (dvbridge_fel_layer_metadata(layer, g_pendingRpuSize[0] ? g_pendingRpu[0] : NULL, g_pendingRpuSize[0],
                                    &coded, &parsed))
      p->meta = RpuHash(coded->data, coded->size);
    av_frame_free(&layer);
    p->how = 2;
    result = 2; // (3, a MEL: out without the layer too; none in this stream)
  }
  if (result == 0 && baseEnded)
    result = 2;
  else if (result == 0 && s->numPending >= LATE_PENDING)
  {
    const unsigned limit = !s->o->guess && timed ? LATE_RESYNC_TIMED : LATE_RESYNC;
    ++s->out->late;
    if (++s->lateInRow >= limit)
      Resync(s, "enhancement pictures are late or missing");
    result = 2;
  }
  if (result < 0 && dvbridge_fel_exhausted(s->fel))
    result = 2;
  else if (result < 0)
  {
    Resync(s, dvbridge_fel_failed(s->fel) ? dvbridge_fel_reason(s->fel) : "an enhancement picture is missing");
    result = 2;
  }
  if (!result)
    return 0;
#if HAVE_OWN_RPU
  // as Kodi's emit: one that goes out without its layer for a reason of the decoder's own
  if (result == 2 && !p->how && usable)
  {
    AVFrame* own = NULL;
    const AVFrameSideData* coded = NULL;
    const AVFrameSideData* parsed = NULL;
    if (dvbridge_fel_layer_rpu(s->fel, p->pts, &own) == 1)
    {
      if (dvbridge_fel_layer_metadata(own, g_pendingRpuSize[0] ? g_pendingRpu[0] : NULL, g_pendingRpuSize[0],
                                      &coded, &parsed))
        p->meta = RpuHash(coded->data, coded->size);
      p->how = 3;
    }
    av_frame_free(&own);
  }
#endif
  if (result == 1)
  {
    s->lateInRow = 0;
    p->paired = 1;
    p->hash = FrameHash(layer);
    p->layer = (int)(intptr_t)layer->opaque;
    {
      const AVFrameSideData* coded = NULL;
      const AVFrameSideData* parsed = NULL;
      if (dvbridge_fel_layer_metadata(layer, g_pendingRpuSize[0] ? g_pendingRpu[0] : NULL, g_pendingRpuSize[0],
                                      &coded, &parsed))
        p->meta = RpuHash(coded->data, coded->size);
      p->how = 1;
    }
    const int known = p->own > 0 && p->own < PACKETS_MAX && p->layer > 0 && p->layer < PACKETS_MAX;
    if (known && g_layer[p->layer] == AV_NOPTS_VALUE)
      ++s->out->unverified; // by picture order: an enhancement unit whose time the stream does not say
    else if (!known || g_layer[p->layer] != g_own[p->own])
    {
      if (!s->out->wrong++)
      {
        s->out->wrongOwn = p->own;
        s->out->wrongLayer = p->layer;
      }
    }
    av_frame_free(&layer);
  }
  // against the RPU of the access unit that the stream presents at its time
  {
    const int known = p->own > 0 && p->own < PACKETS_MAX && g_own[p->own] != AV_NOPTS_VALUE;
    const int t = known ? TruthIndex(g_own[p->own]) : -1;
    struct outcome* r = s->out;
    if (t < 0)
      r->withoutUnknown += !p->paired;
    else if (p->paired)
      ++*(p->meta == g_truthRpu[t] ? &r->pairedOwn : &r->pairedOther);
    else if (p->meta == g_truthRpu[t])
      ++r->withoutOwn;
    else if (!g_truthGiven[t])
      ++r->withoutUnseen;
    else if (g_truthPacket[t] && g_truthPacket[t] < PACKETS_MAX && !g_outputPacket[g_truthPacket[t]])
      ++r->withoutUnread;
    else
    {
      ++*(r->paired ? &r->withoutOther : &r->withoutOtherBefore);
      if (getenv("FEL_TRACE"))
        printf("  trace: picture %u (packet %d, at %lld) went out without its enhancement picture and with another "
               "RPU than its own (how %d, %u paired before it)\n", r->count, p->own, (long long)p->pts, p->how,
               r->paired);
    }
    if (t >= 0 && !p->paired && p->carried != g_truthRpu[t])
      ++r->carriedOther;
    // (an RPU that the module gave, and that is not the picture's own: in any run, never)
    if (t >= 0 && !p->paired && (p->how == 2 || p->how == 3) && p->meta != g_truthRpu[t])
    {
      ++*(s->o->guess || s->o->noCheck || s->o->noGuard ? &g_wrongGivenAsItWas : &g_wrongGiven);
      if (getenv("FEL_TRACE_GIVEN"))
        printf("  given: picture %u (packet %d, at %lld, how %d, carried %s) in a run timed %d seek %.3f limit %u "
               "gap %u every %u sparse %u bare %d base1 %d noPts %u unknown %u+%u later %.3f ticks %lld resync %u "
               "untimed %u skip %u replay %u offset %u/%.3f guess %d noCheck %d noGuard %d\n",
               r->count, p->own, (long long)p->pts, p->how, p->carried == g_truthRpu[t] ? "its own" : "another",
               s->o->timed, s->o->seek, s->o->limit, s->o->gap, s->o->every, s->o->sparseUntil, s->o->bare,
               s->o->base1, s->o->noPts, s->o->unknownFrom, s->o->unknownCount, s->o->startLater,
               (long long)s->o->startTicks, s->o->resyncAfter, s->o->untimedUntil, s->o->skipUntil,
               s->o->replayAfter, s->o->offsetFrom, s->o->offsetSeconds, s->o->guess, s->o->noCheck,
               s->o->noGuard);
    }
    r->fromTake += p->how == 2;
    r->fromCall += p->how == 3;
  }
  if (s->out->count < PICTURES_MAX)
    s->out->pictures[s->out->count++] = *p;
  s->out->paired += p->paired;
  memmove(s->pending, s->pending + 1, --s->numPending * sizeof(*s->pending));
  memmove(g_pendingRpu, g_pendingRpu + 1, s->numPending * sizeof(*g_pendingRpu));
  memmove(g_pendingRpuSize, g_pendingRpuSize + 1, s->numPending * sizeof(*g_pendingRpuSize));
  return 1;
}

static int Run(const char* path, const struct options* o, struct outcome* out)
{
  memset(out, 0, sizeof(*out));
  out->firstKey = AV_NOPTS_VALUE;
  memset(g_truthGiven, 0, sizeof(g_truthGiven));
  memset(g_truthPacket, 0, sizeof(g_truthPacket));
  memset(g_outputPacket, 0, sizeof(g_outputPacket));
  struct merge_source* source = merge_open(path);
  if (!source)
    return 0;
  if (g_swap)
    merge_swap(source);
  AVFormatContext* fc = merge_context(source);
  int index = merge_base(source);
  const AVStream* stream = fc->streams[index];
  g_start =(fc->start_time == AV_NOPTS_VALUE ? 0.0 : (double)fc->start_time / AV_TIME_BASE) + o->startLater;
  if (o->startTicks)
    g_start = (double)o->startTicks * stream->time_base.num / stream->time_base.den;
  out->tick = av_q2d(stream->time_base);
  g_rate = av_guess_frame_rate(fc, (AVStream*)stream, NULL);

  // What Kodi's decoder has: the parameters of the base track, with the record of the joined stream.
  const AVRational micro = {1, 1000000};
  AVCodecParameters* par = avcodec_parameters_alloc();
  AVDOVIDecoderConfigurationRecord record;
  merge_record(source, &record);
  if (!par || avcodec_parameters_copy(par, stream->codecpar) < 0)
    return 0;
  AVPacketSideData* side = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                   AV_PKT_DATA_DOVI_CONF, sizeof(record), 0);
  if (!side)
    return 0;
  memcpy(side->data, &record, sizeof(record));
  g_lengthSize = merge_length_size(source);
  // the hvcC record of an MP4 or Matroska enhancement track, as Kodi hands it to the module
  // (CDVDDemuxFFmpeg::AddStream(), CDVDVideoCodecFFmpeg::Open())
  const uint8_t* config = NULL;
  int configSize = 0;
  if (!o->bare && !g_noConfig && merge_enhancement_config(source, &config, &configSize))
  {
    side = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data, AV_PKT_DATA_HEVC_CONF,
                                   (size_t)configSize, 0);
    if (!side)
      return 0;
    memcpy(side->data, config, (size_t)configSize);
  }

  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  if (!codec || !base || avcodec_parameters_to_context(base, par) < 0)
    return 0;
  base->pkt_timebase = micro;
  base->thread_count = o->base1 ? 1 : 8;
  base->thread_type = FF_THREAD_FRAME;
  base->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 0;

  struct state s = {0};
  s.out = out;
  s.o = o;
  s.epoch = 1;
  AVCodecParameters* bare = NULL;
  if (o->bare)
  {
    bare = avcodec_parameters_alloc();
    if (!bare || avcodec_parameters_copy(bare, par) < 0)
      return 0;
    // Units framed with lengths (MP4, Matroska) cannot be read without the hvcC record that says how long a
    // length is: there the record stays, without its parameter sets (none of its arrays).
    const int hvcc = bare->extradata && bare->extradata_size >= 23 && bare->extradata[0] == 1;
    if (hvcc)
    {
      bare->extradata[22] = 0;
      bare->extradata_size = 23;
    }
    else
    {
      av_freep(&bare->extradata);
      bare->extradata_size = 0;
    }
  }
  s.fel = dvbridge_fel_create(bare ? bare : par, micro);
  avcodec_parameters_free(&bare);
  if (!s.fel)
    return 0;
  // what dvbridge_fel_device() does with a VAAPI device, in software; the thread count stays the module's
  s.fel->decoder->get_format = avcodec_default_get_format;
  s.fel->decoder->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
  if (avcodec_open2(s.fel->decoder, codec, NULL) < 0)
    return 0;
  s.fel->opened = true;

  if (o->seek >= 0 && merge_seek(source, o->seek) < 0)
    return 0;

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  unsigned n = 0;
  int sparse = o->sparseUntil > 0;
  int end = 0, baseEnded = 0, replayed = 0;
  while (!baseEnded)
  {
    if (!end)
    {
      // Only so much of the stream is played: it stops here as a video that is stopped does, and the
      // pictures that still wait for their enhancement pictures are not shown.
      if ((o->limit && n == o->limit) || n == PACKETS_MAX - 1)
        break;
      const int got = merge_read(source, packet);
      if (got >= 0 && packet->stream_index != index)
      {
        av_packet_unref(packet);
        continue;
      }
      end = got < 0;
    }
    if (!end)
    {
      ++n;
      ++out->packets;
      int has, key, baseKey;
      int64_t difference = 0;
      AVRational timeBase = stream->time_base;
      AVRational trackBase = stream->time_base;
      LayerUnits(packet, &has, &key, &baseKey);
      int timed = LayerDifference(packet, &difference, &timeBase);
      // what the stream says, before anything is done to the packet
      const int64_t apart = timed ? av_rescale_q(difference, timeBase, stream->time_base) : 0;
      g_own[n] = packet->pts;
      g_layer[n] = has && timed && packet->pts != AV_NOPTS_VALUE ? packet->pts + apart : AV_NOPTS_VALUE;
      if (has && key && out->numKeys < 8)
        out->keys[out->numKeys++] = n;
      if (baseKey && out->numBaseKeys < 32)
        out->baseKeys[out->numBaseKeys++] = n;

      if (n <= o->skipUntil)
      {
        av_packet_unref(packet);
        continue;
      }
      if (!o->timed || n == o->gap || (o->every && n % o->every == 0) || (sparse && n > 1 && n % 2 == 0) ||
          n <= o->untimedUntil)
      {
        av_packet_side_data_remove(packet->side_data, &packet->side_data_elems, AV_PKT_DATA_STRINGS_METADATA);
        timed = 0;
      }
      // CDVDDemuxFFmpeg::ReadInternal()
      if (timed && !o->noCheck && !merge_check_layer_time(source, packet, ConvertForMerge, &trackBase))
      {
        ++out->unusable;
        timed = 0;
      }
      if (has)
        ++*(timed ? &out->withTime : &out->withoutTime);
      if (has && timed && packet->pts != AV_NOPTS_VALUE)
      {
        // the base picture that is presented then has this time in Kodi
        const int64_t time = KodiTime(packet->pts + apart, stream->time_base, 0);
        if (out->firstKey == AV_NOPTS_VALUE && key)
          out->firstKey = time;
        if (out->firstKey != AV_NOPTS_VALUE && out->numLayerTimes < PICTURES_MAX)
        {
          out->layerTimes[out->numLayerTimes++] = time;
          out->differentTimes += apart != 0;
        }
      }
      // CVideoPlayer: the times of a jump it is not sure of yet are unknown; a jump it joins to its clock
      // is taken off every time from then on
      const double offset = o->offsetFrom && n >= o->offsetFrom ? o->offsetSeconds : 0.0;
      const int64_t back = o->offsetFrom && n >= o->offsetFrom ? o->offsetTicks : 0;
      const int unknown = o->unknownCount && n >= o->unknownFrom && n < o->unknownFrom + o->unknownCount;
      packet->pts = unknown || n == o->noPts || packet->pts == AV_NOPTS_VALUE
                        ? AV_NOPTS_VALUE
                        : KodiTime(packet->pts - back, stream->time_base, offset);
      packet->dts = unknown || packet->dts == AV_NOPTS_VALUE
                        ? AV_NOPTS_VALUE
                        : KodiTime(packet->dts - back, stream->time_base, offset);
      packet->opaque = (void*)(intptr_t)n;
      // CDVDVideoCodecFFmpeg::AddData()
      if (offset != s.offset)
      {
        ++s.epoch;
        if (!o->noGuard && dvbridge_fel_timed(s.fel))
          Resync(&s, "the player moved the times of the stream");
        s.offset = offset;
      }
#if HAVE_TIME_ZERO
      // ... and says where the 0 of the demuxer has gone with what the player took off the times (not in
      // the runs in which it is the stream itself whose times jump: the player took nothing off)
      dvbridge_fel_time_zero(s.fel, (int64_t)((0.0 - (o->noGuard ? 0.0 : offset) * 1000000) / 1000000 * 1000000));
#endif
      g_epoch[n] = s.epoch;
      // the module is given this access unit, with its time when the run is by times
      if (has && g_layer[n] != AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE && (!o->timed || timed))
      {
        const int t = TruthIndex(g_layer[n]);
        if (t >= 0)
        {
          g_truthGiven[t] = 1;
          g_truthPacket[t] = n;
        }
      }
      if (avcodec_send_packet(base, packet) < 0)
        printf("note: the base decoder refused packet %u\n", n);
      if (!dvbridge_fel_submit(s.fel, packet))
        s.failed = 1;
      av_packet_unref(packet);
      if (n == o->resyncAfter)
        Resync(&s, "a failure of the layer, made here");
    }
    else
    {
      avcodec_send_packet(base, NULL);
      if (!dvbridge_fel_drain(s.fel))
        s.failed = 1;
    }
    // CDVDVideoCodecFFmpeg::GetPairedPicture(), called until it has no picture
    for (;;)
    {
      if (s.failed || dvbridge_fel_failed(s.fel))
        Resync(&s, dvbridge_fel_reason(s.fel));
      if (Emit(&s, 0))
        continue;
      const int got = avcodec_receive_frame(base, frame);
      if (got < 0)
      {
        if (end && got == AVERROR_EOF)
        {
          // at the end of the base layer every waiting picture goes out
          while (Emit(&s, 1))
            ;
          baseEnded = 1;
        }
        break;
      }
      if (s.numPending == 64)
      {
        printf("FAIL: 64 base pictures wait\n");
        ++g_failures;
        baseEnded = 1;
        break;
      }
      // CDVDVideoCodecFFmpeg::GetPictureCommon(): the time of the picture is that of its packet; without
      // one, what the decoder guesses
      struct picture* p = &s.pending[s.numPending++];
      memset(p, 0, sizeof(*p));
      p->timed = frame->pts != AV_NOPTS_VALUE;
      p->pts = p->timed ? frame->pts : frame->best_effort_timestamp;
      p->own = (int)(intptr_t)frame->opaque;
      if (p->own > 0 && p->own < PACKETS_MAX)
        g_outputPacket[p->own] = 1;
      if (s.numPending > out->maxPending)
        out->maxPending = s.numPending;
      // as Kodi's GetPictureCommon() and GetPairedPicture(): the RPU carried with the base picture, and
      // what the module is told of it
      {
        const AVFrameSideData* parsed = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        const AVFrameSideData* coded = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
        const unsigned k = s.numPending - 1;
        g_pendingRpuSize[k] = 0;
        if (parsed && coded && coded->size <= RPU_MAX)
        {
          memcpy(g_pendingRpu[k], coded->data, coded->size);
          g_pendingRpuSize[k] = coded->size;
        }
        else if (parsed && coded)
          ++g_rpuTooLong;
#if HAVE_OWN_RPU
        if (p->timed)
          dvbridge_fel_base_picture(s.fel, p->pts);
#endif
#if HAVE_RING
        if (parsed && coded)
          dvbridge_fel_base_metadata(s.fel, coded->data, coded->size, parsed->data, parsed->size);
#endif
      }
      av_frame_unref(frame);
      if (!pump(s.fel)) // dvbridge_fel_device() with the device it already has
        s.failed = 1;
    }

    const int replay = o->replayAfter && !replayed && n == o->replayAfter;
    if (replay)
    {
      replayed = 1;
      for (unsigned i = 0; i < out->count; ++i)
        out->replayZeroPaired |= out->pictures[i].pts == 0 && out->pictures[i].paired;
    }
    if ((sparse && n == o->sparseUntil) || replay)
    {
      // The first part ends here. The stream is played again from its start (the file is opened again, the
      // decoders are the same: CDVDVideoCodecFFmpeg::Reset()), with all its times.
      out->sparseUntimed = dvbridge_fel_untimed(s.fel);
      out->sparseTimedAfter = dvbridge_fel_timed(s.fel);
      snprintf(out->sparseReason, sizeof(out->sparseReason), "%s", out->fallback);
      sparse = 0;
      n = 0;
      s.numPending = 0;
      out->count = out->paired = out->packets = out->late = out->resyncs = out->maxPending = 0;
      out->withTime = out->withoutTime = out->numLayerTimes = out->differentTimes = 0;
      out->wrong = out->unverified = out->numKeys = 0;
      out->firstKey = AV_NOPTS_VALUE;
      out->reason[0] = 0;
      out->pairedOwn = out->pairedOther = out->withoutOwn = out->withoutOther = out->withoutOtherBefore = 0;
      out->withoutUnseen = out->withoutUnknown = out->fromTake = out->fromCall = out->carriedOther = 0;
      memset(g_truthGiven, 0, sizeof(g_truthGiven));
      memset(g_truthPacket, 0, sizeof(g_truthPacket));
      memset(g_outputPacket, 0, sizeof(g_outputPacket));
      out->withoutUnread = 0;
      merge_close(source);
      source = merge_open(path);
      if (!source)
        return 0;
      if (g_swap)
        merge_swap(source);
      fc = merge_context(source);
      index = merge_base(source);
      stream = fc->streams[index];
      avcodec_flush_buffers(base);
      dvbridge_fel_reset(s.fel);
#if HAVE_BASE_FLUSHED
      dvbridge_fel_base_flushed(s.fel);
#endif
      s.failed = 0;
      s.lateInRow = 0;
    }
  }
  out->timed = dvbridge_fel_timed(s.fel);
  out->untimed = dvbridge_fel_untimed(s.fel);
  // what the module did about MEL (an enhancement layer whose RPUs say that it carries no residual)
  struct dvbridge_fel_layer_skip skip = {0};
  dvbridge_fel_layer_skip_counts(s.fel, &skip);
  out->layerDecoded = skip.decoded;
  out->layerNotDecoded = skip.not_decoded;
  out->layerStops = skip.stops;
  snprintf(out->layerKept, sizeof(out->layerKept), "%s", skip.kept ? skip.kept : "-");
  out->mismatches = dvbridge_fel_order_mismatches(s.fel, NULL);
#if HAVE_BASE_TIMES
  dvbridge_fel_passed(s.fel, &out->ambiguous);
#endif
#if HAVE_OWN_RPU
  out->own = dvbridge_fel_own_rpus(s.fel);
  out->ranked = dvbridge_fel_ranked(s.fel);
#endif
  dvbridge_fel_destroy(s.fel);
  avcodec_free_context(&base);
  avcodec_parameters_free(&par);
  av_packet_free(&packet);
  av_frame_free(&frame);
  merge_close(source);
  return 1;
}

static const struct picture* Find(const struct outcome* in, int64_t pts)
{
  for (unsigned i = 0; i < in->count; ++i)
    if (in->pictures[i].pts == pts)
      return &in->pictures[i];
  return NULL;
}

// Pictures paired in both runs: how many, and how many with another enhancement picture. Pictures paired
// in one of them only: how many, and the first and last of them by their place in that run.
struct comparison
{
  unsigned both, different, onlyA, onlyB, firstOnly, lastOnly;
};

// upto: only the first so many pictures of a.
static struct comparison Compare(const struct outcome* a, const struct outcome* b, unsigned upto)
{
  struct comparison c = {0, 0, 0, 0, UINT_MAX, 0};
  for (unsigned i = 0; i < a->count && i < upto; ++i)
  {
    const struct picture* p = &a->pictures[i];
    const struct picture* q = p->pts == AV_NOPTS_VALUE ? NULL : Find(b, p->pts);
    if (!q)
      continue;
    if (p->paired && q->paired)
    {
      ++c.both;
      c.different += p->hash != q->hash;
    }
    else if (p->paired != q->paired)
    {
      ++*(p->paired ? &c.onlyA : &c.onlyB);
      if (i < c.firstOnly)
        c.firstOnly = i;
      if (i > c.lastOnly)
        c.lastOnly = i;
    }
  }
  return c;
}

// Of the first pictures of a run from the start of the stream (all but the last 48, whose enhancement units
// may come after the part that was read): how many go out without the enhancement picture that they have
// in the whole run. Pictures are known by their packet, whatever was done to their times.
static unsigned Lost(const struct outcome* run, const struct outcome* whole)
{
  static unsigned char paired[PACKETS_MAX];
  memset(paired, 0, sizeof(paired));
  for (unsigned i = 0; i < whole->count; ++i)
    if (whole->pictures[i].paired && whole->pictures[i].own > 0 && whole->pictures[i].own < PACKETS_MAX)
      paired[whole->pictures[i].own] = 1;
  unsigned lost = 0;
  for (unsigned i = 0; i + 48 < run->count; ++i)
  {
    const struct picture* p = &run->pictures[i];
    lost += !p->paired && p->own > 0 && p->own < PACKETS_MAX && paired[p->own];
  }
  return lost;
}

// By what the stream says: the pictures that have to be paired are those presented at the time of an
// enhancement unit that was given from the first random-access unit of the layer on, and not before it.
static void Expected(const struct outcome* r, unsigned* expected, unsigned* missing, unsigned* unexpected)
{
  *expected = *missing = *unexpected = 0;
  for (unsigned i = 0; i < r->count; ++i)
  {
    const struct picture* p = &r->pictures[i];
    int wanted = 0;
    if (r->firstKey != AV_NOPTS_VALUE && p->pts != AV_NOPTS_VALUE && p->pts >= r->firstKey)
      for (unsigned k = 0; k < r->numLayerTimes && !wanted; ++k)
        wanted = r->layerTimes[k] == p->pts;
    *expected += wanted;
    *missing += wanted && !p->paired;
    *unexpected += !wanted && p->paired;
  }
}

static unsigned FirstPaired(const struct outcome* r)
{
  for (unsigned i = 0; i < r->count; ++i)
    if (r->pictures[i].paired)
      return i;
  return r->count;
}

static void Show(const char* name, const struct outcome* r)
{
  printf("%s: %u packets (%u with the time of their enhancement unit, %u without), %u pictures, %u paired (the "
         "first one is picture %u), %u with the enhancement picture of another time (%u with one whose time the "
         "stream does not say), %u late, %u resynchronizations%s%s, most base pictures waiting %u; pairing by %s; "
         "%u enhancement units of another time or order than their packet\n",
         name, r->packets, r->withTime, r->withoutTime, r->count, r->paired, FirstPaired(r), r->wrong,
         r->unverified, r->late, r->resyncs, r->resyncs ? ", the first: " : "", r->reason, r->maxPending,
         r->timed ? "times" : "picture order", r->mismatches);
  if (r->wrong)
    printf("  the first of them: the picture of packet %d (presented at %lld) got the enhancement picture of "
           "packet %d (presented at %lld)\n", r->wrongOwn,
           (long long)(r->wrongOwn > 0 && r->wrongOwn < PACKETS_MAX ? g_own[r->wrongOwn] : -1), r->wrongLayer,
           (long long)(r->wrongLayer > 0 && r->wrongLayer < PACKETS_MAX ? g_layer[r->wrongLayer] : -1));
  printf("  RPUs: paired %u with their own, %u with another one; without their enhancement picture %u with their "
         "own (%u by an answer 4, %u by dvbridge_fel_layer_rpu()), %u with another one though the module was given "
         "their access unit (and %u before the first paired picture), %u whose access unit it was not given, %u "
         "with no access unit at their time; %u of them carried another one than their own; %u starts of the layer "
         "at a place it counted\n",
         r->pairedOwn, r->pairedOther, r->withoutOwn, r->fromTake, r->fromCall, r->withoutOther,
         r->withoutOtherBefore, r->withoutUnseen, r->withoutUnknown, r->carriedOther, r->ranked);
  if (r->withoutUnread)
    printf("  ... and %u with another one whose RPU came with a base picture that the base decoder never gave (the first "
           "packets after a seek): nothing reads it\n", r->withoutUnread);
}

// the pictures of a run, one a line, to compare two modules
static void Write(FILE* out, const char* name, const struct outcome* r)
{
  for (unsigned i = 0; out && i < r->count; ++i)
  {
    const struct picture* p = &r->pictures[i];
    fprintf(out, "%s %lld %d %d %d %016llx %016llx %d\n", name, (long long)p->pts, p->own, p->paired, p->layer,
            (unsigned long long)p->hash, (unsigned long long)p->meta, p->how);
  }
}

static struct outcome g_order, g_times, g_part[2], g_other;
// the sums of the five seeks
static struct
{
  unsigned seeks, orderPaired, timesPaired, orderOnly, timesOnly, differ, orderOther, orderBefore, orderOwn,
      orderCarried, timesOther, timesOwn, timesCarried, pairedOther, wrong, ranked, timesUnread;
} g_seekSums;

// For the comparison of one stream in two containers (FEL_DUMP=<file>): each picture of a run by its place in
// the stream (its time over the frame rate: the times of a container may be rounded, the places are not),
// whether it was paired, the samples of its enhancement picture, and the place of the base picture that the
// stream presents with the enhancement access unit that the picture got (the same place when it is right).
static void Dump(const char* path, const struct outcome* r)
{
  FILE* f = path ? fopen(path, "w") : NULL;
  if (!f)
    return;
  const double rate = g_rate.num > 0 && g_rate.den > 0 ? av_q2d(g_rate) : 0;
  for (unsigned i = 0; i < r->count; ++i)
  {
    const struct picture* p = &r->pictures[i];
    if (p->pts == AV_NOPTS_VALUE)
      continue;
    long long layer = -1;
    if (p->paired && p->layer > 0 && p->layer < PACKETS_MAX && g_layer[p->layer] != AV_NOPTS_VALUE)
      layer = llround(((double)g_layer[p->layer] * r->tick - g_start) * rate);
    fprintf(f, "%lld\t%d\t%016llx\t%lld\n", llround((double)p->pts / 1e6 * rate), p->paired,
            p->paired ? (unsigned long long)p->hash : 0ULL, layer);
  }
  fclose(f);
}

static int g_otherStream;

int main(int argc, char** argv)
{
  g_otherStream = getenv("FEL_OTHER_STREAM") && *getenv("FEL_OTHER_STREAM");
  if (argc < 2)
    return 2;
  int base1 = 0, quick = 0, moved = 0, starts = 0, seeks = 0;
  for (int i = 2; i < argc; ++i)
  {
    if (!strcmp(argv[i], "seeks"))
      seeks = 1; // with quick: the five seeks all the same
    else if (!strcmp(argv[i], "starts"))
      starts = 1; // with quick: the files that start later all the same
    else if (!strcmp(argv[i], "base1"))
      base1 = 1;
    else if (!strcmp(argv[i], "quick"))
      quick = 1;
    else if (!strcmp(argv[i], "moved"))
      moved = 1;
    else if (!strcmp(argv[i], "swap"))
      g_swap = 1;
    else if (!strcmp(argv[i], "noconfig"))
      g_noConfig = 1;
    else
      return 2;
  }
  if (g_swap)
    printf("the two tracks have changed places: the base layer is the one that is coded in another order\n");
  av_log_set_level(AV_LOG_FATAL);
  char text[500];
  printf("module: %s%s%s\n", HAVE_RING ? "keeps the metadata of the base pictures" : "without the metadata of the base pictures",
         HAVE_OWN_RPU ? "; the RPU of each picture's own access unit, and the layer started at a counted place"
                     : "", "");
  if (!BuildTruth(argv[1]))
  {
    Check(0, "the time and the RPU of every enhancement access unit of the stream are read");
    return 1;
  }
  printf("the stream presents %u enhancement access units at times it says\n", g_numTruth);
  FILE* pictures = getenv("FEL_PICTURES") ? fopen(getenv("FEL_PICTURES"), "w") : NULL;

  // ---- the whole stream by picture order
  struct options o = {.timed = 0, .seek = -1, .base1 = base1};
  if (!Run(argv[1], &o, &g_order))
  {
    Check(0, "the stream opens, its tracks are joined, and the module takes it");
    return 1;
  }
  Show("by picture order", &g_order);
  Write(pictures, "order", &g_order);
  Check(!g_order.timed && g_order.paired > 2000, "without the times the pictures are paired by picture order");
  if (HAVE_TIMES)
    Check(!g_order.wrong, "... each with the enhancement picture that the stream presents at its time");
  snprintf(text, sizeof(text), "each of the %u pictures paired by picture order has the RPU of its own access "
           "unit (%u another one)", g_order.pairedOwn, g_order.pairedOther);
  CheckRpu(g_order.pairedOwn > 2000 && !g_order.pairedOther && !g_rpuTooLong, text);

  unsigned frameTicks = 0; // the time between two pictures, in the time base of the track
  double frame = 0;        // ... and in seconds
  // The random-access units of the enhancement layer after the first one (three of them at most): the packet
  // of each, when the stream presents it, in the time base of the track, and the base packets that have the
  // time 0 in a file that starts then: the ones presented in the half second up to it. And a packet with a
  // random-access picture of the base layer from which a file that starts then can be played with the layer
  // having nothing to start at before that unit, and two base packets of the time 0 before it (0: none).
  struct
  {
    unsigned packet, firstZero, zeroPackets, baseFrom;
    int64_t start;
  } units[3];
  unsigned numUnits = 0;
  int64_t firstTime = AV_NOPTS_VALUE; // when the stream presents its first picture
  if (HAVE_TIMES)
  {
    // ---- the whole stream by its times
    Check(!strcmp(merge_layer_time_key(), DVBRIDGE_FEL_LAYER_TIME), "the join and the module name the time alike");
    o.timed = 1;
    Run(argv[1], &o, &g_times);
    Show("by the times", &g_times);
    Dump(getenv("FEL_DUMP"), &g_times);
    printf("MEL: %u enhancement access units decoded, %u not decoded (the layer stopped %u times); decoded for good "
           "because: %s\n", g_times.layerDecoded, g_times.layerNotDecoded, g_times.layerStops, g_times.layerKept);
    Write(pictures, "times", &g_times);
    snprintf(text, sizeof(text), "each of the %u pictures paired by the times has the RPU of its own access "
             "unit (%u another one)", g_times.pairedOwn, g_times.pairedOther);
    CheckRpu(g_times.pairedOwn > 2000 && !g_times.pairedOther, text);
    unsigned expected, missing, unexpected;
    Expected(&g_times, &expected, &missing, &unexpected);
    printf("by what the stream says: %u pictures have their enhancement unit (%u of them presented at another time "
           "than the base picture of its packet); not paired %u, paired without one %u\n",
           expected, g_times.differentTimes, missing, unexpected);
    Check(g_times.timed, "with the times the module pairs by them");
    // (a fact of the FEL test stream; another stream, FEL_OTHER_STREAM=1, only says it)
    if (getenv("FEL_OTHER_STREAM") && *getenv("FEL_OTHER_STREAM"))
      printf("not checked (another stream than the FEL test stream): %u of %u enhancement units presented at "
             "another time than the base picture of their packet\n", g_times.differentTimes,
             g_times.numLayerTimes);
    else
      Check(g_times.differentTimes > g_times.numLayerTimes / 2,
            "most enhancement units of this stream are presented at another time than the base picture of their "
            "packet");
    Check(expected > 2000 && !missing && !unexpected,
          "exactly the pictures whose enhancement unit the stream gave are paired");
    Check(!g_times.wrong, "each with the enhancement picture that the stream presents at its time");
    Check(!g_times.resyncs && (base1 || !g_times.late), "nothing was late and the layer never started again");
    Check(g_times.untimed == g_times.withoutTime && !g_times.unusable,
          "the enhancement units without a time are counted, and were not decoded");
    const struct comparison c = Compare(&g_times, &g_order, UINT_MAX);
    unsigned unique = 0;
    for (unsigned i = 0; i < g_times.count; ++i)
    {
      int again = 0;
      for (unsigned k = 0; k < g_times.count && !again; ++k)
        again = k != i && g_times.pictures[k].paired && g_times.pictures[k].hash == g_times.pictures[i].hash;
      unique += g_times.pictures[i].paired && !again;
    }
    snprintf(text, sizeof(text),
             "the two ways give every picture the same enhancement picture (%u paired by both, %u differ in their "
             "samples; %u of the %u enhancement pictures look like no other one)", c.both, c.different, unique,
             g_times.paired);
    Check(c.both > 2000 && !c.different, text);
    printf("paired by the times only: %u, by picture order only: %u (pictures %u to %u of %u)\n", c.onlyA, c.onlyB,
           c.onlyA + c.onlyB ? c.firstOnly : 0, c.lastOnly, g_times.count);
    Check(!(c.onlyA + c.onlyB) || c.firstOnly + 64 >= g_times.count,
          "they pair the same pictures but for the last ones of the stream, whose packets have no time");
    // the time between two pictures: the smallest distance between the times of two base packets
    int64_t smallest = INT64_MAX;
    for (unsigned i = 2; i <= g_times.packets && i < PACKETS_MAX; ++i)
      for (unsigned k = i > 8 ? i - 8 : 1; k < i; ++k)
        if (g_own[i] != AV_NOPTS_VALUE && g_own[k] != AV_NOPTS_VALUE && g_own[i] != g_own[k] &&
            llabs(g_own[i] - g_own[k]) < smallest)
          smallest = llabs(g_own[i] - g_own[k]);
    frameTicks = smallest == INT64_MAX ? 0 : (unsigned)smallest;
    frame = frameTicks * g_times.tick;
    printf("pictures are %u ticks (%.4f s) apart; the layer has random-access units in packets %u, %u, %u, %u\n",
           frameTicks, frame, g_times.keys[0], g_times.keys[1], g_times.keys[2], g_times.keys[3]);
    for (unsigned i = 1; i <= 60 && i <= g_times.packets; ++i)
      if (g_own[i] != AV_NOPTS_VALUE && (firstTime == AV_NOPTS_VALUE || g_own[i] < firstTime))
        firstTime = g_own[i];
    for (unsigned k = 1; k < g_times.numKeys && numUnits < 3 && g_times.tick > 0; ++k)
    {
      const unsigned at = g_times.keys[k];
      if (at >= PACKETS_MAX || g_layer[at] == AV_NOPTS_VALUE)
        continue;
      const int64_t start = g_layer[at];
      const int64_t half = (int64_t)(0.5 / g_times.tick);
      unsigned first = 0, zeros = 0, from = 0;
      for (unsigned i = 1; i <= g_times.packets && i < PACKETS_MAX; ++i)
      {
        if (g_own[i] == AV_NOPTS_VALUE || g_own[i] > start || g_own[i] <= start - half)
          continue;
        if (!first)
          first = i;
        ++zeros;
      }
      for (unsigned b = 0; b < g_times.numBaseKeys; ++b)
      {
        const unsigned base = g_times.baseKeys[b];
        unsigned before = 0;
        for (unsigned i = base; i < at && i < PACKETS_MAX; ++i)
          before += g_own[i] != AV_NOPTS_VALUE && g_own[i] <= start && g_own[i] > start - half;
        if (base > g_times.keys[k - 1] && base < at && before >= 2)
          from = base;
      }
      printf("the unit of packet %u is presented at %lld: in a file that starts then, %u base packets have the time "
             "0, from packet %u on; playing from the base picture of packet %u on, the layer has nothing to start "
             "at before it\n", at, (long long)start, zeros, first, from);
      if (zeros < 2)
        continue;
      units[numUnits].packet = at;
      units[numUnits].start = start;
      units[numUnits].firstZero = first;
      units[numUnits].zeroPackets = zeros;
      units[numUnits++].baseFrom = from;
    }
  }

  // ---- after seeks
  static const double parts[] = {0.1, 0.25, 0.5, 0.75, 0.9};
  unsigned gained = 0;
  int seeksOk = 1, sameOk = 1;
  for (unsigned k = 0; (!quick || seeks) && k < sizeof(parts) / sizeof(*parts); ++k)
  {
    for (int timed = 0; timed <= HAVE_TIMES; ++timed)
    {
      struct options seek = {.timed = timed, .seek = parts[k], .limit = 300, .base1 = base1};
      Run(argv[1], &seek, &g_part[timed]);
      snprintf(text, sizeof(text), "after a seek to %.0f %%, %s", parts[k] * 100, timed ? "by the times" : "by picture order");
      Show(text, &g_part[timed]);
    }
    if (!HAVE_TIMES)
      continue;
    unsigned expected, missing, unexpected;
    Expected(&g_part[1], &expected, &missing, &unexpected);
    const struct comparison whole = Compare(&g_part[1], &g_times, UINT_MAX);
    printf("  by what the stream says %u pictures have their enhancement unit: not paired %u, paired without one %u; "
           "against the whole run %u compared, %u differ\n", expected, missing, unexpected, whole.both, whole.different);
    seeksOk &= expected > 100 && !missing && !unexpected && !g_part[1].resyncs && g_part[1].timed;
    sameOk &= whole.both > 100 && !whole.different && !g_part[0].wrong && !g_part[1].wrong;
    const struct comparison order = Compare(&g_part[0], &g_times, UINT_MAX);
    sameOk &= !order.different;
    seeksOk &= g_part[1].paired >= g_part[0].paired;
    gained += g_part[1].paired - g_part[0].paired;
    // the RPUs of the pictures without their enhancement picture, and what picture order pairs against
    // the times
    const struct comparison ways = Compare(&g_part[0], &g_part[1], UINT_MAX);
    printf("  by picture order %u pictures paired, by the times %u; paired by both %u (%u differ), by picture "
           "order only %u, by the times only %u (pictures %u to %u)\n", g_part[0].paired, g_part[1].paired, ways.both,
           ways.different, ways.onlyA, ways.onlyB, ways.onlyA + ways.onlyB ? ways.firstOnly : 0, ways.lastOnly);
    g_seekSums.orderPaired += g_part[0].paired;
    g_seekSums.timesPaired += g_part[1].paired;
    g_seekSums.orderOnly += ways.onlyA;
    g_seekSums.timesOnly += ways.onlyB;
    g_seekSums.differ += ways.different;
    g_seekSums.orderOther += g_part[0].withoutOther;
    g_seekSums.orderBefore += g_part[0].withoutOtherBefore;
    g_seekSums.orderOwn += g_part[0].withoutOwn;
    g_seekSums.orderCarried += g_part[0].carriedOther;
    g_seekSums.timesOther += g_part[1].withoutOther + g_part[1].withoutOtherBefore;
    g_seekSums.timesUnread += g_part[1].withoutUnread;
    g_seekSums.timesOwn += g_part[1].withoutOwn;
    g_seekSums.timesCarried += g_part[1].carriedOther;
    g_seekSums.pairedOther += g_part[0].pairedOther + g_part[1].pairedOther;
    g_seekSums.wrong += g_part[0].wrong + g_part[1].wrong;
    g_seekSums.ranked += g_part[0].ranked;
    g_seekSums.seeks += 1;
  }
  if (HAVE_TIMES && g_seekSums.seeks)
  {
    printf("the five seeks: by picture order %u pictures paired, by the times %u (by picture order only %u, by "
           "the times only %u, %u differ); %u starts at a counted place\n", g_seekSums.orderPaired, g_seekSums.timesPaired,
           g_seekSums.orderOnly, g_seekSums.timesOnly, g_seekSums.differ, g_seekSums.ranked);
    printf("the five seeks, without their enhancement picture: by the times %u with their own RPU and %u with "
           "another one whose access unit was given (%u had carried another one); by picture order %u with their own, "
           "%u with another one after the first paired picture and %u before it (%u had carried another one)\n",
           g_seekSums.timesOwn, g_seekSums.timesOther, g_seekSums.timesCarried, g_seekSums.orderOwn, g_seekSums.orderOther,
           g_seekSums.orderBefore, g_seekSums.orderCarried);
    snprintf(text, sizeof(text), "after the seeks no paired picture has another RPU than its own (%u) and none "
             "has the enhancement picture of another time (%u)", g_seekSums.pairedOther, g_seekSums.wrong);
    Check(!g_seekSums.wrong, text);
    CheckRpu(!g_seekSums.pairedOther, "... and no paired picture has another RPU than its own");
    snprintf(text, sizeof(text), "by the times, every picture that goes out without its enhancement picture "
             "after a seek has the RPU of its own access unit, when the module was given it and the base decoder gave "
             "the picture that brought it (%u have it, %u do not; %u whose RPU came with a base picture that the base "
             "decoder never gave keep the one carried with them)", g_seekSums.timesOwn, g_seekSums.timesOther,
             g_seekSums.timesUnread);
    // (that there are such pictures is a fact of the FEL test stream; another stream only says it)
    CheckRpu((g_otherStream || g_seekSums.timesOwn > 0) && !g_seekSums.timesOther, text);
    snprintf(text, sizeof(text), "by picture order, once the layer has started, every picture that goes out "
             "without its enhancement picture has the RPU of its own access unit (%u have it, %u do not)",
             g_seekSums.orderOwn, g_seekSums.orderOther);
    CheckRpu(!g_seekSums.orderOther, text);
    snprintf(text, sizeof(text), "by picture order, the layer starts after each seek at a random-access picture "
             "of its own, as by the times: picture order pairs nothing the times do not, and nothing otherwise (%u "
             "paired by the times only, %u by picture order only, %u differ)", g_seekSums.timesOnly, g_seekSums.orderOnly,
             g_seekSums.differ);
    Check(!g_seekSums.orderOnly && !g_seekSums.differ && g_seekSums.orderPaired * 10 >= g_seekSums.timesPaired * 9, text);
  }

  // ---- files that start at a random-access picture of the base layer that the layer does not share. A file
  // of one track whose two layers do not have their random-access pictures in the same packets with the same picture
  // order, and whose base layer reorders its pictures (swap), is so from its first packet: the layer never started
  // by picture order (a MEL stream, 44 s from its start).
  if (HAVE_TIMES && (!quick || starts))
  {
    unsigned tried = 0, last = 0, orderPaired = 0, timesPaired = 0, orderOnly = 0, timesOnly = 0, differ = 0;
    unsigned wrong = 0, other = 0, own = 0, never = 0;
    for (unsigned b = 30; b < g_streamPackets && tried < 4; ++b)
    {
      if (!g_streamBaseKey[b] || g_streamLayerKey[b] || (last && b < last + 400) || b + 240 >= g_streamPackets)
        continue;
      last = b;
      ++tried;
      for (int timed = 0; timed <= 1; ++timed)
      {
        struct options start = {.timed = timed, .seek = -1, .limit = b + 240, .skipUntil = b - 1, .base1 = base1};
        Run(argv[1], &start, &g_part[timed]);
        snprintf(text, sizeof(text), "a file that starts with the base packet %u, %s", b,
                 timed ? "by the times" : "by picture order");
        Show(text, &g_part[timed]);
      }
      const struct comparison c = Compare(&g_part[0], &g_part[1], UINT_MAX);
      printf("  the first paired picture: %u by picture order, %u by the times; paired by both %u (%u differ), by "
             "picture order only %u, by the times only %u\n", FirstPaired(&g_part[0]), FirstPaired(&g_part[1]),
             c.both, c.different, c.onlyA, c.onlyB);
      orderPaired += g_part[0].paired;
      timesPaired += g_part[1].paired;
      orderOnly += c.onlyA;
      timesOnly += c.onlyB;
      differ += c.different;
      wrong += g_part[0].wrong + g_part[1].wrong + (g_swap ? 0 : g_part[0].pairedOther + g_part[1].pairedOther);
      other += g_part[0].withoutOther + g_part[1].withoutOther + g_part[1].withoutOtherBefore;
      own += g_part[0].withoutOwn + g_part[1].withoutOwn;
      never += !g_part[0].paired;
    }
    snprintf(text, sizeof(text), "%u files that start at a random-access picture of the base layer that the "
             "layer does not share: by picture order the layer starts in each (%u never did), at a picture of its "
             "own, as by the times (by picture order %u pictures paired, by the times %u; by picture order only %u, "
             "by the times only %u, %u differ, %u with the enhancement picture or RPU of another one)", tried, never,
             orderPaired, timesPaired, orderOnly, timesOnly, differ, wrong);
    // (that there are such files is a fact of the FEL test stream; another stream only says it)
    Check((g_otherStream || tried > 0) && !never && !orderOnly && !differ && !wrong &&
              orderPaired * 10 >= timesPaired * 9, text);
    snprintf(text, sizeof(text), "... and the pictures without their enhancement picture, once the layer has "
             "started (by the times: from the start), have the RPU of their own access unit (%u have it, %u do not)",
             own, other);
    CheckRpu(!other, text);
  }
  if (HAVE_TIMES && !quick)
  {
    Check(seeksOk, "after each seek the layer starts at its first random-access picture: exactly the pictures from "
                   "it on are paired, without a resynchronization");
    Check(sameOk, "and each gets the enhancement picture of its time, the one it has in the whole run");
    printf("pictures paired after the five seeks by the times and not by picture order: %u\n", gained);
  }
  if (HAVE_TIMES)
  {
    // ---- after a seek, with no parameter sets of the layer but the ones in the stream
    struct options bare = {.timed = 1, .seek = 0.5, .limit = 300, .bare = 1, .base1 = base1};
    Run(argv[1], &bare, &g_other);
    Show("after a seek to 50 %, the module without parameter sets from outside the stream", &g_other);
    unsigned expected, missing, unexpected;
    Expected(&g_other, &expected, &missing, &unexpected);
    const struct comparison c = Compare(&g_other, &g_times, UINT_MAX);
    Check(!g_other.resyncs && !g_other.wrong && expected > 100 && !missing && !unexpected && c.both > 100 &&
              !c.different,
          "access units of the layer that cannot be read yet do not start it and do not fail it: the layer "
          "starts at its first random-access picture, which brings its parameter sets");
  }

  if (HAVE_TIMES)
  {
    // ---- one access unit without its time
    struct options gap = {.timed = 1, .seek = -1, .limit = 600, .gap = 200, .base1 = base1};
    Run(argv[1], &gap, &g_other);
    Show("packet 200 without the time of its enhancement unit", &g_other);
    // without the last pictures read, whose enhancement units come after the part that was read
    const struct comparison c = Compare(&g_other, &g_times, g_other.count > 48 ? g_other.count - 48 : 0);
    snprintf(text, sizeof(text),
             "an enhancement unit without a time is a hole in the layer: %u pictures go out without their "
             "enhancement picture that have one in the whole run (pictures %u to %u), and the %u that are paired "
             "get the same one", c.onlyB, c.onlyB ? c.firstOnly : 0, c.lastOnly, c.both);
    Check(g_other.untimed == 1 && g_other.timed && !g_other.wrong && c.both > 400 && !c.different && !c.onlyA &&
              c.onlyB >= 1 && c.onlyB <= 64, text);

    // ---- a stream that times only some of its access units
    struct options sparse = {.timed = 1, .seek = -1, .limit = 600, .sparseUntil = 60, .base1 = base1};
    Run(argv[1], &sparse, &g_other);
    printf("every second packet without the time: after %u of them the module said \"%s\"\n", g_other.sparseUntimed,
           g_other.sparseReason);
    Show("the stream again from its start, with all its times", &g_other);
    Check(g_other.sparseUntimed > 1 && !g_other.sparseTimedAfter && strstr(g_other.sparseReason, "picture order"),
          "a stream that leaves out the time of many enhancement units is paired by picture order from then on");
    unsigned same = 0, differ = 0;
    for (unsigned i = 0; i < 500 && i < g_other.count && i < g_order.count; ++i)
    {
      const struct picture* p = &g_other.pictures[i];
      const struct picture* q = &g_order.pictures[i];
      const int equal = p->pts == q->pts && p->paired == q->paired && (!p->paired || p->layer == q->layer);
      same += equal;
      differ += !equal;
    }
    snprintf(text, sizeof(text),
             "and stays so: played again with all its times, its first %u pictures are paired as by picture order", same);
    Check(!g_other.timed && !g_other.wrong && same == 500 && !differ, text);
  }

  // ---- times that are missing or were moved on their way: never the enhancement picture of another time
  if (HAVE_TIMES && (!quick || moved) && frameTicks)
  {
    const unsigned key = g_times.keys[1] ? g_times.keys[1] : 60; // the second random-access unit of the layer
    unsigned wrong, wrongAsItWas, lost, mostLost, resyncs;

   if (!quick)
   {
    // a hole at each of the access units around a random-access unit of the layer, one at a time
    wrong = lost = mostLost = resyncs = 0;
    for (unsigned at = key - 2; at <= key + 5; ++at)
    {
      struct options hole = {.timed = 1, .seek = -1, .limit = key + 150, .gap = at, .base1 = base1};
      Run(argv[1], &hole, &g_other);
      const unsigned l = Lost(&g_other, &g_times);
      printf("  packet %u without the time of its enhancement unit: %u pictures without their enhancement "
             "picture, %u late, %u resynchronizations\n", at, l, g_other.late, g_other.resyncs);
      wrong += g_other.wrong + !g_other.timed;
      lost += l;
      resyncs += g_other.resyncs;
      if (l > mostLost)
        mostLost = l;
    }
    snprintf(text, sizeof(text),
             "a hole at a random-access picture of the layer or next to it costs the pictures that cannot be "
             "decoded without it (%u at most here), and the layer goes on without starting again", mostLost);
    Check(!wrong && (!HAVE_TIME_UNUSABLE || (!resyncs && mostLost <= 64)), text);

    // one in 250 without its time, all through the first 1200 packets
    struct options rare = {.timed = 1, .seek = -1, .limit = 1200, .every = 250, .base1 = base1};
    Run(argv[1], &rare, &g_other);
    Show("one enhancement unit in 250 without its time", &g_other);
    snprintf(text, sizeof(text),
             "now and then an enhancement unit without its time: the stream stays paired by its times, %u "
             "pictures go out without their enhancement picture", Lost(&g_other, &g_times));
    Check(g_other.timed && !g_other.wrong && (!HAVE_TIME_UNUSABLE || !g_other.resyncs), text);

    // a base packet without its presentation time
    wrong = wrongAsItWas = lost = 0;
    for (unsigned at = key + 37; at <= key + 48; ++at)
    {
      for (int guess = 0; guess <= 1; ++guess)
      {
        struct options none = {.timed = 1, .seek = -1, .limit = key + 130, .noPts = at, .base1 = base1, .guess = guess};
        Run(argv[1], &none, &g_other);
        *(guess ? &wrongAsItWas : &wrong) += g_other.wrong;
        if (!guess)
          lost += Lost(&g_other, &g_times);
        if (g_other.wrong && guess)
        {
          snprintf(text, sizeof(text), "  packet %u without its presentation time, as it was", at);
          Show(text, &g_other);
        }
      }
    }
    snprintf(text, sizeof(text),
             "a base packet without its presentation time (twelve places): its picture is not paired by a guess "
             "(%u pictures in all without their enhancement picture); as it was, %u pictures got the enhancement "
             "picture of another time", lost, wrongAsItWas);
    Check(!wrong, text);
    printf("%s a base picture paired by the time the decoder guesses for it can get the enhancement picture of a "
           "neighbour (%u in these runs)\n", wrongAsItWas ? "shown:" : "not shown here:", wrongAsItWas);

    // the player is not sure of a jump: packets without any time
    wrong = 0;
    int stayed = 1;
    for (unsigned count = 9; count <= 21; count += 12)
    {
      struct options unknown = {.timed = 1, .seek = -1, .limit = key + 200, .unknownFrom = key + 20,
                                .unknownCount = count, .base1 = base1};
      Run(argv[1], &unknown, &g_other);
      snprintf(text, sizeof(text), "  %u packets in a row without any time", count);
      Show(text, &g_other);
      printf("  ... %u pictures without their enhancement picture\n", Lost(&g_other, &g_times));
      wrong += g_other.wrong;
      stayed &= g_other.timed;
    }
    Check(!wrong && (!HAVE_TIME_UNUSABLE || stayed),
          "packets without any time (the player is not sure of a jump) are holes: the stream stays paired by its "
          "times");
   }

    // the file starts later than its first pictures: the demuxer gives 0 to times up to half a second before
    // the start, which the difference to such a time does not survive
    wrong = wrongAsItWas = 0;
    unsigned unusable = 0;
    static const double later[] = {1, 2, 3, 4, 5, 11, 2.004, 1.996};
    for (unsigned k = 0; k < sizeof(later) / sizeof(*later); ++k)
    {
      for (int noCheck = 0; noCheck <= 1; ++noCheck)
      {
        struct options start = {.timed = 1, .seek = -1, .limit = 200, .startLater = later[k] * frame,
                                .base1 = base1, .noCheck = noCheck};
        Run(argv[1], &start, &g_other);
        *(noCheck ? &wrongAsItWas : &wrong) += g_other.wrong;
        if (!noCheck)
        {
          unusable += g_other.unusable;
          printf("  the file starts %.3f pictures after its first one: %u times of enhancement units not usable, "
                 "%u of %u pictures paired, %u resynchronizations, pairing by %s\n", later[k], g_other.unusable,
                 g_other.paired, g_other.count, g_other.resyncs, g_other.timed ? "times" : "picture order");
        }
        else if (g_other.wrong)
          Show("  ... as it was", &g_other);
      }
    }
    snprintf(text, sizeof(text),
             "a file that starts after its first pictures (eight start times): no picture gets the enhancement "
             "picture of another time; as it was, %u did", wrongAsItWas);
    Check(!wrong && (!HAVE_TIME_UNUSABLE || unusable > 0), text);
    printf("%s the difference to a time that the demuxer set to 0 names another picture (%u in these runs)\n",
           wrongAsItWas ? "shown:" : "not shown here:", wrongAsItWas);
    // With the base layer decoded by one thread, in the order of the stream, the check is what keeps those
    // pairs apart: a run of this section that would not notice the check gone says nothing about it.
    if (base1 && !g_swap)
      Check(wrongAsItWas > 0, "... and with one thread for the base layer these runs show it");

    // The same files, played by a player that has taken something off every time before (it joined a jump
    // to its clock in what it played earlier, and keeps that): the times that the demuxer set to 0 come as
    // one other time. Nothing may change for that: the same pictures paired, and no resynchronization.
    {
      unsigned restarted = 0, fewer = 0, runs = 0, ambiguous = 0;
      wrong = 0;
      static const double taken[] = {-3600, 7.25};
      for (unsigned k = 0; k < sizeof(later) / sizeof(*later); ++k)
      {
        struct options start = {.timed = 1, .seek = -1, .limit = 200, .startLater = later[k] * frame, .base1 = base1};
        Run(argv[1], &start, &g_other);
        const unsigned paired = g_other.paired, first = FirstPaired(&g_other), resyncs = g_other.resyncs;
        for (unsigned t = 0; t < sizeof(taken) / sizeof(*taken); ++t)
        {
          start.offsetFrom = 1;
          start.offsetSeconds = taken[t];
          Run(argv[1], &start, &g_other);
          ++runs;
          wrong += g_other.wrong;
          restarted += g_other.resyncs > resyncs;
          fewer += g_other.paired < paired;
          ambiguous += g_other.ambiguous;
          if (g_other.resyncs > resyncs || g_other.paired != paired)
            printf("  the file starts %.3f pictures after its first one, the player has taken %.2f s off the times: "
                   "%u of %u pictures paired (%u without that), the first one is picture %u (%u), %u "
                   "resynchronizations (%s)\n", later[k], taken[t], g_other.paired, g_other.count, paired,
                   FirstPaired(&g_other), first, g_other.resyncs, g_other.reason);
        }
      }
      snprintf(text, sizeof(text),
               "a player that had taken something off the times before (%u runs): the times that the demuxer set "
               "to 0 are known wherever they have gone (%u pictures of such a time went out without the layer), "
               "the layer does not start again for them, and the same pictures are paired", runs, ambiguous);
      Check(!wrong && (!HAVE_TIME_ZERO || (!restarted && !fewer)), text);
      if (!HAVE_TIME_ZERO)
        printf("with this module the layer started again in %u of these runs, and %u of them paired fewer pictures\n",
               restarted, fewer);
    }

    // The file starts at the time of a random-access unit of the layer. The base packets of the half second
    // up to it have the time 0, and so has that unit, which the layer can start at. None of those pictures
    // is the one picture of that time: none is paired, whatever happens to the layer meanwhile.
    if (HAVE_TIME_ZERO && numUnits)
    {
      unsigned plainWrong = 0, restartWrong = 0, restartRuns = 0, takenWrong = 0, takenRuns = 0, passed = 0;
      unsigned lateWrong = 0, lateRuns = 0, latePassed = 0;
      for (unsigned u = 0; u < numUnits; ++u)
      {
        const unsigned at = units[u].packet;
        struct options file = {.timed = 1, .seek = -1, .limit = at + 80, .startTicks = units[u].start, .base1 = base1};
        Run(argv[1], &file, &g_other);
        plainWrong += g_other.wrong;
        passed += g_other.ambiguous;
        printf("  the file starts with the unit of packet %u: %u of %u pictures paired, %u with the enhancement "
               "picture of another time, %u went out without the layer for their time, %u resynchronizations\n", at,
               g_other.paired, g_other.count, g_other.wrong, g_other.ambiguous, g_other.resyncs);
        // The layer starts again for a failure of its own (the base layer goes on, and the pictures of the
        // packets before are still in its decoder): after the first packet of that time, and after each of
        // the packets around the unit.
        for (unsigned after = at - 3; after <= at + 2; ++after)
        {
          file.resyncAfter = after == at + 2 ? units[u].firstZero : after;
          Run(argv[1], &file, &g_other);
          ++restartRuns;
          restartWrong += g_other.wrong;
          passed += g_other.ambiguous;
          if (g_other.wrong)
            printf("  ... the layer starts again after packet %u: the picture of packet %d got the enhancement unit "
                   "of packet %d (%u wrong, %u went out without the layer for their time)\n", file.resyncAfter,
                   g_other.wrongOwn, g_other.wrongLayer, g_other.wrong, g_other.ambiguous);
        }
        // ... and with a player that had taken something off the times before: the 0 is another time
        static const double taken[] = {-3600, 7.25};
        for (unsigned k = 0; u + 1 == numUnits && k < 4; ++k)
        {
          file.resyncAfter = k % 2 ? at - 1 : 0;
          file.offsetFrom = 1;
          file.offsetSeconds = taken[k / 2];
          Run(argv[1], &file, &g_other);
          ++takenRuns;
          takenWrong += g_other.wrong;
          passed += g_other.ambiguous;
          if (g_other.wrong)
            printf("  ... the player has taken %.2f s off the times, the layer starts again after packet %u: the "
                   "picture of packet %d got the enhancement unit of packet %d\n", taken[k / 2], file.resyncAfter,
                   g_other.wrongOwn, g_other.wrongLayer);
        }
        // The same file played from a random-access picture of the base layer on, and the packets before the
        // unit do not say the time of their enhancement units: the layer has had nothing to start at when the
        // unit comes, and starts with it, by the times that the stream gives from there on. The base packets
        // of the time 0 came before that.
        if (units[u].baseFrom)
        {
          struct options late = {.timed = 1, .seek = -1, .limit = at + 80, .startTicks = units[u].start, .base1 = base1,
                                 .untimedUntil = at - 1, .skipUntil = units[u].baseFrom - 1};
          Run(argv[1], &late, &g_other);
          ++lateRuns;
          lateWrong += g_other.wrong + !g_other.timed;
          latePassed += g_other.ambiguous;
          printf("  ... played from packet %u on, the times given from the unit on: %u of %u pictures paired, %u "
                 "with the enhancement picture of another time (the picture of packet %d the unit of packet %d), "
                 "%u went out without the layer for their time, %u resynchronizations\n", units[u].baseFrom,
                 g_other.paired, g_other.count, g_other.wrong, g_other.wrongOwn, g_other.wrongLayer,
                 g_other.ambiguous, g_other.resyncs);
        }
      }
      snprintf(text, sizeof(text),
               "a file that starts at the time of a random-access unit of the layer (%u such units): no picture of "
               "the time 0 is paired, also when the layer starts again for a failure of its own around the unit "
               "(%u runs; the first picture of that time got the unit), and with the 0 moved by "
               "the player (%u runs); %u pictures went out without the layer for their time", numUnits, restartRuns,
               takenRuns, passed);
      Check(!plainWrong && !restartWrong && !takenWrong && passed > 0, text);
      if (lateRuns)
      {
        snprintf(text, sizeof(text),
                 "base packets of the time 0 that came before the stream gave the time of an enhancement unit count "
                 "as well (%u runs): no picture of that time is paired (%u went out without the layer)", lateRuns,
                 latePassed);
        Check(!lateWrong && latePassed > 0, text);
      }
      else
        printf("not shown here: no random-access picture of the base layer from which the layer has nothing to "
               "start at before such a unit\n");

      // A file that starts with its first picture, which alone has the time 0, played again from its start
      // after a seek to it: what came before the flush is gone from the base decoder too, and the picture of
      // the time 0 is the only one of its time again.
      if (firstTime != AV_NOPTS_VALUE)
      {
        struct options again = {.timed = 1, .seek = -1, .limit = 120, .startTicks = firstTime, .base1 = base1,
                                .replayAfter = 60};
        Run(argv[1], &again, &g_other);
        int zeroPaired = 0, zeros = 0;
        for (unsigned i = 0; i < g_other.count; ++i)
        {
          zeros += g_other.pictures[i].pts == 0;
          zeroPaired |= g_other.pictures[i].pts == 0 && g_other.pictures[i].paired;
        }
        snprintf(text, sizeof(text),
                 "a flush of the base decoder forgets the base packets of the time 0: played again from its start "
                 "after a seek, the first picture of a file, the only one of that time, is paired as it was before "
                 "(%u of %u pictures paired, %u went out without the layer for their time)", g_other.paired,
                 g_other.count, g_other.ambiguous);
        Check(g_other.replayZeroPaired && zeros == 1 && zeroPaired && !g_other.wrong && !g_other.ambiguous, text);
      }

      // The player moves the times while the stream has not given the time of an enhancement unit yet: they
      // go back by twelve pictures, so the base packets after that have the times that packets before it
      // had, in what is another count of the times. They are not the times of earlier packets.
      const unsigned at = units[0].packet;
      struct options back = {.timed = 1, .seek = -1, .limit = at + 80, .base1 = base1, .untimedUntil = at - 3,
                             .skipUntil = g_times.keys[0], .offsetFrom = at - 7, .offsetSeconds = 12 * frame};
      Run(argv[1], &back, &g_other);
      Show("the player moves the times back by twelve pictures before the stream gives its times", &g_other);
      Check(!g_other.wrong && g_other.timed && !g_other.repeats,
            "times that the player moved before the stream gave the time of an enhancement unit are not compared "
            "with the times from before: the layer does not start again for a time that came twice");
    }

    // the player joins a jump to its clock: every time from a packet on is moved
    wrong = wrongAsItWas = 0;
    unsigned wrongBack = 0, resyncsBack = 0; // without the player's count, where the times went back
    static const double moves[] = {-3, -10, 3, -24, 24};
    for (unsigned k = 0; k < sizeof(moves) / sizeof(*moves); ++k)
    {
      for (unsigned from = key + 41; from <= key + 42; ++from)
      {
        for (int noGuard = 0; noGuard <= 1; ++noGuard)
        {
          // Times that go back in the stream itself come again: the times of the stream go back by the
          // distance between two of its own times, in its own time base, before the demuxer makes the times
          // of the player of them. (By a number of pictures in seconds they come again only as far as the
          // rounding lets them: not where the stream rounds them, as Matroska does to the millisecond, nor
          // in an MP4 time base of 1/24000.) The run from the start numbers the packets as the one by the
          // times did.
          double seconds = moves[k] * frame;
          int64_t ticks = 0;
          const unsigned earlier = moves[k] > 0 && from > (unsigned)moves[k] ? from - (unsigned)moves[k] : 0;
          if (noGuard && earlier && from < PACKETS_MAX && g_own[from] != AV_NOPTS_VALUE &&
              g_own[earlier] != AV_NOPTS_VALUE)
          {
            ticks = g_own[from] - g_own[earlier];
            seconds = 0;
          }
          struct options offset = {.timed = 1, .seek = -1, .limit = key + 200, .offsetFrom = from,
                                   .offsetSeconds = seconds, .offsetTicks = ticks, .base1 = base1,
                                   .noGuard = noGuard};
          Run(argv[1], &offset, &g_other);
          if (!noGuard)
          {
            wrong += g_other.wrong;
            printf("  the player takes %.0f pictures off the times from packet %u on: %u of %u pictures paired, %u "
                   "resynchronizations\n", moves[k], from, g_other.paired, g_other.count, g_other.resyncs);
            continue;
          }
          // Without the count of the player: the times of the stream itself jump. Taking pictures off them
          // makes them later, and adding makes them go back: times come again, for other pictures.
          if (moves[k] > 0)
          {
            wrongBack += g_other.wrong;
            resyncsBack += g_other.resyncs;
            printf("  ... the same in the stream itself, its times going back %.0f pictures: %u of %u pictures "
                   "paired, %u with the enhancement picture of another time, %u resynchronizations (%s)\n",
                   moves[k], g_other.paired, g_other.count, g_other.wrong, g_other.resyncs, g_other.reason);
          }
          else
            wrongAsItWas += g_other.wrong;
          if (g_other.wrong)
            Show("  ... as it was", &g_other);
        }
      }
    }
    snprintf(text, sizeof(text),
             "the player moves the times of the stream (ten cases): the layer starts again, and no picture gets "
             "the enhancement picture of another time; without that, %u did where the times became later", wrongAsItWas);
    Check(!wrong, text);
    snprintf(text, sizeof(text),
             "times of the stream that go back (four cases, no word from the player): a base packet has the time "
             "of an earlier one, the layer starts again (%u times), and no picture gets the enhancement picture of "
             "another time", resyncsBack);
    Check(!HAVE_BASE_TIMES || (!wrongBack && resyncsBack >= 4), text);
    if (!HAVE_BASE_TIMES)
      printf("with this module %u pictures did\n", wrongBack);
  }
  if (pictures)
    fclose(pictures);
  snprintf(text, sizeof(text), "in all the runs as Kodi plays, the module never gave a picture another RPU "
           "than its own (%u did)", g_wrongGiven);
  printf("%s with a protection of Kodi's decoder left out (the runs \"as it was\": a guessed time, times that the "
         "player moved asked without a word to the module), the module can give a picture the RPU of the access unit "
         "of the time it is asked for, another picture's (%u in these runs)\n",
         g_wrongGivenAsItWas ? "shown:" : "not shown here:", g_wrongGivenAsItWas);
  CheckRpu(!g_wrongGiven, text);
  if (!HAVE_TIMES)
    printf("this module does not take the times of the enhancement units: only the runs by picture order\n");
  else if (!HAVE_TIME_UNUSABLE)
    printf("this module has no DVBRIDGE_FEL_LAYER_TIME_UNUSABLE: its runs \"as it was\" are its own\n");
  printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
