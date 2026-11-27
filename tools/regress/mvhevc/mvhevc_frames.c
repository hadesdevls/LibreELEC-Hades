/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * The pictures FFmpeg's HEVC decoder gives for a stream, one line each, opened as Kodi opens it: view_ids "-1"
 * for a stream of two layers (or not set, as for every other stream), frame threads as asked. For the frame-threading
 * test of MV-HEVC (run-mvhevc.sh, stage threads): the same stream through two decoders, or through one with
 * different numbers of threads, must give the same lines.
 *
 * usage: mvhevc_frames <input> <threads> <views|none> [seek <percent>] [skip <n>] [explode] [zero] [base <mode>]
 *                      [trace] [verbose]
 *   seek:    after 16 packets, a seek back to the key frame before <percent> % of the duration and a flush of the
 *            decoder, as Kodi seeks; only the pictures after the seek are listed (those before it depend on how
 *            many the threads had finished)
 *   skip:    AVCodecContext.skip_frame (8 non-reference, 16 bidirectional, 32 non-intra, 48 non-key)
 *   explode: AV_EF_EXPLODE
 *   zero:    every picture buffer cleared when it is taken (get_buffer2), so that the parts of a damaged picture
 *            that no slice decoded are the same whichever buffer of the pool it got
 *   base:    the decoder's option base_layer_decoder (ffmpeg-9976: separate, separate_semiplanar)
 *   trace:   on stderr, what each picture says of its base layer ("picture pts <pts> view <id> base <mode>")
 *   verbose: FFmpeg's messages on stderr
 *   An Annex B input (.bit, .hevc) is read by the hevc demuxer, whatever its probe says. An input .aus is a list
 *   of access units, each its length (4 bytes, big endian) and then its NAL units in Annex B: each is given to the
 *   decoder as one packet, as a sample of an MP4 or Matroska file is (the hevc parser would end an access unit at
 *   a parameter set after a slice); pts is its index; seek is not taken with it.
 * output: "frame <n> view <id> pts <pts> key <0|1> <w>x<h> <format> md5 <hex>" per picture, in the order they come,
 *         then "view <id> frames <n>" per view, "packets <n> errors <n>" and "eof"; on stderr "deferred <a> <s>":
 *         the decoder's debug lines of ffmpeg-9975 (access units whose slice data waited for the end of setup,
 *         ffmpeg-9974, and their slices), and "base separate <n> checking <n> internal <n> checks <n> corrupt <n>
 *         error <reason|->": the pictures by what their metadata says of the base layer (ffmpeg-9976), the most
 *         pictures found the same that it said, the pictures marked corrupt, and the first error it gave; before it
 *         "range <n>": the samples of the pictures listed that are above the largest of their bit depth.
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

static int verbose;

static void log_cb(void* avcl, int level, const char* fmt, va_list vl)
{
  (void)avcl;
  if (verbose && level <= AV_LOG_VERBOSE)
  {
    va_list c;
    va_copy(c, vl);
    vfprintf(stderr, fmt, c);
    va_end(c);
  }
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
static long long out_of_range;

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
    {
      const uint8_t* row = f->data[p] + (size_t)y * f->linesize[p];
      av_md5_update(md5, row, (size_t)w * bytes);
      // (planar formats of more than 8 bits keep each sample in the low bits of 16)
      if (bytes == 2 && !(d->flags & AV_PIX_FMT_FLAG_BE) && d->comp[0].shift == 0)
        for (int x = 0; x < w; x++)
          out_of_range += ((row[2 * x] | (row[2 * x + 1] << 8)) >> d->comp[0].depth) != 0;
    }
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

// what the decoder says of the base layer of each picture (base_layer_decoder): every picture,
// listed or not, counted by its view's frames
static int base_counts[3], base_checks, base_corrupt, base_trace;
static char base_error[256];

static void base_of(const AVFrame* f)
{
  const AVDictionaryEntry* e = av_dict_get(f->metadata, "lavc.hevc.base_layer", NULL, 0);
  if (base_trace)
  {
    const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID);
    fprintf(stderr, "picture pts %lld view %d base %s%s\n", (long long)f->pts,
            sd ? *(const int*)sd->data : 0, e ? e->value : "(none)",
            (f->flags & AV_FRAME_FLAG_CORRUPT) ? " corrupt" : "");
  }
  if (e)
  {
    if (!strcmp(e->value, "separate"))
      base_counts[0]++;
    else if (!strcmp(e->value, "checking"))
      base_counts[1]++;
    else
      base_counts[2]++;
  }
  e = av_dict_get(f->metadata, "lavc.hevc.base_layer_checks", NULL, 0);
  if (e && atoi(e->value) > base_checks)
    base_checks = atoi(e->value);
  e = av_dict_get(f->metadata, "lavc.hevc.base_layer_error", NULL, 0);
  if (e && !base_error[0])
    snprintf(base_error, sizeof(base_error), "%s", e->value);
  if (f->flags & AV_FRAME_FLAG_CORRUPT)
    base_corrupt++;
}

static int ends_with(const char* s, const char* e)
{
  const size_t a = strlen(s), b = strlen(e);
  return a >= b && !strcmp(s + a - b, e);
}

// the next access unit of an .aus input, as one packet: 0, AVERROR_EOF at its end, or AVERROR_INVALIDDATA
static FILE* aus;
static int aus_index;

static int read_au(AVPacket* pkt)
{
  uint8_t h[4];
  const size_t got = fread(h, 1, 4, aus);
  if (got == 0 && feof(aus))
    return AVERROR_EOF;
  if (got != 4)
    return AVERROR_INVALIDDATA;
  const uint32_t n = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
  if (n == 0 || n > (64u << 20) || av_new_packet(pkt, (int)n) < 0)
    return AVERROR_INVALIDDATA;
  if (fread(pkt->data, 1, n, aus) != n)
  {
    av_packet_unref(pkt);
    return AVERROR_INVALIDDATA;
  }
  pkt->pts = pkt->dts = aus_index++;
  pkt->stream_index = 0;
  return 0;
}

int main(int argc, char** argv)
{
  if (argc < 4)
    return 2;
  int seek = -1, explode = 0, skip = -1, zero = 0;
  const char* base = NULL;
  for (int i = 4; i < argc; i++)
  {
    if (!strcmp(argv[i], "seek") && i + 1 < argc)
      seek = atoi(argv[++i]);
    else if (!strcmp(argv[i], "base") && i + 1 < argc)
      base = argv[++i];
    else if (!strcmp(argv[i], "trace"))
      base_trace = 1;
    else if (!strcmp(argv[i], "verbose"))
      verbose = 1;
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
  AVStream* st = NULL;
  int si = 0;
  if (ends_with(argv[1], ".aus"))
  {
    if (seek >= 0 || !(aus = fopen(argv[1], "rb")))
    {
      fprintf(stderr, "cannot open %s (seek is not taken with .aus)\n", argv[1]);
      return 2;
    }
  }
  else
  {
    const AVInputFormat* in = ends_with(argv[1], ".bit") || ends_with(argv[1], ".hevc") ? av_find_input_format("hevc") : NULL;
    if (avformat_open_input(&fmt, argv[1], in, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
    {
      fprintf(stderr, "cannot open %s\n", argv[1]);
      return 2;
    }
    si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si < 0)
      return 2;
    st = fmt->streams[si];
  }
  const AVCodec* codec = avcodec_find_decoder_by_name("hevc");
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (st)
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
  if (base && av_opt_set(ctx, "base_layer_decoder", base, AV_OPT_SEARCH_CHILDREN) < 0)
    return 3;
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
    int r = aus ? read_au(pkt) : av_read_frame(fmt, pkt);
    if (r == AVERROR_INVALIDDATA && aus)
      errors++;
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
      base_of(f);
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
  fprintf(stderr, "range %lld\n", out_of_range);
  fprintf(stderr, "base separate %d checking %d internal %d checks %d corrupt %d error %s\n",
          base_counts[0], base_counts[1], base_counts[2], base_checks, base_corrupt,
          base_error[0] ? base_error : "-");
  printf("eof\n");
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  if (fmt)
    avformat_close_input(&fmt);
  if (aus)
    fclose(aus);
  return 0;
}
