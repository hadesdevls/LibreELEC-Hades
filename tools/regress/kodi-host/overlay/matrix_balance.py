#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host check: in every C++ source of a Kodi tree, no function pops a level of a GL matrix stack (glMatrixModview,
# glMatrixProject, glMatrixTexture: Pop() or PopLoad()) that it has not pushed itself, except the pairs of functions
# that are made to push in one and pop in the other (CaptureStateBlock/ApplyStateBlock of the render systems).
# CMatrixGLStack::Pop() on a level pushed by a caller restores the caller's matrix in the middle of its drawing
# and takes the level its own PopLoad() was going to restore.
# Usage: matrix_balance.py <kodi tree>   (exit 0: balanced; 1: a function pops more than it pushes)
import os
import re
import sys

root = sys.argv[1]
CALL = re.compile(r"\b(glMatrixModview|glMatrixProject|glMatrixTexture)\s*\.\s*(Push|Pop|PopLoad)\s*\(")
# a function definition at the top level: "<something> Name::Name(...)" or "<something> name(...)" then "{"
DEF = re.compile(r"([A-Za-z_][\w:<>~]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{")
# the functions that are made to push in one and pop in the other: the render systems' state block (around add-on
# drawing), and the Dolby Vision overlay pass of Hades (the projection, around bitmap subtitles in PQ:
# CRenderManager::Render() calls the two around them, and no text of the GUI is drawn there)
SPLIT = {"CaptureStateBlock": "ApplyStateBlock", "BeginDVBridgeHDROverlays": "EndDVBridgeHDROverlays"}
SKIP_DIRS = ("/windows/", "/rendering/dx/", "/test/")


def strip(text):
    # comments, strings and characters out (keeping the length of lines)
    out = re.sub(r"//[^\n]*", "", text)
    out = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), out, flags=re.S)
    out = re.sub(r'"(\\.|[^"\\\n])*"', '""', out)
    out = re.sub(r"'(\\.|[^'\\\n])*'", "''", out)
    return out


def functions(text):
    # (name, body) of each function whose body starts at brace depth 0 or inside a namespace/class body
    i = 0
    n = len(text)
    while i < n:
        m = DEF.search(text, i)
        if not m:
            return
        start = m.end() - 1
        depth = 0
        j = start
        while j < n:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        name = m.group(1)
        if name not in ("if", "for", "while", "switch", "catch", "namespace"):
            yield name.split("::")[-1], text[start:j + 1]
            i = j + 1
        else:
            i = m.end()


bad = 0
checked = 0
for dirpath, _, files in os.walk(os.path.join(root, "xbmc")):
    if any(s in dirpath + "/" for s in SKIP_DIRS):
        continue
    for f in files:
        if not f.endswith((".cpp", ".h")):
            continue
        path = os.path.join(dirpath, f)
        text = open(path, encoding="utf-8", errors="replace").read()
        if not CALL.search(text):
            continue
        text = strip(text)
        for name, body in functions(text):
            counts = {}
            for stack, op in CALL.findall(body):
                c = counts.setdefault(stack, [0, 0])
                c[0 if op == "Push" else 1] += 1
            for stack, (push, pop) in counts.items():
                checked += 1
                if pop > push and name not in SPLIT.values():
                    print(f"UNBALANCED: {os.path.relpath(path, root)}: {name}(): {stack}: {push} Push, {pop} Pop")
                    bad += 1
                elif push > pop and name not in SPLIT:
                    print(f"UNBALANCED: {os.path.relpath(path, root)}: {name}(): {stack}: {push} Push, {pop} Pop (left pushed)")
                    bad += 1
print(f"{checked} uses of a matrix stack in functions checked, {bad} unbalanced")
sys.exit(1 if bad else 0)
