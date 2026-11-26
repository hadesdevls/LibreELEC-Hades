// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: a link that the kernel marks Bad (the connector property "link-status") set up again by a commit
// that sets it Good and nothing else (drm/LinkStatus.h: CRetrain, LookAt(), Changes(), CarriesGood()).
//
// The kernel is a model of what Linux 7.4 does with an atomic commit (OUTPUT_STATE::IKms, as CLibdrmKms gives it to
// Kodi: a blocking commit with a modeset allowed): the state of every object duplicated, the changes of the request
// applied, link-status only ever moved to Good by user space (drm_atomic_uapi.c: a request for Bad is a no-op) and
// only to an enum value, a change of link-status a full modeset (drm_atomic_helper_check_modeset():
// connectors_changed), and, as i915 does after a link training that fails, Bad set again with a hot-plug event after
// the modeset (intel_connector_modeset_retry_work_fn()). The frames are driven as CWinSystemGbm::LookAtLinkStatus()
// drives them: Due() first, a change of mode of Kodi's a new budget and one look AFTER_MODESET_MS later.
//
// What is checked: Good and absent (most outputs) give no commit and no write, and are read only after an event;
// Bad gives exactly one commit of exactly one change, after which every property of the connector, the CRTC and the
// plane (the mode of a frame-packed 3D mode, HDR_OUTPUT_METADATA, the Colorspace, the depth, the Dolby Vision
// transport and its InfoFrame, VRR, the LUT and the CTM, the framebuffer) is as before, blobs by their bytes, but
// link-status; a link that stays Bad gets ATTEMPTS commits at the times of the backoff, then none (GIVE_UP once),
// until a change of mode of Kodi's; refusals are bounded the same way; nothing while the output is off, not Kodi's,
// or a change of mode of Kodi's is pending; a connector gone is left alone.
//
// g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I<kodi>/xbmc/windowing/gbm linkbad_test.cpp
#include "drm/LinkStatus.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM;
using namespace KODI::WINDOWING::GBM::LINK_STATUS;
using OUTPUT_STATE::Change;
using OUTPUT_STATE::Role;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

// objects and properties of the model (the ids are what a request names)
constexpr uint32_t CONN = 50, CRTC = 60, PLANE = 70;
constexpr uint32_t P_CRTC_ID = 1, P_LINK = 2, P_HDR = 3, P_COLORSPACE = 4, P_MAX_BPC = 5, P_DV_LAB = 6,
                   P_DV_VSIF = 7, P_CONTENT_TYPE = 8, P_RGB = 9;
constexpr uint32_t P_ACTIVE = 20, P_MODE_ID = 21, P_VRR = 22, P_GAMMA = 23, P_CTM = 24;
constexpr uint32_t P_FB_ID = 30, P_PLANE_CRTC = 31, P_ROTATION = 32, P_SRC_W = 33, P_SRC_H = 34;
constexpr uint64_t GOOD = 0, BAD = 1;

const std::map<uint32_t, const char*> NAMES = {
    {P_CRTC_ID, "CRTC_ID"}, {P_LINK, "link-status"}, {P_HDR, "HDR_OUTPUT_METADATA"}, {P_COLORSPACE, "Colorspace"},
    {P_MAX_BPC, "max bpc"}, {P_DV_LAB, "DVBRIDGE_DV_STANDARD_LAB"}, {P_DV_VSIF, "NV_HDMI_VSIF_METADATA"},
    {P_CONTENT_TYPE, "content type"}, {P_RGB, "Broadcast RGB"}, {P_ACTIVE, "ACTIVE"}, {P_MODE_ID, "MODE_ID"},
    {P_VRR, "VRR_ENABLED"}, {P_GAMMA, "GAMMA_LUT"}, {P_CTM, "CTM"}, {P_FB_ID, "FB_ID"},
    {P_PLANE_CRTC, "CRTC_ID (plane)"}, {P_ROTATION, "rotation"}, {P_SRC_W, "SRC_W"}, {P_SRC_H, "SRC_H"}};
const uint32_t BLOBS[] = {P_HDR, P_DV_VSIF, P_MODE_ID, P_GAMMA, P_CTM};

bool IsBlob(uint32_t p)
{
  for (uint32_t b : BLOBS)
    if (b == p)
      return true;
  return false;
}

using State = std::map<uint32_t, std::map<uint32_t, uint64_t>>;

//! The kernel: what an atomic commit does with the state of the output
class CModel : public OUTPUT_STATE::IKms
{
public:
  CModel()
  {
    // the bytes of each blob: a frame-packed 1080p24 mode (drm_mode_modeinfo with DRM_MODE_FLAG_3D_FRAME_PACKING,
    // 1 << 14, in its flags), HDR10 metadata (hdr_output_metadata: ST 2084, BT.2020 primaries, 1000 nits), the
    // Dolby vendor InfoFrame (0x81, 27 bytes, OUI 00-D0-46), a LUT and a CTM
    m_blobs[101] = Bytes(68, 0x24);
    m_blobs[101][20] = 0x00;
    m_blobs[101][21] = 0x40; // flags: 3D frame packing
    m_blobs[102] = Bytes(30, 0x11);
    m_blobs[102][0] = 0;
    m_blobs[102][1] = 2; // EOTF: SMPTE ST 2084
    m_blobs[103] = {0x81, 0x01, 0x1b, 0x46, 0xd0, 0x00};
    for (uint8_t i = 0; m_blobs[103].size() < 27; ++i)
      m_blobs[103].push_back(static_cast<uint8_t>(0x5a ^ i));
    m_blobs[104] = Bytes(256 * 8, 0x37);
    m_blobs[105] = Bytes(9 * 8, 0x42);
    m_state[CONN] = {{P_CRTC_ID, CRTC}, {P_LINK, GOOD},          {P_HDR, 102},         {P_COLORSPACE, 9},
                     {P_MAX_BPC, 12},   {P_DV_LAB, 1},           {P_DV_VSIF, 103},     {P_CONTENT_TYPE, 4},
                     {P_RGB, 1}};
    m_state[CRTC] = {{P_ACTIVE, 1}, {P_MODE_ID, 101}, {P_VRR, 1}, {P_GAMMA, 104}, {P_CTM, 105}};
    m_state[PLANE] = {{P_FB_ID, 77}, {P_PLANE_CRTC, CRTC}, {P_ROTATION, 1}, {P_SRC_W, 1920u << 16},
                      {P_SRC_H, 2205u << 16}};
  }

  std::optional<std::map<uint32_t, uint64_t>> Values(Role, uint32_t object) override
  {
    ++reads;
    if (gone || !m_state.count(object))
      return std::nullopt;
    return m_state[object];
  }
  std::optional<std::vector<uint8_t>> BlobData(uint32_t blob) override
  {
    const auto it = m_blobs.find(blob);
    return it == m_blobs.end() ? std::nullopt : std::optional<std::vector<uint8_t>>(it->second);
  }
  uint32_t CreateBlob(const std::vector<uint8_t>&) override
  {
    ++writes; // (never asked by a retraining)
    return 0;
  }
  void DestroyBlob(uint32_t) override { ++writes; }
  int Commit(const std::vector<Change>& changes, bool testOnly) override
  {
    if (!testOnly)
    {
      ++commits;
      requests.push_back(changes);
    }
    if (gone)
      return ENOENT;
    if (refuse)
      return refuse;
    State next = m_state; // (atomic_duplicate_state() of what the request names, its CRTC and planes)
    for (const auto& c : changes)
    {
      if (!next.count(c.object) || !next[c.object].count(c.property))
        return EINVAL;
      if (c.property == P_LINK)
      {
        if (c.value != GOOD && c.value != BAD)
          return EINVAL; // (not a value of the enum)
        // drm_atomic_connector_set_property(): never Good to Bad on user space's request
        if (next[c.object][P_LINK] != GOOD)
          next[c.object][P_LINK] = c.value;
      }
      else
        next[c.object][c.property] = c.value;
    }
    const bool modeset = next[CONN][P_LINK] != m_state[CONN][P_LINK] ||
                         next[CRTC][P_MODE_ID] != m_state[CRTC][P_MODE_ID] ||
                         next[CRTC][P_ACTIVE] != m_state[CRTC][P_ACTIVE] ||
                         next[CONN][P_CRTC_ID] != m_state[CONN][P_CRTC_ID];
    if (testOnly)
      return 0;
    m_state = next;
    if (modeset)
    {
      ++modesets;
      // i915: the training of the modeset failed, Bad set by its worker with a hot-plug event
      if (trainingFails > 0)
      {
        --trainingFails;
        MarkBad();
      }
    }
    return 0;
  }

  //! What the driver does where a link training failed: Bad, and a hot-plug event
  void MarkBad()
  {
    m_state[CONN][P_LINK] = BAD;
    ++uevents;
  }
  uint64_t Link() const { return m_state.at(CONN).at(P_LINK); }
  //! The whole output: every property of the three objects, a blob by its bytes
  std::map<std::string, std::vector<uint8_t>> Snapshot() const
  {
    std::map<std::string, std::vector<uint8_t>> out;
    for (const auto& [object, props] : m_state)
      for (const auto& [p, v] : props)
      {
        const std::string key = std::to_string(object) + "/" + NAMES.at(p);
        std::vector<uint8_t> bytes;
        for (int i = 0; i < 8; ++i)
          bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
        if (IsBlob(p) && m_blobs.count(v))
          bytes.insert(bytes.end(), m_blobs.at(v).begin(), m_blobs.at(v).end());
        out[key] = bytes;
      }
    return out;
  }

  int reads = 0, commits = 0, writes = 0, modesets = 0, trainingFails = 0, refuse = 0;
  unsigned int uevents = 0;
  bool gone = false;
  std::vector<std::vector<Change>> requests;

private:
  static std::vector<uint8_t> Bytes(size_t n, uint8_t seed)
  {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i)
      b[i] = static_cast<uint8_t>(seed + i * 7);
    return b;
  }
  State m_state;
  std::map<uint32_t, std::vector<uint8_t>> m_blobs;
};

const char* Name(Verdict v)
{
  switch (v)
  {
    case Verdict::NOTHING:
      return "NOTHING";
    case Verdict::ABSENT:
      return "ABSENT";
    case Verdict::GONE:
      return "GONE";
    case Verdict::OFF:
      return "OFF";
    case Verdict::KODI_MODESET:
      return "KODI_MODESET";
    case Verdict::COMMIT:
      return "COMMIT";
    case Verdict::WAIT:
      return "WAIT";
    case Verdict::STILL_BAD:
      return "STILL_BAD";
    case Verdict::GOOD_AGAIN:
      return "GOOD_AGAIN";
    case Verdict::GIVE_UP:
      return "GIVE_UP";
    default:
      return "STOPPED";
  }
}

//! Frames of 16 ms, each as CWinSystemGbm::LookAtLinkStatus() takes it
struct Sim
{
  CModel k;
  CRetrain r;
  std::optional<Values> values = Values{GOOD, BAD};
  uint32_t property = P_LINK;
  Output out{true, true, false};
  int64_t now = 0;
  unsigned int seenEvents = 0;
  int64_t kodiModesetAt = -1, modesetSeen = -1, modesetLook = -1;
  std::vector<std::pair<int64_t, Verdict>> said; // every verdict but NOTHING
  std::vector<int64_t> commitTimes;

  void Frame()
  {
    if (!values || !property)
      return;
    if (kodiModesetAt >= 0 && kodiModesetAt != modesetSeen)
    {
      modesetSeen = kodiModesetAt;
      r.KodiModeset();
      modesetLook = kodiModesetAt + AFTER_MODESET_MS;
    }
    const bool event = k.uevents != seenEvents;
    const bool after = modesetLook >= 0 && now >= modesetLook;
    if (!r.Due(now, event, after))
      return;
    seenEvents = k.uevents;
    if (after)
      modesetLook = -1;
    const Look look = LookAt(r, k, CONN, property, values, [this]() { return out; }, now);
    if (look.verdict != Verdict::NOTHING)
      said.emplace_back(now, look.verdict);
    if (look.verdict == Verdict::COMMIT)
      commitTimes.push_back(now);
  }
  void RunTo(int64_t until)
  {
    while (now < until)
    {
      Frame();
      now += 16;
    }
  }
  int Count(Verdict v) const
  {
    int n = 0;
    for (const auto& s : said)
      n += s.second == v;
    return n;
  }
  std::string Said() const
  {
    std::string s;
    for (const auto& [t, v] : said)
      s += std::to_string(t) + ":" + Name(v) + " ";
    return s;
  }
};

// Good, and no such property: no commit, no write, a read only for an event
void GoodAndAbsent()
{
  Sim s;
  for (int i = 1; i <= 20; ++i)
  {
    s.RunTo(i * 30000);
    ++s.k.uevents; // (a hot-plug event of the device: the TV sends one after each change of mode)
  }
  s.RunTo(20 * 30000 + 1000);
  Check(s.k.commits == 0, "Good: no commit in 10 min with 20 events (" + std::to_string(s.k.commits) + ")");
  Check(s.k.writes == 0, "Good: no blob made or destroyed");
  Check(s.k.reads == 20, "Good: one read for each event and none for the frames between (" +
                             std::to_string(s.k.reads) + " reads, 20 events)");
  Check(s.said.empty(), "Good: nothing said: " + s.Said());

  Sim a;
  a.values.reset(); // (no link-status on the connector, or not with Good and Bad)
  a.k.MarkBad();
  for (int i = 1; i <= 5; ++i)
  {
    a.RunTo(i * 10000);
    ++a.k.uevents;
  }
  Check(a.k.commits == 0 && a.k.reads == 0, "absent: no read and no commit, even with the value Bad (" +
                                               std::to_string(a.k.reads) + " reads, " +
                                               std::to_string(a.k.commits) + " commits)");
  Sim z;
  z.property = 0;
  z.k.MarkBad();
  z.RunTo(10000);
  Check(z.k.commits == 0 && z.k.reads == 0, "no property id: no read and no commit");
  // nothing due: no read at all
  CRetrain idle;
  Check(!idle.Due(123456, false, false), "Due() without an event, a change of mode or a look asked for: false");
}

// Bad once: exactly one commit of exactly link-status Good, the whole output as it was
void BadOnce()
{
  Sim s;
  s.RunTo(5000);
  const auto before = s.k.Snapshot();
  const int modesetsBefore = s.k.modesets;
  s.k.MarkBad();
  s.RunTo(5100);
  Check(s.k.commits == 1, "Bad: one commit at once (" + std::to_string(s.k.commits) + ")");
  Check(s.commitTimes.size() == 1 && s.commitTimes[0] <= 5016, "Bad: the commit with the next frame");
  if (!s.k.requests.empty())
  {
    const auto& req = s.k.requests[0];
    Check(req.size() == 1, "the commit carries one change (" + std::to_string(req.size()) + ")");
    if (!req.empty())
      Check(req[0].role == Role::CONNECTOR && req[0].object == CONN && req[0].property == P_LINK &&
                req[0].value == GOOD && req[0].name == "link-status",
            "the change is link-status Good on the connector Kodi drives");
  }
  Check(s.k.modesets == modesetsBefore + 1, "the commit is a modeset (connectors_changed)");
  auto after = s.k.Snapshot();
  const std::string link = std::to_string(CONN) + "/link-status";
  Check(after[link] == before.at(link), "link-status is Good after it, as before the failure");
  int differ = 0;
  for (const auto& [key, bytes] : before)
    if (after[key] != bytes)
    {
      ++differ;
      std::printf("FAIL: %s is not as before after the commit\n", key.c_str());
    }
  Check(differ == 0 && after.size() == before.size(),
        "every property of the connector, the CRTC and the plane as before, blobs byte for byte: the frame-packed "
        "mode, HDR_OUTPUT_METADATA, Colorspace, max bpc, the Dolby Vision transport and InfoFrame, VRR, the LUT, "
        "the CTM, the framebuffer");
  s.RunTo(7000);
  Check(s.Count(Verdict::GOOD_AGAIN) == 1, "Good again told once: " + s.Said());
  Check(!s.said.empty() && s.said.back().first >= 5000 + VERIFY_MS && s.said.back().first < 5000 + VERIFY_MS + 40,
        "Good again read VERIFY_MS after the commit: " + s.Said());
  for (int i = 1; i <= 20; ++i)
  {
    s.RunTo(7000 + i * 30000);
    ++s.k.uevents;
  }
  s.RunTo(7000 + 21 * 30000);
  Check(s.k.commits == 1, "Bad once: one commit in all in 10 min after it (" + std::to_string(s.k.commits) + ")");
}

// A link that stays Bad: ATTEMPTS commits at the backoff, then none until a change of mode of Kodi's
void StaysBad()
{
  Sim s;
  s.k.trainingFails = 1000;
  s.RunTo(1000);
  s.k.MarkBad();
  s.RunTo(30000);
  const std::string said = s.Said();
  Check(s.k.commits == static_cast<int>(ATTEMPTS),
        "stays Bad: " + std::to_string(ATTEMPTS) + " commits (" + std::to_string(s.k.commits) + "): " + said);
  if (s.commitTimes.size() == 3)
  {
    Check(s.commitTimes[0] < 1016, "the first at once");
    const int64_t gap1 = s.commitTimes[1] - s.commitTimes[0], gap2 = s.commitTimes[2] - s.commitTimes[1];
    Check(gap1 >= BACKOFF_MS[0] && gap1 < BACKOFF_MS[0] + 40,
          "the second BACKOFF_MS[0] after it (" + std::to_string(gap1) + " ms)");
    Check(gap2 >= BACKOFF_MS[1] && gap2 < BACKOFF_MS[1] + 40,
          "the third BACKOFF_MS[1] after it (" + std::to_string(gap2) + " ms)");
  }
  Check(s.Count(Verdict::STILL_BAD) == 2, "Bad again told after the first two: " + said);
  Check(s.Count(Verdict::GIVE_UP) == 1, "given up once: " + said);
  Check(s.r.Stopped(), "stopped");
  // events keep coming: nothing more
  for (int i = 1; i <= 120; ++i)
  {
    s.RunTo(30000 + i * 5000);
    ++s.k.uevents;
  }
  s.RunTo(30000 + 121 * 5000);
  Check(s.k.commits == static_cast<int>(ATTEMPTS), "after it, 10 min of events: no commit (" +
                                                        std::to_string(s.k.commits) + ")");
  Check(s.Count(Verdict::GIVE_UP) == 1, "given up said once only");
  // a change of mode of Kodi's (its commit sets the link Good, and the training fails again): a new budget
  const int before = s.k.commits;
  s.k.MarkBad();
  s.kodiModesetAt = s.now;
  s.RunTo(s.now + 30000);
  Check(s.k.commits == before + static_cast<int>(ATTEMPTS),
        "after a change of mode of Kodi's, the budget again (" + std::to_string(s.k.commits - before) + ")");
}

// The driver refuses the commit: bounded the same way
void Refused()
{
  Sim s;
  s.k.refuse = EINVAL;
  s.k.MarkBad();
  s.RunTo(60000);
  Check(s.k.commits == static_cast<int>(ATTEMPTS),
        "refused: " + std::to_string(ATTEMPTS) + " commits (" + std::to_string(s.k.commits) + "): " + s.Said());
  if (s.commitTimes.size() == 3)
    Check(s.commitTimes[1] - s.commitTimes[0] >= BACKOFF_MS[0] && s.commitTimes[2] - s.commitTimes[1] >= BACKOFF_MS[1],
          "refused: at the backoff");
  Check(s.Count(Verdict::GIVE_UP) == 1, "refused: given up once: " + s.Said());
  Check(s.k.Link() == BAD, "refused: the link stays as the kernel has it");
}

// Not Kodi's to do now
void NotNow()
{
  for (int c = 0; c < 3; ++c)
  {
    Sim s;
    if (c == 0)
      s.out.on = false;
    else if (c == 1)
      s.out.routed = false;
    else
      s.out.kodiModesetPending = true;
    s.k.MarkBad();
    for (int i = 1; i <= 10; ++i)
    {
      s.RunTo(i * 5000);
      ++s.k.uevents;
    }
    const char* what = c == 0 ? "output off" : c == 1 ? "connector not on Kodi's CRTC" : "change of mode pending";
    Check(s.k.commits == 0, std::string(what) + ": no commit (" + std::to_string(s.k.commits) + ")");
    Check(s.Count(c == 2 ? Verdict::KODI_MODESET : Verdict::OFF) >= 1, std::string(what) + ": " + s.Said());
    // then the output is Kodi's again: one commit with the next event
    s.out = Output{true, true, false};
    ++s.k.uevents;
    s.RunTo(s.now + 100);
    Check(s.k.commits == 1, std::string(what) + ", then on: one commit (" + std::to_string(s.k.commits) + ")");
  }
}

// The connector is gone
void Gone()
{
  Sim s;
  s.k.MarkBad();
  s.k.gone = true;
  for (int i = 1; i <= 5; ++i)
  {
    s.RunTo(i * 5000);
    ++s.k.uevents;
  }
  Check(s.k.commits == 0, "gone: no commit");
  Check(s.Count(Verdict::GONE) >= 1, "gone: said (" + s.Said() + ")");
  Check(s.r.Attempts() == 0, "gone: nothing spent");
}

// A link that fails again soon after it was Good takes from the same budget; long after, the whole one
void Flaps()
{
  Sim s;
  s.k.MarkBad();
  s.RunTo(3000); // committed at 0, Good again at 1 s
  s.k.MarkBad();
  s.RunTo(5000);
  Check(s.k.commits == 2 && s.r.Attempts() == 2, "Bad 3 s after Good again: the second attempt of the same "
                                                   "budget (" + std::to_string(s.r.Attempts()) + ")");
  s.RunTo(6000);
  s.k.MarkBad();
  s.RunTo(20000);
  s.k.MarkBad();
  s.RunTo(30000);
  Check(s.k.commits == static_cast<int>(ATTEMPTS), "a link that keeps failing: " + std::to_string(ATTEMPTS) +
                                                       " commits in all (" + std::to_string(s.k.commits) + ")");
  Check(s.Count(Verdict::GIVE_UP) == 1, "a link that keeps failing: given up: " + s.Said());

  Sim c;
  c.k.MarkBad();
  c.RunTo(3000);
  c.RunTo(3000 + CALM_MS);
  c.k.MarkBad();
  c.RunTo(c.now + 3000);
  Check(c.k.commits == 2 && c.r.Attempts() == 1,
        "Bad CALM_MS after the last commit: the first attempt of a new budget (" + std::to_string(c.r.Attempts()) +
            ")");
}

// The change of mode of Kodi's: Good carried only where the link is Bad; the values as the kernel names them
void Values_()
{
  Check(CarriesGood(Values{GOOD, BAD}, BAD), "a change of mode carries Good where the link is Bad");
  Check(!CarriesGood(Values{GOOD, BAD}, GOOD), "... not where it is Good");
  Check(!CarriesGood(std::nullopt, BAD), "... not where the connector has no such property");
  Check(!CarriesGood(Values{GOOD, BAD}, std::nullopt), "... not where it cannot be read");
  OUTPUT_STATE::PropertyInfo p;
  p.name = "link-status";
  p.kind = OUTPUT_STATE::Kind::ENUM;
  p.enums = {{"Good", 0}, {"Bad", 1}};
  const auto v = ValuesOf(p);
  Check(v && v->good == 0 && v->bad == 1, "the values of the kernel's enum (drm_link_status_enum_list)");
  p.enums = {{"Good", 0}};
  Check(!ValuesOf(p), "an enum without Bad: nothing");
  p.enums = {{"Good", 0}, {"Bad", 1}};
  p.name = "link status";
  Check(!ValuesOf(p), "another property: nothing");
  Check(LinkOf(Values{0, 1}, 7) == Link::GOOD, "a value that is not Bad is not taken as Bad");
  Check(LinkOf(std::nullopt, 1) == Link::ABSENT, "no values: absent");
  Check(LinkOf(Values{0, 1}, std::nullopt) == Link::UNREADABLE, "not read: unreadable");
}
} // namespace

int main()
{
  GoodAndAbsent();
  BadOnce();
  StaysBad();
  Refused();
  NotNow();
  Gone();
  Flaps();
  Values_();
  std::printf("LINKBAD-TEST: %d checks, %d failed\n", g_checks, g_failures);
  if (g_failures == 0)
    std::printf("LINKBAD-TEST PASSED\n");
  return g_failures == 0 ? 0 : 1;
}
