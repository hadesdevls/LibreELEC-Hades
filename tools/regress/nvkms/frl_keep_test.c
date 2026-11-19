/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of nvHadesFrlKeepDecide() and nvHadesFrlKeepWhyNot() (nvidia-open-kernel-modules-9078,
 * nvkms-hades.h): whether an HDMI head that runs over FRL stays on FRL for a mode that TMDS would carry, where its
 * client asks for it (NV_HDMI_FRL_KEEP of nvidia-drm), and what the kernel log says where it was asked and not kept.
 * The cases are those of the LG C5 on the RTX 4080 SUPER (Ada: YCbCr 4:2:2 over FRL), each condition alone, their
 * order, and all 1024 combinations of them.
 *
 * Build against a patched tree T of the open modules:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I T/src/nvidia-modeset/include \
 *       -I T/src/common/sdk/nvidia/inc -I T/src/common/inc -I T/kernel-open/common/inc -o t frl_keep_test.c && ./t
 */
#include <stddef.h> /* (nvkms-hades.h uses NULL and takes its types from nvtypes.h) */

#include <stdio.h>
#include <string.h>
#include "nvkms-hades.h"

static int fails, checks;
#define CHECK(cond, what)                                                                                        \
    do {                                                                                                         \
        checks++;                                                                                                \
        if (!(cond)) {                                                                                           \
            fails++;                                                                                             \
            printf("FAIL %s (line %d)\n", (what), __LINE__);                                                     \
        }                                                                                                        \
    } while (0)

/* asked, allowed, bootClocks, tmdsPossible, frlPossible, drivenOverFrl, hdmi3D, ycbcr420, ycbcr422, 422OverFrl */
#define D(a, b, c, d, e, f, g, h, i, j) nvHadesFrlKeepDecide(a, b, c, d, e, f, g, h, i, j)

int main(void)
{
    /* the C5: the menus at 3840x2160 120 Hz over FRL 4x8; a video at 23.976 Hz in RGB 8 bpc (DV), kept */
    CHECK(D(1, 1, 0, 1, 1, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP, "DV video from FRL menus: kept");
    /* an HDR10 video at 4:2:2 12 bpc on Ada: kept (Ada sends 4:2:2 over FRL) */
    CHECK(D(1, 1, 0, 1, 1, 1, 0, 0, 1, 1) == NV_HADES_FRL_KEEP, "4:2:2 where the GPU sends it over FRL: kept");
    /* the way back to the menus: TMDS does not carry 1188 MHz, FRL anyway */
    CHECK(D(1, 1, 0, 0, 1, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_NEEDS_FRL, "back to 4K120: FRL anyway");
    CHECK(D(1, 1, 0, 0, 1, 0, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_NEEDS_FRL, "4K120 from TMDS: FRL anyway");

    /* each condition alone turns it off, with its own reason */
    CHECK(D(0, 1, 0, 1, 1, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_NOT_ASKED, "not asked");
    CHECK(D(1, 0, 0, 1, 1, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_OFF, "hades_frl_keep=0");
    CHECK(D(1, 1, 1, 1, 1, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_BOOT_CLOCKS, "boot clocks");
    CHECK(D(1, 1, 0, 1, 0, 1, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_NO_FRL, "display without FRL");
    CHECK(D(1, 1, 0, 1, 1, 0, 0, 0, 0, 1) == NV_HADES_FRL_KEEP_NOT_ON_FRL, "head over TMDS now: nothing to keep");
    CHECK(D(1, 1, 0, 1, 1, 1, 1, 0, 0, 1) == NV_HADES_FRL_KEEP_HDMI_3D, "HDMI 3D frame packing");
    CHECK(D(1, 1, 0, 1, 1, 1, 0, 1, 0, 1) == NV_HADES_FRL_KEEP_FORMAT, "YCbCr 4:2:0");
    CHECK(D(1, 1, 0, 1, 1, 1, 0, 0, 1, 0) == NV_HADES_FRL_KEEP_FORMAT, "4:2:2 on a GPU without it over FRL");

    /* the order: the first that fails says why */
    CHECK(D(0, 0, 1, 0, 0, 0, 1, 1, 1, 0) == NV_HADES_FRL_KEEP_NOT_ASKED, "not asked comes first");
    CHECK(D(1, 0, 1, 0, 0, 0, 1, 1, 1, 0) == NV_HADES_FRL_KEEP_OFF, "the parameter before the rest");
    CHECK(D(1, 1, 0, 0, 0, 0, 1, 1, 1, 0) == NV_HADES_FRL_KEEP_NEEDS_FRL, "a mode that needs FRL is never refused here");
    CHECK(D(1, 1, 0, 1, 1, 0, 1, 1, 0, 1) == NV_HADES_FRL_KEEP_NOT_ON_FRL, "not on FRL before 3D and format");

    /* no other combination keeps it: exhaustively, kept only with everything right */
    {
        int kept = 0, n;
        for (n = 0; n < 1024; n++) {
            const NvBool b[10] = { n & 1, (n >> 1) & 1, (n >> 2) & 1, (n >> 3) & 1, (n >> 4) & 1,
                                   (n >> 5) & 1, (n >> 6) & 1, (n >> 7) & 1, (n >> 8) & 1, (n >> 9) & 1 };
            const enum NvHadesFrlKeep k = D(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
            const NvBool want = b[0] && b[1] && !b[2] && b[3] && b[4] && b[5] && !b[6] && !b[7] && (!b[8] || b[9]);
            if (k == NV_HADES_FRL_KEEP) {
                kept++;
            }
            if ((k == NV_HADES_FRL_KEEP) != want) {
                CHECK(0, "exhaustive: kept exactly where every condition holds");
                break;
            }
        }
        CHECK(kept == 3, "exhaustive: 3 of 1024 combinations kept (not 4:2:2, or 4:2:2 where the GPU sends it over FRL)");
    }

    /* what the kernel log says where it was asked and not kept; nothing where nothing is to say */
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP) == NULL, "kept: no reason");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_NOT_ASKED) == NULL, "not asked: no reason");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_NEEDS_FRL) == NULL, "FRL anyway: no reason");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_NOT_ON_FRL) != NULL &&
          strstr(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_NOT_ON_FRL), "not driven over FRL") != NULL,
          "not on FRL: said");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_OFF) != NULL &&
          strstr(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_OFF), "hades_frl_keep=0") != NULL,
          "the parameter: said");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_FORMAT) != NULL, "format: said");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_HDMI_3D) != NULL, "3D: said");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_NO_FRL) != NULL, "no FRL: said");
    CHECK(nvHadesFrlKeepWhyNot(NV_HADES_FRL_KEEP_BOOT_CLOCKS) != NULL, "boot clocks: said");
    /* 0 (the timings of a head that GetDfpHdmiProtocol() did not decide, zeroed) is "not asked" */
    CHECK(NV_HADES_FRL_KEEP_NOT_ASKED == 0, "zeroed timings read as not asked");

    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
