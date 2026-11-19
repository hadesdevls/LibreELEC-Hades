// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Kodi's log for the host tests of xbmc/windowing/gbm/drm (nothing is written)
#pragma once

#include <string>

enum
{
  LOGDEBUG,
  LOGINFO,
  LOGWARNING,
  LOGERROR,
  LOGFATAL
};

class CLog
{
public:
  template<typename... Args>
  static void Log(int, const std::string&, Args&&...)
  {
  }
  static bool IsLogLevelLogged(int) { return false; }
};

#define LogF(level, format, ...) Log((level), ("{}: " format), __FUNCTION__, ##__VA_ARGS__)
