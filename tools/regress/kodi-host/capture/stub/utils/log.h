// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
// utils/log.h of Kodi for CaptureConvert.cpp on the host: what it logs, on stdout (its format string only)
#pragma once
#include <cstdio>

#define LOGDEBUG 0
#define LOGINFO 1
#define LOGWARNING 2
#define LOGERROR 3

struct CLog
{
  template<typename... A>
  static void LogF(int, const char* format, A&&...)
  {
    std::printf("LOG %s\n", format);
  }
  template<typename... A>
  static void Log(int, const char* format, A&&...)
  {
    std::printf("LOG %s\n", format);
  }
};
