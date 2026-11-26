# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutant (stage reuse): what the render before left is taken without comparing its key and its metadata
PAIRS = [('    return left == kind && key && r->reuse_meta && r->reuse_meta_size == bytes && metadata &&\n'
          '           !memcmp(key, &r->reuse_key, sizeof(*key)) && !memcmp(metadata, r->reuse_meta, bytes);\n',
          '    return left == kind && key && r->reuse_meta && metadata;\n')]
