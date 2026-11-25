// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// How fast FFmpeg's HEVC decoder gives both views of an MV-HEVC stream on this processor, opened as Kodi opens it
// for the processor's path (view_ids "-1", frame threads), and how evenly: the pictures of two views a second over
// the whole stream, and the time between one picture of two views and the next once the decoder is primed (its
// median, its 99th percentile and its longest), as the playback would wait for them.
// usage: mvhevc_speed <stream> <threads> [rounds] [base]   (base: the base view alone, as a 2D stream decodes)
// Prints: "threads T: N pictures of two views in S s: F a second; between two: median M ms, p99 P ms, max X ms"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void* a, const void* b)
{
  const int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
  return x < y ? -1 : x > y;
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  const int threads = atoi(argv[2]);
  const int rounds = argc > 3 ? atoi(argv[3]) : 1;
  const int views = argc > 4 && !strcmp(argv[4], "base") ? 1 : 2;
  av_log_set_level(AV_LOG_QUIET);
  int64_t gaps[1 << 16];
  int ngaps = 0, pairs = 0;
  int64_t spent = 0;
  for (int round = 0; round < rounds; ++round)
  {
    AVFormatContext* fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
      return 1;
    const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    const AVCodec* codec = avcodec_find_decoder_by_name("hevc");
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
    ctx->thread_count = threads;
    if (views == 2)
      av_opt_set(ctx, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN);
    if (avcodec_open2(ctx, codec, NULL) < 0)
      return 1;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    int got = 0, roundPairs = 0;
    int64_t last = 0;
    const int64_t start = av_gettime_relative();
    for (int eof = 0; !eof;)
    {
      if (av_read_frame(fmt, pkt) < 0)
      {
        eof = 1;
        avcodec_send_packet(ctx, NULL);
      }
      else
      {
        if (pkt->stream_index == si)
          avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);
      }
      while (avcodec_receive_frame(ctx, f) == 0)
      {
        av_frame_unref(f);
        if (++got % views)
          continue;
        const int64_t now = av_gettime_relative();
        // (the first pictures fill the threads: not counted as gaps)
        if (roundPairs >= threads && last && ngaps < (1 << 16))
          gaps[ngaps++] = now - last;
        last = now;
        ++roundPairs;
      }
    }
    spent += av_gettime_relative() - start;
    pairs += roundPairs;
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
  }
  qsort(gaps, ngaps, sizeof(gaps[0]), cmp);
  printf("threads %d: %d pictures (%s) in %.2f s: %.1f a second; between two: median %.1f ms, p99 %.1f ms, "
         "max %.1f ms\n",
         threads, pairs, views == 2 ? "of two views" : "of the base view", spent / 1e6, pairs / (spent / 1e6), ngaps ? gaps[ngaps / 2] / 1e3 : 0.0,
         ngaps ? gaps[(ngaps * 99) / 100] / 1e3 : 0.0, ngaps ? gaps[ngaps - 1] / 1e3 : 0.0);
  return 0;
}
