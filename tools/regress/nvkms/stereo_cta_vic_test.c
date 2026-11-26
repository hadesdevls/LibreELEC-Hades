/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the CTA VIC in the AVI InfoFrame next to a client's half 3D layout in a mode that
 * NVKMS names by its HDMI_VIC (nvidia-open-kernel-modules-9086 and nvidia-legacy-open-9083: nvHadesHalfLayoutVsif(),
 * nvHadesEdidRead3D(), nvHadesEdidDeclares3D() and nvHadesStereoAviCtaVic() of nvkms-hades.h, the same in both), built
 * against either tree, with the AVI InfoFrames built by NVIDIA's
 * own code of the tree (NvTiming_ParseEDIDInfo(), NvTiming_ConstructVideoInfoframeCtrl() and
 * NvTiming_ConstructVideoInfoframe() of src/common/modeset/timing), as SendVideoInfoFrame() changes them, and laid
 * on the wire as the HAL does (nvkms-evo1.c: the packet type of hdmi_spec.h, the checksum in PB0).
 *
 *  - the vendor InfoFrames that Kodi sends (DRMStereo.h, HalfLayoutInfoFrame()) and others, read by
 *    nvHadesHalfLayoutVsif(), and the bytes of the vendor InfoFrame of side by side (half) and of top and bottom
 *    as NVKMS sends a client's one (SendUserDefinedVSIF()) and as Linux packs them (hdmi_vendor_infoframe_pack_only());
 *  - what Linux reads of an EDID for it (drm_edid.c: parse_cta_vdb() and do_hdmi_vsdb_modes()), on two real EDIDs
 *    (argv[1]: the folder of the inputs of inputs-nvkms.tsv) and on synthetic ones that each test one rule; the rule
 *    of Linux whatever the HDMI version of the display (vic_for_avi_infoframe() keeps a VIC that the sink lists);
 *  - the decision, and the AVI InfoFrame that carries the CTA VIC: the same bytes that NVKMS sends for that CTA
 *    mode (VIC 93, 94, 95 and 98), with the checksum.
 *
 * Build against a patched tree T of the open modules or of nvidia-legacy-open (the timing sources of T compiled
 * as NVKMS compiles them: -DNVT_USE_NVKMS and the folders of its interfaces, as build_st of run-nvkms.sh):
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I T/src/nvidia-modeset/include \
 *       -I T/src/common/sdk/nvidia/inc -I T/src/common/inc -I T/src/common/modeset/timing \
 *       -I T/kernel-open/common/inc -o t stereo_cta_vic_test.c T/src/common/modeset/timing/(each .o) && ./t <inputs>
 */
#include <stddef.h> /* (nvkms-hades.h uses NULL and takes its types from nvtypes.h) */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "nvtiming.h"
#include "hdmi_spec.h"

#include "nvkms-hades.h"

/* The timing code is built as NVKMS builds it (-DNVT_USE_NVKMS: build_st of run-nvkms.sh), with the snprintf of
 * NVKMS's interface to the kernel; here the C library's */
int nvkms_snprintf(char *str, size_t size, const char *format, ...);
int nvkms_snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    int n;
    va_start(ap, format);
    n = vsnprintf(str, size, format, ap);
    va_end(ap);
    return n;
}

static int fails, checks;
#define CHECK(cond, ...)                                                                                         \
    do {                                                                                                         \
        checks++;                                                                                                \
        if (!(cond)) {                                                                                           \
            fails++;                                                                                             \
            printf("FAIL (line %d): ", __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                 \
            printf("\n");                                                                                        \
        }                                                                                                        \
    } while (0)

/* ------------------------------------------------------------------------------------------- the wire */

/* An InfoFrame on the wire as the HAL lays it (nvkms-evo1.c): the HDMI packet type, version and length of the
 * header, PB0 the checksum (~sum + 1 of every byte), the payload */
static NvU32 Wire(NvU8 packetType, NvU8 version, NvU8 length, const NvU8 *payload, NvU8 *out)
{
    NvU8 sum = 0;
    NvU32 i;

    out[0] = packetType;
    out[1] = version;
    out[2] = length;
    out[3] = 0;
    memcpy(out + 4, payload, length);
    for (i = 0; i < 4u + length; i++) {
        sum += out[i];
    }
    out[3] = (NvU8)(~sum + 1);
    return 4u + length;
}

static int SameBytes(const NvU8 *a, NvU32 n, const char *hex)
{
    NvU32 i;
    if (strlen(hex) != 3 * n - 1) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned int b;
        if (sscanf(hex + 3 * i, "%2x", &b) != 1 || b != a[i]) {
            return 0;
        }
    }
    return 1;
}

static void Print(const char *what, const NvU8 *a, NvU32 n)
{
    NvU32 i;
    printf("%s:", what);
    for (i = 0; i < n; i++) {
        printf(" %02X", a[i]);
    }
    printf("\n");
}

/* The vendor InfoFrame of a 3D structure as Linux packs it (drivers/video/hdmi.c, hdmi_vendor_infoframe_length()
 * and hdmi_vendor_infoframe_pack_only()): the payload from the OUI on */
static NvU32 LinuxVsifPayload(NvU8 s3dStruct, NvU8 s3dExt, NvU8 *p)
{
    const NvU32 length = (s3dStruct >= 8) ? 6 : 5; /* HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF and above: 6 */
    memset(p, 0, 6);
    p[0] = 0x03; /* HDMI_IEEE_OUI 0x000c03, least significant byte first */
    p[1] = 0x0c;
    p[2] = 0x00;
    p[3] = 0x2 << 5; /* video format: 3D */
    p[4] = (NvU8)((s3dStruct & 0xf) << 4);
    if (s3dStruct >= 8) {
        p[5] = (NvU8)((s3dExt & 0xf) << 4);
    }
    return length;
}

/* ------------------------------------------------------------------------------------------- EDIDs */

struct Ed {
    NvU8 b[384];
    NvU32 at;  /* where the next data block of the extension goes */
    NvU32 ext; /* the offset of the extension being written */
};

static void Sum(NvU8 *block)
{
    NvU8 s = 0;
    int i;
    for (i = 0; i < 127; i++) {
        s += block[i];
    }
    block[127] = (NvU8)(0x100 - s);
}

/* a base block of EDID 1.3 with one extension: a CTA-861 one of revision rev */
static void EdBegin(struct Ed *e, NvU8 rev)
{
    static const NvU8 header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    memset(e, 0, sizeof(*e));
    memcpy(e->b, header, 8);
    e->b[8] = 0x22; /* "HAD" */
    e->b[9] = 0x24;
    e->b[18] = 1;
    e->b[19] = 3;
    e->b[126] = 1;
    Sum(e->b);
    e->ext = 128;
    e->b[128] = 0x02;
    e->b[129] = rev;
    e->at = 132;
}

static void EdAdd(struct Ed *e, const NvU8 *db, NvU32 n)
{
    memcpy(e->b + e->at, db, n);
    e->at += n;
}

static void EdEnd(struct Ed *e)
{
    e->b[e->ext + 2] = (NvU8)(e->at - e->ext);
    Sum(e->b + e->ext);
}

/* a second CTA-861 extension (the base block then says two) */
static void EdNextExt(struct Ed *e, NvU8 rev)
{
    EdEnd(e);
    e->b[126] = 2;
    Sum(e->b);
    e->ext += 128;
    e->b[e->ext] = 0x02;
    e->b[e->ext + 1] = rev;
    e->at = e->ext + 4;
}

#define VDB(...)                                                                                                 \
    do {                                                                                                         \
        const NvU8 svds_[] = {__VA_ARGS__};                                                                      \
        NvU8 db_[32];                                                                                            \
        db_[0] = (NvU8)((2 << 5) | sizeof(svds_));                                                               \
        memcpy(db_ + 1, svds_, sizeof(svds_));                                                                   \
        EdAdd(e, db_, 1 + sizeof(svds_));                                                                        \
    } while (0)

/* an HDMI VSDB: byte 8 (flags8: 0x20 video present, 0x80 latency, 0x40 I_latency), its latency bytes, the byte
 * of 3D_present and 3D_Multi_present, HDMI_VIC_LEN 0, then the 3D bytes */
static void HdmiVsdb(struct Ed *e, NvU8 flags8, NvU8 present, const NvU8 *b3d, NvU32 n3d)
{
    NvU8 db[32];
    NvU32 n = 0, i, lat = (flags8 & 0x80) ? ((flags8 & 0x40) ? 4 : 2) : 0;

    db[n++] = 0; /* header, below */
    db[n++] = 0x03;
    db[n++] = 0x0c;
    db[n++] = 0x00;
    db[n++] = 0x10; /* physical address 1.0.0.0 */
    db[n++] = 0x00;
    db[n++] = 0x00;
    db[n++] = 0x3c; /* 300 MHz */
    db[n++] = flags8;
    for (i = 0; i < lat; i++) {
        db[n++] = 0x10;
    }
    db[n++] = present;
    db[n++] = (NvU8)n3d; /* HDMI_VIC_LEN 0, HDMI_3D_LEN */
    memcpy(db + n, b3d, n3d);
    n += n3d;
    db[0] = (NvU8)((3 << 5) | (n - 1));
    EdAdd(e, db, n);
}

static const NvU8 HF_VSDB_SCDC[8] = {0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x80, 0x00};
static const NvU8 HF_VSDB_NO_SCDC[8] = {0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x00, 0x00};
static const NvU8 HF_SCDB_SCDC[8] = {0xe7, 0x79, 0x00, 0x00, 0x01, 0x78, 0x80, 0x00};
static const NvU8 Y420CMDB_EMPTY[2] = {0xe1, 0x0f};
static const NvU8 Y420CMDB_ZERO[3] = {0xe2, 0x0f, 0x00};
static const NvU8 Y420CMDB_SET[3] = {0xe2, 0x0f, 0x01};
static const NvU8 Y420VDB_97[3] = {0xe2, 0x0e, 97};
static const NvU8 Y420VDB_UNKNOWN[4] = {0xe3, 0x0e, 0x00, 128};
static const NvU8 Y420VDB_93[3] = {0xe2, 0x0e, 93};

/* 3D_Structure_ALL of side by side (half) and top and bottom (and frame packing) */
#define ALL_HI 0x01 /* bit 8 */
#define ALL_LO 0x41 /* bits 6 and 0 */

static struct NvHadesEdid3D Read(const struct Ed *e)
{
    struct NvHadesEdid3D r;
    memset(&r, 0xA5, sizeof(r)); /* read sets every field */
    nvHadesEdidRead3D(e->b, (NvU32)(e->ext + 128), &r);
    return r;
}

/* the decision for HDMI_VIC 3 (3840x2160 at 24 Hz) and a layout */
static NvU32 Decide(const struct NvHadesEdid3D *r, NvU32 half, NvU32 *why)
{
    return nvHadesStereoAviCtaVic(3, half, r, why);
}

/* ------------------------------------------------------------------------------------------- AVI */

static NVT_EDID_INFO c5info;

/* the AVI InfoFrame of a timing as NVKMS makes it at RGB (modepool: the controls DONTCARE, then
 * NvTiming_ConstructVideoInfoframeCtrl(); SendVideoInfoFrame(): the colour, then the CTA VIC of Hades) */
static NvU32 AviWire(const NVT_TIMING *t, NvU32 ctaVic, NvU8 *out, NvU8 *aviVic)
{
    NVT_VIDEO_INFOFRAME_CTRL ctrl;
    NVT_VIDEO_INFOFRAME frame;

    memset(&ctrl, NVT_INFOFRAME_CTRL_DONTCARE, sizeof(ctrl));
    memset(&frame, 0, sizeof(frame));
    CHECK(NvTiming_ConstructVideoInfoframeCtrl(t, &ctrl) == NVT_STATUS_SUCCESS, "AVI controls");
    ctrl.color_space = NVT_VIDEO_INFOFRAME_BYTE1_Y1Y0_RGB;
    *aviVic = ctrl.video_format_id;
    if (ctaVic != 0) {
        ctrl.video_format_id = (NvU8)ctaVic;
        if (nvHadesCtaVicAspectNoData(ctaVic)) {
            ctrl.pic_aspect_ratio = NVT_VIDEO_INFOFRAME_BYTE2_M1M0_NO_DATA;
        }
    }
    CHECK(NvTiming_ConstructVideoInfoframe(&c5info, &ctrl, NULL, &frame) == NVT_STATUS_SUCCESS, "AVI");
    CHECK(frame.type == NVT_INFOFRAME_TYPE_VIDEO, "an AVI InfoFrame");
    return Wire(hdmi_pktType_AviInfoFrame, frame.version, frame.length, &frame.byte1, out);
}

static int ReadFile(const char *dir, const char *name, NvU8 *buf, NvU32 cap, NvU32 *n)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    *n = (NvU32)fread(buf, 1, cap, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    NvU8 p[8], w[40];
    NvU32 n, why, i;
    struct Ed ed, *e = &ed;
    struct NvHadesEdid3D r;

    /* ---- the client's vendor InfoFrames */
    {
        static const NvU8 KODI_SBS[6] = {0x03, 0x0C, 0x00, 0x40, 0x80, 0x00};
        static const NvU8 KODI_TAB[5] = {0x03, 0x0C, 0x00, 0x40, 0x60};
        static const NvU8 KODI_NONE[5] = {0x03, 0x0C, 0x00, 0x00, 0x00}; /* NoLayoutInfoFrame(): no 3D */
        static const NvU8 FP[5] = {0x03, 0x0C, 0x00, 0x40, 0x00};
        static const NvU8 SBS_FULL[5] = {0x03, 0x0C, 0x00, 0x40, 0x30};
        static const NvU8 SBS_QUINCUNX[6] = {0x03, 0x0C, 0x00, 0x40, 0x80, 0x40};
        static const NvU8 HDMI_VIC3[5] = {0x03, 0x0C, 0x00, 0x20, 0x03};
        static const NvU8 DOLBY[27] = {0x46, 0xD0, 0x00, 0x02};
        static const NvU8 OUI_OTHER[6] = {0x03, 0x0C, 0x01, 0x40, 0x80, 0x00};

        CHECK(nvHadesHalfLayoutVsif(KODI_SBS, 6) == NV_HADES_HALF_SIDE_BY_SIDE, "Kodi's side by side (half)");
        CHECK(nvHadesHalfLayoutVsif(KODI_TAB, 5) == NV_HADES_HALF_TOP_AND_BOTTOM, "Kodi's top and bottom");
        CHECK(nvHadesHalfLayoutVsif(KODI_NONE, 5) == NV_HADES_HALF_NONE, "Kodi's 'no 3D' one");
        CHECK(nvHadesHalfLayoutVsif(KODI_SBS, 5) == NV_HADES_HALF_OTHER, "side by side without its PB6");
        CHECK(nvHadesHalfLayoutVsif(KODI_SBS, 4) == NV_HADES_HALF_NONE, "four bytes: no 3D_Structure");
        CHECK(nvHadesHalfLayoutVsif(FP, 5) == NV_HADES_HALF_OTHER, "frame packing is another structure");
        CHECK(nvHadesHalfLayoutVsif(SBS_FULL, 5) == NV_HADES_HALF_OTHER, "side by side (full) is another");
        CHECK(nvHadesHalfLayoutVsif(SBS_QUINCUNX, 6) == NV_HADES_HALF_OTHER, "3D_Ext_Data 4 is not Linux's");
        CHECK(nvHadesHalfLayoutVsif(HDMI_VIC3, 5) == NV_HADES_HALF_NONE, "an HDMI_VIC one is no 3D");
        CHECK(nvHadesHalfLayoutVsif(DOLBY, 27) == NV_HADES_HALF_NONE, "a Dolby one is no 3D");
        CHECK(nvHadesHalfLayoutVsif(OUI_OTHER, 6) == NV_HADES_HALF_NONE, "another OUI");

        /* the bytes on the wire: as NVKMS sends a client's (SendUserDefinedVSIF(): type vendor-specific,
         * version 1, the length of the payload) and as Linux packs the same 3D structure */
        n = Wire(hdmi_pktType_VendorSpecInfoFrame, 1, 6, KODI_SBS, w);
        Print("vendor InfoFrame, side by side (half)", w, n);
        CHECK(SameBytes(w, n, "81 01 06 A9 03 0C 00 40 80 00"), "the vendor InfoFrame of side by side (half)");
        CHECK(LinuxVsifPayload(8, 0, p) == 6 && memcmp(p, KODI_SBS, 6) == 0, "Linux packs the same one");
        n = Wire(hdmi_pktType_VendorSpecInfoFrame, 1, 5, KODI_TAB, w);
        Print("vendor InfoFrame, top and bottom", w, n);
        CHECK(SameBytes(w, n, "81 01 05 CA 03 0C 00 40 60"), "the vendor InfoFrame of top and bottom");
        CHECK(LinuxVsifPayload(6, 0, p) == 5 && memcmp(p, KODI_TAB, 5) == 0, "Linux packs the same one");
    }

    /* ---- the CTA VIC of each HDMI_VIC */
    CHECK(nvHadesCtaVicOfHdmiVic(1) == 95 && nvHadesCtaVicOfHdmiVic(2) == 94 && nvHadesCtaVicOfHdmiVic(3) == 93 &&
              nvHadesCtaVicOfHdmiVic(4) == 98 && nvHadesCtaVicOfHdmiVic(0) == 0 && nvHadesCtaVicOfHdmiVic(5) == 0,
          "HDMI_VIC 1-4: CTA VIC 95, 94, 93, 98");

    /* ---- synthetic EDIDs, each one rule */
    /* (a) HDMI 1.4: 3D_Structure_ALL declares both half layouts for VIC 93 (SVD 0), no HF-VSDB, no 4:2:0: the CTA VIC,
     * as Linux sends it to any sink that lists the VIC (vic_for_avi_infoframe()) */
    EdBegin(e, 3);
    VDB(93, 16, 31, 32);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 4 && r.vic[0] == 93 && r.vic[3] == 32, "(a) the SVDs");
    CHECK(nvHadesEdidDeclares3D(&r, 93, NV_HADES_HALF_SIDE_BY_SIDE) &&
              nvHadesEdidDeclares3D(&r, 93, NV_HADES_HALF_TOP_AND_BOTTOM),
          "(a) declares both for VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93 && why == NV_HADES_3D_AVI_CTA_VIC,
          "(a) HDMI 1.4, side by side (half): CTA VIC 93, as Linux (why %u)", why);
    CHECK(Decide(&r, NV_HADES_HALF_TOP_AND_BOTTOM, &why) == 93 && why == NV_HADES_3D_AVI_CTA_VIC,
          "(a) HDMI 1.4, top and bottom: CTA VIC 93, as Linux (why %u)", why);

    /* (b) the same with SCDC_Present in an HF-VSDB (HDMI 2.0): the same */
    EdBegin(e, 3);
    VDB(93, 16, 31, 32);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93 && why == NV_HADES_3D_AVI_CTA_VIC,
          "(b) side by side (half): CTA VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_TOP_AND_BOTTOM, &why) == 93 && why == NV_HADES_3D_AVI_CTA_VIC,
          "(b) top and bottom: CTA VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_OTHER, &why) == 0 && why == NV_HADES_3D_AVI_OTHER_LAYOUT,
          "(b) another 3D structure: VIC 0");
    CHECK(Decide(&r, NV_HADES_HALF_NONE, &why) == 0 && why == NV_HADES_3D_AVI_NONE, "(b) no 3D: nothing");
    CHECK(nvHadesStereoAviCtaVic(0, NV_HADES_HALF_SIDE_BY_SIDE, &r, &why) == 0 && why == NV_HADES_3D_AVI_NONE,
          "(b) a mode of no HDMI_VIC: nothing");
    CHECK(nvHadesStereoAviCtaVic(2, NV_HADES_HALF_SIDE_BY_SIDE, &r, &why) == 0 &&
              why == NV_HADES_3D_AVI_UNDECLARED,
          "(b) HDMI_VIC 2 (VIC 94, not listed): undeclared");
    CHECK(nvHadesEdidDeclares3D(&r, 93, NV_HADES_HALF_NONE) == NV_FALSE, "(b) no structure, nothing declared");

    /* (c) 3D_MASK leaves out SVD 0 (VIC 93) and keeps SVD 1 (VIC 16) */
    EdBegin(e, 3);
    VDB(93, 16, 31, 32);
    {
        const NvU8 b3d[4] = {ALL_HI, ALL_LO, 0x00, 0x02};
        HdmiVsdb(e, 0x20, 0x80 | (2 << 5), b3d, 4);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(!nvHadesEdidDeclares3D(&r, 93, NV_HADES_HALF_SIDE_BY_SIDE) &&
              nvHadesEdidDeclares3D(&r, 16, NV_HADES_HALF_SIDE_BY_SIDE),
          "(c) 3D_MASK: VIC 16 and not VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(c) undeclared: VIC 0");

    /* (d) 2D_VIC_order entries: SVD 0 top and bottom; SVD 0 side by side with 3D_Detail 0 (not 1) */
    EdBegin(e, 3);
    VDB(93, 16);
    {
        const NvU8 b3d[3] = {0x06, 0x08, 0x00};
        HdmiVsdb(e, 0x20, 0x80, b3d, 3);
    }
    EdAdd(e, HF_SCDB_SCDC, sizeof(HF_SCDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_TOP_AND_BOTTOM, &why) == 93, "(d) top and bottom by its entry: CTA VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(d) side by side with 3D_Detail 0: undeclared");

    /* (e) 2D_VIC_order: SVD 1 (VIC 93) side by side with 3D_Detail 1, after I_latency fields; an empty Y420CMDB
     * beside */
    EdBegin(e, 3);
    VDB(16, 93);
    {
        const NvU8 b3d[2] = {0x18, 0x10};
        HdmiVsdb(e, 0x20 | 0x80 | 0x40, 0x80, b3d, 2);
    }
    EdAdd(e, Y420CMDB_EMPTY, sizeof(Y420CMDB_EMPTY));
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93, "(e) side by side (half) by its entry: CTA VIC 93");
    CHECK(Decide(&r, NV_HADES_HALF_TOP_AND_BOTTOM, &why) == 0, "(e) not top and bottom");

    /* (f) VIC 93 as SVD 16: 3D_Structure_ALL covers the first sixteen only */
    EdBegin(e, 3);
    VDB(1, 2, 3, 4, 5, 16, 17, 18, 19, 20, 21, 22, 31, 32, 33, 34, 93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, Y420CMDB_SET, sizeof(Y420CMDB_SET));
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 17, "(f) 17 SVDs");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(f) VIC 93 past the sixteenth SVD: undeclared");
    CHECK(nvHadesEdidDeclares3D(&r, 34, NV_HADES_HALF_TOP_AND_BOTTOM), "(f) the sixteenth SVD is");

    /* (g) a Y420CMDB with no bit set, a Y420VDB of unknown VICs, an HF-VSDB without SCDC_Present (no HDMI 2.0):
     * nothing of them changes what the EDID declares */
    EdBegin(e, 3);
    VDB(93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, Y420CMDB_ZERO, sizeof(Y420CMDB_ZERO));
    EdAdd(e, Y420VDB_UNKNOWN, sizeof(Y420VDB_UNKNOWN));
    EdAdd(e, HF_VSDB_NO_SCDC, sizeof(HF_VSDB_NO_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 1 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93 && why == NV_HADES_3D_AVI_CTA_VIC,
          "(g) CTA VIC 93");

    /* (h) a Y420VDB with a known VIC, and the HDMI VSDB in a second extension, after the VDB of the first */
    EdBegin(e, 3);
    VDB(93);
    EdAdd(e, Y420VDB_97, sizeof(Y420VDB_97));
    EdNextExt(e, 3);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93, "(h) declared across extensions: CTA VIC 93");

    /* (i) two HDMI VSDBs, the second one declares: Linux reads every one (add_cea_modes()) */
    EdBegin(e, 3);
    VDB(93);
    HdmiVsdb(e, 0x20, 0x00, NULL, 0);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_TOP_AND_BOTTOM, &why) == 93, "(i) the second HDMI VSDB counts");

    /* (j) the extension of (b) of revision 2, then with a wrong checksum: Linux reads neither */
    EdBegin(e, 2);
    VDB(93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 0 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0, "(j) revision 2: nothing read");
    e->b[129] = 3;
    Sum(e->b + 128);
    r = Read(e);
    CHECK(r.svds == 1 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93, "(j) revision 3: read");
    e->b[200] ^= 0x01;
    r = Read(e);
    CHECK(r.svds == 0 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0, "(j) a wrong checksum: nothing read");

    /* (k) an unknown VIC (0 and 128) among the SVDs is 0, and takes its place */
    EdBegin(e, 3);
    VDB(0, 128, 93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 3 && r.vic[0] == 0 && r.vic[1] == 0 && r.vic[2] == 93, "(k) unknown VICs are 0");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 93, "(k) VIC 93 at its place");
    CHECK(!nvHadesEdidDeclares3D(&r, 0, NV_HADES_HALF_SIDE_BY_SIDE), "(k) VIC 0 is never declared");

    /* (l) 3840x2160 at 30 Hz (HDMI_VIC 1, VIC 95) and 4096x2160 at 24 Hz (HDMI_VIC 4, VIC 98) */
    EdBegin(e, 3);
    VDB(95, 98);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(nvHadesStereoAviCtaVic(1, NV_HADES_HALF_SIDE_BY_SIDE, &r, &why) == 95, "(l) HDMI_VIC 1: VIC 95");
    CHECK(nvHadesStereoAviCtaVic(4, NV_HADES_HALF_TOP_AND_BOTTOM, &r, &why) == 98, "(l) HDMI_VIC 4: VIC 98");
    CHECK(nvHadesStereoAviCtaVic(3, NV_HADES_HALF_TOP_AND_BOTTOM, &r, &why) == 0, "(l) HDMI_VIC 3: VIC 93 unlisted");

    /* (n) the declaring EDID of (b) with byte 2 of its extension 128: no data block collection for Linux
     * (cea_db_collection_size(): 4 to 127) */
    EdBegin(e, 3);
    VDB(93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    e->b[130] = 128;
    Sum(e->b + 128);
    r = Read(e);
    CHECK(r.svds == 0 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0, "(n) byte 2 past 127: nothing read");

    /* (o) a VDB and an HF-VSDB of 6 bytes, and no HDMI VSDB: nothing declared */
    EdBegin(e, 3);
    VDB(93);
    {
        const NvU8 hf6[7] = {0x66, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x80};
        EdAdd(e, hf6, sizeof(hf6));
    }
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 1 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(o) no HDMI VSDB: undeclared");

    /* (p) VIC 93 listed only in a Y420VDB, 3D_Structure_ALL over every SVD: the indices of 3D_MASK and 2D_VIC_order
     * are those of the SVDs of the video data blocks (parse_cta_vdb()), and a format of a Y420VDB is none of them, so
     * Linux makes no 3D mode of VIC 93 and the AVI InfoFrame keeps VIC 0; a Y420CMDB of 9 map bytes beside */
    EdBegin(e, 3);
    VDB(16);
    EdAdd(e, Y420VDB_93, sizeof(Y420VDB_93));
    {
        const NvU8 cm9[11] = {0xea, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        EdAdd(e, cm9, sizeof(cm9));
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(r.svds == 1 && r.vic[0] == 16, "(p) one SVD, VIC 16 (%u SVDs)", r.svds);
    CHECK(nvHadesEdidDeclares3D(&r, 16, NV_HADES_HALF_SIDE_BY_SIDE), "(p) VIC 16 declared");
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(p) VIC 93 of a Y420VDB only: undeclared, VIC 0");

    /* (m) an HDMI VSDB whose HDMI_3D_LEN runs past the block: Linux reads none of its 3D part */
    EdBegin(e, 3);
    VDB(93);
    {
        const NvU8 b3d[2] = {ALL_HI, ALL_LO};
        HdmiVsdb(e, 0x20, 0x80 | (1 << 5), b3d, 2);
        e->b[e->at - 3] = 4; /* HDMI_3D_LEN 4, with 2 bytes in the block */
    }
    EdAdd(e, HF_VSDB_SCDC, sizeof(HF_VSDB_SCDC));
    EdEnd(e);
    r = Read(e);
    CHECK(Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0 && why == NV_HADES_3D_AVI_UNDECLARED,
          "(m) a 3D part past the block: undeclared");

    /* no EDID */
    nvHadesEdidRead3D(NULL, 0, &r);
    CHECK(r.svds == 0 && Decide(&r, NV_HADES_HALF_SIDE_BY_SIDE, &why) == 0, "no EDID: nothing");

    /* ---- the real EDIDs (the inputs of inputs-nvkms.tsv) */
    if (argc < 2) {
        printf("no folder of inputs\n");
        fails++;
    } else {
        static NvU8 raw[1024];
        static const NvU8 TV_VICS[16] = {16, 31, 4, 19, 5, 20, 3, 2, 18, 32, 33, 34, 21, 1, 96, 97};
        NVT_TIMING t;
        NvU8 a[40], c[40], aviVic, ctaVic;
        NvU32 na, nc, k;
        int found;

        /* a 3D display of a user's report: 3D up to 1080p, VIC 93 its seventeenth SVD */
        if (!ReadFile(argv[1], "edid/report-3d-1080p.bin", raw, sizeof(raw), &n)) {
            printf("no edid/report-3d-1080p.bin\n");
            fails++;
        } else {
            nvHadesEdidRead3D(raw, n, &r);
            CHECK(n == 256, "the 3D display: 256 bytes");
            CHECK(r.svds == 24 && memcmp(r.vic, TV_VICS, 16) == 0, "the 3D display: its first sixteen SVDs");
            CHECK(r.structures[0] == (NV_HADES_EDID_3D_TOP_AND_BOTTOM | NV_HADES_EDID_3D_SIDE_BY_SIDE),
                  "the 3D display: VIC 16 top and bottom and side by side");
            for (i = 1; i < 14; i++) {
                CHECK(r.structures[i] == (NV_HADES_EDID_3D_FRAME_PACKING | NV_HADES_EDID_3D_TOP_AND_BOTTOM |
                                          NV_HADES_EDID_3D_SIDE_BY_SIDE),
                      "the 3D display: SVD %u all three", i);
            }
            CHECK(r.structures[14] == 0 && r.structures[15] == 0, "the 3D display: none for 2160p at 50 and 60 Hz");
            for (k = 1; k <= 4; k++) {
                CHECK(nvHadesStereoAviCtaVic(k, NV_HADES_HALF_SIDE_BY_SIDE, &r, &why) == 0 &&
                          why == NV_HADES_3D_AVI_UNDECLARED,
                      "the 3D display: HDMI_VIC %u side by side undeclared: VIC 0, as before", k);
                CHECK(nvHadesStereoAviCtaVic(k, NV_HADES_HALF_TOP_AND_BOTTOM, &r, &why) == 0 &&
                          why == NV_HADES_3D_AVI_UNDECLARED,
                      "the 3D display: HDMI_VIC %u top and bottom undeclared: VIC 0, as before", k);
            }
            /* how NVKMS names its 2160p modes: no UHD_VIC in its HF-VSDB, so ValidateModeIndexEdid() of
             * nvkms-modepool.c takes the first timing that matches, and the parser puts the HDMI_VIC ones first */
            memset(&c5info, 0, sizeof(c5info));
            CHECK(NvTiming_ParseEDIDInfo(raw, n, &c5info) == NVT_STATUS_SUCCESS, "the 3D display parsed by NvTiming");
            CHECK(c5info.hdmiForumInfo.uhd_vic == 0, "the 3D display: no UHD_VIC in its HF-VSDB");
            for (i = 0, found = 0; i < c5info.total_timings; i++) {
                found += NVT_GET_TIMING_STATUS_TYPE(c5info.timing[i].etc.status) == NVT_TYPE_HDMI_EXT;
            }
            printf("the 3D display: %d timings of an HDMI_VIC\n", found);
        }

        /* the EDID of an LG C5 with 3D_present added to its HDMI VSDB (a test EDID): HDMI 2.0, UHD_VIC in its
         * HF-VSDB, HDMI_VIC 1 to 4, nothing declared in 3D */
        if (!ReadFile(argv[1], "edid/lg-c5-3d-present.bin", raw, sizeof(raw), &n)) {
            printf("no edid/lg-c5-3d-present.bin\n");
            fails++;
        } else {
            nvHadesEdidRead3D(raw, n, &r);
            CHECK(r.svds == 29, "the C5: 29 SVDs (%u)", r.svds);
            for (i = 0, found = 0; i < 16; i++) {
                found |= r.structures[i] != 0;
            }
            CHECK(!found, "the C5: no 3D structure listed (3D_present only)");
            CHECK(nvHadesStereoAviCtaVic(3, NV_HADES_HALF_SIDE_BY_SIDE, &r, &why) == 0 &&
                      why == NV_HADES_3D_AVI_UNDECLARED,
                  "the C5: undeclared at 2160p: VIC 0, as before");

            /* the AVI InfoFrames, with NVIDIA's timing code on this EDID: its HDMI_VIC timings (1 to 4) */
            memset(&c5info, 0, sizeof(c5info));
            CHECK(NvTiming_ParseEDIDInfo(raw, n, &c5info) == NVT_STATUS_SUCCESS, "the C5 parsed by NvTiming");
            /* UHD_VIC in its HF-VSDB: ValidateModeIndexEdid() looks among the CTA-861 timings first, so NVKMS
             * names its 2160p24 by CTA VIC 93 itself, though the parser puts the HDMI_VIC timings first
             * (prioritizeEdidHDMIExtTiming()) */
            CHECK(c5info.hdmiForumInfo.uhd_vic == 1, "the C5: UHD_VIC in its HF-VSDB");
            {
                int ext = -1, cta = -1;
                for (i = 0; i < c5info.total_timings; i++) {
                    const NvU32 st = c5info.timing[i].etc.status;
                    if (ext < 0 && NVT_GET_TIMING_STATUS_TYPE(st) == NVT_TYPE_HDMI_EXT && NVT_GET_TIMING_STATUS_SEQ(st) == 3) {
                        ext = (int)i;
                    }
                    if (cta < 0 && NVT_GET_TIMING_STATUS_TYPE(st) == NVT_TYPE_EDID_861ST && NVT_GET_CEA_FORMAT(st) == 93) {
                        cta = (int)i;
                    }
                }
                CHECK(ext >= 0 && cta >= 0 && ext < cta,
                      "the C5: the timing of HDMI_VIC 3 (%d) before the one of CTA VIC 93 (%d)", ext, cta);
            }
            for (k = 1; k <= 4; k++) {
                const NvU32 vic = nvHadesCtaVicOfHdmiVic(k);
                /* AVI version 2, 13 bytes; PB0 the checksum; PB1 RGB; PB2 16:9 (no data for VIC 98, 256:135) and
                 * the active format as the picture; PB3 IT content (NVKMS's own); PB4 the CTA VIC */
                static const char *const WANT[5] = {
                    NULL,
                    "82 02 0D 68 00 28 80 5F 00 00 00 00 00 00 00 00 00", /* VIC 95 */
                    "82 02 0D 69 00 28 80 5E 00 00 00 00 00 00 00 00 00", /* VIC 94 */
                    "82 02 0D 6A 00 28 80 5D 00 00 00 00 00 00 00 00 00", /* VIC 93 */
                    "82 02 0D 85 00 08 80 62 00 00 00 00 00 00 00 00 00", /* VIC 98 */
                };
                char what[80];

                for (i = 0, found = 0; i < c5info.total_timings; i++) {
                    if (NVT_GET_TIMING_STATUS_TYPE(c5info.timing[i].etc.status) == NVT_TYPE_HDMI_EXT &&
                        NVT_GET_TIMING_STATUS_SEQ(c5info.timing[i].etc.status) == k) {
                        t = c5info.timing[i];
                        found = 1;
                        break;
                    }
                }
                CHECK(found, "the C5: its timing of HDMI_VIC %u", k);
                if (!found) {
                    continue;
                }
                na = AviWire(&t, 0, a, &aviVic);
                CHECK(aviVic == 0 && a[7] == 0, "HDMI_VIC %u: NVKMS's AVI says VIC 0 (%u)", k, aviVic);
                na = AviWire(&t, vic, a, &aviVic);
                snprintf(what, sizeof(what), "AVI InfoFrame of HDMI_VIC %u with CTA VIC %u", k, vic);
                Print(what, a, na);
                CHECK(a[7] == vic, "HDMI_VIC %u: the AVI says VIC %u", k, vic);
                CHECK(SameBytes(a, na, WANT[k]), "HDMI_VIC %u: the AVI InfoFrame of VIC %u, byte for byte", k, vic);

                /* the same bytes as NVKMS sends for that CTA mode */
                memset(&t, 0, sizeof(t));
                CHECK(NvTiming_EnumCEA861bTiming(vic, &t) == NVT_STATUS_SUCCESS, "VIC %u enumerated", vic);
                nc = AviWire(&t, 0, c, &ctaVic);
                CHECK(ctaVic == vic && na == nc && memcmp(a, c, na) == 0,
                      "HDMI_VIC %u with the CTA VIC: the AVI InfoFrame of the CTA mode %u", k, vic);
            }
        }
    }

    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
