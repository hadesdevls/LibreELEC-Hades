# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants of the kernel's HDR10+ headers (of the image's kernel: the archive of the recipe with its patches,
# kernel_files.py), each caught by khost_test.c ("khost"), or by khost_test.c or the decisions of Kodi with the i915
# model of the header (hdr10plus_kodi_test.cpp, "kernel-kodi"). Expected: "-", the test fails (exit status not 0).
DIRS = ['include', 'drivers']
DRM = 'include/drm/drm_hdr10plus.h'
AMD = 'drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_hdr10plus.h'
I915 = 'drivers/gpu/drm/i915/display/intel_hdr10plus.h'


def m(name, path, old, new, what, test='khost'):
    return (name, path, [(old, new)], test, '-', what)


MUTANTS = [
    # ------------------------------------------------------------------ drm_hdr10plus.h: the framing of the InfoFrame
    m('drm-checksum', DRM, 'buf[3] = (u8)(0x100 - sum);', 'buf[3] = (u8)(sum);', 'the checksum not the complement'),
    m('drm-hb2', DRM, 'hb[2] = 0x1d;', 'hb[2] = 0x1c;', 'the length byte of the DP header'),
    m('drm-hb3', DRM, 'hb[3] = 0x13 << 2;', 'hb[3] = 0x12 << 2;', 'the version byte of the DP header'),
    m('drm-db1', DRM, 'db[1] = DRM_HDR10PLUS_VSIF_SIZE;', 'db[1] = DRM_HDR10PLUS_VSIF_SIZE + 1;', 'the length in the DP data bytes'),
    m('drm-oui', DRM, ' && vsif[2] == 0x90;', ';', 'the last byte of the OUI not checked'),
    m('drm-size', DRM, 'size == DRM_HDR10PLUS_VSIF_SIZE &&', 'size >= DRM_HDR10PLUS_VSIF_SIZE &&', 'a payload longer than 27 bytes taken'),
    m('drm-db-clear', DRM, 'memset(db, 0, db_size);', ';', 'the DP data bytes not cleared'),
    m('drm-buf-size', DRM, 'if (buf_size < DRM_HDR10PLUS_INFOFRAME_SIZE)', 'if (buf_size < DRM_HDR10PLUS_INFOFRAME_SIZE - 1)', 'a buffer one byte short taken'),
    m('drm-db-size', DRM, 'if (db_size < DRM_HDR10PLUS_DP_SDP_DATA_SIZE)', 'if (db_size < DRM_HDR10PLUS_DP_SDP_DATA_SIZE - 1)', 'DP data bytes one short taken'),
    m('drm-length', DRM, 'buf[2] = DRM_HDR10PLUS_VSIF_SIZE;', 'buf[2] = DRM_HDR10PLUS_VSIF_SIZE - 1;', 'the length byte of the InfoFrame'),
    # ------------------------------------------------------------------ amdgpu_dm_hdr10plus.h: the generic packet
    m('amd-body', AMD, 'memcpy(out->sb, &buf[3], len - 3);', 'memcpy(out->sb, &buf[4], len - 4);', 'the body one byte later'),
    m('amd-hb3', AMD, 'out->hb3 = hb[3];', ';', 'HB3 not set'),
    m('amd-valid', AMD, 'out->valid = true;', 'out->valid = hdmi;', 'the packet valid on HDMI only'),
    m('amd-clear', AMD, 'memset(out, 0, sizeof(*out));', ';', 'the packet not cleared first'),
    # ------------------------------------------------------------------ intel_hdr10plus.h: the vendor DIP and the HDMI one
    m('i915-vic0', I915, 'vsi->vic == 0 &&', '1 &&', 'the HDMI VSIF with a VIC taken as saying nothing'),
    m('i915-s3d', I915, 'vsi->s3d_struct == HDMI_3D_STRUCTURE_INVALID;', '1;', 'the HDMI VSIF with 3D taken as saying nothing'),
    m('i915-oui', I915, 'return vsi->oui == HDMI_IEEE_OUI &&', 'return 1 &&', 'another OUI taken as the HDMI one'),
    m('i915-dip3', I915, 'dip[3] = 0;', 'dip[3] = 1;', 'the DIP byte 3 not 0'),
    m('i915-dip5', I915, 'dip[5] == 0x8b && ', '', 'the first byte of the OUI in the DIP not checked'),
    m('i915-dip2', I915, 'dip[2] == DRM_HDR10PLUS_VSIF_SIZE &&', '1 &&', 'the length of the DIP not checked'),
    m('i915-frame-from', I915, 'memcpy(&frame[3], &dip[4], DRM_HDR10PLUS_INFOFRAME_SIZE - 3);', 'memcpy(&frame[3], &dip[3], DRM_HDR10PLUS_INFOFRAME_SIZE - 3);', 'the frame read one byte early from the DIP'),
    m('i915-active-and', I915, 'return intel_hdr10plus_active(old_frame) || intel_hdr10plus_active(new_frame);', 'return intel_hdr10plus_active(old_frame) && intel_hdr10plus_active(new_frame);', 'a change only where both frames are active'),
    m('i915-tag', I915, 'if (tag == 7 && len >= 5', 'if ((tag | 7) == 7 && len >= 5', 'any tag below 8 read as extended'),
    m('i915-len4', I915, 'tag == 7 && len >= 5 &&', 'tag == 7 && len >= 4 &&', 'a block of 4 bytes read'),
    m('i915-ext-tag', I915, 'ext[i + 1] == 0x01 &&', '(ext[i + 1] | 1) == 0x01 &&', 'the extended tag 0 read as 1'),
    m('i915-oui-edid', I915, 'ext[i + 2] == 0x8b && ext[i + 3] == 0x84', 'ext[i + 2] == 0x8b && (ext[i + 3] | 0x40) == 0xc4', 'another OUI read as HDR10+'),
    m('i915-cta-tag', I915, 'if (ext[0] != 0x02 || end < 4', 'if (ext[0] == 0x00 || end < 4', 'a block that is not CTA-861 read'),
    m('i915-end', I915, '|| end > 127)', '|| end > 128)', 'the data blocks read past the block'),
    m('i915-block-size', I915, 'b + 128 <= size', 'b + 128 < size', 'the last extension block not read'),
    m('i915-block-step', I915, 'b += 128)', 'b += 256)', 'every second extension block skipped'),
    m('i915-block-len', I915, 'if (i + 1 + len > end)', 'if (i + 1 + len > end + 8)', 'a data block past the end read'),
    m('i915-hdmi-vic', I915, 'if (!hdmi_vic || ', 'if (!(hdmi_vic | 1) || ', 'a mode without an HDMI VIC given a CTA VIC'),
    m('i915-hdmi2', I915, ' || !hdmi2 || ', ' || !(hdmi2 || 1) || ', 'the CTA VIC also without HDMI 2.x'),
    m('i915-listed', I915, ' || !listed || ', ' || !(listed || 1) || ', 'the CTA VIC also where the display does not list it'),
    m('i915-declares', I915, ' || !declares)', ' || !(declares || 1))', 'the CTA VIC also where the display does not declare HDR10+'),
    m('i915-kept-never', I915, 'return (requested || kept) ? cta_vic : 0;', 'return (requested || (kept && 0)) ? cta_vic : 0;', 'the CTA VIC on the screen not kept'),
    m('i915-requested-and-kept', I915, 'return (requested || kept) ? cta_vic : 0;', 'return (requested && kept) ? cta_vic : 0;', 'the CTA VIC only where asked and kept'),
    m('i915-same-vic', I915, 'return cta_vic && old_avi_vic == cta_vic && same_mode;', 'return (cta_vic || 1) && old_avi_vic == cta_vic && same_mode;', 'a mode without a CTA VIC taken as the same'),
    m('i915-same-mode', I915, 'old_avi_vic == cta_vic && same_mode;', 'old_avi_vic == cta_vic && (same_mode || 1);', 'another mode taken as the same'),
    m('i915-avi-vic', I915, 'avi->video_code = cta_vic;', 'avi->video_code = cta_vic + 1;', 'the AVI InfoFrame given another VIC'),
    m('i915-avi-always', I915, 'if (cta_vic)\n\t\tavi->video_code = cta_vic;', 'if (cta_vic || 1)\n\t\tavi->video_code = cta_vic;',
      'the AVI InfoFrame changed without a CTA VIC'),
    m('i915-vsi-vic', I915, 'vsi->vic = 0;', 'vsi->vic = 1;', 'the HDMI VSIF keeps a VIC'),
    # ------------------------------------------------------------------ intel_hdr10plus.h: the versions (linux-9972)
    m('i915-version-any', I915, 'return receiver_version >= 0 && (int)payload_version <= (receiver_version < 1 ? receiver_version : 1);', 'return receiver_version >= 0 && (int)payload_version <= 3;', 'a payload of any version to a display with a block', 'kernel-kodi'),
    m('i915-version-receiver', I915, 'return receiver_version >= 0 && (int)payload_version <= (receiver_version < 1 ? receiver_version : 1);', 'return receiver_version >= 0 && (int)payload_version <= receiver_version;', 'a version above 1 where the display says so', 'kernel-kodi'),
    m('i915-modeset-allowed', I915, 'return cta_vic && same_mode && old_avi_vic != cta_vic && !allow_modeset;', 'return cta_vic && same_mode && old_avi_vic != cta_vic && (allow_modeset || !allow_modeset);', 'a commit that may change the mode said to need one', 'kernel-kodi'),
    m('i915-modeset-mode', I915, 'return cta_vic && same_mode && old_avi_vic != cta_vic && !allow_modeset;', 'return cta_vic && (same_mode || !same_mode) && old_avi_vic != cta_vic && !allow_modeset;', 'another mode said to need a modeset for the AVI', 'kernel-kodi'),
    m('i915-version-1', I915, '(int)(ext[i + 5] & 0x3) > version)', '(int)(ext[i + 5] & 0x3) >= 1)', 'the version of a later block of version 0 not read', 'kernel-kodi'),
    m('i915-version-always-1', I915, '\t\t\t\tversion = ext[i + 5] & 0x3;', '\t\t\t\tversion = 1;', 'every block read as version 1', 'kernel-kodi'),
    m('i915-declares-0', I915, 'return intel_hdr10plus_edid_version(edid, size) >= 0;', 'return intel_hdr10plus_edid_version(edid, size) >= 1;', 'a display of version 0 not taken as declaring HDR10+', 'kernel-kodi'),
]
