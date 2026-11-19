// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// CDVLayerMerge (C++) for the host tests written in C: a file with two Dolby Vision tracks (transport stream,
// MP4 or Matroska) read as Kodi's demuxer reads it, its two tracks joined.
#pragma once

#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct merge_source;

// NULL when the file does not open or has no pair of tracks that is joined
struct merge_source* merge_open(const char* path);
void merge_close(struct merge_source* source);
AVFormatContext* merge_context(struct merge_source* source);
int merge_base(const struct merge_source* source);        // the stream index of the base layer
int merge_enhancement(const struct merge_source* source); // ... and of the enhancement layer
// The two tracks change places: what is joined and decoded as the base layer is then the track whose
// pictures are coded in another order than they are presented (the base track of the test stream is not).
void merge_swap(struct merge_source* source);
// the Dolby Vision record of the joined stream
void merge_record(const struct merge_source* source, AVDOVIDecoderConfigurationRecord* record);
// the framing of the joined packets: bytes of the length before each unit, 0 for start codes
int merge_length_size(const struct merge_source* source);
// the hvcC record of the enhancement track that Kodi's demuxer hands on to the decoder of the layer
// (doviELConfig, AV_PKT_DATA_HEVC_CONF): 1 and the record, or 0 when there is none
int merge_enhancement_config(const struct merge_source* source, const uint8_t** data, int* size);
// CDVLayerMerge::Read()
int merge_read(struct merge_source* source, AVPacket* packet);
// as CDVDDemuxFFmpeg::SeekTime(): to a part of the duration, backwards, and what waits is forgotten
int merge_seek(struct merge_source* source, double part);
// CDVLayerMerge::CheckLayerTime(), as CDVDDemuxFFmpeg::ReadInternal() calls it for a packet of the base
// layer: 0 when the time of its enhancement access unit does not hold for the times that microseconds()
// makes of the times of the track, and was marked so. With a join that has no such check, 1.
int merge_check_layer_time(struct merge_source* source,
                           AVPacket* packet,
                           double (*microseconds)(int64_t, void*),
                           void* context);
// CDVLayerMerge::LAYER_TIME
const char* merge_layer_time_key(void);

#ifdef __cplusplus
}
#endif
