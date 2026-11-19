# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: mutations of DVGuiStep.h that dvguistep_test must catch, each in a
# folder of its own with DVStepDown.h beside it.
# usage: python3 -I guistep_mut.py <tree's xbmc/windowing/gbm> <out dir>: prints the names of the mutants, one per
# line; status 3, and the names of those whose text is no longer there exactly once, when one does not apply
import os, shutil, sys
src, out = sys.argv[1], sys.argv[2]
h = open(os.path.join(src, 'DVGuiStep.h')).read()
MUTS = {
    # above the base on the way up
    'M1-up-above-base': ('return above >= f.base ? above : -1;', 'return above;'),
    # every level saves time (no gain check)
    'M2-saves-always': ('return now[a] > 0 && now[b] > 0 && now[a] < GAIN * now[b] &&\n           now[b] - now[a] > 2 * std::hypot(error(a), error(b));',
                        'return true;'),
    # down while frame ahead can still be had
    'M3-pending-down': ('if (w.pace == Pace::PENDING || f.exhausted)\n        return step;', 'if (f.exhausted)\n        return step;'),
    # one window behind is enough
    'M4-one-window': ('if (!beyond && ++f.behind < 2)', 'if (!beyond && ++f.behind < 1)'),
    # no settling at the start of a video
    'M5-no-settle': ('static constexpr double SETTLE_MS = 2000;', 'static constexpr double SETTLE_MS = 0;'),
    # no wait before going up
    'M6-no-wait': ('if (above < 0 || f.sinceDown < f.releaseWait)', 'if (above < 0)'),
    # the wait does not double
    'M7-no-double': ('f.releaseWait = std::min(2 * f.releaseWait, RELEASE_MAX_WAIT);', 'f.releaseWait = RELEASE_WAIT;'),
    # unknown: level 0 instead of the base
    'M8-unknown-zero': ('return f ? f->level : base;', 'return f ? f->level : 0;'),
    # a new format starts at level 0, not at its base
    'M9-start-zero': ('    place->level = base;\n', ''),
    # GPU times alone (no missed refreshes) make a window behind
    'M10-no-missed': ('behind = known && missed && average > step.budgetMs;', 'behind = known && average > step.budgetMs;'),
    # frames drawn at another level are taken
    'M11-other-level': ('    if (drawn != f.level)\n      return Step();\n    Window& w', '    Window& w'),
    # no GUI settling after a frame without GUI elements
    'M12-no-gui-settle': ('static constexpr double GUI_SETTLE_MS = 1000;', 'static constexpr double GUI_SETTLE_MS = 0;'),
    # a ladder without a level below the base is decided for
    'M13-applies-any': ('    return CDVStepDown::In(CDVStepDown::Ladder(ladder), base) &&\n           CDVStepDown::Below(CDVStepDown::Ladder(ladder), base) >= 0;',
                        '    return true;'),
    # the exhausted state is kept at another pace
    'M14-exhausted-forever': ('if (f.exhausted && w.pace != Pace::PENDING && f.exhaustedPace != w.pace)\n      f.exhausted = false;', ''),
    # the base is not part of the key
    'M15-no-base-key': ('if (id != 0 && f.id == id && f.base == base && f.ladder == ladder &&', 'if (id != 0 && f.id == id && f.ladder == ladder &&'),
}
stale = [name for name, (a, b) in MUTS.items() if h.count(a) != 1]
if stale:
    print('MUTANTS NO LONGER APPLY to DVGuiStep.h: ' + ' '.join('%s (%d times)' % (n, h.count(MUTS[n][0])) for n in stale))
    sys.exit(3)
for name, (a, b) in MUTS.items():
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'DVGuiStep.h'), 'w').write(h.replace(a, b))
    shutil.copy(os.path.join(src, 'DVStepDown.h'), d)
print('\n'.join(MUTS))
