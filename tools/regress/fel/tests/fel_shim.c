// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Kodi's enhancement-layer module (tools/dvbridge/dvbridge_fel.c, the file itself) for a host test
// written in C++, its layer decoded in software as fel_rpu_test.c has it.
#include <libavcodec/avcodec.h>
static const char* g_vendor = "Mesa Gallium driver 26.2.5 for AMD Radeon Graphics (radeonsi, gfx1036)";
#define vaQueryVendorString(display) ((void)(display), g_vendor)
// The module only takes VAAPI surfaces from its decoder. Here the decoder runs in software.
#define AV_PIX_FMT_VAAPI AV_PIX_FMT_YUV420P10LE
#include "dvbridge_fel.c"
#undef AV_PIX_FMT_VAAPI
#include "fel_shim.h"

struct dvbridge_fel* shim_fel_open(const AVCodecParameters* parameters, AVRational time_base)
{
  struct dvbridge_fel* f = dvbridge_fel_create(parameters, time_base);
  if (!f)
    return NULL;
  // what dvbridge_fel_device() does with a VAAPI device, in software; the thread count stays the module's
  f->decoder->get_format = avcodec_default_get_format;
  if (avcodec_open2(f->decoder, avcodec_find_decoder(AV_CODEC_ID_HEVC), NULL) < 0)
  {
    dvbridge_fel_destroy(f);
    return NULL;
  }
  f->opened = true;
  return f;
}

int shim_fel_draining(const struct dvbridge_fel* f)
{
  return f && f->draining;
}

int shim_fel_synchronizing(const struct dvbridge_fel* f)
{
  return f && f->synchronizing;
}

unsigned shim_fel_drain_dropped(const struct dvbridge_fel* f)
{
#ifdef SHIM_OLD_MODULE
  (void)f;
  return 0;
#else
  return f ? f->drain_dropped : 0;
#endif
}
