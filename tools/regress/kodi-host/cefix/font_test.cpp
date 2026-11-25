// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// font_test.cpp <noto sans .ttf> <noto mono .ttf>: GetFontFamilyNames(buffer, names) of FontUtils.cpp (font_gen.py)
// with FreeType, on TrueType collections made here of the two fonts of Estuary: a face that does not load (a font
// directory of garbage) between them, first, and a face without a name table. The faces after or beside a broken one
// give their names, and the function says it found something only with a name. One line per check, "ok:" or "FAIL:",
// then FONT-TEST PASSED or FONT-TEST FAILED.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace KODI::UTILS::FONT
{
bool GetFontFamilyNames(const std::vector<uint8_t>& buffer, std::set<std::string>& familyNames);
}

namespace
{
using Bytes = std::vector<uint8_t>;
int fails = 0;

void Check(bool ok, const std::string& what)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok)
    ++fails;
}

Bytes Load(const char* path)
{
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), {});
}

uint32_t Be32(const Bytes& b, size_t at)
{
  return (uint32_t{b[at]} << 24) | (uint32_t{b[at + 1]} << 16) | (uint32_t{b[at + 2]} << 8) | b[at + 3];
}

void PutBe32(Bytes& b, size_t at, uint32_t v)
{
  b[at] = v >> 24;
  b[at + 1] = v >> 16;
  b[at + 2] = v >> 8;
  b[at + 3] = v;
}

// A face of a collection: a whole TrueType font, or garbage where its directory should be
struct Face
{
  Bytes font;
  bool broken = false;
  bool nameless = false; // its 'name' table renamed, so that the face has no family name
};

//! A TrueType collection (ttcf 1.0) of the faces: each font whole at its own place, the offsets of its tables made
//! absolute by that place
Bytes Collection(const std::vector<Face>& faces)
{
  Bytes out(12 + 4 * faces.size(), 0);
  out[0] = 't';
  out[1] = 't';
  out[2] = 'c';
  out[3] = 'f';
  PutBe32(out, 4, 0x00010000);
  PutBe32(out, 8, static_cast<uint32_t>(faces.size()));
  for (size_t i = 0; i < faces.size(); ++i)
  {
    while (out.size() % 4)
      out.push_back(0);
    const size_t at = out.size();
    PutBe32(out, 12 + 4 * i, static_cast<uint32_t>(at));
    if (faces[i].broken)
    {
      out.insert(out.end(), 64, 0xFF);
      continue;
    }
    Bytes f = faces[i].font;
    const unsigned int tables = (f[4] << 8) | f[5];
    for (unsigned int t = 0; t < tables; ++t)
    {
      const size_t rec = 12 + 16 * t;
      PutBe32(f, rec + 8, Be32(f, rec + 8) + static_cast<uint32_t>(at));
      if (faces[i].nameless && f[rec] == 'n' && f[rec + 1] == 'a' && f[rec + 2] == 'm' && f[rec + 3] == 'e')
        f[rec] = 'x';
    }
    out.insert(out.end(), f.begin(), f.end());
  }
  return out;
}

std::string Names(const std::set<std::string>& names)
{
  std::string s;
  for (const auto& n : names)
    s += (s.empty() ? "" : ", ") + n;
  return "{" + s + "}";
}

void Expect(const std::string& what, const Bytes& file, bool wantFound, const std::set<std::string>& want)
{
  std::set<std::string> names;
  const bool found = KODI::UTILS::FONT::GetFontFamilyNames(file, names);
  Check(found == wantFound && names == want,
        what + ": " + (found ? "found " : "not found ") + Names(names) + ", " +
            (wantFound ? "found " : "not found ") + Names(want) + " expected");
}
} // namespace

int main(int argc, char** argv)
{
  if (argc != 3)
    return 2;
  const Bytes sans = Load(argv[1]);
  const Bytes mono = Load(argv[2]);
  std::set<std::string> a, b;
  const bool fa = KODI::UTILS::FONT::GetFontFamilyNames(sans, a);
  const bool fb = KODI::UTILS::FONT::GetFontFamilyNames(mono, b);
  Check(fa && a.size() == 1 && fb && b.size() == 1 && a != b,
        "the two fonts alone: " + Names(a) + " and " + Names(b));
  if (fails)
  {
    std::printf("FONT-TEST FAILED\n");
    return 1;
  }
  const std::string nameA = *a.begin(), nameB = *b.begin();

  Expect("a collection of the two", Collection({{sans}, {mono}}), true, {nameA, nameB});
  Expect("a face that does not load between them", Collection({{sans}, {{}, true}, {mono}}), true, {nameA, nameB});
  Expect("a face that does not load first", Collection({{{}, true}, {mono}}), true, {nameB});
  Expect("a face that does not load last", Collection({{sans}, {{}, true}}), true, {nameA});
  Expect("a face without a name after one with", Collection({{sans}, {mono, false, true}}), true, {nameA});
  Expect("a face without a name before one with", Collection({{mono, false, true}, {sans}}), true, {nameA});
  Expect("only faces that do not load", Collection({{{}, true}, {{}, true}}), false, {});
  Expect("not a font", Bytes(4096, 0x5A), false, {});

  std::printf(fails ? "FONT-TEST FAILED\n" : "FONT-TEST PASSED\n");
  return fails ? 1 : 0;
}
