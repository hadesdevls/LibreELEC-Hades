// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/* LibreELEC Hades, TEST ONLY: intel_hdr10plus_edid_version() of the kernel tree, for edid_cross_test.cpp */
#include <drm/drm_hdr10plus.h>
#include "intel_hdr10plus.h"

int k_edid_version(const unsigned char *edid, size_t size);
int k_edid_version(const unsigned char *edid, size_t size)
{
	return intel_hdr10plus_edid_version(edid, size);
}
