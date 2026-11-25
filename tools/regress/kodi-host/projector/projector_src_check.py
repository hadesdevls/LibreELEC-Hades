#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""projector_src_check.py: what no host test of the projector control runs, read in its source
(xbmc/network/ProjectorControl.cpp): every notice that the control gives (Notify()) is made of strings that are
translated, a string of the control (Text(), the core strings 62700-62769 with their Estuary twins) or a word of
the player information (Word(), an Estuary string, whose English is only its fallback), and of names (the brand, a
mode, an address): no English text of its own, which a Spanish interface would show as it is (1.2: "picture mode"
and "lens memory" in the notice of a command the projector refused).

usage: projector_src_check.py <Kodi tree>
Prints "ok: ..." or "FAIL: ..." for each check and "PROJECTOR-SRC PASSED" or "PROJECTOR-SRC FAILED (n)".
"""
import pathlib
import re
import sys

src = (pathlib.Path(sys.argv[1]) / 'xbmc/network/ProjectorControl.cpp').read_text(encoding='utf-8')
fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok: ' if ok else 'FAIL: ') + what)


def argument(text, at):
    """the text between the parenthesis at `at` and the one that closes it (strings taken whole)"""
    depth, i, out = 0, at, []
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return ''.join(out)[1:]
        out.append(c)
        i += 1
    return None


calls = [m.start() for m in re.finditer(r'(?<![:\w])Notify\(', src)]
check(len(calls) >= 5, f'the notices of the control are found in its source ({len(calls)} calls of Notify())')
for at in calls:
    line = src.count('\n', 0, at) + 1
    arg = argument(src, at + len('Notify'))
    if arg is None:
        check(False, f'line {line}: the call of Notify() is read to its end')
        continue
    # (the English of a word of the player information is its fallback only)
    rest = re.sub(r'Word\(\d+,\s*"(?:[^"\\]|\\.)*"\)', 'Word()', arg)
    texts = re.findall(r'"(?:[^"\\]|\\.)*"', rest)
    check(not texts, f'line {line}: the notice has no English text of its own'
                     f'{": " + ", ".join(texts) if texts else ""}')
    ids = [int(x) for x in re.findall(r'Text\((\d+)\)', arg)]
    check(all(62700 <= i <= 62769 for i in ids), f'line {line}: its strings are strings of the control ({ids})')


# Hades: the thread of the control sleeps while it is off, and the test of the connection can be cancelled and is
# waited for a bounded time
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


process = body(src, 'void CProjectorControl::Process(')
check(re.search(r'ControlWaitMs\(!m_configRead \|\| m_config\.brand != Brand::NONE\); ms < 0\)\s*m_wake\.Wait\(\);', process),
      'while the control is off its thread waits until it is woken (ControlWaitMs(), m_wake.Wait())')
changed = body(src, 'void CProjectorControl::OnSettingChanged(')
check(changed.rstrip('}').rstrip().endswith('m_wake.Set();') and 'SETTING_VIDEOSCREEN_HADESPROJECTOR,' in src,
      'turning the control on wakes the thread (OnSettingChanged() of the protocol sets m_wake)')
action = body(src, 'void CProjectorControl::OnSettingAction(')
check('WaitOnEvent' not in action, 'the test is not waited for under the busy dialog, which cannot be cancelled')
check('GetWindow<CGUIDialogProgress>' in action and 'progress->SetCanCancel(true);' in action and
      'progress->IsCanceled()' in action, 'the test is waited for with a progress that can be cancelled')
check(re.search(r'const int limitMs = TestWaitMs\(Timeouts\(\)\);', action) and
      re.search(r'if \(elapsedMs >= limitMs\)\s*break;', action), 'the test is waited for TestWaitMs() at most')
check('m_testAsked.compare_exchange_strong(asked, 0)' in action and re.search(r'm_testStop = true;', action),
      'a test given up is taken back, or stopped')
check('if (m_testDoneNumber == number)' in action, 'the answer of a test given up is never shown for the next one')
check('MakeSession(config, &m_testStop)' in body(src, 'std::string CProjectorControl::RunTest('),
      'the talk of the test stops when it is given up')
check(re.search(r'm_bStop = true;\s*m_testStop = true;', body(src, 'CProjectorControl::~CProjectorControl(')),
      'a test that runs while Kodi stops is stopped')
# every string of the control, in the core and in Estuary, translated
tree = pathlib.Path(sys.argv[1])
core = (tree / 'addons/resource.language.en_gb/resources/strings.po').read_text(encoding='utf-8')
langs = {l: (tree / f'addons/skin.estuary/language/resource.language.{l}/strings.po').read_text(encoding='utf-8')
         for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')}


def entry(t, i):
    m = re.search(r'msgctxt "#%d"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"' % i, t)
    return m.groups() if m else None


ids = sorted({int(x) for x in re.findall(r'\bText\((\d+)\)', src)})
bad = []
for i in ids:
    c = entry(core, i)
    for l, t in langs.items():
        e = entry(t, 54700 + i - 62700)
        if not c or not e or e[0] != c[0] or (l != 'en_gb' and not e[1]):
            bad.append(f'{i}/{l}')
check(ids and not bad, f'the {len(ids)} strings of the control are in the core and in Estuary, translated'
                       f'{": not " + ", ".join(bad) if bad else ""}')
print(f'{checks} checks')
print('PROJECTOR-SRC PASSED' if not fails else f'PROJECTOR-SRC FAILED ({fails})')
sys.exit(1 if fails else 0)
