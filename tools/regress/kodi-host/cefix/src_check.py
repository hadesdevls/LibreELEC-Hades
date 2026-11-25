#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""src_check.py <kodi tree>: the fixes taken from CoreELEC's forks that live in code no host test can run (the player's thread, curl,
libsmbclient, the power manager, a std::thread), checked in the source as the code must stand: each check reads the body
of one function (its braces matched) and looks for the rule there, and where it is in it. One line per check,
"ok: <check>" or "FAIL: <check>: <why>", then CEFIX-SRC PASSED or CEFIX-SRC FAILED. The mutants of mutants.py put each
rule back as it was before its fix, and its check must fail."""
import re
import sys

K = sys.argv[1]
fails = 0


def read(path):
    return open('%s/%s' % (K, path), encoding='utf-8').read()


def body(text, signature):
    """the text of the function whose definition starts with the regex <signature>, from its first { to the }
    that closes it (strings and comments are not skipped: the functions checked have no brace in them)"""
    m = re.search(signature, text, re.M)
    if not m:
        return None
    start = text.index('{', m.end())
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def depth_at(text, pos):
    """the depth of braces at pos of a body (1: the function's own level)"""
    return text.count('{', 0, pos) - text.count('}', 0, pos)


def check(name, ok, why):
    global fails
    if ok:
        print('ok: %s' % name)
    else:
        fails += 1
        print('FAIL: %s: %s' % (name, why))


def has(b, *texts):
    return b is not None and all(t in b for t in texts)


# ---- CurlFile: the credentials of every protocol, before the protocol's own handling (xbmc/xbmc#30391)
curl = read('xbmc/filesystem/CurlFile.cpp')
b = body(curl, r'^void CCurlFile::ParseAndCorrectUrl\(CURL ?&url2\)')
user = b.find('m_username = url2.GetUserName();') if b else -1
ftp = b.find('url2.IsProtocol("ftp")') if b else -1
check('curl-credentials-every-protocol', b is not None and user >= 0 and depth_at(b, user) == 1 and 0 <= user < ftp and
      b.count('m_username = url2.GetUserName();') == 1 and b.count('m_password = url2.GetPassWord();') == 1,
      'm_username is not read once at the level of the function, before the protocol branches')
check('curl-ftp-no-lone-slash', has(b, '!filename.empty() && StringUtils::EndsWith(url2.GetFileName(), "/")'),
      'the trailing slash of an FTP path is put back after an empty name')

# ---- DVDDemuxFFmpeg: a .sup seek before the first packet, the HLS options
dmx = read('xbmc/cores/VideoPlayer/DVDDemuxers/DVDDemuxFFmpeg.cpp')
b = body(dmx, r'^bool CDVDDemuxFFmpeg::SeekTime\(')
check('sup-seek-before-first-packet',
      b is not None and re.search(r'if \(ret < 0 && backwards && m_bSup &&\s+m_pFormatContext->start_time != '
                                  r'static_cast<int64_t>\(AV_NOPTS_VALUE\) &&\s+seek_pts < m_pFormatContext->start_time\)'
                                  r'\s+ret = av_seek_frame\(m_pFormatContext, m_seekStream, seek_pts, 0\);', b) is not None,
      'no forward seek of a .sup file whose backward one failed before its start')
b = body(dmx, r'^bool CDVDDemuxFFmpeg::Open\(')
check('hls-one-variant-options', has(b, 'av_dict_set_int(&options, "load_all_variants", 0, 0);',
                                     'av_dict_set_int(&options, "max_bandwidth",', 'CSettings::SETTING_NETWORK_BANDWIDTH'),
      'the open does not pass load_all_variants=0 with max_bandwidth')
again = b.find('if (opened < 0 && opened != AVERROR_EXIT && !m_abortRequested && mayBeHls)') if b else -1
check('hls-open-again-with-all', again >= 0 and
      re.search(r'options = GetFFMpegOptionsFromInput\(\);.*avformat_open_input', b[again:again + 900], re.S) is not None,
      'an HLS stream whose one variant does not open is not opened again with all of them, or not only then')

# ---- VideoPlayer: the subtitle file
vp = read('xbmc/cores/VideoPlayer/VideoPlayer.cpp')
m = re.search(r'const auto FinishChapterSeek = \[[^\]]*\]\(\)\s*\{', vp)
lam = body(vp[m.start():], r'\(\)') if m else None
check('chapter-seek-subtitle-file', has(lam, 'm_pSubtitleDemuxer->SeekTime(DVD_TIME_TO_MSEC(start), true)'),
      'a chapter seek does not seek the subtitle file')
b = body(vp, r'^bool CVideoPlayer::CheckPlayerInit\(')
check('seek-start-audio-video-only', b is not None and re.search(
    r'if \(\(current\.type == StreamType::AUDIO \|\| current\.type == StreamType::VIDEO\) &&\s+'
    r'\(current\.startpts - current\.dts\) > DVD_SEC_TO_TIME\(20\)\)', b) is not None,
      'any stream moves the start of a seek back')
b = body(vp, r'^bool CVideoPlayer::OpenStream\(')
res = b.find('  bool res;') if b else -1
check('subtitle-file-set-when-open', b is not None and res > 0 and 'm_pSubtitleDemuxer' not in b[:res] and
      re.search(r'if \(current\.type == StreamType::SUBTITLE\)\s*\{\s*m_pSubtitleDemuxer = subtitleDemuxer;', b[res:])
      is not None and 'fileStream->uniqueId == iStream' in b[res:],
      'the subtitle demuxer is set before its stream opened, or not to the file of the stream that opened')
b = body(vp, r'^bool CVideoPlayer::CloseStream\(')
check('subtitle-file-dropped-when-closed', b is not None and re.search(
    r'if \(STREAM_SOURCE_MASK\(current\.source\) == STREAM_SOURCE_DEMUX_SUB\)\s+m_pSubtitleDemuxer\.reset\(\);', b)
      is not None, 'a closed stream of a subtitle file leaves its demuxer read')
nxt = vp.find('if(next == CDVDInputStream::NEXTSTREAM_OPEN)')
seg = vp[nxt:nxt + 800] if nxt >= 0 else ''
check('subtitle-file-dropped-next-stream', 'm_CurrentSubtitle.Clear();' in seg and 'm_pSubtitleDemuxer.reset();' in seg,
      'the next stream of an input keeps the old subtitle file read')
b = body(vp, r'^void CVideoPlayer::Process\(\)')
full = b.find('if (queuesFull())') if b else -1
check('subtitle-file-read-while-full', full >= 0 and 'ReadSubtitleFileWhileFull(queuesFull);' in b[full:full + 900],
      'the subtitle file is not read while the queues are full')
b = body(vp, r'^void CVideoPlayer::ReadSubtitleFileWhileFull\(')
check('subtitle-file-read-while-full-bounds', has(b, 'm_VideoPlayerSubtitle->AcceptsData()', 'm_messenger.IsEmpty()',
                                                  'queuesFull()', '!m_bAbortRequest', 'ProcessPacket(stream, packet);'),
      'the reading while full is not bounded by the subtitle player, the messages and the queues')

# ---- the decoded channel layout: one translation
codec = read('xbmc/cores/VideoPlayer/DVDCodecs/Audio/DVDAudioCodecFFmpeg.cpp')
b = body(codec, r'^void CDVDAudioCodecFFmpeg::BuildChannelMap\(\)')
check('decoded-layout-one-translation', has(b, 'CAEUtil::GetAEChannelLayout(') and 'AV_CH_TOP_BACK' not in (b or ''),
      'BuildChannelMap() has its own copy of the table')

# ---- SMB while the system sleeps (xbmc/xbmc#30662)
smbh = read('xbmc/platform/posix/filesystem/SMBFile.h')
check('smb-power-state', re.search(r'class CSMB : public CCriticalSection, public CPowerState', smbh) is not None,
      'CSMB is no CPowerState')
b = body(read('xbmc/platform/posix/filesystem/SMBFile.cpp'), r'^void CSMB::Init\(\)')
check('smb-no-context-asleep', has(b, 'if (!m_context && !IsSleeping())'), 'Init() makes a context while asleep')
smbd = read('xbmc/platform/posix/filesystem/SMBDirectory.cpp')
ok = True
for f in ('Create', 'Remove', 'Exists'):
    b = body(smbd, r'^bool CSMBDirectory::%s\(' % f)
    ok = ok and b is not None and re.search(r'smb\.Init\(\);\s+if \(!smb\.IsSmbValid\(\)\)\s+return false;', b) is not None
check('smb-directory-checks-context', ok, 'Create(), Remove() or Exists() goes on without a context')
pm = read('xbmc/powermanagement/PowerManager.cpp')
b = body(pm, r'^void CPowerManager::OnSleep\(\)')
check('smb-told-of-sleep', b is not None and 0 <= b.find('smb.OnSleep();') < b.find('g_application.CloseNetworkShares();'),
      'the SMB client is not told of the sleep before the shares are closed')
b = body(pm, r'^void CPowerManager::OnWake\(\)')
check('smb-told-of-wake', b is not None and 0 <= b.find('GetNetwork().WaitForNet();') < b.find('smb.OnWake();'),
      'the SMB client is not told of the wake once the network is back')

# ---- the refresh rates of a size, the demuxer ids, a thread's end
b = body(read('xbmc/settings/DisplaySettings.cpp'), r'^void CDisplaySettings::SettingOptionsRefreshRatesFiller\(')
check('refresh-rates-empty-list', has(b, 'if (!match && !refreshrates.empty())'),
      'the default refresh rate is asked of an empty list')
b = body(read('xbmc/windowing/WinSystem.cpp'), r'^REFRESHRATE CWinSystemBase::DefaultRefreshRate\(')
check('default-refresh-rate-empty-list', b is not None and 0 <= b.find('if (rates.empty())') < b.find('REFRESHRATE bestmatch = rates[0];'),
      'DefaultRefreshRate() reads rates[0] of an empty list')
check('demuxer-id-atomic', 'static std::atomic<int64_t> guid{0};' in read('xbmc/cores/VideoPlayer/DVDDemuxers/DVDDemux.h'),
      'the counter of demuxer ids is not atomic')
thr = read('xbmc/threads/Thread.cpp')
check('thread-exception-error', re.search(r'catch \(const std::exception& e\)\s*\{\s*CLog::Log\(LOGERROR, "Thread \{\} '
                                          r'ended by an exception: \{\}", threadName, e\.what\(\)\);', thr) is not None and
      re.search(r'catch \(\.\.\.\)\s*\{\s*CLog::Log\(LOGERROR, "Thread \{\} ended by an unknown exception", threadName\);',
                thr) is not None and 'threadName = pThread->m_ThreadName;' in thr,
      'a thread that ends by an exception is not an error with its name')

print('CEFIX-SRC PASSED' if fails == 0 else 'CEFIX-SRC FAILED')
sys.exit(1 if fails else 0)
