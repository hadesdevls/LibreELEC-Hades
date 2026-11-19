// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: host test of the framing of HDR10PLUS_VSIF in the kernel: drm_hdr10plus.h (linux-9952),
// amdgpu_dm_hdr10plus.h (linux-9953) and intel_hdr10plus.h (linux-9954), the headers of the tree as they are,
// with a few stand-ins for kernel headers. Every expected byte is written out here, not computed by the code
// under test: also the five functions that linux-9954 adds to intel_hdr10plus.h, and those of linux-9972 (a
// display of application version 0 declares HDR10+, intel_hdr10plus_edid_version(), intel_hdr10plus_version_fits()
// and intel_hdr10plus_cta_vic_needs_modeset()). The runner proves the checks with mutants of the three headers.
#include <stdio.h>
#include <string.h>

#include "dc_stub.h"
#include <drm/drm_hdr10plus.h>
#include "amdgpu_dm_hdr10plus.h"
#include "intel_hdr10plus.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The payload that Kodi's test gets for FFmpeg's FATE HDR10+ picture (hdr10plus_test.cpp)
static const u8 fate[27] = {0x8B, 0x84, 0x90, 0x5A, 0x03, 0x00, 0x12, 0xFF, 0x00, 0x08, 0x0A, 0x0C, 0x0C, 0x12,
                            0x90, 0x00, 0x00, 0x1A, 0x33, 0x4D, 0x67, 0x80, 0x9A, 0xB3, 0xCD, 0xE7, 0x40};

static unsigned sum_of(const u8 *p, size_t n)
{
  unsigned s = 0;
  for (size_t i = 0; i < n; i++)
    s += p[i];
  return s & 0xff;
}

int main(void)
{
  u8 bad[27], buf[64], hb[4], db[32];
  memcpy(bad, fate, 27);

  // --- valid: exactly 27 bytes with the OUI
  CHECK(drm_hdr10plus_vsif_valid(fate, 27), "the FATE payload is valid");
  CHECK(!drm_hdr10plus_vsif_valid(fate, 26), "26 bytes");
  CHECK(!drm_hdr10plus_vsif_valid(fate, 28), "28 bytes");
  CHECK(!drm_hdr10plus_vsif_valid(NULL, 27), "no payload");
  for (int i = 0; i < 3; i++) {
    memcpy(bad, fate, 27);
    bad[i] ^= 0x01;
    CHECK(!drm_hdr10plus_vsif_valid(bad, 27), "OUI byte %d changed", i);
  }
  memcpy(bad, fate, 27);
  bad[0] = 0x46; bad[1] = 0xd0; bad[2] = 0x00; // the Dolby OUI
  CHECK(!drm_hdr10plus_vsif_valid(bad, 27), "the Dolby OUI");

  // --- HDMI: 81 01 1B, checksum, payload
  memset(buf, 0xAA, sizeof(buf));
  ssize_t n = drm_hdr10plus_hdmi_infoframe_pack(fate, 27, buf, sizeof(buf));
  CHECK(n == 31, "HDMI size %zd", n);
  CHECK(buf[0] == 0x81 && buf[1] == 0x01 && buf[2] == 0x1B, "HDMI header %02x %02x %02x", buf[0], buf[1], buf[2]);
  CHECK(!memcmp(&buf[4], fate, 27), "HDMI payload after the checksum");
  CHECK(sum_of(buf, 31) == 0, "HDMI checksum: the 31 bytes add to %u", sum_of(buf, 31));
  {
    // checksum written out: 0x100 - (0x81 + 0x01 + 0x1B + sum of the payload), modulo 256
    unsigned s = 0x81 + 0x01 + 0x1B;
    for (int i = 0; i < 27; i++)
      s += fate[i];
    CHECK(buf[3] == ((0x100 - (s & 0xff)) & 0xff), "HDMI checksum %02x", buf[3]);
  }
  CHECK(buf[31] == 0xAA, "HDMI: nothing written past 31 bytes");
  CHECK(drm_hdr10plus_hdmi_infoframe_pack(fate, 27, buf, 30) == -ENOSPC, "HDMI, 30-byte buffer");
  CHECK(drm_hdr10plus_hdmi_infoframe_pack(fate, 27, buf, 31) == 31, "HDMI, 31-byte buffer");
  bad[0] = 0x8a;
  CHECK(drm_hdr10plus_hdmi_infoframe_pack(bad, 27, buf, sizeof(buf)) == -EINVAL, "HDMI, wrong OUI");
  CHECK(drm_hdr10plus_hdmi_infoframe_pack(fate, 26, buf, sizeof(buf)) == -EINVAL, "HDMI, 26 bytes");

  // --- DisplayPort: 00 81 1D 4C; 01 1B payload, the rest zero
  memset(db, 0xAA, sizeof(db));
  memset(hb, 0xAA, sizeof(hb));
  int m = drm_hdr10plus_dp_sdp_pack(fate, 27, hb, db, sizeof(db));
  CHECK(m == 29, "DP data bytes %d", m);
  CHECK(hb[0] == 0x00 && hb[1] == 0x81 && hb[2] == 0x1D && hb[3] == 0x4C, "DP header %02x %02x %02x %02x", hb[0], hb[1], hb[2], hb[3]);
  CHECK(db[0] == 0x01 && db[1] == 0x1B, "DP version and length %02x %02x", db[0], db[1]);
  CHECK(!memcmp(&db[2], fate, 27), "DP payload");
  CHECK(db[29] == 0 && db[30] == 0 && db[31] == 0, "DP: the rest of the data bytes zero");
  CHECK(drm_hdr10plus_dp_sdp_pack(fate, 27, hb, db, 28) == -ENOSPC, "DP, 28 data bytes");
  CHECK(drm_hdr10plus_dp_sdp_pack(bad, 27, hb, db, sizeof(db)) == -EINVAL, "DP, wrong OUI");

  // --- amdgpu: the DC packet
  struct dc_info_packet p;
  memset(&p, 0x55, sizeof(p));
  CHECK(amdgpu_dm_hdr10plus_packet(fate, 27, true, &p) == 0, "amdgpu HDMI");
  CHECK(p.valid && p.hb0 == 0x81 && p.hb1 == 0x01 && p.hb2 == 0x1B && p.hb3 == 0, "amdgpu HDMI header");
  CHECK(sum_of(&p.hb0, 3) == ((0x81 + 0x01 + 0x1B) & 0xff) && ((p.hb0 + p.hb1 + p.hb2 + sum_of(p.sb, 28)) & 0xff) == 0,
        "amdgpu HDMI checksum in sb[0]");
  CHECK(!memcmp(&p.sb[1], fate, 27), "amdgpu HDMI payload from sb[1]");
  CHECK(p.sb[28] == 0 && p.sb[29] == 0 && p.sb[30] == 0 && p.sb[31] == 0, "amdgpu HDMI: the rest zero");
  memset(&p, 0x55, sizeof(p));
  CHECK(amdgpu_dm_hdr10plus_packet(fate, 27, false, &p) == 0, "amdgpu DP");
  CHECK(p.valid && p.hb0 == 0x00 && p.hb1 == 0x81 && p.hb2 == 0x1D && p.hb3 == 0x4C, "amdgpu DP header");
  CHECK(p.sb[0] == 0x01 && p.sb[1] == 0x1B && !memcmp(&p.sb[2], fate, 27) && p.sb[29] == 0 && p.sb[31] == 0, "amdgpu DP data");
  for (int hdmi = 0; hdmi < 2; hdmi++) {
    memset(&p, 0x55, sizeof(p));
    CHECK(amdgpu_dm_hdr10plus_packet(bad, 27, hdmi, &p) == -EINVAL, "amdgpu wrong OUI (%s)", hdmi ? "HDMI" : "DP");
    const u8 *q = (const u8 *)&p;
    int zero = 1;
    for (size_t i = 0; i < sizeof(p); i++)
      zero &= q[i] == 0;
    CHECK(zero && !p.valid, "amdgpu wrong OUI (%s): the packet invalid and zero", hdmi ? "HDMI" : "DP");
  }

  // --- i915: the vendor DIP
  struct hdmi_vendor_infoframe vsi = {.oui = HDMI_IEEE_OUI, .vic = 0, .s3d_struct = HDMI_3D_STRUCTURE_INVALID};
  CHECK(intel_hdr10plus_vsi_empty(&vsi), "an empty HDMI vendor InfoFrame");
  vsi.vic = 3;
  CHECK(!intel_hdr10plus_vsi_empty(&vsi), "HDMI VIC 3 (2160p24)");
  vsi.vic = 1;
  CHECK(!intel_hdr10plus_vsi_empty(&vsi), "HDMI VIC 1 (2160p30)");
  vsi.vic = 0;
  vsi.s3d_struct = HDMI_3D_STRUCTURE_FRAME_PACKING;
  CHECK(!intel_hdr10plus_vsi_empty(&vsi), "3D frame packing");
  vsi.s3d_struct = HDMI_3D_STRUCTURE_TOP_AND_BOTTOM;
  CHECK(!intel_hdr10plus_vsi_empty(&vsi), "3D top and bottom");
  vsi.s3d_struct = HDMI_3D_STRUCTURE_INVALID;
  vsi.oui = 0x00d046;
  CHECK(!intel_hdr10plus_vsi_empty(&vsi), "another OUI");

  u8 frame[31], dip[32], back[31];
  drm_hdr10plus_hdmi_infoframe_pack(fate, 27, frame, sizeof(frame));
  memset(dip, 0xAA, sizeof(dip));
  intel_hdr10plus_to_dip(frame, dip);
  CHECK(dip[0] == 0x81 && dip[1] == 0x01 && dip[2] == 0x1B && dip[3] == 0x00, "DIP header and the zero byte");
  CHECK(dip[4] == frame[3] && !memcmp(&dip[5], fate, 27), "DIP checksum and payload");
  CHECK(intel_hdr10plus_dip_holds(dip), "the DIP holds HDR10+");
  memset(back, 0, sizeof(back));
  intel_hdr10plus_from_dip(dip, back);
  CHECK(!memcmp(back, frame, 31), "DIP and back");
  {
    // the Dolby VSIF is also 81 01 1B, with its own OUI
    u8 dv[32] = {0x81, 0x01, 0x1B, 0x00, 0x00, 0x46, 0xD0, 0x00};
    CHECK(!intel_hdr10plus_dip_holds(dv), "the Dolby VSIF");
    // the empty HDMI vendor InfoFrame: 81 01 04
    u8 hv[32] = {0x81, 0x01, 0x04, 0x00, 0x00, 0x03, 0x0C, 0x00};
    CHECK(!intel_hdr10plus_dip_holds(hv), "the HDMI vendor InfoFrame");
    u8 lenx[32];
    memcpy(lenx, dip, 32);
    lenx[2] = 0x1A;
    CHECK(!intel_hdr10plus_dip_holds(lenx), "another length");
    for (int i = 5; i < 8; i++) {
      memcpy(lenx, dip, 32);
      lenx[i] ^= 0x10;
      CHECK(!intel_hdr10plus_dip_holds(lenx), "OUI byte %d of the DIP", i - 5);
    }
    memcpy(lenx, dip, 32);
    lenx[1] = 0x02;
    CHECK(!intel_hdr10plus_dip_holds(lenx), "version 2");
  }
  u8 none[31] = {0};
  CHECK(!intel_hdr10plus_active(none), "none is not active");
  CHECK(intel_hdr10plus_active(frame), "a frame is active");
  CHECK(!intel_hdr10plus_fastset_vendor(none, none), "fastset: neither sends HDR10+");
  CHECK(intel_hdr10plus_fastset_vendor(frame, none), "fastset: HDR10+ stops");
  CHECK(intel_hdr10plus_fastset_vendor(none, frame), "fastset: HDR10+ starts");
  CHECK(intel_hdr10plus_fastset_vendor(frame, frame), "fastset: HDR10+ changes");

  // --- i915, since linux-9954: a mode that DRM signals with an HDMI VIC goes out with its CTA VIC where
  // HDR10+ needs the vendor DIP
  {
    // An EDID: the base block (its content does not matter here) and CTA-861 extensions. ext[2] is the
    // offset of the detailed timings, the end of the data blocks, which start at 4.
    // The HDR10+ vendor-specific video data block: E5 (tag 7, the extended tag, length 5) 01 (extended
    // tag 1, vendor-specific video) 8B 84 90 (OUI 90-84-8B) and the application version in bits 1-0.
    static const u8 vdb[4] = {0x43, 93, 95, 96};               // video data block: VICs 93, 95, 96
    static const u8 hdr10p[6] = {0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01};
    u8 e[384];
#define EXT(at, end) (memset(&e[at], 0, 128), e[at] = 0x02, e[at + 1] = 0x03, e[at + 2] = (end))
    memset(e, 0, sizeof(e));
    EXT(128, 14);
    memcpy(&e[132], vdb, 4);
    memcpy(&e[136], hdr10p, 6);
    CHECK(intel_hdr10plus_edid_declares(e, 256), "EDID: HDR10+ after a video data block");
    CHECK(!intel_hdr10plus_edid_declares(e, 255), "EDID: 255 bytes hold no extension");
    CHECK(!intel_hdr10plus_edid_declares(e, 128), "EDID: the base block alone");
    CHECK(!intel_hdr10plus_edid_declares(NULL, 256), "EDID: none");
    e[141] = 0x00;
    CHECK(intel_hdr10plus_edid_declares(e, 256), "EDID: application version 0 declares HDR10+ (linux-9972)");
    e[141] = 0x02;
    CHECK(intel_hdr10plus_edid_declares(e, 256), "EDID: application version 2");
    e[141] = 0x04;
    CHECK(intel_hdr10plus_edid_declares(e, 256), "EDID: application version byte 04 (bits 1-0 are 0: version 0)");
    {
      static const u8 vb[6] = {0x01, 0x00, 0x02, 0x03, 0x04, 0x07};
      static const int vv[6] = {1, 0, 2, 3, 0, 3};
      for (int k = 0; k < 6; k++) {
        e[141] = vb[k];
        CHECK(intel_hdr10plus_edid_version(e, 256) == vv[k], "EDID version: byte %02x gives %d (%d)", vb[k], vv[k],
              intel_hdr10plus_edid_version(e, 256));
      }
      CHECK(intel_hdr10plus_edid_version(e, 128) == -1 && intel_hdr10plus_edid_version(NULL, 256) == -1,
            "EDID version: -1 without an extension");
    }
    e[141] = 0x01;
    e[138] = 0x46; e[139] = 0xD0; e[140] = 0x00;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: the Dolby OUI");
    e[138] = 0x8B; e[139] = 0xC4; e[140] = 0x90;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: OUI 90-C4-8B");
    e[139] = 0x84;
    e[137] = 0x06;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: extended tag 6 (HDR static metadata) with those bytes");
    e[137] = 0x00;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: extended tag 0 (video capability) with those bytes");
    e[137] = 0x01;
    e[136] = 0x65;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: tag 3 (vendor-specific data block) with those bytes");
    e[136] = 0xE5;
    e[128] = 0x70;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: an extension that is not CTA-861 (DisplayID)");
    e[128] = 0x02;
    e[130] = 128;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: data blocks up to 128 (out of the extension)");
    e[130] = 3;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: data blocks up to 3");
    e[130] = 13;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: the block runs past the end of the data blocks");
    e[130] = 14;
    // a block of length 4 (E4) followed by a video data block, whose first byte (43) has bits 1-0 set
    memset(&e[132], 0, 124);
    static const u8 short4[5] = {0xE4, 0x01, 0x8B, 0x84, 0x90};
    memcpy(&e[132], short4, 5);
    memcpy(&e[137], vdb, 4);
    e[130] = 13;
    CHECK(!intel_hdr10plus_edid_declares(e, 256), "EDID: an HDR10+ block of length 4 (no version byte)");
    // the block in the second extension; the first one without it
    EXT(128, 8);
    memcpy(&e[132], vdb, 4);
    EXT(256, 10);
    memcpy(&e[260], hdr10p, 6);
    CHECK(intel_hdr10plus_edid_declares(e, 384), "EDID: HDR10+ in the second extension");
    CHECK(!intel_hdr10plus_edid_declares(e, 383), "EDID: the second extension cut short");
    // two blocks: version 0 in the first extension, version 1 in the second: the highest
    EXT(128, 10);
    memcpy(&e[132], hdr10p, 6);
    e[137] = 0x00;
    CHECK(intel_hdr10plus_edid_version(e, 384) == 1, "EDID version: the highest of two blocks");
    e[265] = 0x00;
    CHECK(intel_hdr10plus_edid_version(e, 384) == 0, "EDID version: two blocks of version 0");
#undef EXT

    // The CTA VIC of a mode: only for an HDMI VIC whose CTA VIC the sink lists, to an HDMI 2.0 sink that
    // declares HDR10+, with HDR10PLUS_VSIF requested or the mode kept from the screen. Every combination
    // (HDMI VIC 3 is 2160p24, CTA VIC 93).
    int wrong = 0;
    for (int m = 0; m < 128; m++) {
      const u8 hv = (m & 1) ? 3 : 0, cv = (m & 2) ? 93 : 0;
      const bool h2 = m & 4, li = m & 8, de = m & 16, rq = m & 32, kp = m & 64;
      const u8 want = (hv && cv && h2 && li && de && (rq || kp)) ? 93 : 0;
      const u8 got = intel_hdr10plus_cta_vic(hv, cv, h2, li, de, rq, kp);
      if (got != want) {
        wrong++;
        CHECK(0, "CTA VIC: hdmi_vic %u cta_vic %u hdmi2 %d listed %d declares %d requested %d kept %d: %u, not %u",
              hv, cv, h2, li, de, rq, kp, got, want);
      }
    }
    CHECK(wrong == 0, "CTA VIC: all 128 combinations");
    CHECK(intel_hdr10plus_cta_vic(1, 95, true, true, true, true, false) == 95, "CTA VIC of 2160p30: 95");

    CHECK(intel_hdr10plus_cta_vic_kept(93, 93, true), "kept: the screen went out with VIC 93, same mode");
    CHECK(!intel_hdr10plus_cta_vic_kept(93, 93, false), "kept: another mode");
    CHECK(!intel_hdr10plus_cta_vic_kept(0, 93, true), "kept: the screen went out with no VIC (as DRM signals)");
    CHECK(!intel_hdr10plus_cta_vic_kept(95, 93, true), "kept: another VIC on the screen");
    CHECK(!intel_hdr10plus_cta_vic_kept(0, 0, true), "kept: no CTA VIC for the mode");

    struct hdmi_avi_infoframe avi = {.video_code = 0};
    intel_hdr10plus_avi_cta_vic(&avi, 93);
    CHECK(avi.video_code == 93, "AVI: VIC 93 where it was 0 (%u)", avi.video_code);
    avi.video_code = 16;
    intel_hdr10plus_avi_cta_vic(&avi, 0);
    CHECK(avi.video_code == 16, "AVI: no CTA VIC leaves the VIC (%u)", avi.video_code);
    struct hdmi_vendor_infoframe v2 = {.oui = HDMI_IEEE_OUI, .vic = 3, .s3d_struct = HDMI_3D_STRUCTURE_INVALID};
    intel_hdr10plus_vsi_cta_vic(&v2, 93);
    CHECK(v2.vic == 0 && intel_hdr10plus_vsi_empty(&v2), "vendor InfoFrame: the HDMI VIC taken out (%u)", v2.vic);
    v2.vic = 3;
    intel_hdr10plus_vsi_cta_vic(&v2, 0);
    CHECK(v2.vic == 3, "vendor InfoFrame: no CTA VIC leaves the HDMI VIC (%u)", v2.vic);
  }

  // linux-9972: an InfoFrame of version pv to a display of version rv (-1: none)
  {
    int wrong = 0;
    for (int pv = 0; pv < 4; pv++)
      for (int rv = -1; rv < 4; rv++) {
        const bool want = rv >= 0 && pv <= (rv < 1 ? rv : 1);
        if (intel_hdr10plus_version_fits((u8)pv, rv) != want) {
          wrong++;
          CHECK(0, "version fits: %d to %d", pv, rv);
        }
      }
    CHECK(wrong == 0, "version fits: all 20 combinations");
    CHECK(!intel_hdr10plus_version_fits(1, 0), "version fits: version 1 does not go to a display of version 0");
    CHECK(intel_hdr10plus_version_fits(0, 1), "version fits: version 0 goes to a display of version 1");
  }
  // linux-9972: where a commit that may not change the mode would have to change the AVI InfoFrame
  {
    int wrong = 0;
    for (int m = 0; m < 32; m++) {
      const u8 cv = (m & 1) ? 93 : 0, old = (m & 2) ? 93 : ((m & 4) ? 95 : 0);
      const bool same = m & 8, allow = m & 16;
      const bool want = cv && same && old != cv && !allow;
      if (intel_hdr10plus_cta_vic_needs_modeset(cv, old, same, allow) != want) {
        wrong++;
        CHECK(0, "needs modeset: vic %u old %u same %d allow %d", cv, old, same, allow);
      }
    }
    CHECK(wrong == 0, "needs modeset: all 32 combinations");
    CHECK(intel_hdr10plus_cta_vic_needs_modeset(93, 0, true, false),
          "needs modeset: the first HDR10+ after a mode that went with its HDMI VIC, without ALLOW_MODESET (issue 2)");
    CHECK(!intel_hdr10plus_cta_vic_needs_modeset(93, 93, true, false), "needs modeset: kept");
    CHECK(!intel_hdr10plus_cta_vic_needs_modeset(93, 0, true, true), "needs modeset: with ALLOW_MODESET");
    CHECK(!intel_hdr10plus_cta_vic_needs_modeset(93, 0, false, false), "needs modeset: another mode");
  }
  printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
  return fails != 0;
}
