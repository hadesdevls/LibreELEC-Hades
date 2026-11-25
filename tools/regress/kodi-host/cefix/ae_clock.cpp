// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// The clock of utils/TimeUtils.h that AEUtil.cpp (AEDelayStatus) links with; the tests of cefix-ae do not read it
#include <cstdint>

int64_t CurrentHostCounter(void)
{
  return 0;
}

int64_t CurrentHostFrequency(void)
{
  return 1;
}
