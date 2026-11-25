// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// What the code of FontUtils.cpp that font_gen.py takes needs of Kodi: its log (nothing is written) and the two
// conversions of the charset converter it calls, for the names of the fonts of the test (Latin letters)
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
};

#define LogF(level, format, ...) Log((level), ("{}: " format), __FUNCTION__, ##__VA_ARGS__)

class CCharsetConverter
{
public:
  //! UTF-16BE of the basic plane, as UTF-8
  static bool utf16BEtoUTF8(const std::string& in, std::string& out)
  {
    out.clear();
    if (in.size() % 2)
      return false;
    for (size_t i = 0; i < in.size(); i += 2)
    {
      const unsigned int c = (static_cast<unsigned char>(in[i]) << 8) | static_cast<unsigned char>(in[i + 1]);
      if (c >= 0xD800 && c < 0xE000)
        return false;
      if (c < 0x80)
        out += static_cast<char>(c);
      else if (c < 0x800)
      {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
      }
      else
      {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
      }
    }
    return true;
  }
  //! Mac Roman, of its ASCII half
  static bool MacintoshToUTF8(const std::string& in, std::string& out)
  {
    for (const char ch : in)
      if (static_cast<unsigned char>(ch) >= 0x80)
        return false;
    out = in;
    return true;
  }
};
