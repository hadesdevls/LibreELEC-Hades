// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
// a stub of utils/log.h of Kodi for the host tests: a line per message, its format only
#pragma once
#include <cstdio>
enum { LOGDEBUG, LOGINFO, LOGWARNING, LOGERROR };
struct CLog
{
  template<typename... A>
  static void Log(int, const char* f, A&&...)
  {
    std::printf("log: %s\n", f);
  }
};
