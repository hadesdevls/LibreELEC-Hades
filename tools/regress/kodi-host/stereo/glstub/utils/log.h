// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Stand-in of Kodi's utils/log.h for autodepth_gl_test.cpp: each line is kept (and printed), its {} filled in order.
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

constexpr int LOGDEBUG = 0;
constexpr int LOGINFO = 1;
constexpr int LOGWARNING = 2;
constexpr int LOGERROR = 3;

class CLog
{
public:
  static std::vector<std::string>& Lines()
  {
    static std::vector<std::string> lines;
    return lines;
  }
  template<typename... Args>
  static void Log(int level, const char* format, Args&&... args)
  {
    std::ostringstream out;
    std::string rest{format};
    auto next = [&](const auto& value) {
      const size_t at = rest.find("{}");
      if (at == std::string::npos)
        return;
      out << rest.substr(0, at) << value;
      rest = rest.substr(at + 2);
    };
    (next(args), ...);
    out << rest;
    Lines().push_back(out.str());
    std::printf("log %d: %s\n", level, out.str().c_str());
  }
};
