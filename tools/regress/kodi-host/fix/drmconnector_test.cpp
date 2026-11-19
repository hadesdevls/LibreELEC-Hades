/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): CDRMConnector::CheckConnector() and what it reads of a
 * connector, on the host, with the files of the tree (DRMConnector.cpp, DRMObject.cpp) and libdrm made of a
 * script here: each drmModeGetConnector() gives the next read of the script (connected or not, its modes, or
 * none: a read that fails), and drmModeFreeConnector() frees it. Built with AddressSanitizer: a read of a
 * connector that was freed stops the test.
 * Adopt() keeps what was read before (a mode of it, or a field, may still be read: the selected mode of
 * CDRMUtils points into the modes of the connector, GetConnectorId() gives a pointer into it). CheckConnector()
 * waits for a display that is not connected by reading the connector again: it used to free the read before
 * (m_connector.reset()), and to leave m_connector empty where a read failed.
 * And what was read before goes only by ForgetBefore() (built with -DHAVE_FORGET, where the tree has it): not
 * before a minute, never the 16 newest reads nor the one that the selected mode is a mode of ("bounded", which
 * waits a minute; the tree before kept every read until Kodi ended: nothing goes).
 * Usage: drmconnector_test kept|failed|connected|never|bounded
 */
#include "DRMConnector.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#ifdef HAVE_FORGET
#include "utils/RetiredKept.h"
#endif

using namespace KODI::WINDOWING::GBM;

namespace
{
struct Read
{
  bool fails = false;
  bool connected = true;
  int modes = 2;
  unsigned short width = 3840;
};
std::deque<Read> g_script;
int g_reads = 0, g_freed = 0, g_failures = 0;

void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}
} // namespace

int g_sleeps = 0; // (utils/XTimeUtils.h of the stubs)

// --- libdrm, as far as these files call it
extern "C"
{
drmModeConnectorPtr drmModeGetConnector(int, uint32_t connectorId)
{
  ++g_reads;
  Read r;
  if (!g_script.empty())
  {
    r = g_script.front();
    g_script.pop_front();
  }
  if (r.fails)
    return nullptr;
  auto* c = static_cast<drmModeConnectorPtr>(std::calloc(1, sizeof(drmModeConnector)));
  c->connector_id = connectorId;
  c->connector_type = DRM_MODE_CONNECTOR_HDMIA;
  c->connector_type_id = 1;
  c->connection = r.connected ? DRM_MODE_CONNECTED : DRM_MODE_DISCONNECTED;
  c->count_modes = r.modes;
  c->modes = static_cast<drmModeModeInfoPtr>(std::calloc(r.modes ? r.modes : 1, sizeof(drmModeModeInfo)));
  for (int i = 0; i < r.modes; ++i)
  {
    c->modes[i].hdisplay = r.width;
    c->modes[i].vdisplay = static_cast<uint16_t>(r.width * 9 / 16);
    c->modes[i].clock = static_cast<uint32_t>(297000 * (i + 1));
  }
  return c;
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
  p->count_props = 1;
  p->props = static_cast<uint32_t*>(std::calloc(1, sizeof(uint32_t)));
  p->prop_values = static_cast<uint64_t*>(std::calloc(1, sizeof(uint64_t)));
  p->props[0] = 5;
  p->prop_values[0] = 0;
  return p;
}
void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr p)
{
  if (!p)
    return;
  std::free(p->props);
  std::free(p->prop_values);
  std::free(p);
}
drmModePropertyPtr drmModeGetProperty(int, uint32_t propertyId)
{
  auto* p = static_cast<drmModePropertyPtr>(std::calloc(1, sizeof(drmModePropertyRes)));
  p->prop_id = propertyId;
  p->flags = DRM_MODE_PROP_BLOB;
  std::strcpy(p->name, "EDID");
  return p;
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

int main(int argc, char** argv)
{
  const std::string what = argc > 1 ? argv[1] : "";
  // the connector as Kodi read it when it started: connected, two modes
  CDRMConnector connector(3, 42);
  if (what == "connected")
  {
    const int reads = g_reads;
    Check(connector.CheckConnector(), "connected: CheckConnector() says so");
    Check(g_reads == reads && g_sleeps == 0 && g_freed == 0, "connected: nothing read again, no wait");
  }
  else if (what == "kept" || what == "failed" || what == "never")
  {
    // read again while nothing is connected (Refresh(): a hot-plug that took the display away, as
    // CWinSystemGbm::RefreshModes() and CDRMUtils::PlanOutputMove() read connectors): one mode, kept by
    // what reads it (the selected mode of CDRMUtils points into it, GetConnectorId() gives a pointer into it)
    g_script.push_back({false, false, 1, 1920});
    Check(connector.Refresh() && !connector.IsConnected(), "the display is gone: read again, not connected");
    const drmModeModeInfo* mode = connector.GetModeForIndex(0);
    const uint32_t* id = connector.GetConnectorId();
    const int freed = g_freed;
    if (what == "kept")
    {
      // the display comes back at the second read
      g_script.push_back({false, false, 1, 1920});
      g_script.push_back({false, true, 3, 3840});
      Check(connector.CheckConnector(), "kept: the display came back, CheckConnector() says so");
      Check(connector.IsConnected() && connector.GetModesCount() == 3 &&
                connector.GetModeForIndex(2)->hdisplay == 3840,
            "kept: the connector is the read in which it came back");
      Check(g_sleeps == 2, "kept: a second waited before each read again");
    }
    else if (what == "failed")
    {
      // every read again fails (drmModeGetConnector() gives nothing)
      for (int i = 0; i < 7; ++i)
        g_script.push_back({true});
      Check(!connector.CheckConnector(), "failed: reads that fail: CheckConnector() says it is not connected");
      Check(connector.GetConnectorId() && *connector.GetConnectorId() == 42 && !connector.IsConnected(),
            "failed: the connector is the one read before, whole");
    }
    else
    {
      for (int i = 0; i < 7; ++i)
        g_script.push_back({false, false, 1, 1920});
      Check(!connector.CheckConnector(), "never: seven reads, still not connected");
      Check(g_sleeps == 7, "never: seven waits");
    }
    // what was read before is kept, not freed: what points into it still reads it
    Check(g_freed == freed, "nothing that was read is freed (" + std::to_string(g_freed - freed) + " freed)");
    Check(mode->hdisplay == 1920 && *id == 42, "a mode and a field read before are still there");
  }
  else if (what == "bounded")
  {
    // read again 40 times (each connected, two modes), the selected mode one of the modes of the 4th read
    for (int i = 0; i < 40; ++i)
      g_script.push_back({false, true, 2, 3840});
    const drmModeModeInfo* selected = nullptr;
    for (int i = 0; i < 40; ++i)
    {
      if (i == 3)
        selected = connector.GetModeForIndex(1);
      connector.Refresh();
    }
#ifdef HAVE_FORGET
    connector.ForgetBefore({selected});
    Check(g_freed == 0, "bounded: right after, nothing goes (each was read again less than a minute ago)");
    std::this_thread::sleep_for(std::chrono::milliseconds(RETIRED_KEPT::KEPT_MS + 500));
    connector.ForgetBefore({selected});
#endif
    // 40 reads before (the first one and 39 of the 40 again): all but the 16 newest and the selected mode's
    Check(g_freed == 40 - 16 - 1, "bounded: a minute later, all but the 16 newest reads and the one of the "
                                  "selected mode went (" + std::to_string(g_freed) + " of 23)");
    Check(selected->hdisplay == 3840 && connector.GetModesCount() == 2,
          "bounded: the selected mode and the connector read now are still there");
  }
  else
  {
    std::printf("usage: drmconnector_test kept|failed|connected|never|bounded\n");
    return 2;
  }
  std::printf("%s %s (%d failed)\n", what.c_str(), g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures != 0;
}
