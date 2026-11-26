# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutant (stage reuse): HDR over the tunnel takes the picture as it is in rgb although the GUI was composited into it
PAIRS = [('    if (repeat && !r->rgb_drawn) {\n        reused = true;\n', '    if (repeat) {\n        reused = true;\n')]
