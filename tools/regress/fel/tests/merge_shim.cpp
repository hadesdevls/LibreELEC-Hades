// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// See merge_shim.h. The file under test is DVLayerMerge.cpp as Kodi compiles it.
#include "DVLayerMerge.h"

// tools/regress: the join under test has every part that the tests check
#if defined(DVMERGE_NO_CONFIGURE) || defined(DVMERGE_NO_LAYER_CHECK) || defined(DVMERGE_NO_LAYER_TIME)
#error "the join under test lacks a part that the tests check"
#endif

#include <cstring>
#include <utility>

extern "C"
{
#include "merge_shim.h"
}

struct merge_source
{
  AVFormatContext* context = nullptr;
  CDVLayerMerge::Layers layers;
  CDVLayerMerge merge;
};

extern "C" struct merge_source* merge_open(const char* path)
{
  merge_source* source = new merge_source;
  if (avformat_open_input(&source->context, path, nullptr, nullptr) < 0 ||
      avformat_find_stream_info(source->context, nullptr) < 0)
  {
    merge_close(source);
    return nullptr;
  }
  source->layers = CDVLayerMerge::Find(source->context, 0);
  if (source->layers.result != CDVLayerMerge::Layers::Result::JOINED)
  {
    merge_close(source);
    return nullptr;
  }
#ifdef DVMERGE_NO_CONFIGURE
  source->merge.SetRate(source->layers.rate);
#else
  // CDVDDemuxFFmpeg::FindDVLayers()
  source->merge.Configure(source->layers);
#endif
  return source;
}

extern "C" void merge_close(struct merge_source* source)
{
  if (!source)
    return;
  source->merge.Clear();
  avformat_close_input(&source->context);
  delete source;
}

extern "C" AVFormatContext* merge_context(struct merge_source* source)
{
  return source->context;
}

extern "C" int merge_base(const struct merge_source* source)
{
  return source->layers.base;
}

extern "C" int merge_enhancement(const struct merge_source* source)
{
  return source->layers.enhancement;
}

extern "C" void merge_swap(struct merge_source* source)
{
  std::swap(source->layers.base, source->layers.enhancement);
#ifndef DVMERGE_NO_CONFIGURE
  std::swap(source->layers.baseLengthSize, source->layers.enhancementLengthSize);
  source->merge.Configure(source->layers);
#endif
}

extern "C" void merge_record(const struct merge_source* source, AVDOVIDecoderConfigurationRecord* record)
{
  *record = source->layers.record;
}

extern "C" int merge_length_size(const struct merge_source* source)
{
#ifdef DVMERGE_NO_CONFIGURE
  (void)source;
  return 0;
#else
  return source->layers.baseLengthSize;
#endif
}

extern "C" int merge_enhancement_config(const struct merge_source* source, const uint8_t** data, int* size)
{
#ifdef DVMERGE_NO_CONFIGURE
  (void)source;
  (void)data;
  (void)size;
  return 0;
#else
  // CDVDDemuxFFmpeg::AddStream() for the base layer of the pair
  const AVCodecParameters* layer = source->context->streams[source->layers.enhancement]->codecpar;
  if (!CDVLayerMerge::IsConfigurationRecord(layer->extradata, layer->extradata_size))
    return 0;
  *data = layer->extradata;
  *size = layer->extradata_size;
  return 1;
#endif
}

extern "C" int merge_read(struct merge_source* source, AVPacket* packet)
{
  return source->merge.Read(source->context, source->layers.base, source->layers.enhancement, packet);
}

extern "C" int merge_seek(struct merge_source* source, double part)
{
  const int64_t start = source->context->start_time == AV_NOPTS_VALUE ? 0 : source->context->start_time;
  const int64_t target = start + static_cast<int64_t>(part * static_cast<double>(source->context->duration));
  const int result = av_seek_frame(source->context, -1, target, AVSEEK_FLAG_BACKWARD);
  source->merge.Clear();
  return result;
}

extern "C" int merge_check_layer_time(struct merge_source* source,
                                      AVPacket* packet,
                                      double (*microseconds)(int64_t, void*),
                                      void* context)
{
#ifdef DVMERGE_NO_LAYER_CHECK
  (void)source;
  (void)packet;
  (void)microseconds;
  (void)context;
  return 1;
#else
  return source->merge.CheckLayerTime(
      packet, [microseconds, context](int64_t pts) { return microseconds(pts, context); });
#endif
}

extern "C" const char* merge_layer_time_key(void)
{
#ifdef DVMERGE_NO_LAYER_TIME
  return "";
#else
  return CDVLayerMerge::LAYER_TIME;
#endif
}
