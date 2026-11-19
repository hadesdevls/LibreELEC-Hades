// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/*
 * LibreELEC Hades, TEST ONLY: what i915 does with HDR10PLUS_VSIF at a mode that DRM names by an HDMI VIC,
 * from the rule of the kernel tree's own intel_hdr10plus.h (linux-9954 and linux-9972), for
 * the decision test of Kodi (hdr10plus_kodi_test.cpp). The mode: 3840x2160 at 23.976 Hz, HDMI VIC 3, CTA VIC 93; the
 * sink: HDMI 2.0 (SCDC), lists VIC 93, declares HDR10+ of version receiver_version (the S90H of issue 2: version 1).
 *
 * A commit, as intel_hdmi_compute_avi_infoframe(), intel_hdmi_compute_hdmi_infoframe() and the fastset check of
 * intel_pipe_config_compare() decide it (intel_hdmi.c, intel_display.c of the tree):
 *  - the CTA VIC: intel_hdr10plus_cta_vic(), with kept from the configuration on the screen; with linux-9972, 0 where
 *    a fastset would have to change the AVI InfoFrame (intel_hdr10plus_cta_vic_needs_modeset());
 *  - HDR10+ with the HDMI VIC left in the vendor InfoFrame: refused, with a kernel log line (I915_REFUSED_LOGGED);
 *    with linux-9972 also one of a version the sink does not take;
 *  - a commit that may not change the mode whose AVI InfoFrame differs from the one on the screen: refused as one that
 *    needs a modeset, with nothing in the kernel log but a debug line (I915_REFUSED_SILENT).
 */
#include "i915_model.h"

#include <drm/drm_hdr10plus.h>
#include "intel_hdr10plus.h"

int i915_commit(struct i915_state *s, bool allow_modeset, bool new_mode, const unsigned char *hdr10plus)
{
	const bool same_mode = s->on && !new_mode;
	const bool requested = hdr10plus != NULL;
	const bool kept = s->on && intel_hdr10plus_cta_vic_kept(s->avi_vic, 93, same_mode);
	u8 vic = intel_hdr10plus_cta_vic(3, 93, true, true, s->receiver_version >= 0, requested, kept);

	if (intel_hdr10plus_cta_vic_needs_modeset(vic, s->avi_vic, same_mode, allow_modeset))
		vic = 0;
	if (requested && !intel_hdr10plus_version_fits(hdr10plus[3] >> 6, s->receiver_version))
		return I915_REFUSED_LOGGED;
	/* the vendor InfoFrame keeps the HDMI VIC without the CTA VIC: HDR10+ cannot take its DIP */
	if (requested && vic == 0)
		return I915_REFUSED_LOGGED;
	/* the fastset check compares the AVI InfoFrame */
	if (same_mode && !allow_modeset && vic != s->avi_vic)
		return I915_REFUSED_SILENT;
	s->avi_vic = vic;
	s->on = true;
	s->hdr10plus = requested;
	return I915_ACCEPTED;
}
