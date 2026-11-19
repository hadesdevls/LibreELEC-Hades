// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of a drain in the middle of a stream, as Kodi's video thread makes one when the stream
// runs dry (CVideoPlayerVideo::Process(): DVD_CODEC_CTRL_DRAIN), with a real dual-layer stream of one track, its
// base layer decoded in software and its enhancement layer by the module itself (tools/dvbridge/dvbridge_fel.c).
//
// The decoder is CDVDVideoCodecFFmpeg as it was before CDrainRestart ("before") or as it is ("after", with CDrainRestart,
// DrainRestart.h of the tree): AddData(), GetBasePicture() (DRAIN sends the decoder its end; a hardware pipeline that
// gives a picture once it holds hw=D of them, as VAAPI's CDecoder::Decode() does, D 2 by default; Kodi's m_started,
// which leaves out pictures until a key one after a flush), GetPairedPicture() (six pictures waiting at most, three
// late in a row resynchronize the layer), GetPicture() and Reset(). The video thread: after each packet, pictures are
// asked for while there is one; after packet dry=N (counted from the start or the seek), the stream runs dry: the
// squeeze asks for pull=K pictures at most with DRAIN, then stall=M more (the stalled thread's timeouts), and the
// packets come again; a packet that is not taken comes again after a call for pictures.
//
// A run without the drain, the same otherwise, is the reference: with the drain, every picture of it has to go out,
// once, in order, paired (or not) as it is there, with the same enhancement picture; and no resynchronization.
//
// Usage: fel_drain_test <file> mode=before|after [annexb] [from=S] [count=L] [seek=A:B] [dry=N] [pull=K] [stall=M]
//        [hw=D] [oldfeldrain] [units]
//   units: the checks of CDrainRestart alone (no file needed: give "-")
#include "DrainRestart.h"

extern "C"
{
#include "fel_shim.h"
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
// The header of the tree as it is (DrainRestart.h: a picture made from a packet given again is told by its mark,
// Decoded()) or as it was (by its time, Given() and LastGiven(), with a window of 2 s)
template <typename R>
void TellDecoded(R& r, const AVBufferRef* mark, double time)
{
  if constexpr (requires { r.Decoded(mark, time); })
    r.Decoded(mark, time);
}
template <typename R>
void TellGiven(R& r, double time)
{
  if constexpr (requires { r.Given(time); })
    r.Given(time);
}
template <typename R>
constexpr bool MARKS = requires(R r) { r.Decoded(nullptr, 0.0); };
// tools/regress: the header under test tells the pictures made from a packet given again by their mark
static_assert(MARKS<CDrainRestart>, "the header under test does not mark the packets given again (Decoded())");
template <typename R>
bool LastGivenOf(R& r, double& last)
{
  if constexpr (requires { r.LastGiven(last); })
    return r.LastGiven(last);
  else
    return false;
}
template <typename R, typename K>
void GiveAgainTo(R& r, K&& kept, bool given, double last)
{
  if constexpr (MARKS<R>)
    r.GiveAgain(std::move(kept));
  else
    r.GiveAgain(std::move(kept), given, last);
}
constexpr double DVD_TIME_BASE = 1000000.0;
constexpr double DVD_NOPTS_VALUE = -4503599627370496.0;
constexpr size_t DV_EL_LATE_PENDING = 6;
constexpr unsigned DV_EL_LATE_RESYNC = 3;
enum VC
{
  VC_ERROR,
  VC_BUFFER,
  VC_PICTURE,
  VC_EOF
};

int g_failures = 0;
void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  g_failures += !ok;
}

uint64_t FrameHash(const AVFrame* f)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (int plane = 0; plane < 3 && f->data[plane]; ++plane)
  {
    const int rows = plane ? (f->height + 1) / 2 : f->height;
    const int bytes = (plane ? (f->width + 1) / 2 : f->width) * 2;
    for (int y = 0; y < rows; y += 7)
      for (int x = 0; x < bytes; x += 3)
      {
        h ^= f->data[plane][(ptrdiff_t)y * f->linesize[plane] + x];
        h *= UINT64_C(1099511628211);
      }
  }
  return h;
}

struct Options
{
  bool after = false, annexb = false, oldFelDrain = false;
  unsigned from = 1, count = 0, seekAfter = 0, seekTo = 0, dry = 0, pull = 1000000, stall = 0, hw = 2;
  unsigned jump = 0; // after the drain, the times of every packet go back by this many microseconds, without a flush
};

struct Pic
{
  unsigned n = 0; // its packet (the frame's opaque, AV_CODEC_FLAG_COPY_OPAQUE)
  int64_t pts;
  bool paired;
  uint64_t layer;
  bool drain = false; // it went out during the drain (before the packets came again)
};

struct Pending
{
  int64_t pts;
  AVFrame* base;
  unsigned n;
};

// CDVDVideoCodecFFmpeg, as far as a drain goes
struct Codec
{
  const Options& o;
  AVCodecContext* base = nullptr;
  dvbridge_fel* fel = nullptr;
  std::deque<AVFrame*> hw; // the hardware pipeline
  std::deque<Pending> pending;
  bool eof = false, started = false, startedInput = false, dvFailed = false;
  int control = 0; // DRAIN
  unsigned lateInRow = 0;
  CDrainRestart restart;
  int64_t clockMs = 0;
  // for the checks
  unsigned resyncs = 0, refused = 0, unpaired = 0;
  std::map<std::string, unsigned> reasons;

  explicit Codec(const Options& options) : o(options) {}
  ~Codec()
  {
    for (AVFrame* f : hw)
      av_frame_free(&f);
    for (Pending& p : pending)
      av_frame_free(&p.base);
    avcodec_free_context(&base);
    dvbridge_fel_destroy(fel);
  }

  void Resync(const char* reason)
  {
    ++resyncs;
    ++reasons[reason ? reason : "unknown"];
    dvbridge_fel_reset(fel);
    dvFailed = false;
    lateInRow = 0;
  }

  void Reset()
  {
    if (o.after)
      restart.Reset();
    for (Pending& p : pending)
      av_frame_free(&p.base);
    pending.clear();
    dvbridge_fel_reset(fel);
    dvbridge_fel_base_flushed(fel);
    dvFailed = false;
    lateInRow = 0;
    started = startedInput = eof = false;
    avcodec_flush_buffers(base);
    for (AVFrame* f : hw)
      av_frame_free(&f);
    hw.clear();
  }

  // SendPacket(): true when the packet is to be sent again
  bool Send(AVPacket* p, const CDrainRestart::Info& info)
  {
    if (info.recoveryPoint)
      started = true;
    const int ret = avcodec_send_packet(base, p);
    const bool again = ret == AVERROR(EAGAIN);
    refused += ret == AVERROR_EOF;
    if (!again && !dvbridge_fel_submit(fel, p))
      dvFailed = true;
    if (o.after && !ret)
      restart.Taken(*p, info, static_cast<double>(p->pts) * DVD_TIME_BASE / AV_TIME_BASE);
    if (!again)
      startedInput = true;
    return again;
  }

  bool AddData(AVPacket* p, const CDrainRestart::Info& info)
  {
    ++clockMs;
    if (o.after)
    {
      const CDrainRestart::Arrival a = restart.Arrived(clockMs);
      if (const std::string note = restart.Note(); !note.empty())
        std::printf("log: %s\n", note.c_str());
      if (a == CDrainRestart::Arrival::WAIT)
        return false;
      if (a == CDrainRestart::Arrival::RESTART)
      {
        std::deque<CDrainRestart::Kept> kept = restart.TakeKept();
        std::printf("log: restart at %.3f s with %zu packets given again\n", restart.AccessTime() / DVD_TIME_BASE,
                    kept.size());
        double last = 0;
        const bool given = LastGivenOf(restart, last);
        Reset();
        GiveAgainTo(restart, std::move(kept), given, last);
      }
    }
    if (eof)
      Reset();
    if (o.after && restart.HasAgain())
    {
      CDrainRestart::Kept& k = restart.Again();
      if (!Send(k.packet, k.info))
        restart.GivenAgain();
      return false;
    }
    return !Send(p, info);
  }

  VC GetBase(AVFrame** out)
  {
    if (!startedInput)
      return VC_BUFFER;
    if (eof)
      return VC_EOF;
    if (o.hw && hw.size() >= o.hw)
    {
      *out = hw.front();
      hw.pop_front();
      return VC_PICTURE;
    }
    if (control)
    {
      avcodec_send_packet(base, nullptr);
      if (o.after)
        restart.Drained();
    }
    AVFrame* f = av_frame_alloc();
    const int ret = avcodec_receive_frame(base, f);
    if (ret == AVERROR_EOF)
    {
      av_frame_free(&f);
      if (!hw.empty())
      {
        *out = hw.front();
        hw.pop_front();
        return VC_PICTURE;
      }
      eof = true;
      return VC_EOF;
    }
    if (ret < 0)
    {
      av_frame_free(&f);
      return ret == AVERROR(EAGAIN) ? VC_BUFFER : VC_ERROR;
    }
    if (f->flags & AV_FRAME_FLAG_KEY)
      started = true;
    if (!started)
    {
      av_frame_free(&f);
      return VC_BUFFER;
    }
    // (Kodi's GetPictureCommon(), as the picture is made)
    if (o.after)
      TellDecoded(restart, f->opaque_ref, static_cast<double>(f->pts) * DVD_TIME_BASE / AV_TIME_BASE);
    hw.push_back(f);
    if (hw.size() >= o.hw)
    {
      *out = hw.front();
      hw.pop_front();
      return VC_PICTURE;
    }
    return VC_BUFFER;
  }

  // emit() of GetPairedPicture()
  bool Emit(bool baseEnded, Pic& pic)
  {
    if (pending.empty())
      return false;
    Pending& b = pending.front();
    AVFrame* layer = nullptr;
    int result = dvbridge_fel_take(fel, b.pts, &layer);
    if (result == 3 || result == 4)
    {
      av_frame_free(&layer);
      result = result == 3 ? 5 : 2;
    }
    if (result == 0 && baseEnded)
      result = 2;
    else if (result == 0 && pending.size() >= DV_EL_LATE_PENDING)
    {
      if (++lateInRow >= DV_EL_LATE_RESYNC)
        Resync("enhancement pictures are late or missing");
      result = 2;
    }
    if (result < 0 && dvbridge_fel_exhausted(fel))
      result = 2;
    else if (result < 0)
    {
      Resync(dvbridge_fel_failed(fel) ? dvbridge_fel_reason(fel) : "an enhancement picture is missing");
      result = 2;
    }
    if (result == 0)
      return false;
    pic.pts = b.pts;
    pic.n = b.n;
    pic.paired = result == 1;
    pic.layer = result == 1 ? FrameHash(layer) : 0;
    if (result == 1)
      lateInRow = 0;
    unpaired += result == 2;
    av_frame_free(&layer);
    av_frame_free(&b.base);
    pending.pop_front();
    return true;
  }

  VC GetPaired(Pic& pic)
  {
    if (dvFailed || dvbridge_fel_failed(fel))
      Resync(dvbridge_fel_reason(fel));
    if (Emit(false, pic))
      return VC_PICTURE;
    AVFrame* f = nullptr;
    const VC result = GetBase(&f);
    if (result == VC_PICTURE)
    {
      dvbridge_fel_base_picture(fel, f->pts);
      pending.push_back({f->pts, f, static_cast<unsigned>(reinterpret_cast<intptr_t>(f->opaque))});
    }
    const bool drain = o.after && !o.oldFelDrain ? restart.Draining() : control != 0;
    if (result == VC_EOF || drain)
      if (!dvbridge_fel_drain(fel))
        Resync(dvbridge_fel_reason(fel));
    if (Emit(result == VC_EOF, pic))
      return VC_PICTURE;
    return result == VC_PICTURE ? VC_BUFFER : result;
  }

  VC GetPicture(Pic& pic)
  {
    for (;;)
    {
      const VC r = GetPaired(pic);
      if (!o.after)
        return r;
      if (r == VC_EOF)
        restart.Ended();
      const double t = static_cast<double>(pic.pts) * DVD_TIME_BASE / AV_TIME_BASE;
      if (r != VC_PICTURE)
        return r;
      if (!restart.Drop(t))
      {
        TellGiven(restart, t);
        return r;
      }
      unpaired -= !pic.paired;
    }
  }
};

// CDrainRestart alone
template <typename R = CDrainRestart>
void Units()
{
  if constexpr (!MARKS<R>)
  {
    std::printf("note: units: not for this header (it has no Decoded())\n");
    return;
  }
  else
  {
  // an IDR picture (type 19) after a parameter set and an RPU, with lengths; a trailing picture (1); with start codes
  const uint8_t idr[] = {0, 0, 0, 3, 0x40, 1, 0x0c, 0, 0, 0, 3, 0x7c, 1, 0x19, 0, 0, 0, 3, 0x26, 1, 0xaf};
  const uint8_t trail[] = {0, 0, 0, 3, 0x7e, 1, 0x01, 0, 0, 0, 3, 0x02, 1, 0xd0};
  const uint8_t craAnnexB[] = {0, 0, 0, 1, 0x46, 1, 0x10, 0, 0, 1, 0x2a, 1, 0xaf, 0x80};
  const uint8_t trailAnnexB[] = {0, 0, 1, 0x7e, 1, 1, 0, 0, 1, 0x02, 1, 0xd0};
  const uint8_t elOnly[] = {0, 0, 0, 3, 0x7e, 1, 0x2a, 0, 0, 0, 3, 0x7c, 1, 0x19};
  Check(CDrainRestart::RandomAccess(idr, sizeof(idr), 4) && !CDrainRestart::RandomAccess(trail, sizeof(trail), 4) &&
            CDrainRestart::RandomAccess(craAnnexB, sizeof(craAnnexB), 0) &&
            !CDrainRestart::RandomAccess(trailAnnexB, sizeof(trailAnnexB), 0) &&
            !CDrainRestart::RandomAccess(elOnly, sizeof(elOnly), 4) && !CDrainRestart::RandomAccess(nullptr, 0, 4),
        "units: a random-access point is told by the first unit of the video coding layer, in both framings (units "
        "of type 63, the enhancement layer, are not of it)");
  const uint8_t hvcc[23] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0};
  auto packet = [](const uint8_t* d, int size, int64_t pts) {
    AVPacket* p = av_packet_alloc();
    av_new_packet(p, size);
    std::memcpy(p->data, d, size);
    p->pts = pts;
    return p;
  };
  R r;
  r.Configure(true, hvcc, sizeof(hvcc));
  AVPacket* a = packet(idr, sizeof(idr), 1000);
  AVPacket* b = packet(trail, sizeof(trail), 2000);
  AVPacket* c = packet(trail, sizeof(trail), AV_NOPTS_VALUE);
  r.Taken(*b, {}, 2);
  bool ok = r.KeptCount() == 0; // nothing before a random-access point
  r.Taken(*a, {}, 1);
  r.Taken(*b, {}, 2);
  ok &= r.KeptCount() == 2 && r.Arrived(0) == CDrainRestart::Arrival::TAKE;
  r.Drained();
  ok &= r.Draining() && r.Arrived(10) == CDrainRestart::Arrival::WAIT &&
        r.Arrived(10 + CDrainRestart::WAIT_MAX_MS) == CDrainRestart::Arrival::WAIT;
  r.Ended();
  ok &= !r.Draining() && r.Arrived(20) == CDrainRestart::Arrival::RESTART;
  std::deque<CDrainRestart::Kept> kept = r.TakeKept();
  r.Reset();
  r.GiveAgain(std::move(kept));
  ok &= r.HasAgain() && r.Again().packet->pts == 1000 && r.Again().packet->opaque_ref;
  AVBufferRef* mark = r.HasAgain() && r.Again().packet->opaque_ref ? av_buffer_ref(r.Again().packet->opaque_ref) : nullptr;
  r.GivenAgain();
  ok &= r.HasAgain() && r.Again().packet->pts == 2000;
  r.GivenAgain();
  // the two pictures of the packets given again, and one of another packet with the time of one of them
  r.Decoded(mark, 1);
  r.Decoded(nullptr, 2);
  r.Decoded(mark, 2);
  ok &= !r.HasAgain() && r.Drop(2) && r.Drop(1) && !r.Drop(1) && !r.Drop(2) && !r.Drop(3);
  av_buffer_unref(&mark);
  Check(ok, "units: packets are kept from a random-access point on, a packet waits for the drain, then the kept ones "
            "are given again with the mark of the restart, and the pictures made from them, and only those (not one "
            "of another packet with the same time), are left out, once each");
  // the decoder that does not give its end: after WAIT_MAX_MS the packet goes as before, and the log says so
  r.Taken(*a, {}, 1);
  r.Drained();
  ok = r.Arrived(100) == CDrainRestart::Arrival::WAIT && r.Note().empty() &&
       r.Arrived(101 + CDrainRestart::WAIT_MAX_MS) == CDrainRestart::Arrival::TAKE && !r.Note().empty() &&
       r.Arrived(200 + CDrainRestart::WAIT_MAX_MS) == CDrainRestart::Arrival::TAKE;
  r.Ended();
  ok &= r.Arrived(300 + CDrainRestart::WAIT_MAX_MS) == CDrainRestart::Arrival::TAKE && r.Note().empty();
  Check(ok, "units: a decoder that does not give its end within WAIT_MAX_MS: the packets go to it as before, said "
            "once");
  // a packet without a time, and a stream of another codec: no restart, and the log says why
  r.Reset();
  r.Taken(*a, {}, 1);
  r.Taken(*c, {}, 3);
  r.Drained();
  r.Ended();
  ok = r.KeptCount() == 0 && r.Arrived(0) == CDrainRestart::Arrival::TAKE &&
       r.Note().find("without a time") != std::string::npos;
  CDrainRestart other;
  other.Configure(false, nullptr, 0);
  other.Taken(*a, {}, 1);
  other.Drained();
  ok &= other.KeptCount() == 0 && other.Arrived(0) == CDrainRestart::Arrival::TAKE;
  Check(ok, "units: without every packet since a random-access point (one without a time; another codec) the "
            "decoder starts again at the packet that came, as before");
  // a packet given again whose picture the decoder does not make, and a picture of another packet with its time: only
  // the mark tells the two apart, and the picture of the other packet goes out (by the times alone it would not)
  {
    R s;
    s.Configure(true, hvcc, sizeof(hvcc));
    s.Taken(*a, {}, 1);
    s.Taken(*b, {}, 2);
    s.Drained();
    s.Ended();
    ok = s.Arrived(0) == CDrainRestart::Arrival::RESTART;
    std::deque<CDrainRestart::Kept> again = s.TakeKept();
    s.Reset();
    s.GiveAgain(std::move(again));
    AVBufferRef* m = s.HasAgain() && s.Again().packet->opaque_ref ? av_buffer_ref(s.Again().packet->opaque_ref) : nullptr;
    s.GivenAgain();
    s.GivenAgain();
    s.Decoded(m, 1);
    s.Decoded(nullptr, 2);
    ok &= m && !s.HasAgain() && s.Drop(1) && !s.Drop(2) && !s.Drop(1);
    av_buffer_unref(&m);
    Check(ok, "units: the picture of another packet with the time of a packet given again whose picture the decoder "
              "did not make goes out: the mark of the restart, not the time, tells the pictures to leave out");
  }
  av_packet_free(&a);
  av_packet_free(&b);
  av_packet_free(&c);
  }
}

struct Run
{
  std::vector<Pic> pics;
  unsigned resyncs = 0, refused = 0, unpaired = 0, dropped = 0, restarts = 0, drainDropped = 0, late = 0;
  std::map<std::string, unsigned> reasons;
};

bool Play(const char* path, const Options& o, bool withDry, Run& run)
{
  AVFormatContext* fc = nullptr;
  if (avformat_open_input(&fc, path, nullptr, nullptr) < 0 || avformat_find_stream_info(fc, nullptr) < 0)
    return false;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  AVStream* stream = fc->streams[index];
  const AVCodecParameters* par = stream->codecpar;
  AVBSFContext* annexb = nullptr;
  if (o.annexb)
  {
    if (av_bsf_alloc(av_bsf_get_by_name("hevc_mp4toannexb"), &annexb) < 0 ||
        avcodec_parameters_copy(annexb->par_in, par) < 0)
      return false;
    annexb->time_base_in = stream->time_base;
    if (av_bsf_init(annexb) < 0)
      return false;
    par = annexb->par_out;
  }
  const AVRational micro = {1, AV_TIME_BASE};
  auto codec = std::make_unique<Codec>(o);
  const AVCodec* hevc = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  codec->base = avcodec_alloc_context3(hevc);
  if (avcodec_parameters_to_context(codec->base, par) < 0)
    return false;
  codec->base->pkt_timebase = micro;
  codec->base->flags |= AV_CODEC_FLAG_COPY_OPAQUE; // as Kodi
  codec->base->thread_count = 1; // as with VAAPI
  if (avcodec_open2(codec->base, hevc, nullptr) < 0)
    return false;
  codec->fel = shim_fel_open(par, micro);
  if (!codec->fel)
    return false;
  codec->restart.Configure(true, par->extradata, par->extradata_size);

  AVPacket* p = av_packet_alloc();
  unsigned n = 0, given = 0, sinceStart = 0;
  bool seeked = false, recovery = false, inDrain = false;
  Pic pic{};
  auto pull = [&](unsigned most) {
    unsigned got = 0;
    while (got < most && codec->GetPicture(pic) == VC_PICTURE)
    {
      pic.drain = inDrain;
      run.pics.push_back(pic);
      ++got;
    }
    return got;
  };
  while (av_read_frame(fc, p) >= 0)
  {
    if (p->stream_index != index)
    {
      av_packet_unref(p);
      continue;
    }
    ++n;
    if (n < o.from && !seeked)
    {
      av_packet_unref(p);
      continue;
    }
    if (annexb && (av_bsf_send_packet(annexb, p) < 0 || av_bsf_receive_packet(annexb, p) < 0))
      return false;
    p->pts = p->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : av_rescale_q(p->pts, stream->time_base, micro);
    p->dts = p->dts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : av_rescale_q(p->dts, stream->time_base, micro);
    p->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(n));
    // jump: the times go back after the drain (in both runs), as a stream whose times jump without a flush
    if (o.jump && o.dry && sinceStart + 1 > o.dry && (!o.seekAfter || seeked))
    {
      p->pts -= p->pts == AV_NOPTS_VALUE ? 0 : o.jump;
      p->dts -= p->dts == AV_NOPTS_VALUE ? 0 : o.jump;
    }
    ++given;
    ++sinceStart;
    codec->control = 0;
    unsigned tries = 0;
    while (!codec->AddData(p, {0.0, 0.0, recovery}))
    {
      pull(1000000);
      if (++tries > 100000)
      {
        std::printf("FAIL: a packet was never taken\n");
        return false;
      }
    }
    recovery = false;
    inDrain = false;
    av_packet_unref(p);
    pull(1000000);
    // (with a seek, after it)
    if (withDry && o.dry && sinceStart == o.dry && (!o.seekAfter || seeked))
    {
      codec->control = 1; // DRAIN
      inDrain = true;
      pull(o.pull);
      pull(o.stall);
    }
    if (o.count && given >= o.count)
      break;
    if (o.seekAfter && !seeked && given == o.seekAfter)
    {
      // Kodi's seek: the demuxer to a key frame at or before the time of packet seekTo; the decoder reset
      seeked = true;
      int64_t target = 0;
      {
        AVFormatContext* scan = nullptr;
        avformat_open_input(&scan, path, nullptr, nullptr);
        avformat_find_stream_info(scan, nullptr);
        AVPacket* q = av_packet_alloc();
        unsigned k = 0;
        while (av_read_frame(scan, q) >= 0)
        {
          if (q->stream_index == index && ++k == o.seekTo)
          {
            target = q->pts;
            av_packet_unref(q);
            break;
          }
          av_packet_unref(q);
        }
        av_packet_free(&q);
        avformat_close_input(&scan);
      }
      if (av_seek_frame(fc, index, target, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
      if (annexb)
        av_bsf_flush(annexb);
      codec->Reset();
      sinceStart = 0;
      recovery = true;
      run.pics.push_back({0, INT64_MIN, false, 0}); // (the seek, in the list)
    }
  }
  // the end: VIDEO_DRAIN
  codec->control = 1;
  pull(1000000);
  run.resyncs = codec->resyncs;
  run.refused = codec->refused;
  run.unpaired = codec->unpaired;
  run.reasons = codec->reasons;
  run.dropped = static_cast<unsigned>(codec->restart.DroppedCount());
  run.restarts = codec->restart.Restarts();
  run.drainDropped = shim_fel_drain_dropped(codec->fel);
  av_packet_free(&p);
  av_bsf_free(&annexb);
  codec.reset();
  avformat_close_input(&fc);
  return true;
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
    return 2;
  Options o;
  bool units = false;
  for (int i = 2; i < argc; ++i)
  {
    const std::string a = argv[i];
    auto num = [&](const char* key, unsigned& v) {
      const size_t l = std::strlen(key);
      if (a.compare(0, l, key) != 0)
        return false;
      v = static_cast<unsigned>(std::atoi(a.c_str() + l));
      return true;
    };
    if (a == "mode=after")
      o.after = true;
    else if (a == "mode=before")
      o.after = false;
    else if (a == "annexb")
      o.annexb = true;
    else if (a == "oldfeldrain")
      o.oldFelDrain = true;
    else if (a == "units")
      units = true;
    else if (a.rfind("seek=", 0) == 0)
    {
      if (std::sscanf(a.c_str() + 5, "%u:%u", &o.seekAfter, &o.seekTo) != 2)
        return 2;
    }
    else if (!num("from=", o.from) && !num("count=", o.count) && !num("dry=", o.dry) && !num("pull=", o.pull) &&
             !num("stall=", o.stall) && !num("hw=", o.hw) && !num("jump=", o.jump))
      return 2;
  }
  av_log_set_level(AV_LOG_QUIET);
  if (units)
  {
    Units();
    std::printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
    return g_failures != 0;
  }
  Run ref, run;
  if (!Play(argv[1], o, false, ref) || !Play(argv[1], o, true, run))
  {
    std::printf("FAIL: the file could not be played\n");
    return 1;
  }
  // each picture is known by its packet; the order is the order of the reference
  std::map<unsigned, std::pair<const Pic*, unsigned>> refBy;
  unsigned refPaired = 0, index = 0;
  for (const Pic& p : ref.pics)
    if (p.pts != INT64_MIN)
    {
      refBy[p.n] = {&p, index++};
      refPaired += p.paired;
    }
  // (a picture presented before the last one that the drain gave out, whose packet came after the drain, goes out
  // after it: late, counted apart)
  unsigned drainLast = 0;
  for (const Pic& p : run.pics)
    if (p.drain && refBy.count(p.n))
      drainLast = std::max(drainLast, refBy[p.n].second);
  std::map<unsigned, unsigned> seen;
  unsigned extra = 0, unpairedNew = 0, unpairedDrain = 0, wrong = 0, order = 0, late = 0, paired = 0;
  unsigned last = 0;
  bool haveLast = false;
  for (const Pic& p : run.pics)
  {
    if (p.pts == INT64_MIN)
    {
      haveLast = false;
      continue;
    }
    ++seen[p.n];
    paired += p.paired;
    auto it = refBy.find(p.n);
    if (it == refBy.end())
    {
      ++extra;
      continue;
    }
    const unsigned at = it->second.second;
    if (haveLast && at <= last)
      ++*(!p.drain && at < drainLast ? &late : &order);
    if (!haveLast || at > last)
      last = at;
    haveLast = true;
    const Pic* r = it->second.first;
    unpairedNew += r->paired && !p.paired && !p.drain;
    unpairedDrain += r->paired && !p.paired && p.drain;
    wrong += p.paired && (!r->paired || r->layer != p.layer);
    if ((r->paired && !p.paired) || (p.paired && (!r->paired || r->layer != p.layer)))
      std::printf("differs: picture of packet %u (%.3f s), paired %d here, %d in the reference\n", p.n, p.pts / 1e6,
                  p.paired, r->paired);
  }
  unsigned lost = 0, twice = 0;
  for (const auto& [n, ref] : refBy)
  {
    lost += !seen.count(n);
    if (!seen.count(n))
      std::printf("lost: the picture of packet %u (%.3f s)\n", n, ref.first->pts / 1e6);
  }
  for (const auto& [n, count] : seen)
    twice += count > 1;
  std::printf("reference: %zu pictures, %u paired, %u resyncs; with the drain: %zu pictures, %u paired, %u went out "
              "without the layer; %u lost, %u twice, %u out of order, %u late (after a later picture that the drain "
              "gave out), %u not in the reference, %u paired in the reference and not here (and %u more that went out "
              "during the drain), %u paired with another enhancement picture; resyncs %u; packets the base decoder "
              "refused %u; restarts %u, pictures left out %u; access units a drain passed over %u\n",
              ref.pics.size(), refPaired, ref.resyncs, run.pics.size(), paired, run.unpaired, lost, twice, order, late,
              extra, unpairedNew, unpairedDrain, wrong, run.resyncs, run.refused, run.restarts, run.dropped,
              run.drainDropped);
  for (const auto& [why, count] : run.reasons)
    std::printf("resync: %u times: %s\n", count, why.c_str());
  Check(ref.resyncs == 0 && refPaired > 0, "the reference run pairs pictures, without a resynchronization");
  Check(lost == 0 && twice == 0 && order == 0 && extra == 0,
        "with the drain every picture of the reference goes out, once, in order (but those that come after a later "
        "picture the drain gave out)");
  Check(unpairedNew == 0 && wrong == 0,
        "with the drain every picture paired in the reference is paired, with the same enhancement picture (but "
        "those that went out during the drain)");
  // FEL_DRAIN_LOSS=N: at most N of those (the stream had not brought their enhancement access units yet)
  const unsigned allowed = getenv("FEL_DRAIN_LOSS") ? static_cast<unsigned>(std::atoi(getenv("FEL_DRAIN_LOSS"))) : 0;
  Check(unpairedDrain <= allowed, "the pictures that went out during the drain are paired as in the reference, but " +
                                      std::to_string(allowed) + " at most");
  Check(run.resyncs == 0, "with the drain the layer is not resynchronized");
  std::printf("%s\n", g_failures ? "FAILED" : "ALL PASSED");
  return g_failures != 0;
}
