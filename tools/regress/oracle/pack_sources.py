#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Write pack_sources.h (PACK_VERTEX, PACK_FRAGMENT as C string literals) from the bridge's
pack_gles300.vert/.frag, as Kodi's build embeds them.

usage: pack_sources.py <tools/dvbridge dir> <output dir>
"""
import pathlib
import sys


def lit(text):
    out = []
    for line in text.splitlines():
        out.append('"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n')
    return ''.join(out)


def main():
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    text = ('static const char PACK_VERTEX[] =\n' + lit((src / 'pack_gles300.vert').read_text()) + ';\n'
            'static const char PACK_FRAGMENT[] =\n' + lit((src / 'pack_gles300.frag').read_text()) + ';\n')
    (dst / 'pack_sources.h').write_text(text)


if __name__ == '__main__':
    main()
