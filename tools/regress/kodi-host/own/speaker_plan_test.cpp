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
//  - where WinSystemGbmGLESContext.cpp takes each of them (in the source).
// usage: speaker_plan_test <Kodi tree> <lg-c5-direct> <lg-c5-sound-bar> <receiver-denon> <receiver-onkyo> <tv-lg-1>
#include "utils/SpeakerPlan.h"

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

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
  std::vector<double> m(static_cast<std::size_t>(i.nb_channels * o.nb_channels));
  const int r = swr_build_matrix2(&i, &o, M_SQRT1_2, M_SQRT1_2, lfe, 1.0, 1.0, m.data(), i.nb_channels,
                                  AV_MATRIX_ENCODING_NONE, nullptr);
  const std::size_t stride = static_cast<std::size_t>(i.nb_channels);
  av_channel_layout_uninit(&i);
  av_channel_layout_uninit(&o);
  if (r < 0)
    return "swr_build_matrix2 failed";
  return SP::Folded(in, out, m, stride, dropped);
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 7)
  {
    std::printf("usage: speaker_plan_test <Kodi tree> <5 EDIDs>\n");
    return 2;
  }
  const std::string kodi = argv[1];

  // ---- 1. real EDIDs
  const auto c5 = ReadFile(argv[2]), bar = ReadFile(argv[3]), denon = ReadFile(argv[4]), onkyo = ReadFile(argv[5]),
             lgtv = ReadFile(argv[6]);
  const SP::EdidAudio c5a = SP::AudioOfEdid(c5);
  Check(c5a.allocation && c5a.allocation->bytes == std::array<uint8_t, 3>{0x6F, 0x0F, 0x0C},
        "LG C5: its speaker allocation, 6f 0f 0c");
  Check(Join(DeclaredOf(c5)) == "BL BR FC FL FLOC FR FROC LFE SL SR TC TFC TFL TFR TSL TSR",
        "LG C5: FL/FR, LFE, FC, FLc/FRc, LS/RS as the sides and BL/BR (RL/RR, RLC/RRC) as the backs; TpFL/TpFR, TpC, "
        "TpFC, TpLS/TpRS", Join(DeclaredOf(c5)));
  Check(SP::LayoutName(DeclaredOf(c5)) == "9.1.6" && c5a.allocation &&
            SP::DeclaredOffPlan(*c5a.allocation) == std::vector<std::string>{"BtFL/BtFR"},
        "LG C5: 9.1.6 declared, and BtFL/BtFR, which the plan has no place for", SP::LayoutName(DeclaredOf(c5)));
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
              p.declared == ";BL;BR;FLOC;FROC;TC;TFC;TFL;TFR;TSL;TSR;",
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
  Check(ctx.find("    SpeakerPlanInfo(info, speakers.lit, channels, audio.codecName);") != std::string::npos,
        "the plan from what the player said of the stream");
  Check(ctx.find("return o.role.find(\"the output that the sink opened\") != std::string::npos;") !=
                std::string::npos &&
            ctx.find("    else if (!display || !KODI::UTILS::SameDisplay(opened->identity,") != std::string::npos,
        "declared speakers only where the sink opened the output of the display that Kodi shows on");
  Check(ctx.find("swr_build_matrix2(&inLayout, &outLayout, M_SQRT1_2, M_SQRT1_2, lfe, 1.0, 1.0,") !=
            std::string::npos,
        "the downmix by the matrix of libswresample with Kodi's levels");
  Check(ctx.find("Player information: what the player said of the sound names no speaker") != std::string::npos,
        "the line of kodi.log of a sound that names no speaker stays");
  std::printf("%d checks, %d failed\n%s\n", checks, failed,
              failed ? "SPEAKER-PLAN FAILED" : "SPEAKER-PLAN PASSED");
  return failed ? 1 : 0;
}
