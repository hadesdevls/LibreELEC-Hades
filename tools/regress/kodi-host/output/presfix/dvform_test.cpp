// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: the form of the Dolby Vision metadata and of the AVI InfoFrame, to compare with what licensed
// players send: CDVEngineSettings::Resolve() of its settings (the level 5 block, the order of the blocks, level 254,
// the neutral level 3, BT.2020 in the AVI InfoFrame) and the defaults that keep Hades' own form, with the engine's
// defaults read as "all automatic"; and what the transport sets for BT.2020 in the AVI InfoFrame (TransportSpecs()):
// DVBRIDGE_DV_AVI_BT2020_LAB on the kernel's backends ("Colorspace" stays Default there), "Colorspace" BT2020_RGB on
// nvidia-drm, and neither where it is not asked for.
// Build: g++ -std=c++20 -Wall -Wextra -I <tree>/xbmc -I <tree>/xbmc/windowing/gbm dvform_test.cpp
#include "DVEngineSettings.h"
#include "drm/TransportSpecs.h"

#include <cstdio>
#include <string>
#include <vector>

using KODI::WINDOWING::GBM::CDVEngineSettings;
namespace OS = KODI::WINDOWING::GBM::OUTPUT_STATE;

static int failures = 0, checks = 0;

static void Check(const std::string& what, bool ok)
{
  ++checks;
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++failures;
}

// the spec of a property of the connector, or nullptr
static const OS::Spec* Find(const std::vector<OS::Spec>& specs, const char* name)
{
  for (const auto& s : specs)
    if (s.role == OS::Role::CONNECTOR && s.name == name)
      return &s;
  return nullptr;
}

static std::string Says(const OS::Spec* s)
{
  if (!s)
    return "(not set)";
  return (s->how == OS::Spec::How::ENUM_NAME ? s->enumName : std::to_string(s->number)) +
         (s->required ? ", required" : ", if there");
}

int main()
{
  using OS::TRANSPORT::Kind;
  using OS::TRANSPORT::TransportSpecs;
  // 1. the transport: the kernel's backends
  Kind k;
  auto specs = TransportSpecs(k);
  const OS::Spec* avi = Find(specs, OS::TRANSPORT::KERNEL_AVI_BT2020);
  const OS::Spec* cs = Find(specs, "Colorspace");
  Check("kernel, not asked: DVBRIDGE_DV_AVI_BT2020_LAB " + Says(avi) + " (want 0, if there); Colorspace " + Says(cs) +
            " (want Default, required)",
        avi && avi->how == OS::Spec::How::NUMBER && avi->number == 0 && !avi->required && cs &&
            cs->enumName == "Default" && cs->required);
  k.aviBt2020 = true;
  specs = TransportSpecs(k);
  avi = Find(specs, OS::TRANSPORT::KERNEL_AVI_BT2020);
  cs = Find(specs, "Colorspace");
  Check("kernel, asked: DVBRIDGE_DV_AVI_BT2020_LAB " + Says(avi) + " (want 1, if there); Colorspace " + Says(cs) +
            " (want Default: the pixels of the tunnel are not touched)",
        avi && avi->number == 1 && !avi->required && cs && cs->enumName == "Default");
  // ... nvidia-drm
  Kind n;
  n.nvidia = true;
  specs = TransportSpecs(n);
  cs = Find(specs, "Colorspace");
  Check("nvidia-drm, not asked: Colorspace " + Says(cs) + " (want Default, required); no kernel property: " +
            Says(Find(specs, OS::TRANSPORT::KERNEL_AVI_BT2020)),
        cs && cs->enumName == "Default" && cs->required && !Find(specs, OS::TRANSPORT::KERNEL_AVI_BT2020));
  n.aviBt2020 = true;
  specs = TransportSpecs(n);
  cs = Find(specs, "Colorspace");
  Check("nvidia-drm, asked: Colorspace " + Says(cs) + " (want BT2020_RGB, required)",
        cs && cs->enumName == "BT2020_RGB" && cs->required);

  // 2. the settings: the defaults keep Hades' own form, and read as all automatic
  const CDVEngineSettings::Read read;
  const CDVEngineSettings::Advanced none;
  const CDVEngineSettings s = CDVEngineSettings::Resolve(read, none);
  Check("the defaults, Hades 1.1's form: level 5 Hades' own, the blocks in the order of the video, level 254 and "
        "level 3 as the video, no BT.2020 in the AVI InfoFrame; the engine reads as all automatic: " +
            s.Describe(),
        s.l5Form == CDVEngineSettings::L5Form::PLACED && !s.levelOrder && !s.l254Zero && !s.l3Neutral &&
            !s.aviBt2020 && s.Automatic());
  const int values[] = {0, 1, 2, 3, 4, -1};
  const CDVEngineSettings::L5Form forms[] = {
      CDVEngineSettings::L5Form::PLACED, CDVEngineSettings::L5Form::AS_GIVEN, CDVEngineSettings::L5Form::NONE,
      CDVEngineSettings::L5Form::UNMADE, CDVEngineSettings::L5Form::PLACED, CDVEngineSettings::L5Form::PLACED};
  bool all = true;
  std::string got;
  for (int i = 0; i < 6; ++i)
  {
    CDVEngineSettings::Read r;
    r.l5Form = values[i];
    const auto e = CDVEngineSettings::Resolve(r, none);
    all = all && e.l5Form == forms[i];
    got += " " + std::to_string(static_cast<int>(e.l5Form));
  }
  Check("the level 5 setting 0 1 2 3, and two values that are none of the options, read as:" + got +
            " (want 0 1 2 3 0 0)",
        all);
  CDVEngineSettings::Read licensed;
  licensed.l5Form = 2;
  licensed.levelOrder = true;
  licensed.l254Zero = true;
  licensed.l3Neutral = true;
  licensed.aviBt2020 = true;
  const auto l = CDVEngineSettings::Resolve(licensed, none);
  const std::string d = l.Describe();
  Check("everything as licensed set-top boxes: " + d,
        l.l5Form == CDVEngineSettings::L5Form::NONE && l.levelOrder && l.l254Zero && l.l3Neutral && l.aviBt2020 &&
            !l.Automatic() && d.find("level 5 not sent (as licensed set-top boxes)") != std::string::npos &&
            d.find("level 254 with version 0") != std::string::npos &&
            d.find("a neutral level 3") != std::string::npos && d.find("says BT.2020") != std::string::npos);
  CDVEngineSettings::Read level;
  level.levelOrder = true;
  const auto v = CDVEngineSettings::Resolve(level, none);
  Check("the blocks in the order of their level: " + v.Describe(),
        v.levelOrder && !v.Automatic() &&
            v.Describe().find("the blocks in the order of their level") != std::string::npos);

  std::printf("%d checks, %d failed\nDVFORM-TEST %s\n", checks, failures, failures ? "FAILED" : "PASSED");
  return failures ? 1 : 0;
}
