// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/* LibreELEC Hades, TEST ONLY: see i915_model.c */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I915_ACCEPTED 0
#define I915_REFUSED_LOGGED (-1)
#define I915_REFUSED_SILENT (-2)

struct i915_state {
	unsigned char avi_vic; /* of the configuration on the screen */
	bool on;
	bool hdr10plus;
	int receiver_version;
};

/* a commit (hdr10plus: the 27 bytes of HDR10PLUS_VSIF, NULL for none); new_mode: it sets another mode */
int i915_commit(struct i915_state *s, bool allow_modeset, bool new_mode, const unsigned char *hdr10plus);

#ifdef __cplusplus
}
#endif
