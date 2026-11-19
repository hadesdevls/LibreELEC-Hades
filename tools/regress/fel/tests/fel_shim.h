// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// The module for the C++ host test (fel_shim.c)
#pragma once
#include "dvbridge_fel.h"
#ifdef __cplusplus
extern "C"
{
#endif
struct dvbridge_fel* shim_fel_open(const AVCodecParameters* parameters, AVRational time_base);
int shim_fel_draining(const struct dvbridge_fel* f);
int shim_fel_synchronizing(const struct dvbridge_fel* f);
unsigned shim_fel_drain_dropped(const struct dvbridge_fel* f);
#ifdef __cplusplus
}
#endif
