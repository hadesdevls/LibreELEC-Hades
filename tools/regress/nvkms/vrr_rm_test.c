/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of nvHadesVrrHeadForRm() (nvidia-open-kernel-modules-9075, nvkms-hades.h): whether
 * NVKMS tells RM that a head can extend its vblank. With nvidia-modeset.hades_vrr_rm_by_timings on (the default),
 * the rule of NVKMS's own comment in nvEnableVrr(), as driver 615 had it: a G-SYNC display always, an Adaptive-Sync
 * one only where the timings of the mode set have VRR. Off: driver 620's answer, the display's VRR capability.
 *
 * Build against a patched tree T of the open modules:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I T/src/nvidia-modeset/include \
 *       -I T/src/common/sdk/nvidia/inc -I T/src/common/inc -I T/kernel-open/common/inc -o t vrr_rm_test.c && ./t
 */
#include <stddef.h> /* (nvkms-hades.h uses NULL and takes its types from nvtypes.h) */

#include "nvkms-hades.h"

#include <stdio.h>

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

int main(void)
{
    int m;
    char what[160];

    /* the 16 combinations against the rule written out */
    for (m = 0; m < 16; m++) {
        const NvBool byTimings = (m & 1) != 0;
        const NvBool gsync = (m & 2) != 0;
        const NvBool timings = (m & 4) != 0;
        const NvBool display = (m & 8) != 0;
        NvBool want;

        if (byTimings) {
            /* NVKMS's comment: "all G-SYNC displays ... but only Adaptive-Sync displays which put the display
             * into Adaptive-Sync mode" */
            want = gsync || (display && timings);
        } else {
            /* driver 620: the display's capability */
            want = display;
        }
        snprintf(what, sizeof(what), "byTimings %d, G-SYNC %d, VRR timings %d, VRR display %d", byTimings,
                 gsync, timings, display);
        check(nvHadesVrrHeadForRm(byTimings, gsync, timings, display) == want, what);
    }

    /* the cases of the LG C5 on an RTX 4080 SUPER (an Adaptive-Sync display through HDMI, VRR 40-120 Hz) */
    check(!nvHadesVrrHeadForRm(NV_TRUE, NV_FALSE, NV_FALSE, NV_TRUE),
          "23.976 Hz (no VRR in the timings): RM is not told, as with 615");
    check(nvHadesVrrHeadForRm(NV_FALSE, NV_FALSE, NV_FALSE, NV_TRUE),
          "23.976 Hz with the parameter off: RM is told, as with 620");
    check(nvHadesVrrHeadForRm(NV_TRUE, NV_FALSE, NV_TRUE, NV_TRUE),
          "120 Hz with VRR in the timings: RM is told, both ways");
    check(nvHadesVrrHeadForRm(NV_FALSE, NV_FALSE, NV_TRUE, NV_TRUE),
          "120 Hz with VRR in the timings, parameter off: RM is told");
    /* a display without VRR is never told; a G-SYNC one always is */
    check(!nvHadesVrrHeadForRm(NV_TRUE, NV_FALSE, NV_FALSE, NV_FALSE), "no VRR display: not told");
    check(!nvHadesVrrHeadForRm(NV_FALSE, NV_FALSE, NV_FALSE, NV_FALSE), "no VRR display, 620: not told");
    check(nvHadesVrrHeadForRm(NV_TRUE, NV_TRUE, NV_FALSE, NV_TRUE), "G-SYNC display without VRR timings: told");

    printf("%d checks, %d failures\n%s\n", checks, failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
