/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Stand-in for the kernel's linux/hdmi.h in the host tests of the HDR10+ headers: what intel_hdr10plus.h uses of it,
 * as the kernel declares it. The runner checks, before every build, that the kernel of the recipe says the same
 * (the OUI, the 3D structures, and the video_code member of struct hdmi_avi_infoframe): else "STUB-MISMATCH".
 */
#pragma once
#include <linux/types.h>
#define HDMI_IEEE_OUI 0x000c03
enum hdmi_3d_structure {
	HDMI_3D_STRUCTURE_INVALID = -1, HDMI_3D_STRUCTURE_FRAME_PACKING = 0, HDMI_3D_STRUCTURE_FIELD_ALTERNATIVE,
	HDMI_3D_STRUCTURE_LINE_ALTERNATIVE, HDMI_3D_STRUCTURE_SIDE_BY_SIDE_FULL, HDMI_3D_STRUCTURE_L_DEPTH,
	HDMI_3D_STRUCTURE_L_DEPTH_GFX_GFX_DEPTH, HDMI_3D_STRUCTURE_TOP_AND_BOTTOM, HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF = 8,
};
struct hdmi_vendor_infoframe {
	int type; unsigned char version; unsigned char length; unsigned int oui; u8 vic;
	enum hdmi_3d_structure s3d_struct; unsigned int s3d_ext_data;
};
struct hdmi_avi_infoframe {
	int type; unsigned char version; unsigned char length; unsigned char video_code;
};
