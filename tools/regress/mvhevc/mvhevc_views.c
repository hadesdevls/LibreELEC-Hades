/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Host test of MV-HEVC decoding as Kodi of Hades opens it (CDVDVideoCodecFFmpeg::Open() for a stream of
 * two views): FFmpeg's own HEVC decoder, the option view_ids set to "-1" through av_opt_set() with
 * AV_OPT_SEARCH_CHILDREN, frame threads as asked (Kodi asks for 1.5 threads per processor, at most 16).
 * Reads any container FFmpeg reads (an Annex B .bit, MP4/MOV, Matroska) and writes, per view of the
 * stream, the MD5 of its pictures as a planar YUV file has them (the reference decoder's .yuv: the
 * cropped picture, 8-bit samples as bytes, deeper ones as 16-bit little endian), and how the pictures of
 * an access unit pair: the two views with the same presentation time, one after the other.
 *
 * usage: mvhevc_views <input> <threads> [views [extradata]]   (views: the value of view_ids, "-1" by
 *        default; "none" leaves the option unset, as the decoder of every other stream is opened;
 *        extradata: a file the private data of the track is written to, as the demuxer gives it)
 * output: "view <id> frames <n> md5 <hex> pos <left|right|unspec|none>" per view in the order they come,
 *         then "pairs <n> unpaired <n> order <base-first|other>" and "eof".
 */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/stereo3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VIEWS 8

struct view
{
  int used, id, frames, pos;
  struct AVMD5* md5;
};

static struct view views[MAX_VIEWS];
static int64_t last_pts = AV_NOPTS_VALUE;
static int last_view = -1, pairs = 0, unpaired_runs = 0, base_first = 1, base_id = -1, nb_views = 0;

static struct view* view_of(int id)
{
  for (int i = 0; i < MAX_VIEWS; i++)
    if (views[i].used && views[i].id == id)
      return &views[i];
  for (int i = 0; i < MAX_VIEWS; i++)
    if (!views[i].used)
    {
      views[i].used = 1;
      views[i].id = id;
      views[i].pos = -1;
      views[i].md5 = av_md5_alloc();
      av_md5_init(views[i].md5);
      nb_views++;
      return &views[i];
    }
  fprintf(stderr, "too many views\n");
  exit(2);
}

static void take(const AVFrame* f)
{
  const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID);
  const int id = sd ? *(const int*)sd->data : 0;
  struct view* v = view_of(id);
  const AVFrameSideData* st = av_frame_get_side_data(f, AV_FRAME_DATA_STEREO3D);
  if (st)
    v->pos = ((const AVStereo3D*)st->data)->view;
  const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f->format);
  const int bytes = d->comp[0].depth > 8 ? 2 : 1;
  for (int p = 0; p < 3; p++)
  {
    const int w = p ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width;
    const int h = p ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
    for (int y = 0; y < h; y++)
      av_md5_update(v->md5, f->data[p] + (size_t)y * f->linesize[p], (size_t)w * bytes);
  }
  v->frames++;
  if (base_id < 0)
    base_id = id;
  // two pictures of different views with the same time, one right after the other
  if (last_view >= 0 && last_view != id && f->pts != AV_NOPTS_VALUE && f->pts == last_pts)
  {
    pairs++;
    if (last_view != base_id)
      base_first = 0;
    last_view = -1;
  }
  else
  {
    if (last_view >= 0)
      unpaired_runs++;
    last_view = id;
    last_pts = f->pts;
  }
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  const char* viewsOpt = argc > 3 ? argv[3] : "-1";
  AVFormatContext* fmt = NULL;
  if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
  {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (si < 0)
    return 2;
  AVStream* st = fmt->streams[si];
  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, st->codecpar);
  ctx->thread_count = atoi(argv[2]);
  if (strcmp(viewsOpt, "none") && av_opt_set(ctx, "view_ids", viewsOpt, AV_OPT_SEARCH_CHILDREN) < 0)
  {
    printf("view_ids refused\n");
    return 3;
  }
  if (avcodec_open2(ctx, codec, NULL) < 0)
    return 2;
  printf("disposition_multilayer %d\n", (st->disposition & AV_DISPOSITION_MULTILAYER) ? 1 : 0);
  if (argc > 4)
  {
    FILE* out = fopen(argv[4], "wb");
    if (!out || fwrite(st->codecpar->extradata, 1, (size_t)st->codecpar->extradata_size, out) !=
                    (size_t)st->codecpar->extradata_size)
      return 2;
    fclose(out);
  }
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  int errors = 0;
  for (int eof = 0; !eof;)
  {
    int r = av_read_frame(fmt, pkt);
    if (r < 0)
    {
      eof = 1;
      avcodec_send_packet(ctx, NULL);
    }
    else
    {
      if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) < 0)
        errors++;
      av_packet_unref(pkt);
    }
    while (avcodec_receive_frame(ctx, f) == 0)
    {
      take(f);
      av_frame_unref(f);
    }
  }
  for (int i = 0; i < MAX_VIEWS; i++)
  {
    if (!views[i].used)
      continue;
    uint8_t sum[16];
    av_md5_final(views[i].md5, sum);
    printf("view %d frames %d md5 ", views[i].id, views[i].frames);
    for (int k = 0; k < 16; k++)
      printf("%02x", sum[k]);
    printf(" pos %s\n", views[i].pos == AV_STEREO3D_VIEW_LEFT    ? "left"
                        : views[i].pos == AV_STEREO3D_VIEW_RIGHT ? "right"
                        : views[i].pos < 0                       ? "none"
                                                                 : "unspec");
  }
  printf("pairs %d unpaired %d order %s errors %d\n", pairs, unpaired_runs,
         base_first ? "base-first" : "other", errors);
  printf("eof\n");
  return 0;
}
