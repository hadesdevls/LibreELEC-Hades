// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host stand-in for Kodi's threads/CriticalSection.h: a recursive mutex (Kodi's CCriticalSection is one).
#pragma once

#include <mutex>

class CCriticalSection : public std::recursive_mutex
{
};
