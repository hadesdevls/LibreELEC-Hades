/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades: host test of GL_GPU_DISJOINT_EXT read once a frame and told to every reader of GPU times
 * (xbmc/windowing/gbm/GpuDisjoint.h): the state of the GL (a fake here: what each read gives, and how many reads were
 * made) is read by the first reader of a frame alone; every reader learns of every disjoint period since it last
 * asked, whatever frame it asks in and in whatever order; a reader that has just made its timers takes no period of
 * before. With the rule before (each reader read and cleared the state itself), the second reader of a frame never
 * learnt of a period that the first had taken. Built with -I <Kodi tree>/xbmc/windowing/gbm; each failure prints
 * FAIL; the last line says GPU-DISJOINT PASSED or FAILED.
 */
#include "GpuDisjoint.h"

#include <cstdio>
#include <string>
#include <vector>

using KODI::WINDOWING::GBM::CGpuDisjoint;

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

// the GL: the disjoint state is set by the GPU and cleared by a read
struct FakeGl
{
  bool disjoint = false;
  int reads = 0;
  bool Read()
  {
    ++reads;
    const bool was = disjoint;
    disjoint = false;
    return was;
  }
};
} // namespace

int main()
{
  {
    // five readers, as the render thread has them: native Dolby Vision, its passes, Dolby Vision rebuilt, HDR10
    // adapted, Kodi's renderer
    CGpuDisjoint d;
    FakeGl gl;
    CGpuDisjoint::Reader dv, passes, conv, adapt, chroma;
    auto read = [&gl] { return gl.Read(); };
    // a frame without a disjoint period: nobody lost anything, and the GL was read once
    d.Frame();
    Check(!d.Lost(dv, read) && !d.Lost(passes, read) && !d.Lost(chroma, read), "no period: nothing lost");
    Check(gl.reads == 1, "read once in a frame (" + std::to_string(gl.reads) + ")");
    // the GPU loses its time: every reader of the next frame learns of it, not only the first
    gl.disjoint = true;
    d.Frame();
    const bool a = d.Lost(dv, read), b = d.Lost(passes, read), c = d.Lost(conv, read), e = d.Lost(chroma, read);
    Check(a && b && c && e, "every reader of the frame learns of the period (the second one too)");
    Check(gl.reads == 2, "still once a frame (" + std::to_string(gl.reads) + ")");
    // asked again in the same frame: nothing new
    Check(!d.Lost(dv, read) && !d.Lost(chroma, read), "asked again in the frame: nothing new");
    // a reader that did not ask in that frame learns of it later, after frames without a period
    d.Frame();
    d.Frame();
    Check(d.Lost(adapt, read), "a reader that did not ask then learns of it in a later frame");
    Check(!d.Lost(adapt, read), "... once");
    Check(!d.Lost(dv, read), "the others not again");
    // in another order: the renderer first
    gl.disjoint = true;
    d.Frame();
    Check(d.Lost(chroma, read) && d.Lost(dv, read), "the renderer first: both learn of it");
    Check(d.Periods() == 2, "two periods counted (" + std::to_string(d.Periods()) + ")");
    // a period that comes between two frames while nobody reads is not lost: the next read takes it
    gl.disjoint = true;
    d.Frame();
    d.Frame();
    Check(d.Lost(conv, read), "a period with frames that nobody read is taken by the next read");
    Check(d.Lost(passes, read), "... and told to the other readers too");
  }
  {
    // a reader that makes its timers after periods were found takes none of them
    CGpuDisjoint d;
    FakeGl gl;
    auto read = [&gl] { return gl.Read(); };
    CGpuDisjoint::Reader first, late;
    gl.disjoint = true;
    d.Frame();
    Check(d.Lost(first, read), "the first reader takes the period");
    d.Join(late);
    Check(!d.Lost(late, read), "a reader that joined after it takes no period of before");
    gl.disjoint = true;
    d.Frame();
    Check(d.Lost(late, read) && d.Lost(first, read), "and takes the next one, as the others");
  }
  {
    // many frames, a period now and then: each reader is told of each period exactly once
    CGpuDisjoint d;
    FakeGl gl;
    auto read = [&gl] { return gl.Read(); };
    std::vector<CGpuDisjoint::Reader> readers(5);
    std::vector<int> told(5, 0);
    int periods = 0;
    for (int frame = 0; frame < 1000; ++frame)
    {
      d.Frame();
      if (frame % 37 == 5)
      {
        gl.disjoint = true;
        ++periods;
      }
      // the readers ask in an order that turns, and one of them only every third frame
      for (int i = 0; i < 5; ++i)
      {
        const int r = (i + frame) % 5;
        if (r == 4 && frame % 3)
          continue;
        told[r] += d.Lost(readers[r], read) ? 1 : 0;
      }
    }
    for (int r = 0; r < 4; ++r)
      Check(told[r] == periods, "reader " + std::to_string(r) + " told of " + std::to_string(told[r]) + " of " +
                                    std::to_string(periods) + " periods");
    Check(told[4] == periods, "the reader that asks every third frame is told of each period once (" +
                                  std::to_string(told[4]) + " of " + std::to_string(periods) + ")");
    Check(gl.reads == 1000, "the GL read once a frame: " + std::to_string(gl.reads) + " reads in 1000 frames");
  }
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  std::printf("%s\n", g_failures ? "GPU-DISJOINT FAILED" : "GPU-DISJOINT PASSED");
  return g_failures ? 1 : 0;
}
