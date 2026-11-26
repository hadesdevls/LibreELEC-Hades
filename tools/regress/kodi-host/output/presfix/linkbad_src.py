#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""linkbad_src.py <kodi tree>: in the source, what the host test of a link that the kernel marks Bad (linkbad_test.cpp)
cannot run, as LinkStatus.h and its two callers have it:

- a change of mode of Kodi's (CDRMAtomic::DrmAtomicCommit()) sets link-status Good inside the block of a commit that
  allows a modeset, after MODE_ID and ACTIVE, only for an output that it keeps or turns on (m_active) and only where
  CarriesGood() says the link is Bad; and nowhere else in Kodi is link-status written (it stays out of the capture of
  native output: OutputState.h);
- CWinSystemGbm::LookAtLinkStatus() does nothing for a connector without the property (with Good and Bad), reads the
  link only when CRetrain::Due() says so, gives the budget back with a change of mode of Kodi's, asks for the output
  only through LookAt(), with a change of mode pending of Kodi's as CDRMAtomic says it, and commits through
  CLibdrmKms, whose commit waits and allows a modeset; it is called once per frame from PresentRender();
- Changes() asks for Good only.
Prints LINKBAD-SRC PASSED, or a line FAIL: for each thing that is not so.
"""
import os
import re
import sys

K = sys.argv[1]
G = os.path.join(K, 'xbmc/windowing/gbm')
fails = []
checks = [0]


def read(rel):
    return open(os.path.join(G, rel), encoding='utf-8').read()


def need(ok, what):
    checks[0] += 1
    if not ok:
        fails.append(what)


def body(text, head):
    """the text of a function from its head to the brace that closes it (braces in string and character literals and
    in comments are not counted)"""
    i = text.find(head)
    if i < 0:
        return ''
    j = text.find('{', i + len(head))
    depth = 0
    k = j
    while 0 <= k < len(text):
        c = text[k]
        if c in '"\'':
            k += 1
            while k < len(text) and text[k] != c:
                k += 2 if text[k] == '\\' else 1
        elif text.startswith('//', k):
            k = text.find('\n', k)
            continue
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
        k += 1
    return ''


A = read('drm/DRMAtomic.cpp')
add = 'AddProperty(m_connector, LINK_STATUS::PROPERTY, link->good)'
need(A.count(add) == 1, 'DRMAtomic.cpp: link-status Good added once (%d)' % A.count(add))
commit = body(A, 'bool CDRMAtomic::DrmAtomicCommit(')
block = commit.find('  if (flags & DRM_MODE_ATOMIC_ALLOW_MODESET)\n  {\n    if (!AddProperty(m_connector, "CRTC_ID"')
end = commit.find('  // In Direct-To-Plane', block)
at = commit.find(add)
need(block >= 0 and end > block and block < at < end,
     'DRMAtomic.cpp: link-status Good inside the block of a commit that allows a modeset')
need(commit.find('AddProperty(m_crtc, "ACTIVE", m_active ? 1 : 0)', block) < at,
     'DRMAtomic.cpp: link-status Good after MODE_ID and ACTIVE')
gate = '    if (m_active && m_connector->SupportsProperty(LINK_STATUS::PROPERTY))\n'
need(gate in commit and commit.find(gate) < at, 'DRMAtomic.cpp: only for an output that is on, with the property')
need('LINK_STATUS::CarriesGood(link, m_connector->ReadPropertyValue(LINK_STATUS::PROPERTY))' in commit,
     'DRMAtomic.cpp: only where the link reads Bad now (CarriesGood())')

# nowhere else is link-status written
writers = []
for root, _, files in os.walk(os.path.join(K, 'xbmc')):
    for f in files:
        if not f.endswith(('.cpp', '.h')):
            continue
        p = os.path.join(root, f)
        t = open(p, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'(AddProperty|AddSoftProperty|SetProperty|drmModeAtomicAddProperty|drmModeObjectSetProperty)'
                             r'\([^;]*(LINK_STATUS::PROPERTY|"link-status")', t):
            writers.append(os.path.relpath(p, K))
need(writers == ['xbmc/windowing/gbm/drm/DRMAtomic.cpp'], 'link-status written only by DRMAtomic.cpp: %s' % writers)
O = read('drm/OutputState.h')
need(re.search(r'\{Role::CONNECTOR, "link-status",', O) is not None, 'OutputState.h: link-status left out of the capture')

L = read('drm/LinkStatus.h')
ch = body(L, 'inline std::vector<OUTPUT_STATE::Change> Changes(')
need('values.good' in ch and 'values.bad' not in ch and ch.count('{OUTPUT_STATE::Role::') == 1,
     'LinkStatus.h: Changes() asks for link-status Good, alone')
K2 = read('drm/LibdrmKms.h')
need('const int flags = DRM_MODE_ATOMIC_ALLOW_MODESET | (testOnly ? DRM_MODE_ATOMIC_TEST_ONLY : 0);' in K2,
     'LibdrmKms.h: its commit allows a modeset and waits (no NONBLOCK)')

W = read('WinSystemGbm.cpp')
look = body(W, 'void CWinSystemGbm::LookAtLinkStatus()')
need(look != '', 'WinSystemGbm.cpp: LookAtLinkStatus()')
order = ['  if (!m_linkValues || !m_linkProperty)\n    return;',
         '    m_linkRetrain.KodiModeset();',
         '  if (!m_linkRetrain.Due(nowMs, event, afterModeset))\n    return;',
         '    out.kodiModesetPending = drm->ModesetPending() || drm->ModesetRetryPending();',
         '  OUTPUT_STATE::CLibdrmKms kms(drm->GetFileDescriptor());',
         '  const Look look = LookAt(m_linkRetrain, kms, id, m_linkProperty, m_linkValues, output, nowMs);']
pos = -1
for o in order:
    p = look.find(o)
    need(p > pos, 'WinSystemGbm.cpp LookAtLinkStatus(): %r in its place' % o.strip()[:70])
    pos = max(pos, p)
need(look.count('kms.Commit(') == 0 and look.count('drmModeAtomicCommit') == 0,
     'LookAtLinkStatus(): no commit but through LookAt()')
E = read('WinSystemGbmGLESContext.cpp')
pr = body(E, 'void CWinSystemGbmGLESContext::PresentRender(')
need(pr.count('  LookAtLinkStatus();\n') == 1 and E.count('LookAtLinkStatus();') == 1,
     'WinSystemGbmGLESContext.cpp: LookAtLinkStatus() once, in PresentRender()')

for f in fails:
    print('FAIL: ' + f)
print('LINKBAD-SRC: %d checks, %d failed' % (checks[0], len(fails)))
if not fails:
    print('LINKBAD-SRC PASSED')
sys.exit(1 if fails else 0)
