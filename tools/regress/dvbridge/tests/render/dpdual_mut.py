# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: mutations of NvidiaDualMode.h and ModeChangeHold.h that dpdual_test.cpp
# must catch. Usage: python3 -I dpdual_mut.py <tree> <test.cpp> <work dir>
import pathlib, shutil, subprocess, sys

tree, test, work = map(pathlib.Path, sys.argv[1:4])
G = tree / "xbmc/windowing/gbm"
files = {"NvidiaDualMode.h": (G / "drm/NvidiaDualMode.h").read_text(),
         "ModeChangeHold.h": (G / "ModeChangeHold.h").read_text(),
         "DRMCommittedMode.h": (G / "drm/DRMCommittedMode.h").read_text()}

M = [
    ("NvidiaDualMode.h", "entry: Kodi's commits only", "  return modeOnCrtc.value_or(modeCommitted);", "  return modeCommitted;"),
    ("NvidiaDualMode.h", "entry: the kernel only, unreadable = no", "  return modeOnCrtc.value_or(modeCommitted);", "  return modeOnCrtc.value_or(false);"),
    ("NvidiaDualMode.h", "entry: both must say so", "  return modeOnCrtc.value_or(modeCommitted);", "  return modeOnCrtc.value_or(true) && modeCommitted;"),
    ("NvidiaDualMode.h", "restart: without the setting", "  return setting && active && otherTiming && LinkFromScratch(type, dualModeTmds);",
     "  return active && otherTiming && LinkFromScratch(type, dualModeTmds);"),
    ("NvidiaDualMode.h", "restart: also when off", "  return setting && active && otherTiming && LinkFromScratch(type, dualModeTmds);",
     "  return setting && otherTiming && LinkFromScratch(type, dualModeTmds);"),
    ("NvidiaDualMode.h", "restart: also the same mode", "  return setting && active && otherTiming && LinkFromScratch(type, dualModeTmds);",
     "  return setting && active && LinkFromScratch(type, dualModeTmds);"),
    ("NvidiaDualMode.h", "restart: every connector driven as HDMI", "  return setting && active && otherTiming && LinkFromScratch(type, dualModeTmds);",
     "  return setting && active && otherTiming && DrivenAsHdmi(type, dualModeTmds);"),
    ("NvidiaDualMode.h", "restart: never", "  return setting && active && otherTiming && LinkFromScratch(type, dualModeTmds);",
     "  return false;"),
    ("ModeChangeHold.h", "hold: HDR held from scratch too", "    return ((video == Video::HDR && !c.modeFromScratch) ||",
     "    return ((video == Video::HDR) ||"),
    ("ModeChangeHold.h", "hold: native DV held from scratch", "            (video == Video::NATIVE_DV && !c.linkFromScratch)) &&",
     "            (video == Video::NATIVE_DV)) &&"),
    ("DRMCommittedMode.h", "committed: a modeset of a fallback counts", "    if (!carriedSelected || !selected)\n      return;",
     "    if (!selected)\n      return;"),
]

caught = 0
for f, name, old, new in M:
    src = files[f]
    if src.count(old) != 1:
        print(f"MUTATION NOT APPLIED ({src.count(old)}): {name}")
        continue
    d = work / "mut"
    shutil.rmtree(d, ignore_errors=True)
    (d / "drm").mkdir(parents=True)
    for k, v in files.items():
        (d / ("drm" if k != "ModeChangeHold.h" else "") / k).write_text(v.replace(old, new) if k == f else v)
    b = subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-I" + str(d), "-I" + str(d / "drm"), str(test),
                        "-o", str(d / "t")], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"caught (does not compile): {name}: {b.stderr.splitlines()[0] if b.stderr else ''}")
        caught += 1
        continue
    r = subprocess.run([str(d / "t")], capture_output=True, text=True)
    fails = [l for l in r.stdout.splitlines() if l.startswith("FAIL")]
    if r.returncode != 0:
        caught += 1
        print(f"caught: {name} ({len(fails)} checks fail, e.g. {fails[0][5:90] if fails else '?'})")
    else:
        print(f"NOT CAUGHT: {name}")
shutil.rmtree(work / "mut", ignore_errors=True)
print(f"MUTATIONS: {caught} of {len(M)} caught")
