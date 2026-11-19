/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): the callers of what lets go of the reads of the connector and
 * of the lists of resolutions (utils/RetiredKept.h), compiled and run on the host as the tree has them
 * (pieces_gen.py takes them out of it): CDRMUtils::SetMode() and CDRMUtils::TakeConnectorModes() with the real
 * CDRMConnector (DRMConnector.cpp, DRMObject.cpp) over the libdrm of a script, and
 * CDisplaySettings::ReplaceCustomResolutions(). Built with AddressSanitizer.
 *  - the selected mode stays a mode of the read it was selected in while reads after it come (the display took
 *    other modes and the mode on the screen is none of them: TakeConnectorModes() with -1), also once those reads
 *    are a minute old and more than sixteen: it is read after them;
 *  - once another mode is selected (SetMode()), that read goes with the others;
 *  - the lists of resolutions and their times stay side by side.
 * Waits a minute. Usage: pieces_test
 */
#include "DRMConnector.h"
#include "settings/ResolutionListReplace.h"
#include "utils/RetiredKept.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace KODI::WINDOWING::GBM;

int g_sleeps = 0; // (utils/XTimeUtils.h of the stubs)

namespace
{
int g_freed = 0, g_failures = 0, g_width = 3840;
void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}
drmModeConnectorPtr MakeRead(uint32_t id, int modes, unsigned short width)
{
  auto* c = static_cast<drmModeConnectorPtr>(std::calloc(1, sizeof(drmModeConnector)));
  c->connector_id = id;
  c->connector_type = DRM_MODE_CONNECTOR_HDMIA;
  c->connection = DRM_MODE_CONNECTED;
  c->count_modes = modes;
  c->modes = static_cast<drmModeModeInfoPtr>(std::calloc(modes, sizeof(drmModeModeInfo)));
  for (int i = 0; i < modes; ++i)
  {
    c->modes[i].hdisplay = width;
    c->modes[i].clock = static_cast<uint32_t>(297000 * (i + 1));
  }
  return c;
}
} // namespace

extern "C"
{
drmModeConnectorPtr drmModeGetConnector(int, uint32_t connectorId)
{
  return MakeRead(connectorId, 3, static_cast<unsigned short>(g_width));
}
void drmModeFreeConnector(drmModeConnectorPtr c)
{
  if (!c)
    return;
  ++g_freed;
  std::free(c->modes);
  std::free(c);
}
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int, uint32_t, uint32_t)
{
  auto* p = static_cast<drmModeObjectPropertiesPtr>(std::calloc(1, sizeof(drmModeObjectProperties)));
  return p;
}
void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr p)
{
  std::free(p);
}
drmModePropertyPtr drmModeGetProperty(int, uint32_t)
{
  return nullptr;
}
void drmModeFreeProperty(drmModePropertyPtr p)
{
  std::free(p);
}
int drmModeObjectSetProperty(int, uint32_t, uint32_t, uint32_t, uint64_t)
{
  return 0;
}
drmModePropertyBlobPtr drmModeGetPropertyBlob(int, uint32_t)
{
  return nullptr;
}
void drmModeFreePropertyBlob(drmModePropertyBlobPtr)
{
}
int drmModeAtomicCommit(int, drmModeAtomicReqPtr, uint32_t, void*)
{
  return 0;
}
int drmModeCreatePropertyBlob(int, const void*, size_t, uint32_t* id)
{
  *id = 1;
  return 0;
}
int drmModeDestroyPropertyBlob(int, uint32_t)
{
  return 0;
}
}

struct RESOLUTION_INFO
{
  int iWidth = 0, iHeight = 0;
  std::string strMode, strId;
};

// (CDRMUtils, as far as these two go)
class CDRMUtils
{
public:
  bool SetMode(const RESOLUTION_INFO& res)
  {
#include "setmode.inc"
  }
  void TakeConnectorModes(drmModeConnector* probed, int screenPlace)
  {
#include "take.inc"
  }
  CDRMConnector* m_connector = nullptr;
  drmModeModeInfo* m_mode = nullptr;
  int m_width = 0, m_height = 0;
};

constexpr std::size_t RES_DESKTOP = 16;
constexpr std::size_t RES_CUSTOM = 17;
RESOLUTION_INFO ListEntryOf(const RESOLUTION_INFO& r)
{
  return r;
}
// (CDisplaySettings, as far as this goes)
class CDisplaySettings
{
public:
  void ReplaceCustomResolutions(const std::vector<RESOLUTION_INFO>& resolutions, std::optional<size_t> desktop)
  {
#include "replace.inc"
  }
  void ApplyCalibrations() { ++m_calibrated; }
  std::mutex m_critical;
  std::vector<RESOLUTION_INFO> m_resolutions = std::vector<RESOLUTION_INFO>(RES_CUSTOM);
  std::vector<std::vector<RESOLUTION_INFO>> m_retired;
  std::vector<int64_t> m_retiredSince;
  int m_calibrated = 0;
};

int main()
{
  CDRMConnector connector(3, 42);
  CDRMUtils utils;
  utils.m_connector = &connector;
  // the mode on the screen, of the read of the start
  RESOLUTION_INFO res;
  res.iWidth = 3840;
  res.iHeight = 2160;
  res.strId = "1";
  Check(utils.SetMode(res) && utils.m_mode == connector.GetModeForIndex(1), "SetMode(): the mode selected");
  const drmModeModeInfo* selected = utils.m_mode;
  // the display comes with other modes 30 times, none of them the one on the screen
  for (int i = 0; i < 30; ++i)
    utils.TakeConnectorModes(MakeRead(42, 3, static_cast<unsigned short>(1000 + i)), -1);
  Check(utils.m_mode == selected && g_freed == 0, "right after: the selected mode stays, nothing goes");
  std::this_thread::sleep_for(std::chrono::milliseconds(RETIRED_KEPT::KEPT_MS + 500));
  utils.TakeConnectorModes(MakeRead(42, 3, 2000), -1);
  // 31 reads before: the 16 newest stay, and the start's, which the selected mode is a mode of
  Check(g_freed == 31 - 16 - 1, "a minute later: all but the 16 newest and the selected mode's went (" +
                                    std::to_string(g_freed) + " of 14)");
  Check(utils.m_mode == selected && utils.m_mode->hdisplay == 3840 && utils.m_mode->clock == 594000,
        "the selected mode is still read as it was");
  // another mode selected: the read of the start is held no more once the call that selected it is over
  res.strId = "0";
  int before = g_freed;
  Check(utils.SetMode(res) && utils.m_mode == connector.GetModeForIndex(0) && utils.m_mode->hdisplay == 2000,
        "SetMode(): another mode selected, of the read now");
  Check(g_freed == before, "... the read of the mode it had kept while it selects another one");
  // the display comes with other modes once more, the mode on the screen among them (place 0): the read of the
  // start goes, and the oldest of the 16 kept (more than a minute old, and no longer among the 16 newest)
  before = g_freed;
  utils.TakeConnectorModes(MakeRead(42, 3, 2000), 0);
  Check(g_freed == before + 2 && utils.m_mode == connector.GetModeForIndex(0) && utils.m_mode->hdisplay == 2000,
        "the next reading of the modes: the read of the start went, and the oldest one (" +
            std::to_string(g_freed - before) + " of 2)");

  CDisplaySettings ds;
  std::vector<RESOLUTION_INFO> resolutions(30);
  for (int i = 0; i < 30; ++i)
    resolutions[i].strMode = "a resolution named with more than fifteen characters " + std::to_string(i);
  ds.ReplaceCustomResolutions(resolutions, 0);
  const RESOLUTION_INFO& kept = ds.m_resolutions[RES_CUSTOM + 3];
  const std::string was = kept.strMode;
  for (int i = 0; i < 40; ++i)
    ds.ReplaceCustomResolutions(resolutions, 0);
  Check(ds.m_retired.size() == 41 && ds.m_retiredSince.size() == 41 && ds.m_calibrated == 41,
        "ReplaceCustomResolutions(): 41 lists retired in a moment, all kept, each with its time, calibrated");
  Check(kept.strMode == was, "a reference into the first list still reads it");
  std::printf("%s (%d failed)\n", g_failures ? "PIECES FAILED" : "PIECES PASSED", g_failures);
  return g_failures != 0;
}
