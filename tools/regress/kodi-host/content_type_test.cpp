/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the content type of the AVI InfoFrame ("Content type", videoscreen.hadescontenttype;
 * windowing/gbm/ContentType.h) and of its place in the transport of native Dolby Vision (drm/TransportSpecs.h):
 *  - DeclaredOf(): the CNC bits of the HDMI VSDB of real EDIDs (an LG C5 through a TCL bar: Cinema and Game; a Sony:
 *    no Cinema; a Samsung S90H: Game only; a DisplayPort monitor: none) and of broken ones (short, cut, no VSDB);
 *  - Wanted(): for each choice, situation, declaration, value of the kernel and value of Kodi's own, the value the
 *    property is to have: Cinema for a video, Game for a game, each only where the display declares it; "No Data"
 *    for the menus; As LibreELEC takes back only Kodi's own value;
 *  - Carried(): only in a commit that allows a mode set and changes the mode, never with a mode set of its own, never
 *    in the request of the transport, never while native Dolby Vision holds the output;
 *  - ForTransport(): "No Data" for Dolby Vision that the TV processes, Cinema for Low-Latency and HDR over the tunnel;
 *  - AviOf(): ITC and CN of the AVI InfoFrame that each driver makes of each value, with and without Dolby Vision,
 *    and the bytes PB3 and PB5 that they are (the AVI InfoFrame of "As LibreELEC" is each driver's own);
 *  - TransportSpecs(): the transport is the one of before but for the one property "content type", for either driver.
 * With -DOLD_RULE the rule before (Kodi never set the property) is put in Wanted()'s place: it must fail.
 */
#include "windowing/gbm/ContentType.h"
#include "windowing/gbm/drm/TransportSpecs.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM::CONTENT_TYPE;
namespace OS = KODI::WINDOWING::GBM::OUTPUT_STATE;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static std::vector<uint8_t> Hex(const char* hex)
{
  std::vector<uint8_t> out;
  for (const char* p = hex; p[0] && p[1]; p += 2)
    out.push_back(static_cast<uint8_t>(std::stoi(std::string(p, 2), nullptr, 16)));
  return out;
}

// The EDID of the LG C5 as the TCL Q85H Pro repeats it (read by a Homatics R 4K Plus, 2026-11-24): its HDMI VSDB
// has byte 8 0x2c (CNC2 Cinema, CNC3 Game; no latency fields)
static const char* C5 =
    "00ffffffffffff001e6dcd83010101010123010380a05a780aee91a3544c99260f5054a1080031404540614071408180d1c0010101"
    "0108e80030f2705a80b0588a0040846300001e6fc200a0a0a055503020350040846300001e000000fd0018781eff77000a20202020"
    "2020000000fc004c472054562053534352320a200199020376f15d616076756665dbda101f04130514030212202122155d5e5f6263"
    "643f4038097f070f7f076754075704031507503d07c05f7e035f7e01836f0f0c6e030c002100b8442c0080010203046dd85dc4017880"
    "5b022878c33305e200cfe305c000e3060d01e20fffeb0146d0002a2c03b9865d6c0000000000000000005a";

// An EDID of two blocks with one CTA data block of the HDMI VSDB whose byte 8 is cnc (length vsdbLength)
static std::vector<uint8_t> WithVsdb(uint8_t cnc, uint8_t vsdbLength)
{
  std::vector<uint8_t> e(256, 0);
  const uint8_t head[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
  std::copy(head, head + 8, e.begin());
  e[126] = 1;
  uint8_t* c = e.data() + 128;
  c[0] = 0x02;
  c[1] = 0x03;
  const uint8_t vsdb[] = {static_cast<uint8_t>(0x60 | vsdbLength), 0x03, 0x0C, 0x00, 0x10, 0x00, 0x78, 0x00, cnc};
  std::copy(vsdb, vsdb + 1 + vsdbLength, c + 4);
  c[2] = static_cast<uint8_t>(4 + 1 + vsdbLength);
  return e;
}

#ifdef OLD_RULE
// Kodi before: it never set the property
static std::optional<Type> Want(Choice, Situation, const Declared&, Type, std::optional<Type>)
{
  return std::nullopt;
}
#else
static std::optional<Type> Want(Choice c, Situation s, const Declared& d, Type k, std::optional<Type> own)
{
  return Wanted(c, s, d, k, own);
}
#endif

static std::string Name(std::optional<Type> t)
{
  return t ? EnumName(*t) : "nothing";
}

int main()
{
  // ---- the declaration of the display
  const Declared c5 = DeclaredOf(Hex(C5));
  Check(c5.cinema && c5.game && !c5.graphics && !c5.photo, "LG C5 (through the TCL bar): Cinema and Game");
  const Declared sony = DeclaredOf(WithVsdb(0x2b, 8)); // a SONY TV of the reports: CNC0, CNC1, CNC3
  Check(!sony.cinema && sony.game && sony.graphics && sony.photo, "Sony 0x2b: no Cinema");
  const Declared s90h = DeclaredOf(WithVsdb(0x28, 8)); // the Samsung S90H: CNC3 only
  Check(!s90h.cinema && s90h.game, "Samsung S90H 0x28: Game only");
  const Declared shortVsdb = DeclaredOf(WithVsdb(0x2c, 7)); // no byte 8
  Check(!shortVsdb.cinema && !shortVsdb.game, "an HDMI VSDB without byte 8 declares nothing");
  std::vector<uint8_t> dp = WithVsdb(0x2c, 8);
  dp[128 + 4 + 1] = 0x04; // another OUI: no HDMI VSDB
  Check(!DeclaredOf(dp).cinema, "a vendor block of another OUI declares nothing");
  std::vector<uint8_t> cut = Hex(C5);
  cut.resize(200);
  Check(!DeclaredOf(cut).cinema, "an EDID cut in its extension declares nothing (and is not read past its end)");
  std::vector<uint8_t> bad = WithVsdb(0x2c, 8);
  bad[128 + 2] = 200; // an offset of the detailed timings past the block
  Check(!DeclaredOf(bad).cinema, "a broken offset of the detailed timings: nothing read past the block");
  Check(!DeclaredOf({}).cinema, "no EDID: nothing");

  // ---- the value of the property
  const Choice A = Choice::AUTOMATIC, L = Choice::AS_LIBREELEC;
  const Type N = Type::NO_DATA, C = Type::CINEMA, G = Type::GAME;
  struct Case
  {
    Choice choice;
    Situation situation;
    Declared declared;
    Type kernel;
    std::optional<Type> own;
    std::optional<Type> want;
    const char* what;
  };
  const Declared none{};
  const Case cases[] = {
      {A, Situation::VIDEO, c5, N, {}, C, "Automatic: a video on the C5 goes to Cinema"},
      {A, Situation::VIDEO, c5, C, C, {}, "Automatic: a video with Cinema already: nothing"},
      {A, Situation::GAME, c5, N, {}, G, "Automatic: a game on the C5 goes to Game"},
      {A, Situation::MENUS, c5, C, C, N, "Automatic: the menus after a film go back to No Data"},
      {A, Situation::MENUS, c5, N, {}, {}, "Automatic: the menus with No Data: nothing (the driver's own value)"},
      {A, Situation::VIDEO, sony, N, {}, {}, "Automatic: a video on a TV without Cinema: nothing"},
      {A, Situation::GAME, sony, N, {}, G, "Automatic: a game on that TV, which declares Game: Game"},
      {A, Situation::VIDEO, none, N, {}, {}, "Automatic: a display without a VSDB: nothing"},
      {A, Situation::GAME, none, N, {}, {}, "Automatic: a game on a display without a VSDB: nothing"},
      {A, Situation::VIDEO, sony, C, C, N, "Automatic: a video on a TV without Cinema where Kodi left Cinema: No Data"},
      {A, Situation::DV_TV_LED, c5, C, C, N, "Automatic: Dolby Vision that the TV processes: No Data"},
      {A, Situation::MENUS, c5, Type::GRAPHICS, {}, N, "Automatic: another program's Graphics in the menus: No Data"},
      {L, Situation::VIDEO, c5, N, {}, {}, "As LibreELEC: a video: nothing"},
      {L, Situation::MENUS, c5, C, C, N, "As LibreELEC: Kodi's own Cinema goes back to No Data"},
      {L, Situation::VIDEO, c5, C, C, N, "As LibreELEC: Kodi's own Cinema goes back to No Data during a video too"},
      {L, Situation::MENUS, c5, C, {}, {}, "As LibreELEC: a Cinema that another program set stays"},
      {L, Situation::MENUS, c5, G, C, {}, "As LibreELEC: a Game that is not Kodi's own value stays"},
  };
  for (const auto& c : cases)
  {
    const auto got = Want(c.choice, c.situation, c.declared, c.kernel, c.own);
    Check(got == c.want, std::string(c.what) + " (" + Name(got) + ", wanted " + Name(c.want) + ")");
  }

  // ---- when: never a mode set of its own
  Check(Carried(true, true, false, false), "a commit that allows a mode set and changes the mode carries it");
  Check(!Carried(true, false, false, false), "a mode set to the same timing does not (it would be one of its own)");
  Check(!Carried(false, true, false, false), "a commit that does not allow a mode set does not");
  Check(!Carried(true, true, true, false), "a request that has it already (the transport, its end) does not");
  Check(!Carried(true, true, false, true), "native Dolby Vision decides its own");

  // ---- the transport of native Dolby Vision
  Check(ForTransport(A, false, false, c5, C, C) == N, "transport, Standard (TV-led): No Data");
  Check(ForTransport(A, true, false, c5, N, {}) == C, "transport, Low-Latency on the C5: Cinema");
  Check(ForTransport(A, true, false, sony, N, {}) == N, "transport, Low-Latency on a TV without Cinema: No Data");
  Check(ForTransport(A, false, true, c5, N, {}) == C, "transport, HDR over the tunnel: Cinema");
  Check(ForTransport(L, false, false, c5, N, {}) == std::nullopt, "transport, As LibreELEC: not part of it");
  Check(ForTransport(L, false, false, c5, C, C) == N, "transport, As LibreELEC with Kodi's own Cinema: No Data");

  // ---- what the AVI InfoFrame says, and its bytes (PB3 bit 7 ITC, PB5 bits 5:4 CN)
  struct AviCase
  {
    Driver driver;
    Type type;
    bool dolby;
    int itc, cn;
    const char* row;
  };
  const AviCase avis[] = {
      // "No Data", As LibreELEC and the menus: each driver's own
      {Driver::INTEL, N, false, 0, 0, "none"},
      {Driver::AMDGPU, N, false, 1, 0, "graphics"},
      {Driver::NVIDIA, N, false, 1, 0, "graphics"},
      // Dolby Vision that the TV processes: no data in every driver of Hades, as a licensed box sends it
      {Driver::INTEL, N, true, 0, 0, "none"},
      {Driver::AMDGPU, N, true, 0, 0, "none"},
      {Driver::NVIDIA, N, true, 0, 0, "none"},
      {Driver::INTEL, C, false, 1, 2, "cinema"},
      {Driver::AMDGPU, C, false, 1, 2, "cinema"},
      {Driver::NVIDIA, C, false, 1, 2, "cinema"},
      {Driver::NVIDIA, C, true, 1, 2, "cinema"},
      {Driver::AMDGPU, G, false, 1, 3, "game"},
      {Driver::NVIDIA, Type::PHOTO, false, 1, 1, "photo"},
      {Driver::INTEL, Type::GRAPHICS, false, 1, 0, "graphics"},
  };
  for (const auto& a : avis)
  {
    const auto avi = AviOf(a.driver, a.type, a.dolby);
    const std::string what = std::string("AVI of ") + EnumName(a.type) + (a.dolby ? " with Dolby Vision" : "") +
                             " on driver " + std::to_string(static_cast<int>(a.driver));
    Check(avi && avi->itc == a.itc && avi->cn == a.cn, what);
    const int pb3 = avi ? avi->itc << 7 : -1;
    const int pb5 = avi ? avi->cn << 4 : -1;
    Check(pb3 == (a.itc << 7) && pb5 == (a.cn << 4),
          what + ": PB3 " + std::to_string(pb3) + ", PB5 " + std::to_string(pb5));
    Check(std::string(RowValue(avi)) == a.row, what + ": the player information says " + RowValue(avi));
  }
  Check(!AviOf(Driver::OTHER, C, false) && std::string(RowValue(std::nullopt)).empty(),
        "another driver: not known, the row is not shown");
  Check(DriverOf("xe") == Driver::INTEL && DriverOf("nvidia-drm") == Driver::NVIDIA &&
            DriverOf("amdgpu") == Driver::AMDGPU && DriverOf("vc4") == Driver::OTHER,
        "the drivers by their names");
  for (const Type t : {N, Type::GRAPHICS, Type::PHOTO, C, G})
    Check(TypeOfName(EnumName(t)) == t, std::string("the name of ") + EnumName(t) + " both ways (DRM's names)");
  Check(EnumName(N) == std::string("No Data") && EnumName(C) == std::string("Cinema"), "DRM's names");
  Check(!TypeOfName("Movie"), "a name DRM does not have: nothing");
  Check(ChoiceOf(1) == L && ChoiceOf(0) == A && ChoiceOf(7) == A, "the values of the setting");

  // ---- the transport: the one of before, but for "content type"
  for (const bool nvidia : {false, true})
    for (const bool lowLatency : {false, true})
    {
      OS::TRANSPORT::Kind kind;
      kind.nvidia = nvidia;
      kind.lowLatency = lowLatency;
      const auto before = OS::TRANSPORT::TransportSpecs(kind);
      kind.contentType = EnumName(lowLatency ? C : N);
      const auto now = OS::TRANSPORT::TransportSpecs(kind);
      const std::string what = std::string(nvidia ? "NVIDIA" : "the kernel's backends") +
                               (lowLatency ? ", Low-Latency" : ", Standard");
      Check(now.size() == before.size() + 1, what + ": one property more in the transport");
      size_t same = 0, content = 0;
      for (const auto& s : now)
      {
        if (std::string(s.name) == PROPERTY)
        {
          ++content;
          Check(s.role == OS::Role::CONNECTOR && s.how == OS::Spec::How::ENUM_NAME && !s.required &&
                    s.enumName == std::string(kind.contentType),
                what + ": content type on the connector, by its name, not required");
          continue;
        }
        for (const auto& b : before)
          if (std::string(b.name) == s.name && b.role == s.role && b.number == s.number &&
              b.enumName == s.enumName && b.required == s.required)
          {
            ++same;
            break;
          }
      }
      Check(content == 1 && same == before.size(), what + ": every other property as before");
      Check(OS::PartOf(OS::Role::CONNECTOR, PROPERTY) == OS::Part::SETTING,
            "content type is a setting of the transport: Kodi asks again with its next change of mode");
    }

  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
