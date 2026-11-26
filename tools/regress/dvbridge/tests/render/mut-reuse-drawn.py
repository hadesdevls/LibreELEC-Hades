# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutant (stage reuse): a converted picture is taken as it is in rgb although the GUI was composited into it since
PAIRS = [('    if (repeat && !r->rgb_drawn) {\n        reused = rendered = true;\n', '    if (repeat) {\n        reused = rendered = true;\n')]
