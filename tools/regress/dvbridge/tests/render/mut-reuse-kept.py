# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutant (stage reuse): the copy of a converted picture is not made, and is taken all the same
PAIRS = [('                dvbridge_gl_drain_errors();\n                pl_tex_blit(r->gpu, pl_tex_blit_params(.src = r->rgb, .dst = r->rgb_kept));\n',
          '                dvbridge_gl_drain_errors();\n')]
