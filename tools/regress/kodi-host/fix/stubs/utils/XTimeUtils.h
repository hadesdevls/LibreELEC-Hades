// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Kodi's sleep for the host tests of xbmc/windowing/gbm/drm: counted, not slept
#pragma once

#include <chrono>

extern int g_sleeps;

namespace KODI
{
namespace TIME
{
template<typename Rep, typename Period>
void Sleep(std::chrono::duration<Rep, Period>)
{
  ++g_sleeps;
}
} // namespace TIME
} // namespace KODI
