// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image).
// Writes, for chosen pictures of a profile 7 FEL stream of one track, what the Dolby Vision bridge is given:
// the decoded base layer and its paired enhancement layer (10-bit codes, one uint16 per sample, planar
// Y, Cb, Cr), their chroma location, and the parsed RPU (AVDOVIMetadata) as text, so that a reference can
// recompute the reconstruction outside of the GPU.
//
// Decoding, pairing, picture numbering and the choice of the metadata are those of the bridge's render tests
// (the same loop): base layer with 16 frame threads, the module dvbridge_fel.c of the tree under test for the
// enhancement layer, the metadata of the enhancement picture when that brings its own.
//
// Usage: ref_dump <file> <output directory> <picture> [<picture> ...]
// For picture N it writes <dir>/pN/{bl_y,bl_u,bl_v,el_y,el_u,el_v}.u16, <dir>/pN/meta.txt and <dir>/pN/meta.bin
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI

static int WritePlane(const char* dir, const char* name, const uint8_t* data, int linesize, int w, int h)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.u16", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  for (int r = 0; r < h; ++r)
    if (fwrite(data + (ptrdiff_t)r * linesize, 2, (size_t)w, f) != (size_t)w)
    {
      fclose(f);
      return 0;
    }
  return fclose(f) == 0;
}

static int WriteFrame(const char* dir, const char* prefix, const AVFrame* f, FILE* meta)
{
  if (f->format != AV_PIX_FMT_YUV420P10LE)
  {
    fprintf(stderr, "unexpected format %s\n", av_get_pix_fmt_name(f->format));
    return 0;
  }
  char n[16];
  const int cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
  snprintf(n, sizeof n, "%s_y", prefix);
  if (!WritePlane(dir, n, f->data[0], f->linesize[0], f->width, f->height))
    return 0;
  snprintf(n, sizeof n, "%s_u", prefix);
  if (!WritePlane(dir, n, f->data[1], f->linesize[1], cw, ch))
    return 0;
  snprintf(n, sizeof n, "%s_v", prefix);
  if (!WritePlane(dir, n, f->data[2], f->linesize[2], cw, ch))
    return 0;
  fprintf(meta, "%s.width %d\n%s.height %d\n%s.format %s\n", prefix, f->width, prefix, f->height, prefix,
          av_get_pix_fmt_name(f->format));
  fprintf(meta, "%s.chroma_location %d %s\n", prefix, (int)f->chroma_location,
          av_chroma_location_name(f->chroma_location) ? av_chroma_location_name(f->chroma_location) : "?");
  fprintf(meta, "%s.color_range %d %s\n", prefix, (int)f->color_range,
          av_color_range_name(f->color_range) ? av_color_range_name(f->color_range) : "?");
  fprintf(meta, "%s.colorspace %d\n%s.pts %lld\n", prefix, (int)f->colorspace, prefix, (long long)f->pts);
  return 1;
}

static void Rational(FILE* m, const char* name, const AVRational* r, int n)
{
  fprintf(m, "%s", name);
  for (int i = 0; i < n; ++i)
    fprintf(m, " %d/%d", r[i].num, r[i].den);
  fprintf(m, "\n");
}

static void WriteMeta(FILE* m, const AVDOVIMetadata* d)
{
  const AVDOVIRpuDataHeader* h = av_dovi_get_header(d);
  const AVDOVIDataMapping* map = av_dovi_get_mapping(d);
  const AVDOVIColorMetadata* c = av_dovi_get_color(d);
  fprintf(m, "hdr.rpu_type %u\nhdr.rpu_format %u\nhdr.vdr_rpu_profile %u\nhdr.vdr_rpu_level %u\n", h->rpu_type,
          h->rpu_format, h->vdr_rpu_profile, h->vdr_rpu_level);
  fprintf(m, "hdr.chroma_resampling_explicit_filter_flag %u\nhdr.coef_data_type %u\nhdr.coef_log2_denom %u\n",
          h->chroma_resampling_explicit_filter_flag, h->coef_data_type, h->coef_log2_denom);
  fprintf(m, "hdr.vdr_rpu_normalized_idc %u\nhdr.bl_video_full_range_flag %u\nhdr.bl_bit_depth %u\n",
          h->vdr_rpu_normalized_idc, h->bl_video_full_range_flag, h->bl_bit_depth);
  fprintf(m, "hdr.el_bit_depth %u\nhdr.vdr_bit_depth %u\nhdr.spatial_resampling_filter_flag %u\n", h->el_bit_depth,
          h->vdr_bit_depth, h->spatial_resampling_filter_flag);
  fprintf(m, "hdr.el_spatial_resampling_filter_flag %u\nhdr.disable_residual_flag %u\n",
          h->el_spatial_resampling_filter_flag, h->disable_residual_flag);
  fprintf(m, "hdr.ext_mapping_idc_0_4 %u\nhdr.ext_mapping_idc_5_7 %u\n", h->ext_mapping_idc_0_4,
          h->ext_mapping_idc_5_7);
  fprintf(m, "map.vdr_rpu_id %u\nmap.mapping_color_space %u\nmap.mapping_chroma_format_idc %u\n", map->vdr_rpu_id,
          map->mapping_color_space, map->mapping_chroma_format_idc);
  for (int k = 0; k < 3; ++k)
  {
    const AVDOVIReshapingCurve* cv = &map->curves[k];
    fprintf(m, "curve%d.num_pivots %u\ncurve%d.pivots", k, cv->num_pivots, k);
    for (int i = 0; i < cv->num_pivots; ++i)
      fprintf(m, " %u", cv->pivots[i]);
    fprintf(m, "\n");
    for (int i = 0; i + 1 < cv->num_pivots; ++i)
    {
      fprintf(m, "curve%d.piece%d.mapping_idc %d\n", k, i, (int)cv->mapping_idc[i]);
      if (cv->mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL)
        fprintf(m, "curve%d.piece%d.poly %u %lld %lld %lld\n", k, i, cv->poly_order[i], (long long)cv->poly_coef[i][0],
                (long long)cv->poly_coef[i][1], (long long)cv->poly_coef[i][2]);
      else
      {
        fprintf(m, "curve%d.piece%d.mmr_order %u\ncurve%d.piece%d.mmr_constant %lld\n", k, i, cv->mmr_order[i], k, i,
                (long long)cv->mmr_constant[i]);
        for (int j = 0; j < cv->mmr_order[i]; ++j)
        {
          fprintf(m, "curve%d.piece%d.mmr%d", k, i, j + 1);
          for (int t = 0; t < 7; ++t)
            fprintf(m, " %lld", (long long)cv->mmr_coef[i][j][t]);
          fprintf(m, "\n");
        }
      }
    }
  }
  fprintf(m, "map.nlq_method_idc %d\nmap.num_x_partitions %u\nmap.num_y_partitions %u\n", (int)map->nlq_method_idc,
          map->num_x_partitions, map->num_y_partitions);
  fprintf(m, "map.nlq_pivots %u %u\n", map->nlq_pivots[0], map->nlq_pivots[1]);
  for (int k = 0; k < 3; ++k)
    fprintf(m, "nlq%d %u %llu %llu %llu\n", k, map->nlq[k].nlq_offset, (unsigned long long)map->nlq[k].vdr_in_max,
            (unsigned long long)map->nlq[k].linear_deadzone_slope,
            (unsigned long long)map->nlq[k].linear_deadzone_threshold);
  fprintf(m, "color.dm_metadata_id %u\ncolor.scene_refresh_flag %u\n", c->dm_metadata_id, c->scene_refresh_flag);
  Rational(m, "color.ycc_to_rgb_matrix", c->ycc_to_rgb_matrix, 9);
  Rational(m, "color.ycc_to_rgb_offset", c->ycc_to_rgb_offset, 3);
  Rational(m, "color.rgb_to_lms_matrix", c->rgb_to_lms_matrix, 9);
  fprintf(m, "color.signal_eotf %u %u %u %u\n", c->signal_eotf, c->signal_eotf_param0, c->signal_eotf_param1,
          c->signal_eotf_param2);
  fprintf(m, "color.signal_bit_depth %u\ncolor.signal_color_space %u\ncolor.signal_chroma_format %u\n",
          c->signal_bit_depth, c->signal_color_space, c->signal_chroma_format);
  fprintf(m, "color.signal_full_range_flag %u\ncolor.source_min_pq %u\ncolor.source_max_pq %u\n",
          c->signal_full_range_flag, c->source_min_pq, c->source_max_pq);
  for (int i = 0; i < d->num_ext_blocks; ++i)
  {
    const AVDOVIDmData* e = av_dovi_get_ext(d, i);
    fprintf(m, "ext%d.level %u\n", i, e->level);
    if (e->level == 1)
      fprintf(m, "ext%d.l1 %u %u %u\n", i, e->l1.min_pq, e->l1.max_pq, e->l1.avg_pq);
    else if (e->level == 5)
      fprintf(m, "ext%d.l5 %u %u %u %u\n", i, e->l5.left_offset, e->l5.right_offset, e->l5.top_offset,
              e->l5.bottom_offset);
    else if (e->level == 6)
      fprintf(m, "ext%d.l6 %u %u %u %u\n", i, e->l6.max_luminance, e->l6.min_luminance, e->l6.max_cll,
              e->l6.max_fall);
  }
}

int main(int argc, char** argv)
{
  if (argc < 4)
    return 2;
  const char* out = argv[2];
  unsigned wanted[64], numWanted = 0, last = 0;
  for (int i = 3; i < argc && numWanted < 64; ++i)
  {
    wanted[numWanted] = (unsigned)atoi(argv[i]);
    if (wanted[numWanted] > last)
      last = wanted[numWanted];
    ++numWanted;
  }
  av_log_set_level(AV_LOG_ERROR);
  AVFormatContext* fc = NULL;
  if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0)
    return 1;
  const int index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (index < 0)
    return 1;
  AVStream* stream = fc->streams[index];
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  AVCodecContext* base = avcodec_alloc_context3(codec);
  if (!codec || !base || avcodec_parameters_to_context(base, stream->codecpar) < 0)
    return 1;
  base->pkt_timebase = stream->time_base;
  base->thread_count = 16;
  base->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(base, codec, NULL) < 0)
    return 1;
  printf("stream: codecpar chroma_location %d (%s), color_range %d, %dx%d\n", (int)stream->codecpar->chroma_location,
         av_chroma_location_name(stream->codecpar->chroma_location) ? av_chroma_location_name(stream->codecpar->chroma_location) : "?",
         (int)stream->codecpar->color_range, stream->codecpar->width, stream->codecpar->height);
  struct dvbridge_fel* fel = dvbridge_fel_create(stream->codecpar, stream->time_base);
  if (!fel)
    return 1;
  fel->decoder->get_format = avcodec_default_get_format;
  fel->decoder->thread_count = 16;
  fel->decoder->thread_type = FF_THREAD_FRAME;
  if (avcodec_open2(fel->decoder, codec, NULL) < 0)
    return 1;
  fel->opened = true;

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* pending[64];
  unsigned numPending = 0, pictures = 0, written = 0;
  int end = 0, ok = 1;
  while (!end && ok && pictures <= last)
  {
    const int got = av_read_frame(fc, packet);
    if (got >= 0 && packet->stream_index != index)
    {
      av_packet_unref(packet);
      continue;
    }
    end = got < 0;
    if (!end)
    {
      avcodec_send_packet(base, packet);
      if (!dvbridge_fel_submit(fel, packet))
        ok = 0;
      av_packet_unref(packet);
    }
    else
    {
      avcodec_send_packet(base, NULL);
      dvbridge_fel_drain(fel);
    }
    while (ok && avcodec_receive_frame(base, frame) >= 0)
    {
      if (numPending == 64)
      {
        ok = 0;
        break;
      }
      pending[numPending++] = av_frame_clone(frame);
      av_frame_unref(frame);
      while (numPending && pictures <= last)
      {
        AVFrame* b = pending[0];
        AVFrame* layer = NULL;
        const int result = dvbridge_fel_take(fel, b->pts, &layer);
        if (result == 0 && !end)
          break;
        const unsigned number = pictures++;
        int want = 0;
        for (unsigned i = 0; i < numWanted; ++i)
          want |= wanted[i] == number;
        if (want)
        {
          const AVFrame* from = b;
          if (layer && av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_RPU_BUFFER) &&
              av_frame_get_side_data(layer, AV_FRAME_DATA_DOVI_METADATA))
            from = layer;
          const AVFrameSideData* sd = av_frame_get_side_data(from, AV_FRAME_DATA_DOVI_METADATA);
          char dir[1024];
          snprintf(dir, sizeof dir, "%s/p%u", out, number);
          mkdir(dir, 0755);
          char path[1100];
          snprintf(path, sizeof path, "%s/meta.txt", dir);
          FILE* m = fopen(path, "w");
          if (!m || !sd || !layer)
          {
            printf("picture %u: %s\n", number, !sd ? "no metadata" : !layer ? "no enhancement picture" : "no file");
            ok = 0;
          }
          else
          {
            fprintf(m, "picture %u\nmetadata_from %s\n", number, from == layer ? "enhancement" : "base");
            ok &= WriteFrame(dir, "bl", b, m);
            ok &= WriteFrame(dir, "el", layer, m);
            WriteMeta(m, (const AVDOVIMetadata*)sd->data);
            ok &= fclose(m) == 0;
            // the parsed metadata itself (the side data as FFmpeg gives it: its blocks are
            // found by offsets from its start), for renderers that are given the layers from these files
            snprintf(path, sizeof path, "%s/meta.bin", dir);
            FILE* mb = fopen(path, "wb");
            ok &= mb && fwrite(sd->data, 1, sd->size, mb) == sd->size;
            if (mb)
              ok &= fclose(mb) == 0;
            ++written;
            printf("picture %u written to %s (metadata of the %s picture)\n", number, dir,
                   from == layer ? "enhancement" : "base");
          }
        }
        av_frame_free(&layer);
        av_frame_free(&b);
        memmove(pending, pending + 1, --numPending * sizeof(*pending));
      }
    }
  }
  printf("%s: %u of %u pictures written\n", ok && written == numWanted ? "DONE" : "FAILED", written, numWanted);
  fflush(stdout);
  _Exit(ok && written == numWanted ? 0 : 1);
}
