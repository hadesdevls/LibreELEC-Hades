# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutant (stage reuse): the source transport takes the composer's luma again in place of what its render before packed
# (the codes of the IPT tunnel)
PAIRS = [('        luma = r->reuse_luma;\n', '        luma = composed_luma;\n')]
