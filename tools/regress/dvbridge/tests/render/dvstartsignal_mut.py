# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: mutations of DVStartSignal.h that dvstartsignal_test.cpp must catch.
# Usage: python3 -I dvstartsignal_mut.py <tree> <test.cpp> <work dir>
import pathlib, shutil, subprocess, sys

tree, test, work = map(pathlib.Path, sys.argv[1:4])
src = (tree / "xbmc/windowing/gbm/DVStartSignal.h").read_text()

MUTATIONS = [
    ("no switch", "    if (!c.switchedOn)\n      return Refusal::SWITCHED_OFF;\n", ""),
    ("no off check", "    if (Off())\n      return Refusal::NOT_TAKEN_BEFORE;\n", ""),
    ("no profile check", "    if (!ProfileTaken(c.profile, c.compatibility))\n      return Refusal::PROFILE;\n", ""),
    ("8.4 taken", "(compatibility == 1 || compatibility == 6)", "(compatibility == 1 || compatibility == 4 || compatibility == 6)"),
    ("AV1 taken", "return profile == 5 || profile == 7 ||", "return profile == 5 || profile == 7 || profile == 10 ||"),
    ("no disabled check", "    if (c.disabled)\n      return Refusal::DISABLED;\n", ""),
    ("no Standard check", "    if (!c.standard)\n      return Refusal::NOT_STANDARD;\n", ""),
    ("no hold check", "    if (!c.holds)\n      return Refusal::NO_HOLD;\n", ""),
    ("no mode check", "    if (!c.transportMode)\n      return Refusal::NOT_TRANSPORT_MODE;\n", ""),
    ("no video output check", "    if (c.videoOutput)\n      return Refusal::VIDEO_OUTPUT;\n", ""),
    ("no busy check", "    if (c.busy)\n      return Refusal::BUSY;\n", ""),
    ("no window check", "    if (!c.windowFits)\n      return Refusal::WINDOW;\n", ""),
    ("stop not seen", "    if (!playing)\n      return Wait::STOPPED;\n", ""),
    ("deadline one late", "nowMs - m_sentMs >= WAIT_MS", "nowMs - m_sentMs > WAIT_MS"),
    ("deadline before stop",
     "    if (!playing)\n      return Wait::STOPPED;\n"
     "    if (m_back ? nowMs - m_backMs >= WAIT_MS : nowMs - m_sentMs >= WAIT_MS + DISPLAY_WAIT_MS)\n      return Wait::DEADLINE;\n",
     "    if (m_back ? nowMs - m_backMs >= WAIT_MS : nowMs - m_sentMs >= WAIT_MS + DISPLAY_WAIT_MS)\n      return Wait::DEADLINE;\n"
     "    if (!playing)\n      return Wait::STOPPED;\n"),
    ("wait from the send, not from the display back", "m_back ? nowMs - m_backMs >= WAIT_MS", "m_back ? nowMs - m_sentMs >= WAIT_MS"),
    ("no time for the display to come back", "nowMs - m_sentMs >= WAIT_MS + DISPLAY_WAIT_MS", "nowMs - m_sentMs >= WAIT_MS"),
    ("taken keeps the row", "    m_pending = false;\n    m_notTaken = 0;\n", "    m_pending = false;\n"),
    ("not taken not counted", "    m_pending = false;\n    ++m_notTaken;\n", "    m_pending = false;\n"),
    ("ended counts", "  void Ended() { m_pending = false; }", "  void Ended() { m_pending = false; ++m_notTaken; }"),
    ("refusal not kept", "    m_pending = false;\n    m_refused = true;\n", "    m_pending = false;\n"),
    ("off one late", "m_notTaken >= NOT_TAKEN_LIMIT", "m_notTaken > NOT_TAKEN_LIMIT"),
    ("sent not pending", "    m_pending = true;\n    m_sentMs = nowMs;\n", "    m_sentMs = nowMs;\n"),
    ("wait 30 s", "WAIT_MS = 10000;", "WAIT_MS = 30000;"),
    ("same words", '        return "it did not work before in this run of Kodi";', '        return "nothing";'),
]

caught = 0
for name, old, new in MUTATIONS:
    if src.count(old) != 1:
        print(f"MUTATION NOT APPLIED ({src.count(old)}): {name}")
        continue
    d = work / "mut"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    (d / "DVStartSignal.h").write_text(src.replace(old, new))
    b = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I" + str(d), str(test), "-o", str(d / "t")],
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
