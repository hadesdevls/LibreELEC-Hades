/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * The pictures FFmpeg's HEVC decoder gives for a stream, one line each, opened as Kodi opens it: view_ids "-1"
 * for a stream of two layers (or not set, as for every other stream), frame threads as asked. For the frame-threading
 * test of MV-HEVC (run-mvhevc.sh, stage threads): the same stream through two decoders, or through one with
 * different numbers of threads, must give the same lines.
 *
 * usage: mvhevc_frames <input> <threads> <views|none> [seek <percent>] [skip <n>] [explode] [zero]
 *   seek:    after 16 packets, a seek back to the key frame before <percent> % of the duration and a flush of the
 *            decoder, as Kodi seeks; only the pictures after the seek are listed (those before it depend on how
 *            many the threads had finished)
 *   skip:    AVCodecContext.skip_frame (8 non-reference, 16 bidirectional, 32 non-intra, 48 non-key)
 *   explode: AV_EF_EXPLODE
 *   zero:    every picture buffer cleared when it is taken (get_buffer2), so that the parts of a damaged picture
 *            that no slice decoded are the same whichever buffer of the pool it got
 *   An Annex B input (.bit, .hevc) is read by the hevc demuxer, whatever its probe says.
 * output: "frame <n> view <id> pts <pts> key <0|1> <w>x<h> <format> md5 <hex>" per picture, in the order they come,
 *         then "view <id> frames <n>" per view, "packets <n> errors <n>" and "eof"; on stderr "deferred <a> <s>":
 *         the decoder's debug lines of ffmpeg-9975 (access units whose slice data waited for the end of setup,
 *         ffmpeg-9974, and their slices).
 */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_int deferred_aus, deferred_slices;

static void log_cb(void* avcl, int level, const char* fmt, va_list vl)
{
  (void)avcl;
  if (level <= AV_LOG_DEBUG && !strncmp(fmt, "Decoded %u slice(s) set aside", 29))
  {
    va_list c;
    va_copy(c, vl);
    const unsigned n = va_arg(c, unsigned);
    va_end(c);
    atomic_fetch_add(&deferred_aus, 1);
    atomic_fetch_add(&deferred_slices, (int)n);
  }
}

static int get_buffer_zero(AVCodecContext* ctx, AVFrame* f, int flags)
{
  const int ret = avcodec_default_get_buffer2(ctx, f, flags);
  if (ret < 0)
    return ret;
  for (int i = 0; i < AV_NUM_DATA_POINTERS && f->buf[i]; i++)
    memset(f->buf[i]->data, 0, f->buf[i]->size);
  return 0;
}

static int frames, counts[64], ids[64], nids;

static void take(const AVFrame* f)
{
  const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID);
  const int id = sd ? *(const int*)sd->data : 0;
  const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f->format);
  const int bytes = d->comp[0].depth > 8 ? 2 : 1;
  struct AVMD5* md5 = av_md5_alloc();
  uint8_t sum[16];
  int k;
  av_md5_init(md5);
  for (int p = 0; p < 4 && f->data[p]; p++)
  {
    const int w = (p == 1 || p == 2) ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width;
    const int h = (p == 1 || p == 2) ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
    for (int y = 0; y < h; y++)
      av_md5_update(md5, f->data[p] + (size_t)y * f->linesize[p], (size_t)w * bytes);
  }
  av_md5_final(md5, sum);
  av_free(md5);
  printf("frame %d view %d pts %lld key %d %dx%d %s md5 ", frames++, id, (long long)f->pts,
         (f->flags & AV_FRAME_FLAG_KEY) ? 1 : 0, f->width, f->height, d->name);
  for (k = 0; k < 16; k++)
    printf("%02x", sum[k]);
  printf("%s\n", (f->flags & AV_FRAME_FLAG_CORRUPT) ? " corrupt" : "");
  for (k = 0; k < nids && ids[k] != id; k++)
    ;
  if (k == nids && nids < 64)
    ids[nids++] = id;
  if (k < 64)
    counts[k]++;
}

static int ends_with(const char* s, const char* e)
{
  const size_t a = strlen(s), b = strlen(e);
  return a >= b && !strcmp(s + a - b, e);
}

int main(int argc, char** argv)
{
  if (argc < 4)
    return 2;
  int seek = -1, explode = 0, skip = -1, zero = 0;
  for (int i = 4; i < argc; i++)
  {
    if (!strcmp(argv[i], "seek") && i + 1 < argc)
      seek = atoi(argv[++i]);
    else if (!strcmp(argv[i], "skip") && i + 1 < argc)
      skip = atoi(argv[++i]);
    else if (!strcmp(argv[i], "explode"))
      explode = 1;
    else if (!strcmp(argv[i], "zero"))
      zero = 1;
    else
      return 2;
  }
  av_log_set_level(AV_LOG_DEBUG);
  av_log_set_callback(log_cb);
  AVFormatContext* fmt = NULL;
  const AVInputFormat* in = ends_with(argv[1], ".bit") || ends_with(argv[1], ".hevc") ? av_find_input_format("hevc") : NULL;
  if (avformat_open_input(&fmt, argv[1], in, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
  {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (si < 0)
    return 2;
  AVStream* st = fmt->streams[si];
  const AVCodec* codec = avcodec_find_decoder_by_name("hevc");
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, st->codecpar);
  ctx->thread_count = atoi(argv[2]);
  if (strcmp(argv[3], "none") && av_opt_set(ctx, "view_ids", argv[3], AV_OPT_SEARCH_CHILDREN) < 0)
    return 3;
  if (skip >= 0)
    ctx->skip_frame = (enum AVDiscard)skip;
  if (explode)
    ctx->err_recognition |= AV_EF_EXPLODE;
  if (zero)
    ctx->get_buffer2 = get_buffer_zero;
  if (avcodec_open2(ctx, codec, NULL) < 0)
    return 2;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* f = av_frame_alloc();
  int sent = 0, errors = 0, listing = seek < 0;
  for (int eof = 0; !eof;)
  {
    if (seek >= 0 && sent == 16)
    {
      const int64_t dur = fmt->duration > 0 ? fmt->duration : 0;
      const int64_t ts = av_rescale_q(dur * seek / 100, AV_TIME_BASE_Q, st->time_base) +
                         (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0);
      printf("seek to %lld: %d\n", (long long)ts, av_seek_frame(fmt, si, ts, AVSEEK_FLAG_BACKWARD));
      avcodec_flush_buffers(ctx);
      seek = -1;
      listing = 1;
    }
    int r = av_read_frame(fmt, pkt);
    if (r < 0)
    {
      eof = 1;
      avcodec_send_packet(ctx, NULL);
    }
    else
    {
      if (pkt->stream_index == si)
      {
        sent++;
        if (avcodec_send_packet(ctx, pkt) < 0)
          errors++;
      }
      av_packet_unref(pkt);
    }
    while ((r = avcodec_receive_frame(ctx, f)) == 0)
    {
      if (listing)
        take(f);
      av_frame_unref(f);
    }
    if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF)
      errors++;
  }
  for (int k = 0; k < nids; k++)
    printf("view %d frames %d\n", ids[k], counts[k]);
  printf("packets %d errors %d\n", sent, errors);
  fflush(stdout);
  fprintf(stderr, "deferred %d %d\n", deferred_aus, deferred_slices);
  printf("eof\n");
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return 0;
}
