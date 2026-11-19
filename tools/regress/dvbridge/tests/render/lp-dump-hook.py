# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# libplacebo for ccm_gpu_test.c (rg_libplacebo edit file), TEST ONLY: the composed planes of the composer of CCM 001
# read back after each render where DV_CCM_DUMP=<prefix> is set (<prefix>-<n>_y.f32, one float a texel, and
# <prefix>-<n>_c.f32, two), from intermediate textures made readable by the host where they are of 32-bit floats.
FILE = 'src/renderer.c'
PAIRS = [
    ('    image->planes[0] = luma;\n    image->planes[1] = chroma;\n',
     '''    {
        // HADES TEST ONLY: the composed planes read back
        const char *dump = getenv("DV_CCM_DUMP");
        static int count;
        if (dump) {
            pl_tex tt[2] = { ly, lc };
            const char *nm[2] = { "y", "c" };
            for (int k = 0; k < 2; k++) {
                size_t sz = (size_t) tt[k]->params.w * tt[k]->params.h * tt[k]->params.format->texel_size;
                void *buf = malloc(sz);
                if (buf && pl_tex_download(rr->gpu, pl_tex_transfer_params(.tex = tt[k], .ptr = buf))) {
                    char name[600];
                    snprintf(name, sizeof(name), "%s-%d_%s.f32", dump, count, nm[k]);
                    FILE *fo = fopen(name, "wb");
                    if (fo) { fwrite(buf, 1, sz, fo); fclose(fo); }
                }
                free(buf);
            }
            count++;
        }
    }
    image->planes[0] = luma;
    image->planes[1] = chroma;
'''),
    ('#include <math.h>\n', '#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n'),
    ('        .renderable = true,\n        .blit_src   = fmt->caps & PL_FMT_CAP_BLITTABLE,\n',
     '        .renderable = true,\n'
     '        .host_readable = fmt->type == PL_FMT_FLOAT && fmt->component_depth[0] == 32,\n'
     '        .blit_src   = fmt->caps & PL_FMT_CAP_BLITTABLE,\n'),
]
