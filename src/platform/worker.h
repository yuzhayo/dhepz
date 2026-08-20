// Run-once jobs off the UI thread, with completions posted back to it.
//
// This layer is the answer to the constraint pair that defines it: G1 says
// **no threads alive at idle** and G2 says **the UI thread never blocks**.
// Both at once rules out a persistent pool sitting on a condition variable,
// so jobs are spawned per job and joined, and a job's result travels back as
// a *message to the UI window*, never as a direct touch of UI state. There
// are no locks around UI data because there is no shared UI data.
//
// Two delivery guards, both pinned by tests:
//
//   - Every completion carries the requester's **generation** (epoch). A
//     result whose generation was invalidated — its window closed, its route
//     changed — is dropped before it is posted, and the receiver re-checks
//     on arrival, so a stale result can never land on a fresh tree.
//   - A cancelled job stops as soon as its work observes the flag, and its
//     completion is discarded even if the work already returned.
//
// Coalescing for rapid repeats (typing in a folder box): SubmitCoalesced
// keeps only the latest request per key and lets it settle through a quiet
// period inside the serving thread. The quiet wait exists only while a
// request is pending; nothing is armed at idle, so G1 still holds.
//
// Like all of platform/, this layer keeps windows.h out of the header.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace worker {

// Work runs on a worker thread. It receives the job's cancel flag and is
// expected to poll it at its safe points; returning early on cancel is the
// job's own business. The returned cargo is handed to Deliver on the UI
// thread. `std::shared_ptr<void>` keeps this header free of the caller's
// result type.
using Work = std::function<std::shared_ptr<void>(const std::atomic<bool>& cancelled)>;
using Deliver = std::function<void(std::shared_ptr<void> cargo)>;

// A submitted job's only handles: its cancel flag and the generation its
// completion is stamped with.
struct JobHandle {
  std::shared_ptr<std::atomic<bool>> cancel;
  std::uint64_t generation = 0;
};

class Worker final {
 public:
  // Bind to the window completions are posted to, and the message used for
  // them (RegisterWindowMessage-style ids work well). The window's procedure
  // must route that message to Settle().
  Worker(void* ui_window, unsigned int completion_message);
  ~Worker();

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  // Spawns one thread for this job. generation 0 means "no epoch guard".
  JobHandle Submit(Work work, Deliver deliver, std::uint64_t generation = 0);

  // Latest-wins per key. Submitting again while a request for the same key
  // is settling replaces it; the work runs once per quiet period at most.
  JobHandle SubmitCoalesced(std::wstring_view key, Work work, Deliver deliver,
                            std::uint64_t generation = 0,
                            std::chrono::milliseconds quiet = std::chrono::milliseconds(30));

  // Cancels one job: its flag is raised, and its completion is discarded
  // even if the work has already finished.
  void Cancel(const JobHandle& handle);

  // Epochs. Create on mount, invalidate on close/route-change, pass to
  // Submit, and re-check with Alive() when settling — that second check is
  // the authoritative one.
  std::uint64_t CreateGeneration();
  void InvalidateGeneration(std::uint64_t generation);
  bool GenerationAlive(std::uint64_t generation) const;

  // Joins every thread that has finished, releasing its handle. Cheap; call
  // it any time a handle count matters. Not called from any idle path.
  void JoinFinished();

  // Worker threads currently tracked (finished-but-unjoined ones included
  // until JoinFinished runs). Zero is the idle state.
  std::size_t ThreadCount() const;

  // Invalidates every generation, cancels every coalescing slot, and joins
  // every thread. Clean, no terminate, no leaked handle. Idempotent; also
  // runs from the destructor. After it, Submit is a no-op.
  void Shutdown();

  // What the UI window's procedure calls for the completion message. Runs
  // the delivery if the generation is still alive, drops it otherwise, and
  // always frees the payload.
  static void Settle(long long lparam);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace worker
