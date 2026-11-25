// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades: whether the EDID that the PC reads comes from the display itself or through a sound bar, an AV receiver or a
// switch, by the HDMI address of its HDMI VSDB, and the sound formats it declares (xbmc/utils/EdidHdmiPath.h), on the
// EDIDs of one LG C5 read both ways (direct: 4.0.0.0, Dolby Vision block v2, no DTS; through a TCL sound bar: 2.2.0.0,
// v1, DTS and DTS-HD, as worked out by hand from their bytes), on EDIDs of AV receivers and TVs of the linuxhw database,
// and on EDIDs made here byte by byte: the rules of HDMI 1.4b 8.7 for an address (F.F.F.F and 0.0.0.0 say nothing, no
// nibble after a 0), the first HDMI VSDB only, the audio data blocks of every CTA extension, nothing read past a block.
// usage: edidpath_test <c5 direct> <c5 through a sound bar> <receiver>... -- <tv>...
#include "utils/EdidHdmiPath.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using KODI::UTILS::EdidHdmiPath;
using KODI::UTILS::EdidHdmiPathOf;

static int fails = 0;
static int checks = 0;

static void Check(bool ok, const std::string& what)
{
  ++checks;
  if (!ok)
  {
    ++fails;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static std::vector<uint8_t> Read(const char* path)
{
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// A base block and one CTA extension holding the data blocks given (each with its header byte), detailed timings after
static std::vector<uint8_t> Edid(const std::vector<std::vector<uint8_t>>& blocks, int extensions = 1)
{
  std::vector<uint8_t> e(128 * (1 + extensions), 0);
  const uint8_t head[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
  std::copy(head, head + 8, e.begin());
  e[126] = static_cast<uint8_t>(extensions);
  for (int x = 0; x < extensions; ++x)
  {
    uint8_t* c = &e[128 * (1 + x)];
    c[0] = 0x02;
    c[1] = 0x03;
    unsigned at = 4;
    for (const auto& b : blocks)
      for (uint8_t v : b)
        if (at < 127)
          c[at++] = v;
    c[2] = static_cast<uint8_t>(at);
  }
  return e;
}

static std::vector<uint8_t> Vsdb(uint8_t ab, uint8_t cd)
{
  return {0x65, 0x03, 0x0c, 0x00, ab, cd, 0x00};
}

static std::string Say(const EdidHdmiPath& p)
{
  return "between " + std::to_string(p.Between()) + ", address '" + p.AddressText() + "', sound '" + p.AudioText() + "'";
}

int main(int argc, char** argv)
{
  if (argc < 4)
  {
    std::printf("usage: edidpath_test <c5 direct> <c5 through a sound bar> <receiver>... -- <tv>...\n");
    return 2;
  }
  // ---- the LG C5, direct and through the TCL Q85H Pro (the same TV, its EDID read both ways)
  const EdidHdmiPath direct = EdidHdmiPathOf(Read(argv[1]));
  std::printf("c5 direct: %s\n", Say(direct).c_str());
  Check(direct.AddressText() == "4.0.0.0" && direct.Between() == 0, "the C5 read direct is on input 4 of the TV");
  Check(direct.AudioText() == "TrueHD, DD+, PCM 8 ch", "the C5 read direct declares no DTS");
  const EdidHdmiPath bar = EdidHdmiPathOf(Read(argv[2]));
  std::printf("c5 through the bar: %s\n", Say(bar).c_str());
  Check(bar.AddressText() == "2.2.0.0" && bar.Between() == 1, "the C5 through the sound bar is behind one device");
  Check(bar.Declares(EdidHdmiPath::DTS) && bar.Declares(EdidHdmiPath::DTSHD),
        "through the sound bar the EDID declares DTS and DTS-HD");
  Check(bar.AudioText() == "TrueHD, DD+, DTS-HD, DTS, PCM 8 ch", "the sound of the bar's EDID in words");

  // ---- EDIDs of AV receivers as their sources read them: behind one device; and of TVs: direct
  int i = 3;
  int receivers = 0;
  for (; i < argc && std::string(argv[i]) != "--"; ++i, ++receivers)
  {
    const EdidHdmiPath p = EdidHdmiPathOf(Read(argv[i]));
    std::printf("receiver %s: %s\n", argv[i], Say(p).c_str());
    Check(p.Between() == 1, std::string("a receiver's EDID is behind one device: ") + argv[i]);
  }
  int tvs = 0;
  for (++i; i < argc; ++i, ++tvs)
  {
    const EdidHdmiPath p = EdidHdmiPathOf(Read(argv[i]));
    std::printf("tv %s: %s\n", argv[i], Say(p).c_str());
    Check(p.Between() == 0, std::string("a TV's EDID is direct: ") + argv[i]);
  }
  Check(receivers >= 1 && tvs >= 1, "receivers and TVs were given");

  // ---- made byte by byte
  struct Case
  {
    const char* what;
    std::vector<uint8_t> edid;
    int between;
    const char* address;
  };
  const Case cases[] = {
      {"input 1 of the TV", Edid({Vsdb(0x10, 0x00)}), 0, "1.0.0.0"},
      {"input 3 of a receiver on input 2", Edid({Vsdb(0x23, 0x00)}), 1, "2.3.0.0"},
      {"two devices", Edid({Vsdb(0x21, 0x40)}), 2, "2.1.4.0"},
      {"three devices", Edid({Vsdb(0x21, 0x43)}), 3, "2.1.4.3"},
      {"no address (F.F.F.F, which HDMI writes for none)", Edid({Vsdb(0xff, 0xff)}), -1, ""},
      {"the TV's own address (0.0.0.0)", Edid({Vsdb(0x00, 0x00)}), -1, "0.0.0.0"},
      {"a nibble after a 0", Edid({Vsdb(0x20, 0x10)}), -1, "2.0.1.0"},
      {"a nibble after a 0, last", Edid({Vsdb(0x10, 0x01)}), -1, "1.0.0.1"},
      {"no HDMI VSDB (a DisplayPort monitor)", Edid({{0x23, 0x09, 0x07, 0x07}}), -1, ""},
      {"an HDMI VSDB too short for an address", Edid({{0x64, 0x03, 0x0c, 0x00, 0x10}}), -1, ""},
      {"another vendor's block first", Edid({{0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x80, 0x00}, Vsdb(0x31, 0x00)}), 1,
       "3.1.0.0"},
      {"the first HDMI VSDB of two", Edid({Vsdb(0x30, 0x00), Vsdb(0x31, 0x00)}), 0, "3.0.0.0"},
      {"no extension", Edid({}, 0), -1, ""},
  };
  for (const Case& c : cases)
  {
    const EdidHdmiPath p = EdidHdmiPathOf(c.edid);
    Check(p.Between() == c.between && p.AddressText() == c.address && p.Valid() == (c.between >= 0),
          std::string(c.what) + ": " + Say(p) + ", expected between " + std::to_string(c.between) + " at '" +
              c.address + "'");
  }
  // the sound: LPCM of 8 channels, AC-3, DTS, DD+ (code 10), TrueHD (MAT, code 12); in an audio block of its own
  {
    std::vector<uint8_t> audio = {static_cast<uint8_t>(0x20 | 15)};
    audio.insert(audio.end(), {0x0f, 0x7f, 0x07, 0x15, 0x07, 0x50, 0x3e, 0x06, 0xc0, 0x57, 0x06, 0x00, 0x67, 0x54, 0x00});
    const EdidHdmiPath p = EdidHdmiPathOf(Edid({audio, Vsdb(0x40, 0x00)}));
    Check(p.AudioText() == "TrueHD, DD+, DTS, PCM 8 ch", "the formats of an audio data block: '" + p.AudioText() + "'");
    Check(p.pcmChannels == 8, "8 channels of LPCM");
  }
  // an EDID that declares no sound: nothing said
  Check(EdidHdmiPathOf(Edid({Vsdb(0x10, 0x00)})).AudioText().empty(), "no audio data block: no formats");
  // a block whose length runs past the extension: nothing read past its 128 bytes
  {
    std::vector<uint8_t> e = Edid({});
    uint8_t* c = &e[128];
    c[2] = 127;
    c[124] = 0x7f; // a vendor block of 31 bytes at the end: past the block
    c[125] = 0x03;
    c[126] = 0x0c;
    c[127] = 0x00;
    const EdidHdmiPath p = EdidHdmiPathOf(e);
    Check(p.address == EdidHdmiPath::NONE, "a block cut by the end of the extension gives no address");
  }
  // the audio blocks of a second CTA extension count too
  {
    std::vector<uint8_t> e = Edid({Vsdb(0x10, 0x00)}, 2);
    uint8_t* c = &e[256];
    c[4] = 0x23;
    c[5] = 0x3d;
    c[6] = 0x07;
    c[7] = 0x50;
    c[2] = 8;
    const EdidHdmiPath p = EdidHdmiPathOf(e);
    Check(p.Declares(EdidHdmiPath::DTS), "a second CTA extension: its DTS counts");
  }
  std::printf("EDIDPATH: %d checks, %d failed, %d receivers, %d TVs\n", checks, fails, receivers, tvs);
  if (fails)
    return 1;
  std::printf("EDIDPATH-TEST PASSED\n");
  return 0;
}
