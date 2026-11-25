// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// projector_cli: the protocols of the projector control of Hades (xbmc/network/ProjectorLink.cpp, built as it is)
// driven from the command line, against the simulators of projector_link_test.py.
// usage: projector_cli <jvc|adcp|sdcp|epson> <port> <password|-> [option|op]...
//   options: hash (JVC: the hashed password first), lens-ms=N, mode-ms=N, answer-ms=N, gap-ms=N
//   ops:     open, power, model, lens=N, mode=N (decimal), close
// Every line of the session's log is printed as "LOG <line>", every result as "RESULT <op> <failure> <detail>",
// and at the end "HASHED yes|no". The SHA-256 here is the test's own (FIPS 180-4), which the simulators check
// against Python's hashlib: Kodi gives its own (CDigest).
#include "ProjectorLink.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
uint32_t Rot(uint32_t x, int n)
{
  return (x >> n) | (x << (32 - n));
}

std::string Sha256Hex(const std::string& data)
{
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string m = data;
  const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  m += static_cast<char>(0x80);
  while (m.size() % 64 != 56)
    m += '\0';
  for (int i = 7; i >= 0; i--)
    m += static_cast<char>((bits >> (8 * i)) & 0xFF);
  for (size_t chunk = 0; chunk < m.size(); chunk += 64)
  {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t(uint8_t(m[chunk + 4 * i])) << 24) | (uint32_t(uint8_t(m[chunk + 4 * i + 1])) << 16) |
             (uint32_t(uint8_t(m[chunk + 4 * i + 2])) << 8) | uint32_t(uint8_t(m[chunk + 4 * i + 3]));
    for (int i = 16; i < 64; i++)
    {
      const uint32_t s0 = Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++)
    {
      const uint32_t t1 = k + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      const uint32_t t2 = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      k = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
  }
  char out[65];
  for (int i = 0; i < 8; i++)
    std::snprintf(out + 8 * i, 9, "%08x", h[i]);
  return std::string(out, 64);
}

void Say(const char* op, const PROJECTOR::Result& r, const std::string& value = "")
{
  std::printf("RESULT %s %s%s%s\n", op, PROJECTOR::FailureName(r.failure), r.detail.empty() ? "" : " | ",
              r.detail.c_str());
  if (r.Ok() && !value.empty())
    std::printf("VALUE %s %s\n", op, value.c_str());
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 4)
  {
    std::fprintf(stderr, "usage: projector_cli <jvc|adcp|sdcp|epson> <port> <password|-> [option|op]...\n");
    return 2;
  }
  const std::string kind = argv[1];
  const PROJECTOR::Brand brand = kind == "jvc"    ? PROJECTOR::Brand::JVC
                                 : kind == "adcp" ? PROJECTOR::Brand::SONY_ADCP
                                 : kind == "sdcp" ? PROJECTOR::Brand::SONY_SDCP
                                 : kind == "epson" ? PROJECTOR::Brand::EPSON
                                                   : PROJECTOR::Brand::NONE;
  const auto port = static_cast<uint16_t>(std::atoi(argv[2]));
  const std::string password = std::strcmp(argv[3], "-") == 0 ? "" : argv[3];
  PROJECTOR::Timeouts timeouts;
  timeouts.gapMs = 50;
  bool hash = false;
  for (int i = 4; i < argc; i++)
  {
    const std::string a = argv[i];
    const auto number = [&a](const char* key) { return std::atoi(a.c_str() + std::strlen(key)); };
    if (a == "hash")
      hash = true;
    else if (a.rfind("lens-ms=", 0) == 0)
      timeouts.lensMs = number("lens-ms=");
    else if (a.rfind("mode-ms=", 0) == 0)
      timeouts.modeMs = number("mode-ms=");
    else if (a.rfind("answer-ms=", 0) == 0)
      timeouts.answerMs = number("answer-ms=");
    else if (a.rfind("gap-ms=", 0) == 0)
      timeouts.gapMs = number("gap-ms=");
  }
  std::atomic<bool> stop{false};
  PROJECTOR::CSession session(
      brand, "127.0.0.1", port, password, Sha256Hex,
      [](const std::string& line) { std::printf("LOG %s\n", line.c_str()); }, &stop, timeouts);
  session.PreferHashedPassword(hash);
  for (int i = 4; i < argc; i++)
  {
    const std::string a = argv[i];
    if (a == "open")
      Say("open", session.Open());
    else if (a == "power")
    {
      PROJECTOR::Power power = PROJECTOR::Power::UNKNOWN;
      const PROJECTOR::Result r = session.GetPower(power);
      Say("power", r, PROJECTOR::PowerName(power));
    }
    else if (a == "model")
    {
      std::string model;
      const PROJECTOR::Result r = session.GetModel(model);
      Say("model", r, model);
    }
    else if (a.rfind("lens=", 0) == 0)
      Say("lens", session.SetLensMemory(std::atoi(a.c_str() + 5)));
    else if (a.rfind("mode=", 0) == 0)
      Say("mode", session.SetPictureMode(std::atoi(a.c_str() + 5)));
    else if (a == "close")
      session.Close();
    std::fflush(stdout);
  }
  session.Close();
  std::printf("HASHED %s\n", session.UsedHashedPassword() ? "yes" : "no");
  std::printf("SHA256-SELFTEST %s\n", Sha256Hex("abc").c_str());
  return 0;
}
