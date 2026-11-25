// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// A Matroska file of MV-HEVC whose private data lacks the parameter sets of the second view (the muxers of FFmpeg
// and mkvmerge keep the base layer's only), as Kodi's demuxer takes it (xbmc/cores/VideoPlayer/MultiviewChoice.h:
// LookInPacket(), HvccWithSets(), MvHevcSecondLayer(); CDVDDemuxFFmpeg::LookForSecondViewSets()):
//   found  a file made of an MP4 file of 'hev1', whose packets carry the sets in band: they are found in its first
//          packets, before the first picture of the second view; the private data with them added says two views;
//          the file decoded from its start with that private data, as Kodi opens it (FFmpeg's HEVC decoder,
//          view_ids "-1", frame threads), gives each view as the reference decoder does. (A file with the sets in
//          its CodecPrivate cannot be written here: FFmpeg's Matroska muxer writes the record again without them.
//          That record is the one an MP4 file gives, which the stages choice and pack check.)
//   none   a file made of an MP4 file of 'hvc1' (the sets out of band, in 'lhvC', which the Matroska muxer drops):
//          they are not there, and the track stays 2D
// usage: mvhevc_mkv_test <file.mkv> found <md5 of the base view> <md5 of the second view>
//        mvhevc_mkv_test <file.mkv> none
#include "MultiviewChoice.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace KODI::MULTIVIEW;

static int checks = 0, failed = 0;
#define CHECK(cond, ...) \
  do \
  { \
    ++checks; \
    if (!(cond)) \
    { \
      ++failed; \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__); \
      std::printf("\n"); \
    } \
  } while (0)

using Bytes = std::vector<uint8_t>;

struct Opened
{
  AVFormatContext* fmt{nullptr};
  int index{-1};
  Bytes extradata;
};

static Opened Open(const std::string& path)
{
  Opened o;
  if (avformat_open_input(&o.fmt, path.c_str(), nullptr, nullptr) < 0 ||
      avformat_find_stream_info(o.fmt, nullptr) < 0)
    return o;
  o.index = av_find_best_stream(o.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (o.index >= 0)
  {
    const AVCodecParameters* par = o.fmt->streams[o.index]->codecpar;
    o.extradata.assign(par->extradata, par->extradata + par->extradata_size);
  }
  return o;
}

//! the MD5 of each view of the file, decoded from its start with this private data, in the order in which the
//! views first come (the base view first; their view ids are what the stream makes them)
static std::vector<std::string> Views(const std::string& path, const Bytes& extradata)
{
  std::vector<std::string> out;
  std::vector<int> order;
  Opened o = Open(path);
  if (o.index < 0)
    return out;
  const AVCodec* codec = avcodec_find_decoder_by_name(HEVC_DECODER_NAME);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, o.fmt->streams[o.index]->codecpar);
  av_freep(&ctx->extradata);
  ctx->extradata = static_cast<uint8_t*>(av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
  std::memcpy(ctx->extradata, extradata.data(), extradata.size());
  ctx->extradata_size = static_cast<int>(extradata.size());
  ctx->thread_count = 4;
  av_opt_set(ctx, DECODER_VIEWS_OPTION, DECODER_ALL_VIEWS, AV_OPT_SEARCH_CHILDREN);
  if (avcodec_open2(ctx, codec, nullptr) < 0)
    return out;
  std::map<int, AVMD5*> md5;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  for (bool eof = false; !eof;)
  {
    if (av_read_frame(o.fmt, pkt) < 0)
    {
      eof = true;
      avcodec_send_packet(ctx, nullptr);
    }
    else
    {
      if (pkt->stream_index == o.index)
        avcodec_send_packet(ctx, pkt);
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, f) == 0)
    {
      int view = 0;
      if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID))
        std::memcpy(&view, sd->data, sizeof(int));
      if (!md5[view])
      {
        order.push_back(view);
        md5[view] = av_md5_alloc();
        av_md5_init(md5[view]);
      }
      const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
      const int bytes = d->comp[0].depth > 8 ? 2 : 1;
      for (int p = 0; p < 3; ++p)
      {
        const int w = p ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width;
        const int h = p ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
        for (int y = 0; y < h; ++y)
          av_md5_update(md5[view], f->data[p] + static_cast<size_t>(y) * f->linesize[p],
                        static_cast<size_t>(w) * bytes);
      }
      av_frame_unref(f);
    }
  }
  for (int view : order)
  {
    uint8_t sum[16];
    av_md5_final(md5[view], sum);
    char hex[33];
    for (int i = 0; i < 16; ++i)
      std::snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    out.push_back(hex);
    av_free(md5[view]);
  }
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&o.fmt);
  return out;
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  av_log_set_level(AV_LOG_QUIET);
  const std::string path = argv[1];
  const std::string want = argv[2];
  Opened o = Open(path);
  CHECK(o.index >= 0, "%s: no video", path.c_str());
  if (o.index < 0)
    return 1;
  const Bytes& ed = o.extradata;
  const auto declared = MvHevcDeclaredLayer(ed.data(), ed.size());
  const auto lengthSize = HvccLengthSize(ed.data(), ed.size());
  CHECK(declared == 1, "%s: the private data does not declare the second view in layer 1", path.c_str());
  CHECK(lengthSize.has_value(), "%s: the private data is no 'hvcC' record", path.c_str());
  CHECK(!MvHevcSecondLayer(ed.data(), ed.size()),
        "%s: the private data has the second view's sets already: no case of the search", path.c_str());
  if (!declared || !lengthSize)
    return 1;

  InBandSearch search;
  search.layer = *declared;
  search.lengthSize = *lengthSize;
  InBand found = InBand::LOOKING;
  AVPacket* pkt = av_packet_alloc();
  while (found == InBand::LOOKING && av_read_frame(o.fmt, pkt) >= 0)
  {
    if (pkt->stream_index == o.index)
      found = LookInPacket(search, pkt->data, static_cast<size_t>(pkt->size));
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&o.fmt);

  if (want == "none")
  {
    CHECK(found == InBand::NOT_THERE, "%s: the sets found where there are none (%d)", path.c_str(),
          static_cast<int>(found));
    std::printf("%s: not in its packets either (%u packets looked through): 2D\n", path.c_str(), search.packets);
  }
  else
  {
    CHECK(found == InBand::FOUND, "%s: the sets not found in its packets (%d after %u)", path.c_str(),
          static_cast<int>(found), search.packets);
    const Bytes record = HvccWithSets(ed.data(), ed.size(), search);
    CHECK(MvHevcSecondLayer(record.data(), record.size()) == 1, "%s: the private data made is not two views",
          path.c_str());
    const std::vector<std::string> views = Views(path, record);
    CHECK(views.size() == 2 && views[0] == argv[3] && views[1] == argv[4], "%s: %zu views, %s %s", path.c_str(),
          views.size(), views.empty() ? "" : views[0].c_str(), views.size() > 1 ? views[1].c_str() : "");
    std::printf("%s: %zu SPS and %zu PPS of the second view in its first %u packet(s); decoded with them: both "
                "views as the reference\n",
                path.c_str(), search.sps.size(), search.pps.size(), search.packets);
  }
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
