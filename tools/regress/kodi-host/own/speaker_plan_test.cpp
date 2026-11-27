// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// speaker_plan_test.cpp: the plan of the speakers of the Audio page of the player information (utils/SpeakerPlan.h):
//  - the Speaker Allocation Data Block of real EDIDs (an LG C5 direct and through a TCL sound bar, a Denon and an
//    Onkyo receiver, an LG TV that declares none) and of made ones (2.0, 5.1, 5.1.2 with front or middle heights,
//    7.1 in the words of CTA-861-D and of CTA-861-G, 7.1.4, a sound bar of 3.1.2), each bit as edid-decode
//    (v4l-utils 1.32.0, cta_speaker_map[]) and libdisplay-info 0.4.0 (parse_speaker_alloc()) read it; the first byte
//    is what the kernel puts in the ELD (drm_edid.c: eld[DRM_ELD_SPEAKER] = data[0]) and what Kodi's ELD parser
//    reads (AEELDParser.cpp, by the masks of drm_eld.h);
//  - Atmos declared by the descriptors (E-AC-3 with Joint Object Coding, MAT with object audio);
//  - what the plan shows: passed through, decoded and not known; the objects that a receiver renders (Dolby Digital
//    Plus Atmos with a 5.1 bed to a 5.1.2 receiver), and never a speaker shown as used where nothing says so;
//  - what a downmix folds, by the matrix that libswresample builds (swr_build_matrix2(), the host's FFmpeg, with the
//    levels that Kodi gives it);
//  - where WinSystemGbmGLESContext.cpp takes each of them (in the source);
//  - (1.2.3, Ryudoadema: "not known yet" with a receiver of 5.1.2) where what is declared comes from (DeclaredBy()):
//    the EDID of the display the sound goes to, shown or on an output of its own, with its Room Configuration and
//    Speaker Location Data Blocks (CTA-861-G, as libdisplay-info 0.4.0 reads them), else its ELD (no heights), and
//    "not known yet" only where the output or its display is not known; a receiver of 5.1.2 with Atmos passed
//    through: its heights as the speakers it renders the objects to.
//  - (1.2.3, the owner's TCL Q85H Pro: "9.1.6 (LG TV SSCR2)" read as his speakers) the two bits that CTA-861-H
//    deprecates place no speaker of their own (F33, TpLS/TpRS, never; F16, RLC/RRC, only in the form of
//    CTA-861-D); which device declares them, by the physical address of the HDMI vendor block: a repeater between
//    the PC and the display (the sound bar, 2.2.0.0), the display or what is on its eARC (4.0.0.0, the bar on the
//    TV's eARC), the TV's own speakers (4.0.0.0, none declared), none at all (DisplayPort); the speakers of the room
//    only from a Room Configuration or Speaker Location Data Block.
// usage: speaker_plan_test <Kodi tree> <lg-c5-direct> <lg-c5-sound-bar> <receiver-denon> <receiver-onkyo> <tv-lg-1>
//                          <lg-c5-tv-speakers>
#include "utils/SpeakerPlan.h"

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace SP = KODI::UTILS::SPEAKER_PLAN;

namespace
{
int checks = 0;
int failed = 0;

void Check(bool ok, const std::string& what, const std::string& got = "")
{
  ++checks;
  if (!ok)
  {
    ++failed;
    std::printf("FAIL: %s%s%s\n", what.c_str(), got.empty() ? "" : ": got ", got.c_str());
  }
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::string ReadText(const std::string& path)
{
  std::ifstream in(path);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

std::string Join(const SP::Places& p)
{
  std::string s;
  for (const std::string& x : p)
    s += (s.empty() ? "" : " ") + x;
  return s;
}

// an EDID of a base block and one CTA-861 extension of revision 3 with a speaker allocation (where sadb) and the
// short audio descriptors given (3 bytes each)
std::vector<uint8_t> MadeEdid(const std::vector<uint8_t>& sadb, const std::vector<uint8_t>& sads)
{
  std::vector<uint8_t> e(256, 0);
  const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  for (int i = 0; i < 8; ++i)
    e[i] = header[i];
  e[126] = 1;
  uint8_t* c = e.data() + 128;
  c[0] = 0x02;
  c[1] = 0x03;
  std::size_t at = 4;
  if (!sads.empty())
  {
    c[at++] = static_cast<uint8_t>(0x20 | sads.size());
    for (uint8_t b : sads)
      c[at++] = b;
  }
  if (!sadb.empty())
  {
    c[at++] = static_cast<uint8_t>(0x80 | sadb.size());
    for (uint8_t b : sadb)
      c[at++] = b;
  }
  c[2] = static_cast<uint8_t>(at);
  return e;
}

// short audio descriptors: E-AC-3 6 channels with or without Joint Object Coding; MAT 8 channels with or without
// object audio
std::vector<uint8_t> Sads(bool joc, bool matObjects)
{
  return {static_cast<uint8_t>((10 << 3) | 5), 0x07, static_cast<uint8_t>(joc ? 0x01 : 0x00),
          static_cast<uint8_t>((12 << 3) | 7), 0x04, static_cast<uint8_t>(matObjects ? 0x01 : 0x00)};
}

SP::Places DeclaredOf(const std::vector<uint8_t>& edid)
{
  const SP::EdidAudio a = SP::AudioOfEdid(edid);
  return a.allocation ? SP::Declared(*a.allocation) : SP::Places{};
}

// what the downmix folds, by the matrix of the host's libswresample with Kodi's levels
std::string Fold(const std::vector<std::string>& in,
                 uint64_t inMask,
                 const std::vector<std::string>& out,
                 uint64_t outMask,
                 double lfe,
                 std::string& dropped)
{
  AVChannelLayout i = {}, o = {};
  av_channel_layout_from_mask(&i, inMask);
  av_channel_layout_from_mask(&o, outMask);
  // (1.2.3: the block that libswresample writes, as WinSystemGbmGLESContext.cpp gives it, and a guard after it of
  // 1.0 that nothing may change: the library is not instrumented, so an overrun of the matrix is seen there. A matrix
  // of the layouts' channels alone, as 1.2.2 gave it, was overrun and the heap of Kodi corrupted)
  constexpr std::size_t GUARD = 4096;
  std::vector<double> m(SP::SWR_MATRIX_SIZE + GUARD, 0.0);
  std::fill(m.begin() + static_cast<std::ptrdiff_t>(SP::SWR_MATRIX_SIZE), m.end(), 1.0);
  const int r = swr_build_matrix2(&i, &o, M_SQRT1_2, M_SQRT1_2, lfe, 1.0, 1.0, m.data(),
                                  static_cast<ptrdiff_t>(SP::SWR_MATRIX_STRIDE), AV_MATRIX_ENCODING_NONE, nullptr);
  av_channel_layout_uninit(&i);
  av_channel_layout_uninit(&o);
  Check(std::all_of(m.begin() + static_cast<std::ptrdiff_t>(SP::SWR_MATRIX_SIZE), m.end(),
                    [](double v) { return v == 1.0; }),
        "swr_build_matrix2() writes nothing past the block that SpeakerPlan.h gives it");
  if (r < 0)
    return "swr_build_matrix2 failed";
  m.resize(SP::SWR_MATRIX_SIZE);
  return SP::Folded(in, out, m, SP::SWR_MATRIX_STRIDE, dropped);
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 8)
  {
    std::printf("usage: speaker_plan_test <Kodi tree> <6 EDIDs>\n");
    return 2;
  }
  const std::string kodi = argv[1];

  // ---- 1. real EDIDs
  const auto c5 = ReadFile(argv[2]), bar = ReadFile(argv[3]), denon = ReadFile(argv[4]), onkyo = ReadFile(argv[5]),
             lgtv = ReadFile(argv[6]);
  const SP::EdidAudio c5a = SP::AudioOfEdid(c5);
  Check(c5a.allocation && c5a.allocation->bytes == std::array<uint8_t, 3>{0x6F, 0x0F, 0x0C},
        "LG C5: its speaker allocation, 6f 0f 0c");
  // (up to 1.2.3 its deprecated TpLS/TpRS made two more heights, TSL/TSR: "9.1.6"; CTA-861-H deprecates the bit,
  // libdisplay-info 0.4.0 reads nothing of it, and the top sides are TpSiL/TpSiR's, which it does not set)
  Check(Join(DeclaredOf(c5)) == "BL BR FC FL FLOC FR FROC LFE SL SR TC TFC TFL TFR",
        "LG C5: FL/FR, LFE, FC, FLc/FRc, LS/RS as the sides and BL/BR as the backs (RLC/RRC, deprecated, adds "
        "nothing); TpFL/TpFR, TpC, TpFC; TpLS/TpRS, deprecated, not placed", Join(DeclaredOf(c5)));
  Check(SP::LayoutName(DeclaredOf(c5)) == "9.1.4" && c5a.allocation &&
            SP::DeclaredOffPlan(*c5a.allocation) ==
                std::vector<std::string>{"BtFL/BtFR", "TpLS/TpRS (deprecated)"},
        "LG C5: up to 9.1.4 accepted, and BtFL/BtFR and the deprecated TpLS/TpRS, which the plan does not place",
        SP::LayoutName(DeclaredOf(c5)));
  Check(DeclaredOf(bar) == DeclaredOf(c5) && SP::AudioOfEdid(bar).eac3Joc && SP::AudioOfEdid(c5).eac3Joc,
        "LG C5 through the sound bar: the same allocation; Atmos over Dolby Digital Plus (JOC) declared");
  Check(Join(DeclaredOf(denon)) == "BC BL BR FC FL FR LFE SL SR" && SP::LayoutName(DeclaredOf(denon)) == "8.1",
        "Denon (5f 00 00, CTA-861-D): RL/RR the surrounds, RLC/RRC the backs, and the back centre",
        Join(DeclaredOf(denon)));
  Check(Join(DeclaredOf(onkyo)) == "BL BR FC FL FR LFE SL SR" && SP::LayoutName(DeclaredOf(onkyo)) == "7.1",
        "Onkyo (4f 00 00): 7.1", Join(DeclaredOf(onkyo)));
  Check(!SP::AudioOfEdid(lgtv).allocation && DeclaredOf(lgtv).empty() && SP::AudioOfEdid(lgtv).read,
        "an LG TV without a speaker allocation: none declared (not every speaker, as the HDA codec shows it)");
  // the first byte is what the ELD carries, read by Kodi's ELD parser with the masks of drm_eld.h
  const std::string eld = ReadText(kodi + "/xbmc/cores/AudioEngine/Utils/AEELDParser.cpp");
  Check(eld.find("header.rlrc                = (data[7 ] & 0x40) == 0x40;") != std::string::npos &&
            eld.find("header.flrc                = (data[7 ] & 0x20) == 0x20;") != std::string::npos &&
            eld.find("header.rc                  = (data[7 ] & 0x10) == 0x10;") != std::string::npos &&
            eld.find("header.rlr                 = (data[7 ] & 0x08) == 0x08;") != std::string::npos &&
            eld.find("header.fc                  = (data[7 ] & 0x04) == 0x04;") != std::string::npos &&
            eld.find("header.lfe                 = (data[7 ] & 0x02) == 0x02;") != std::string::npos &&
            eld.find("header.flr                 = (data[7 ] & 0x01) == 0x01;") != std::string::npos,
        "Kodi's ELD parser: byte 7 by the bits of DRM_ELD_SPEAKER_* (RLRC 0x40 ... FLR 0x01), the SADB's first byte");

  // ---- 1b. which device declares them, by the physical address of the HDMI vendor block (the owner's EDIDs of
  // 2026-10-21 and of 1.2.3); and the speakers of the room only where a block says them
  {
    const auto tvSpeakers = ReadFile(argv[7]);
    const SP::EdidAudio barA = SP::AudioOfEdid(bar), c5A = SP::AudioOfEdid(c5), tvA = SP::AudioOfEdid(tvSpeakers);
    Check(SP::PhysicalAddressText(barA.physicalAddress) == "2.2.0.0" && SP::DeclarerOf(barA) == SP::Declarer::REPEATER,
          "through the sound bar (2.2.0.0, two levels): the bar between the PC and the TV declares them, not the TV",
          SP::PhysicalAddressText(barA.physicalAddress));
    Check(SP::PhysicalAddressText(c5A.physicalAddress) == "4.0.0.0" &&
              SP::DeclarerOf(c5A) == SP::Declarer::DISPLAY_OR_ITS_ARC,
          "the PC on the TV with the bar on its eARC (4.0.0.0, a speaker allocation): the TV or what is on its eARC",
          SP::PhysicalAddressText(c5A.physicalAddress));
    Check(SP::PhysicalAddressText(tvA.physicalAddress) == "4.0.0.0" && !tvA.allocation && DeclaredOf(tvSpeakers).empty(),
          "the PC on the TV with its own speakers (4.0.0.0): no speaker allocation, none declared");
    Check(!barA.room && barA.located.empty() && !SP::RoomPlaces(barA) && !SP::RoomPlaces(c5A),
          "the bar and the TV: no Room Configuration nor Speaker Location Data Block, so no speakers of the room");
    SP::SinkFacts f;
    f.output = true;
    f.kind = SP::SinkFacts::Kind::DISPLAY;
    f.identity = true;
    f.shown = true;
    f.edid = bar;
    SP::SinkDeclared d = SP::DeclaredBy(f);
    Check(d.places && SP::LayoutName(*d.places) == "9.1.4" && d.declarer == SP::Declarer::REPEATER && !d.room,
          "DeclaredBy(), the bar: accepts up to 9.1.4, declared by the device between, its speakers not said");
    f.edid = c5;
    d = SP::DeclaredBy(f);
    Check(d.places && d.declarer == SP::Declarer::DISPLAY_OR_ITS_ARC, "DeclaredBy(), the bar on the TV's eARC");
    f.edid = tvSpeakers;
    Check(SP::DeclaredBy(f).why == "none", "DeclaredBy(), the TV's own speakers: none declared");
    // depths
    Check(SP::PhysicalDepth(0x1000) == 1 && SP::PhysicalDepth(0x2200) == 2 && SP::PhysicalDepth(0x1230) == 3 &&
              SP::PhysicalDepth(0x1234) == 4 && SP::PhysicalDepth(0x0000) == 0 && SP::PhysicalDepth(0x1020) == 0 &&
              SP::PhysicalDepth(0xFFFF) == 0,
          "the depth of a physical address: its levels before the first 0; 0.0.0.0, a gap or none: 0");
    // made: an EDID without an HDMI vendor block (DisplayPort): the device it names; an ELD: the same
    const SP::EdidAudio noVsdb = SP::AudioOfEdid(MadeEdid({0x0F, 0x00, 0x00}, Sads(true, true)));
    Check(noVsdb.physicalAddress == 0xFFFF && SP::DeclarerOf(noVsdb) == SP::Declarer::NAMED,
          "made: no HDMI vendor block, no address: the device it names");
    SP::EdidAudio eldOne;
    eldOne.fromEld = true;
    eldOne.physicalAddress = 0x2200;
    Check(SP::DeclarerOf(eldOne) == SP::Declarer::NAMED, "an ELD: no address is read from it");
    // a Room Configuration Data Block of 5.1.2 behind an allocation of 7.1.4: the speakers of the room are its own
    std::vector<uint8_t> roomEdid = c5;
    SP::EdidAudio roomA = SP::AudioOfEdid(roomEdid);
    roomA.room = SP::Allocation{{0x0F, 0x01, 0x00}};
    Check(SP::RoomPlaces(roomA) && SP::LayoutName(*SP::RoomPlaces(roomA)) == "5.1.2",
          "a Room Configuration Data Block: the speakers it says the room has (5.1.2), apart from what it accepts");
    roomA.room.reset();
    roomA.located = {0x00, 0x01, 0x02, 0x03};
    Check(SP::RoomPlaces(roomA) && SP::LayoutName(*SP::RoomPlaces(roomA)) == "3.1",
          "a Speaker Location Data Block alone: the speakers it names");
  }
  // the deprecated bits, made: F33 (TpLS/TpRS) alone places nothing; F16 (RLC/RRC) with the surrounds named and
  // no BL/BR places nothing; in the form of CTA-861-D (no surrounds named) it is the backs
  {
    std::vector<std::string> off;
    const SP::EdidAudio f33 = SP::AudioOfEdid(MadeEdid({0x0F, 0x00, 0x08}, Sads(true, true)));
    Check(Join(SP::DeclaredPlaces(f33, off)) == "FC FL FR LFE SL SR" &&
              off == std::vector<std::string>{"TpLS/TpRS (deprecated)"},
          "made: TpLS/TpRS (F33) alone: no height placed, named off the plan", Join(SP::DeclaredPlaces(f33, off)));
    const SP::Places f16 = DeclaredOf(MadeEdid({0x47, 0x08, 0x00}, Sads(true, true)));
    Check(Join(f16) == "FC FL FR LFE SL SR", "made: RLC/RRC (F16) with LS/RS and no BL/BR: no backs", Join(f16));
  }

  // ---- 2. made EDIDs
  struct Made
  {
    const char* what;
    std::vector<uint8_t> sadb;
    const char* places;
    const char* layout;
  };
  const Made made[] = {
      {"2.0", {0x01, 0x00, 0x00}, "FL FR", "2.0"},
      {"5.1 (CTA-861-D: RL/RR the surrounds)", {0x0F, 0x00, 0x00}, "FC FL FR LFE SL SR", "5.1"},
      {"5.1.2, front heights (TpFL/TpFR)", {0x0F, 0x01, 0x00}, "FC FL FR LFE SL SR TFL TFR", "5.1.2"},
      {"5.1.2, middle heights (TpSiL/TpSiR)", {0x0F, 0x80, 0x00}, "FC FL FR LFE SL SR TSL TSR", "5.1.2"},
      {"7.1 (CTA-861-D: RL/RR and RLC/RRC)", {0x4F, 0x00, 0x00}, "BL BR FC FL FR LFE SL SR", "7.1"},
      {"7.1 (CTA-861-G: LS/RS and BL/BR)", {0x0F, 0x08, 0x00}, "BL BR FC FL FR LFE SL SR", "7.1"},
      {"7.1.4 (TpFL/TpFR and TpBL/TpBR)", {0x4F, 0x01, 0x01}, "BL BR FC FL FR LFE SL SR TBL TBR TFL TFR", "7.1.4"},
      {"a sound bar of 3.1.2", {0x07, 0x01, 0x00}, "FC FL FR LFE TFL TFR", "3.1.2"},
      {"a block of one byte", {0x0F}, "FC FL FR LFE SL SR", "5.1"},
  };
  for (const Made& m : made)
  {
    const SP::Places p = DeclaredOf(MadeEdid(m.sadb, Sads(true, true)));
    Check(Join(p) == m.places && SP::LayoutName(p) == m.layout, std::string("made: ") + m.what,
          Join(p) + " / " + SP::LayoutName(p));
  }
  Check(!SP::AudioOfEdid(MadeEdid({}, Sads(false, false))).allocation, "made: no allocation, none declared");
  Check(SP::AudioOfEdid(MadeEdid({0x0B}, Sads(true, false))).eac3Joc &&
            !SP::AudioOfEdid(MadeEdid({0x0B}, Sads(false, false))).eac3Joc &&
            SP::AudioOfEdid(MadeEdid({0x0B}, Sads(false, true))).matObjects &&
            !SP::AudioOfEdid(MadeEdid({0x0B}, Sads(true, false))).matObjects,
        "made: JOC by bit 0 of the E-AC-3 descriptor, object audio by bit 0 of the MAT one");

  // ---- 3. the plan
  const SP::Places bed51 = SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR"));
  Check(Join(bed51) == "FC FL FR LFE SL SR", "a 5.1 of BL/BR: the surrounds", Join(bed51));
  Check(Join(SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR,SL,SR"))) == "BL BR FC FL FR LFE SL SR",
        "a 7.1: BL/BR the backs");
  const SP::Places r512 = DeclaredOf(MadeEdid({0x0F, 0x01, 0x00}, Sads(true, true)));
  // Ryudoadema: Dolby Digital Plus Atmos, a 5.1 bed, passed through to a 5.1.2 receiver that declares JOC
  {
    const SP::Plan p = SP::PlanOf(SP::Path::PASSTHROUGH, bed51, {}, r512, SP::ObjectStreamOf("eac3_ddp_atmos"),
                                  SP::Objects::DECLARED);
    Check(p.sound == ";FC;FL;FR;LFE;SL;SR;" && p.objects == ";TFL;TFR;" && p.declared.empty() &&
              p.objectsSaid == "yes",
          "DD+ Atmos 5.1 to a 5.1.2 receiver: the bed, and its two heights rendered by the receiver",
          p.sound + " " + p.objects + " " + p.declared + " " + p.objectsSaid);
  }
  {
    const SP::Plan p = SP::PlanOf(SP::Path::PASSTHROUGH, bed51, {}, r512, SP::ObjectStream::ATMOS_EAC3,
                                  SP::ObjectsDeclared(SP::AudioOfEdid(MadeEdid({0x0F, 0x01}, Sads(false, false))),
                                                      SP::ObjectStream::ATMOS_EAC3));
    Check(p.objects.empty() && p.declared == ";TFL;TFR;" && p.objectsSaid == "notdeclared",
          "... to one that does not declare JOC: the heights declared, nothing rendered there",
          p.objects + " " + p.declared + " " + p.objectsSaid);
  }
  {
    const SP::Plan p = SP::PlanOf(SP::Path::PASSTHROUGH, bed51, {}, std::nullopt, SP::ObjectStream::ATMOS_EAC3,
                                  SP::Objects::NOT_DECLARED);
    Check(p.objects.empty() && p.declared.empty() && p.objectsSaid == "notknown",
          "... where the receiver's speakers are not known: no height shown, not known said");
  }
  {
    const SP::Places r714 = DeclaredOf(MadeEdid({0x4F, 0x01, 0x01}, Sads(true, true)));
    const SP::Plan p =
        SP::PlanOf(SP::Path::PASSTHROUGH, SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR,SL,SR")), {}, r714,
                   SP::ObjectStreamOf("truehd_atmos"),
                   SP::ObjectsDeclared(SP::AudioOfEdid(MadeEdid({0x4F, 0x01, 0x01}, Sads(true, true))),
                                       SP::ObjectStream::ATMOS_TRUEHD));
    Check(p.objects == ";TBL;TBR;TFL;TFR;" && p.objectsSaid == "yes",
          "TrueHD Atmos 7.1 to a 7.1.4 receiver with object audio: four heights rendered", p.objects);
    const SP::Plan d = SP::PlanOf(SP::Path::PASSTHROUGH, SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR,SL,SR")), {},
                                  r714, SP::ObjectStreamOf("dtshd_ma_x"),
                                  SP::ObjectsDeclared(SP::AudioOfEdid(MadeEdid({0x4B}, Sads(true, true))),
                                                      SP::ObjectStream::DTSX));
    Check(d.objects.empty() && d.declared == ";TBL;TBR;TFL;TFR;" && d.objectsSaid == "notknown",
          "DTS:X: its EDID cannot say it: the heights declared, not shown as used", d.objects + " " + d.objectsSaid);
  }
  {
    // decoded: the channels sent, not the stream's; no objects (decoded Atmos is its core)
    const SP::Places sent = SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR"));
    const SP::Plan p = SP::PlanOf(SP::Path::DECODED, SP::Positions(SP::Names("FL,FR,FC,LFE,BL,BR,SL,SR")), sent,
                                  DeclaredOf(c5), SP::ObjectStream::ATMOS_TRUEHD, SP::Objects::DECLARED);
    Check(p.sound == ";FC;FL;FR;LFE;SL;SR;" && p.objects.empty() && p.objectsSaid.empty() &&
              p.declared == ";BL;BR;FLOC;FROC;TC;TFC;TFL;TFR;",
          "decoded to 5.1 on the LG C5: the six channels sent; the rest declared, no objects", p.sound + " " + p.declared);
    const SP::Plan s = SP::PlanOf(SP::Path::PASSTHROUGH, SP::Positions(SP::Names("FL,FR")), {},
                                  DeclaredOf(MadeEdid({0x0F}, Sads(true, true))), SP::ObjectStream::NONE,
                                  SP::Objects::NOT_DECLARED);
    Check(s.sound == ";FL;FR;" && s.declared == ";FC;LFE;SL;SR;" && s.objects.empty() && s.objectsSaid.empty(),
          "stereo passed through to a 5.1 receiver: two speakers with sound, the others declared");
    const SP::Plan n = SP::PlanOf(SP::Path::NOT_KNOWN, bed51, {}, std::nullopt, SP::ObjectStream::ATMOS_EAC3,
                                  SP::Objects::NOT_KNOWN);
    Check(n.sound == ";FC;FL;FR;LFE;SL;SR;" && n.objects.empty() && n.declared.empty() && n.objectsSaid.empty(),
          "no sink open: what the stream has, nothing else");
  }

  // ---- 4. the downmix, by the host's libswresample
  {
    std::string dropped;
    const std::string f = Fold({"FL", "FR", "FC", "LFE", "BL", "BR"}, AV_CH_LAYOUT_5POINT1_BACK, {"FL", "FR"},
                               AV_CH_LAYOUT_STEREO, 0.0, dropped);
    Check(f == "FC > FL+FR; BL+BR > FL+FR" && dropped == "LFE",
          "5.1 to stereo: the centre into both, the surrounds each into its side, the LFE left out (level 0)",
          f + " / " + dropped);
    const std::string g = Fold({"FL", "FR", "FC", "LFE", "BL", "BR"}, AV_CH_LAYOUT_5POINT1_BACK, {"FL", "FR"},
                               AV_CH_LAYOUT_STEREO, 0.5, dropped);
    Check(g == "FC+LFE > FL+FR; BL+BR > FL+FR" && dropped.empty(),
          "... with audiooutput.mixsublevel: the LFE into both", g);
    const std::string h = Fold({"FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR"}, AV_CH_LAYOUT_7POINT1,
                               {"FL", "FR", "FC", "LFE", "BL", "BR"}, AV_CH_LAYOUT_5POINT1_BACK, 0.0, dropped);
    Check(h == "SL+SR > BL+BR" && dropped.empty(), "7.1 to 5.1: the sides into the backs", h);
  }

  // ---- 5. where Kodi takes them
  const std::string ctx = ReadText(kodi + "/xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  Check(ctx.find("    SpeakerPlanInfo(info, speakers.lit, channels, audio.codecName,") != std::string::npos,
        "the plan from what the player said of the stream");
  Check(ctx.find("    auto output = withRole(\"the output that the sink opened\");\n"
                 "    if (output == outputs.end())\n"
                 "      output = withRole(path == SP::Path::PASSTHROUGH ? \"the passthrough output in use\"\n"
                 "                                                      : \"the audio output in use\");") !=
                std::string::npos,
        "the output the sink opened, else the output of the path in use (never nothing while one is said)");
  Check(ctx.find("          facts.edid = m_DRM->EdidOfSoundDisplay(output->identity);") != std::string::npos &&
            ctx.find("  const SP::SinkDeclared declared = SP::DeclaredBy(facts);") != std::string::npos &&
            ctx.find("      facts.eld = output->eld;") != std::string::npos,
        "the EDID of the display on any output, its ELD, decided by DeclaredBy()");
  const std::string ae = ReadText(kodi + "/xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAE.cpp");
  const size_t change = ae.find("void CActiveAE::HandleDeviceCountChange(");
  const size_t said = ae.find("  PublishSoundOutputs();", change);
  const size_t decision = ae.find("  if (!INTERNAL::ShouldReconfigure(decision))", change);
  Check(change != std::string::npos && said != std::string::npos && decision != std::string::npos && said < decision,
        "a listing of the outputs again is said, also where the sink stays as it is");
  Check(ReadText(kodi + "/xbmc/cores/AudioEngine/Sinks/AESinkALSA.cpp").find("    info.m_eld.assign(eld, eld + dataLength);") !=
                std::string::npos &&
            ae.find("        out.eld = d.m_eld;") != std::string::npos,
        "the ELD of each output kept and said with it");

  // ---- 6. where what is declared comes from (1.2.3)
  {
    // an EDID made of a base block and one CTA-861 extension with the data blocks given
    const auto made = [](const std::vector<uint8_t>& blocks)
    {
      std::vector<uint8_t> e(256, 0);
      const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
      std::copy(header, header + 8, e.begin());
      e[126] = 1;
      e[128] = 0x02;
      e[129] = 0x03;
      e[130] = static_cast<uint8_t>(4 + blocks.size());
      std::copy(blocks.begin(), blocks.end(), e.begin() + 132);
      return e;
    };
    // a receiver of 5.1.2 that declares Atmos over Dolby Digital Plus: an E-AC-3 descriptor with JOC, 5.1 with
    // front heights (TpFL/TpFR)
    const std::vector<uint8_t> r512 = made({0x23, 0x55, 0x07, 0x01, 0x83, 0x0F, 0x01, 0x00});
    // 5.1 in its allocation, its middle heights (TpSiL/TpSiR) in its Room Configuration Data Block (speaker count 8),
    // FLw, TpBL and TpBR named in its Speaker Location Data Block
    const std::vector<uint8_t> room = made({0x83, 0x0F, 0x00, 0x00, 0xE5, 0x13, 0x47, 0x0F, 0x80, 0x00,
                                            0xE7, 0x14, 0x20, 0x18, 0x21, 0x10, 0x22, 0x11});
    std::vector<std::string> off;
    const SP::Places roomPlaces = SP::DeclaredPlaces(SP::AudioOfEdid(room), off);
    Check(Join(roomPlaces) == "FC FL FR LFE SL SR TBL TBR TSL TSR",
          "the Room Configuration and Speaker Location Data Blocks with the allocation: 5.1, TpSiL/TpSiR, TpBL/TpBR",
          Join(roomPlaces));
    Check(off == std::vector<std::string>{"FLw"}, "FLw named, no place on the plan: said apart");
    // the ELD of a Denon (as the RTX 5090 of Khazar321 read it, 1.2.2: speakers 0x5f, CTA-861-D), with an E-AC-3
    // descriptor with JOC
    std::vector<uint8_t> eld = {0x10, 0x00, 0x08, 0x00, 0x69, 0x10, 0x00, 0x5F, 0, 0, 0, 0, 0, 0, 0, 0,
                                0x11, 0xEE, 0x76, 0x00, 'D', 'E', 'N', 'O', 'N', '-', 'A', 'V', 'R'};
    eld.insert(eld.end(), {0x55, 0x07, 0x01});
    const SP::EdidAudio eldAudio = SP::AudioOfEld(eld);
    Check(eldAudio.read && eldAudio.fromEld && eldAudio.eac3Joc && eldAudio.allocation &&
              eldAudio.allocation->bytes == std::array<uint8_t, 3>{0x5F, 0, 0},
          "the ELD: its speaker byte alone and its descriptors (Atmos over Dolby Digital Plus)");

    SP::SinkFacts f;
    Check(SP::DeclaredBy(f).why == "notknown" && !SP::DeclaredBy(f).places, "no output said yet: not known yet");
    f.output = true;
    f.kind = SP::SinkFacts::Kind::OTHER;
    Check(SP::DeclaredBy(f).why == "other", "an output without a display: not read");
    f.kind = SP::SinkFacts::Kind::DISPLAY;
    Check(SP::DeclaredBy(f).why == "notknown", "an HDMI output whose display is not known yet (no ELD): not known yet");
    f.identity = true;
    Check(SP::DeclaredBy(f).why == "otherdisplay", "a display that no output has, without its ELD: not read");
    f.eld = eld;
    SP::SinkDeclared d = SP::DeclaredBy(f);
    Check(d.from == "eld" && d.places && Join(*d.places) == "BC BL BR FC FL FR LFE SL SR",
          "no output has the display, its ELD is known: 8.1 from the ELD, no heights", d.places ? Join(*d.places) : "");
    f.edid = r512;
    d = SP::DeclaredBy(f);
    Check(d.from == "edidother" && d.places && SP::LayoutName(*d.places) == "5.1.2",
          "the receiver on an output of its own: 5.1.2 from its EDID, heights included",
          d.from + " " + (d.places ? SP::LayoutName(*d.places) : ""));
    f.shown = true;
    d = SP::DeclaredBy(f);
    Check(d.from == "edid" && d.places && SP::LayoutName(*d.places) == "5.1.2",
          "the receiver Kodi shows through: 5.1.2 from the EDID of the display shown");
    // Ryudoadema: Dolby Digital Plus Atmos with a 5.1 bed passed through to it
    const SP::Plan plan = SP::PlanOf(SP::Path::PASSTHROUGH, SP::Positions(SP::Names("FL FR FC LFE BL BR")), {},
                                     d.places, SP::ObjectStream::ATMOS_EAC3,
                                     SP::ObjectsDeclared(d.audio, SP::ObjectStream::ATMOS_EAC3));
    Check(plan.objects == ";TFL;TFR;" && plan.objectsSaid == "yes",
          "DD+ Atmos 5.1 passed through to it: its heights as the speakers it renders the objects to",
          plan.objects + " " + plan.objectsSaid);
    f.edid = ReadFile(argv[6]);
    Check(SP::DeclaredBy(f).why == "none" && !SP::DeclaredBy(f).places,
          "an EDID without a speaker allocation: none declared");
  }
  Check(ctx.find("swr_build_matrix2(&inLayout, &outLayout, M_SQRT1_2, M_SQRT1_2, lfe, 1.0, 1.0,") !=
            std::string::npos,
        "the downmix by the matrix of libswresample with Kodi's levels");
  Check(ctx.find("  std::vector<double> matrix(SP::SWR_MATRIX_SIZE);") != std::string::npos &&
            ctx.find("static_cast<ptrdiff_t>(SP::SWR_MATRIX_STRIDE), AV_MATRIX_ENCODING_NONE,") != std::string::npos &&
            ctx.find("SP::Folded(inOrdered, outOrdered, matrix, SP::SWR_MATRIX_STRIDE, dropped);") != std::string::npos,
        "Kodi gives libswresample the whole block it writes (1.2.2: the layouts' channels alone, the heap corrupted)");
  Check(ctx.find("  const SP::Path path = !sound               ? SP::Path::NOT_KNOWN\n") != std::string::npos &&
            ctx.find("SpeakerPlanInfo(info, speakers.lit, channels, audio.codecName, player->GetAudioStream() >= 0);") !=
                std::string::npos,
        "a video without a stream of sound: no speaker gets sound (the sink only plays silence)");
  Check(ctx.find("Player information: what the player said of the sound names no speaker") != std::string::npos,
        "the line of kodi.log of a sound that names no speaker stays");
  std::printf("%d checks, %d failed\n%s\n", checks, failed,
              failed ? "SPEAKER-PLAN FAILED" : "SPEAKER-PLAN PASSED");
  return failed ? 1 : 0;
}
