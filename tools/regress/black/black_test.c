/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: the black and the first codes of Dolby Vision sent as HDR10 and mapped to the display, against
 * ITU-R BT.2390. The bridge maps a reconstructed picture to a display of a given peak with libplacebo, and gives
 * both sides PQ black (PL_COLOR_HDR_BLACK, "infinite contrast": map_to_display() in tools/dvbridge/dvbridge_render.c),
 * so that the black of the picture goes out as mastered. With a black level on either side, the black point
 * adaptation of BT.2390 (5.4.1, the term b(1 - E2)^4) lifts every dark code, and the darkest the most.
 *
 * Measured here with libplacebo's own curves (pl_tone_map_sample(), the function the GPU's table is made from), as
 * the bridge sets them up: PQ in and out, the source from its black to the peak of its mastering display, the
 * target from its black to the display's peak, BT.2390 and the spline. For every 12-bit PQ code from 0 to 1 cd/m2
 * (code 0 to 615), the code that comes out, against the code that goes in: BT.2390 leaves everything below its knee
 * as it is when b = 0, so those codes must come out as they went in (within half a code). The spline is not BT.2390:
 * it scales the dark part of the picture with the rest (its lower segment goes through black with a slope of its
 * own), so of it only its black is checked (code 0 out as 0) and that it never goes down; how far it moves the code of
 * 1 cd/m2 is said.
 *
 * Usage: black_test [<black level of the target in cd/m2, default: PL_COLOR_HDR_BLACK as the bridge sets it>]
 * A black level above zero is the other direction of the test: it must fail.
 * Exit 0 when every mapping passes, 1 otherwise.
 */
#include <libplacebo/colorspace.h>
#include <libplacebo/tone_mapping.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float pq(float nits)
{
    return pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits);
}

int main(int argc, char **argv)
{
    const float black = argc > 1 ? (float)atof(argv[1]) : PL_COLOR_HDR_BLACK;
    const struct {
        const char *name;
        const struct pl_tone_map_function *function;
    } curves[] = {{"bt2390", &pl_tone_map_bt2390}, {"spline", &pl_tone_map_spline}};
    /* mastering peaks of the streams at hand (1000, 4000 cd/m2) to displays of 100 to 1500 cd/m2 */
    const float sources[] = {1000, 4000};
    const float targets[] = {100, 150, 400, 600, 800, 1000, 1500};
    const float top = pq(1.0f); /* the codes from black to 1 cd/m2 */
    int worst_code = 0, failed = 0, cases = 0;
    double worst = 0;
    printf("black of the target: %g cd/m2 (PL_COLOR_HDR_BLACK = %g)\n", black, (double)PL_COLOR_HDR_BLACK);
    for (unsigned c = 0; c < sizeof(curves) / sizeof(*curves); ++c)
        for (unsigned s = 0; s < sizeof(sources) / sizeof(*sources); ++s)
            for (unsigned t = 0; t < sizeof(targets) / sizeof(*targets); ++t) {
                if (targets[t] >= sources[s])
                    continue; /* nothing to map: the picture goes out as it is */
                struct pl_tone_map_params p = {
                    .function = curves[c].function,
                    .input_scaling = PL_HDR_PQ,
                    .output_scaling = PL_HDR_PQ,
                    .lut_size = 1024,
                    .input_min = pq(PL_COLOR_HDR_BLACK),
                    .input_max = pq(sources[s]),
                    .output_min = pq(black),
                    .output_max = pq(targets[t]),
                };
                pl_tone_map_params_infer(&p);
                double dev = 0, last = -1;
                int at = 0, lifted0 = 0, falls = 0;
                for (int code = 0; code <= (int)ceilf(top * 4095); ++code) {
                    const float x = code / 4095.0f;
                    const float y = pl_tone_map_sample(x < p.input_min ? p.input_min : x, &p);
                    const double d = fabs(y * 4095.0 - (x < p.input_min ? p.input_min : x) * 4095.0);
                    if (code == 0)
                        lifted0 = (int)lround(y * 4095.0);
                    falls += y * 4095.0 < last - 1e-3;
                    last = y * 4095.0;
                    if (d > dev) {
                        dev = d;
                        at = code;
                    }
                }
                ++cases;
                const int bt2390 = curves[c].function == &pl_tone_map_bt2390;
                const int bad = bt2390 ? dev > 0.5 : (lifted0 != 0 || falls > 0);
                failed += bad;
                if (dev > worst) {
                    worst = dev;
                    worst_code = at;
                }
                printf("%s %4.0f -> %4.0f cd/m2: codes 0..%d (to 1 cd/m2): largest change %.4f of a code (at code %d, "
                       "%.5f cd/m2); code 0 goes out as %d%s\n",
                       curves[c].name, sources[s], targets[t], (int)ceilf(top * 4095), dev, at,
                       pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, at / 4095.0f), lifted0, bad ? "  FAIL" : "");
            }
    printf("%d mappings, %d failed (BT.2390: a dark code moved by more than half a code; spline: black lifted or a "
           "code that goes down); the largest change %.4f (code %d)\n",
           cases, failed, worst, worst_code);
    printf("BLACK-VERDICT: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
