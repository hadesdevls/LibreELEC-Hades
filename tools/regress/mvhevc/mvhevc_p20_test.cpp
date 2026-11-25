// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// A Dolby Vision profile 20 3D file (mkp20.py: an ITU-T MV-HEVC conformance stream with a profile 5 RPU in each access
// unit and a 'dvvC' of profile 20, base layer compatibility 0) through what Kodi of Hades does with it before the
// bridge (TEST ONLY):
//   - the file says profile 20 with compatibility 0 (FFmpeg's demuxer reads 'dvvC');
//   - Kodi's choice (MultiviewChoice.h, KODI::MULTIVIEW::TwoViews()) decodes both views where the window system can
//     rebuild Dolby Vision in 3D (rebuiltIn3D), and its base view alone elsewhere;
//   - FFmpeg's decoder, opened as Kodi opens it (view_ids "-1"), gives both views of every access unit the Dolby
//     Vision metadata of the RPU (the one of the base layer: FFmpeg reads no other), the same for both;
//   - Kodi's packer (MultiviewPacker.cpp) keeps those metadata on the picture of the two views, as they were on the
//     base view: what the bridge rebuilds each view from;
//   - the RPU changes no picture: each half is the view of the same file made without it (the MD5 of each half, as
//     mvhevc_views gives them for that file, in the order the packer puts them, as mvhevc_pack_test.cpp has it).
// usage: mvhevc_p20_test <file> <md5 of the left half> <md5 of the right half>
#include "cores/VideoPlayer/MultiviewChoice.h"
#include "MultiviewPacker.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <cstdio>
#include <cstring>
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

static std::string Hex(AVMD5* md5)
{
  uint8_t sum[16];
  av_md5_final(md5, sum);
  char out[33];
  for (int i = 0; i < 16; ++i)
    std::snprintf(out + 2 * i, 3, "%02x", sum[i]);
  return out;
}

static void HalfInto(AVMD5* md5, const AVFrame* f, int half)
{
  const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
  const int bytes = d->comp[0].depth > 8 ? 2 : 1;
  for (int p = 0; p < 3; ++p)
  {
    const int w = (p ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width) / 2;
    const int h = p ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
    for (int y = 0; y < h; ++y)
      av_md5_update(md5, f->data[p] + static_cast<size_t>(y) * f->linesize[p] + half * w * bytes,
                    static_cast<size_t>(w) * bytes);
  }
}

static std::vector<uint8_t> Dovi(const AVFrame* f)
{
  const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DOVI_METADATA);
  if (!sd)
    return {};
  return std::vector<uint8_t>(sd->data, sd->data + sd->size);
}

int main(int argc, char** argv)
{
  if (argc != 4)
    return 2;
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0)
  {
    std::printf("FAIL cannot open %s\n", argv[1]);
    return 1;
  }
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodecParameters* par = fmt->streams[si]->codecpar;
  const AVPacketSideData* conf =
      av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
  const auto* dovi = conf ? reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(conf->data) : nullptr;
  CHECK(dovi && dovi->dv_profile == 20 && dovi->dv_bl_signal_compatibility_id == 0 && dovi->rpu_present_flag &&
            dovi->bl_present_flag && !dovi->el_present_flag,
        "the file does not say profile 20, compatibility 0 (%d, %d)", dovi ? dovi->dv_profile : -1,
        dovi ? dovi->dv_bl_signal_compatibility_id : -1);

  // --- Kodi's choice
  Track t;
  t.hevc = true;
  t.allowed = true;
  t.decoder = DecoderOfTwoViews(AV_CODEC_ID_HEVC) != nullptr;
  t.extradata = par->extradata;
  t.extradataSize = static_cast<size_t>(par->extradata_size);
  t.dvProfile = dovi ? dovi->dv_profile : 0;
  t.dvCompatibility = dovi ? dovi->dv_bl_signal_compatibility_id : 0;
  CHECK(MvHevcSecondLayer(par->extradata, static_cast<size_t>(par->extradata_size)),
        "the private data does not carry the second view's sets");
  t.rebuiltIn3D = true;
  CHECK(TwoViews(t).has_value(), "where Dolby Vision is rebuilt in 3D: not decoded as two views");
  t.rebuiltIn3D = false;
  CHECK(!TwoViews(t).has_value(), "where it cannot be: decoded as two views (its base layer would be shown as it is)");

  // --- decoded as Kodi decodes it, and put together by its packer
  const AVCodec* codec = DecoderOfTwoViews(AV_CODEC_ID_HEVC);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, par);
  ctx->thread_count = 1;
  CHECK(av_opt_set(ctx, DECODER_VIEWS_OPTION, DECODER_ALL_VIEWS, AV_OPT_SEARCH_CHILDREN) >= 0, "view_ids refused");
  avcodec_open2(ctx, codec, nullptr);
  CMultiviewPacker packer(BaseEye::LEFT, true);
  std::map<int64_t, std::vector<uint8_t>> baseMeta;
  int views = 0, viewsWith = 0, viewsSame = 0, pictures = 0, picturesWith = 0, picturesAsBase = 0;
  int64_t packets = 0;
  AVMD5* l = av_md5_alloc();
  AVMD5* r = av_md5_alloc();
  av_md5_init(l);
  av_md5_init(r);
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  auto out = [&](CMultiviewPacker::Result made)
  {
    if (made != CMultiviewPacker::Result::PICTURE)
      return;
    ++pictures;
    const std::vector<uint8_t> m = Dovi(f);
    picturesWith += !m.empty();
    const auto base = baseMeta.find(f->pts);
    picturesAsBase += base != baseMeta.end() && !m.empty() && base->second == m;
    HalfInto(l, f, 0);
    HalfInto(r, f, 1);
    av_frame_unref(f);
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
      if (pkt->pts == AV_NOPTS_VALUE)
        pkt->pts = pkt->dts = packets;
      ++packets;
      if (pkt->stream_index == si)
        packer.Sent(avcodec_send_packet(ctx, pkt) < 0);
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, f) == 0)
    {
      ++views;
      const std::vector<uint8_t> m = Dovi(f);
      viewsWith += !m.empty();
      // (the base layer's picture of an access unit comes first, as the decoder gives them: its view id is the
      // stream's own, not 0 in every stream)
      const auto base = baseMeta.find(f->pts);
      if (base == baseMeta.end())
        baseMeta[f->pts] = m;
      else
        viewsSame += base->second == m && !m.empty();
      out(packer.Add(f));
    }
  }
  out(packer.Flush(f));
  const std::string left = Hex(l), right = Hex(r);
  std::printf("views %d (with Dolby Vision metadata %d, second views as their base view %d); pictures of two %d "
              "(with them %d, as the base view's %d); paired %llu alone %llu\n",
              views, viewsWith, viewsSame, pictures, picturesWith, picturesAsBase,
              static_cast<unsigned long long>(packer.Paired()), static_cast<unsigned long long>(packer.Alone()));
  CHECK(views > 0 && viewsWith == views, "a view without Dolby Vision metadata (%d of %d)", views - viewsWith, views);
  CHECK(pictures > 0 && 2 * viewsSame == views, "a second view with other metadata than its base view (%d of %d)",
        views / 2 - viewsSame, views / 2);
  CHECK(pictures > 0 && pictures == views / 2 && picturesWith == pictures && picturesAsBase == pictures,
        "a picture of two views without the metadata of its base view (%d of %d)", pictures - picturesAsBase,
        pictures);
  CHECK(left == argv[2] && right == argv[3], "the views are not those of the file without the RPU: %s %s", left.c_str(),
        right.c_str());
  av_free(l);
  av_free(r);
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
