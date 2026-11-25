// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of CDVLayerMerge (xbmc/cores/VideoPlayer/DVDDemuxers/DVLayerMerge.cpp), with the project's host
// build of FFmpeg 9.0.2. The file under test is the one Kodi compiles.
// 1. the two video tracks of FEL_test_for_AVS.m2ts, found and joined as Kodi's demuxer does it (Find(), Read()),
//    against BL_EL.mkv, the one-track file made of the same two tracks: every access unit, unit by unit;
// 2. the same with a copy of the file that is signalled as the discs are (no Dolby Vision descriptor: the pair is
//    known by its stream numbers);
// 3. the same after seeks;
// 4. enhancement packets that are missing, late, far behind, far ahead or without a time;
// 5. Append() with buffers that are not unit streams; the level and the size rule;
// 6. what a joined packet says of the presentation time of its enhancement access unit, against the
//    enhancement track read on its own, and with packets made here.
// 8-11. with a fourth argument: the same stream in Matroska and MP4 (and variants of them), and the file of
//    issue 1 (MP4) in three containers, joined to the same access units as the transport stream;
// 12. Append() with units that have a length before them; 13. the times made from presentation times
//    (Matroska); 14. an enhancement track in another time base; 16. Matroska tracks that libavformat does not group,
//    a pair only when their packets show it; 17. the probe of an RPU and the record made of it.
// Usage: dvmerge_test <two-track .m2ts> <one-track .mkv> <two-track .m2ts signalled as a disc> [<samples>]
// <samples>: the folder of the files of sections 8 to 17, which also holds BL_EL.mkv and FEL_test_for_AVS.m2ts
#include "DVLayerMerge.h"
#include "StreamGroupStreams.h"

// tools/regress: the join under test has every part that this test checks; a build without one of them fails
#ifdef DVMERGE_NO_PROBE
#error "the test of the join is built without the probe of the RPU"
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/dovi_meta.h>
}
#ifndef DVMERGE_NO_PROBE
#include "dvbridge_probe.h"
#endif

static int failures = 0;

static void Check(const std::string& what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++failures;
}

using Unit = std::vector<uint8_t>;

// the units of a start-code buffer, without trailing zeros
static bool UnitsAnnexB(const uint8_t* d, size_t n, std::vector<Unit>& out)
{
  auto find = [&](size_t from) {
    for (size_t k = from; k + 3 <= n; ++k)
      if (d[k] == 0 && d[k + 1] == 0 && d[k + 2] == 1)
        return k;
    return n;
  };
  size_t code = find(0);
  if (code == n)
    return false;
  for (size_t i = 0; i < code; ++i)
    if (d[i] != 0)
      return false;
  while (code < n)
  {
    const size_t begin = code + 3, next = find(begin);
    size_t end = next;
    while (end > begin && d[end - 1] == 0)
      --end;
    out.emplace_back(d + begin, d + end);
    code = next;
  }
  return true;
}

// the units of a length-prefixed buffer
static bool UnitsLength(const uint8_t* d, size_t n, size_t lengthSize, std::vector<Unit>& out)
{
  size_t i = 0;
  while (i + lengthSize <= n)
  {
    size_t len = 0;
    for (size_t k = 0; k < lengthSize; ++k)
      len = (len << 8) | d[i + k];
    i += lengthSize;
    if (len > n - i)
      return false;
    out.emplace_back(d + i, d + i + len);
    i += len;
  }
  return i == n;
}

static unsigned int Type(const Unit& u)
{
  return u.empty() ? 99 : (u[0] >> 1) & 0x3f;
}

// the one-track file has an access unit delimiter in front of each base access unit; the transport stream has none
static std::vector<Unit> WithoutDelimiters(const std::vector<Unit>& units)
{
  std::vector<Unit> out;
  for (const auto& u : units)
    if (Type(u) != 35)
      out.push_back(u);
  return out;
}

struct Source
{
  AVFormatContext* ctx = nullptr;
  CDVLayerMerge::Layers layers;
  bool Open(const char* path)
  {
    if (avformat_open_input(&ctx, path, nullptr, nullptr) < 0 || avformat_find_stream_info(ctx, nullptr) < 0)
      return false;
    layers = CDVLayerMerge::Find(ctx, 0);
    return true;
  }
  bool Joined() const { return layers.result == CDVLayerMerge::Layers::Result::JOINED; }
  ~Source() { avformat_close_input(&ctx); }
};

struct Joined
{
  int64_t dts;
  int64_t pts;
  bool key;
  std::vector<uint8_t> data;
  bool timed = false; // it says when its enhancement access unit is presented:
  int64_t difference = 0; // this much later than the packet itself,
  AVRational timeBase{0, 1}; // in this time base
};

// What a packet says of the presentation time of its enhancement access unit (CDVLayerMerge::LAYER_TIME),
// and how many entries its strings metadata has.
static bool LayerTime(const AVPacket* pkt, int64_t& difference, AVRational& timeBase, std::string* value = nullptr,
                      int* entries = nullptr)
{
  size_t size = 0;
  const uint8_t* data = av_packet_get_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA, &size);
  AVDictionary* dictionary = nullptr;
  bool ok = false;
  if (value)
    value->clear();
  if (entries)
    *entries = 0;
  if (data && av_packet_unpack_dictionary(data, size, &dictionary) >= 0)
  {
    if (entries)
      *entries = av_dict_count(dictionary);
    const AVDictionaryEntry* entry = av_dict_get(dictionary, CDVLayerMerge::LAYER_TIME, nullptr, 0);
    long long number = 0;
    int num = 0, den = 0;
    if (entry && value)
      *value = entry->value;
    if (entry && std::sscanf(entry->value, "%lld %d %d", &number, &num, &den) == 3)
    {
      difference = number;
      timeBase = AVRational{num, den};
      ok = true;
    }
  }
  av_dict_free(&dictionary);
  return ok;
}

// read the file as Kodi's demuxer does
static std::vector<Joined> ReadJoined(Source& s, CDVLayerMerge& merge, size_t limit, int* last = nullptr)
{
  std::vector<Joined> out;
  AVPacket* pkt = av_packet_alloc();
  int result = 0;
  while (out.size() < limit && (result = merge.Read(s.ctx, s.layers.base, s.layers.enhancement, pkt)) >= 0)
  {
    if (pkt->stream_index == s.layers.base)
    {
      out.push_back({pkt->dts, pkt->pts, (pkt->flags & AV_PKT_FLAG_KEY) != 0,
                     std::vector<uint8_t>(pkt->data, pkt->data + pkt->size)});
      out.back().timed = LayerTime(pkt, out.back().difference, out.back().timeBase);
    }
    else if (pkt->stream_index == s.layers.enhancement)
      Check("a packet of the enhancement track came out", false);
    av_packet_unref(pkt);
  }
  if (last)
    *last = result;
  av_packet_free(&pkt);
  return out;
}

static AVPacket* Packet(int64_t dts, const std::vector<uint8_t>& bytes, int64_t duration = 0)
{
  AVPacket* p = av_packet_alloc();
  av_new_packet(p, static_cast<int>(bytes.size()));
  std::memcpy(p->data, bytes.data(), bytes.size());
  p->dts = p->pts = dts;
  p->duration = duration;
  return p;
}

static bool IsLayer(const Unit& u)
{
  return Type(u) == 63 || Type(u) == 62;
}

static bool SameRecord(const AVDOVIDecoderConfigurationRecord& r)
{
  return r.dv_version_major == 1 && r.dv_version_minor == 0 && r.dv_profile == 7 && r.dv_level == 6 &&
         r.rpu_present_flag == 1 && r.el_present_flag == 1 && r.bl_present_flag == 1 &&
         r.dv_bl_signal_compatibility_id == 6 && r.dv_md_compression == AV_DOVI_COMPRESSION_NONE;
}

// the joined packets of a two-track file against the one-track file
static void Compare(const std::string& name, const std::vector<Joined>& whole, const CDVLayerMerge::Counts& counts,
                    const char* reference)
{
  std::printf("      %s: %zu base packets: %llu joined, %llu alone; %llu enhancement packets dropped, %llu malformed; at "
              "most %zu base and %zu enhancement packets waited\n",
              name.c_str(), whole.size(), (unsigned long long)counts.joined, (unsigned long long)counts.alone,
              (unsigned long long)counts.dropped, (unsigned long long)counts.malformed, counts.waited, counts.queued);

  Source one;
  Check(name + ": the one-track file opens and has no pair",
        one.Open(reference) && one.ctx->nb_streams >= 1 && one.layers.result == CDVLayerMerge::Layers::Result::NONE);
  const AVCodecParameters* par = one.ctx->streams[0]->codecpar;
  const size_t lengthSize = par->extradata_size >= 23 && par->extradata[0] == 1 ? (par->extradata[21] & 3u) + 1u : 0u;
  Check(name + ": the one-track file is length-prefixed (" + std::to_string(lengthSize) + " bytes)", lengthSize > 0);
  const AVPacketSideData* sd = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
  Check(name + ": the one-track file has the Dolby Vision record that the joined stream gets",
        sd && sd->size >= sizeof(AVDOVIDecoderConfigurationRecord) &&
            SameRecord(*reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(sd->data)));

  size_t units = 0, bytes = 0, wrapped = 0, rpus = 0, compared = 0, different = 0, withLayer = 0, refWithLayer = 0;
  struct Difference
  {
    size_t index;
    std::vector<Unit> reference, joined;
  };
  std::vector<Difference> differences;
  AVPacket* pkt = av_packet_alloc();
  size_t index = 0;
  while (av_read_frame(one.ctx, pkt) >= 0)
  {
    if (pkt->stream_index != 0)
    {
      av_packet_unref(pkt);
      continue;
    }
    std::vector<Unit> ref, got;
    const bool refOk = UnitsLength(pkt->data, static_cast<size_t>(pkt->size), lengthSize, ref);
    const bool gotOk = index < whole.size() && UnitsAnnexB(whole[index].data.data(), whole[index].data.size(), got);
    const auto a = WithoutDelimiters(ref), b = WithoutDelimiters(got);
    bool refLayer = false, gotLayer = false;
    for (const auto& u : a)
      refLayer |= Type(u) == 63;
    for (const auto& u : b)
    {
      gotLayer |= Type(u) == 63;
      wrapped += Type(u) == 63;
      rpus += Type(u) == 62;
      bytes += u.size();
    }
    units += b.size();
    withLayer += gotLayer;
    refWithLayer += refLayer;
    ++compared;
    if (!refOk || !gotOk || a != b)
    {
      if (differences.size() < 5)
        differences.push_back({index, a, b});
      if (different++ < 5)
      {
        std::printf("      access unit %zu differs: %zu units against %zu;", index, b.size(), a.size());
        for (size_t k = 0; k < std::max(a.size(), b.size()) && k < 40; ++k)
          if (k >= a.size() || k >= b.size() || a[k] != b[k])
          {
            std::printf(" first difference at unit %zu (type %u/%u, size %zu/%zu)", k, k < b.size() ? Type(b[k]) : 99,
                        k < a.size() ? Type(a[k]) : 99, k < b.size() ? b[k].size() : 0, k < a.size() ? a[k].size() : 0);
            break;
          }
        std::printf("\n");
      }
    }
    av_packet_unref(pkt);
    ++index;
  }
  av_packet_free(&pkt);
  std::printf("      compared %zu access units: %zu units, %zu bytes, %zu enhancement units wrapped, %zu RPUs\n",
              compared, units, bytes, wrapped, rpus);
  Check(name + ": as many access units as the one-track file (" + std::to_string(whole.size()) + " against " +
            std::to_string(compared) + ")",
        whole.size() == compared && compared > 2000);
  // The last PES packet of each track of the test file holds two access units, so the last unit of each track
  // has no time of its own. The enhancement track ends two units before the base track: its last unit follows
  // the one of the base unit n-4, so it belongs to the base unit n-3, and the join puts it there. The one-track
  // file has it on the last base unit, n-1, instead. Everything else is the same.
  bool moved = different == 2 && differences.size() == 2 && compared >= 4 && differences[0].index == compared - 3 &&
               differences[1].index == compared - 1;
  if (moved)
  {
    const Difference& early = differences[0]; // ours has the layer, the reference has none
    const Difference& late = differences[1]; // the reference has the layer, ours has none
    std::vector<Unit> expectEarly = early.reference, expectLate;
    for (const auto& u : late.reference)
      (IsLayer(u) ? expectEarly : expectLate).push_back(u);
    moved = early.joined == expectEarly && late.joined == expectLate && expectEarly.size() > early.reference.size();
  }
  Check(name + ": every access unit has the same units with the same bytes, but for the last enhancement unit, which "
               "goes with base unit n-3 as its place in the track says (" + std::to_string(different) + " units differ)",
        moved);
  Check(name + ": as many access units carry an enhancement layer (" + std::to_string(withLayer) + " against " +
            std::to_string(refWithLayer) + ")",
        withLayer == refWithLayer && withLayer == counts.joined && withLayer + counts.alone == whole.size());
  Check(name + ": nothing dropped, nothing malformed", counts.dropped == 0 && counts.malformed == 0);
  Check(name + ": one RPU for each joined access unit", rpus == counts.joined);
}

// ---- the same stream in other containers (MP4, Matroska): a joined access unit by what it is, whatever the
// framing and the time base. A picture is named by its place in the stream (its time over the frame rate,
// from the start of the file): the times of Matroska are rounded to the millisecond, the places are not.
struct Canon
{
  bool key = false;              // the base picture is a random-access picture
  std::vector<Unit> base;        // the units of the base layer
  std::vector<Unit> layer;       // the units of the enhancement layer (out of their type 63) and the RPU
  long long layerPlace = LLONG_MIN; // where the enhancement access unit is presented (LAYER_TIME), if said
};

static long long Place(int64_t pts, AVRational timeBase, double start, AVRational rate)
{
  return llround((static_cast<double>(pts) * av_q2d(timeBase) - start) * av_q2d(rate));
}

// noParameterSets: VPS, SPS and PPS are left out ('hvc1' and 'dvh1' tracks have them only in their records)
static bool CanonUnits(const std::vector<uint8_t>& data, int lengthSize, Canon& c, bool noParameterSets)
{
  std::vector<Unit> units;
  const bool ok = lengthSize ? UnitsLength(data.data(), data.size(), static_cast<size_t>(lengthSize), units)
                             : UnitsAnnexB(data.data(), data.size(), units);
  const auto parameterSet = [](const Unit& u) { return Type(u) >= 32 && Type(u) <= 34; };
  for (const auto& u : units)
  {
    if (Type(u) == 63 && u.size() > 2)
    {
      Unit inner(u.begin() + 2, u.end());
      if (!noParameterSets || !parameterSet(inner))
        c.layer.push_back(std::move(inner));
    }
    else if (Type(u) == 62)
      c.layer.push_back(u);
    else if (!noParameterSets || !parameterSet(u))
      c.base.push_back(u);
  }
  return ok;
}

struct Joins
{
  std::map<long long, Canon> byPlace;
  size_t packets = 0, unreadable = 0, untimed = 0, repeated = 0;
  long long firstRepeated = LLONG_MAX; // the place of the first packet at the place of an earlier one
  CDVLayerMerge::Counts counts;
};

// Every packet of the file through the join, by place. limit: base packets; seek: a part of the duration to
// seek to first (as Kodi's demuxer seeks), negative: none. order: the places of the base packets as they came.
// verified: what the packets of an unverified pair said (1 a pair, 0 pending, -1 refused).
static Joins ReadPlaces(Source& s, size_t limit = SIZE_MAX, double seek = -1, bool noParameterSets = false,
                        std::vector<long long>* order = nullptr, int* verified = nullptr)
{
  Joins out;
  if (!s.Joined())
    return out;
  CDVLayerMerge merge;
  merge.Configure(s.layers);
  struct Done
  {
    CDVLayerMerge& merge;
    int* verified;
    ~Done()
    {
      if (!verified)
        return;
#ifndef DVMERGE_NO_PROBE
      using V = CDVLayerMerge::Verification;
      *verified = merge.GetVerification() == V::VERIFIED ? 1 : merge.GetVerification() == V::PENDING ? 0 : -1;
#else
      *verified = 1;
#endif
    }
  } done{merge, verified};
  if (seek >= 0)
  {
    const int64_t start = s.ctx->start_time == AV_NOPTS_VALUE ? 0 : s.ctx->start_time;
    av_seek_frame(s.ctx, -1, start + static_cast<int64_t>(seek * static_cast<double>(s.ctx->duration)),
                  AVSEEK_FLAG_BACKWARD);
    merge.Clear();
  }
  const AVStream* st = s.ctx->streams[s.layers.base];
  const double start = s.ctx->start_time == AV_NOPTS_VALUE ? 0.0 : s.ctx->start_time / 1e6;
  for (const Joined& j : ReadJoined(s, merge, limit))
  {
    ++out.packets;
    if (j.pts == AV_NOPTS_VALUE)
    {
      ++out.untimed;
      continue;
    }
    Canon c;
    c.key = j.key;
    out.unreadable += !CanonUnits(j.data, s.layers.baseLengthSize, c, noParameterSets);
    if (j.timed && j.timeBase.num > 0 && j.timeBase.den > 0)
    {
      const double layerSeconds =
          static_cast<double>(j.pts) * av_q2d(st->time_base) + static_cast<double>(j.difference) * av_q2d(j.timeBase);
      c.layerPlace = llround((layerSeconds - start) * av_q2d(s.layers.rate));
    }
    const long long place = Place(j.pts, st->time_base, start, s.layers.rate);
    if (order)
      order->push_back(place);
    if (out.byPlace.count(place))
    {
      ++out.repeated;
      out.firstRepeated = std::min(out.firstRepeated, place);
    }
    out.byPlace[place] = std::move(c);
  }
  out.counts = merge.GetCounts();
  return out;
}

struct Agreement
{
  size_t both = 0, sameBase = 0, sameLayer = 0, sameTime = 0, withLayer = 0, onlyA = 0, onlyB = 0;
  size_t tail = 0; // access units from `from` on that are not compared (see Agree())
  long long firstDifferent = LLONG_MIN, lastBoth = LLONG_MIN, firstOnly = LLONG_MAX;
};

// from: the access units from this place on are left out (counted in tail). The transport stream of the test file
// ends with a PES packet of two access units in each track, so its last enhancement unit has no time of its own
// and goes with base unit n-3 (see Compare()); a file made of it in another container gives it a time, and it goes
// elsewhere.
static Agreement Agree(const Joins& a, const Joins& b, long long from = LLONG_MAX)
{
  Agreement r;
  for (const auto& [place, c] : a.byPlace)
  {
    if (place >= from)
    {
      ++r.tail;
      continue;
    }
    const auto it = b.byPlace.find(place);
    if (it == b.byPlace.end())
    {
      ++r.onlyA;
      r.firstOnly = std::min(r.firstOnly, place);
      continue;
    }
    ++r.both;
    r.lastBoth = std::max(r.lastBoth, place);
    const bool base = c.base == it->second.base, layer = c.layer == it->second.layer,
               time = c.layerPlace == it->second.layerPlace;
    r.sameBase += base;
    r.sameLayer += layer;
    r.sameTime += time;
    r.withLayer += !c.layer.empty();
    if ((!base || !layer || !time) && r.firstDifferent == LLONG_MIN)
    {
      r.firstDifferent = place;
      // what differs, unit by unit: type/size
      const auto show = [](const char* what, const std::vector<Unit>& units) {
        std::printf("        %s:", what);
        for (const auto& u : units)
          std::printf(" %u/%zu", Type(u), u.size());
        std::printf("\n");
      };
      std::printf("      picture %lld differs (time of the enhancement unit %lld against %lld)\n", place, c.layerPlace,
                  it->second.layerPlace);
      show("base, first", c.base);
      show("base, second", it->second.base);
      show("layer, first", c.layer);
      show("layer, second", it->second.layer);
    }
  }
  for (const auto& [place, c] : b.byPlace)
    if (place < from && !a.byPlace.count(place))
    {
      ++r.onlyB;
      r.firstOnly = std::min(r.firstOnly, place);
    }
  return r;
}

static std::string Text(const Agreement& r)
{
  return std::to_string(r.both) + " access units in both, " + std::to_string(r.withLayer) +
         " with their layer; the same base units in " + std::to_string(r.sameBase) + ", the same enhancement units and "
         "RPU in " + std::to_string(r.sameLayer) + ", the same time of the enhancement unit in " +
         std::to_string(r.sameTime) + "; " + std::to_string(r.onlyA) + " only in the first, " +
         std::to_string(r.onlyB) + " only in the second" +
         (r.tail ? ", the last " + std::to_string(r.tail) + " of the first not compared" : "") +
         (r.firstDifferent != LLONG_MIN ? ", first difference at picture " + std::to_string(r.firstDifferent) : "");
}

static bool Same(const Agreement& r, size_t atLeast)
{
  return r.both >= atLeast && r.sameBase == r.both && r.sameLayer == r.both && r.sameTime == r.both &&
         r.withLayer + 4 >= r.both;
}

#ifndef DVMERGE_NO_PROBE
// ---- 17. dvbridge_rpu_probe_packet() and RecordFromRpu(): on packets made here, and on every packet of the
// samples, and what the probe reads against what FFmpeg's decoder parses of the same RPUs
struct BitWriter
{
  std::vector<uint8_t> d;
  size_t n = 0;
  void Put(uint64_t v, int bits)
  {
    for (int i = bits - 1; i >= 0; --i, ++n)
    {
      if (n % 8 == 0)
        d.push_back(0);
      if ((v >> i) & 1)
        d.back() |= static_cast<uint8_t>(0x80 >> (n % 8));
    }
  }
  void Ue(uint64_t v)
  {
    ++v;
    int len = 0;
    while ((v >> len) > 1)
      ++len;
    Put(0, len);
    Put(v, len + 1);
  }
};

struct RpuHeader
{
  unsigned prefix = 25, type = 2, format = 0, profile = 1, seq = 1, coefType = 0, denom = 23, full = 0;
  unsigned bl = 10, el = 10, vdr = 12, elResampling = 1, disabled = 0;
};

// an access unit framed with 4-byte lengths: a slice unit, then (unless first) the RPU unit made of h
static std::vector<uint8_t> RpuPacket(const RpuHeader& h, bool rpuFirst = false, bool layerUnit = false)
{
  BitWriter w;
  w.Put(h.prefix, 8);
  w.Put(h.type, 6);
  w.Put(h.format, 11);
  w.Put(h.profile, 4);
  w.Put(0, 4);
  w.Put(h.seq, 1);
  w.Put(0, 1);
  w.Put(h.coefType, 2);
  if (h.coefType == 0)
    w.Ue(h.denom);
  w.Put(1, 2);
  w.Put(h.full, 1);
  w.Ue(h.bl - 8);
  w.Ue(h.el - 8);
  w.Ue(h.vdr - 8);
  w.Put(0, 1);
  w.Put(0, 3);
  w.Put(h.elResampling, 1);
  w.Put(h.disabled, 1);
  w.Put(0xa5a5a5a5, 32);
  std::vector<uint8_t> rpu = {0x7c, 0x01};
  int zeros = 0;
  for (uint8_t b : w.d)
  {
    if (zeros >= 2 && b <= 3)
    {
      rpu.push_back(3);
      zeros = 0;
    }
    rpu.push_back(b);
    zeros = b ? 0 : zeros + 1;
  }
  const std::vector<uint8_t> slice = {0x02, 0x01, 0xd0, 0x2f, 0x81};
  const std::vector<uint8_t> layer = {0x7e, 0x01, 0x02, 0x01, 0xd0, 0x2f, 0x81};
  std::vector<uint8_t> out;
  const auto add = [&out](const std::vector<uint8_t>& u) {
    const uint32_t n = static_cast<uint32_t>(u.size());
    for (int i = 3; i >= 0; --i)
      out.push_back(static_cast<uint8_t>(n >> (8 * i)));
    out.insert(out.end(), u.begin(), u.end());
  };
  if (rpuFirst)
    add(rpu);
  add(slice);
  if (layerUnit)
    add(layer);
  if (!rpuFirst)
    add(rpu);
  return out;
}

static void Probes(const std::string& dir)
{
  // made here: the profile as FFmpeg guesses it, and the record that is made of it
  {
    AVCodecParameters* par = avcodec_parameters_alloc();
    par->width = 3840;
    par->height = 2160;
    struct Case
    {
      const char* what;
      RpuHeader h;
      bool first, layer;
      int primaries, trc;
      bool rpu;
      unsigned profile;
      int record;  // the profile of the record made, 0: none
      int compat;
    };
    RpuHeader fel, p5, p8, p4, p0, noSeq, coef2, denom12, prefix, type1, format;
    p5.profile = 0, p5.full = 1, p5.elResampling = 0, p5.disabled = 1, p5.vdr = 12;
    p8.elResampling = 0, p8.disabled = 1;
    p4.vdr = 10;
    p0.profile = 0, p0.full = 0;
    noSeq.seq = 0;
    coef2.coefType = 2;
    denom12.denom = 12;
    prefix.prefix = 24;
    type1.type = 1;
    format.format = 0x100;
    const int bt2020 = AVCOL_PRI_BT2020, pq = AVCOL_TRC_SMPTE2084, hlg = AVCOL_TRC_ARIB_STD_B67;
    const int bt709 = AVCOL_PRI_BT709, t709 = AVCOL_TRC_BT709, none = AVCOL_PRI_UNSPECIFIED;
    const Case cases[] = {
        {"profile 7 with both layers", fel, false, true, bt2020, pq, true, 7, 7, 6},
        {"profile 7 RPU without the layer", fel, false, false, bt2020, pq, true, 7, 0, 0},
        {"profile 4 with both layers", p4, false, true, bt709, t709, true, 4, 0, 0},
        {"profile 5", p5, false, false, none, none, true, 5, 5, 0},
        {"profile 8, PQ", p8, false, false, bt2020, pq, true, 8, 8, 1},
        {"profile 8, HLG", p8, false, false, bt2020, hlg, true, 8, 8, 4},
        {"profile 8, SDR", p8, false, false, bt709, t709, true, 8, 8, 2},
        {"profile 8, colour not said", p8, false, false, none, none, true, 8, 0, 0},
        {"profile 8 with units of type 63", p8, false, true, bt2020, pq, true, 8, 0, 0},
        {"RPU profile 0, limited range", p0, false, false, bt2020, pq, true, 0, 0, 0},
        {"the RPU as the first unit", fel, true, true, bt2020, pq, false, 0, 0, 0},
        {"no sequence information", noSeq, false, true, bt2020, pq, false, 0, 0, 0},
        {"coefficient type 2", coef2, false, true, bt2020, pq, false, 0, 0, 0},
        {"denominator 12", denom12, false, true, bt2020, pq, false, 0, 0, 0},
        {"another prefix", prefix, false, true, bt2020, pq, false, 0, 0, 0},
        {"RPU type 1", type1, false, true, bt2020, pq, false, 0, 0, 0},
        {"another RPU format", format, false, true, bt2020, pq, false, 0, 0, 0},
    };
    bool ok = true;
    std::string got;
    for (const Case& c : cases)
    {
      const std::vector<uint8_t> p = RpuPacket(c.h, c.first, c.layer);
      dvbridge_rpu_probe probe;
      dvbridge_rpu_probe_packet(p.data(), p.size(), 4, &probe);
      par->color_primaries = static_cast<AVColorPrimaries>(c.primaries);
      par->color_trc = static_cast<AVColorTransferCharacteristic>(c.trc);
      AVDOVIDecoderConfigurationRecord r{};
      const bool made = CDVLayerMerge::RecordFromRpu(probe, par, AVRational{24000, 1001}, r);
      const bool right = probe.rpu == c.rpu && (!c.rpu || (probe.profile == c.profile && probe.layer == c.layer)) &&
                         made == (c.record != 0) &&
                         (!made || (r.dv_profile == c.record && r.dv_bl_signal_compatibility_id == c.compat &&
                                    r.rpu_present_flag && r.bl_present_flag && r.el_present_flag == (c.record == 7) &&
                                    r.dv_level == 6 && r.dv_md_compression == AV_DOVI_COMPRESSION_NONE));
      got += std::string(right ? "" : " WRONG:") + (right ? "" : c.what);
      ok &= right;
    }
    // start codes, and a packet cut anywhere: no read past it (the sanitizers watch), nothing taken from half a unit
    {
      const std::vector<uint8_t> p = RpuPacket(fel, false, true);
      std::vector<uint8_t> annexB;
      for (size_t i = 0; i + 4 <= p.size();)
      {
        const size_t n = (size_t(p[i]) << 24) | (size_t(p[i + 1]) << 16) | (size_t(p[i + 2]) << 8) | p[i + 3];
        annexB.insert(annexB.end(), {0, 0, 0, 1});
        annexB.insert(annexB.end(), p.begin() + i + 4, p.begin() + i + 4 + n);
        i += 4 + n;
      }
      dvbridge_rpu_probe probe;
      dvbridge_rpu_probe_packet(annexB.data(), annexB.size(), 0, &probe);
      ok &= probe.rpu && probe.layer && probe.profile == 7;
      got += probe.rpu && probe.layer && probe.profile == 7 ? "" : " WRONG:start codes";
      size_t cutRead = 0;
      for (size_t cut = 0; cut < p.size(); ++cut)
      {
        std::vector<uint8_t> part(p.begin(), p.begin() + cut);
        dvbridge_rpu_probe_packet(part.empty() ? nullptr : part.data(), part.size(), 4, &probe);
        cutRead += probe.rpu;
      }
      ok &= cutRead == 0;
      got += cutRead == 0 ? "" : " WRONG:a cut packet gave an RPU " + std::to_string(cutRead) + " times";
    }
    avcodec_parameters_free(&par);
    Check("the probe of an RPU and the record made of it, on packets made here (" + std::to_string(std::size(cases)) +
              " headers, start codes, a packet cut at each byte)" + (ok ? "" : ":" + got),
          ok);
  }

  // every packet of the samples
  struct Track
  {
    std::string file;
    int stream;
    const char* what;
    bool rpu;      // every packet has one, or none has
    bool layer;
    unsigned profile;
  };
  const std::string dvtest = dir;
  const Track tracks[] = {
      {dir + "/FEL_test_dual.mkv", 0, "base track of the FEL test stream (HDR10)", false, false, 0},
      {dir + "/FEL_test_dual.mkv", 1, "its enhancement track", true, false, 7},
      {dir + "/issue1_dual.mkv", 0, "base track of the file of issue 1 (HDR10)", false, false, 0},
      {dir + "/issue1_dual.mkv", 1, "its enhancement track", true, false, 7},
      {dvtest + "/FEL_test_for_AVS.m2ts", 0, "base track of the transport stream, start codes (HDR10)", false, false, 0},
      {dvtest + "/FEL_test_for_AVS.m2ts", 1, "its enhancement track", true, false, 7},
      {dvtest + "/BL_EL.mkv", 0, "the one-track file, both layers", true, true, 7},
      {dir + "/issue1_norpu.mkv", 1, "the enhancement track without its RPUs", false, false, 0},
  };
  size_t hdr10 = 0;
  for (const Track& t : tracks)
  {
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, t.file.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(fc, nullptr) < 0)
    {
      Check(t.file + " opens", false);
      avformat_close_input(&fc);
      continue;
    }
    const AVCodecParameters* par = fc->streams[t.stream]->codecpar;
    const int lengthSize = CDVLayerMerge::LengthSize(par);
    AVPacket* pkt = av_packet_alloc();
    size_t n = 0, rpu = 0, layer = 0, right = 0, records = 0;
    while (av_read_frame(fc, pkt) >= 0)
    {
      if (pkt->stream_index == t.stream && pkt->size > 0)
      {
        ++n;
        dvbridge_rpu_probe probe;
        dvbridge_rpu_probe_packet(pkt->data, static_cast<size_t>(pkt->size), lengthSize, &probe);
        rpu += probe.rpu;
        layer += probe.layer;
        right += probe.rpu && probe.profile == t.profile && probe.rpu_profile == 1 && probe.vdr_bit_depth == 12;
        AVDOVIDecoderConfigurationRecord r{};
        if (CDVLayerMerge::RecordFromRpu(probe, par, av_guess_frame_rate(fc, fc->streams[t.stream], nullptr), r))
          records += r.dv_profile == 7 && r.el_present_flag && r.dv_bl_signal_compatibility_id == 6;
      }
      av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fc);
    if (!t.rpu)
      hdr10 += n;
    // the last two access units of the test stream have no RPU (they are only base pictures: see section 1), and
    // a packet of the enhancement track of the transport stream may hold two access units
    const size_t slack = 3;
    const bool ok = n > 100 && (t.rpu ? rpu + slack >= n && right == rpu && layer == (t.layer ? rpu : 0) &&
                                            records == (t.layer ? rpu : 0)
                                      : rpu == 0 && layer == 0 && records == 0);
    Check(t.file.substr(t.file.rfind('/') + 1) + ", " + t.what + ": " + std::to_string(n) + " packets, " +
              std::to_string(rpu) + " with an RPU (" + std::to_string(right) + " of profile " +
              std::to_string(t.profile) + "), " + std::to_string(layer) + " with units of the layer; " +
              std::to_string(records) + " would make a profile 7 stream of it",
          ok);
  }
  Check("on " + std::to_string(hdr10) + " packets of HDR10 tracks without RPUs, none: nothing would make them Dolby "
        "Vision", hdr10 > 10000);

  // what the probe reads of each RPU against what FFmpeg's decoder parses of it (the one-track file: its
  // decoder takes the RPU of each packet)
  {
    AVFormatContext* fc = nullptr;
    AVCodecContext* dec = nullptr;
    size_t frames = 0, same = 0;
    std::map<int64_t, dvbridge_rpu_probe> byPts;
    if (avformat_open_input(&fc, (dvtest + "/BL_EL.mkv").c_str(), nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(fc, nullptr) >= 0)
    {
      const AVStream* st = fc->streams[0];
      const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
      dec = codec ? avcodec_alloc_context3(codec) : nullptr;
      if (dec && avcodec_parameters_to_context(dec, st->codecpar) >= 0)
      {
        dec->thread_count = 8;
        dec->pkt_timebase = st->time_base;
        if (avcodec_open2(dec, codec, nullptr) >= 0)
        {
          AVPacket* pkt = av_packet_alloc();
          AVFrame* frame = av_frame_alloc();
          const auto take = [&]() {
            while (avcodec_receive_frame(dec, frame) >= 0)
            {
              const AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
              const auto it = byPts.find(frame->pts);
              if (sd && it != byPts.end())
              {
                ++frames;
                const auto* h = av_dovi_get_header(reinterpret_cast<const AVDOVIMetadata*>(sd->data));
                const dvbridge_rpu_probe& p = it->second;
                same += p.rpu && h->vdr_rpu_profile == p.rpu_profile && h->vdr_bit_depth == p.vdr_bit_depth &&
                        h->bl_video_full_range_flag == p.bl_full_range &&
                        h->el_spatial_resampling_filter_flag == p.el_resampling &&
                        h->disable_residual_flag == p.residual_disabled;
              }
              av_frame_unref(frame);
            }
          };
          for (int n = 0; n < 96 && av_read_frame(fc, pkt) >= 0; av_packet_unref(pkt))
          {
            if (pkt->stream_index != 0)
              continue;
            ++n;
            dvbridge_rpu_probe probe;
            dvbridge_rpu_probe_packet(pkt->data, static_cast<size_t>(pkt->size), CDVLayerMerge::LengthSize(st->codecpar),
                                      &probe);
            byPts[pkt->pts] = probe;
            avcodec_send_packet(dec, pkt);
            take();
          }
          avcodec_send_packet(dec, nullptr);
          take();
          av_frame_free(&frame);
          av_packet_free(&pkt);
        }
      }
    }
    avcodec_free_context(&dec);
    avformat_close_input(&fc);
    Check("the probe reads of each RPU what FFmpeg's decoder parses of it: " + std::to_string(same) + " of " +
              std::to_string(frames) + " pictures",
          frames >= 90 && same == frames);
  }
}

// ---- 16. two HEVC tracks of Matroska that libavformat does not group (no record, the record of a MEL, or a record
// on the first track that is not one of an enhancement layer): found unverified, a pair only when their packets
// show it; else every packet as it is read.
static void Unverified(const std::string& dir, const Joins& felDeclared)
{
  using Verification = CDVLayerMerge::Verification;
  Source issueDeclared;
  const Joins issue = issueDeclared.Open((dir + "/issue1_dual.mkv").c_str()) && issueDeclared.Joined()
                          ? ReadPlaces(issueDeclared)
                          : Joins{};
  struct Case
  {
    const char* name;
    const Joins* declared; // the same tracks with their record: the same join; nullptr: no pair
    size_t atLeast;
    const char* what;
    // libavformat makes a Dolby Vision group of the two tracks the wrong way round (its
    // "enhancement" track is the larger one), which Find() leaves to this rule
    bool reversed = false;
  };
  const Case cases[] = {
      {"FEL_test_dual_norecord.mkv", &felDeclared, 2870, "no record (the FEL test stream)"},
      {"FEL_test_dual_p8el.mkv", &felDeclared, 2870, "the record of a MEL on the second track (profile 8)"},
      {"issue1_norecord.mkv", &issue, 2900, "no record (the file of issue 1)"},
      {"issue1_norpu.mkv", nullptr, 0, "no record, and the second track has no RPU"},
      {"BL_EL_p8_plus_1080.mkv", nullptr, 0,
       "a one-track Dolby Vision file (profile 8 record) and a second track without RPUs"},
      {"BL_EL_p8_plus_el.mkv", nullptr, 0,
       "a one-track Dolby Vision file (profile 8 record) and the enhancement track of another pair (RPUs, no "
       "record)"},
      // (made from FEL_test_dual.mkv: its record moved to the base track)
      {"FEL_test_dual_blrecord.mkv", &felDeclared, 2870,
       "the record on the base track alone (\"base layer present\"), which libavformat takes for the enhancement "
       "track", true},
      {"BL_EL_plus_1080.mkv", nullptr, 0,
       "a one-track profile 7 file with its record and a second HEVC track, which libavformat takes for its base "
       "layer", true},
  };
  for (const Case& c : cases)
  {
    Source s;
    const bool opened = s.Open((dir + "/" + c.name).c_str());
    unsigned groups = 0;
    for (unsigned i = 0; opened && i < s.ctx->nb_stream_groups; ++i)
    {
      const AVStreamGroup* g = s.ctx->stream_groups[i];
      if (g->type != AV_STREAM_GROUP_PARAMS_DOLBY_VISION)
        continue;
      // (a group the wrong way round, its "enhancement" track the larger one)
      const AVStream* el = KODI::STREAMGROUP::Stream(g, g->params.layered_video ? g->params.layered_video->el_index : 0);
      const AVStream* bl = KODI::STREAMGROUP::Stream(g, g->params.layered_video ? 1 - g->params.layered_video->el_index : 0);
      const bool wrongWay = KODI::STREAMGROUP::Size(g) == 2 && g->params.layered_video && el && bl &&
                            el->codecpar->width > bl->codecpar->width;
      groups += c.reversed && wrongWay ? 100 : 1;
    }
    const auto& l = s.layers;
    Check(std::string(c.name) + ", " + c.what + (c.reversed ? ": a group of libavformat the wrong way round, not "
                                                              "taken as a group; "
                                                            : ": no group of libavformat; ") +
              "found unverified (the larger track as the base), with the record of the one-track form",
          opened && groups == (c.reversed ? 100u : 0u) && s.Joined() && !l.verified &&
              l.container == CDVLayerMerge::Container::MATROSKA && l.baseLengthSize == 4 &&
              l.enhancementLengthSize == 4 && !l.decodingTimes && l.base == 0 && l.enhancement == 1 &&
              SameRecord(l.record));
    if (!opened || !s.Joined())
      continue;
    {
      CDVLayerMerge merge;
      merge.Configure(l);
      Check("  pending before its first packet", merge.GetVerification() == Verification::PENDING);
    }
    if (c.declared)
    {
      int verified = 0;
      const Joins j = ReadPlaces(s, SIZE_MAX, -1, false, nullptr, &verified);
      const Agreement a = Agree(*c.declared, j);
      Check("  its packets make it a pair, joined as the file with the record: " + Text(a) + "; at most " +
                std::to_string(j.counts.waited) + " base packets waited",
            verified == 1 && Same(a, c.atLeast) && !a.onlyA && !a.onlyB && j.counts.dropped == 0 &&
                j.counts.malformed == 0 && j.counts.waited <= 64);
      // after seeks: what the packets said stays (Configure() of the same pair keeps it), and a new one says it again
      size_t seeksVerified = 0, different = 0;
      for (const double where : {0.25, 0.75})
      {
        int v = 0;
        const Joins part = ReadPlaces(s, 100, where, false, nullptr, &v);
        seeksVerified += v == 1;
        for (const auto& [place, cc] : part.byPlace)
        {
          const auto it = c.declared->byPlace.find(place);
          different += it == c.declared->byPlace.end() ||
                       (!cc.layer.empty() && (cc.layer != it->second.layer || cc.base != it->second.base));
        }
      }
      Check("  and after seeks too (" + std::to_string(different) + " access units unlike the file with the record)",
            seeksVerified == 2 && different == 0);
      continue;
    }

    // no pair: every packet comes out as av_read_frame() gives it, each track in its order
    Source plain;
    if (!plain.Open((dir + "/" + c.name).c_str()))
    {
      Check("  opens a second time", false);
      continue;
    }
    std::map<int, std::vector<std::tuple<int64_t, int64_t, std::vector<uint8_t>>>> read, joined;
    AVPacket* pkt = av_packet_alloc();
    size_t n = 0, m = 0;
    while (av_read_frame(plain.ctx, pkt) >= 0)
    {
      read[pkt->stream_index].emplace_back(pkt->pts, pkt->dts, std::vector<uint8_t>(pkt->data, pkt->data + pkt->size));
      ++n;
      av_packet_unref(pkt);
    }
    CDVLayerMerge merge;
    merge.Configure(l);
    bool refusedEarly = false;
    while (merge.Read(s.ctx, l.base, l.enhancement, pkt) >= 0)
    {
      joined[pkt->stream_index].emplace_back(pkt->pts, pkt->dts, std::vector<uint8_t>(pkt->data, pkt->data + pkt->size));
      ++m;
      if (m == 1)
        refusedEarly = merge.GetVerification() == Verification::REFUSED;
      av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    const CDVLayerMerge::Counts& counts = merge.GetCounts();
    Check("  its packets refuse it before the first one comes out; then " + std::to_string(m) + " packets of " +
              std::to_string(n) + " come out, each track as it is read (" + std::to_string(counts.joined) +
              " joined)",
          refusedEarly && merge.GetVerification() == Verification::REFUSED && merge.Empty() && m == n &&
              read == joined && counts.joined == 0);
  }

  Probes(dir);

  // the one-track file with its own record and a second track: libavformat groups them, the wrong way round. (
  // such a group is left to the rule of the Matroska files that libavformat does not group,
  // whose packets refuse this one: a case of Unverified() above, "BL_EL_plus_1080.mkv".)

  // Find() on tracks made here, without a group
  {
    struct T
    {
      int width, height;
      bool hvcc;
      int profile = -1; // -1: no record
      bool bl = true, el = false, rpu = true;
      bool picture = false; // an attached picture
    };
    const auto find = [](const char* demuxer, std::vector<T> tracks) {
      AVFormatContext* ctx = avformat_alloc_context();
      ctx->iformat = av_find_input_format(demuxer);
      for (const T& t : tracks)
      {
        AVStream* st = avformat_new_stream(ctx, nullptr);
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = AV_CODEC_ID_HEVC;
        st->codecpar->width = t.width;
        st->codecpar->height = t.height;
        st->time_base = AVRational{1, 1000};
        st->avg_frame_rate = st->r_frame_rate = AVRational{24000, 1001};
        if (t.picture)
          st->disposition |= AV_DISPOSITION_ATTACHED_PIC;
        const std::vector<uint8_t> hvcc = {1, 0x22, 0x20, 0, 0, 0, 0x90, 0, 0, 0, 0, 0, 0x99, 0xf0, 0, 0xfc,
                                           0xfd, 0xfa, 0xfa, 0, 0, 0x0f, 0};
        const std::vector<uint8_t> annexB = {0, 0, 0, 1, 0x40, 0x01, 0x0c};
        const std::vector<uint8_t>& ed = t.hvcc ? hvcc : annexB;
        st->codecpar->extradata = static_cast<uint8_t*>(av_mallocz(ed.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        std::memcpy(st->codecpar->extradata, ed.data(), ed.size());
        st->codecpar->extradata_size = static_cast<int>(ed.size());
        if (t.profile >= 0)
        {
          AVPacketSideData* sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                         &st->codecpar->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF,
                                                         sizeof(AVDOVIDecoderConfigurationRecord), 0);
          auto* r = reinterpret_cast<AVDOVIDecoderConfigurationRecord*>(sd->data);
          std::memset(r, 0, sizeof(*r));
          r->dv_version_major = 1;
          r->dv_profile = static_cast<uint8_t>(t.profile);
          r->dv_level = 6;
          r->rpu_present_flag = t.rpu;
          r->el_present_flag = t.el;
          r->bl_present_flag = t.bl;
        }
      }
      const CDVLayerMerge::Layers l = CDVLayerMerge::Find(ctx, 0);
      avformat_free_context(ctx);
      return l;
    };
    using R = CDVLayerMerge::Layers::Result;
    const T b4k{3840, 2160, true}, e1080{1920, 1080, true}, e4k{3840, 2160, true}, e1080ab{1920, 1080, false};
    const T b4kab{3840, 2160, false}, e1080p8{1920, 1080, true, 8, true, false, true};
    const T e1080p5{1920, 1080, true, 5, true, false, true}, e1080p7norpu{1920, 1080, true, 7, false, true, false};
    const T b4kp8{3840, 2160, true, 8, true, false, true}, b4knobl{3840, 2160, true, 7, false, true, true};
    const T picture{1920, 1080, true, -1, true, false, true, true}, e0{0, 0, true};
    bool ok = true;
    std::string got;
    const auto expect = [&](const char* what, const CDVLayerMerge::Layers& l, R wanted, bool unverified, int base) {
      const bool right = l.result == wanted && (wanted != R::JOINED || (l.verified == !unverified && l.base == base));
      got += std::string(" ") + what + (right ? ":ok" : ":WRONG");
      ok &= right;
    };
    expect("two", find("matroska", {b4k, e1080}), R::JOINED, true, 0);
    expect("swapped", find("matroska", {e1080, b4k}), R::JOINED, true, 1);
    expect("p8-el", find("matroska", {b4k, e1080p8}), R::JOINED, true, 0);
    expect("p8-base", find("matroska", {b4kp8, e1080}), R::JOINED, true, 0);
    expect("base-no-bl", find("matroska", {b4knobl, e1080}), R::NONE, false, -1);
    expect("p5-el", find("matroska", {b4k, e1080p5}), R::NONE, false, -1);
    expect("el-no-rpu", find("matroska", {b4k, e1080p7norpu}), R::NONE, false, -1);
    expect("same-size", find("matroska", {b4k, e4k}), R::NONE, false, -1);
    expect("unknown-size", find("matroska", {b4k, e0}), R::NONE, false, -1);
    expect("start-codes", find("matroska", {b4k, e1080ab}), R::NONE, false, -1);
    expect("start-codes-base", find("matroska", {b4kab, e1080}), R::NONE, false, -1);
    expect("three", find("matroska", {b4k, e1080, e1080}), R::NONE, false, -1);
    expect("picture", find("matroska", {b4k, e1080, picture}), R::JOINED, true, 0);
    expect("mp4", find("mp4", {b4k, e1080}), R::NONE, false, -1);
    expect("ts", find("mpegts", {b4kab, e1080ab}), R::NONE, false, -1);
    Check("Find() on Matroska tracks made here, without a group: two HEVC tracks of the sizes of a pair, framed with "
          "lengths, with no record that speaks against it, found unverified; anything else, and MP4 or a transport "
          "stream, nothing (" + got + " )",
          ok);
  }
}
#endif

static void Containers(const std::string& dir, const char* transportStream)
{
  using Result = CDVLayerMerge::Layers::Result;
  using Container = CDVLayerMerge::Container;
  const std::string ts = transportStream;
  const std::string mkv = dir + "/FEL_test_dual.mkv", mp4 = dir + "/FEL_test_dual.mp4";
  const std::string mkvBl = dir + "/FEL_test_dual_bl1.mkv", dvh1 = dir + "/FEL_test_dual_dvh1.mp4";
  const std::string issue = dir + "/issue1-sample.mp4", issueMkv = dir + "/issue1_dual.mkv",
                    issueTs = dir + "/issue1_disc.m2ts";

  // ---- 8. what is found
  struct Expect
  {
    std::string path;
    Container container;
    int lengthSize;
    bool decodingTimes;
    int level;
  };
  const Expect expect[] = {
      {mkv, Container::MATROSKA, 4, false, 6},     {mp4, Container::MP4, 4, true, 6},
      {mkvBl, Container::MATROSKA, 4, false, 6},   {dvh1, Container::MP4, 4, true, 6},
      {issue, Container::MP4, 4, true, 6},         {issueMkv, Container::MATROSKA, 4, false, 6},
      {issueTs, Container::TRANSPORT_STREAM, 0, true, 6},
      {dir + "/FEL_test_188.ts", Container::TRANSPORT_STREAM, 0, true, 6},
      {dir + "/FEL_test_as_disc_188.ts", Container::TRANSPORT_STREAM, 0, true, 6},
  };
  for (const Expect& e : expect)
  {
    Source s;
    const bool opened = s.Open(e.path.c_str());
    const auto& l = s.layers;
    Check(e.path.substr(e.path.rfind('/') + 1) + ": its two tracks are found and joined (" +
              std::to_string(l.baseLengthSize) + " and " + std::to_string(l.enhancementLengthSize) +
              " bytes of length, " + (l.decodingTimes ? "decoding times" : "times made from presentation times") +
              "), with the record of the one-track form",
          opened && s.Joined() && l.container == e.container && l.baseLengthSize == e.lengthSize &&
              l.enhancementLengthSize == e.lengthSize && l.decodingTimes == e.decodingTimes && l.base >= 0 &&
              l.enhancement >= 0 && l.base != l.enhancement && l.record.dv_profile == 7 &&
              l.record.dv_level == e.level && l.record.bl_present_flag && l.record.el_present_flag &&
              l.record.rpu_present_flag && l.record.dv_bl_signal_compatibility_id == 6 &&
              l.record.dv_md_compression == AV_DOVI_COMPRESSION_NONE);
    if (opened && s.Joined() && e.container != Container::TRANSPORT_STREAM)
    {
      const AVCodecParameters* el = s.ctx->streams[l.enhancement]->codecpar;
      Check("  and the hvcC record of its enhancement track is handed on to the decoder of the layer",
            CDVLayerMerge::IsConfigurationRecord(el->extradata, el->extradata_size) &&
                CDVLayerMerge::LengthSize(el) == e.lengthSize);
    }
  }
  {
    Source s;
    Check("a one-track Matroska file has no pair", s.Open((dir + "/BL_EL.mkv").c_str()) &&
                                                      s.layers.result == Result::NONE);
  }
  // The same two tracks where nothing says that they are Dolby Vision: nothing is joined, the base layer plays
  // as it did (HDR10). A transport stream with the stream numbers of a disc but no Blu-ray program (no HDMV
  // registration) and no descriptor, one with other stream numbers, Matroska without the record of the
  // enhancement track, MP4 with the record but no 'vdep' reference. (Matroska without the record is
  // found unverified: section 16.)
  std::vector<std::string> undeclared = {"issue1_dual.ts", "issue1_otherpids.ts", "FEL_test_novdep.mp4"};
#ifdef DVMERGE_NO_PROBE
  undeclared.push_back("issue1_norecord.mkv");
#endif
  for (const std::string& name : undeclared)
  {
    Source s;
    const bool opened = s.Open((dir + "/" + name).c_str());
    unsigned hevc = 0;
    for (unsigned i = 0; opened && i < s.ctx->nb_streams; ++i)
      hevc += s.ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC;
    Check(name + ": two HEVC tracks that nothing declares as Dolby Vision are not joined", opened && hevc == 2 &&
                                                                                              s.layers.result == Result::NONE);
  }

  // ---- 9. the same stream in three containers gives the same joined access units
  Source t, k, m;
  if (!t.Open(ts.c_str()) || !k.Open(mkv.c_str()) || !m.Open(mp4.c_str()) || !t.Joined() || !k.Joined() ||
      !m.Joined())
  {
    Check("the three files of the FEL test stream open and are joined", false);
    return;
  }
  const Joins inTs = ReadPlaces(t), inMkv = ReadPlaces(k), inMp4 = ReadPlaces(m);
  const auto counts = [](const char* name, const Joins& j) {
    std::printf("      %s: %zu base packets, %llu joined, %llu alone, %llu enhancement packets dropped, %llu "
                "malformed; at most %zu base and %zu enhancement packets waited; %zu without a time, %zu at the "
                "place of an earlier one, %zu not read as units\n",
                name, j.packets, (unsigned long long)j.counts.joined, (unsigned long long)j.counts.alone,
                (unsigned long long)j.counts.dropped, (unsigned long long)j.counts.malformed, j.counts.waited,
                j.counts.queued, j.untimed, j.repeated, j.unreadable);
  };
  counts("transport stream", inTs);
  counts("Matroska", inMkv);
  counts("MP4", inMp4);
  Agreement a = Agree(inTs, inMkv);
  Check("Matroska against the transport stream: " + Text(a), Same(a, 2870) && inMkv.unreadable == 0 && !inMkv.repeated);
  Check("  the access units of the transport stream that Matroska lacks are its last ones (the file ends two "
        "earlier: Matroska cannot carry the last two, which have no time)",
        a.onlyB == 0 && a.onlyA <= 3 && a.firstOnly > a.lastBoth);
  // the last enhancement unit of the transport stream (see Agree()): its base unit n-3 and those after it
  const long long quirk = inTs.byPlace.empty() ? LLONG_MAX : inTs.byPlace.rbegin()->first - 1;
  a = Agree(inTs, inMp4, quirk);
  // (FFmpeg's muxer gave the last base packet, which has no time in the transport stream, the time of an earlier
  // one at the end of the file: that one is left out too)
  Check("MP4 against the transport stream: " + Text(a),
        Same(a, 2870) && a.tail == 2 && !a.onlyA && !a.onlyB && inMp4.unreadable == 0 &&
            (!inMp4.repeated || inMp4.firstRepeated >= quirk));
  Check("  Matroska and MP4 join every enhancement packet they have: none dropped, none malformed",
        inMkv.counts.dropped == 0 && inMkv.counts.malformed == 0 && inMp4.counts.dropped == 0 &&
            inMp4.counts.malformed == 0);

  // the transport stream with 188-byte packets (the same packets without the four bytes of a BDAV stream),
  // with its descriptor and signalled as a disc: the same join, to the last unit
  for (const char* name : {"FEL_test_188.ts", "FEL_test_as_disc_188.ts"})
  {
    Source s;
    if (s.Open((dir + "/" + name).c_str()) && s.Joined())
    {
      a = Agree(inTs, ReadPlaces(s));
      Check(std::string(name) + ": the same join as the BDAV stream: " + Text(a),
            Same(a, 2870) && !a.onlyA && !a.onlyB);
    }
    else
      Check(std::string(name) + " opens and is joined", false);
  }

  // the enhancement track of a Matroska file in use says that it has a base layer
  {
    Source s;
    if (s.Open(mkvBl.c_str()) && s.Joined())
    {
      a = Agree(inMkv, ReadPlaces(s));
      Check("Matroska whose enhancement record says it has a base layer: the same join (" + Text(a) + ")",
            Same(a, 2870) && !a.onlyA && !a.onlyB);
    }
    else
      Check("the Matroska file whose enhancement record says it has a base layer opens and is joined", false);
  }
  // 'hvc1' and 'dvh1': the parameter sets of both layers only in the records of their tracks (FFmpeg's muxer
  // takes them out of the samples of an 'hvc1' track)
  {
    Source s, s2, t2;
    if (s.Open(dvh1.c_str()) && s.Joined() && s2.Open(dvh1.c_str()) && t2.Open(ts.c_str()))
    {
      const Joins inDvh1 = ReadPlaces(s);
      size_t parameterSets = 0;
      for (const auto& [place, c] : inDvh1.byPlace)
        for (const auto& u : c.layer)
          parameterSets += Type(u) >= 32 && Type(u) <= 34;
      a = Agree(ReadPlaces(t2, SIZE_MAX, -1, true), ReadPlaces(s2, SIZE_MAX, -1, true), quirk);
      Check("'dvh1' MP4: no parameter set of the layer in its stream (" + std::to_string(parameterSets) +
                "); but for them, the same join as the transport stream: " + Text(a),
            parameterSets == 0 && Same(a, 2870) && inDvh1.counts.joined == inMp4.counts.joined);
    }
    else
      Check("the 'dvh1' MP4 opens and is joined", false);
  }

  // ---- 10. after seeks, a joined packet is the one of the whole read, and every base packet has the
  // enhancement access unit and the RPU it has in the whole read: in Matroska, whose packets have no decoding
  // times, those of the enhancement track before its first random-access packet too (CDVLayerMerge::Backfill()).
  // A base packet whose enhancement access unit lies before the place the seek reads from cannot have it: in
  // FEL_test_dual_bl1.mkv (the same packets, muxed again) the first enhancement access unit of each seek point
  // is in the cluster before it, and so the first base packet there goes out without its layer.
  Source kb;
  const Joins inMkvBl = kb.Open(mkvBl.c_str()) && kb.Joined() ? ReadPlaces(kb) : Joins{};
  for (const auto& [name, source, whole, lost] :
       {std::tuple<const char*, Source*, const Joins*, size_t>{"Matroska", &k, &inMkv, 0},
        std::tuple<const char*, Source*, const Joins*, size_t>{"MP4", &m, &inMp4, 0},
        std::tuple<const char*, Source*, const Joins*, size_t>{"Matroska muxed again", &kb, &inMkvBl, 1}})
  {
    if (!source->Joined())
    {
      Check(std::string(name) + " opens and is joined", false);
      continue;
    }
    size_t total = 0, layered = 0, different = 0, missing = 0, unlayered = 0, notFirst = 0;
    std::string each;
    for (const double where : {0.1, 0.25, 0.5, 0.75, 0.9})
    {
      std::vector<long long> order;
      const Joins part = ReadPlaces(*source, 300, where, false, &order);
      size_t alone = 0;
      for (const auto& [place, c] : part.byPlace)
      {
        ++total;
        layered += !c.layer.empty();
        const auto it = whole->byPlace.find(place);
        if (it == whole->byPlace.end())
          ++missing;
        else if (!c.layer.empty() && (c.layer != it->second.layer || c.base != it->second.base ||
                                      c.layerPlace != it->second.layerPlace))
          ++different;
        else if (c.layer.empty() && !it->second.layer.empty())
        {
          ++alone;
          // only the first base packet read, a random-access one
          notFirst += order.empty() || order.front() != place || !c.key;
        }
      }
      unlayered += alone > lost;
      each += " " + std::to_string(alone);
    }
    Check(std::string(name) + " after five seeks: " + std::to_string(total) + " access units, " +
              std::to_string(layered) + " with their layer, " + std::to_string(different) +
              " of them unlike the whole read, " + std::to_string(missing) + " not in it; without the layer they " +
              "have in the whole read, at each seek:" + each + " (at most " + std::to_string(lost) + ")",
          total >= 1400 && different == 0 && missing == 0 && unlayered == 0 && notFirst == 0);
  }

  // ---- 11. the file of issue 1 in its three containers
  {
    Source a1, a2, a3;
    if (a1.Open(issue.c_str()) && a2.Open(issueMkv.c_str()) && a3.Open(issueTs.c_str()) && a1.Joined() &&
        a2.Joined() && a3.Joined())
    {
      const Joins j1 = ReadPlaces(a1), j2 = ReadPlaces(a2), j3 = ReadPlaces(a3);
      counts("issue 1, MP4", j1);
      counts("issue 1, Matroska", j2);
      counts("issue 1, transport stream", j3);
      Agreement x = Agree(j1, j2);
      Check("issue 1, Matroska against MP4: " + Text(x), Same(x, 2900) && !x.onlyA && !x.onlyB);
      x = Agree(j1, j3);
      Check("issue 1, transport stream against MP4: " + Text(x), Same(x, 2900) && !x.onlyA && !x.onlyB);
      Check("issue 1: every base packet joined, nothing dropped or malformed",
            j1.counts.joined == j1.packets && !j1.counts.dropped && !j1.counts.malformed && !j1.counts.alone);
    }
    else
      Check("the three files of issue 1 open and are joined", false);
  }

#ifndef DVMERGE_NO_PROBE
  Unverified(dir, inMkv);
#endif
}

int main(int argc, char** argv)
{
  if (argc != 4 && argc != 5)
    return 2;
  av_log_set_level(AV_LOG_ERROR);
  using Result = CDVLayerMerge::Layers::Result;

  // ---- 1. the whole file against the one-track file
  Source two;
  Check("the two-track file opens and its pair is found by the Dolby Vision descriptor",
        two.Open(argv[1]) && two.Joined() && two.layers.described && two.layers.base >= 0 &&
            two.layers.enhancement >= 0 && two.layers.base != two.layers.enhancement);
  Check("the record of the joined stream: 1.0, profile 7, level 6, RPU, both layers, HDR10 base, no compression",
        SameRecord(two.layers.record));
  Check("its frame rate is 24000/1001", two.layers.rate.num == 24000 && two.layers.rate.den == 1001);
  {
    const CDVLayerMerge::Layers any = CDVLayerMerge::Find(two.ctx, UINT_MAX);
    const CDVLayerMerge::Layers other = CDVLayerMerge::Find(two.ctx, 0);
    Check("found the same when all streams play, and again",
          any.result == Result::JOINED && any.base == two.layers.base && any.enhancement == two.layers.enhancement &&
              other.base == two.layers.base && SameRecord(any.record));
    const AVPacketSideData* sd = av_packet_side_data_get(two.ctx->streams[two.layers.enhancement]->codecpar->coded_side_data,
                                                         two.ctx->streams[two.layers.enhancement]->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DOVI_CONF);
    const auto* record = sd ? reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(sd->data) : nullptr;
    Check("the record of the enhancement track itself is left as it was (no base layer)",
          record && record->dv_profile == 7 && !record->bl_present_flag && record->el_present_flag);
  }

  CDVLayerMerge merge;
  merge.SetRate(two.layers.rate);
  int last = 0;
  const std::vector<Joined> whole = ReadJoined(two, merge, SIZE_MAX, &last);
  Check("the read ends with the end of the file", last == AVERROR_EOF);
  {
    AVPacket* pkt = av_packet_alloc();
    const int again = merge.Read(two.ctx, two.layers.base, two.layers.enhancement, pkt);
    const int third = merge.Read(two.ctx, two.layers.base, two.layers.enhancement, pkt);
    Check("and reports the end again when read again", again == AVERROR_EOF && third == AVERROR_EOF);
    av_packet_free(&pkt);
  }
  Compare("descriptor", whole, merge.GetCounts(), argv[2]);

  // ---- 6. what each joined packet says of the presentation time of its enhancement access unit, against the
  // enhancement track read on its own
  {
    Source raw;
    const bool opened = raw.Open(argv[1]);
    std::map<int64_t, int64_t> layerPts; // of the access units of the enhancement track, by decoding time
    std::set<int64_t> basePts;
    AVPacket* pkt = av_packet_alloc();
    while (opened && av_read_frame(raw.ctx, pkt) >= 0)
    {
      if (pkt->stream_index == raw.layers.enhancement && pkt->dts != AV_NOPTS_VALUE)
        layerPts[pkt->dts] = pkt->pts;
      else if (pkt->stream_index == raw.layers.base && pkt->pts != AV_NOPTS_VALUE)
        basePts.insert(pkt->pts);
      av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    const AVRational baseTimeBase = two.ctx->streams[two.layers.base]->time_base;
    const AVRational layerTimeBase = two.ctx->streams[two.layers.enhancement]->time_base;
    size_t withLayer = 0, exact = 0, rightlyNone = 0, unchecked = 0, timedWithoutLayer = 0, apart = 0, noBase = 0;
    int64_t most = 0;
    std::set<int64_t> targets;
    for (const auto& j : whole)
    {
      std::vector<Unit> units;
      UnitsAnnexB(j.data.data(), j.data.size(), units);
      bool layer = false;
      for (const auto& u : units)
        layer |= Type(u) == 63;
      if (!layer)
      {
        timedWithoutLayer += j.timed;
        continue;
      }
      ++withLayer;
      const auto it = j.dts == AV_NOPTS_VALUE ? layerPts.end() : layerPts.find(j.dts);
      if (it == layerPts.end())
      {
        ++unchecked; // a unit without a decoding time of its own, joined by the time that follows for it
        continue;
      }
      if (it->second == AV_NOPTS_VALUE || j.pts == AV_NOPTS_VALUE)
      {
        rightlyNone += !j.timed;
        continue;
      }
      const int64_t expected = av_rescale_q(it->second, layerTimeBase, baseTimeBase) - j.pts;
      if (!j.timed || j.difference != expected || av_cmp_q(j.timeBase, baseTimeBase) != 0)
        continue;
      ++exact;
      apart += expected != 0;
      most = std::max<int64_t>(most, expected < 0 ? -expected : expected);
      noBase += !basePts.count(j.pts + j.difference);
      targets.insert(j.pts + j.difference);
    }
    std::printf("      %zu joined packets: %zu say the presentation time of their enhancement access unit as the "
                "enhancement track has it, %zu rightly say none, %zu not looked at (no decoding time of their own); "
                "%zu of those times are not the packet's own, by up to %lld units of 1/%d s\n",
                withLayer, exact, rightlyNone, unchecked, apart, static_cast<long long>(most), baseTimeBase.den);
    Check("every joined packet says the presentation time of its enhancement access unit, as the enhancement track "
          "itself has it; a packet without enhancement units says none",
          opened && withLayer > 2000 && exact + rightlyNone + unchecked == withLayer && unchecked <= 2 &&
              timedWithoutLayer == 0);
    Check("each of those times is the presentation time of a base picture, and no two are the same",
          noBase == 0 && targets.size() == exact);
    Check("in this stream most enhancement access units are presented at another time than the base picture of their "
          "packet",
          apart > withLayer / 2);
  }

  // ---- 2. the file signalled as the discs are
  {
    Source disc;
    Check("the file signalled as a disc opens: no Dolby Vision descriptor, the pair is found by its stream numbers",
          disc.Open(argv[3]) && disc.Joined() && !disc.layers.described && disc.layers.base == two.layers.base &&
              disc.layers.enhancement == two.layers.enhancement);
    const AVCodecParameters* e = disc.ctx->streams[disc.layers.enhancement]->codecpar;
    Check("its enhancement track has no Dolby Vision record",
          !av_packet_side_data_get(e->coded_side_data, e->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF));
    Check("the record made for it is the same", SameRecord(disc.layers.record));
    CDVLayerMerge discMerge;
    discMerge.SetRate(disc.layers.rate);
    const std::vector<Joined> discWhole = ReadJoined(disc, discMerge, SIZE_MAX);
    Compare("disc", discWhole, discMerge.GetCounts(), argv[2]);
  }

  // ---- 3. after seeks: every packet that comes out is a base packet in order, joined to the enhancement
  // packet of its own decoding time
  {
    bool ok = true;
    size_t total = 0, layered = 0;
    for (const double where : {0.25, 0.5, 0.9, 0.1})
    {
      const int64_t target = two.ctx->start_time + static_cast<int64_t>(where * static_cast<double>(two.ctx->duration));
      CDVLayerMerge after;
      ok &= av_seek_frame(two.ctx, -1, target, AVSEEK_FLAG_BACKWARD) >= 0;
      const std::vector<Joined> part = ReadJoined(two, after, 200);
      ok &= part.size() == 200;
      int64_t before = INT64_MIN;
      for (const auto& j : part)
      {
        ok &= j.dts > before;
        before = j.dts;
        // the same packet in the full read
        for (const auto& full : whole)
          if (full.dts == j.dts)
          {
            std::vector<Unit> x, y;
            UnitsAnnexB(full.data.data(), full.data.size(), x);
            UnitsAnnexB(j.data.data(), j.data.size(), y);
            bool layer = false;
            for (const auto& u : y)
              layer |= Type(u) == 63;
            layered += layer;
            // after a seek the first base packets may come without their layer; one that has it has the
            // same units as in the full read
            ok &= !layer || x == y;
            break;
          }
        ++total;
      }
    }
    Check("after four seeks: " + std::to_string(total) + " packets in order, " + std::to_string(layered) +
              " with their layer, each identical to the full read", ok && layered > total * 9 / 10);
  }

  // ---- 4. missing, late and reordered enhancement packets
  {
    const std::vector<uint8_t> bl = {0, 0, 0, 1, 0x02, 0x01, 0xaa};
    const std::vector<uint8_t> el = {0, 0, 0, 1, 0x02, 0x01, 0xbb, 0, 0, 1, 0x7c, 0x01, 0x19, 0x08};
    const int blSize = static_cast<int>(bl.size());
    CDVLayerMerge m;
    AVPacket* out = av_packet_alloc();
    AVPacket* p;
    p = Packet(10, bl); m.PushBase(p); av_packet_free(&p);
    Check("a base packet waits for its enhancement packet", !m.Pop(out, false));
    p = Packet(10, el); m.PushEnhancement(p); av_packet_free(&p);
    const bool popped = m.Pop(out, false);
    const std::vector<uint8_t> expect = {0, 0, 0, 1, 0x02, 0x01, 0xaa, 0, 0, 0, 1, 0x7e, 0x01, 0x02, 0x01, 0xbb, 0, 0, 0, 1, 0x7c, 0x01, 0x19, 0x08};
    const int both = static_cast<int>(expect.size());
    Check("joined: the enhancement unit wrapped in type 63, the RPU as it is",
          popped && out->dts == 10 && std::vector<uint8_t>(out->data, out->data + out->size) == expect);
    av_packet_unref(out);

    p = Packet(20, bl); m.PushBase(p); av_packet_free(&p);
    p = Packet(30, bl); m.PushBase(p); av_packet_free(&p);
    p = Packet(30, el); m.PushEnhancement(p); av_packet_free(&p);
    bool ok = m.Pop(out, false) && out->dts == 20 && out->size == blSize;
    av_packet_unref(out);
    ok &= m.Pop(out, false) && out->dts == 30 && out->size == both;
    av_packet_unref(out);
    Check("a base packet whose enhancement packet never came goes out alone once the layer is past it", ok);

    p = Packet(40, el); m.PushEnhancement(p); av_packet_free(&p);
    p = Packet(50, el); m.PushEnhancement(p); av_packet_free(&p);
    p = Packet(50, bl); m.PushBase(p); av_packet_free(&p);
    ok = m.Pop(out, false) && out->dts == 50 && out->size == both && m.GetCounts().dropped == 1;
    av_packet_unref(out);
    Check("an enhancement packet without a base packet is dropped, the next one still pairs", ok);

    // the enhancement layer goes silent: base packets wait for SILENT of them, then all go out, and the ones
    // that follow do not wait
    const int silent = static_cast<int>(CDVLayerMerge::SILENT);
    for (int i = 0; i < silent - 2; ++i) // one base packet (50) has come since the last enhancement packet
    {
      p = Packet(100 + i, bl); m.PushBase(p); av_packet_free(&p);
    }
    ok = !m.Pop(out, false);
    p = Packet(100 + silent - 2, bl); m.PushBase(p); av_packet_free(&p);
    int alone = 0;
    while (m.Pop(out, false))
    {
      ok &= out->dts == 100 + alone && out->size == blSize;
      ++alone;
      av_packet_unref(out);
    }
    p = Packet(400, bl); m.PushBase(p); av_packet_free(&p);
    ok &= m.Pop(out, false) && out->dts == 400;
    av_packet_unref(out);
    Check("a silent enhancement layer: base packets wait for " + std::to_string(silent) + " of them, then none waits (" +
              std::to_string(alone) + " went out)", ok && alone == silent - 1);

    // it comes back: base packets wait for their own again
    p = Packet(500, el); m.PushEnhancement(p); av_packet_free(&p);
    p = Packet(500, bl); m.PushBase(p); av_packet_free(&p);
    p = Packet(510, bl); m.PushBase(p); av_packet_free(&p);
    ok = m.Pop(out, false) && out->dts == 500 && out->size == both;
    av_packet_unref(out);
    ok &= !m.Pop(out, false);
    int rest = 0;
    while (m.Pop(out, true))
    {
      ++rest;
      av_packet_unref(out);
    }
    Check("when it is back they wait again, and at the end what waits goes out (" + std::to_string(rest) + ")", ok && rest == 1);

    for (int i = 0; i < 300; ++i)
    {
      p = Packet(1000 + i, el); m.PushEnhancement(p); av_packet_free(&p);
    }
    p = Packet(1299, bl); m.PushBase(p); av_packet_free(&p);
    ok = m.Pop(out, false) && out->size == both;
    av_packet_unref(out);
    Check("the enhancement queue is bounded and keeps the newest packets", ok);

    // a multiplexer that puts the enhancement packet far behind its base packet, or far ahead of it. Behind by
    // more than the silence bound: the first base packets (the start, a seek) go out alone, then all are joined.
    for (const int lag : {50, -200, 100, 200})
    {
      CDVLayerMerge far;
      far.SetSilent(lag == 200 ? 220 : CDVLayerMerge::SILENT);
      bool good = true;
      int paired = 0, single = 0;
      const int total = 1000;
      const int lost = lag == 100 ? 100 : 0;
      for (int i = 0; i < total + std::abs(lag); ++i)
      {
        const int b = lag > 0 ? i : i + lag; // the base packet that comes now
        const int e = lag > 0 ? i - lag : i; // the enhancement packet that comes now
        if (e >= 0 && e < total)
        {
          p = Packet(e * 3754, el); far.PushEnhancement(p); av_packet_free(&p);
        }
        if (b >= 0 && b < total)
        {
          p = Packet(b * 3754, bl); far.PushBase(p); av_packet_free(&p);
        }
        while (far.Pop(out, false))
        {
          good &= out->dts == (paired + single) * 3754;
          if (out->size == both)
            ++paired;
          else
          {
            good &= paired == 0; // the ones that go out alone are the first ones
            ++single;
          }
          av_packet_unref(out);
        }
      }
      Check("enhancement packets " + std::to_string(std::abs(lag)) + (lag > 0 ? " behind" : " ahead of") +
                " their base packets, silence bound " + std::to_string(lag == 200 ? 220 : CDVLayerMerge::SILENT) + ": " +
                std::to_string(paired) + " joined and " + std::to_string(single) + " alone, in order (" +
                std::to_string(far.GetCounts().waited) + " base and " + std::to_string(far.GetCounts().queued) +
                " enhancement packets waited at most)",
            good && paired == total - lost && single == lost && far.GetCounts().dropped == static_cast<uint64_t>(lost));
    }

    // farther apart than the bounds: nothing is joined, nothing grows
    {
      CDVLayerMerge far;
      const int lag = static_cast<int>(CDVLayerMerge::MAX_WAITING) + 60;
      int single = 0, paired = 0;
      for (int i = 0; i < 2000; ++i)
      {
        if (i >= lag)
        {
          p = Packet((i - lag) * 3754, el); far.PushEnhancement(p); av_packet_free(&p);
        }
        p = Packet(i * 3754, bl); far.PushBase(p); av_packet_free(&p);
        while (far.Pop(out, false))
        {
          if (out->size == both)
            ++paired;
          else
            ++single;
          av_packet_unref(out);
        }
      }
      Check("enhancement packets " + std::to_string(lag) + " behind (more than the bound): base packets go out alone (" +
                std::to_string(single) + "), none joined, " + std::to_string(far.GetCounts().dropped) + " enhancement packets dropped",
            paired == 0 && single > 2000 - static_cast<int>(CDVLayerMerge::MAX_WAITING) - 2 && far.GetCounts().dropped > 1000);
    }

    // Access units without a time of their own (not the first one of their PES packet): their time follows from
    // the unit before them in the track, that time plus that duration.
    {
      const int64_t none = AV_NOPTS_VALUE;
      // how many base packets come out joined, and how many alone, when the packets come in this order
      // ('b'/'e', time, duration); every Pop that succeeds is taken at once
      struct In
      {
        char track;
        int64_t time, duration;
      };
      const auto run = [&](const std::vector<In>& in, std::string& order) {
        CDVLayerMerge u;
        int paired = 0;
        order.clear();
        for (size_t i = 0; i <= in.size(); ++i)
        {
          if (i < in.size())
          {
            p = Packet(in[i].time, in[i].track == 'b' ? bl : el, in[i].duration);
            if (in[i].track == 'b')
              u.PushBase(p);
            else
              u.PushEnhancement(p);
            av_packet_free(&p);
          }
          while (u.Pop(out, i == in.size()))
          {
            paired += out->size == both;
            order += out->size == both ? 'J' : 'a';
            av_packet_unref(out);
          }
        }
        return paired;
      };
      std::string order;

      // neither track gives the second unit a time: both follow from the first one
      int paired = run({{'b', 10, 10}, {'e', 10, 10}, {'b', none, 10}, {'e', none, 10}, {'b', 30, 10}, {'e', 30, 10}}, order);
      Check("units without a time in both tracks pair by the time that follows from the units before them (" + order + ")",
            paired == 3 && order == "JJJ");

      // one track has a time on every unit, the other on every second one
      paired = run({{'b', 100, 10}, {'e', 100, 10}, {'b', 110, 10}, {'e', none, 10}, {'b', 120, 10}, {'e', 120, 10},
                    {'b', 130, 10}, {'e', none, 10}}, order);
      Check("a unit without a time pairs with a unit that has that time (" + order + ")", paired == 4 && order == "JJJJ");
      paired = run({{'b', 100, 10}, {'e', 100, 10}, {'b', none, 10}, {'e', 110, 10}, {'b', 120, 10}, {'e', 120, 10}}, order);
      Check("and the other way round (" + order + ")", paired == 3 && order == "JJJ");

      // the duration is a rounded number: 3753 where the next time of its own is 3754 later
      paired = run({{'b', 1000, 3753}, {'e', 1000, 3753}, {'b', 4754, 3753}, {'e', none, 3753}}, order);
      Check("a time that follows is taken within half a duration (" + order + ")", paired == 2 && order == "JJ");

      // the base unit without a time has no enhancement unit: the layer goes on with the unit after it
      paired = run({{'b', 40, 10}, {'e', 40, 10}, {'b', none, 10}, {'b', 60, 10}, {'e', 60, 10}}, order);
      Check("a base unit without a time and without a partner goes out alone, the next one pairs (" + order + ")",
            paired == 2 && order == "JaJ");
      // ... and it waits for it until then
      {
        CDVLayerMerge u;
        p = Packet(40, bl, 10); u.PushBase(p); av_packet_free(&p);
        p = Packet(40, el, 10); u.PushEnhancement(p); av_packet_free(&p);
        p = Packet(none, bl, 10); u.PushBase(p); av_packet_free(&p);
        bool good = u.Pop(out, false) && out->dts == 40 && out->size == both;
        av_packet_unref(out);
        good &= !u.Pop(out, false); // its time, 50, is known: its enhancement unit may still come
        p = Packet(none, el, 10); u.PushEnhancement(p); av_packet_free(&p);
        good &= u.Pop(out, false) && out->dts == none && out->size == both;
        av_packet_unref(out);
        Check("it waits for its enhancement unit like any other", good);
      }

      // without a duration no time follows: such a unit pairs with nothing and holds nothing back
      paired = run({{'b', 200, 0}, {'e', 200, 0}, {'b', none, 0}, {'e', none, 0}, {'b', 220, 0}, {'e', 220, 0}}, order);
      Check("a unit whose time cannot be known goes out alone at once (" + order + ")", paired == 2 && order == "JaJ");

      // the first unit after a seek has no unit before it
      paired = run({{'b', none, 10}, {'e', none, 10}, {'b', 300, 10}, {'e', 300, 10}}, order);
      Check("nor does a time follow for the first unit read (" + order + ")", paired == 1 && order == "aJ");

      // a time that goes back (a new clip) while a base packet has no partner
      paired = run({{'b', 9000, 10}, {'b', 100, 10}, {'e', 100, 10}}, order);
      Check("a base unit without a partner goes out when the layer has the unit of a later one, also when the times "
            "go back (" + order + ")", paired == 1 && order == "aJ");
    }

    // a stream that says it has the layer and has no packet of it: the wait is paid once, not after every seek
    {
      CDVLayerMerge absent;
      int waited = 0, gone = 0;
      for (int i = 0; i < 200; ++i)
      {
        p = Packet(i * 10, bl, 10); absent.PushBase(p); av_packet_free(&p);
        if (!gone)
          ++waited;
        while (absent.Pop(out, false))
        {
          ++gone;
          av_packet_unref(out);
        }
      }
      absent.Clear(); // a seek
      p = Packet(5000, bl, 10); absent.PushBase(p); av_packet_free(&p);
      const bool atOnce = absent.Pop(out, false);
      av_packet_unref(out);
      // the layer shows up after all: base packets wait for their own again, also after the next seek
      p = Packet(5010, el, 10); absent.PushEnhancement(p); av_packet_free(&p);
      absent.Clear();
      p = Packet(6000, bl, 10); absent.PushBase(p); av_packet_free(&p);
      const bool waitsAgain = !absent.Pop(out, false);
      Check("a layer that never had a packet: " + std::to_string(waited) + " base packets waited once, none after a seek; "
            "when a packet of it comes, they wait again",
            waited == static_cast<int>(CDVLayerMerge::SILENT) && gone == 200 && atOnce && waitsAgain);
    }

    // the silence bound from the frame rate: two and a half seconds, within its limits
    {
      const auto waits = [&](AVRational rate) {
        CDVLayerMerge r;
        r.SetRate(rate);
        int n = 0;
        do
        {
          p = Packet(n, bl); r.PushBase(p); av_packet_free(&p);
          ++n;
        } while (!r.Pop(out, false) && n < 1000);
        av_packet_unref(out);
        return n;
      };
      const int a = waits({24000, 1001}), b = waits({60000, 1001}), c = waits({0, 1}), d = waits({1, 1}), e = waits({240, 1});
      Check("base packets that wait for a silent layer: " + std::to_string(a) + " at 23.976, " + std::to_string(b) +
                " at 59.94, " + std::to_string(c) + " at an unknown rate, " + std::to_string(d) + " at 1, " +
                std::to_string(e) + " at 240",
            a == 60 && b == 150 && c == 60 && d == 60 && e == static_cast<int>(CDVLayerMerge::MAX_WAITING) - 1);
    }
    // ---- 6. what a joined packet says of the presentation time of its enhancement access unit
    {
      const AVRational ninety{1, 90000};
      // a base packet and an enhancement packet of these presentation times, joined: the value of the time,
      // and the number of entries of the strings metadata
      const auto join = [&](CDVLayerMerge& which, int64_t basePts, int64_t layerPts, std::string& value, int& entries,
                            bool metadata = false) {
        AVPacket* b = Packet(500, bl);
        b->pts = basePts;
        if (metadata)
        {
          AVDictionary* d = nullptr;
          av_dict_set(&d, "other", "kept", 0);
          size_t n = 0;
          uint8_t* packed = av_packet_pack_dictionary(d, &n);
          av_packet_add_side_data(b, AV_PKT_DATA_STRINGS_METADATA, packed, n);
          av_dict_free(&d);
        }
        AVPacket* e = Packet(500, el);
        e->pts = layerPts;
        which.PushBase(b);
        which.PushEnhancement(e);
        av_packet_free(&b);
        av_packet_free(&e);
        const bool joined = which.Pop(out, false) && out->size == both;
        int64_t difference = 0;
        AVRational timeBase{0, 1};
        LayerTime(out, difference, timeBase, &value, &entries);
        av_packet_unref(out);
        return joined;
      };
      std::string value;
      int entries = 0;
      CDVLayerMerge none;
      bool timesOk = join(none, 1000, 4754, value, entries) && value.empty() && entries == 0;
      Check("without the time bases of the tracks a joined packet says no time", timesOk);
      CDVLayerMerge t;
      t.SetTimeBases(ninety, ninety);
      timesOk = join(t, 1000, 4754, value, entries) && value == "3754 1 90000" && entries == 1;
      timesOk &= join(t, 1000, 100, value, entries) && value == "-900 1 90000";
      timesOk &= join(t, 1000, 1000, value, entries) && value == "0 1 90000";
      timesOk &= join(t, -500, 250, value, entries) && value == "750 1 90000";
      Check("the time is the difference to the packet's own (later, earlier or none), in the time base of the base track",
            timesOk);
      timesOk = join(t, 1000, AV_NOPTS_VALUE, value, entries) && value.empty() && entries == 0;
      timesOk &= join(t, AV_NOPTS_VALUE, 1000, value, entries) && value.empty();
      Check("no time when one of the two is not known", timesOk);
      timesOk = join(t, 1000, 1000 + 10 * 90000, value, entries) && value == "900000 1 90000";
      timesOk &= join(t, 1000, 1001 + 10 * 90000, value, entries) && value.empty();
      timesOk &= join(t, 1000, 999 - 10 * 90000, value, entries) && value.empty();
      timesOk &= join(t, INT64_MAX, INT64_MIN + 1, value, entries) && value.empty();
      Check("nor when they are more than ten seconds apart", timesOk);
      CDVLayerMerge other;
      other.SetTimeBases(ninety, AVRational{1, 1000});
      // The decoding times of an enhancement track in another time base are taken to the one of the base track
      // (since the join takes MP4 and Matroska), so the two packets have the same decoding time: 900 ticks of
      // 1/90000 and 10 of 1/1000.
      AVPacket* b0 = Packet(900, bl);
      b0->pts = 8000;
      AVPacket* e0 = Packet(10, el);
      e0->pts = 100;
      other.PushBase(b0);
      other.PushEnhancement(e0);
      av_packet_free(&b0);
      av_packet_free(&e0);
      timesOk = other.Pop(out, false) && out->size == both;
      {
        int64_t difference = 0;
        AVRational timeBase{0, 1};
        LayerTime(out, difference, timeBase, &value, &entries);
        av_packet_unref(out);
      }
      timesOk &= value == "1000 1 90000";
      Check("the time of an enhancement track with another time base is converted", timesOk);
      timesOk = join(t, 1000, 4754, value, entries, true) && value == "3754 1 90000" && entries == 2;
      Check("strings metadata that the packet already has is kept", timesOk);
    }
    // ---- 7. whether that time holds for the times that a reader makes of the times of the stream
    {
      const AVRational ninety{1, 90000};
      // CDVDDemuxFFmpeg::ConvertTimestamp(): the start time of the file is taken off a time after it, a time up
      // to half a second before it becomes 0, and an earlier one stays as it is
      const auto kodi = [](double start) -> std::function<double(int64_t)> {
        return [start](int64_t pts) {
          double seconds = static_cast<double>(pts) / 90000;
          if (seconds > start)
            seconds -= start;
          else if (seconds + 0.5 > start)
            seconds = 0;
          return seconds * 1e6;
        };
      };
      // a packet presented at basePts whose enhancement unit is presented at layerPts, joined and checked: what
      // CheckLayerTime() says, and the value it leaves
      const auto check = [&](CDVLayerMerge& which, int64_t basePts, int64_t layerPts,
                             const std::function<double(int64_t)>& reader, std::string& value, bool again = false) {
        AVPacket* b = Packet(500, bl);
        b->pts = basePts;
        AVPacket* e = Packet(500, el);
        e->pts = layerPts;
        which.PushBase(b);
        which.PushEnhancement(e);
        av_packet_free(&b);
        av_packet_free(&e);
        which.Pop(out, false);
        bool kept = which.CheckLayerTime(out, reader);
        if (again) // the packet is looked at a second time, as it is
          kept = which.CheckLayerTime(out, reader);
        int64_t difference = 0;
        AVRational timeBase{0, 1};
        LayerTime(out, difference, timeBase, &value);
        av_packet_unref(out);
        return kept;
      };
      const int64_t start = 600 * 90000;
      const auto reader = kodi(600.0);
      CDVLayerMerge t;
      t.SetTimeBases(ninety, ninety);
      std::string value;
      bool held = check(t, start + 9000, start + 12754, reader, value) && value == "3754 1 90000";
      held &= check(t, start + 9000, start + 5246, reader, value) && value == "-3754 1 90000";
      // a time that is the start time itself is 0 either way
      held &= check(t, start, start + 3754, reader, value) && value == "3754 1 90000";
      held &= check(t, start + 3754, start, reader, value) && value == "-3754 1 90000";
      Check("after the start time of the file, and at it, the time of an enhancement unit holds for Kodi's times",
            held && t.GetCounts().unusable == 0);
      // The packet up to half a second before the start (its time becomes 0), its enhancement unit after it: the
      // difference between the reader's two times, in microseconds (3754 ticks are 41711.1, 3753 are 41700).
      held = check(t, start - 3754, start + 3754, reader, value) && value == "41711 1 1000000";
      held &= check(t, start - 1, start + 3753, reader, value) && value == "41700 1 1000000";
      held &= check(t, start - 3754, start + 1, reader, value) && value == "11 1 1000000";
      // ... and the enhancement unit at the start itself, the one time that is 0 and names a picture
      held &= check(t, start - 3754, start, reader, value) && value == "0 1 1000000";
      Check("with the packet up to half a second before the start, the difference becomes the one between the "
            "reader's times",
            held && t.GetCounts().unusable == 0);
      // The enhancement unit there: its time is 0 like every time of that half second, and names no picture,
      // also when its packet is presented with it.
      held = !check(t, start + 3754, start - 3754, reader, value) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      held &= !check(t, start - 7508, start - 3754, reader, value) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      held &= !check(t, start - 3754, start - 3754, reader, value) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      Check("with the enhancement unit there, the packet is marked, and counted", held && t.GetCounts().unusable == 3);
      // more than half a second before the start a time stays as it is: two of those are still as far apart
      held = check(t, start - 90000, start - 86246, reader, value) && value == "3754 1 90000";
      held &= !check(t, start - 46000, start - 44000, reader, value) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      // ... and one of those with its enhancement unit after the start is as far from it as the start time
      held &= !check(t, start - 90000, start + 3754, reader, value) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      Check("two times that the reader leaves as they are keep their difference; with one of them on another side "
            "the packet is marked",
            held && t.GetCounts().unusable == 5);
      held = check(t, start + 3754, start - 3754, reader, value, true) && value == CDVLayerMerge::LAYER_TIME_UNUSABLE;
      Check("a packet that is checked again stays marked and is counted once", held && t.GetCounts().unusable == 6);
      // One whose difference was set right, looked at again (the reader keeps the first packet after a seek and
      // reads it once more): the difference is in microseconds now, and is not one to add to the time of the
      // packet, which is in ticks.
      held = check(t, start - 3754, start + 3754, reader, value, true) && value == "41711 1 1000000";
      held &= check(t, start - 3754, start + 9 * 3754, reader, value, true) && value == "375400 1 1000000";
      held &= check(t, start - 1, start + 3753, reader, value, true) && value == "41700 1 1000000";
      Check("a packet whose difference was set right keeps it when it is checked again",
            held && t.GetCounts().unusable == 6);
      held = check(t, 1000, 4754, [](int64_t pts) { return static_cast<double>(pts) / 90000 * 1e6 - 12345678.0; }, value) &&
           value == "3754 1 90000";
      held &= check(t, 1000, AV_NOPTS_VALUE, reader, value) && value.empty();
      Check("a reader that moves every time alike changes nothing, and a packet without the time is left alone",
            held && t.GetCounts().unusable == 6);
    }
    av_packet_free(&out);
  }

  // ---- 5. Append() with what is not a stream of units: refused, the base packet unchanged
  {
    const std::vector<uint8_t> bl = {0, 0, 0, 1, 0x02, 0x01, 0xaa};
    const std::vector<std::vector<uint8_t>> bad = {
        {},
        {0x02, 0x01, 0xbb},                // no start code
        {0xff, 0, 0, 1, 0x02, 0x01},       // something before the first start code
        {0, 0, 1, 0x02},                   // shorter than a header
        {0, 0, 1},                         // an empty unit
        {0, 0, 1, 0x82, 0x01, 0xbb},       // forbidden bit
        {0, 0, 1, 0x02, 0x01, 0, 0, 1},    // then an empty unit
    };
    bool ok = true;
    for (size_t i = 0; i < bad.size(); ++i)
    {
      AVPacket* p = Packet(1, bl);
      ok &= !CDVLayerMerge::Append(p, bad[i].data(), bad[i].size());
      ok &= p->size == static_cast<int>(bl.size()) && std::memcmp(p->data, bl.data(), bl.size()) == 0;
      av_packet_free(&p);
    }
    Check("seven malformed enhancement buffers are refused and leave the base packet as it was", ok);

    // random buffers: no crash, and whatever is accepted is a well-formed append
    unsigned int seed = 1;
    size_t accepted = 0;
    ok = true;
    for (int i = 0; i < 200000; ++i)
    {
      std::vector<uint8_t> e(1 + seed % 40);
      for (auto& c : e)
      {
        seed = seed * 1103515245u + 12345u;
        c = static_cast<uint8_t>((seed >> 16) % 5 == 0 ? (seed >> 20) : (seed >> 24) % 3);
      }
      AVPacket* p = Packet(1, bl);
      if (CDVLayerMerge::Append(p, e.data(), e.size()))
      {
        ++accepted;
        std::vector<Unit> u;
        ok &= UnitsAnnexB(p->data, static_cast<size_t>(p->size), u) && u.size() >= 2 && u[0] == Unit({0x02, 0x01, 0xaa});
        for (size_t k = 1; k < u.size(); ++k)
          ok &= Type(u[k]) == 63 || Type(u[k]) == 62;
      }
      else
        ok &= p->size == static_cast<int>(bl.size());
      av_packet_free(&p);
    }
    Check("200000 random buffers: no fault; the " + std::to_string(accepted) + " accepted give only wrapped units and RPUs after the base", ok);

    const auto level = [](int w, int h, int num, int den) { return static_cast<int>(CDVLayerMerge::Level(w, h, {num, den})); };
    Check("levels: 3840x2160 at 23.976 is 6, at 24 is 6, at 25 is 7, at 30 is 7, at 59.94 is 9, at 60 is 9; 1920x1080 at "
          "23.976 is 3, at 60 is 5; 7680x4320 at 60 is 12; 0 when not known",
          level(3840, 2160, 24000, 1001) == 6 && level(3840, 2160, 24, 1) == 6 && level(3840, 2160, 25, 1) == 7 &&
              level(3840, 2160, 30, 1) == 7 && level(3840, 2160, 60000, 1001) == 9 && level(3840, 2160, 60, 1) == 9 &&
              level(1920, 1080, 24000, 1001) == 3 && level(1920, 1080, 60, 1) == 5 && level(7680, 4320, 60, 1) == 12 &&
              level(0, 2160, 24, 1) == 0 && level(3840, 2160, 0, 1) == 0 && level(3840, 2160, 24, 0) == 0 &&
              level(7680, 4320, 240, 1) == 0);
    Check("the size of an enhancement layer: a quarter, or the same up to 1920x1080; unknown sizes do not speak against it",
          CDVLayerMerge::EnhancementSize(3840, 2160, 1920, 1080) && CDVLayerMerge::EnhancementSize(1920, 1080, 1920, 1080) &&
              CDVLayerMerge::EnhancementSize(1920, 1080, 960, 540) && !CDVLayerMerge::EnhancementSize(3840, 2160, 3840, 2160) &&
              !CDVLayerMerge::EnhancementSize(3840, 2160, 1280, 720) && !CDVLayerMerge::EnhancementSize(3840, 2160, 1920, 800) &&
              CDVLayerMerge::EnhancementSize(0, 0, 1920, 1080) && CDVLayerMerge::EnhancementSize(3840, 2160, 0, 0));
  }

  // ---- 12. Append() with units that have a length before them (MP4, Matroska)
  {
    const auto lengths = [](int size, const std::vector<std::vector<uint8_t>>& units) {
      std::vector<uint8_t> out;
      for (const auto& u : units)
      {
        for (int k = size - 1; k >= 0; --k)
          out.push_back(static_cast<uint8_t>(u.size() >> (8 * k)));
        out.insert(out.end(), u.begin(), u.end());
      }
      return out;
    };
    const std::vector<uint8_t> blUnit = {0x02, 0x01, 0xaa}, elUnit = {0x02, 0x01, 0xbb}, rpu = {0x7c, 0x01, 0x19, 0x08};
    std::vector<uint8_t> wrappedEl = {0x7e, 0x01};
    wrappedEl.insert(wrappedEl.end(), elUnit.begin(), elUnit.end());
    bool ok = true;
    for (const int baseSize : {1, 2, 4})
      for (const int elSize : {1, 2, 4})
      {
        const std::vector<uint8_t> bl = lengths(baseSize, {blUnit});
        const std::vector<uint8_t> el = lengths(elSize, {elUnit, rpu});
        AVPacket* p = Packet(1, bl);
        const bool appended = CDVLayerMerge::Append(p, el.data(), el.size(), baseSize, elSize);
        const std::vector<uint8_t> expect = lengths(baseSize, {blUnit, wrappedEl, rpu});
        ok &= appended && std::vector<uint8_t>(p->data, p->data + p->size) == expect;
        av_packet_free(&p);
      }
    // start codes on one side, lengths on the other
    {
      const std::vector<uint8_t> bl = lengths(4, {blUnit});
      const std::vector<uint8_t> el = {0, 0, 0, 1, 0x02, 0x01, 0xbb, 0, 0, 1, 0x7c, 0x01, 0x19, 0x08};
      AVPacket* p = Packet(1, bl);
      ok &= CDVLayerMerge::Append(p, el.data(), el.size(), 4, 0) &&
            std::vector<uint8_t>(p->data, p->data + p->size) == lengths(4, {blUnit, wrappedEl, rpu});
      av_packet_free(&p);
      const std::vector<uint8_t> annexB = {0, 0, 0, 1, 0x02, 0x01, 0xaa};
      const std::vector<uint8_t> el4 = lengths(4, {elUnit, rpu});
      p = Packet(1, annexB);
      const std::vector<uint8_t> expect = {0, 0, 0, 1, 0x02, 0x01, 0xaa, 0, 0, 0, 1, 0x7e, 0x01, 0x02, 0x01, 0xbb,
                                           0, 0, 0, 1, 0x7c, 0x01, 0x19, 0x08};
      ok &= CDVLayerMerge::Append(p, el4.data(), el4.size(), 0, 4) &&
            std::vector<uint8_t>(p->data, p->data + p->size) == expect;
      av_packet_free(&p);
    }
    Check("units with lengths of 1, 2 and 4 bytes are joined in the framing of the base packet; also from and to "
          "start codes", ok);

    // an empty unit is none, and fewer bytes at the end than a length are left
    {
      const std::vector<uint8_t> bl = lengths(4, {blUnit});
      std::vector<uint8_t> el = lengths(4, {elUnit});
      el.insert(el.end(), {0, 0, 0, 0});
      const std::vector<uint8_t> tail = lengths(4, {rpu});
      el.insert(el.end(), tail.begin(), tail.end());
      el.insert(el.end(), {0, 0, 1});
      AVPacket* p = Packet(1, bl);
      ok = CDVLayerMerge::Append(p, el.data(), el.size(), 4, 4) &&
           std::vector<uint8_t>(p->data, p->data + p->size) == lengths(4, {blUnit, wrappedEl, rpu});
      av_packet_free(&p);
      Check("a unit of length 0 is no unit, and three bytes at the end are left as libavcodec leaves them", ok);
    }

    const std::vector<uint8_t> bl = lengths(4, {blUnit});
    std::vector<std::vector<uint8_t>> bad = {
        {},
        {0, 0, 0},                                  // shorter than a length
        {0, 0, 0, 9, 0x02, 0x01, 0xbb},             // a length beyond the end
        {0, 0, 0, 1, 0x02},                         // a unit shorter than its header
        {0, 0, 0, 3, 0x82, 0x01, 0xbb},             // forbidden bit
        {0, 0, 0, 0},                               // only an empty unit
        lengths(4, {elUnit, {0x7e, 0x01, 0x02, 0x01, 0xcc}}), // a unit of type 63: the one-track form itself
    };
    ok = true;
    for (const auto& b : bad)
    {
      AVPacket* p = Packet(1, bl);
      ok &= !CDVLayerMerge::Append(p, b.data(), b.size(), 4, 4);
      ok &= std::vector<uint8_t>(p->data, p->data + p->size) == bl;
      av_packet_free(&p);
    }
    {
      // and in start codes, a unit of type 63 is refused too
      const std::vector<uint8_t> annexB = {0, 0, 0, 1, 0x02, 0x01, 0xaa};
      const std::vector<uint8_t> el = {0, 0, 1, 0x02, 0x01, 0xbb, 0, 0, 1, 0x7e, 0x01, 0x02, 0x01, 0xcc};
      AVPacket* p = Packet(1, annexB);
      ok &= !CDVLayerMerge::Append(p, el.data(), el.size()) && p->size == static_cast<int>(annexB.size());
      av_packet_free(&p);
    }
    // a unit too long for the length of the base framing
    {
      const std::vector<uint8_t> bl1 = lengths(1, {blUnit});
      const std::vector<uint8_t> big(254, 0x01);
      std::vector<uint8_t> unit = {0x02, 0x01};
      unit.insert(unit.end(), big.begin(), big.end()); // 256 bytes, 258 wrapped: more than one byte says
      const std::vector<uint8_t> el = lengths(2, {unit});
      AVPacket* p = Packet(1, bl1);
      ok &= !CDVLayerMerge::Append(p, el.data(), el.size(), 1, 2) && p->size == static_cast<int>(bl1.size());
      av_packet_free(&p);
      const std::vector<uint8_t> fits(250, 0x01);
      std::vector<uint8_t> small = {0x02, 0x01};
      small.insert(small.end(), fits.begin(), fits.end()); // 252 bytes, 254 wrapped
      const std::vector<uint8_t> el2 = lengths(2, {small});
      p = Packet(1, bl1);
      ok &= CDVLayerMerge::Append(p, el2.data(), el2.size(), 1, 2) && p->size == static_cast<int>(bl1.size() + 1 + 254);
      av_packet_free(&p);
    }
    Check("enhancement buffers that are no stream of units with lengths, that hold a unit of type 63, or whose unit "
          "the base framing cannot hold are refused and leave the base packet as it was", ok);

    // random buffers with lengths: no fault, and what is accepted is well formed
    unsigned int seed = 7;
    size_t accepted = 0;
    ok = true;
    const auto next = [&seed]() {
      seed = seed * 1103515245u + 12345u;
      return seed >> 16;
    };
    for (int i = 0; i < 200000; ++i)
    {
      // one to four units with a length before them, of random bytes; then, often, a random byte changed
      const int size = 1 + static_cast<int>(next() % 4); // 1, 2, 3 or 4 bytes of length
      std::vector<uint8_t> e;
      for (unsigned n = 1 + next() % 4; n > 0; --n)
      {
        const unsigned length = next() % 9;
        for (int k = size - 1; k >= 0; --k)
          e.push_back(static_cast<uint8_t>(length >> (8 * k)));
        for (unsigned k = 0; k < length; ++k)
          e.push_back(static_cast<uint8_t>(next() % 3 == 0 ? 0x7e : next()));
      }
      if (next() % 3 && !e.empty())
        e[next() % e.size()] = static_cast<uint8_t>(next());
      AVPacket* p = Packet(1, bl);
      if (CDVLayerMerge::Append(p, e.data(), e.size(), 4, size))
      {
        ++accepted;
        std::vector<Unit> u;
        ok &= UnitsLength(p->data, static_cast<size_t>(p->size), 4, u) && u.size() >= 2 && u[0] == Unit(blUnit);
        for (size_t k = 1; k < u.size(); ++k)
          ok &= (Type(u[k]) == 63 && u[k].size() >= 4 && Type(Unit(u[k].begin() + 2, u[k].end())) != 63) ||
                Type(u[k]) == 62;
      }
      else
        ok &= std::vector<uint8_t>(p->data, p->data + p->size) == bl;
      av_packet_free(&p);
    }
    Check("200000 random buffers with lengths: no fault; the " + std::to_string(accepted) +
              " accepted give only wrapped units and RPUs after the base", ok && accepted > 1000);
  }

  // ---- 13. times made from presentation times (Matroska): base pictures in their order, enhancement pictures
  // reordered (as in the test stream: a P picture two ahead, then the two B pictures before it)
  {
    using Layers = CDVLayerMerge::Layers;
    Layers layers;
    layers.decodingTimes = false;
    // a unit tagged with its decode slot, plus one: a unit never ends with a zero byte in a start-code stream
    const auto unit = [](unsigned type, uint8_t slot) {
      return std::vector<uint8_t>{0, 0, 0, 1, static_cast<uint8_t>(type << 1), 0x01, static_cast<uint8_t>(slot + 1)};
    };
    const int both = 7 + 4 + 2 + 3; // a base unit and a wrapped enhancement unit
    const int elTag = 7 + 4 + 2 + 2; // where the tag of the enhancement unit is in a joined packet
    // the enhancement layer in the order P B B of three-picture groups: presentation times of decode slots
    // (complete groups for a count of 3k + 1 from slot 0)
    const auto elPts = [](int slot) {
      if (slot == 0)
        return 0;
      const int group = (slot - 1) / 3, in = (slot - 1) % 3;
      return 10 * (3 * group + (in == 0 ? 3 : in));
    };
    // decode slot -> presentation slot: base in order; feed n packets, each base right before its enhancement
    // packet, or the enhancement layer `ahead` packets in front; keys: random-access pictures
    const auto play = [&](int first, int count, int ahead, int keyEvery, std::vector<std::pair<int64_t, int64_t>>& pairs,
                          CDVLayerMerge::Counts& counts) {
      CDVLayerMerge mm;
      mm.Configure(layers);
      AVPacket* out = av_packet_alloc();
      pairs.clear();
      const auto drain = [&](bool end) {
        while (mm.Pop(out, end))
        {
          // the tag of the base unit is its decode slot, the one of the enhancement unit that of its own
          const int64_t baseSlot = out->data[6] - 1;
          const int64_t elSlot = out->size == both ? out->data[elTag] - 1 : -1;
          pairs.emplace_back(baseSlot, elSlot);
          av_packet_unref(out);
        }
      };
      for (int i = first; i < first + count + std::abs(ahead); ++i)
      {
        const int b = ahead >= 0 ? i - ahead : i, e = ahead >= 0 ? i : i + ahead;
        if (e >= first && e < first + count)
        {
          AVPacket* p = Packet(AV_NOPTS_VALUE, unit(e % keyEvery == 0 ? 19 : 1, static_cast<uint8_t>(e)), 10);
          p->pts = elPts(e);
          mm.PushEnhancement(p);
          av_packet_free(&p);
        }
        if (b >= first && b < first + count)
        {
          AVPacket* p = Packet(AV_NOPTS_VALUE, unit(b % keyEvery == 0 ? 19 : 1, static_cast<uint8_t>(b)), 10);
          p->pts = 10 * b;
          mm.PushBase(p);
          av_packet_free(&p);
        }
        drain(false);
      }
      drain(true);
      counts = mm.GetCounts();
      av_packet_free(&out);
    };
    std::vector<std::pair<int64_t, int64_t>> pairs;
    CDVLayerMerge::Counts c;
    play(0, 199, 0, 1000, pairs, c);
    bool ok = pairs.size() == 199;
    for (size_t i = 0; i < pairs.size(); ++i)
      ok &= pairs[i].first == static_cast<int64_t>(i) && pairs[i].second == static_cast<int64_t>(i);
    Check("Matroska times: every base packet with the enhancement packet of its own decode slot, in order (" +
              std::to_string(c.joined) + " joined, " + std::to_string(c.alone) + " alone)",
          ok && c.joined == 199 && c.alone == 0 && c.dropped == 0);
    for (const int ahead : {5, -5, 40, -40})
    {
      play(0, 199, ahead, 1000, pairs, c);
      ok = pairs.size() == 199;
      for (size_t i = 0; i < pairs.size(); ++i)
        ok &= pairs[i].first == static_cast<int64_t>(i) && pairs[i].second == static_cast<int64_t>(i);
      Check("  the same with the enhancement layer " + std::to_string(std::abs(ahead)) + " packets " +
                (ahead > 0 ? "ahead" : "behind"),
            ok && c.joined == 199 && c.dropped == 0);
    }
    // without any random-access picture the tracks have no times: nothing is joined, nothing waits for long
    play(1, 198, 0, 1000, pairs, c);
    Check("  without a random-access picture in either track nothing is joined and the base packets go out (" +
              std::to_string(pairs.size()) + ")",
          pairs.size() == 198 && c.joined == 0 && c.waited == 0);
    // an enhancement layer that never brings a picture to start at: base packets wait for it no longer than
    // SetSilent() says (and then do not wait at all), as for a layer that is silent
    {
      CDVLayerMerge mm;
      mm.Configure(layers);
      AVPacket* out = av_packet_alloc();
      size_t pushed = 0, popped = 0, most = 0;
      for (int i = 0; i < 400; ++i)
      {
        AVPacket* p = Packet(AV_NOPTS_VALUE, unit(1, static_cast<uint8_t>(i % 200)), 10);
        p->pts = elPts(i % 199) + 10 * 199 * (i / 199);
        mm.PushEnhancement(p);
        av_packet_free(&p);
        p = Packet(AV_NOPTS_VALUE, unit(i % 24 == 0 ? 19 : 1, static_cast<uint8_t>(i % 200)), 10);
        p->pts = 10 * i;
        mm.PushBase(p);
        av_packet_free(&p);
        ++pushed;
        while (mm.Pop(out, false))
        {
          ++popped;
          av_packet_unref(out);
        }
        most = std::max(most, pushed - popped);
      }
      Check("  an enhancement layer without a picture to start at: at most " + std::to_string(most) +
                " base packets wait (silence bound " + std::to_string(CDVLayerMerge::SILENT) + ", " +
                std::to_string(CDVLayerMerge::REORDER) + " for the times), none joined",
            most <= CDVLayerMerge::SILENT + CDVLayerMerge::REORDER + 1 && mm.GetCounts().joined == 0);
      av_packet_free(&out);
    }
    // from the middle (a seek): the base layer from its random-access picture at slot 40, the enhancement layer
    // from slot 41 (not one) to its random-access picture at 49. The packets of the layer before it have no
    // time (the reordering window starts at a random-access packet), and are the access units just before it
    // (CDVLayerMerge::Backfill()): base 41 to 48 go out with them, each with its own; base 40, whose
    // enhancement packet was not read, alone; then all pair. (Before that rule: base 40 to 48 alone, and the eight
    // enhancement packets dropped.)
    {
      CDVLayerMerge mm;
      mm.Configure(layers);
      AVPacket* out = av_packet_alloc();
      std::vector<std::pair<int64_t, int64_t>> got;
      for (int i = 40; i < 139; ++i)
      {
        if (i >= 41)
        {
          AVPacket* p = Packet(AV_NOPTS_VALUE, unit(i == 49 || i == 100 ? 19 : 1, static_cast<uint8_t>(i)), 10);
          p->pts = elPts(i);
          mm.PushEnhancement(p);
          av_packet_free(&p);
        }
        AVPacket* p = Packet(AV_NOPTS_VALUE, unit(i == 40 || i == 100 ? 19 : 1, static_cast<uint8_t>(i)), 10);
        p->pts = 10 * i;
        mm.PushBase(p);
        av_packet_free(&p);
        while (mm.Pop(out, false))
        {
          got.emplace_back(out->data[6] - 1, out->size == both ? out->data[elTag] - 1 : -1);
          av_packet_unref(out);
        }
      }
      while (mm.Pop(out, true))
      {
        got.emplace_back(out->data[6] - 1, out->size == both ? out->data[elTag] - 1 : -1);
        av_packet_unref(out);
      }
      ok = got.size() == 99;
      size_t alone = 0;
      for (size_t i = 0; i < got.size(); ++i)
      {
        const int64_t slot = 40 + static_cast<int64_t>(i);
        ok &= got[i].first == slot;
        if (slot == 40)
          alone += got[i].second == -1;
        else
          ok &= got[i].second == slot;
      }
      Check("  after a seek: the base packets before the first random-access picture of the layer go out with the "
            "enhancement packets read before it, each with its own; alone only the one whose enhancement packet "
            "was not read (" + std::to_string(alone) + "); " + std::to_string(mm.GetCounts().dropped) +
                " enhancement packets dropped",
            ok && alone == 1 && mm.GetCounts().dropped == 0);
      av_packet_free(&out);
    }
    // the same packets as the transport stream gives them (decoding times, the enhancement layer one behind):
    // with decoding times the join pairs by those, and the presentation times change nothing
    {
      layers.decodingTimes = true;
      CDVLayerMerge mm;
      mm.Configure(layers);
      AVPacket* out = av_packet_alloc();
      size_t right = 0;
      for (int i = 0; i < 100; ++i)
      {
        AVPacket* p = Packet(10 * i, unit(1, static_cast<uint8_t>(i)), 10);
        p->pts = elPts(i);
        mm.PushEnhancement(p);
        av_packet_free(&p);
        p = Packet(10 * i, unit(1, static_cast<uint8_t>(i)), 10);
        mm.PushBase(p);
        av_packet_free(&p);
        while (mm.Pop(out, false))
        {
          right += out->size == both && out->data[6] == out->data[elTag];
          av_packet_unref(out);
        }
      }
      Check("  with decoding times the presentation times are not used (" + std::to_string(right) + " right)",
            right == 100);
      av_packet_free(&out);
      layers.decodingTimes = false;
    }
  }

  // ---- 14. an enhancement track with another time base (MP4 tracks have their own)
  {
    CDVLayerMerge mm;
    mm.SetTimeBases(AVRational{1, 90000}, AVRational{1, 24000});
    const std::vector<uint8_t> bl = {0, 0, 0, 1, 0x02, 0x01, 0xaa};
    const std::vector<uint8_t> el = {0, 0, 0, 1, 0x02, 0x01, 0xbb, 0, 0, 1, 0x7c, 0x01, 0x19, 0x08};
    AVPacket* out = av_packet_alloc();
    size_t joined = 0;
    for (int i = 0; i < 100; ++i)
    {
      // 1001 ticks of 1/24000 are 3753.75 of 1/90000: the base times are those, rounded
      AVPacket* p = Packet(llround(i * 3753.75), bl, 3754);
      mm.PushBase(p);
      av_packet_free(&p);
      p = Packet(i * 1001, el, 1001);
      mm.PushEnhancement(p);
      av_packet_free(&p);
      while (mm.Pop(out, false))
      {
        joined += out->size > static_cast<int>(bl.size());
        av_packet_unref(out);
      }
    }
    Check("an enhancement track in another time base pairs by its times taken to the base time base (" +
              std::to_string(joined) + " of 100 joined)",
          joined == 100 && mm.GetCounts().dropped == 0);
    av_packet_free(&out);
  }

  // ---- 15. Find() on stream groups made here: what each container needs of a pair
  {
    struct Track
    {
      int width, height;
      bool hvcc;   // an hvcC record (lengths), or start codes
      int record;  // -1: no Dolby Vision record; else its bl_present_flag
      bool rpu = true;
    };
    const auto find = [](const char* demuxer, const Track& base, const Track& layer) {
      AVFormatContext* ctx = avformat_alloc_context();
      ctx->iformat = av_find_input_format(demuxer);
      AVStreamGroup* group = avformat_stream_group_create(ctx, AV_STREAM_GROUP_PARAMS_DOLBY_VISION, nullptr);
      for (const Track* t : {&base, &layer})
      {
        AVStream* st = avformat_new_stream(ctx, nullptr);
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = AV_CODEC_ID_HEVC;
        st->codecpar->width = t->width;
        st->codecpar->height = t->height;
        st->time_base = AVRational{1, 1000};
        st->avg_frame_rate = st->r_frame_rate = AVRational{24000, 1001};
        const std::vector<uint8_t> hvcc = {1, 0x22, 0x20, 0, 0, 0, 0x90, 0, 0, 0, 0, 0, 0x99, 0xf0, 0, 0xfc,
                                           0xfd, 0xfa, 0xfa, 0, 0, 0x0f, 0};
        const std::vector<uint8_t> annexB = {0, 0, 0, 1, 0x40, 0x01, 0x0c};
        const std::vector<uint8_t>& ed = t->hvcc ? hvcc : annexB;
        st->codecpar->extradata = static_cast<uint8_t*>(av_mallocz(ed.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        std::memcpy(st->codecpar->extradata, ed.data(), ed.size());
        st->codecpar->extradata_size = static_cast<int>(ed.size());
        if (t->record >= 0)
        {
          AVPacketSideData* sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                         &st->codecpar->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF,
                                                         sizeof(AVDOVIDecoderConfigurationRecord), 0);
          auto* r = reinterpret_cast<AVDOVIDecoderConfigurationRecord*>(sd->data);
          std::memset(r, 0, sizeof(*r));
          r->dv_version_major = 1;
          r->dv_profile = 7;
          r->dv_level = 6;
          r->rpu_present_flag = t->rpu;
          r->el_present_flag = 1;
          r->bl_present_flag = static_cast<uint8_t>(t->record);
          r->dv_bl_signal_compatibility_id = 6;
        }
        avformat_stream_group_add_stream(group, st);
      }
      group->params.layered_video->el_index = 1;
      const CDVLayerMerge::Layers l = CDVLayerMerge::Find(ctx, 0);
      avformat_free_context(ctx);
      return l.result;
    };
    const Track base4k{3840, 2160, true, -1}, el1080{1920, 1080, true, 0}, el1080bl{1920, 1080, true, 1};
    const Track el4k{3840, 2160, true, 0}, el1080annexB{1920, 1080, false, 0}, el1080norpu{1920, 1080, true, 0, false};
    const Track el1080none{1920, 1080, true, -1};
    const Track tsBase{3840, 2160, false, -1}, tsEl{1920, 1080, false, 0}, tsEl4k{3840, 2160, false, 0};
    const Track tsDisc4k{3840, 2160, false, -1}, tsElHvcc{1920, 1080, true, 0};
    bool ok = true;
    std::string got;
    const auto expect = [&](const char* what, Result r, Result wanted) {
      got += std::string(" ") + what + (r == wanted ? ":ok" : ":WRONG");
      ok &= r == wanted;
    };
    expect("mkv", find("matroska", base4k, el1080), Result::JOINED);
    expect("mp4", find("mp4", base4k, el1080), Result::JOINED);
    expect("mkv-bl", find("matroska", base4k, el1080bl), Result::JOINED);
    expect("mp4-bl", find("mp4", base4k, el1080bl), Result::NONE);
    expect("mkv-4k", find("matroska", base4k, el4k), Result::WRONG_SIZE);
    expect("mp4-4k", find("mp4", base4k, el4k), Result::WRONG_SIZE);
    expect("mkv-annexb", find("matroska", base4k, el1080annexB), Result::FRAMING);
    expect("mkv-norpu", find("matroska", base4k, el1080norpu), Result::NONE);
    expect("mkv-norecord", find("matroska", base4k, el1080none), Result::NONE);
    expect("avi", find("avi", base4k, el1080), Result::NONE);
    expect("ts", find("mpegts", tsBase, tsEl), Result::JOINED);
    expect("ts-hvcc", find("mpegts", tsBase, tsElHvcc), Result::FRAMING);
    expect("ts-4k", find("mpegts", tsBase, tsEl4k), Result::JOINED); // a described pair: no size rule, as before
    expect("disc-4k", find("mpegts", tsBase, tsDisc4k), Result::WRONG_SIZE);
    // a group the wrong way round, as libavformat makes one of two Matroska tracks whose record is
    // on the base track alone: in Matroska the two tracks are found as libavformat would have found them without a
    // group (unverified, the larger track as the base: its packets decide); in MP4 (a 'vdep' reference says which
    // is which) still refused for its size
    // (in MP4 a record that says "base layer present" is refused before the size: there, one that does not)
    const Track small1080{1920, 1080, true, -1}, record4k{3840, 2160, true, 1}, record4kel{3840, 2160, true, 0};
    expect("mkv-reversed", find("matroska", small1080, record4k), Result::JOINED);
    expect("mp4-reversed", find("mp4", small1080, record4kel), Result::WRONG_SIZE);
    Check("Find() on groups made here: MP4 and Matroska joined; the base-layer flag refuses an MP4 enhancement "
          "record and not a Matroska one; another size, start codes in Matroska or an hvcC record in a transport "
          "stream refused; no RPU or no record, nothing; transport streams as before; a Matroska group the wrong way "
          "round found as without a group, an MP4 one refused (" + got + " )",
          ok);
  }

  if (argc == 5)
    Containers(argv[4], argv[1]);

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
