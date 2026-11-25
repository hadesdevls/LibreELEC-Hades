// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of the depth measured from a 3D video (Kodi's StereoDepth.cpp) on real stereo pictures: the two
// views of an MV-HEVC conformance stream, decoded as Kodi decodes them (FFmpeg's HEVC decoder, view_ids "-1"),
// paired by their time, the base view taken as the left eye (as Kodi does where nothing says otherwise). Each pair
// is reduced as the graphics reduce it for the measure (CStereoDepthCapture: to twice 480 x 270 and then by 2 x 2
// averages) and measured as Kodi measures it, in the lower part of the picture (the subtitles' area before any
// subtitle was seen) and in the whole picture. The same pairs are measured at twice that size (960 x 540, with
// blocks, steps and search twice as large) as a reference of finer sampling.
// usage: mvhevc_depth <input> [swap]   (swap: the second view as the left eye)
// output: one line per pair "pair <n> area <z|-> whole <z|-> area2 <z|-> whole2 <z|->", then
//         "summary pairs <n> found <area> <whole> agree <area> <whole> worst <area> <whole> jitter <area> <whole>
//         ms <one measure>", the depths as shares of the width (crossed disparity, nearer is more).
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "StereoDepth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

using namespace KODI::STEREODEPTH;

namespace
{
struct Luma
{
  int width{0};
  int height{0};
  std::vector<float> v;
};

Luma LumaOf(const AVFrame* f)
{
  const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
  const int depth = d->comp[0].depth;
  Luma l{f->width, f->height, std::vector<float>(static_cast<size_t>(f->width) * f->height)};
  for (int y = 0; y < f->height; ++y)
    for (int x = 0; x < f->width; ++x)
    {
      const uint8_t* row = f->data[0] + static_cast<size_t>(y) * f->linesize[0];
      const int v = depth > 8 ? reinterpret_cast<const uint16_t*>(row)[x] : row[x];
      l.v[static_cast<size_t>(y) * f->width + x] = static_cast<float>(v) / static_cast<float>(1 << (depth - 8));
    }
  return l;
}

// the mean of each area of the source an output pixel covers (fractions of pixels counted as such)
Luma Area2(const Luma& in, int w, int h)
{
  Luma out{w, h, std::vector<float>(static_cast<size_t>(w) * h)};
  const double sx = static_cast<double>(in.width) / w;
  const double sy = static_cast<double>(in.height) / h;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
    {
      double sum = 0.0;
      double weight = 0.0;
      const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
      for (int j = static_cast<int>(y0); j < static_cast<int>(std::ceil(y1)) && j < in.height; ++j)
      {
        const double wy = std::min(y1, j + 1.0) - std::max(y0, static_cast<double>(j));
        for (int i = static_cast<int>(x0); i < static_cast<int>(std::ceil(x1)) && i < in.width; ++i)
        {
          const double wx = std::min(x1, i + 1.0) - std::max(x0, static_cast<double>(i));
          sum += wx * wy * in.v[static_cast<size_t>(j) * in.width + i];
          weight += wx * wy;
        }
      }
      out.v[static_cast<size_t>(y) * w + x] = static_cast<float>(sum / weight);
    }
  return out;
}

std::vector<uint8_t> Bytes(const Luma& l)
{
  std::vector<uint8_t> b(l.v.size());
  for (size_t i = 0; i < b.size(); ++i)
    b[i] = static_cast<uint8_t>(std::clamp(std::lround(l.v[i]), 0L, 255L));
  return b;
}

struct Result
{
  std::optional<float> area, whole, area2, whole2;
};

const Area SUBS{0.1f, 0.6f, 0.9f, 1.0f};
const Area WHOLE{0.02f, 0.02f, 0.98f, 0.98f};
double g_ms = 0.0;
int g_timed = 0;

Result Measure2(const Luma& left, const Luma& right)
{
  Result r;
  // as the graphics: to twice 480 x 270, then 2 x 2 averages
  const Luma l960 = Area2(left, 960, 540), r960 = Area2(right, 960, 540);
  const Luma l480 = Area2(l960, 480, 270), r480 = Area2(r960, 480, 270);
  const std::vector<uint8_t> a = Bytes(l480), b = Bytes(r480);
  const Plane pl{a.data(), 480, 270, 480}, pr{b.data(), 480, 270, 480};
  MatchParams whole;
  whole.step = 16;
  const auto t0 = std::chrono::steady_clock::now();
  r.area = MeasureNearest(pl, pr, SUBS).nearest;
  r.whole = MeasureNearest(pl, pr, WHOLE, whole).nearest;
  g_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  ++g_timed;
  // the reference: twice as fine
  const std::vector<uint8_t> c = Bytes(l960), d = Bytes(r960);
  const Plane ql{c.data(), 960, 540, 960}, qr{d.data(), 960, 540, 960};
  MatchParams fine;
  fine.block = 16;
  fine.step = 16;
  fine.nearest = 64;
  fine.farthest = 32;
  MatchParams fineWhole = fine;
  fineWhole.step = 32;
  r.area2 = MeasureNearest(ql, qr, SUBS, fine).nearest;
  r.whole2 = MeasureNearest(ql, qr, WHOLE, fineWhole).nearest;
  return r;
}

void Print(const char* name, const std::optional<float>& v)
{
  if (v)
    std::printf(" %s %.5f", name, static_cast<double>(*v));
  else
    std::printf(" %s -", name);
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
    return 2;
  const bool swap = argc > 2 && std::strcmp(argv[2], "swap") == 0;
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0)
    return 2;
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (si < 0)
    return 2;
  const AVCodec* codec = avcodec_find_decoder(fmt->streams[si]->codecpar->codec_id);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
  ctx->thread_count = 1;
  if (av_opt_set(ctx, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN) < 0 || avcodec_open2(ctx, codec, nullptr) < 0)
    return 3;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  std::vector<Result> results;
  std::optional<Luma> first;
  int64_t firstPts = AV_NOPTS_VALUE;
  int baseId = -1;
  auto take = [&](const AVFrame* frame) {
    const AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIEW_ID);
    const int id = sd ? *reinterpret_cast<const int*>(sd->data) : 0;
    if (baseId < 0)
      baseId = id;
    if (id == baseId)
    {
      first = LumaOf(frame);
      firstPts = frame->pts;
      return;
    }
    if (!first || frame->pts != firstPts)
      return;
    Luma other = LumaOf(frame);
    if (other.width != first->width || other.height != first->height)
      return;
    results.push_back(swap ? Measure2(other, *first) : Measure2(*first, other));
    first.reset();
  };
  for (bool eof = false; !eof;)
  {
    if (av_read_frame(fmt, pkt) < 0)
    {
      eof = true;
      avcodec_send_packet(ctx, nullptr);
    }
    else
    {
      if (pkt->stream_index == si)
        avcodec_send_packet(ctx, pkt);
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, f) == 0)
    {
      take(f);
      av_frame_unref(f);
    }
  }

  int found[2]{}, agree[2]{};
  double worst[2]{}, jitter[2]{};
  int steps[2]{};
  std::optional<float> before[2];
  for (size_t i = 0; i < results.size(); ++i)
  {
    const Result& r = results[i];
    std::printf("pair %zu", i);
    Print("area", r.area);
    Print("whole", r.whole);
    Print("area2", r.area2);
    Print("whole2", r.whole2);
    std::printf("\n");
    const std::optional<float>* m[2] = {&r.area, &r.whole};
    const std::optional<float>* ref[2] = {&r.area2, &r.whole2};
    for (int k = 0; k < 2; ++k)
    {
      if (!*m[k] || !*ref[k])
        continue;
      ++found[k];
      const double diff = std::fabs(static_cast<double>(**m[k] - **ref[k]));
      worst[k] = std::max(worst[k], diff);
      agree[k] += diff <= 0.0025;
      if (before[k])
      {
        jitter[k] += std::fabs(static_cast<double>(**m[k] - *before[k]));
        ++steps[k];
      }
      before[k] = **m[k];
    }
  }
  std::printf("summary pairs %zu found %d %d agree %d %d worst %.5f %.5f jitter %.5f %.5f ms %.2f\n", results.size(),
              found[0], found[1], agree[0], agree[1], worst[0], worst[1], steps[0] ? jitter[0] / steps[0] : 0.0,
              steps[1] ? jitter[1] / steps[1] : 0.0, g_timed ? g_ms / g_timed : 0.0);
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return 0;
}
