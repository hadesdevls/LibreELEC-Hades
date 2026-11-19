#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC5): the Python module xbmcaddon as Kodi's build makes it, from a Kodi tree: AddonModuleXbmcaddon.i
# through the swig of the build folder's toolchain (as Kodi's CMake runs it, with fix-swig-3535.cmake), then the
# generated source syntax-checked with the compile command that the build folder's compile_commands.json has for it.
# Hades' typemap of kodi_addonclass.i (a call that gave the script an error is told in kodi.log) is in the generated
# source once per wrapped call; the last line says how many lines carry it.
# usage: swig_check.py <kodi tree> <LibreELEC build folder> <output folder>
# Writes only into the output folder; reads the build folder.
import glob
import json
import os
import re
import shlex
import subprocess
import sys

tree, build, out = (os.path.abspath(a) for a in sys.argv[1:4])
os.makedirs(out, exist_ok=True)
swig = build + '/toolchain/bin/swig'
cmake = build + '/toolchain/bin/cmake'
dbs = sorted(glob.glob(build + '/build/kodi-*/.x86_64-libreelec-linux-gnu/compile_commands.json'))
if not dbs or not os.access(swig, os.X_OK):
    print('FAIL: no compile_commands.json of Kodi or no swig in %s' % build)
    sys.exit(1)
version = re.search(r'SWIG Version (\S+)', subprocess.run([swig, '-version'], capture_output=True, text=True).stdout)
gen = out + '/AddonModuleXbmcaddon.i.cpp'
r = subprocess.run([swig, '-python', '-c++', '-builtin', '-interface', 'xbmcaddon', '-pyi', '-w401,389,514,362,302',
                    '-o', gen, '-I' + tree + '/xbmc', tree + '/xbmc/interfaces/swig/AddonModuleXbmcaddon.i'],
                   cwd=out, capture_output=True, text=True)
print('swig %s: exit %d %s' % (version.group(1) if version else '?', r.returncode, r.stderr[-2000:]))
if r.returncode:
    print('FAIL: swig')
    sys.exit(1)
r = subprocess.run([cmake, '-DFILE=' + gen, '-DSWIG_VERSION=' + (version.group(1) if version else ''), '-P',
                    tree + '/xbmc/interfaces/swig/fix-swig-3535.cmake'], capture_output=True, text=True)
print('fix-swig-3535: exit %d %s' % (r.returncode, r.stderr[-500:]))
if r.returncode:
    print('FAIL: fix-swig-3535.cmake')
    sys.exit(1)
db = json.load(open(dbs[-1]))
entries = [x for x in db if x['file'].endswith('build/swig/AddonModuleXbmcaddon.i.cpp')]
if not entries:
    print('FAIL: no compile command of AddonModuleXbmcaddon.i.cpp in %s' % dbs[-1])
    sys.exit(1)
e = entries[0]
args = shlex.split(e['command'])
if '-o' in args:
    i = args.index('-o')
    del args[i:i + 2]
args = [a for a in args if a not in ('-c', e['file'])] + ['-fsyntax-only', gen]
# the headers of the tree under test where the build's source has them (its build folder, with the generated
# headers, stays the build's)
src = os.path.dirname(os.path.dirname(dbs[-1]))


def mapped(a):
    i = a.find(src + '/')
    if i < 0:
        return a
    p = tree + a[i + len(src):]
    return a[:i] + p if os.path.exists(p) else a


args = [mapped(a) for a in args]
r = subprocess.run(args, cwd=e['directory'], capture_output=True, text=True)
print('syntax: exit %d %s' % (r.returncode, r.stderr[-3000:]))
if r.returncode:
    print('FAIL: the generated source does not compile')
lines = sum(1 for line in open(gen, encoding='utf-8', errors='replace') if 'gave the script a' in line)
print('lines with Hades text: %d' % lines)
sys.exit(r.returncode)
