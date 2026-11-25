# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# info_refresh_src.py <Kodi tree>: in the source, what the host test of PlayerInfoRefresh.h cannot run: both dialogs of
# the player information (Kodi's, whose skin file may be a skin's own, and Hades' own) keep the panel processed in every
# frame while a control of it animates or is about to (Animates() in ProcessDue()), with every type of animation of
# Kodi's but none; CGUIControl::ConditionsChanged() compares the visibility and the conditions of conditional
# animations with what UpdateVisibility() last saw, and changes nothing. Prints FAIL lines and INFO-REFRESH-SRC PASSED or
# FAILED (n) last.
import os
import re
import sys

K = sys.argv[1]
fails = []


def need(cond, text):
    if not cond:
        fails.append(text)
        print('FAIL: ' + text)


def read(rel):
    return open(os.path.join(K, rel), encoding='utf-8').read()


def body(text, signature):
    at = text.find(signature)
    if at < 0:
        return ''
    start = text.index('{', at)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return ''


kodi = read('xbmc/dialogs/GUIDialogPlayerProcessInfo.cpp')
hades = read('xbmc/dialogs/GUIDialogHadesPlayerInfo.cpp')
need('Animates(*this)' in body(kodi, 'bool CGUIDialogPlayerProcessInfo::ProcessDue('),
     'Kodi\'s player information is not processed while a control of it animates')
need('CGUIDialogPlayerProcessInfo::Animates(*this)' in body(hades, 'bool CGUIDialogHadesPlayerInfo::ProcessDue('),
     'the player information of Hades is not processed while a control of it animates')
need('CPlayerInfoRefresh::Animates<CGUIControlGroup, CGUIControl>(group, ANIMATIONS)' in
     body(kodi, 'bool CGUIDialogPlayerProcessInfo::Animates('), 'Animates() does not look at the controls of the group')
effect = read('xbmc/guilib/VisibleEffect.h')
enum = effect[effect.find('enum ANIMATION_TYPE'):]
enum = enum[:enum.find('}')]
types = set(re.findall(r'\b(ANIM_TYPE_\w+)', enum)) - {'ANIM_TYPE_NONE'}
m = re.search(r'ANIMATIONS = \{([^}]*)\}', kodi)
listed = set(re.findall(r'\b(ANIM_TYPE_\w+)', m.group(1))) if m else set()
need(types and listed == types, f'the animations that keep the panel processed are not every type but none (missing: '
     f'{sorted(types - listed)}, beyond: {sorted(listed - types)})')
cc = body(read('xbmc/guilib/GUIControl.cpp'), 'bool CGUIControl::ConditionsChanged(')
need(re.search(r'm_visibleCondition->Get\(INFO::DEFAULT_CONTEXT, nullptr\) != m_visibleFromSkinCondition', cc),
     'ConditionsChanged() does not compare the visibility with what UpdateVisibility() last saw')
need(re.search(r'ANIM_TYPE_CONDITIONAL && anim\.ConditionChanged\(\)', cc),
     'ConditionsChanged() does not look at the conditional animations')
need(not re.search(r'm_visibleFromSkinCondition\s*=[^=]', cc), 'ConditionsChanged() changes the visibility')
ac = body(read('xbmc/guilib/VisibleEffect.cpp'), 'bool CAnimation::ConditionChanged(')
need('m_condition->Get(INFO::DEFAULT_CONTEXT) != m_lastCondition' in ac and not re.search(r'm_lastCondition\s*=[^=]', ac),
     'CAnimation::ConditionChanged() does not compare with the condition last seen, or changes it')
print(f'INFO-REFRESH-SRC FAILED ({len(fails)})' if fails else 'INFO-REFRESH-SRC PASSED')
sys.exit(1 if fails else 0)
