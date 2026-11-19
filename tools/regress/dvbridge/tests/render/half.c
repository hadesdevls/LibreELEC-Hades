// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: gamut_half() of libplacebo-9950 (a patch of the libplacebo recipe; the copy below
// must be the function of the patched source, which run-dvbridge-render.sh checks) against the compiler's conversion to
// _Float16 (round to nearest, ties to even) for every float within [0, 1], and two mutations that must fail.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t gamut_half(float f)
{
    union { float f; uint32_t u; } v = { .f = f };
    if (v.u < 0x38800000) // below 2^-14, the smallest normal half: a subnormal one
        return (uint16_t) lrintf(f * 16777216.0f); // (2^24, its unit; 1024 is the smallest normal)
    const uint32_t r = v.u + 0x0fff + ((v.u >> 13) & 1);
    return (uint16_t) ((r - 0x38000000) >> 13);
}

// mutation 1: ties away from zero; mutation 2: truncation
static uint16_t m1(float f)
{
    union { float f; uint32_t u; } v = { .f = f };
    if (v.u < 0x38800000)
        return (uint16_t) lrintf(f * 16777216.0f);
    return (uint16_t) ((v.u + 0x1000 - 0x38000000) >> 13);
}
static uint16_t m2(float f)
{
    union { float f; uint32_t u; } v = { .f = f };
    if (v.u < 0x38800000)
        return (uint16_t) (f * 16777216.0f);
    return (uint16_t) ((v.u - 0x38000000) >> 13);
}

int main(void)
{
    unsigned long long n = 0, bad = 0, bad1 = 0, bad2 = 0;
    for (uint32_t u = 0; u <= 0x3f800000u; ++u) {
        float f;
        memcpy(&f, &u, 4);
        _Float16 h = (_Float16) f;
        uint16_t ref;
        memcpy(&ref, &h, 2);
        ++n;
        bad += gamut_half(f) != ref;
        bad1 += m1(f) != ref;
        bad2 += m2(f) != ref;
    }
    printf("half: %llu floats within [0, 1]: %llu differ from _Float16; mutation ties-away %llu, truncation %llu\n",
           n, bad, bad1, bad2);
    printf("%s\n", bad == 0 && bad1 > 0 && bad2 > 0 ? "HALF-OK" : "HALF-FAIL");
    return bad == 0 && bad1 > 0 && bad2 > 0 ? 0 : 1;
}
