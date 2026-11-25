// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Kodi's picture of the two views of MV-HEVC (CMultiviewPacker, xbmc/cores/VideoPlayer/DVDCodecs/Video/
// MultiviewPacker.cpp, and the decoder it is opened with, KODI::MULTIVIEW::DecoderOfTwoViews()) on a host FFmpeg:
//   conformance  each ITU-T MV-HEVC stream (and files made of them), decoded as Kodi opens the decoder (FFmpeg's
//                own HEVC decoder, view_ids "-1", frame threads), its pictures put together by the packer: the left
//                half of every picture is the base view, the right half the second view, and the MD5 of each half
//                over the stream is the reference decoder's (<stream>.md5); a stream whose second view the decoder
//                cannot give (D: views of two sizes) plays its base view in both halves, never two pictures that
//                do not belong together
//   sizes        made-up 10-bit pictures (yuv420p10le, as an HDR or Dolby Vision MV-HEVC film has them) of 1920x1080
//                and 3840x2160 per eye: the picture of the two is twice as wide, its pixels half as wide, each half
//                the bytes of its view; a base view that says it is the right eye goes right; the side data of one
//                view is not left on the picture of the two
//   nvdec        the views as NVIDIA's decoder gives them copied from the card: the conformance streams in NV12 (each
//                half the reference decoder's view), made-up 10-bit views in P010 at 1080p and 2160p per eye (into
//                yuv420p10, each half its view), and how long a picture takes
//   disparity    each eye moved by half of a disparity adjustment, both signs and none, limited and full range, in
//                yuv420p10, P010 and NV12, sample by sample, the edge black and without colour
//   pace         pictures that go out while the player is behind: the second view is given up as the processor that
//                does not keep up, and the pace starts anew after a seek
// usage: mvhevc_pack_test <folder of MVHEVCS_*.bit and .md5> [<file> <md5 of view 0> <md5 of view 1>]...
#include "MultiviewPacker.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/stereo3d.h>
#include <libavutil/time.h>
}

#include <cstdio>
#include <cstring>
#include <fstream>
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

//! one half of a picture of two views into an MD5, as a planar YUV file has it
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

struct Run
{
  int pictures = 0;
  std::string left, right;
  uint64_t paired = 0, alone = 0;
  bool givenUp = false;
};

static Run Decode(const std::string& path, int threads)
{
  Run run;
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0 ||
      avformat_find_stream_info(fmt, nullptr) < 0)
    return run;
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodec* codec = DecoderOfTwoViews(AV_CODEC_ID_HEVC);
  CHECK(codec != nullptr, "no decoder of two views for HEVC");
  if (!codec || si < 0)
    return run;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
  ctx->thread_count = threads;
  CHECK(av_opt_set(ctx, DECODER_VIEWS_OPTION, DECODER_ALL_VIEWS, AV_OPT_SEARCH_CHILDREN) >= 0,
        "view_ids refused");
  avcodec_open2(ctx, codec, nullptr);
  CMultiviewPacker packer(BaseEye::LEFT, true);
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
    ++run.pictures;
    HalfInto(l, f, 0);
    HalfInto(r, f, 1);
    av_frame_unref(f);
  };
  int64_t packets = 0;
  for (bool eof = false; !eof;)
  {
    if (av_read_frame(fmt, pkt) < 0)
    {
      eof = true;
      avcodec_send_packet(ctx, nullptr);
    }
    else
    {
      // (a stream without times, as the conformance streams are: each access unit is given the number of
      // its packet, which both of its views then carry, as a container's time would)
      if (pkt->pts == AV_NOPTS_VALUE)
        pkt->pts = pkt->dts = packets;
      ++packets;
      if (pkt->stream_index == si)
        packer.Sent(avcodec_send_packet(ctx, pkt) < 0);
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, f) == 0)
      out(packer.Add(f));
  }
  out(packer.Flush(f));
  run.left = Hex(l);
  run.right = Hex(r);
  run.paired = packer.Paired();
  run.alone = packer.Alone();
  run.givenUp = packer.GivenUp();
  av_free(l);
  av_free(r);
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return run;
}

static std::string RefMd5(const std::string& file, const std::string& yuv)
{
  std::ifstream in(file);
  std::string sum, name;
  while (in >> sum >> name)
    if (name == yuv)
      return sum;
  return "";
}

static void Conformance(const std::string& dir)
{
  for (const char* n : {"A", "B", "C", "E", "F"})
  {
    const std::string base = dir + "/MVHEVCS_" + n;
    for (int threads : {1, 8})
    {
      const Run run = Decode(base + ".bit", threads);
      CHECK(run.left == RefMd5(base + ".md5", std::string("MVHEVCS_") + n + "_0.yuv"),
            "%s threads %d: left half %s is not the base view", n, threads, run.left.c_str());
      CHECK(run.right == RefMd5(base + ".md5", std::string("MVHEVCS_") + n + "_1.yuv"),
            "%s threads %d: right half %s is not the second view", n, threads, run.right.c_str());
      CHECK(run.alone == 0 && run.paired == static_cast<uint64_t>(run.pictures) && !run.givenUp,
            "%s threads %d: %llu paired, %llu alone, given up %d", n, threads,
            static_cast<unsigned long long>(run.paired), static_cast<unsigned long long>(run.alone),
            run.givenUp);
      std::printf("%s threads %d: %d pictures of two views, halves as the reference\n", n, threads,
                  run.pictures);
    }
  }
  // D: a second view of another size, which the decoder refuses: the base view alone, in both halves
  {
    const std::string base = dir + "/MVHEVCS_D";
    // (one thread: with frame threads FFmpeg drops base pictures of packets it refuses, which Kodi gives up
    // after REFUSED_UNTIL_GIVEN_UP of them anyway)
    const Run run = Decode(base + ".bit", 1);
    CHECK(run.paired == 0 && run.alone > 0, "D: %llu paired, %llu alone",
          static_cast<unsigned long long>(run.paired), static_cast<unsigned long long>(run.alone));
    CHECK(run.left == RefMd5(base + ".md5", "MVHEVCS_D_0.yuv") && run.right == run.left,
          "D: the halves are not the base view");
    std::printf("D: %d pictures, the base view in both halves\n", run.pictures);
  }
}

static AVFrame* Made(int w, int h, int view, int64_t pts, int seed, int eye)
{
  AVFrame* f = av_frame_alloc();
  f->format = AV_PIX_FMT_YUV420P10LE;
  f->width = w;
  f->height = h;
  f->pts = pts;
  f->sample_aspect_ratio = AVRational{1, 1};
  av_frame_get_buffer(f, 0);
  for (int p = 0; p < 3; ++p)
  {
    const int pw = p ? w / 2 : w, ph = p ? h / 2 : h;
    for (int y = 0; y < ph; ++y)
    {
      auto* row = reinterpret_cast<uint16_t*>(f->data[p] + static_cast<size_t>(y) * f->linesize[p]);
      for (int x = 0; x < pw; ++x)
        row[x] = static_cast<uint16_t>((x * 7 + y * 13 + p * 101 + seed * 257) & 0x3ff);
    }
  }
  AVFrameSideData* sd = av_frame_new_side_data(f, AV_FRAME_DATA_VIEW_ID, sizeof(int));
  std::memcpy(sd->data, &view, sizeof(int));
  if (eye)
  {
    AVStereo3D* s = av_stereo3d_create_side_data(f);
    s->type = AV_STEREO3D_FRAMESEQUENCE;
    s->view = eye == 1 ? AV_STEREO3D_VIEW_LEFT : AV_STEREO3D_VIEW_RIGHT;
  }
  return f;
}

static bool HalfIs(const AVFrame* packed, int half, const AVFrame* view)
{
  for (int p = 0; p < 3; ++p)
  {
    const int bytes = (p ? view->width / 2 : view->width) * 2;
    const int ph = p ? view->height / 2 : view->height;
    for (int y = 0; y < ph; ++y)
      if (std::memcmp(packed->data[p] + static_cast<size_t>(y) * packed->linesize[p] + half * bytes,
                      view->data[p] + static_cast<size_t>(y) * view->linesize[p], bytes))
        return false;
  }
  return true;
}

static void Sizes()
{
  for (auto [w, h] : {std::pair{1920, 1080}, std::pair{3840, 2160}})
  {
    for (int eye : {0, 1, 2}) // the base view says nothing, left, right
    {
      CMultiviewPacker packer(BaseEye::LEFT, true);
      AVFrame* base = Made(w, h, 0, 1000, 1, eye);
      AVFrame* other = Made(w, h, 1, 1000, 2, 0);
      AVFrame* keepBase = av_frame_clone(base);
      AVFrame* keepOther = av_frame_clone(other);
      CHECK(packer.Add(base) == CMultiviewPacker::Result::NONE, "%dx%d: the base view went out alone", w,
            h);
      CHECK(packer.Add(other) == CMultiviewPacker::Result::PICTURE, "%dx%d: no picture of two", w, h);
      AVFrame* packed = other; // (the frame holds the picture that goes out)
      CHECK(packed->width == 2 * w && packed->height == h, "%dx%d: packed %dx%d", w, h, packed->width,
            packed->height);
      CHECK(packed->sample_aspect_ratio.num == 1 && packed->sample_aspect_ratio.den == 2,
            "%dx%d: pixels %d:%d", w, h, packed->sample_aspect_ratio.num,
            packed->sample_aspect_ratio.den);
      const bool baseRight = eye == 2;
      CHECK(HalfIs(packed, baseRight ? 1 : 0, keepBase), "%dx%d eye %d: base view not in its half", w, h,
            eye);
      CHECK(HalfIs(packed, baseRight ? 0 : 1, keepOther), "%dx%d eye %d: second view not in its half", w,
            h, eye);
      CHECK(!av_frame_get_side_data(packed, AV_FRAME_DATA_VIEW_ID) &&
                !av_frame_get_side_data(packed, AV_FRAME_DATA_STEREO3D),
            "%dx%d: side data of one view left", w, h);
      const AVDictionaryEntry* mode = av_dict_get(packed->metadata, "stereo_mode", nullptr, 0);
      CHECK(mode && std::string(mode->value) == PACKED_STEREO_MODE, "%dx%d: stereo_mode", w, h);
      // the eye said by the picture is believed only where the packer is told so (H.264 MVC: never)
      CMultiviewPacker mvc(BaseEye::LEFT, false);
      AVFrame* b2 = Made(w, h, 0, 2000, 1, 2);
      AVFrame* o2 = Made(w, h, 1, 2000, 2, 0);
      mvc.Add(b2);
      CHECK(mvc.Add(o2) == CMultiviewPacker::Result::PICTURE && HalfIs(o2, 0, keepBase),
            "%dx%d: an MVC packer took the eye of the picture", w, h);
      av_frame_free(&b2);
      av_frame_free(&o2);
      av_frame_free(&base);
      av_frame_free(&other);
      av_frame_free(&keepBase);
      av_frame_free(&keepOther);
    }
    std::printf("%dx%d per eye, 10-bit: packed as two views, each half its view\n", w, h);
  }
}

//! a picture of yuv420p10 as NVIDIA's decoder gives it, copied from the card: P010 (each sample in the high
//! bits of 16, U and V interleaved), with the side data and the times of the picture
static AVFrame* P010Of(const AVFrame* in)
{
  AVFrame* f = av_frame_alloc();
  f->format = AV_PIX_FMT_P010LE;
  f->width = in->width;
  f->height = in->height;
  av_frame_get_buffer(f, 0);
  av_frame_copy_props(f, in);
  for (int y = 0; y < in->height; ++y)
  {
    const auto* s = reinterpret_cast<const uint16_t*>(in->data[0] + static_cast<size_t>(y) * in->linesize[0]);
    auto* d = reinterpret_cast<uint16_t*>(f->data[0] + static_cast<size_t>(y) * f->linesize[0]);
    for (int x = 0; x < in->width; ++x)
      d[x] = static_cast<uint16_t>(s[x] << 6);
  }
  for (int y = 0; y < in->height / 2; ++y)
  {
    const auto* u = reinterpret_cast<const uint16_t*>(in->data[1] + static_cast<size_t>(y) * in->linesize[1]);
    const auto* v = reinterpret_cast<const uint16_t*>(in->data[2] + static_cast<size_t>(y) * in->linesize[2]);
    auto* d = reinterpret_cast<uint16_t*>(f->data[1] + static_cast<size_t>(y) * f->linesize[1]);
    for (int x = 0; x < in->width / 2; ++x)
    {
      d[2 * x] = static_cast<uint16_t>(u[x] << 6);
      d[2 * x + 1] = static_cast<uint16_t>(v[x] << 6);
    }
  }
  return f;
}

//! ... and an 8-bit one: NV12
static AVFrame* Nv12Of(const AVFrame* in)
{
  AVFrame* f = av_frame_alloc();
  f->format = AV_PIX_FMT_NV12;
  f->width = in->width;
  f->height = in->height;
  av_frame_get_buffer(f, 0);
  av_frame_copy_props(f, in);
  av_image_copy_plane(f->data[0], f->linesize[0], in->data[0], in->linesize[0], in->width, in->height);
  for (int y = 0; y < (in->height + 1) / 2; ++y)
  {
    const uint8_t* u = in->data[1] + static_cast<size_t>(y) * in->linesize[1];
    const uint8_t* v = in->data[2] + static_cast<size_t>(y) * in->linesize[2];
    uint8_t* d = f->data[1] + static_cast<size_t>(y) * f->linesize[1];
    for (int x = 0; x < (in->width + 1) / 2; ++x)
    {
      d[2 * x] = u[x];
      d[2 * x + 1] = v[x];
    }
  }
  return f;
}

//! one half of a picture of NV12 into an MD5, as a planar YUV file has it (Y, then U, then V)
static void HalfIntoNv12(AVMD5* md5, const AVFrame* f, int half)
{
  const int w = f->width / 2;
  for (int y = 0; y < f->height; ++y)
    av_md5_update(md5, f->data[0] + static_cast<size_t>(y) * f->linesize[0] + half * w, w);
  for (int c = 0; c < 2; ++c)
    for (int y = 0; y < (f->height + 1) / 2; ++y)
    {
      const uint8_t* row = f->data[1] + static_cast<size_t>(y) * f->linesize[1] + half * w;
      std::vector<uint8_t> plane(static_cast<size_t>(w / 2));
      for (int x = 0; x < w / 2; ++x)
        plane[x] = row[2 * x + c];
      av_md5_update(md5, plane.data(), plane.size());
    }
}

// The conformance streams with their 8-bit views given to the packer as NVIDIA's decoder gives them (NV12): the
// halves of the pictures of NV12 are each view as the reference decoder's
static void ConformanceNv12(const std::string& dir)
{
  for (const char* n : {"A", "E"})
  {
    const std::string base = dir + "/MVHEVCS_" + n;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, (base + ".bit").c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(fmt, nullptr) < 0)
    {
      CHECK(false, "%s: not opened", n);
      continue;
    }
    const AVCodec* codec = DecoderOfTwoViews(AV_CODEC_ID_HEVC);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[0]->codecpar);
    ctx->thread_count = 4;
    av_opt_set(ctx, DECODER_VIEWS_OPTION, DECODER_ALL_VIEWS, AV_OPT_SEARCH_CHILDREN);
    avcodec_open2(ctx, codec, nullptr);
    CMultiviewPacker packer(BaseEye::LEFT, true);
    AVMD5* l = av_md5_alloc();
    AVMD5* r = av_md5_alloc();
    av_md5_init(l);
    av_md5_init(r);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    int pictures = 0, notNv12 = 0;
    int64_t packets = 0;
    // (the frame given to the packer holds the picture that goes out)
    auto out = [&](CMultiviewPacker::Result made, AVFrame* picture)
    {
      if (made != CMultiviewPacker::Result::PICTURE)
        return;
      ++pictures;
      if (picture->format != AV_PIX_FMT_NV12)
        ++notNv12;
      HalfIntoNv12(l, picture, 0);
      HalfIntoNv12(r, picture, 1);
      av_frame_unref(picture);
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
        pkt->pts = pkt->dts = packets++;
        packer.Sent(avcodec_send_packet(ctx, pkt) < 0);
        av_packet_unref(pkt);
      }
      while (avcodec_receive_frame(ctx, f) == 0)
      {
        AVFrame* nv12 = Nv12Of(f);
        av_frame_unref(f);
        out(packer.Add(nv12), nv12);
        av_frame_free(&nv12);
      }
    }
    AVFrame* none = av_frame_alloc();
    out(packer.Flush(none), none);
    av_frame_free(&none);
    const std::string left = Hex(l), right = Hex(r);
    CHECK(notNv12 == 0 && pictures > 0, "%s: %d pictures, %d not NV12", n, pictures, notNv12);
    CHECK(left == RefMd5(base + ".md5", std::string("MVHEVCS_") + n + "_0.yuv") &&
              right == RefMd5(base + ".md5", std::string("MVHEVCS_") + n + "_1.yuv"),
          "%s in NV12: halves %s %s are not the views", n, left.c_str(), right.c_str());
    std::printf("%s in NV12 (as from NVDEC): %d pictures of two views, halves as the reference\n", n, pictures);
    av_free(l);
    av_free(r);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
  }
}

// Views of P010 (NVDEC's 10 bits) go into a picture of yuv420p10, sample for sample, at 1080p and 2160p per eye;
// and how long that takes on this processor, per picture of two views
static void P010Views()
{
  for (auto [w, h] : {std::pair{1920, 1080}, std::pair{3840, 2160}})
  {
    AVFrame* base = Made(w, h, 0, 1000, 1, 0);
    AVFrame* other = Made(w, h, 1, 1000, 2, 0);
    CMultiviewPacker packer(BaseEye::LEFT, true);
    AVFrame* b = P010Of(base);
    AVFrame* o = P010Of(other);
    packer.Add(b);
    CHECK(packer.Add(o) == CMultiviewPacker::Result::PICTURE, "%dx%d P010: no picture", w, h);
    CHECK(o->format == AV_PIX_FMT_YUV420P10LE, "%dx%d P010: the picture is %s", w, h,
          av_get_pix_fmt_name(static_cast<AVPixelFormat>(o->format)));
    CHECK(o->width == 2 * w && HalfIs(o, 0, base) && HalfIs(o, 1, other),
          "%dx%d P010: the halves are not the views", w, h);
    av_frame_free(&b);
    av_frame_free(&o);
    // the time of the packing, NVDEC's path (P010 in) and the processor's (yuv420p10 in), per picture of two
    for (int p010 = 0; p010 < 2; ++p010)
    {
      constexpr int ROUNDS = 24;
      int64_t spent = 0;
      for (int i = 0; i < ROUNDS; ++i)
      {
        AVFrame* bb = p010 ? P010Of(base) : av_frame_clone(base);
        AVFrame* oo = p010 ? P010Of(other) : av_frame_clone(other);
        bb->pts = oo->pts = 2000 + i;
        const int64_t t0 = av_gettime_relative();
        packer.Add(bb);
        packer.Add(oo);
        spent += av_gettime_relative() - t0;
        av_frame_free(&bb);
        av_frame_free(&oo);
      }
      std::printf("timing %dx%d per eye, %s in: %.2f ms per picture of two views (one thread)\n", w, h,
                  p010 ? "P010" : "yuv420p10", spent / 1000.0 / ROUNDS);
    }
    av_frame_free(&base);
    av_frame_free(&other);
    std::printf("%dx%d per eye, P010 (NVDEC): into yuv420p10, each half its view\n", w, h);
  }
}

//! the sample at (x, y) of a plane of a planar 10-bit picture, or of a component of NV12 (c: 0 U, 1 V)
static int Sample(const AVFrame* f, int plane, int x, int y, int c = 0)
{
  if (f->format == AV_PIX_FMT_NV12)
    return plane == 0 ? f->data[0][static_cast<size_t>(y) * f->linesize[0] + x]
                      : f->data[1][static_cast<size_t>(y) * f->linesize[1] + 2 * x + c];
  if (f->format == AV_PIX_FMT_YUV420P)
    return f->data[plane][static_cast<size_t>(y) * f->linesize[plane] + x];
  return reinterpret_cast<const uint16_t*>(f->data[plane] + static_cast<size_t>(y) * f->linesize[plane])[x];
}

// The disparity adjustment: each eye moved by half of it, the left one to the left for a positive value, the edge
// it leaves black (limited or full range) and without colour; in planar 10 bits, from P010, and in NV12
static void Disparity()
{
  constexpr int W = 64, H = 32;
  for (int kind = 0; kind < 3; ++kind) // yuv420p10, P010, NV12
  {
    for (int adjustment : {2500, -2500, 0})
    {
      for (bool full : {false, true})
      {
        const int shift = LeftEyeShift(adjustment, W, 2); // -8, 8, 0
        AVFrame* base = Made(W, H, 0, 7, 1, 0);
        AVFrame* other = Made(W, H, 1, 7, 2, 0);
        if (kind == 2)
          for (AVFrame* f : {base, other})
          {
            // 8 bits: the 10-bit samples halved twice
            AVFrame* e = av_frame_alloc();
            e->format = AV_PIX_FMT_YUV420P;
            e->width = W;
            e->height = H;
            av_frame_get_buffer(e, 0);
            av_frame_copy_props(e, f);
            for (int p = 0; p < 3; ++p)
              for (int y = 0; y < (p ? H / 2 : H); ++y)
                for (int x = 0; x < (p ? W / 2 : W); ++x)
                  e->data[p][static_cast<size_t>(y) * e->linesize[p] + x] =
                      static_cast<uint8_t>(Sample(f, p, x, y) >> 2);
            av_frame_unref(f);
            av_frame_move_ref(f, e);
            av_frame_free(&e);
          }
        for (AVFrame* f : {base, other})
          f->color_range = full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        AVFrame* b = kind == 1 ? P010Of(base) : kind == 2 ? Nv12Of(base) : av_frame_clone(base);
        AVFrame* o = kind == 1 ? P010Of(other) : kind == 2 ? Nv12Of(other) : av_frame_clone(other);
        CMultiviewPacker packer(BaseEye::LEFT, true);
        packer.SetDisparityAdjustment(adjustment);
        packer.Add(b);
        const bool made = packer.Add(o) == CMultiviewPacker::Result::PICTURE;
        CHECK(made, "kind %d %d: no picture", kind, adjustment);
        if (made)
        {
          const int depth8 = kind == 2;
          const int black = full ? 0 : (depth8 ? 16 : 64);
          const int grey = depth8 ? 128 : 512;
          int wrong = 0;
          for (int half = 0; half < 2; ++half)
          {
            const AVFrame* view = half ? other : base;
            const int s = half ? -shift : shift;
            for (int p = 0; p < 3; ++p)
            {
              const int pw = p ? W / 2 : W, ph = p ? H / 2 : H, ps = p ? s / 2 : s;
              for (int y = 0; y < ph; ++y)
                for (int x = 0; x < pw; ++x)
                {
                  const int from = x - ps;
                  const int want = from >= 0 && from < pw ? Sample(view, p, from, y) : (p ? grey : black);
                  const int got = kind == 2 ? (p ? Sample(o, 1, half * pw + x, y, p - 1)
                                                 : Sample(o, 0, half * pw + x, y))
                                            : Sample(o, p, half * pw + x, y);
                  if (got != want)
                    ++wrong;
                }
            }
          }
          CHECK(wrong == 0, "kind %d adjustment %d full %d (shift %d): %d samples wrong", kind, adjustment, full,
                shift, wrong);
        }
        for (AVFrame* f : {base, other, b, o})
          av_frame_free(&f);
      }
    }
  }
  std::printf("disparity: each eye moved by half, the edge black and without colour, in all three formats\n");
}

static void Pacing()
{
  CMultiviewPacker packer(BaseEye::LEFT, true);
  int at = -1;
  for (int i = 0; i < 1000 && at < 0; ++i)
  {
    AVFrame* b = Made(64, 32, 0, i, 1, 0);
    AVFrame* o = Made(64, 32, 1, i, 2, 0);
    packer.Add(b);
    packer.Add(o);
    av_frame_free(&b);
    av_frame_free(&o);
    packer.Paced(true, true);
    if (packer.GivenUp())
      at = i;
  }
  CHECK(at == static_cast<int>(PACE_GRACE + 2 * PACE_WINDOW - 1) && packer.Slow(),
        "always late: given up at %d, slow %d", at, packer.Slow());
  CHECK(std::strstr(packer.Reason(), "processor") != nullptr, "reason: %s", packer.Reason());
  // after a seek the pace starts anew, and a seek alone gives nothing up
  CMultiviewPacker again(BaseEye::LEFT, true);
  for (unsigned int i = 0; i < PACE_GRACE + PACE_WINDOW; ++i)
    again.Paced(true, true);
  again.Reset();
  for (unsigned int i = 0; i < PACE_GRACE + PACE_WINDOW; ++i)
    again.Paced(true, true);
  CHECK(!again.GivenUp(), "the pace was not judged anew after a seek");
  CHECK(!again.Slow(), "slow without giving up");
}

int main(int argc, char** argv)
{
  if (argc < 2)
    return 2;
  av_log_set_level(AV_LOG_QUIET);
  Conformance(argv[1]);
  for (int i = 2; i + 2 < argc; i += 3)
  {
    const Run run = Decode(argv[i], 4);
    CHECK(run.left == argv[i + 1] && run.right == argv[i + 2], "%s: halves %s %s", argv[i], run.left.c_str(),
          run.right.c_str());
    std::printf("%s: %d pictures of two views\n", argv[i], run.pictures);
  }
  Sizes();
  ConformanceNv12(argv[1]);
  P010Views();
  Disparity();
  Pacing();
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
