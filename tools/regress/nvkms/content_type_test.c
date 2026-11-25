/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the content type of the AVI InfoFrame of NVKMS (nvidia-open-kernel-modules-9085,
 * nvidia-legacy-open-9082: nvHadesAviContent(), nvHadesContentTypeOf() and nvHadesContentTypeOverride() of
 * nvkms-hades.h), with the AVI InfoFrames built by NVIDIA's own code of the tree: NvTiming_ConstructVideoInfoframeCtrl()
 * and NvTiming_ConstructVideoInfoframe() of src/common/modeset/timing, for the CTA modes of the LG C5 (VIC 93,
 * 3840x2160 at 24 Hz, and VIC 97, at 60 Hz) parsed from its EDID as the TCL bar repeats it. SendVideoInfoFrame()
 * applies the content type to the controls the same way, after everything else it changes (Dolby Vision
 * Low-Latency, the CTA VIC).
 *
 * For each case: the bytes of the AVI InfoFrame, and that every byte but ITC (byte 3 bit 7), CN1 CN0 (byte 5 bits
 * 5:4) and the checksum is the one NVKMS makes by itself, so that the tunnel of Dolby Vision keeps its AVI
 * InfoFrame but for those bits. The rule before the patch (NVKMS's own ITC 1, Graphics, whatever the client asks
 * and with Dolby Vision) is run too and must differ where the patch changes something.
 *
 * Build against a patched tree T of the open modules (the timing sources of T compiled with it):
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I T/src/nvidia-modeset/include \
 *       -I T/src/common/sdk/nvidia/inc -I T/src/common/inc -I T/src/common/modeset/timing \
 *       -I T/kernel-open/common/inc -o t content_type_test.c T/src/common/modeset/timing/(each .c) && ./t
 */
#include <stddef.h> /* (nvkms-hades.h uses NULL and takes its types from nvtypes.h) */

#include <stdio.h>
#include <string.h>

#include "nvtiming.h"

#include "nvkms-hades.h"

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

/* The EDID of the LG C5 as the TCL Q85H Pro repeats it (read by a Homatics R 4K Plus on 2026-11-24, serial 01010101) */
static const char *C5 =
    "00ffffffffffff001e6dcd83010101010123010380a05a780aee91a3544c99260f5054a1080031404540614071408180d1c0010101"
    "0108e80030f2705a80b0588a0040846300001e6fc200a0a0a055503020350040846300001e000000fd0018781eff77000a20202020"
    "2020000000fc004c472054562053534352320a200199020376f15d616076756665dbda101f04130514030212202122155d5e5f6263"
    "643f4038097f070f7f076754075704031507503d07c05f7e035f7e01836f0f0c6e030c002100b8442c0080010203046dd85dc4017880"
    "5b022878c33305e200cfe305c000e3060d01e20fffeb0146d0002a2c03b9865d6c0000000000000000005a";

/* the payloads of the vendor InfoFrames of Hades' transport: Dolby Standard, Dolby Low-Latency, HDMI 1.4b DV */
static const NvU8 DOLBY_STD[27] = {0x46, 0xD0, 0x00, 0x02};
static const NvU8 DOLBY_LL[27] = {0x46, 0xD0, 0x00, 0x03};
static const NvU8 HDMI14B_DV[24] = {0x03, 0x0C, 0x00, 0x20, 0x03};

/* (nvHadesIsDolbyVisionVsif() of 620 takes the payload, nvHadesVsifIsDolbyVision() of 580 the metadata) */
#if defined(NV_HADES_DV_HDMI14B_PAYLOAD_SIZE)
static NvBool Dolby(const NvU8 *p, NvU32 n) { return nvHadesIsDolbyVisionVsif(p, n); }
#else
static NvBool Dolby(const NvU8 *p, NvU32 n)
{
    struct NvKmsHdmiVsifMetadata m;
    memset(&m, 0, sizeof(m));
    memcpy(m.payload, p, n);
    m.payloadSize = n;
    return nvHadesVsifIsDolbyVision(&m);
}
#endif

static NVT_EDID_INFO edid;

/* The AVI InfoFrame of a CTA mode as NVKMS makes it (RGB 8 bpc), with the content type of the patch where newRule
 * (nvHadesAviContent(), as SendVideoInfoFrame() calls it), else as before it (NVKMS's own) */
static NVT_VIDEO_INFOFRAME Avi(NvU32 vic, NvU8 type, NvBool dolby, NvBool newRule)
{
    NVT_TIMING timing;
    NVT_VIDEO_INFOFRAME_CTRL ctrl;
    NVT_VIDEO_INFOFRAME frame;
    NvU8 itc, cn;

    memset(&timing, 0, sizeof(timing));
    memset(&ctrl, NVT_INFOFRAME_CTRL_DONTCARE, sizeof(ctrl));
    memset(&frame, 0, sizeof(frame));
    CHECK(NvTiming_EnumCEA861bTiming(vic, &timing) == NVT_STATUS_SUCCESS, "VIC %u enumerated", vic);
    CHECK(NvTiming_ConstructVideoInfoframeCtrl(&timing, &ctrl) == NVT_STATUS_SUCCESS, "controls of VIC %u", vic);
    ctrl.color_space = NVT_VIDEO_INFOFRAME_BYTE1_Y1Y0_RGB;
    if (newRule && nvHadesAviContent(type, dolby, &itc, &cn)) {
        ctrl.it_content = itc;
        ctrl.it_content_type = cn;
    }
    CHECK(NvTiming_ConstructVideoInfoframe(&edid, &ctrl, NULL, &frame) == NVT_STATUS_SUCCESS, "AVI of VIC %u", vic);
    return frame;
}

static int Itc(const NVT_VIDEO_INFOFRAME *f) { return (f->byte3 >> 7) & 1; }
static int Cn(const NVT_VIDEO_INFOFRAME *f) { return (f->byte5 >> 4) & 3; }

/* every byte but byte 3 bit 7 and byte 5 bits 5:4 the same */
static int SameButContent(const NVT_VIDEO_INFOFRAME *a, const NVT_VIDEO_INFOFRAME *b)
{
    NVT_VIDEO_INFOFRAME x = *a, y = *b;
    x.byte3 &= 0x7F;
    y.byte3 &= 0x7F;
    x.byte5 &= 0xCF;
    y.byte5 &= 0xCF;
    return memcmp(&x, &y, sizeof(x)) == 0;
}

int main(void)
{
    NvU8 raw[256];
    size_t i;
    static const struct {
        const char *name;
        NvU8 type;
        int vsif; /* 0 none, 1 Dolby Standard, 2 Dolby Low-Latency, 3 HDMI 1.4b Dolby Vision */
        int itc, cn;
    } cases[] = {
        /* no content type asked ("No Data", Kodi's "As LibreELEC" and its menus): NVKMS's own */
        {"No Data, SDR/HDR", NV_HADES_CONTENT_TYPE_NONE, 0, 1, 0},
        /* ... but no data with Dolby Vision, as the tunnels of i915 and amdgpu and a licensed box say it */
        {"No Data, Dolby Vision Standard (TV-led)", NV_HADES_CONTENT_TYPE_NONE, 1, 0, 0},
        {"No Data, Dolby Vision Low-Latency", NV_HADES_CONTENT_TYPE_NONE, 2, 0, 0},
        {"No Data, Dolby Vision over HDMI 1.4b", NV_HADES_CONTENT_TYPE_NONE, 3, 0, 0},
        {"Graphics", NV_HADES_CONTENT_TYPE_GRAPHICS, 0, 1, 0},
        {"Photo", NV_HADES_CONTENT_TYPE_PHOTO, 0, 1, 1},
        {"Cinema (a film: Filmmaker Mode Auto Start)", NV_HADES_CONTENT_TYPE_CINEMA, 0, 1, 2},
        {"Game (RetroPlayer)", NV_HADES_CONTENT_TYPE_GAME, 0, 1, 3},
        /* what is asked wins over Dolby Vision: Kodi asks Cinema for Low-Latency, a video the source maps */
        {"Cinema, Dolby Vision Low-Latency", NV_HADES_CONTENT_TYPE_CINEMA, 2, 1, 2},
        {"Graphics, Dolby Vision Standard", NV_HADES_CONTENT_TYPE_GRAPHICS, 1, 1, 0},
        /* a value out of range is none (nvHadesContentTypeOf() gives 0 for it) */
        {"out of range (7) read from the overrides", 7, 0, 1, 0},
    };
    const NvU32 vics[] = {93, 97};
    const NvU32 MASK = 7u << 27, SHIFT = 27;
    int differs = 0;

    for (i = 0; i < 256; i++) {
        unsigned int b;
        sscanf(C5 + 2 * i, "%2x", &b);
        raw[i] = (NvU8)b;
    }
    memset(&edid, 0, sizeof(edid));
    CHECK(NvTiming_ParseEDIDInfo(raw, sizeof(raw), &edid) == NVT_STATUS_SUCCESS, "the EDID of the C5 parsed");

    /* the field of the overrides, both ways */
    for (i = 0; i <= 4; i++) {
        const NvU32 o = nvHadesContentTypeOverride((NvU32)i, MASK, SHIFT);
        CHECK(nvHadesContentTypeOf(o | (1u << 30) | 1u, MASK, SHIFT) == i,
              "content type %zu through the overrides, next to FRL keep and another bit", i);
        CHECK((o & ~MASK) == 0, "content type %zu stays in bits 27-29", i);
    }
    CHECK(nvHadesContentTypeOverride(5, MASK, SHIFT) == 0, "a value out of range asks for nothing");
    CHECK(nvHadesContentTypeOf(7u << 27, MASK, SHIFT) == NV_HADES_CONTENT_TYPE_NONE, "7 in the field is none");
    CHECK(nvHadesContentTypeOf(0xFFFFFFFFu & ~MASK, MASK, SHIFT) == NV_HADES_CONTENT_TYPE_NONE,
          "the other bits of the overrides are no content type");

    for (size_t v = 0; v < sizeof(vics) / sizeof(vics[0]); v++) {
        const NVT_VIDEO_INFOFRAME own = Avi(vics[v], NV_HADES_CONTENT_TYPE_NONE, NV_FALSE, NV_FALSE);

        CHECK(Itc(&own) == 1 && Cn(&own) == 0, "NVKMS's own AVI of VIC %u says IT content, graphics", vics[v]);
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const NvU8 *p = cases[i].vsif == 1 ? DOLBY_STD : cases[i].vsif == 2 ? DOLBY_LL : HDMI14B_DV;
            const NvU32 n = cases[i].vsif == 3 ? sizeof(HDMI14B_DV) : sizeof(DOLBY_STD);
            const NvBool dolby = cases[i].vsif ? Dolby(p, n) : NV_FALSE;
            const NvU8 type = nvHadesContentTypeOf(nvHadesContentTypeOverride(cases[i].type, MASK, SHIFT) |
                                                       (cases[i].type > 4 ? (NvU32)cases[i].type << SHIFT : 0),
                                                   MASK, SHIFT);
            const NVT_VIDEO_INFOFRAME got = Avi(vics[v], type, dolby, NV_TRUE);
            const NVT_VIDEO_INFOFRAME before = Avi(vics[v], type, dolby, NV_FALSE);

            CHECK(!cases[i].vsif || dolby, "%s: the VSIF is one of Dolby Vision", cases[i].name);
            printf("VIC %u, %s: AVI PB1-PB5 %02x %02x %02x %02x %02x, ITC %d, CN %d\n", vics[v], cases[i].name,
                   got.byte1, got.byte2, got.byte3, got.byte4, got.byte5, Itc(&got), Cn(&got));
            CHECK(Itc(&got) == cases[i].itc && Cn(&got) == cases[i].cn, "VIC %u, %s: ITC %d CN %d (wanted %d %d)",
                  vics[v], cases[i].name, Itc(&got), Cn(&got), cases[i].itc, cases[i].cn);
            CHECK(SameButContent(&got, &own), "VIC %u, %s: every other byte as NVKMS makes it", vics[v],
                  cases[i].name);
            /* the rule before: NVKMS's own whatever the client asked */
            CHECK(Itc(&before) == 1 && Cn(&before) == 0 && memcmp(&before, &own, sizeof(own)) == 0,
                  "VIC %u, %s: before the patch, NVKMS's own AVI", vics[v], cases[i].name);
            differs += memcmp(&before, &got, sizeof(got)) != 0;
        }
    }
    /* the rule before passes only the cases where the AVI is NVKMS's own: it fails the other 7 of 11, twice */
    CHECK(differs == 14, "the rule before differs in %d cases (14 wanted)", differs);
    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
