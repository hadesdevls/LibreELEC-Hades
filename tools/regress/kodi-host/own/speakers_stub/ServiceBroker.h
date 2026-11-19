// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// The one thing of Kodi's service broker that cores/DataCacheCore.cpp and speakers_test.cpp use (the test defines it)
#pragma once

class CDataCacheCore;

class CServiceBroker
{
public:
  static CDataCacheCore& GetDataCacheCore();
};
