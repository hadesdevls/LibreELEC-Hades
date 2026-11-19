# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants of Dolby Vision sent as HDR10+ (tools/dvbridge/dvbridge_dvplus.c, xbmc/windowing/gbm/DVPlusClock.h,
# DVPlusModeStart.h, DVPlusVBlank.h, HdmiVrrOff.h, HDR10PlusOutput.h, xbmc/utils/EdidHdr10Plus.h, EdidHdmiVic.h) and of
# the policy of Dolby Vision converted to HDR10+ (DVPlaybackPolicy.h), each with the test of run-dvbridge-meta.sh
# (stage dvplus) that must fail on it. Expected: the test's lines that start with FAIL, or "-" for any number above 0.
DIRS = ['tools/dvbridge', 'xbmc/utils', 'xbmc/windowing/gbm', 'xbmc/cores/VideoPlayer/DVDCodecs/Video']
D = 'tools/dvbridge/dvbridge_dvplus.c'
C = 'xbmc/windowing/gbm/DVPlusClock.h'
H = 'xbmc/windowing/gbm/HDR10PlusOutput.h'
S = 'xbmc/windowing/gbm/DVPlusModeStart.h'
V = 'xbmc/windowing/gbm/HdmiVrrOff.h'
E = 'xbmc/utils/EdidHdr10Plus.h'
N = 'xbmc/utils/EdidHdmiVic.h'
B = 'xbmc/windowing/gbm/DVPlusVBlank.h'


def m(name, path, old, new, test, what, expected='-'):
    return (name, path, [(old, new)], test, expected, what)


MUTANTS = [
    # ------------------------------------------------------------------ the module (dvplus_test.c)
    m('unit-no-refresh-rule', D, 'return !(b->refresh && !a->refresh);', 'return true;', 'unit', 'a scene refresh no longer needed to change the statistics'),
    m('unit-no-level1-rule', D, 'if (a->level1 && (a->min_pq != b->min_pq || a->max_pq != b->max_pq || a->avg_pq != b->avg_pq))', 'if (0)', 'unit', 'another level 1 block is the same scene'),
    m('unit-every-refresh-cuts', D, 'return !(b->refresh && !a->refresh);', 'return !b->refresh;', 'unit', 'every refresh flag cuts'),
    m('unit-keep-old-scene-measure', D, 'if (at < 0 || m->ring[at].scene != m->scene || !m->open) {', 'if (at < 0 || !m->open) {', 'unit', 'the measure of another scene kept'),
    m('unit-no-pq-step', D, 'return !pq_step || fabs(pq_code(a) - pq_code(b)) > pq_step;', 'return true;', 'unit', 'no step of PQ before a change'),
    m('unit-no-hold', D, '} else if (moves(&made, &m->sent, m->params.hysteresis, m->params.pq_step, m->version, DARK_STEP) &&', '} else if (1 &&', 'unit', 'no hysteresis: every small move sent'),
    m('unit-never-change', D, '} else if (moves(&made, &m->sent, m->params.hysteresis, m->params.pq_step, m->version, DARK_STEP) &&', '} else if (0 &&', 'unit', 'the statistics never change within a scene'),
    m('unit-no-interval', D, '(m->since_change >= m->params.interval ||', '(1 ||', 'unit', 'no interval between two changes'),
    m('unit-no-bypass', D, '(m->params.big_step &&\n                 moves(&made, &m->sent, m->params.hysteresis, m->params.big_step, m->version, DARK_BIG_STEP))', '0', 'unit', 'a big step waits for the interval too'),
    m('unit-budget-no-margin', D, 'return next - margin - now;', 'return next - now;', 'unit', 'the wait budget without its margin'),
    m('unit-budget-this-vblank', D, '((now - last_vblank) / period + 1) * period', '((now - last_vblank) / period + 2) * period', 'unit', 'the budget to the vblank after the next'),
    m('unit-again-not-kept', D, '    m->last_send = true;\n    m->last_stats = m->sent;', '    m->last_send = true;', 'unit', 'the statistics sent again are not those kept'),
    m('unit-no-lost', D, 'm->last_send = m->have && m->unmeasured <= m->params.lost;', 'm->last_send = m->have;', 'unit', 'statistics kept however long the measure is lost'),
    m('unit-repeat-not-cached', D, '*stats = m->last_stats;', '*stats = m->sent;', 'unit', 'a repeated frame takes the statistics of now'),
    m('unit-q9998-as-99', D, '0.95, 0.9998};', '0.95, 0.99};', 'unit', 'the last place the 99th percentile, not 99.98'),
    m('unit-l5-given-only', D, 'v[i] = !master ? given : !fits ? scaled : fmin(given, scaled);', 'v[i] = given;', 'unit', 'the level 5 area as given, not fitted'),
    m('unit-l5-outward', D, '(int)ceil((y0 - (double)src[1]) * sy - 1e-6)', '(int)floor((y0 - (double)src[1]) * sy) - 1', 'unit', 'the level 5 area rounded outwards'),
    m('unit-tsdml-992', D, 'out->targeted_system_display_maximum_luminance = (AVRational){0, 1};', 'out->targeted_system_display_maximum_luminance = (AVRational){992, 1};', 'unit', 'a target display of 992'),
    m('unit-back-not-cut', D, 'const bool cut = !m->open || back || !same_scene(&m->last_rpu, rpu);', 'const bool cut = !m->open || !same_scene(&m->last_rpu, rpu);', 'unit', 'a picture before the last is no cut'),
    # ------------------------------------------------------------------ the bytes of the InfoFrame (dvplus_bytes_test.cpp)
    m('bytes-places-0', D, '0.01, 0.05, 0.10, 0.25', '0.01, 0, 0, 0.25', 'bytes', 'the places of 5 and 10 % at 0'),
    m('bytes-version-1', D, 'out->application_version = version == 0 ? 0 : 1;', 'out->application_version = 1;', 'bytes', 'always application version 1'),
    m('bytes-no-min', D, '    av = av < mn ? mn : av;', '', 'bytes', 'the average below the minimum of the level 1 block'),
    m('bytes-no-l1q', D, 'stats->percentiles[i] = units(dvbridge_dvplus_nits(DVPLUS_L1_Q(QUANTILES[i])));', 'stats->percentiles[i] = units(dvbridge_dvplus_nits(QUANTILES[i]));', 'bytes', 'the provisional percentiles not from the level 1 block'),
    m('bytes-no-level1', D, 'if (!rpu || !rpu->level1 || !stats)', 'if (!rpu || !stats)', 'bytes', 'provisional statistics without a level 1 block'),
    m('bytes-places-swapped', D, '0.01, 0.05, 0.10, 0.25', '0.01, 0.10, 0.05, 0.25', 'bytes', 'the places of 5 and 10 % swapped'),
    # ------------------------------------------------------------------ the GPU samples, the policy, the native path
    m('gl-luma-bt709', 'tools/dvbridge/dvbridge_dvplus_gl.c', 'vec3(0.2627, 0.6780, 0.0593)', 'vec3(0.2126, 0.7152, 0.0722)', 'gl', 'the luminance of each sample with the weights of BT.709'),
    m('policy-no-hdr10plus', 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h', 'return hdr10Plus && setting != ConversionSetting::NEVER &&', 'return false && hdr10Plus && setting != ConversionSetting::NEVER &&', 'policy', 'no stream is rebuilt for Dolby Vision sent as HDR10+'),
    m('native-ten-99', 'xbmc/utils/HDR10PlusVsif.h', 'if (w.distribution_maxrgb[entry].percentage != 98)', 'if (w.distribution_maxrgb[entry].percentage != 99)', 'native', 'the form of ten percentiles taken with 99 % where the ninth place of the form is 98 %'),
    # ------------------------------------------------------------------ the clock (dvplus_clock_test.cpp)
    m('clock-flip-over-vblank', C, 'if (vblank > 0 && period > 0)', 'if (vblank > 0 && period > 0 && m_flip <= 0)', 'clock', 'a known flip taken before the vblank'),
    m('clock-no-period-check', C, 'if (m_flip > 0 && period > 0 && period == m_period)', 'if (m_flip > 0 && period > 0)', 'clock', 'a flip of another period taken'),
    m('clock-lost-keeps', C, '  void Lost()\n  {\n    m_flip = 0;', '  void Lost()\n  {\n    return;', 'clock', 'a lost clock keeps its flip'),
    m('clock-no-reanchor', C, '    m_flip = ns;\n    m_period = period;', '    if (!m_flip) m_flip = ns;\n    m_period = period;', 'clock', 'the first flip kept for ever'),
    m('clock-unread-kept', C, '    if (ns <= 0 || period <= 0)\n    {\n      Lost();\n      return;\n    }', '    if (ns <= 0 || period <= 0)\n      return;', 'clock', 'a flip that could not be read keeps the one before'),
    # ------------------------------------------------------------------ the decisions of Kodi (hdr10plus_kodi_test.cpp)
    m('kodi-version-any', H, 'return receiverVersion >= 0 && PayloadVersion(payload) <= std::min(receiverVersion, 1);', 'return receiverVersion >= 0;', 'kodi', 'a payload of any version to a display with a block'),
    m('kodi-version-receiver', H, 'return receiverVersion >= 0 && PayloadVersion(payload) <= std::min(receiverVersion, 1);', 'return receiverVersion >= 0 && PayloadVersion(payload) <= receiverVersion;', 'kodi', 'a version above 1 taken where the display says so'),
    m('kodi-no-version-gate', H, 'if (on && payload && VersionFits(*payload, output.receiverVersion))', 'if (on && payload)', 'kodi', 'no gate of the version'),
    m('kodi-version-bits', H, 'static int PayloadVersion(const Payload& payload) { return payload[3] >> 6; }', 'static int PayloadVersion(const Payload& payload) { return payload[3] >> 5; }', 'kodi', 'the version read from the wrong bits'),
    m('kodi-mode-commit-carried', H, 'm_modeCommitCarried = m_queued.has_value();', 'm_modeCommitCarried = true;', 'kodi', 'the commit of the mode always said to carry a payload'),
    m('kodi-wait-without-mode', S, 'return needed && withMode && !sent ? Then::WAIT : Then::AS_BEFORE;', 'return needed && !sent ? Then::WAIT : Then::AS_BEFORE;', 'kodi', 'the wait also without a commit of the mode'),
    m('kodi-never-wait', S, 'return needed && withMode && !sent ? Then::WAIT : Then::AS_BEFORE;', 'return Then::AS_BEFORE;', 'kodi', 'never a wait for the first payload'),
    m('kodi-wait-rate', S, 'refreshHz > 0 && refreshHz < 30.5;', 'refreshHz > 0 && refreshHz < 61;', 'kodi', 'the wait at every rate up to 60 Hz'),
    m('kodi-wait-i915-only', S, 'return (driver == "i915" || driver == "xe") && height == 2160 &&', 'return (driver == "i915") && height == 2160 &&', 'kodi', 'xe left out of the wait'),
    m('kodi-provisional-ever', S, 'return provisional && !moduleSends && since < lost;', 'return provisional && !moduleSends;', 'kodi', 'provisional statistics kept for ever'),
    m('kodi-provisional-never', S, 'return provisional && !moduleSends && since < lost;', 'return false;', 'kodi', 'provisional statistics never kept'),
    m('kodi-wait-one-period', S, 'std::min<int64_t>(2 * periodNs, WAIT_MAX_NS)', 'std::min<int64_t>(periodNs, WAIT_MAX_NS)', 'kodi', 'a wait of one period'),
    m('kodi-outcome-order', S, 'return measured ? Outcome::MEASURED : provisional ? Outcome::PROVISIONAL : Outcome::NONE;', 'return provisional ? Outcome::PROVISIONAL : measured ? Outcome::MEASURED : Outcome::NONE;', 'kodi', 'provisional before measured'),
    m('kodi-vrr-50', V, 'refreshMilliHz >= 50000;', 'refreshMilliHz > 50000;', 'kodi', 'VRR off from above 50 Hz only'),
    m('kodi-vrr-display', V, 'return property && withMode && hdr10Plus && displayVrr && !withVrrSlot && vdisplay >= 720 &&', 'return property && withMode && hdr10Plus && !withVrrSlot && vdisplay >= 720 &&', 'kodi', 'VRR off for a display without VRR'),
    m('kodi-vrr-1080', V, 'return property && withMode && hdr10Plus && displayVrr && !withVrrSlot && vdisplay >= 720 &&', 'return property && withMode && hdr10Plus && displayVrr && !withVrrSlot && vdisplay >= 1080 &&', 'kodi', 'VRR off from 1080 lines'),
    m('kodi-vrr-modeset', V, 'if (nativeDV || !modesetPending || !kernel)', 'if (nativeDV || !kernel)', 'kodi', 'VRR staged without a commit of the mode'),
    m('kodi-vrr-want', V, '    if (*kernel == want)', '    if (false)', 'kodi', 'VRR staged although the kernel has it'),
    m('kodi-vrr-rounding', V, '* 1000 + frame / 2) / frame) * 1000;', '* 1000) / frame) * 1000;', 'kodi', 'the refresh in mHz not rounded'),
    m('kodi-edid-bit0', E, 'version = std::max(version, static_cast<int>(data[4] & 3));', 'version = std::max(version, static_cast<int>(data[4] & 1));', 'kodi', 'one bit of the version read'),
    m('kodi-edid-last', E, 'version = std::max(version, static_cast<int>(data[4] & 3));', 'version = static_cast<int>(data[4] & 3);', 'kodi', 'the version of the last block, not the highest'),
    m('kodi-edid-len4', E, 'if (tag == 7 && len >= 5 && data[0] == 0x01', 'if (tag == 7 && len >= 4 && data[0] == 0x01', 'kodi', 'a block of 4 bytes read'),
    m('kodi-edid-tag3', E, 'if (tag == 7 && len >= 5 && data[0] == 0x01', 'if ((tag == 7 || tag == 3) && len >= 5 && data[0] == 0x01', 'kodi', 'a vendor block read as HDR10+'),
    m('cross-edid-version0', E, 'data[3] == 0x90)', 'data[3] == 0x90 && data[4] != 0)', 'cross', 'a block of application version 0 not read (the kernel reads it)'),
    m('kodi-avi-vic-always', S, 'return aviCtaVicProperty &&', 'return true ||', 'kodi', 'the CTA VIC in the AVI InfoFrame wherever the property is not there'),
    m('nvvic-vics', N, 'return !s.vics[CtaVicOfHdmiVic(hv)] && !(integerTwin && ownDtd);', 'return !(integerTwin && ownDtd);', 'nvvic', 'NVKMS names a mode by an HDMI VIC also where the display lists its CTA VIC'),
    m('nvvic-4096', N, 'if (width == 4096)\n      return true;', 'if (width == 4096)\n      return false;', 'nvvic', '4096 wide modes not named by an HDMI VIC'),
    m('nvvic-twin', N, 'return !(integerTwin && ownDtd);\n}', 'return true;\n}', 'nvvic', 'integer-rate twins with their own timing named by an HDMI VIC'),
    m('nvvic-hdmi-vics', N, 'if (!hv || !s.hdmiVics[hv])', 'if (!hv)', 'nvvic', 'a mode named by an HDMI VIC the display does not list'),
    m('nvvic-23976', N, 'return at(24000) ? 4 : 0;', 'return at(24000) || at(23976) ? 4 : 0;', 'nvvic', '23.976 Hz taken as an HDMI VIC rate'),
    m('nvvic-uhd-bit', N, 's.uhdVic = (blk[6] & 0x08) != 0;', 's.uhdVic = (blk[6] & 0x04) != 0;', 'nvvic', 'the UHD VIC bit read from the wrong place'),
    m('nvvic-vic-count', N, 'const unsigned n = blk[k + 1] >> 5;', 'const unsigned n = blk[k + 1] & 0x1f;', 'nvvic', 'the count of HDMI VICs read from the wrong bits'),
    m('kodi-needs-vic', N, 's.vics[CtaVicOfHdmiVic(HdmiVicOfMode(width, height, rrx1k))] && s.scdc && s.hdr10Plus && s.st2084;', 's.scdc && s.hdr10Plus && s.st2084;', 'kodi', 'the wait without the display listing the CTA VIC'),
    m('nvvic-scdc-bit', N, 'blk[0] == 0x06 && (blk[1] & 0x04) && (blk[2] & 0x01))', 'blk[0] == 0x06 && (blk[1] & 0x08) && (blk[2] & 0x01))', 'nvvic', 'the HDR10+ bit of the display read from the wrong place'),
    m('kodi-st2084', N, 'blk[0] == 0x06 && (blk[1] & 0x04) && (blk[2] & 0x01))', 'blk[0] == 0x06 && (blk[1] & 0x04))', 'kodi', 'ST 2084 not asked of the display'),
    m('vblank-any-answer', B, 'if (s == 0 && ns > 0 && ns <= static_cast<uint64_t>(INT64_MAX))', 'if (s == 0)', 'vblank', 'any answer of CRTC_GET_SEQUENCE taken'),
    m('vblank-no-crtc', B, 'const int s = crtcId ? seq(crtcId, &sequence, &ns) : ENODEV;', 'const int s = seq(crtcId, &sequence, &ns);', 'vblank', 'CRTC_GET_SEQUENCE asked without a CRTC'),
    m('vblank-high-crtc', B, 'return RELATIVE | ((index << HIGH_CRTC_SHIFT) & HIGH_CRTC_MASK);', 'return RELATIVE | (index & HIGH_CRTC_MASK);', 'vblank', 'the CRTC of the legacy request not in its high bits'),
    m('vblank-error-0', B, 'e.sequence = s ? s : EINVAL;', 'e.sequence = s;', 'vblank', 'an error of 0 kept'),
    m('vblank-no-legacy', B, 'if (l == 0 && (sec > 0 || usec > 0))', 'if (false)', 'vblank', 'the legacy answer never taken'),
    m('vblank-usec', B, '* 1000000000 + static_cast<int64_t>(usec) * 1000;', '* 1000000000 + static_cast<int64_t>(usec);', 'vblank', 'microseconds taken as nanoseconds'),
]
