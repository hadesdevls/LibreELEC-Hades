/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Stand-in for struct dc_info_packet (drivers/gpu/drm/amd/display/dc/dc_types.h) in the host tests of the HDR10+
 * headers; the runner checks that the kernel of the recipe declares it so (else "STUB-MISMATCH").
 */
#pragma once
#include <linux/types.h>
struct dc_info_packet { bool valid; uint8_t hb0; uint8_t hb1; uint8_t hb2; uint8_t hb3; uint8_t sb[32]; };
