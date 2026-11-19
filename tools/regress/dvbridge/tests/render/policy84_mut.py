# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: mutations of the rules for a base layer in HLG in DVPlaybackPolicy.h that
# policy84_test.cpp must catch. Usage: python3 -I policy84_mut.py <tree> <test.cpp> <work dir>
# Prints "caught"/"NOT CAUGHT" for each, "MUTATION NOT APPLIED" for one whose text is no longer there exactly once,
# and "MUTATIONS: n of m caught".
import pathlib
import shutil
import subprocess
import sys

tree, test, work = map(pathlib.Path, sys.argv[1:4])
V = tree / "xbmc/cores/VideoPlayer/DVDCodecs/Video"
src = (V / "DVPlaybackPolicy.h").read_text()

MUTATIONS = [
    ("profile 8.4 not HLG", "         : compatibility == 4 ? BASE_LAYER_HLG\n", "         : compatibility == 4 ? BASE_LAYER_SDR\n"),
    ("PQ where the RPU says another signal", "  return eotf == 0xFFFF && param0 == 0", "  return eotf != 0xFFFF && param0 == 0"),
    ("the bit depth not compared", " && signalBits == hdrBits;\n", ";\n"),
    ("every base layer composable", "  return transfer != BASE_LAYER_HLG || declaresPq;\n", "  return true;\n"),
    ("HLG never composable", "  return transfer != BASE_LAYER_HLG || declaresPq;\n", "  return transfer != BASE_LAYER_HLG;\n"),
]

caught = 0
for name, old, new in MUTATIONS:
    if src.count(old) != 1:
        print(f"MUTATION NOT APPLIED ({src.count(old)}): {name}")
        continue
    d = work / "mut"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    (d / "DVPlaybackPolicy.h").write_text(src.replace(old, new))
    b = subprocess.run(["g++", "-std=c++20", "-w", "-I" + str(d), "-I" + str(V), "-I" + str(tree / "xbmc"), str(test),
                        "-o", str(d / "t")],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f"caught (does not compile): {name}")
        caught += 1
        continue
    r = subprocess.run([str(d / "t")], capture_output=True, text=True)
    fails = [l for l in r.stdout.splitlines() if l.startswith("FAIL")]
    if r.returncode != 0:
        caught += 1
        print(f"caught: {name} ({len(fails)} checks fail, e.g. {fails[0][5:80] if fails else '?'})")
    else:
        print(f"NOT CAUGHT: {name}")
shutil.rmtree(work / "mut", ignore_errors=True)
print(f"MUTATIONS: {caught} of {len(MUTATIONS)} caught")
