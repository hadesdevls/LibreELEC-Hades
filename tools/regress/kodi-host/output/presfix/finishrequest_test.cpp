/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): the queue of atomic requests of CDRMAtomic
 * after a commit whose test the driver refused and whose fallback it took.
 *
 * With the tree's windowing/gbm/drm/DRMRequestQueue.h (FinishAtomicRequest(), which
 * CDRMAtomic::FinishRequest() calls), the real policy is driven. Without it (the code before the fix), the code
 * of the code before the fix is transcribed (DRMAtomic.cpp:336-340: pop_front() whenever a commit is taken), so
 * that the test is seen to fail there.
 *
 * The rest of CDRMAtomic::DrmAtomicCommit() (DRMAtomic.cpp:161-176, 230-287) is transcribed: a
 * request gets the framebuffer of the frame and, with a modeset, a MODE_ID blob made for that
 * commit; when the driver refuses its test, the commit is made of the front of the queue with the
 * framebuffer of the frame. the code before the fix destroys the blob when the commit returns (a local
 * CDRMPropertyBlob); the tree keeps it with its request as well (KeepModeBlob()). The kernel keeps
 * a blob alive while the state of the CRTC names it, and gives the id of a freed blob to the next
 * one that is made (idr): both modelled.
 *
 * The driver refuses any request that carries property BAD (a value it does not take: HDR
 * metadata, a vendor InfoFrame), a mode it cannot carry (named X), and a MODE_ID that names no
 * blob or another mode than the one it was made for.
 */
#if __has_include("windowing/gbm/drm/DRMRequestQueue.h")
#include "windowing/gbm/drm/DRMRequestQueue.h"
#define HAVE_FIX 1
#else
#define HAVE_FIX 0
#endif

#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

// The kernel's blobs, by id: the mode each holds, and its references (the user's handle, and the
// state of the CRTC when it names it)
struct Kernel
{
  struct Entry
  {
    std::string mode;
    int refs = 0;
  };
  std::map<int, Entry> blobs;
  int stateBlob = 0;
  int Create(const std::string& mode)
  {
    int id = 1;
    while (blobs.count(id))
      ++id;
    blobs[id] = {mode, 1};
    return id;
  }
  void Unref(int id)
  {
    auto b = blobs.find(id);
    if (b != blobs.end() && --b->second.refs == 0)
      blobs.erase(b);
  }
  void SetState(int id)
  {
    if (id == stateBlob)
      return;
    if (id)
      blobs[id].refs++;
    if (stateBlob)
      Unref(stateBlob);
    stateBlob = id;
  }
  std::string ModeOnScreen() const
  {
    const auto b = blobs.find(stateBlob);
    return b == blobs.end() ? "" : b->second.mode;
  }
};

struct Blob
{
  Kernel* kernel = nullptr;
  int id = 0;
  Blob() = default;
  Blob(Kernel& k, const std::string& mode) : kernel(&k), id(k.Create(mode)) {}
  Blob(const Blob&) = delete;
  Blob& operator=(Blob&& o) noexcept
  {
    Reset();
    kernel = o.kernel;
    id = o.id;
    o.id = 0;
    return *this;
  }
  ~Blob() { Reset(); }
  void Reset()
  {
    if (kernel && id)
      kernel->Unref(id);
    id = 0;
  }
};

struct Request
{
  std::set<std::string> props;
  int modeId = 0; //!< the MODE_ID it carries (0: none)
  std::string modeName; //!< the mode it was made for
  Blob blob; //!< the tree: KeepModeBlob()
};

struct Atomic
{
  bool keepBlob;
  Kernel kernel;
  std::deque<std::unique_ptr<Request>> queue;
  Request* req = nullptr;
  std::set<std::string> committed;
  int refusedTests = 0;
  int droppedFrames = 0;

  explicit Atomic(bool keep) : keepBlob(keep)
  {
    queue.emplace_back(std::make_unique<Request>());
    req = queue.back().get();
  }
  bool DriverTakes(const Request& r) const
  {
    if (r.props.count("BAD"))
      return false;
    if (r.modeId)
    {
      const auto blob = kernel.blobs.find(r.modeId);
      if (blob == kernel.blobs.end() || blob->second.mode != r.modeName || r.modeName == "X")
        return false;
    }
    return true;
  }
  void Finish(bool accepted)
  {
    if (accepted)
      committed = req->props;
#if HAVE_FIX
    KODI::WINDOWING::GBM::FinishAtomicRequest(queue, req, accepted);
#else
    if (accepted)
    {
      if (queue.size() > 1)
        queue.pop_front();
    }
    else if (!queue.empty())
      queue.pop_back();
#endif
    queue.emplace_back(std::make_unique<Request>());
    req = queue.back().get();
  }
  //! a frame of the ordinary output, with a modeset to mode when it is given
  void Flip(std::initializer_list<std::string> added, const std::string& fb,
            const std::string& mode = "")
  {
    Blob local; // CDRMPropertyBlob modeBlob, at function scope
    for (const auto& p : added)
      req->props.insert(p);
    if (!mode.empty())
    {
      local = Blob(kernel, mode);
      req->modeId = local.id;
      req->modeName = mode;
      if (keepBlob)
        req->blob = std::move(local);
    }
    req->props.insert(fb);
    if (!DriverTakes(*req))
    {
      ++refusedTests;
      req = queue.front().get();
      req->props.insert(fb); // "update the old atomic request with the new fb id"
    }
    if (!DriverTakes(*req))
    {
      ++droppedFrames;
      Finish(false);
      return;
    }
    if (req->modeId)
      kernel.SetState(req->modeId);
    Finish(true);
  }
};

static void Scenario(bool keepBlob)
{
  const std::string v = std::string(HAVE_FIX ? " (tree" : " (the code before the fix") +
                        (keepBlob ? ", blob kept with its request)" : ", blob destroyed)");
  {
    // the reviewer's case (finishrequest_model)
    Atomic drm(keepBlob);
    drm.Flip({"HDR=a"}, "fb1", "A");
    drm.Flip({}, "fb2");
    drm.Flip({"BAD"}, "fb3"); // test refused; fallback to the last good one, with fb3: taken
    Check(drm.refusedTests == 1 && drm.droppedFrames == 0 && drm.committed.count("fb3"),
          "a refused property: the frame goes out with the request before it" + v);
    Check(drm.DriverTakes(*drm.queue.front()),
          "after that, the request to fall back to is one that the driver takes" + v);
    drm.Flip({"BAD"}, "fb4");
    Check(drm.droppedFrames == 0,
          "the next refused test falls back to a request that the driver takes: the frame is "
          "shown" + v);
  }
  {
    // a change of mode that the link cannot carry, refused: the frame goes out in the mode on the
    // screen, with the modeset that set it; blobs made and destroyed meanwhile (HDR metadata,
    // InfoFrames) take the freed ids; the next refused test still falls back to that mode
    Atomic m(keepBlob);
    m.Flip({}, "fb1", "A");
    m.Flip({}, "fb2", "X");
    Check(m.droppedFrames == 0 && m.kernel.ModeOnScreen() == "A",
          "a refused change of mode: the frame goes out in the mode on the screen" + v);
    {
      Blob other(m.kernel, "C");
    }
    Blob reused(m.kernel, "C");
    m.Flip({"BAD"}, "fb3");
    Check(m.droppedFrames == 0 && m.kernel.ModeOnScreen() == "A",
          "a later refused test falls back to the request of the mode on the screen" + v);
  }
}

int main()
{
  Scenario(false);
  Scenario(true);
  std::printf(g_failures ? "FINISHREQUEST-TEST FAILED (%d)\n" : "FINISHREQUEST-TEST PASSED\n",
              g_failures);
  return g_failures ? 1 : 0;
}
