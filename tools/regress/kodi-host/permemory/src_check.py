#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""src_check.py <kodi tree>: the settings of films from add-ons kept by their ids (FilmMemory.h) where they meet code that no
host test runs (the player's thread, the queue of its callbacks, the video database, the settings and the strings), checked in
the source as it must stand: each check reads the body of one function (its braces matched) and looks for the rule there, and
where it is in it. One line per check, "ok: <check>" or "FAIL: <check>: <why>", then PERMEMORY-SRC PASSED or
PERMEMORY-SRC FAILED. The logic itself is tested by TestFilmMemory (googletest); the mutants of mutants.py break each rule here,
and its check must fail."""
import re
import sys

K = sys.argv[1]
fails = 0


def read(path):
    return open('%s/%s' % (K, path), encoding='utf-8').read()


def body(text, signature):
    """the text of the function whose definition starts with the regex <signature>, from its first { to the } that closes it"""
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


def check(name, ok, why):
    global fails
    if ok:
        print('ok: %s' % name)
    else:
        print('FAIL: %s: %s' % (name, why))
        fails += 1


def order(text, *parts):
    """each part is in text, after the one before it"""
    if text is None:
        return False
    pos = -1
    for p in parts:
        pos = text.find(p, pos + 1)
        if pos < 0:
            return False
    return True


VP = read('xbmc/cores/VideoPlayer/VideoPlayer.cpp')

# ---- the player: when the film is named, when its records are taken, when they are kept
prepare = body(VP, r'^void CVideoPlayer::Prepare\(\)')
check('named-before-the-dv-memory',
      order(prepare, 'm_film.identity = KODI::FILMMEMORY::IdentityOfItem(m_item, why);',
            'DVBRIDGE::FileOpened(m_film.identity ? KODI::FILMMEMORY::OutputMemoryKey(*m_film.identity)'),
      'the output of Dolby Vision chosen for the film is not looked for by its ids (or before they are known)')
check('path-settings-signalled',
      prepare is not None and re.search(r'cb->RequestVideoSettings\(fileItem\);\s*settingsLoaded->Set\(\);', prepare) is not None,
      'nothing says when the settings kept for the path were given to the player')
check('restored-between-demuxer-and-streams',
      order(prepare, 'if (!OpenDemuxStream())', 'CreatePlayers();', 'RestoreFilmSettings();', 'OpenDefaultStreams();',
            'm_film.subtitleVisibleAtStart = m_VideoPlayerVideo->IsSubtitleEnabled();'),
      'the settings of the film are not taken after the demuxer opened and before the streams are chosen')
restore = body(VP, r'^void CVideoPlayer::RestoreFilmSettings\(\)')
check('no-wait-for-other-videos',
      order(restore, 'if (!m_film.identity', 'm_film.settingsLoaded->Wait(5000ms)', 'm_callback.RestoreFilmSettings(',
            'SetVideoSettings(settings);'),
      'a video without an identity waits for the queue, or the settings are taken before those of the path')
check('discs-by-their-path',
      order(restore, 'IsStreamType(DVDSTREAM_TYPE_DVD)', 'IsStreamType(DVDSTREAM_TYPE_BLURAY)', 'm_film.identity.reset();',
            'm_film.settingsLoaded->Wait('),
      'a disc does not keep its settings for its path')
check('late-path-settings-take-nothing',
      restore is not None and re.search(r'Wait\(5000ms\)\)\s*\{.*?m_film\.identity\.reset\(\);\s*return;\s*\}', restore, re.S)
      is not None,
      'where the settings of the path come late, those of the film are taken anyway (the late ones would undo them)')
stores = [m.start() for m in re.finditer(r'cb->StoreVideoSettings\(fileItem, vs\);\s*\}\);\s*StoreFilmSettings\(vs\);', VP)]
check('kept-at-both-ends', len(stores) == 2,
      'the settings of the film are not kept where those of the path are (the end of the file and another file asked for): %d'
      % len(stores))
store = body(VP, r'^void CVideoPlayer::StoreFilmSettings\(const CVideoSettings& vs\)')
# (seen in QEMU: subtitles turned off where they did not show at the start were not kept as a choice)
check('subtitles-off-kept',
      store is not None and 'vs.m_SubtitleOn != CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn ||' in store,
      'subtitles turned off are not kept as a choice where they did not show at the start')
check('kept-once-after-looked', order(store, 'if (!m_film.identity || !m_film.looked)', 'm_film.looked = false;',
                                      'm_outboundEvents->Submit('),
      'the settings of a film are kept without having been looked at, or twice')

# ---- the item: who keeps its settings by its ids
item = body(read('xbmc/cores/VideoPlayer/FilmMemoryItem.cpp'), r'^std::optional<Identity> IdentityOfItem\(')
check('only-with-the-setting', order(item, 'SETTING_VIDEOPLAYER_HADESFILMMEMORY', 'IdentityOf('),
      'the setting is not asked first')
check('only-from-add-ons',
      order(item, 'URIUtils::IsPlugin(path)', 'if (!fromAddon && !URIUtils::IsInternetStream(played) && '
            '!URIUtils::IsInternetStream(path))\n    return std::nullopt;'),
      'a file of this device or of the local network could keep its settings by its ids')
check('library-by-its-file', item is not None and re.search(r'^  if \(tag\.m_iDbId > 0\)$', item, re.M) is not None,
      'an item of the library could keep its settings by its ids')
# (a file id is given to any path that played before: a stream with a link that does not change, or the plugin:// path of an
# add-on that resolves it; seen in QEMU, where the second playback of each film was taken for an item of the library)
check('not-by-file-id', item is not None and 'm_iFileId' not in item,
      'a stream that played before is taken for an item of the library by its file id')

# ---- the database: a table of its own, the version of Kodi's untouched
VDB = read('xbmc/video/VideoDatabase.cpp')
check('table-if-not-exists', 'CREATE TABLE IF NOT EXISTS hadesfilmsettings (' in VDB,
      'the table is not made where it is missing (a database of 1.1 has none)')
mig = read('xbmc/video/VideoDatabaseMigration.cpp')
check('no-migration-of-kodi', 'hadesfilmsettings' not in mig,
      'Kodi\'s migrations name the table (its version would carry it)')
erase = body(VDB, r'^void CVideoDatabase::EraseAllVideoSettings\(\)')
check('erased-with-all', order(erase, 'DELETE FROM settings', 'DELETE FROM hadesfilmsettings'),
      '"set as default for all media" leaves the settings of films')

# ---- the callback: the records of the same film only
cbk = read('xbmc/application/ApplicationPlayerCallback.cpp')
rb = body(cbk, r'^std::optional<KODI::FILMMEMORY::Restoration> CApplicationPlayerCallback::RestoreFilmSettings\(')
check('same-film-only', rb is not None and 'if (SameVideo(row.identity, identity) && Parse(row.text, defaults, record))' in rb,
      'a record with an id that differs could be taken')
sb = body(cbk, r'^void CApplicationPlayerCallback::StoreFilmSettings\(')
check('kept-by-release', order(sb, 'SameVideo(row.identity, identity)', 'SameRelease(kept.release, record.release)',
                               'db.SetFilmSettings(id, ids,'),
      'a record is not replaced by release')

# ---- the setting and its strings
st = read('system/settings/settings.xml')
m = re.search(r'<setting id="videoplayer\.hadesfilmmemory" type="boolean" label="62650" help="62651">(.*?)</setting>', st, re.S)
check('setting-on-by-default', m is not None and '<default>true</default>' in m.group(1), 'the setting is missing or off by default')
base = read('xbmc/settings/dialogs/GUIDialogSettingsBase.cpp')
check('setting-translated', 'if (labelId == 62650 || labelId == 62651)\n    return CGUIUtils::GetHadesString(labelId, 54650 + (labelId - 62650));'
      in base, 'the label and help are not taken from Estuary')
core = read('addons/resource.language.en_gb/resources/strings.po')
missing = [n for n in range(62650, 62660) if 'msgctxt "#%d"' % n not in core]
check('core-strings', not missing, 'core strings missing: %s' % missing)
for lang in ('en_gb', 'es_es', 'es_ar', 'es_mx'):
    po = read('addons/skin.estuary/language/resource.language.%s/strings.po' % lang)
    bad = []
    for n in range(54650, 54660):
        e = re.search(r'msgctxt "#%d"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"' % n, po)
        if e is None or (lang != 'en_gb') != (e.group(2) != ''):
            bad.append(n)
    check('estuary-%s' % lang, not bad, 'strings missing or not translated as they should be: %s' % bad)

print('PERMEMORY-SRC PASSED' if fails == 0 else 'PERMEMORY-SRC FAILED')
sys.exit(1 if fails else 0)
