// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// What the player information says when the second view of a 3D video is not shown (xbmc/utils/HadesPlayerInfo.h,
// SummaryInput::secondView): "slow" (the processor did not keep up and the video went back to 2D) is a warning
// whose remedy is the system; "dvipt" (Dolby Vision profile 20 kept to one view) a note with nothing to do; nothing
// otherwise. The skin shows them by the code second_view and its argument (strings 54350 and 54351 of Estuary).
// usage: mvhevc_note_test      Prints FAIL lines and "N checks, M failed".
#include "HadesPlayerInfo.h"

#include <cstdio>

using namespace KODI::UTILS::PLAYERINFO;

static int checks = 0, failed = 0;
#define CHECK(cond, what) \
  do \
  { \
    ++checks; \
    if (!(cond)) \
    { \
      ++failed; \
      std::printf("FAIL: %s\n", what); \
    } \
  } while (0)

static SummaryInput Playing(const std::string& secondView)
{
  SummaryInput in;
  in.video = true;
  in.source = "sdr";
  in.signal = "sdr";
  in.secondView = secondView;
  return in;
}

int main()
{
  {
    const Summary s = Decide(Playing("slow"));
    CHECK(s.state == Severity::WARN && s.code == "second_view" && s.arg == "slow" && s.todo == "system",
          "back to 2D, the processor too slow: a warning, the system to change");
  }
  {
    const Summary s = Decide(Playing("dvipt"));
    CHECK(s.state == Severity::NOTE && s.code == "second_view" && s.arg == "dvipt" && s.todo.empty(),
          "Dolby Vision profile 20 kept to one view: a note, nothing to do");
  }
  {
    const Summary s = Decide(Playing(""));
    CHECK(s.state == Severity::OK && s.code == "ok", "a 2D video: nothing said");
    const Summary t = Decide(Playing("other"));
    CHECK(t.code != "second_view", "an unknown reason is not said");
  }
  {
    // a warning of the picture comes first; the second view is one more thing to say
    SummaryInput in = Playing("slow");
    in.stepLevel = 1;
    const Summary s = Decide(in);
    CHECK(s.code == "step_down" && s.more == 1, "the step-down first, the second view counted");
    SummaryInput n = Playing("dvipt");
    n.source = "dolbyvision";
    n.dvState = "hdr10";
    n.signal = "pq";
    const Summary d = Decide(n);
    CHECK(d.code == "dv_not_native" && d.more == 1, "Dolby Vision as HDR10 first, the one view counted");
  }
  {
    SummaryInput in = Playing("slow");
    in.video = false;
    CHECK(Decide(in).code == "ok", "nothing said without a video");
  }
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
