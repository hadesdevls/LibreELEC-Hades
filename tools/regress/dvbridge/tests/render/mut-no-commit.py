# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for start_signal_test.c (rg_edit): dvbridge_render_commit() says the
# frame was committed without committing it to the serializer, so the next frame takes the same metadata id. The
# test's check of the serializer after the start signal must fail.
PAIRS = [
    ('    return r && r->ready && r->pending && dvbridge_commit(r->context, r->pending);',
     '    return r && r->ready && r->pending;'),
]
