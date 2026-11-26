# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): the compute pass of pass 9 never used,
# though it is asked for and made. Its bytes are those of the fragment passes; ipt-forms must see that the compute
# pass made none of the pictures.
PAIRS = [
    ('    bool compute = p->compute_rows && p->ipt_compute && !p->ipt_compute_failed;',
     '    bool compute = false && p->compute_rows && p->ipt_compute && !p->ipt_compute_failed;'),
]
